#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mus_solver/mus_game.h"
#include "mus_solver/mus_score.h"

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__,          \
                    #condition);                                                \
            exit(EXIT_FAILURE);                                                 \
        }                                                                      \
    } while (0)

static GameState *as_game(MusGameState *state) {
    return mus_game_state_as_game_state(state);
}

static void check_discard_hand(const uint8_t hand[MUS_GAME_CLASS_COUNT]) {
    const Game *game = mus_ordago_game_descriptor();
    MusGameState state;
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    size_t count = 0;

    CHECK(mus_ordago_game_state_init(&state) == CFR_STATUS_SUCCESS);
    for (int player = 0; player < MUS_GAME_PLAYER_COUNT; player += 1) {
        for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT - 1;
             card_class += 1) {
            /* Four kings are compatible with every player-0 hand. */
            CHECK(cfr_game_apply_action(game, as_game(&state),
                  player == 0 ? hand[card_class] : 0) == CFR_STATUS_SUCCESS);
        }
    }
    CHECK(cfr_game_apply_action(game, as_game(&state), MUS_ACTION_MUS) ==
          CFR_STATUS_SUCCESS);
    CHECK(cfr_game_apply_action(game, as_game(&state), MUS_ACTION_MUS) ==
          CFR_STATUS_SUCCESS);
    CHECK(game->trusted_operations->legal_actions(game->context, as_game(&state),
          actions, MUS_GAME_MAX_LEGAL_ACTIONS, &count) == CFR_STATUS_SUCCESS);
    size_t expected = 0;
    for (unsigned int mask = 1; mask < MUS_GAME_DISCARD_MASK_COUNT; mask += 1) {
        if (mus_ordago_game_discard_mask_is_canonical(hand, mask)) {
            CHECK(expected < count);
            CHECK(actions[expected++] == MUS_ACTION_DISCARD_MASK(mask));
        }
    }
    CHECK(count == expected);
}

static void test_all_discard_hands(void) {
    size_t count = 0;
    for (int a = 0; a < MUS_GAME_CLASS_COUNT; a += 1) {
        for (int b = a; b < MUS_GAME_CLASS_COUNT; b += 1) {
            for (int c = b; c < MUS_GAME_CLASS_COUNT; c += 1) {
                for (int d = c; d < MUS_GAME_CLASS_COUNT; d += 1) {
                    uint8_t hand[MUS_GAME_CLASS_COUNT] = {0};
                    hand[a] += 1;
                    hand[b] += 1;
                    hand[c] += 1;
                    hand[d] += 1;
                    check_discard_hand(hand);
                    count += 1;
                }
            }
        }
    }
    CHECK(count == 330);
}

static void test_score_accounting_paths(unsigned int target) {
    MusScoreTable table;
    uint32_t random = 17;

    CHECK(mus_score_table_init(&table, target) == CFR_STATUS_SUCCESS);
    for (unsigned int a = 0; a < target; a += 1) {
        for (unsigned int b = 0; b < target; b += 1) {
            MusScoreGame score;
            CHECK(mus_score_game_init(&score, &table, a, b) == CFR_STATUS_SUCCESS);
            const Game *game = &score.game;
            const GameOperations *ops = game->trusted_operations;
            for (unsigned int sample = 0; sample < 4; sample += 1) {
                MusGameState state;
                CHECK(mus_score_game_state_init(&state, &score) == CFR_STATUS_SUCCESS);
                while (state.model.phase != MUS_PHASE_TERMINAL) {
                    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
                    Action untouched[MUS_GAME_MAX_LEGAL_ACTIONS];
                    size_t count = 0, required = 0;
                    CHECK(cfr_game_validate_state(game, as_game(&state)) ==
                          CFR_STATUS_SUCCESS);
                    CHECK(ops->legal_actions(game->context, as_game(&state), actions,
                          MUS_GAME_MAX_LEGAL_ACTIONS, &count) == CFR_STATUS_SUCCESS);
                    CHECK(count > 0);
                    memset(untouched, 0x5a, sizeof(untouched));
                    Action before_actions[MUS_GAME_MAX_LEGAL_ACTIONS];
                    memcpy(before_actions, untouched, sizeof(untouched));
                    CHECK(ops->legal_actions(game->context, as_game(&state), untouched,
                          count - 1, &required) == CFR_STATUS_BUFFER_TOO_SMALL);
                    CHECK(required == count);
                    CHECK(memcmp(untouched, before_actions, sizeof(untouched)) == 0);

                    /* Validate every child against the independent full
                     * accounting recomputation, then verify exact undo. */
                    const MusGameModel before = state.model;
                    const size_t before_undo = state.undo_count;
                    for (size_t index = 0; index < count; index += 1) {
                        CHECK(ops->apply_action(game->context, as_game(&state),
                              actions[index]) == CFR_STATUS_SUCCESS);
                        CHECK(cfr_game_validate_state(game, as_game(&state)) ==
                              CFR_STATUS_SUCCESS);
                        CHECK(ops->undo_action(game->context, as_game(&state)) ==
                              CFR_STATUS_SUCCESS);
                        CHECK(memcmp(&before, &state.model, sizeof(before)) == 0);
                        CHECK(state.undo_count == before_undo);
                    }
                    random = random * UINT32_C(1664525) + UINT32_C(1013904223);
                    size_t choices = count;
                    /* Alternate unrestricted paths and paths without ordagos
                     * so final counting is reached at low initial scores. */
                    if ((sample & 1U) != 0 && count > 1 &&
                        actions[count - 1] == MUS_ACTION_ORDAGO)
                        choices -= 1;
                    CHECK(ops->apply_action(game->context, as_game(&state),
                          actions[random % choices]) == CFR_STATUS_SUCCESS);
                }
                CHECK(cfr_game_validate_state(game, as_game(&state)) ==
                      CFR_STATUS_SUCCESS);
                while (state.undo_count > 0) {
                    CHECK(ops->undo_action(game->context, as_game(&state)) ==
                          CFR_STATUS_SUCCESS);
                    CHECK(cfr_game_validate_state(game, as_game(&state)) ==
                          CFR_STATUS_SUCCESS);
                }
            }
        }
    }
}

int main(void) {
    test_all_discard_hands();
    test_score_accounting_paths(3);
    test_score_accounting_paths(5);
    test_score_accounting_paths(40);
    puts("Mus hot-path equivalence tests passed.");
    return EXIT_SUCCESS;
}
