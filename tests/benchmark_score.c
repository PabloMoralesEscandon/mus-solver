#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "cfr/checkpoint.h"
#include "mus_solver/mus_score.h"

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

/* Hash the canonical checkpoint outside the timed region. This includes all
 * regrets, strategy sums, traversal statistics, schema and RNG state. */
static Status checkpoint_hash(const Trainer *trainer, uint64_t *hash) {
    FILE *stream = tmpfile();
    if (stream == NULL)
        return CFR_STATUS_IO_ERROR;
    Status status = cfr_checkpoint_write(stream, trainer);
    if (status == CFR_STATUS_SUCCESS && fseek(stream, 0, SEEK_SET) != 0)
        status = CFR_STATUS_IO_ERROR;
    *hash = UINT64_C(14695981039346656037);
    unsigned char buffer[16384];
    size_t count;
    while (status == CFR_STATUS_SUCCESS &&
           (count = fread(buffer, 1, sizeof(buffer), stream)) > 0) {
        for (size_t i = 0; i < count; i += 1) {
            *hash ^= buffer[i];
            *hash *= UINT64_C(1099511628211);
        }
    }
    if (ferror(stream))
        status = CFR_STATUS_IO_ERROR;
    if (fclose(stream) != 0)
        status = CFR_STATUS_IO_ERROR;
    return status;
}

int main(int argc, char **argv) {
    if (argc < 5 || argc > 7) {
        fprintf(stderr, "Usage: %s A B ITERATIONS SAMPLES [SEED [RESERVE]]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const size_t a = number(argv[1]);
    const size_t b = number(argv[2]);
    const size_t iterations = number(argv[3]);
    const size_t samples = number(argv[4]);
    const uint64_t seed = argc >= 6 ? (uint64_t)number(argv[5]) : UINT64_C(7);
    const size_t reserve = argc >= 7 ? number(argv[6]) : 0;
    if (a >= MUS_ORDAGO_WAGER || b >= MUS_ORDAGO_WAGER || iterations == 0 || samples == 0)
        return EXIT_FAILURE;

    MusScoreTable table;
    MusScoreGame game;
    MusGameState *state = malloc(sizeof(*state));
    InfoStore store = {0};
    Trainer trainer;
    Status status = mus_score_table_init(&table, MUS_ORDAGO_WAGER);
    if (state == NULL)
        return EXIT_FAILURE;
    /* Synthetic immutable continuations isolate one cell without training all
     * 1600 score cells. These values are a workload, not a solved policy. */
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
    const double start = seconds();
    if (status == CFR_STATUS_SUCCESS && reserve > 0)
        status = cfr_info_store_reserve(&store, reserve);
    if (status == CFR_STATUS_SUCCESS)
        status = cfr_trainer_run(&trainer, iterations);
    const double trained = seconds();
    double value = 0;
    if (status == CFR_STATUS_SUCCESS)
        status = mus_score_evaluate(&game, &store, samples,
                                    seed ^ UINT64_C(0xd1b54a32d192ed03), &value);
    const double evaluated = seconds();
    uint64_t hash = 0;
    if (status == CFR_STATUS_SUCCESS)
        status = checkpoint_hash(&trainer, &hash);
    InfoStoreStats stats;
    if (status == CFR_STATUS_SUCCESS)
        status = cfr_info_store_get_stats(&store, &stats);
    if (status == CFR_STATUS_SUCCESS) {
        printf("{\"score\":\"%zu:%zu\",\"iterations\":%zu,\"samples\":%zu,"
               "\"seed\":%" PRIu64 ",\"reserve\":%zu,\"training_seconds\":%.9f,"
               "\"iterations_per_second\":%.3f,\"evaluation_seconds\":%.9f,"
               "\"information_sets\":%zu,\"visited_nodes\":%zu,"
               "\"value\":%.17g,\"checkpoint_hash\":\"%016" PRIx64 "\"}\n",
               a, b, iterations, samples, seed, reserve, trained - start,
               (double)iterations / (trained - start), evaluated - trained,
               stats.size, trainer.stats.visited_nodes, value, hash);
    } else {
        fprintf(stderr, "Benchmark failed with status %d\n", (int)status);
    }
    (void)cfr_info_store_destroy(&store);
    free(state);
    return status == CFR_STATUS_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
