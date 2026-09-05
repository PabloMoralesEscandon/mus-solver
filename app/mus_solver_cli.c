#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cfr/checkpoint.h"
#include "cfr/info_store.h"
#include "cfr/trainer.h"
#include "mus_solver/mus_evaluation.h"
#include "mus_solver/mus_game.h"
#include "mus_solver/mus_strategy.h"
#include "reservation_internal.h"

enum { EXIT_RUNTIME_ERROR = 1, EXIT_USAGE_ERROR = 2 };

typedef enum {
    CLI_FORMAT_SINGLE_BET,
    CLI_FORMAT_RAISES_TO_EIGHT,
    CLI_FORMAT_ORDAGO
} CliFormat;

typedef struct {
    size_t iterations;
    size_t report_every;
    size_t reserve_information_sets;
    size_t threads;
    uint64_t seed;
    const char *load_path;
    const char *save_path;
    const char *export_path;
    CliFormat format;
    bool iterations_set;
    bool report_every_set;
    bool reserve_information_sets_set;
    bool threads_set;
    bool seed_set;
    bool format_set;
    bool load_compressed;
    bool save_compressed;
    bool evaluate;
    bool help;
} CliOptions;

static bool print_usage(FILE *stream, const char *program) {
    return fprintf(
               stream,
               "Uso:\n"
               "  %s --iterations N [--seed N] [--report-every N] "
               "[--threads N] "
               "[--format FORMAT] [--reserve-information-sets N] "
               "[--save FILE]\n"
               "     [--evaluate] [--export-strategy FILE]\n"
               "  %s --load FILE --iterations N [--report-every N] "
               "[--threads N] "
               "[--format FORMAT] [--save FILE]\n"
               "     [--evaluate] [--export-strategy FILE]\n"
               "  %s [--format FORMAT] [--load FILE] --evaluate "
               "[--export-strategy FILE]\n"
               "  %s [--format FORMAT] --load FILE "
               "--export-strategy FILE\n"
               "\n"
               "Opciones:\n"
               "  --iterations N         Iteraciones MCCFR nuevas.\n"
               "  --report-every N       Frecuencia de informes de entrenamiento.\n"
               "  --threads N            Hilos de entrenamiento (1 por defecto).\n"
               "  --seed N               Semilla de un entrenamiento nuevo (0 por "
               "defecto).\n"
               "  --format FORMAT        single-bet (por defecto) o "
               "raises-to-eight u ordago.\n"
               "  --reserve-information-sets N\n"
               "                          Sustituye la reserva automatica de "
               "ordago.\n"
               "  --load FILE            Carga un checkpoint reanudable.\n"
               "  --load-compressed FILE Carga un checkpoint Zstandard.\n"
               "  --save FILE            Guarda el checkpoint tras entrenar.\n"
               "  --save-compressed FILE Guarda un checkpoint Zstandard nivel 1.\n"
               "  --evaluate             Evalua la estrategia media; ordago usa "
               "un diagnostico acotado.\n"
               "  --export-strategy FILE Exporta la estrategia con etiquetas de "
               "mus.\n"
               "  --help, -h             Muestra esta ayuda.\n",
               program, program, program, program) >= 0;
}

static bool parse_size(const char *text, size_t *value_out) {
    char *end = NULL;
    uintmax_t value;

    if (text == NULL || text[0] == '\0' || text[0] == '-')
        return false;
    errno = 0;
    value = strtoumax(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0 ||
        value > SIZE_MAX) {
        return false;
    }
    *value_out = (size_t)value;
    return true;
}

static bool parse_seed(const char *text, uint64_t *value_out) {
    char *end = NULL;
    uintmax_t value;

    if (text == NULL || text[0] == '\0' || text[0] == '-')
        return false;
    errno = 0;
    value = strtoumax(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT64_MAX)
        return false;
    *value_out = (uint64_t)value;
    return true;
}

static bool option_value(int argc, char *argv[], int *index,
                         const char **value_out) {
    if (*index + 1 >= argc)
        return false;
    *index += 1;
    *value_out = argv[*index];
    return true;
}

