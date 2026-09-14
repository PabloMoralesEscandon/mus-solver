#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfr/checkpoint.h"
#include "mus_solver/mus_four.h"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__,  \
                    #condition);                                                \
            exit(EXIT_FAILURE);                                                 \
        }                                                                       \
    } while (0)

static void test_state_contracts(void) {
    MusScoreTable two_table, four_table;
    MusScoreGame two, four;
    CHECK(mus_score_table_init(&two_table, 1) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_table_init(&four_table, 1) == CFR_STATUS_SUCCESS);
    four_table.four_player = true;
    CHECK(mus_score_game_init(&two, &two_table, 0, 0) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_init(&four, &four_table, 0, 0) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_matches(&two.game));
    CHECK(!mus_score_game_matches(&four.game));
    CHECK(!mus_four_score_game_matches(&two.game));
    CHECK(mus_four_score_game_matches(&four.game));
    CHECK(mus_score_game_matches_any(&two.game));
    CHECK(mus_score_game_matches_any(&four.game));
    CHECK(!mus_score_game_matches(NULL));
    CHECK(!mus_score_game_matches_any(NULL));
    CHECK(!mus_score_game_matches_any(mus_ordago_game_descriptor()));
    CHECK(mus_score_game_state_size(&two) == sizeof(MusGameState));
    CHECK(mus_score_game_state_size(&four) == sizeof(MusFourGameState));
    CHECK(mus_score_game_state_size(NULL) == 0);

    /* Descriptor rejection must precede any write to the supplied storage. */
    unsigned char *small = malloc(1);
    CHECK(small != NULL);
    *small = 0xa5;
    CHECK(mus_score_game_state_init((MusGameState *)small, &four) ==
          CFR_STATUS_INVALID_ARGUMENT);
    CHECK(*small == 0xa5);
    const MusScoreGame foreign = {.game = *mus_ordago_game_descriptor()};
    CHECK(mus_score_game_state_size(&foreign) == 0);
    CHECK(mus_score_game_state_init_generic((GameState *)small, &foreign) ==
          CFR_STATUS_INVALID_ARGUMENT);
    CHECK(*small == 0xa5);
    free(small);

    MusGameState *legacy = malloc(sizeof(*legacy));
    CHECK(legacy != NULL);
    CHECK(mus_score_game_state_init(legacy, &two) == CFR_STATUS_SUCCESS);
    CHECK(legacy->continuation == &two_table);
    CHECK(legacy->model.phase == MUS_PHASE_DEAL_PLAYER_0);
    free(legacy);

    const MusScoreGame *games[] = {&two, &four};
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); ++i) {
        GameState *state = malloc(mus_score_game_state_size(games[i]));
        CHECK(state != NULL);
        CHECK(mus_score_game_state_init_generic(state, games[i]) == CFR_STATUS_SUCCESS);
        CHECK(cfr_game_validate_state(&games[i]->game, state) == CFR_STATUS_SUCCESS);
        Actor actor;
        CHECK(cfr_game_current_actor(&games[i]->game, state, &actor) == CFR_STATUS_SUCCESS);
        CHECK(actor.kind == CFR_ACTOR_CHANCE);
        free(state);
    }
}

typedef struct {
    const MusScoreTable *source;
    const MusScoreTable *destination;
    FILE *checkpoint;
    size_t legacy_loads;
    size_t generic_loads;
    size_t recovery_calls;
    size_t saves;
} ExtensionTest;

static Status save_source(const MusScoreGame *game, const Trainer *trainer,
                           const MusScoreCell *cell, void *context) {
    ExtensionTest *test = context;
    CHECK(game->continuation == test->source);
    CHECK(cell->iterations == 2);
    CHECK(test->checkpoint == NULL);
    test->checkpoint = tmpfile();
    CHECK(test->checkpoint != NULL);
    return cfr_checkpoint_write(test->checkpoint, trainer);
}

/* This callback uses the historical signature and concrete state fields. */
static Status load_legacy(const MusScoreGame *game, MusGameState *state,
                          InfoStore *store, Trainer *trainer, void *context) {
    ExtensionTest *test = context;
    CHECK(mus_score_game_matches(&game->game));
    CHECK(!game->continuation->four_player);
    CHECK(state->continuation == test->source);
    CHECK(state->model.phase == MUS_PHASE_DEAL_PLAYER_0);
    CHECK(mus_score_game_state_init(state, game) == CFR_STATUS_SUCCESS);
    test->legacy_loads += 1;
    rewind(test->checkpoint);
    return cfr_checkpoint_read(test->checkpoint, &game->game,
                              mus_game_state_as_game_state(state), store, trainer);
}

