#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mus_solver/mus_simplified.h"
#include "cfr/info_node.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #c); exit(EXIT_FAILURE); } } while (0)
#define OK(c) CHECK((c) == CFR_STATUS_SUCCESS)

static unsigned int rng = 7;
static unsigned int random_number(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

static size_t menu(const Game *game, GameState *state, Action *actions) {
    size_t count;
    OK(cfr_game_legal_actions(game, state, actions, MUS_GAME_MAX_LEGAL_ACTIONS, &count));
    return count;
}

static bool contains(const Action *actions, size_t count, Action action) {
    for (size_t i = 0; i < count; ++i)
        if (actions[i] == action) return true;
    return false;
}

static void apply(const Game *game, GameState *state, Action action) {
    OK(cfr_game_apply_action(game, state, action));
    OK(cfr_game_validate_state(game, state));
}

static void init(GameState *state, const MusScoreGame *score, bool four) {
    if (score != NULL) OK(mus_score_game_state_init_generic(state, score));
    else if (four) OK(mus_four_game_state_init((MusFourGameState *)state));
    else OK(mus_ordago_public_game_state_init((MusGameState *)state));
}

static void deal(const Game *game, GameState *state) {
    for (;;) {
        Actor actor; OK(cfr_game_current_actor(game, state, &actor));
        if (actor.kind != CFR_ACTOR_CHANCE) break;
        Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
        size_t count = menu(game, state, actions);
        apply(game, state, actions[count - 1]);
    }
    apply(game, state, MUS_ACTION_NO_MUS);
}

static void reply(const Game *game, GameState *state, Action action) {
    apply(game, state, action);
    if (game->strategic_player_count == 4 &&
        ((MusFourGameState *)state)->model.pending_response != MUS_FOUR_RESPONSE_NONE)
        apply(game, state, action);
}

static void check_information(const Game *game, GameState *state, bool four, bool score) {
    InfoSetKey key, encoded;
    OK(cfr_game_information_set_key(game, state, &key));
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS], decoded_actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    const size_t count = menu(game, state, actions);
    size_t decoded_count;
    if (four) {
        MusFourInformationSet info;
        OK(score ? mus_four_score_information_set_decode(key, &info)
                 : mus_four_information_set_decode(key, &info));
        OK(score ? mus_four_score_information_set_encode(&info, &encoded)
                 : mus_four_information_set_encode(&info, &encoded));
        OK(mus_simplified_four_information_set_legal_actions(&info, score, decoded_actions,
                                                            MUS_GAME_MAX_LEGAL_ACTIONS, &decoded_count));
    } else {
        MusOrdagoInformationSet info;
        OK(score ? mus_score_game_information_set_decode(key, 40, &info)
                 : mus_ordago_public_game_information_set_decode(key, &info));
        OK(score ? mus_score_game_information_set_encode(&info, 40, &encoded)
                 : mus_ordago_public_game_information_set_encode(&info, &encoded));
        OK(mus_simplified_ordago_information_set_legal_actions(&info, decoded_actions,
                                                              MUS_GAME_MAX_LEGAL_ACTIONS, &decoded_count));
    }
    CHECK(key == encoded && count == decoded_count);
    CHECK(memcmp(actions, decoded_actions, count * sizeof(*actions)) == 0);
}

