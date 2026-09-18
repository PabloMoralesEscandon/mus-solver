#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#include "mus_solver/mus_score.h"
#include "mus_solver/mus_four.h"
#include "mus_solver/mus_simplified.h"
#include "mus_solver/mus_training.h"

static double seconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        exit(EXIT_FAILURE);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static double timeval_seconds(struct timeval value) {
    return (double)value.tv_sec + (double)value.tv_usec * 1e-6;
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
    if (argc < 4 || argc == 6 || argc > 8) {
        fprintf(stderr, "Usage: %s FORMAT ITERATIONS THREADS [SEED [A B [REPORT_EVERY]]]\n"
                        "FORMAT must be ordago, ordago-four, ordago-score, or ordago-score-four.\n"
                        "Each format also accepts the -simplified suffix.\n"
                        "ordago-public is an alias of ordago.\n", argv[0]);
        return EXIT_FAILURE;
    }
    const bool simplified = strcmp(argv[1], "ordago-simplified") == 0 ||
        strcmp(argv[1], "ordago-four-simplified") == 0 ||
        strcmp(argv[1], "ordago-score-simplified") == 0 ||
        strcmp(argv[1], "ordago-score-four-simplified") == 0;
    const bool four = strcmp(argv[1], "ordago-four") == 0 ||
        strcmp(argv[1], "ordago-four-simplified") == 0 ||
        strcmp(argv[1], "ordago-score-four") == 0 ||
        strcmp(argv[1], "ordago-score-four-simplified") == 0;
    const bool score = strcmp(argv[1], "ordago-score") == 0 ||
        strcmp(argv[1], "ordago-score-simplified") == 0 ||
        strcmp(argv[1], "ordago-score-four") == 0 ||
        strcmp(argv[1], "ordago-score-four-simplified") == 0;
    if (!score && !four && !simplified &&
        strcmp(argv[1], "ordago") != 0 && strcmp(argv[1], "ordago-public") != 0) {
        fprintf(stderr, "Invalid format: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    const size_t iterations = number(argv[2]);
    const size_t threads = number(argv[3]);
    const uint64_t seed = argc >= 5 ? (uint64_t)number(argv[4]) : UINT64_C(7);
    const size_t a = argc >= 7 ? number(argv[5]) : 0;
    const size_t b = argc >= 7 ? number(argv[6]) : 0;
    const size_t report_every = argc == 8 ? number(argv[7]) : iterations;
    if (iterations == 0 || threads == 0 || report_every == 0 ||
        a >= MUS_ORDAGO_WAGER || b >= MUS_ORDAGO_WAGER || (!score && (a != 0 || b != 0))) {
        fprintf(stderr, "Invalid iterations, threads, scores, or report interval.\n");
        return EXIT_FAILURE;
    }

    MusScoreTable table;
    MusScoreGame score_game;
    void *state = malloc(four ? sizeof(MusFourGameState) : sizeof(MusGameState));
    InfoStore store = {0};
    Trainer trainer;
    if (state == NULL)
        return EXIT_FAILURE;
    Status status;
    const Game *game = simplified ? mus_simplified_game_descriptor(four)
        : four ? mus_four_game_descriptor() : mus_ordago_public_game_descriptor();
    if (score) {
        status = mus_score_table_init(&table, MUS_ORDAGO_WAGER);
        /* Fixed synthetic continuations isolate training from table solving. */
        if (status == CFR_STATUS_SUCCESS) {
            table.four_player = four;
            table.simplified = simplified;
            for (int x = 0; x < MUS_ORDAGO_WAGER; x += 1) {
                for (int y = 0; y < MUS_ORDAGO_WAGER; y += 1) {
                    table.cells[x][y] = (MusScoreCell){
                        .ready = true, .value = (double)(x - y) / MUS_ORDAGO_WAGER,
                        .iterations = 1, .evaluation_samples = 1, .training_seed = seed,
                    };
                }
            }
            status = mus_score_game_init(&score_game, &table,
                                         (unsigned int)a, (unsigned int)b);
        }
        if (status == CFR_STATUS_SUCCESS) {
            game = &score_game.game;
            status = mus_score_game_state_init_generic(state, &score_game);
        }
    } else {
        status = four ? mus_four_game_state_init(state) : mus_ordago_public_game_state_init(state);
    }
    if (status == CFR_STATUS_SUCCESS)
        status = cfr_info_store_init(&store);
    if (status == CFR_STATUS_SUCCESS)
        status = cfr_trainer_init_mccfr(&trainer, game, state, &store, seed);

    struct rusage before = {0}, after = {0};
    if (status == CFR_STATUS_SUCCESS && getrusage(RUSAGE_SELF, &before) != 0)
        status = CFR_STATUS_IO_ERROR;
    /* Include store preparation, worker creation, and final worker joins. */
    const double start = seconds();
    if (status == CFR_STATUS_SUCCESS)
        status = mus_training_run(&trainer, iterations, threads, report_every, NULL, NULL);
    const double end = seconds();
    if (status == CFR_STATUS_SUCCESS && getrusage(RUSAGE_SELF, &after) != 0)
        status = CFR_STATUS_IO_ERROR;
    const double elapsed = end - start;
    InfoStoreStats stats;
    if (status == CFR_STATUS_SUCCESS)
        status = cfr_info_store_get_stats(&store, &stats);
    if (status == CFR_STATUS_SUCCESS &&
        (trainer.training_iterations != iterations || trainer.stats.iterations != iterations ||
         trainer.stats.traversals != iterations * game->strategic_player_count ||
         trainer.stats.errors != 0 ||
         (four ? ((MusFourGameState *)state)->undo_count
               : ((MusGameState *)state)->undo_count) != 0))
        status = CFR_STATUS_INVALID_ARGUMENT;
    if (status == CFR_STATUS_SUCCESS) {
        printf("{\"format\":\"%s\",\"schema\":\"%s\",\"max_muses\":%d,"
               "\"score\":\"%zu:%zu\",\"iterations\":%zu,\"threads\":%zu,"
               "\"seed\":%" PRIu64 ",\"report_every\":%zu,"
               "\"training_start_monotonic\":%.9f,\"training_end_monotonic\":%.9f,"
               "\"training_seconds\":%.9f,\"iterations_per_second\":%.3f,"
               "\"traversals\":%zu,\"visited_nodes\":%zu,\"nodes_per_second\":%.3f,"
               "\"information_sets\":%zu,\"store_capacity\":%zu,"
               "\"store_collisions\":%zu,\"store_growths\":%zu,\"peak_rss_kib\":%ld,"
               "\"user_cpu_seconds\":%.6f,\"system_cpu_seconds\":%.6f,"
               "\"minor_faults\":%ld,\"major_faults\":%ld,"
               "\"voluntary_context_switches\":%ld,\"involuntary_context_switches\":%ld,"
               "\"input_blocks\":%ld,\"output_blocks\":%ld}\n",
               argv[1], game->strategy_schema_id, MUS_GAME_MAX_MUSES,
               a, b, iterations, threads, seed, report_every, start, end, elapsed,
               (double)iterations / elapsed, trainer.stats.traversals,
               trainer.stats.visited_nodes, (double)trainer.stats.visited_nodes / elapsed,
               stats.size, stats.capacity, stats.collision_count, stats.growth_count,
               after.ru_maxrss, timeval_seconds(after.ru_utime) - timeval_seconds(before.ru_utime),
               timeval_seconds(after.ru_stime) - timeval_seconds(before.ru_stime),
               after.ru_minflt - before.ru_minflt, after.ru_majflt - before.ru_majflt,
               after.ru_nvcsw - before.ru_nvcsw, after.ru_nivcsw - before.ru_nivcsw,
               after.ru_inblock - before.ru_inblock, after.ru_oublock - before.ru_oublock);
        (void)fflush(stdout);
    } else {
        fprintf(stderr, "Benchmark failed with status %d\n", (int)status);
    }
    (void)cfr_info_store_destroy(&store);
    free(state);
    return status == CFR_STATUS_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
