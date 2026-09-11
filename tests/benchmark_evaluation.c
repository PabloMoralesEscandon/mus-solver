#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cfr/trainer.h"
#include "mus_solver/mus_evaluation.h"

static double seconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) exit(EXIT_FAILURE);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static size_t number(const char *text) {
    char *end;
    errno = 0;
    const uintmax_t value = strtoumax(text, &end, 10);
    if (*text < '0' || *text > '9' || *end != '\0' || errno || value > SIZE_MAX)
        exit(EXIT_FAILURE);
    return (size_t)value;
}

int main(int argc, char **argv) {
    if (argc != 5 || (strcmp(argv[1], "single-bet") && strcmp(argv[1], "raises-to-eight")) ||
        (strcmp(argv[3], "root") && strcmp(argv[3], "first-hand-fixed"))) {
        fprintf(stderr, "Usage: %s single-bet|raises-to-eight ITERATIONS root|first-hand-fixed REPEATS\n", argv[0]);
        return EXIT_FAILURE;
    }
    const bool raises = strcmp(argv[1], "raises-to-eight") == 0;
    const size_t iterations = number(argv[2]);
    const size_t repeats = number(argv[4]);
    if (!repeats) return EXIT_FAILURE;
    const Game *game = raises ? mus_raises_to_eight_game_descriptor()
                              : mus_single_bet_game_descriptor();
    MusGameState state;
    InfoStore store = {0};
    Trainer trainer;
    Status status = raises ? mus_raises_to_eight_game_state_init(&state)
                           : mus_single_bet_game_state_init(&state);
    if (status == CFR_STATUS_SUCCESS) status = cfr_info_store_init(&store);
    if (status == CFR_STATUS_SUCCESS)
        status = cfr_trainer_init_mccfr(&trainer, game,
            mus_game_state_as_game_state(&state), &store, UINT64_C(7));
    if (status == CFR_STATUS_SUCCESS && iterations)
        status = cfr_trainer_run(&trainer, iterations);
    if (strcmp(argv[3], "first-hand-fixed") == 0) {
        /* P0 holds 4, knight, king, king. P1's hand is still enumerated.
         * This conditional diagnostic is not root exploitability. */
        const Action hand[MUS_GAME_CLASS_COUNT - 1] = {0, 1, 0, 0, 0, 0, 1};
        for (size_t i = 0; i < MUS_GAME_CLASS_COUNT - 1 && status == CFR_STATUS_SUCCESS; i++)
            status = cfr_game_apply_action(game, mus_game_state_as_game_state(&state), hand[i]);
    }
    EvaluationMetrics metrics = {0};
    const double start = seconds();
    for (size_t i = 0; i < repeats && status == CFR_STATUS_SUCCESS; i++)
        status = raises ? mus_raises_to_eight_evaluation_metrics_from_state(&state, &store, &metrics)
                        : mus_single_bet_evaluation_metrics_from_state(&state, &store, &metrics);
    const double elapsed = seconds() - start;
    /* Store size and visited_nodes below describe training, not evaluation. */
    InfoStoreStats stats;
    if (status == CFR_STATUS_SUCCESS) status = cfr_info_store_get_stats(&store, &stats);
    if (status == CFR_STATUS_SUCCESS)
        printf("{\"format\":\"%s\",\"scope\":\"%s\",\"iterations\":%zu,\"repeats\":%zu,"
               "\"evaluation_seconds\":%.9f,\"information_sets\":%zu,\"visited_nodes\":%zu,"
               "\"profile0\":%.17g,\"profile1\":%.17g,\"br0\":%.17g,\"br1\":%.17g,"
               "\"improvement0\":%.17g,\"improvement1\":%.17g,\"nash_conv\":%.17g,\"exploitability\":%.17g}\n",
               argv[1], argv[3], iterations, repeats, elapsed, stats.size, trainer.stats.visited_nodes,
               metrics.profile_value_player_0, metrics.profile_value_player_1,
               metrics.best_response_value_player_0, metrics.best_response_value_player_1,
               metrics.improvement_player_0, metrics.improvement_player_1,
               metrics.nash_conv, metrics.exploitability);
    else fprintf(stderr, "Evaluation benchmark failed with status %d\n", (int)status);
    (void)cfr_info_store_destroy(&store);
    return status == CFR_STATUS_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