static void strict_examples(const Game *game, GameState *state, const MusScoreGame *score, bool four) {
    init(state, score, four); deal(game, state);
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    size_t count = menu(game, state, actions);
    CHECK(count == 5 && !contains(actions, count, MUS_ACTION_BET_SEVEN));
    CHECK(cfr_game_apply_action(game, state, MUS_ACTION_BET_SEVEN) == CFR_STATUS_ILLEGAL_ACTION);
    CHECK(game->trusted_operations->apply_action(game->context, state, MUS_ACTION_BET_SEVEN)
          == CFR_STATUS_ILLEGAL_ACTION);
    /* The original descriptor still offers the initial seven. */
    count = menu(mus_simplified_game_base(game), state, actions);
    CHECK(contains(actions, count, MUS_ACTION_BET_SEVEN));
    apply(game, state, MUS_ACTION_BET_TWO);
    count = menu(game, state, actions);
    CHECK(!contains(actions, count, MUS_ACTION_BET_TWO));
    CHECK(contains(actions, count, MUS_ACTION_BET_FOUR) && contains(actions, count, MUS_ACTION_BET_TEN));
    CHECK(cfr_game_apply_action(game, state, MUS_ACTION_BET_TWO) == CFR_STATUS_ILLEGAL_ACTION);
    reply(game, state, MUS_ACTION_BET_FOUR);
    count = menu(game, state, actions);
    CHECK(!contains(actions, count, MUS_ACTION_BET_TWO) && !contains(actions, count, MUS_ACTION_BET_FOUR));
    CHECK(contains(actions, count, MUS_ACTION_BET_TEN));
    CHECK(game->trusted_operations->apply_action(game->context, state, MUS_ACTION_BET_FOUR)
          == CFR_STATUS_ILLEGAL_ACTION);
    reply(game, state, MUS_ACTION_BET_TEN);
    count = menu(game, state, actions);
    CHECK(count == 3 && actions[0] == MUS_ACTION_REJECT && actions[1] == MUS_ACTION_ACCEPT &&
          actions[2] == MUS_ACTION_ORDAGO);
    const unsigned int wager = four ? ((MusFourGameState *)state)->model.current_wager
                                    : ((MusGameState *)state)->model.current_wager;
    CHECK(wager == 16);
    reply(game, state, MUS_ACTION_ORDAGO);
    count = menu(game, state, actions);
    CHECK(count == 2 && actions[0] == MUS_ACTION_REJECT && actions[1] == MUS_ACTION_ACCEPT);
    reply(game, state, MUS_ACTION_ACCEPT);
    bool terminal; OK(cfr_game_is_terminal(game, state, &terminal)); CHECK(terminal);
    for (unsigned int p = 0; p < game->strategic_player_count; ++p) {
        Utility value; OK(cfr_game_terminal_utility(game, state, (Player)p, &value));
        CHECK(fabs(value) == (score == NULL ? 40 : 1));
    }
    /* The strict comparison applies from any numeric opening. */
    init(state, score, four); deal(game, state); apply(game, state, MUS_ACTION_BET_TEN);
    count = menu(game, state, actions); CHECK(count == 3);
    reply(game, state, MUS_ACTION_REJECT);
    count = menu(game, state, actions);
    CHECK(contains(actions, count, MUS_ACTION_BET_TWO));
}