/* The same callback loads both variants without a concrete state cast. */
static Status load_generic(const MusScoreGame *game, GameState *state,
                           InfoStore *store, Trainer *trainer, void *context) {
    ExtensionTest *test = context;
    CHECK(mus_score_game_matches_any(&game->game));
    CHECK(game->continuation == test->source);
    CHECK(cfr_game_validate_state(&game->game, state) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_game_state_init_generic(state, game) == CFR_STATUS_SUCCESS);
    test->generic_loads += 1;
    rewind(test->checkpoint);
    return cfr_checkpoint_read(test->checkpoint, &game->game, state, store, trainer);
}

static Status recover_nothing(const MusScoreGame *game, MusScoreCell *cell,
                               void *context) {
    ExtensionTest *test = context;
    (void)game;
    (void)cell;
    test->recovery_calls += 1;
    return CFR_STATUS_NOT_FOUND;
}

static Status save_destination(const MusScoreGame *game, const Trainer *trainer,
                                const MusScoreCell *cell, void *context) {
    ExtensionTest *test = context;
    CHECK(game->continuation == test->destination);
    CHECK(trainer->game == &game->game);
    CHECK(cfr_game_validate_state(&game->game, trainer->state) == CFR_STATUS_SUCCESS);
    CHECK(cell->iterations == 4 && trainer->training_iterations == 4);
    CHECK(trainer->stats.traversals == 4 * game->game.strategic_player_count);
    CHECK(!test->destination->cells[0][0].ready);
    test->saves += 1;
    return CFR_STATUS_SUCCESS;
}

static void test_extension(bool four_player, bool generic) {
    MusScoreTable source, destination;
    CHECK(mus_score_table_init(&source, 1) == CFR_STATUS_SUCCESS);
    CHECK(mus_score_table_init(&destination, 1) == CFR_STATUS_SUCCESS);
    source.four_player = destination.four_player = four_player;
    ExtensionTest test = {.source = &source, .destination = &destination};
    MusScoreTrainingOptions options = {.iterations = 2, .evaluation_samples = 8, .seed = 7};
    CHECK(mus_score_train(&source, 0, &options, save_source, NULL, &test) == CFR_STATUS_SUCCESS);
    CHECK(source.cells[0][0].ready);
    const MusScoreTable original_source = source;
    const MusScoreTable original_destination = destination;
    MusScoreExtension extension = {.table = &source, .context = &test};
    options.extension = &extension;
    options.threads_per_cell = 2;
    options.recover_cell = recover_nothing;
    options.recovery_context = &test;

    /* Invalid callback configurations cannot reach recovery or either loader. */
    CHECK(mus_score_train(&destination, 0, &options, save_destination, NULL, &test) ==
          CFR_STATUS_INVALID_ARGUMENT);
    extension.load_cell = load_legacy;
    if (four_player) {
        CHECK(mus_score_train(&destination, 0, &options, save_destination, NULL, &test) ==
              CFR_STATUS_INVALID_ARGUMENT);
    }
    extension.load_cell_generic = load_generic;
    if (four_player) {
        CHECK(mus_score_train(&destination, 0, &options, save_destination, NULL, &test) ==
              CFR_STATUS_INVALID_ARGUMENT);
    }
    CHECK(test.legacy_loads == 0 && test.generic_loads == 0 && test.recovery_calls == 0);
    CHECK(test.saves == 0);
    CHECK(memcmp(&destination, &original_destination, sizeof(destination)) == 0);

    extension.load_cell = generic ? NULL : load_legacy;
    /* Old callers need not initialize the appended field when a legacy loader
     * is present. A competing generic loader must not change that dispatch. */
    extension.load_cell_generic = load_generic;
    CHECK(mus_score_train(&destination, 0, &options, save_destination, NULL, &test) ==
          CFR_STATUS_SUCCESS);
    CHECK(test.legacy_loads == (generic ? 0U : 1U));
    CHECK(test.generic_loads == (generic ? 1U : 0U));
    CHECK(test.recovery_calls == 1 && test.saves == 1);
    CHECK(destination.cells[0][0].ready && destination.cells[0][0].iterations == 4);
    CHECK(memcmp(&source, &original_source, sizeof(source)) == 0);
    CHECK(fclose(test.checkpoint) == 0);
}

int main(void) {
    test_state_contracts();
    test_extension(false, false);
    test_extension(false, true);
    test_extension(true, true);
    puts("All score API compatibility checks passed.");
    return EXIT_SUCCESS;
}
