#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cfr/checkpoint.h"
#include "cfr/info_store.h"
#include "cfr/trainer.h"
#include "mus_solver/mus_evaluation.h"
#include "mus_solver/mus_game.h"
#include "mus_solver/mus_strategy.h"
#include "mus_solver/mus_score.h"
#include "reservation_internal.h"

enum { EXIT_RUNTIME_ERROR = 1, EXIT_USAGE_ERROR = 2 };

typedef enum {
    CLI_FORMAT_SINGLE_BET,
    CLI_FORMAT_RAISES_TO_EIGHT,
    CLI_FORMAT_ORDAGO,
    CLI_FORMAT_ORDAGO_SCORE
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
    const char *continuation_dir;
    size_t score_target;
    size_t evaluation_samples;
    unsigned int score_a;
    unsigned int score_b;
    bool score_set;
    bool target_set;
    bool evaluation_samples_set;
    const char *preload_ordago_path;
    bool preload_ordago_compressed;
    double preload_weight;
    bool preload_weight_set;
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
               "  --threads N            Hilos de entrenamiento (1 por defecto;\n"
               "                          ordago-score admite 1).\n"
               "  --seed N               Semilla de un entrenamiento nuevo (0 por "
               "defecto).\n"
               "  --format FORMAT        single-bet (por defecto) o "
               "raises-to-eight, ordago u ordago-score.\n"
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
               "  --continuation DIR     Tabla y checkpoints de ordago-score.\n"
               "  --score A:B            Marcador inicial (0:0 por defecto).\n"
               "  --target N             Objetivo de ordago-score (40 por defecto).\n"
               "  --evaluation-samples N Manos para evaluar cada celda (10000).\n"
               "                          En ordago-score, --iterations se aplica\n"
               "                          a cada celda pendiente; se guarda por diagonal.\n"
               "                          Con --load[-compressed], se suman solo\n"
               "                          al checkpoint del marcador elegido.\n"
               "                          Requiere --save[-compressed] fuera de DIR;\n"
               "                          conserva values.txt y admite --report-every.\n"
               "  --preload-ordago FILE   Precarga la estrategia media de ordago.\n"
               "  --preload-ordago-compressed FILE\n"
               "                          Precarga desde un checkpoint Zstandard.\n"
               "  --preload-weight X     Peso inicial por nodo (1 por defecto).\n"
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

static bool parse_positive_double(const char *text, double *result) {
    char *end;
    errno = 0;
    const double value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !isfinite(value) || value <= 0)
        return false;
    *result = value;
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
    *options = (CliOptions){.threads = 1, .score_target = 40,
                            .evaluation_samples = 10000,
                            .preload_weight = MUS_SCORE_PRELOAD_DEFAULT_WEIGHT};
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
            } else if (strcmp(value, "ordago-score") == 0) {
                options->format = CLI_FORMAT_ORDAGO_SCORE;
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
        } else if (strcmp(argument, "--continuation") == 0) {
            if (options->continuation_dir != NULL ||
                !option_value(argc, argv, &index, &options->continuation_dir))
                return false;
        } else if (strcmp(argument, "--score") == 0) {
            if (options->score_set || !option_value(argc, argv, &index, &value))
                return false;
            const char *separator = strchr(value, ':');
            if (separator == NULL || separator == value || separator - value > 2)
                return false;
            char first[3] = {0};
            memcpy(first, value, (size_t)(separator - value));
            uint64_t a, b;
            if (!parse_seed(first, &a) || !parse_seed(separator + 1, &b) ||
                a >= MUS_ORDAGO_WAGER || b >= MUS_ORDAGO_WAGER)
                return false;
            options->score_a = (unsigned int)a;
            options->score_b = (unsigned int)b;
            options->score_set = true;
        } else if (strcmp(argument, "--target") == 0) {
            if (options->target_set || !option_value(argc, argv, &index, &value) ||
                !parse_size(value, &options->score_target) ||
                options->score_target > MUS_ORDAGO_WAGER)
                return false;
            options->target_set = true;
        } else if (strcmp(argument, "--evaluation-samples") == 0) {
            if (options->evaluation_samples_set ||
                !option_value(argc, argv, &index, &value) ||
                !parse_size(value, &options->evaluation_samples))
                return false;
            options->evaluation_samples_set = true;
        } else if (strcmp(argument, "--preload-ordago") == 0 ||
                   strcmp(argument, "--preload-ordago-compressed") == 0) {
            if (options->preload_ordago_path != NULL ||
                !option_value(argc, argv, &index, &options->preload_ordago_path) ||
                options->preload_ordago_path[0] == '\0')
                return false;
            options->preload_ordago_compressed =
                strcmp(argument, "--preload-ordago-compressed") == 0;
        } else if (strcmp(argument, "--preload-weight") == 0) {
            if (options->preload_weight_set ||
                !option_value(argc, argv, &index, &value) ||
                !parse_positive_double(value, &options->preload_weight))
                return false;
            options->preload_weight_set = true;
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
    if (options->format == CLI_FORMAT_ORDAGO_SCORE) {
        if (options->continuation_dir == NULL ||
            options->continuation_dir[0] == '\0' ||
            options->score_a >= options->score_target ||
            options->score_b >= options->score_target || options->threads != 1)
            return false;
        if (options->load_path != NULL) {
            if (options->load_path[0] == '\0' ||
                options->preload_ordago_path != NULL || options->preload_weight_set ||
                (options->iterations_set && (options->seed_set || options->save_path == NULL)) ||
                (!options->iterations_set && (options->save_path != NULL || options->report_every_set)) ||
                (!options->iterations_set && !options->evaluate && options->export_path == NULL))
                return false;
            if (options->iterations_set && !options->report_every_set)
                options->report_every = options->iterations;
            return true;
        }
        return options->save_path == NULL &&
               (options->preload_ordago_path == NULL || options->iterations_set) &&
               (!options->preload_weight_set || options->preload_ordago_path != NULL) &&
               !options->report_every_set &&
               (options->iterations_set || options->evaluate || options->export_path != NULL);
    }
    if (options->continuation_dir != NULL || options->score_set ||
        options->target_set || options->evaluation_samples_set ||
        options->preload_ordago_path != NULL || options->preload_weight_set)
        return false;
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

typedef struct TrainingControl TrainingControl;

typedef struct {
    TrainingControl *control;
    Trainer *trainer;
    size_t iterations;
    size_t interval;
    size_t index;
    size_t ready_blocks;
    Status status;
    bool concurrent;
    bool finished;
} TrainingWorker;

struct TrainingControl {
    atomic_bool start;
    atomic_bool cancel;
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_cond_t resume;
    TrainingWorker *workers;
    Trainer *trainers;
    const InfoStore *store;
    const struct timespec *start_time;
    size_t active_count;
    size_t thread_count;
    size_t iterations;
    size_t report_every;
    size_t completed;
    size_t released_blocks;
    bool stop;
    Status status;
};

static bool print_report(const Trainer *trainers, size_t thread_count,
                         const InfoStore *store,
                         const struct timespec *start);

static void stop_training_locked(TrainingControl *control, Status status) {
    if (!control->stop) {
        control->stop = true;
        control->status = status;
    }
    (void)pthread_cond_broadcast(&control->resume);
    (void)pthread_cond_broadcast(&control->changed);
}

static Status report_training_progress(const Trainer *trainer,
                                       size_t completed, void *context) {
    TrainingWorker *worker = context;
    TrainingControl *control = worker->control;

    (void)trainer;
    (void)completed;
    (void)pthread_mutex_lock(&control->mutex);
    worker->ready_blocks += 1;
    if (worker->index != 0) {
        (void)pthread_cond_broadcast(&control->changed);
        while (!control->stop &&
               control->released_blocks < worker->ready_blocks) {
            if (pthread_cond_wait(&control->resume, &control->mutex) != 0)
                stop_training_locked(control, CFR_STATUS_INVALID_ARGUMENT);
        }
    } else {
        for (;;) {
            bool ready = true;
            for (size_t index = 1; index < control->active_count; index += 1) {
                const TrainingWorker *other = &control->workers[index];
                if (!other->finished &&
                    other->ready_blocks < worker->ready_blocks) {
                    ready = false;
                    break;
                }
            }
            if (ready || control->stop)
                break;
            if (pthread_cond_wait(&control->changed, &control->mutex) != 0)
                stop_training_locked(control, CFR_STATUS_INVALID_ARGUMENT);
        }
        if (!control->stop) {
            const size_t remaining = control->iterations - control->completed;
            control->completed += remaining < control->report_every
                                      ? remaining
                                      : control->report_every;
            if (!print_report(control->trainers, control->thread_count,
                               control->store, control->start_time)) {
                stop_training_locked(control, CFR_STATUS_IO_ERROR);
            } else {
                control->released_blocks = worker->ready_blocks;
                (void)pthread_cond_broadcast(&control->resume);
            }
        }
    }
    const Status status = control->status;
    (void)pthread_mutex_unlock(&control->mutex);
    return status;
}

static void train_worker(TrainingWorker *worker) {
    TrainingControl *control = worker->control;
    const Status status = cfr_trainer_run_with_callback(
        worker->trainer, worker->iterations, worker->interval,
        worker->concurrent, report_training_progress, worker);

    (void)pthread_mutex_lock(&control->mutex);
    worker->status = status;
    worker->finished = true;
    if (status != CFR_STATUS_SUCCESS)
        stop_training_locked(control, status);
    (void)pthread_cond_broadcast(&control->changed);
    (void)pthread_mutex_unlock(&control->mutex);
}

static void *run_training_worker(void *raw_worker) {
    TrainingWorker *worker = raw_worker;
    TrainingControl *control = worker->control;

    while (!atomic_load_explicit(&control->start, memory_order_acquire))
        (void)sched_yield();
    if (!atomic_load_explicit(&control->cancel, memory_order_acquire))
        train_worker(worker);
    return NULL;
}

/*
 * Each worker retains its thread and traversal workspace for the whole run.
 * Callbacks stop every worker at reporting boundaries. A worker with no work
 * in the final partial block may finish before that last boundary.
 * The start gate preserves cancellation after a partial pthread_create error.
 */
static int run_training(Trainer *trainers, size_t thread_count,
                        size_t iterations, size_t report_every,
                        pthread_t *thread_handles, TrainingWorker *workers,
                        const InfoStore *store, const struct timespec *start,
                        Status *status_out) {
    size_t created = 0;
    int thread_error = 0;

    if (trainers == NULL || thread_count == 0 || iterations == 0 ||
        report_every == 0 || workers == NULL || status_out == NULL ||
        store == NULL || start == NULL) {
        return EINVAL;
    }
    size_t active_count = thread_count < iterations ? thread_count : iterations;
    if (active_count > report_every)
        active_count = report_every;
    if (active_count > 1 && thread_handles == NULL)
        return EINVAL;
    TrainingControl control = {
        .workers = workers, .trainers = trainers, .store = store,
        .start_time = start, .active_count = active_count,
        .thread_count = thread_count, .iterations = iterations,
        .report_every = report_every, .status = CFR_STATUS_SUCCESS};
    atomic_init(&control.start, false);
    atomic_init(&control.cancel, false);
    thread_error = pthread_mutex_init(&control.mutex, NULL);
    if (thread_error != 0)
        return thread_error;
    thread_error = pthread_cond_init(&control.changed, NULL);
    if (thread_error != 0) {
        (void)pthread_mutex_destroy(&control.mutex);
        return thread_error;
    }
    thread_error = pthread_cond_init(&control.resume, NULL);
    if (thread_error != 0) {
        (void)pthread_cond_destroy(&control.changed);
        (void)pthread_mutex_destroy(&control.mutex);
        return thread_error;
    }
    const size_t full_blocks = iterations / report_every;
    const size_t remainder = iterations % report_every;
    for (size_t index = 0; index < active_count; index += 1) {
        const size_t interval = report_every / active_count +
                                (index < report_every % active_count ? 1 : 0);
        workers[index] = (TrainingWorker){
            .control = &control,
            .trainer = &trainers[index],
            .iterations = full_blocks * interval + remainder / active_count +
                          (index < remainder % active_count ? 1 : 0),
            .interval = interval,
            .index = index,
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
        atomic_store_explicit(&control.cancel, true, memory_order_release);
    atomic_store_explicit(&control.start, true, memory_order_release);
    if (thread_error == 0)
        train_worker(&workers[0]);
    for (size_t index = 0; index < created; index += 1) {
        const int join_error = pthread_join(thread_handles[index], NULL);

        if (thread_error == 0 && join_error != 0)
            thread_error = join_error;
    }
    *status_out = control.status;
    (void)pthread_cond_destroy(&control.resume);
    (void)pthread_cond_destroy(&control.changed);
    (void)pthread_mutex_destroy(&control.mutex);
    return thread_error;
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
    if (format == CLI_FORMAT_ORDAGO_SCORE) {
        status = mus_score_strategy_write_text(stream, trainer);
    } else if (format == CLI_FORMAT_RAISES_TO_EIGHT) {
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

static char *score_path(const char *directory, const char *name) {
    const size_t size = strlen(directory) + strlen(name) + 2;
    char *path = malloc(size);
    if (path != NULL)
        (void)snprintf(path, size, "%s/%s", directory, name);
    return path;
}

static char *score_cell_path(const char *directory, unsigned int a, unsigned int b) {
    char name[64];
    (void)snprintf(name, sizeof(name), "score-%02u-%02u.cfr.zst", a, b);
    return score_path(directory, name);
}

typedef struct {
    const CliOptions *options;
    const MusScorePreloadStats *preload_stats;
} ScorePersistence;

static Status save_score_cell(const MusScoreGame *game, const Trainer *trainer,
                               const MusScoreCell *cell, void *context) {
    const ScorePersistence *persistence = context;
    const CliOptions *options = persistence->options;
    char *path = score_cell_path(options->continuation_dir,
                                game->initial_scores[0], game->initial_scores[1]);
    if (path == NULL)
        return CFR_STATUS_OUT_OF_MEMORY;
    Status status = save_checkpoint(path, trainer, true);
    free(path);
    if (status == CFR_STATUS_SUCCESS && persistence->preload_stats != NULL) {
        const MusScorePreloadStats *stats = persistence->preload_stats;
        if (printf("score-preload score=%u:%u source_nodes=%zu imported_nodes=%zu "
                   "existing_nodes=%zu expanded_action_nodes=%zu skipped_history_nodes=%zu skipped_illegal_nodes=%zu\n",
                   game->initial_scores[0], game->initial_scores[1],
                   stats->source_nodes, stats->imported_nodes, stats->existing_nodes,
                   stats->expanded_action_nodes, stats->skipped_history_nodes,
                   stats->skipped_illegal_nodes) < 0)
            status = CFR_STATUS_IO_ERROR;
    }
    if (status == CFR_STATUS_SUCCESS &&
        printf("score-cell score=%u:%u target=%u iterations=%zu "
               "evaluation_samples=%zu value=%.17g win_probability=%.17g\n",
               game->initial_scores[0], game->initial_scores[1],
               game->continuation->target, cell->iterations, cell->evaluation_samples,
               cell->value, (cell->value + 1) / 2) < 0)
        status = CFR_STATUS_IO_ERROR;
    (void)fflush(stdout);
    return status;
}

static Status save_score_diagonal(const MusScoreTable *table, void *context) {
    const CliOptions *options = ((const ScorePersistence *)context)->options;
    char *path = score_path(options->continuation_dir, "values.txt");
    char *temporary = score_path(options->continuation_dir, "values.txt.tmp");
    if (path == NULL || temporary == NULL) {
        free(path);
        free(temporary);
        return CFR_STATUS_OUT_OF_MEMORY;
    }
    FILE *stream = fopen(temporary, "w");
    Status status = stream == NULL ? CFR_STATUS_IO_ERROR
                                  : mus_score_table_write(stream, table);
    if (stream != NULL && fclose(stream) != 0)
        status = CFR_STATUS_IO_ERROR;
    if (status == CFR_STATUS_SUCCESS && rename(temporary, path) != 0)
        status = CFR_STATUS_IO_ERROR;
    free(path);
    free(temporary);
    return status;
}

static Status write_score_lock(FILE *stream) {
    char hostname[256] = "unknown";
    char timestamp[32];
    const time_t now = time(NULL);
    struct tm utc;
    if (now == (time_t)-1 || gmtime_r(&now, &utc) == NULL ||
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0)
        return CFR_STATUS_IO_ERROR;
    if (gethostname(hostname, sizeof(hostname)) != 0)
        strcpy(hostname, "unknown");
    hostname[sizeof(hostname) - 1] = '\0';
    return fprintf(stream, "pid=%jd host=%s started=%s\n",
                   (intmax_t)getpid(), hostname, timestamp) < 0
               ? CFR_STATUS_IO_ERROR : CFR_STATUS_SUCCESS;
}

static void report_score_lock(const char *path) {
    FILE *stream = fopen(path, "r");
    if (stream == NULL)
        return;
    char owner[512];
    if (fgets(owner, sizeof(owner), stream) != NULL) {
        owner[strcspn(owner, "\r\n")] = '\0';
        fprintf(stderr, "error: datos del bloqueo: %s\n", owner);
    }
    (void)fclose(stream);
}

static bool print_score_resume(const MusScoreTable *table, const CliOptions *options) {
    size_t count = 0, mismatched = 0;
    size_t min_iterations = SIZE_MAX, max_iterations = 0;
    size_t min_samples = SIZE_MAX, max_samples = 0;
    for (unsigned int a = 0; a < table->target; a += 1) {
        for (unsigned int b = 0; b < table->target; b += 1) {
            const MusScoreCell *cell = &table->cells[a][b];
            if (!cell->ready)
                continue;
            count += 1;
            if (cell->iterations < min_iterations)
                min_iterations = cell->iterations;
            if (cell->iterations > max_iterations)
                max_iterations = cell->iterations;
            if (cell->evaluation_samples < min_samples)
                min_samples = cell->evaluation_samples;
            if (cell->evaluation_samples > max_samples)
                max_samples = cell->evaluation_samples;
            const uint64_t seed = options->seed + a * MUS_ORDAGO_WAGER + b;
            if (cell->iterations != options->iterations ||
                cell->evaluation_samples != options->evaluation_samples ||
                cell->training_seed != seed)
                mismatched += 1;
        }
    }
    return count == 0 ||
           printf("score-resume cells=%zu iterations_min=%zu iterations_max=%zu "
                  "samples_min=%zu samples_max=%zu requested_iterations=%zu "
                  "requested_samples=%zu different_settings_cells=%zu\n",
                  count, min_iterations, max_iterations, min_samples, max_samples,
                  options->iterations, options->evaluation_samples, mismatched) >= 0;
}

/* Standalone outputs must not replace files managed by the coordinator. */
static Status check_score_output_path(const char *directory, const char *path) {
    if (path == NULL)
        return CFR_STATUS_SUCCESS;
    if (path[0] == '\0')
        return CFR_STATUS_INVALID_ARGUMENT;
    char *root = realpath(directory, NULL);
    if (root == NULL)
        return CFR_STATUS_IO_ERROR;
    char *output = realpath(path, NULL);
    if (output == NULL && errno == ENOENT) {
        char *parent = strdup(path);
        if (parent == NULL) {
            free(root);
            return CFR_STATUS_OUT_OF_MEMORY;
        }
        char *slash = strrchr(parent, '/');
        if (slash != NULL) {
            if (slash == parent)
                slash[1] = '\0';
            else
                *slash = '\0';
        }
        output = realpath(slash == NULL ? "." : parent, NULL);
        free(parent);
    }
    Status status = CFR_STATUS_IO_ERROR;
    if (output != NULL) {
        const size_t length = strlen(root);
        const bool inside = strcmp(root, "/") == 0 ||
            (strncmp(root, output, length) == 0 &&
             (output[length] == '\0' || output[length] == '/'));
        status = inside ? CFR_STATUS_INVALID_ARGUMENT : CFR_STATUS_SUCCESS;
        if (inside)
            fprintf(stderr, "error: guarde '%s' fuera de --continuation; "
                            "los checkpoints publicados y values.txt deben conservarse\n", path);
    }
    free(output);
    free(root);
    return status;
}

static Status report_score_checkpoint(const Trainer *trainer, size_t completed,
                                      void *context) {
    (void)completed;
    return print_report(trainer, 1, trainer->store, context)
               ? CFR_STATUS_SUCCESS : CFR_STATUS_IO_ERROR;
}

/* Resume one checkpoint against an immutable snapshot of continuation values.
 * The selected cell need not be published. Only higher diagonals are required. */
static int run_score_checkpoint(const CliOptions *options) {
    MusScoreTable table;
    MusScoreGame game;
    InfoStore store = {0};
    Trainer trainer;
    MusGameState *state = NULL;
    char *path = score_path(options->continuation_dir, "values.txt");
    Status status = path == NULL ? CFR_STATUS_OUT_OF_MEMORY : CFR_STATUS_SUCCESS;
    if (status != CFR_STATUS_SUCCESS)
        goto done;
    status = check_score_output_path(options->continuation_dir, options->save_path);
    if (status == CFR_STATUS_SUCCESS)
        status = check_score_output_path(options->continuation_dir, options->export_path);
    if (status != CFR_STATUS_SUCCESS)
        goto done;
    FILE *stream = fopen(path, "r");
    if (stream == NULL) {
        status = CFR_STATUS_IO_ERROR;
        goto done;
    }
    status = mus_score_table_read(stream, &table);
    if (fclose(stream) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    if (status != CFR_STATUS_SUCCESS)
        goto done;
    if (table.target != options->score_target) {
        status = CFR_STATUS_INCOMPATIBLE_GAME;
        goto done;
    }
    const unsigned int sum = options->score_a + options->score_b;
    for (unsigned int a = 0; a < table.target; a += 1) {
        for (unsigned int b = 0; b < table.target; b += 1) {
            if (a + b > sum && !table.cells[a][b].ready) {
                fprintf(stderr, "error: falta el valor de continuacion %u:%u\n", a, b);
                status = CFR_STATUS_NOT_FOUND;
                goto done;
            }
        }
    }
    state = malloc(sizeof(*state));
    if (state == NULL) {
        status = CFR_STATUS_OUT_OF_MEMORY;
        goto done;
    }
    status = mus_score_game_init(&game, &table, options->score_a, options->score_b);
    if (status == CFR_STATUS_SUCCESS)
        status = mus_score_game_state_init(state, &game);
    if (status == CFR_STATUS_SUCCESS)
        status = load_checkpoint(options->load_path, &game.game,
                                 mus_game_state_as_game_state(state), &store,
                                 &trainer, options->load_compressed);
    if (status != CFR_STATUS_SUCCESS)
        goto done;
    const size_t initial_iterations = trainer.training_iterations;
    if (options->iterations > SIZE_MAX - initial_iterations) {
        status = CFR_STATUS_INVALID_ARGUMENT;
        goto done;
    }
    if (options->reserve_information_sets_set)
        status = cfr_info_store_reserve(&store, options->reserve_information_sets);
    if (status != CFR_STATUS_SUCCESS)
        goto done;
    if (options->iterations_set) {
        struct timespec start;
        if (timespec_get(&start, TIME_UTC) != TIME_UTC) {
            status = CFR_STATUS_IO_ERROR;
            goto done;
        }
        status = cfr_trainer_run_with_callback(
            &trainer, options->iterations, options->report_every, false,
            report_score_checkpoint, &start);
        if (status != CFR_STATUS_SUCCESS)
            goto done;
    }
    if (options->save_path != NULL)
        status = save_checkpoint(options->save_path, &trainer, options->save_compressed);
    if (status != CFR_STATUS_SUCCESS)
        goto done;
    if (printf("score-checkpoint score=%u:%u target=%u initial_iterations=%zu "
               "added_iterations=%zu total_iterations=%zu continuation=frozen\n",
               options->score_a, options->score_b, table.target, initial_iterations,
               options->iterations, trainer.training_iterations) < 0) {
        status = CFR_STATUS_IO_ERROR;
        goto done;
    }
    if (options->evaluate) {
        double value;
        status = mus_score_evaluate(&game, &store, options->evaluation_samples,
                                    options->seed, &value);
        if (status == CFR_STATUS_SUCCESS &&
            printf("evaluation scope=score-checkpoint-average-policy score=%u:%u "
                   "iterations=%zu samples=%zu estimate=fresh seed=%" PRIu64 " "
                   "profile_value_player_0=%.17g win_probability=%.17g\n",
                   options->score_a, options->score_b, trainer.training_iterations,
                   options->evaluation_samples, options->seed, value, (value + 1) / 2) < 0)
            status = CFR_STATUS_IO_ERROR;
    }
    if (status == CFR_STATUS_SUCCESS && options->export_path != NULL)
        status = export_strategy(options->export_path, &trainer, options->format);
done:
    (void)cfr_info_store_destroy(&store);
    free(state);
    free(path);
    if (status != CFR_STATUS_SUCCESS)
        fprintf(stderr, "error: checkpoint ordago-score: %s\n", status_name(status));
    return status == CFR_STATUS_SUCCESS ? 0 : EXIT_RUNTIME_ERROR;
}

static int run_score(const CliOptions *options) {
    if (options->load_path != NULL)
        return run_score_checkpoint(options);
    MusScoreTable table;
    Status status = mus_score_table_init(&table, (unsigned int)options->score_target);
    InfoStore store = {0};
    bool store_initialized = false;
    MusGameState state;
    MusScoreGame game;
    Trainer trainer;
    InfoStore preload_store = {0};
    MusGameState *preload_state = NULL;
    Trainer preload_trainer;
    MusScorePreloadStats preload_stats;
    const MusScorePreloadOptions preload = {
        .source = &preload_trainer, .weight = options->preload_weight,
        .uniform_mix = MUS_SCORE_PRELOAD_DEFAULT_UNIFORM_MIX,
    };
    ScorePersistence persistence = {
        .options = options,
        .preload_stats = options->preload_ordago_path == NULL ? NULL : &preload_stats,
    };
    char *path = score_path(options->continuation_dir, "values.txt");
    char *lock_path = score_path(options->continuation_dir, "training.lock");
    bool locked = false;
    if (path == NULL || lock_path == NULL) {
        status = CFR_STATUS_OUT_OF_MEMORY;
        goto done;
    }
    if (options->preload_ordago_path != NULL) {
        preload_state = malloc(sizeof(*preload_state));
        status = preload_state == NULL ? CFR_STATUS_OUT_OF_MEMORY
                                        : mus_ordago_game_state_init(preload_state);
        if (status == CFR_STATUS_SUCCESS)
            status = load_checkpoint(options->preload_ordago_path, mus_ordago_game_descriptor(),
                                      mus_game_state_as_game_state(preload_state), &preload_store,
                                      &preload_trainer, options->preload_ordago_compressed);
        if (status != CFR_STATUS_SUCCESS) {
            fprintf(stderr, "error: no se pudo precargar '%s': %s\n",
                    options->preload_ordago_path, status_name(status));
            goto done;
        }
        if (printf("score-preload-source path=%s schema=%s source_iterations=%zu "
                   "weight=%.17g uniform_mix=%.17g\n",
                   options->preload_ordago_path, preload_trainer.game->strategy_schema_id,
                   preload_trainer.training_iterations, preload.weight, preload.uniform_mix) < 0) {
            status = CFR_STATUS_IO_ERROR;
            goto done;
        }
    }
    if (options->iterations_set) {
        if (mkdir(options->continuation_dir, 0777) != 0 && errno != EEXIST) {
            status = CFR_STATUS_IO_ERROR;
            goto done;
        }
        FILE *lock = fopen(lock_path, "wx");
        if (lock == NULL) {
            const bool exists = errno == EEXIST;
            fprintf(stderr, "error: no se pudo crear '%s'; "
                            "compruebe si hay otro entrenamiento\n", lock_path);
            if (exists)
                report_score_lock(lock_path);
            status = CFR_STATUS_IO_ERROR;
            goto done;
        }
        locked = true;
        status = write_score_lock(lock);
        if (fclose(lock) != 0)
            status = CFR_STATUS_IO_ERROR;
        if (status != CFR_STATUS_SUCCESS)
            goto done;
    }
    FILE *stream = fopen(path, "r");
    if (stream != NULL) {
        status = mus_score_table_read(stream, &table);
        if (fclose(stream) != 0)
            status = CFR_STATUS_IO_ERROR;
        if (status != CFR_STATUS_SUCCESS)
            goto done;
        if (table.target != options->score_target) {
            status = CFR_STATUS_INCOMPATIBLE_GAME;
            goto done;
        }
        for (unsigned int a = 0; a < table.target; a += 1) {
            for (unsigned int b = 0; b < table.target; b += 1) {
                if (!table.cells[a][b].ready)
                    continue;
                char *cell_path = score_cell_path(options->continuation_dir, a, b);
                FILE *cell_file = cell_path == NULL ? NULL : fopen(cell_path, "rb");
                free(cell_path);
                if (cell_file == NULL) {
                    status = CFR_STATUS_IO_ERROR;
                    goto done;
                }
                if (fclose(cell_file) != 0) {
                    status = CFR_STATUS_IO_ERROR;
                    goto done;
                }
            }
        }
    } else if (errno != ENOENT || !options->iterations_set) {
        status = CFR_STATUS_IO_ERROR;
        goto done;
    }
    if (options->iterations_set) {
        if (!print_score_resume(&table, options) || fflush(stdout) != 0) {
            status = CFR_STATUS_IO_ERROR;
            goto done;
        }
        const MusScoreTrainingOptions training = {
            .iterations = options->iterations,
            .evaluation_samples = options->evaluation_samples,
            .seed = options->seed,
            .reserve_information_sets = options->reserve_information_sets,
            .preload = options->preload_ordago_path == NULL ? NULL : &preload,
            .preload_stats = persistence.preload_stats == NULL ? NULL : &preload_stats,
        };
        status = mus_score_train(&table, options->score_a + options->score_b,
                                  &training, save_score_cell, save_score_diagonal,
                                  &persistence);
        if (status != CFR_STATUS_SUCCESS)
            goto done;
    }
    const MusScoreCell *cell = &table.cells[options->score_a][options->score_b];
    if (!cell->ready) {
        status = CFR_STATUS_NOT_FOUND;
        goto done;
    }
    if (printf("score-table score=%u:%u target=%u value=%.17g win_probability=%.17g\n",
               options->score_a, options->score_b, table.target, cell->value,
               (cell->value + 1) / 2) < 0) {
        status = CFR_STATUS_IO_ERROR;
        goto done;
    }
    if (options->evaluate || options->export_path != NULL) {
        status = mus_score_game_init(&game, &table, options->score_a, options->score_b);
        if (status == CFR_STATUS_SUCCESS)
            status = mus_score_game_state_init(&state, &game);
        if (status != CFR_STATUS_SUCCESS)
            goto done;
        char *cell_path = score_cell_path(options->continuation_dir, options->score_a,
                                          options->score_b);
        if (cell_path == NULL) {
            status = CFR_STATUS_OUT_OF_MEMORY;
            goto done;
        }
        status = load_checkpoint(cell_path, &game.game, mus_game_state_as_game_state(&state),
                                  &store, &trainer, true);
        free(cell_path);
        if (status != CFR_STATUS_SUCCESS)
            goto done;
        store_initialized = true;
        if (trainer.training_iterations != cell->iterations) {
            status = CFR_STATUS_INCOMPATIBLE_GAME;
            goto done;
        }
        if (options->evaluate) {
            double value;
            status = mus_score_evaluate(&game, &store, options->evaluation_samples,
                                        options->seed, &value);
            if (status != CFR_STATUS_SUCCESS)
                goto done;
            if (printf("evaluation scope=score-average-policy score=%u:%u "
                       "samples=%zu estimate=fresh seed=%" PRIu64 " "
                       "stored_value=%.17g stored_samples=%zu "
                       "profile_value_player_0=%.17g "
                       "profile_value_player_1=%.17g win_probability=%.17g\n",
                       options->score_a, options->score_b, options->evaluation_samples,
                       options->seed, cell->value, cell->evaluation_samples,
                       value, -value, (value + 1) / 2) < 0) {
                status = CFR_STATUS_IO_ERROR;
                goto done;
            }
        }
        if (options->export_path != NULL)
            status = export_strategy(options->export_path, &trainer, CLI_FORMAT_ORDAGO_SCORE);
    }
done:
    (void)cfr_info_store_destroy(&preload_store);
    free(preload_state);
    if (store_initialized)
        (void)cfr_info_store_destroy(&store);
    if (locked)
        (void)remove(lock_path);
    free(lock_path);
    free(path);
    if (status != CFR_STATUS_SUCCESS)
        fprintf(stderr, "error: ordago-score: %s\n", status_name(status));
    return status == CFR_STATUS_SUCCESS ? 0 : EXIT_RUNTIME_ERROR;
}

static int run(const CliOptions *options) {
    if (options->format == CLI_FORMAT_ORDAGO_SCORE)
        return run_score(options);
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

        if (timespec_get(&start, TIME_UTC) != TIME_UTC)
            goto runtime_error;
        const int thread_error = run_training(
            trainers, options->threads, options->iterations,
            options->report_every, thread_handles, workers, &store, &start,
            &status);
        if (thread_error != 0) {
            fprintf(stderr,
                    "error: fallo de los hilos de entrenamiento: %s\n",
                    strerror(thread_error));
            goto cleanup;
        }
        if (status != CFR_STATUS_SUCCESS)
            goto runtime_error;
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