static void random_hands(const Game *game, GameState *state, const MusScoreGame *score, bool four) {
    const size_t size = four ? sizeof(MusFourGameState) : sizeof(MusGameState);
    GameState *saved = malloc(size); CHECK(saved != NULL);
    for (unsigned int trial = 0; trial < 250; ++trial) {
        init(state, score, four);
        for (size_t step = 0;; ++step) {
            CHECK(step < MUS_FOUR_UNDO_CAPACITY);
            bool terminal; OK(cfr_game_is_terminal(game, state, &terminal));
            if (terminal) break;
            Actor actor; OK(cfr_game_current_actor(game, state, &actor));
            Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
            const size_t count = menu(game, state, actions);
            if (actor.kind == CFR_ACTOR_PLAYER) {
                check_information(game, state, four, score != NULL);
                CHECK(!contains(actions, count, MUS_ACTION_BET_SEVEN));
            } else {
                Action base_actions[MUS_GAME_MAX_LEGAL_ACTIONS];
                const size_t base_count = menu(mus_simplified_game_base(game), state, base_actions);
                CHECK(base_count == count && memcmp(actions, base_actions, count * sizeof(Action)) == 0);
            }
            const Action action = actions[random_number() % count];
            memcpy(saved, state, size);
            apply(game, state, action);
            OK(cfr_game_undo_action(game, state));
            if (four) {
                CHECK(memcmp(&((MusFourGameState *)state)->model, &((MusFourGameState *)saved)->model,
                             sizeof(MusFourGameModel)) == 0);
                CHECK(((MusFourGameState *)state)->undo_count == ((MusFourGameState *)saved)->undo_count);
            } else {
                CHECK(memcmp(&((MusGameState *)state)->model, &((MusGameState *)saved)->model,
                             sizeof(MusGameModel)) == 0);
                CHECK(((MusGameState *)state)->undo_count == ((MusGameState *)saved)->undo_count);
            }
            apply(game, state, action);
        }
        Utility values[4];
        for (unsigned int p = 0; p < game->strategic_player_count; ++p) {
            OK(cfr_game_terminal_utility(game, state, (Player)p, &values[p]));
            CHECK(isfinite(values[p]) && fabs(values[p]) <= (score == NULL ? 40 : 1));
        }
        CHECK(values[0] == -values[1]);
        if (four) CHECK(values[0] == values[2] && values[1] == values[3]);
    }
    free(saved);
}

static InfoSetKey preload_node(InfoStore *store, const Game *game, GameState *state) {
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    size_t count = menu(game, state, actions);
    InfoSetKey key; InfoNode *node;
    OK(cfr_game_information_set_key(game, state, &key));
    OK(cfr_info_store_get_or_create(store, key, count, &node));
    for (size_t a = 0; a < count; ++a)
        node->strategy_sums[a] = actions[a] == MUS_ACTION_BET_TWO || actions[a] == MUS_ACTION_BET_SEVEN
            ? 10 : actions[a] == MUS_ACTION_BET_TEN ? 1 : 0;
    return key;
}