static bool parse_options(int argc, char *argv[], CliOptions *options) {
    *options = (CliOptions){.threads = 1};
    for (int index = 1; index < argc; index += 1) {
        const char *argument = argv[index];
        const char *value = NULL;

        if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
            options->help = true;
        } else if (strcmp(argument, "--iterations") == 0) {
            if (options->iterations_set ||
                !option_value(argc, argv, &index, &value) ||
                !parse_size(value, &options->iterations)) {
                return false;
            }
            options->iterations_set = true;
        } else if (strcmp(argument, "--report-every") == 0) {
            if (options->report_every_set ||
                !option_value(argc, argv, &index, &value) ||
                !parse_size(value, &options->report_every)) {
                return false;
            }
            options->report_every_set = true;
        } else if (strcmp(argument, "--threads") == 0) {
            if (options->threads_set ||
                !option_value(argc, argv, &index, &value) ||
                !parse_size(value, &options->threads)) {
                return false;
            }
            options->threads_set = true;
        } else if (strcmp(argument, "--seed") == 0) {
            if (options->seed_set ||
                !option_value(argc, argv, &index, &value) ||
                !parse_seed(value, &options->seed)) {
                return false;
            }
            options->seed_set = true;
        } else if (strcmp(argument, "--reserve-information-sets") == 0) {
            if (options->reserve_information_sets_set ||
                !option_value(argc, argv, &index, &value) ||
                !parse_size(value, &options->reserve_information_sets)) {
                return false;
            }
            options->reserve_information_sets_set = true;
        } else if (strcmp(argument, "--format") == 0) {
            if (options->format_set ||
                !option_value(argc, argv, &index, &value)) {
                return false;
            }
            if (strcmp(value, "single-bet") == 0) {
                options->format = CLI_FORMAT_SINGLE_BET;
            } else if (strcmp(value, "raises-to-eight") == 0) {
                options->format = CLI_FORMAT_RAISES_TO_EIGHT;
            } else if (strcmp(value, "ordago") == 0) {
                options->format = CLI_FORMAT_ORDAGO;
            } else {
                return false;
            }
            options->format_set = true;
        } else if (strcmp(argument, "--load") == 0) {
            if (options->load_path != NULL ||
                !option_value(argc, argv, &index, &options->load_path)) {
                return false;
            }
        } else if (strcmp(argument, "--load-compressed") == 0) {
            if (options->load_path != NULL ||
                !option_value(argc, argv, &index, &options->load_path)) {
                return false;
            }
            options->load_compressed = true;
        } else if (strcmp(argument, "--save") == 0) {
            if (options->save_path != NULL ||
                !option_value(argc, argv, &index, &options->save_path)) {
                return false;
            }
        } else if (strcmp(argument, "--save-compressed") == 0) {
            if (options->save_path != NULL ||
                !option_value(argc, argv, &index, &options->save_path)) {
                return false;
            }
            options->save_compressed = true;
        } else if (strcmp(argument, "--export-strategy") == 0) {
            if (options->export_path != NULL ||
                !option_value(argc, argv, &index, &options->export_path)) {
                return false;
            }
        } else if (strcmp(argument, "--evaluate") == 0) {
            if (options->evaluate)
                return false;
            options->evaluate = true;
        } else {
            return false;
        }
    }
    if (options->help)
        return true;
    if (!options->iterations_set && !options->evaluate &&
        !(options->load_path != NULL && options->export_path != NULL))
        return false;
    if ((options->report_every_set || options->threads_set) &&
        !options->iterations_set) {
        return false;
    }
    if (options->seed_set && options->load_path != NULL)
        return false;
    if (options->save_path != NULL && !options->iterations_set)
        return false;
    if (!options->iterations_set)
        options->report_every = 0;
    else if (!options->report_every_set)
        options->report_every = options->iterations;
    return true;
}

static const char *status_name(Status status) {
    switch (status) {
    case CFR_STATUS_SUCCESS:
        return "success";
    case CFR_STATUS_INVALID_ARGUMENT:
        return "invalid argument";
    case CFR_STATUS_ILLEGAL_ACTION:
        return "illegal action";
    case CFR_STATUS_BUFFER_TOO_SMALL:
        return "buffer too small";
    case CFR_STATUS_NUMERIC_ERROR:
        return "numeric error";
    case CFR_STATUS_OUT_OF_MEMORY:
        return "out of memory";
    case CFR_STATUS_NOT_FOUND:
        return "not found";
    case CFR_STATUS_IO_ERROR:
        return "I/O error";
    case CFR_STATUS_FORMAT_ERROR:
        return "invalid checkpoint format";
    case CFR_STATUS_INCOMPATIBLE_GAME:
        return "incompatible game";
    }
    return "unknown error";
}

