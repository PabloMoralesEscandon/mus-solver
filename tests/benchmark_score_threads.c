#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>

#include "mus_solver/mus_score.h"
#include "mus_solver/mus_training.h"

static double seconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        exit(EXIT_FAILURE);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static size_t number(const char *text) {
    char *end;
    errno = 0;
    const uintmax_t value = strtoumax(text, &end, 10);
    if (*text < '0' || *text > '9' || *end != '\0' || errno != 0 || value > SIZE_MAX) {
        fprintf(stderr, "Invalid unsigned integer: %s\n", text);
        exit(EXIT_FAILURE);
    }
    return (size_t)value;
}

int main(int argc, char **argv) {
    if (argc < 5 || argc > 6) {
        fprintf(stderr, "Usage: %s A B ITERATIONS THREADS [SEED]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const size_t a = number(argv[1]);
    const size_t b = number(argv[2]);
    const size_t iterations = number(argv[3]);
    const size_t threads = number(argv[4]);
    const uint64_t seed = argc >= 6 ? (uint64_t)number(argv[5]) : UINT64_C(7);
    if (a >= MUS_ORDAGO_WAGER || b >= MUS_ORDAGO_WAGER || iterations == 0 || threads == 0)
        return EXIT_FAILURE;

    MusScoreTable table;
    MusScoreGame game;
    MusGameState *state = malloc(sizeof(*state));
    InfoStore store = {0};
    Trainer trainer;
    Status status = mus_score_table_init(&table, MUS_ORDAGO_WAGER);
    if (state == NULL)
        return EXIT_FAILURE;
    /* Use the same synthetic continuations as benchmark_score.c. */
    for (int x = 0; x < MUS_ORDAGO_WAGER; x += 1) {
        for (int y = 0; y < MUS_ORDAGO_WAGER; y += 1) {
            table.cells[x][y] = (MusScoreCell){
                .ready = true, .value = (double)(x - y) / MUS_ORDAGO_WAGER,
                .iterations = 1, .evaluation_samples = 1, .training_seed = seed,
            };
        }
    }
    if (status == CFR_STATUS_SUCCESS)
        status = mus_score_game_init(&game, &table, (unsigned int)a, (unsigned int)b);
    if (status == CFR_STATUS_SUCCESS)
        status = mus_score_game_state_init(state, &game);
    if (status == CFR_STATUS_SUCCESS)
        status = cfr_info_store_init(&store);
    if (status == CFR_STATUS_SUCCESS)
        status = cfr_trainer_init_mccfr(&trainer, &game.game,
                                      mus_game_state_as_game_state(state), &store, seed);

    /* Include concurrent-store setup and worker creation; omit evaluation and
     * persistence. There is no explicit reservation or progress reporting. */
    const double start = seconds();
    if (status == CFR_STATUS_SUCCESS)
        status = mus_training_run(&trainer, iterations, threads, iterations, NULL, NULL);
    const double elapsed = seconds() - start;
    InfoStoreStats stats;
    struct rusage usage;
    if (status == CFR_STATUS_SUCCESS)
        status = cfr_info_store_get_stats(&store, &stats);
    if (status == CFR_STATUS_SUCCESS && getrusage(RUSAGE_SELF, &usage) != 0)
        status = CFR_STATUS_IO_ERROR;
    if (status == CFR_STATUS_SUCCESS &&
        (trainer.training_iterations != iterations || trainer.stats.iterations != iterations))
        status = CFR_STATUS_INVALID_ARGUMENT;
    if (status == CFR_STATUS_SUCCESS) {
        printf("{\"score\":\"%zu:%zu\",\"iterations\":%zu,\"threads\":%zu,"
               "\"seed\":%" PRIu64 ",\"training_seconds\":%.9f,"
               "\"iterations_per_second\":%.3f,\"visited_nodes\":%zu,"
               "\"information_sets\":%zu,\"peak_rss_kib\":%ld}\n",
               a, b, iterations, threads, seed, elapsed, (double)iterations / elapsed,
               trainer.stats.visited_nodes, stats.size, usage.ru_maxrss);
    } else {
        fprintf(stderr, "Benchmark failed with status %d\n", (int)status);
    }
    (void)cfr_info_store_destroy(&store);
    free(state);
    return status == CFR_STATUS_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