static void check_preload(void) {
    const Game *base = mus_ordago_public_game_descriptor();
    MusGameState state;
    GameState *raw = (GameState *)&state;
    InfoStore source = {0}, destination = {0};
    OK(cfr_info_store_init(&source)); OK(cfr_info_store_init(&destination));
    init(raw, NULL, false); deal(base, raw);
    InfoSetKey opening = preload_node(&source, base, raw);
    apply(base, raw, MUS_ACTION_BET_TWO);
    InfoSetKey response = preload_node(&source, base, raw);
    apply(base, raw, MUS_ACTION_BET_TWO);
    preload_node(&source, base, raw); /* Repeated increment: not a simplified history. */
    init(raw, NULL, false); deal(base, raw); apply(base, raw, MUS_ACTION_BET_SEVEN);
    preload_node(&source, base, raw);
    MusScoreTable table; MusScoreGame game;
    OK(mus_score_table_init(&table, 40)); table.simplified = true;
    OK(mus_score_game_init(&game, &table, 0, 0));
    Trainer trainer = {.game = base, .store = &source};
    MusScorePreloadOptions options = {.source = &trainer, .weight = 1, .uniform_mix = 0.05};
    MusScorePreloadStats stats;
    OK(mus_score_preload_ordago(&game, &options, &destination, &stats));
    CHECK(stats.source_nodes == 4 && stats.imported_nodes == 2 && stats.skipped_illegal_nodes == 2);
    CHECK(stats.expanded_action_nodes == 0);
    for (size_t i = 0; i < 2; ++i) {
        InfoSetKey key; InfoNode *node;
        OK(mus_score_information_set_from_ordago_public(&game, i == 0 ? opening : response, &key));
        OK(cfr_info_store_find(&destination, key, &node));
        MusOrdagoInformationSet info; Action actions[MUS_GAME_MAX_LEGAL_ACTIONS]; size_t count;
        OK(mus_score_game_information_set_decode(key, 40, &info));
        OK(mus_simplified_ordago_information_set_legal_actions(&info, actions,
                                                              MUS_GAME_MAX_LEGAL_ACTIONS, &count));
        CHECK(count == node->action_count);
        for (size_t a = 0; a < count; ++a) {
            const double filtered = i == 0
                ? (actions[a] == MUS_ACTION_BET_TWO ? 10.0 / 11 : actions[a] == MUS_ACTION_BET_TEN ? 1.0 / 11 : 0)
                : (actions[a] == MUS_ACTION_BET_TEN ? 1 : 0);
            CHECK(fabs(node->strategy_sums[a] - (0.95 * filtered + 0.05 / (double)count)) < 1e-12);
        }
    }
    /* Malformed source data must fail, not count as an incompatible history. */
    InfoNode *broken; OK(cfr_info_store_find(&source, opening, &broken));
    --broken->action_count;
    CHECK(mus_score_preload_ordago(&game, &options, &destination, &stats) == CFR_STATUS_FORMAT_ERROR);
    ++broken->action_count;
    OK(cfr_info_store_destroy(&source)); OK(cfr_info_store_destroy(&destination));

    /* The opposite direction adds the original actions with uniform prior mass. */
    OK(cfr_info_store_init(&source)); OK(cfr_info_store_init(&destination));
    const Game *simple = mus_simplified_game_descriptor(false);
    init(raw, NULL, false); deal(simple, raw); opening = preload_node(&source, simple, raw);
    trainer.game = simple; table.simplified = false;
    OK(mus_score_game_init(&game, &table, 0, 0));
    OK(mus_score_preload_ordago(&game, &options, &destination, &stats));
    CHECK(stats.imported_nodes == 1 && stats.expanded_action_nodes == 1);
    InfoSetKey key; InfoNode *node;
    OK(mus_score_information_set_from_ordago_public(&game, opening, &key));
    OK(cfr_info_store_find(&destination, key, &node));
    CHECK(node->action_count == 6);
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS]; size_t count = menu(base, raw, actions);
    for (size_t a = 0; a < count; ++a)
        if (actions[a] == MUS_ACTION_BET_SEVEN)
            CHECK(fabs(node->strategy_sums[a] - 0.05 / (double)count) < 1e-12);
    OK(cfr_info_store_destroy(&source)); OK(cfr_info_store_destroy(&destination));
}

int main(void) {
    check_preload();
    for (unsigned int four = 0; four < 2; ++four) {
        MusScoreTable table;
        OK(mus_score_table_init(&table, 40));
        table.four_player = four != 0; table.simplified = true;
        for (unsigned int a = 0; a < 40; ++a)
            for (unsigned int b = 0; b < 40; ++b)
                table.cells[a][b] = (MusScoreCell){.ready = true, .iterations = 1, .evaluation_samples = 1};
        MusScoreGame score;
        OK(mus_score_game_init(&score, &table, 0, 0));
        MusScoreGame another_cell;
        OK(mus_score_game_init(&another_cell, &table, 1, 1));
        CHECK(score.game.context == another_cell.game.context);
        CHECK(score.game.strategy_schema_id == score.schema);
        CHECK(another_cell.game.strategy_schema_id == another_cell.schema);
        GameState *state = calloc(1, mus_score_game_state_size(&score)); CHECK(state != NULL);
        for (unsigned int scoring = 0; scoring < 2; ++scoring) {
            const Game *game = scoring ? &score.game : mus_simplified_game_descriptor(four != 0);
            CHECK(mus_simplified_game_matches(game));
            CHECK(!mus_simplified_game_matches(mus_simplified_game_base(game)));
            strict_examples(game, state, scoring ? &score : NULL, four != 0);
            random_hands(game, state, scoring ? &score : NULL, four != 0);
        }
        free(state);
    }
    puts("All simplified rule checks passed (1000 hands across four formats).");
    return EXIT_SUCCESS;
}