static size_t automatic_information_set_reservation(
    const CliOptions *options) {
    if (options->format != CLI_FORMAT_ORDAGO ||
        !options->iterations_set || options->load_path != NULL) {
        return 0;
    }
    return mus_cli_ordago_reservation(options->iterations);
}

static bool elapsed_seconds(const struct timespec *start,
                            const struct timespec *end, double *seconds_out) {
    const time_t seconds = end->tv_sec - start->tv_sec;
    const long nanoseconds = end->tv_nsec - start->tv_nsec;

    *seconds_out = (double)seconds + (double)nanoseconds / 1000000000.0;
    return *seconds_out >= 0.0;
}

static size_t saturated_add(size_t left, size_t right) {
    return left > SIZE_MAX - right ? SIZE_MAX : left + right;
}

static bool aggregate_trainers(const Trainer *trainers, size_t thread_count,
                               size_t *training_iterations_out,
                               TrainerStats *stats_out) {
    size_t training_iterations = 0;
    TrainerStats total = {0};

    if (trainers == NULL || thread_count == 0 ||
        training_iterations_out == NULL || stats_out == NULL) {
        return false;
    }
    for (size_t index = 0; index < thread_count; index += 1) {
        TrainerStats current;

        if (cfr_trainer_get_stats(&trainers[index], &current) !=
            CFR_STATUS_SUCCESS) {
            return false;
        }
        training_iterations = saturated_add(
            training_iterations, trainers[index].training_iterations);
        total.iterations = saturated_add(total.iterations, current.iterations);
        total.traversals =
            saturated_add(total.traversals, current.traversals);
        total.visited_nodes =
            saturated_add(total.visited_nodes, current.visited_nodes);
        total.errors = saturated_add(total.errors, current.errors);
    }
    *training_iterations_out = training_iterations;
    *stats_out = total;
    return true;
}

static uint64_t parallel_worker_seed(uint64_t base_seed,
                                     size_t worker_index) {
    uint64_t value =
        base_seed + UINT64_C(0x9e3779b97f4a7c15) * (uint64_t)worker_index;

    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

typedef struct {
    atomic_bool *start;
    atomic_bool *cancel;
    Trainer *trainer;
    size_t iterations;
    Status status;
    bool concurrent;
} TrainingWorker;

static void *run_training_worker(void *raw_worker) {
    TrainingWorker *worker = raw_worker;

    while (!atomic_load_explicit(worker->start, memory_order_acquire))
        (void)sched_yield();
    if (!atomic_load_explicit(worker->cancel, memory_order_acquire)) {
        worker->status = worker->concurrent
                             ? cfr_trainer_run_concurrent(
                                   worker->trainer, worker->iterations)
                             : cfr_trainer_run(worker->trainer,
                                               worker->iterations);
    }
    return NULL;
}

/*
 * Runs one reporting block. The main thread handles worker zero while each
 * remaining active worker owns one pthread. A start gate prevents a partial
 * pthread_create failure from applying only part of the requested block.
 */
static int run_training_block(Trainer *trainers, size_t thread_count,
                              size_t iterations, pthread_t *thread_handles,
                              TrainingWorker *workers, Status *status_out) {
    atomic_bool start;
    atomic_bool cancel;
    const size_t active_count =
        thread_count < iterations ? thread_count : iterations;
    const size_t iterations_per_worker = iterations / active_count;
    const size_t extra_iterations = iterations % active_count;
    size_t created = 0;
    int thread_error = 0;

    if (trainers == NULL || thread_count == 0 || iterations == 0 ||
        workers == NULL || status_out == NULL ||
        (active_count > 1 && thread_handles == NULL)) {
        return EINVAL;
    }
    atomic_init(&start, false);
    atomic_init(&cancel, false);
    for (size_t index = 0; index < active_count; index += 1) {
        workers[index] = (TrainingWorker){
            .start = &start,
            .cancel = &cancel,
            .trainer = &trainers[index],
            .iterations = iterations_per_worker +
                          (index < extra_iterations ? 1 : 0),
            .status = CFR_STATUS_SUCCESS,
            .concurrent = active_count > 1,
        };
    }
    for (size_t index = 1; index < active_count; index += 1) {
        thread_error = pthread_create(&thread_handles[index - 1], NULL,
                                      run_training_worker, &workers[index]);
        if (thread_error != 0)
            break;
        created += 1;
    }
    if (thread_error != 0)
        atomic_store_explicit(&cancel, true, memory_order_release);
    atomic_store_explicit(&start, true, memory_order_release);
    if (thread_error == 0) {
        workers[0].status = workers[0].concurrent
                                ? cfr_trainer_run_concurrent(
                                      workers[0].trainer,
                                      workers[0].iterations)
                                : cfr_trainer_run(workers[0].trainer,
                                                  workers[0].iterations);
    }
    for (size_t index = 0; index < created; index += 1) {
        const int join_error = pthread_join(thread_handles[index], NULL);

        if (thread_error == 0 && join_error != 0)
            thread_error = join_error;
    }
    if (thread_error != 0)
        return thread_error;
    *status_out = CFR_STATUS_SUCCESS;
    for (size_t index = 0; index < active_count; index += 1) {
        if (workers[index].status != CFR_STATUS_SUCCESS) {
            *status_out = workers[index].status;
            break;
        }
    }
    return 0;
}

static Status load_checkpoint(const char *path, const Game *game,
                              GameState *state, InfoStore *store,
                              Trainer *trainer, bool compressed) {
    FILE *stream = fopen(path, "rb");
    Status status;

    if (stream == NULL)
        return CFR_STATUS_IO_ERROR;
    status = compressed
                 ? cfr_checkpoint_read_zstd(stream, game, state, store,
                                            trainer)
                 : cfr_checkpoint_read(stream, game, state, store, trainer);
    if (fclose(stream) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    return status;
}

static Status save_checkpoint(const char *path, const Trainer *trainer,
                              bool compressed) {
    const size_t length = strlen(path);
    char *temporary_path;
    FILE *stream = NULL;
    Status status;

    if (length > SIZE_MAX - 32)
        return CFR_STATUS_INVALID_ARGUMENT;
    temporary_path = malloc(length + 32);
    if (temporary_path == NULL)
        return CFR_STATUS_OUT_OF_MEMORY;
    for (unsigned int attempt = 0; attempt < 1000; attempt += 1) {
        const int written = snprintf(temporary_path, length + 32,
                                     "%s.tmp.%u", path, attempt);

        if (written < 0 || (size_t)written >= length + 32) {
            free(temporary_path);
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        errno = 0;
        stream = fopen(temporary_path, "wbx");
        if (stream != NULL)
            break;
        if (errno != EEXIST) {
            free(temporary_path);
            return CFR_STATUS_IO_ERROR;
        }
    }
    if (stream == NULL) {
        free(temporary_path);
        return CFR_STATUS_IO_ERROR;
    }
    status = compressed ? cfr_checkpoint_write_zstd(stream, trainer)
                        : cfr_checkpoint_write(stream, trainer);
    if (fclose(stream) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    if (status == CFR_STATUS_SUCCESS && rename(temporary_path, path) != 0)
        status = CFR_STATUS_IO_ERROR;
    if (status != CFR_STATUS_SUCCESS)
        (void)remove(temporary_path);
    free(temporary_path);
    return status;
}

static Status export_strategy(const char *path, const Trainer *trainer,
                              CliFormat format) {
    FILE *stream = fopen(path, "wx");
    Status status;

    if (stream == NULL)
        return CFR_STATUS_IO_ERROR;
    if (format == CLI_FORMAT_RAISES_TO_EIGHT) {
        status = mus_raises_to_eight_strategy_write_text(stream, trainer);
    } else if (format == CLI_FORMAT_ORDAGO) {
        status = mus_ordago_strategy_write_text(stream, trainer);
    } else {
        status = mus_single_bet_strategy_write_text(stream, trainer);
    }
    if (fclose(stream) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    if (status != CFR_STATUS_SUCCESS)
        (void)remove(path);
    return status;
}

static bool print_report(const Trainer *trainers, size_t thread_count,
                         const InfoStore *store,
                         const struct timespec *start) {
    TrainerStats trainer_stats;
    InfoStoreStats store_stats;
    struct timespec now;
    size_t training_iterations;
    double seconds;

    if (!aggregate_trainers(trainers, thread_count, &training_iterations,
                            &trainer_stats) ||
        cfr_info_store_get_stats(store, &store_stats) != CFR_STATUS_SUCCESS ||
        timespec_get(&now, TIME_UTC) != TIME_UTC ||
        !elapsed_seconds(start, &now, &seconds)) {
        return false;
    }
    return printf("report iterations=%zu threads=%zu information_sets=%zu "
                  "traversals=%zu visited_nodes=%zu seconds=%.6f\n",
                  training_iterations, thread_count, store_stats.size,
                  trainer_stats.traversals, trainer_stats.visited_nodes,
                  seconds) >= 0;
}

static bool print_evaluation(size_t iterations, CliFormat format,
                             const EvaluationMetrics *metrics) {
    return printf(
               "evaluation scope=%s iterations=%zu "
               "profile_value_player_0=%.17g "
               "profile_value_player_1=%.17g "
               "best_response_value_player_0=%.17g "
               "best_response_value_player_1=%.17g "
               "improvement_player_0=%.17g improvement_player_1=%.17g "
               "nash_conv=%.17g exploitability=%.17g\n",
               format == CLI_FORMAT_ORDAGO ? "bounded-final-lances"
                                            : "root",
               iterations, metrics->profile_value_player_0,
               metrics->profile_value_player_1,
               metrics->best_response_value_player_0,
               metrics->best_response_value_player_1,
               metrics->improvement_player_0,
               metrics->improvement_player_1, metrics->nash_conv,
               metrics->exploitability) >= 0;
}

static int run(const CliOptions *options) {
    MusGameState *states = NULL;
    InfoStore store = {0};
    Trainer *trainers = NULL;
    TrainingWorker *workers = NULL;
    pthread_t *thread_handles = NULL;
    const Game *game;
    bool store_initialized = false;
    int result = EXIT_RUNTIME_ERROR;
    Status status;

    if (options->format == CLI_FORMAT_RAISES_TO_EIGHT) {
        game = mus_raises_to_eight_game_descriptor();
    } else if (options->format == CLI_FORMAT_ORDAGO) {
        game = mus_ordago_game_descriptor();
    } else {
        game = mus_single_bet_game_descriptor();
    }
    states = calloc(options->threads, sizeof(*states));
    trainers = calloc(options->threads, sizeof(*trainers));
    workers = calloc(options->threads, sizeof(*workers));
    if (options->threads > 1) {
        thread_handles =
            calloc(options->threads - 1, sizeof(*thread_handles));
    }
    if (states == NULL || trainers == NULL || workers == NULL ||
        (options->threads > 1 && thread_handles == NULL)) {
        status = CFR_STATUS_OUT_OF_MEMORY;
        goto runtime_error;
    }
    for (size_t index = 0; index < options->threads; index += 1) {
        if (options->format == CLI_FORMAT_RAISES_TO_EIGHT) {
            status = mus_raises_to_eight_game_state_init(&states[index]);
        } else if (options->format == CLI_FORMAT_ORDAGO) {
            status = mus_ordago_game_state_init(&states[index]);
        } else {
            status = mus_single_bet_game_state_init(&states[index]);
        }
        if (status != CFR_STATUS_SUCCESS)
            goto runtime_error;
    }
    if (options->load_path != NULL) {
        status = load_checkpoint(options->load_path, game,
                                 mus_game_state_as_game_state(&states[0]),
                                 &store, &trainers[0],
                                 options->load_compressed);
        if (status != CFR_STATUS_SUCCESS) {
            fprintf(stderr, "error: no se pudo cargar '%s': %s\n",
                    options->load_path, status_name(status));
            goto cleanup;
        }
        store_initialized = true;
    } else {
        status = cfr_info_store_init(&store);
        if (status != CFR_STATUS_SUCCESS)
            goto runtime_error;
        store_initialized = true;
        status = cfr_trainer_init_mccfr(
            &trainers[0], game,
            mus_game_state_as_game_state(&states[0]), &store, options->seed);
        if (status != CFR_STATUS_SUCCESS)
            goto runtime_error;
    }
    if (options->threads > 1 &&
        trainers[0].variant != CFR_TRAINER_VARIANT_MCCFR_EXTERNAL) {
        fprintf(stderr,
                "error: el entrenamiento con varios hilos requiere un "
                "checkpoint MCCFR\n");
        goto cleanup;
    }
    const uint64_t parallel_seed_base = trainers[0].mccfr_rng.state;

    for (size_t index = 1; index < options->threads; index += 1) {
        status = cfr_trainer_init_mccfr(
            &trainers[index], game,
            mus_game_state_as_game_state(&states[index]), &store,
            parallel_worker_seed(parallel_seed_base, index));
        if (status != CFR_STATUS_SUCCESS)
            goto runtime_error;
    }
    if (options->threads > 1) {
        status = cfr_info_store_prepare_concurrent(&store);
        if (status != CFR_STATUS_SUCCESS)
            goto runtime_error;
    }
    const size_t reservation = options->reserve_information_sets_set
                                   ? options->reserve_information_sets
                                   : automatic_information_set_reservation(
                                         options);
    if (reservation > 0) {
        status = cfr_info_store_reserve(&store, reservation);
        if (status != CFR_STATUS_SUCCESS)
            goto runtime_error;
    }

    if (options->iterations_set) {
        struct timespec start;
        size_t completed = 0;

        if (timespec_get(&start, TIME_UTC) != TIME_UTC)
            goto runtime_error;
        while (completed < options->iterations) {
            const size_t remaining = options->iterations - completed;
            const size_t block = options->report_every < remaining
                                     ? options->report_every
                                     : remaining;
            int thread_error;

            thread_error = run_training_block(
                trainers, options->threads, block, thread_handles, workers,
                &status);
            if (thread_error != 0) {
                fprintf(stderr,
                        "error: fallo de los hilos de entrenamiento: %s\n",
                        strerror(thread_error));
                goto cleanup;
            }
            if (status != CFR_STATUS_SUCCESS)
                goto runtime_error;
            completed += block;
            if (!print_report(trainers, options->threads, &store, &start)) {
                status = CFR_STATUS_IO_ERROR;
                goto runtime_error;
            }
        }
        if (!aggregate_trainers(trainers, options->threads,
                                &trainers[0].training_iterations,
                                &trainers[0].stats)) {
            status = CFR_STATUS_INVALID_ARGUMENT;
            goto runtime_error;
        }
    }

    if (options->save_path != NULL) {
        status = save_checkpoint(options->save_path, &trainers[0],
                                 options->save_compressed);
        if (status != CFR_STATUS_SUCCESS) {
            fprintf(stderr, "error: no se pudo guardar '%s': %s\n",
                    options->save_path, status_name(status));
            goto cleanup;
        }
    }
    if (options->evaluate) {
        EvaluationMetrics metrics;

        if (options->format == CLI_FORMAT_RAISES_TO_EIGHT) {
            status = mus_raises_to_eight_evaluation_metrics(&store,
                                                             &metrics);
        } else if (options->format == CLI_FORMAT_ORDAGO) {
            status = mus_ordago_evaluation_metrics(&store, &metrics);
        } else {
            status = mus_single_bet_evaluation_metrics(&store, &metrics);
        }
        if (status != CFR_STATUS_SUCCESS)
            goto runtime_error;
        if (!print_evaluation(trainers[0].training_iterations,
                              options->format, &metrics)) {
            status = CFR_STATUS_IO_ERROR;
            goto runtime_error;
        }
    }
    if (options->export_path != NULL) {
        status = export_strategy(options->export_path, &trainers[0],
                                 options->format);
        if (status != CFR_STATUS_SUCCESS) {
            fprintf(stderr, "error: no se pudo exportar '%s': %s\n",
                    options->export_path, status_name(status));
            goto cleanup;
        }
    }
    result = 0;
    goto cleanup;

runtime_error:
    fprintf(stderr, "error: fallo del solver: %s\n", status_name(status));

cleanup:
    if (store_initialized &&
        cfr_info_store_destroy(&store) != CFR_STATUS_SUCCESS) {
        result = EXIT_RUNTIME_ERROR;
    }
    free(thread_handles);
    free(workers);
    free(trainers);
    free(states);
    return result;
}

int main(int argc, char *argv[]) {
    CliOptions options;
    const char *program = argc > 0 && argv[0] != NULL ? argv[0] : "mus-solver";

    if (!parse_options(argc, argv, &options)) {
        (void)print_usage(stderr, program);
        return EXIT_USAGE_ERROR;
    }
    if (options.help) {
        return print_usage(stdout, program) ? 0 : EXIT_RUNTIME_ERROR;
    }
    return run(&options);
}
