#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
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

enum { EXIT_RUNTIME_ERROR = 1, EXIT_USAGE_ERROR = 2 };

typedef struct {
    size_t iterations;
    size_t report_every;
    uint64_t seed;
    const char *load_path;
    const char *save_path;
    const char *export_path;
    bool iterations_set;
    bool report_every_set;
    bool seed_set;
    bool evaluate;
    bool help;
} CliOptions;

static bool print_usage(FILE *stream, const char *program) {
    return fprintf(
               stream,
               "Uso:\n"
               "  %s --iterations N [--seed N] [--report-every N] "
               "[--save FILE]\n"
               "     [--evaluate] [--export-strategy FILE]\n"
               "  %s --load FILE --iterations N [--report-every N] "
               "[--save FILE]\n"
               "     [--evaluate] [--export-strategy FILE]\n"
               "  %s [--load FILE] --evaluate [--export-strategy FILE]\n"
               "\n"
               "Opciones:\n"
               "  --iterations N         Iteraciones MCCFR nuevas.\n"
               "  --report-every N       Frecuencia de informes de entrenamiento.\n"
               "  --seed N               Semilla de un entrenamiento nuevo (0 por "
               "defecto).\n"
               "  --load FILE            Carga un checkpoint reanudable.\n"
               "  --save FILE            Guarda el checkpoint tras entrenar.\n"
               "  --evaluate             Evalua exactamente la estrategia media.\n"
               "  --export-strategy FILE Exporta la estrategia con etiquetas de "
               "mus.\n"
               "  --help, -h             Muestra esta ayuda.\n",
               program, program, program) >= 0;
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
    *options = (CliOptions){0};
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
        } else if (strcmp(argument, "--seed") == 0) {
            if (options->seed_set ||
                !option_value(argc, argv, &index, &value) ||
                !parse_seed(value, &options->seed)) {
                return false;
            }
            options->seed_set = true;
        } else if (strcmp(argument, "--load") == 0) {
            if (options->load_path != NULL ||
                !option_value(argc, argv, &index, &options->load_path)) {
                return false;
            }
        } else if (strcmp(argument, "--save") == 0) {
            if (options->save_path != NULL ||
                !option_value(argc, argv, &index, &options->save_path)) {
                return false;
            }
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
        return argc == 2;
    if (!options->iterations_set && !options->evaluate)
        return false;
    if (options->report_every_set && !options->iterations_set)
        return false;
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

static bool elapsed_seconds(const struct timespec *start,
                            const struct timespec *end, double *seconds_out) {
    const time_t seconds = end->tv_sec - start->tv_sec;
    const long nanoseconds = end->tv_nsec - start->tv_nsec;

    *seconds_out = (double)seconds + (double)nanoseconds / 1000000000.0;
    return *seconds_out >= 0.0;
}

static Status load_checkpoint(const char *path, const Game *game,
                              GameState *state, InfoStore *store,
                              Trainer *trainer) {
    FILE *stream = fopen(path, "rb");
    Status status;

    if (stream == NULL)
        return CFR_STATUS_IO_ERROR;
    status = cfr_checkpoint_read(stream, game, state, store, trainer);
    if (fclose(stream) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    return status;
}

static Status save_checkpoint(const char *path, const Trainer *trainer) {
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
    status = cfr_checkpoint_write(stream, trainer);
    if (fclose(stream) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    if (status == CFR_STATUS_SUCCESS && rename(temporary_path, path) != 0)
        status = CFR_STATUS_IO_ERROR;
    if (status != CFR_STATUS_SUCCESS)
        (void)remove(temporary_path);
    free(temporary_path);
    return status;
}

static Status export_strategy(const char *path, const Trainer *trainer) {
    FILE *stream = fopen(path, "wx");
    Status status;

    if (stream == NULL)
        return CFR_STATUS_IO_ERROR;
    status = mus_strategy_write_text(stream, trainer);
    if (fclose(stream) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    if (status != CFR_STATUS_SUCCESS)
        (void)remove(path);
    return status;
}

static bool print_report(const Trainer *trainer, const InfoStore *store,
                         const struct timespec *start) {
    TrainerStats trainer_stats;
    InfoStoreStats store_stats;
    struct timespec now;
    double seconds;

    if (cfr_trainer_get_stats(trainer, &trainer_stats) != CFR_STATUS_SUCCESS ||
        cfr_info_store_get_stats(store, &store_stats) != CFR_STATUS_SUCCESS ||
        timespec_get(&now, TIME_UTC) != TIME_UTC ||
        !elapsed_seconds(start, &now, &seconds)) {
        return false;
    }
    return printf("report iterations=%zu information_sets=%zu traversals=%zu "
                  "visited_nodes=%zu seconds=%.6f\n",
                  trainer->training_iterations, store_stats.size,
                  trainer_stats.traversals, trainer_stats.visited_nodes,
                  seconds) >= 0;
}

static bool print_evaluation(size_t iterations,
                             const EvaluationMetrics *metrics) {
    return printf(
               "evaluation iterations=%zu profile_value_player_0=%.17g "
               "profile_value_player_1=%.17g "
               "best_response_value_player_0=%.17g "
               "best_response_value_player_1=%.17g "
               "improvement_player_0=%.17g improvement_player_1=%.17g "
               "nash_conv=%.17g exploitability=%.17g\n",
               iterations, metrics->profile_value_player_0,
               metrics->profile_value_player_1,
               metrics->best_response_value_player_0,
               metrics->best_response_value_player_1,
               metrics->improvement_player_0,
               metrics->improvement_player_1, metrics->nash_conv,
               metrics->exploitability) >= 0;
}

static int run(const CliOptions *options) {
    MusGameState state;
    InfoStore store = {0};
    Trainer trainer = {0};
    const Game *game = mus_game_descriptor();
    GameState *game_state;
    bool store_initialized = false;
    int result = EXIT_RUNTIME_ERROR;
    Status status = mus_game_state_init(&state);

    if (status != CFR_STATUS_SUCCESS)
        goto cleanup;
    game_state = mus_game_state_as_game_state(&state);
    if (options->load_path != NULL) {
        status = load_checkpoint(options->load_path, game, game_state, &store,
                                 &trainer);
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
        status = cfr_trainer_init_mccfr(&trainer, game, game_state, &store,
                                        options->seed);
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

            status = cfr_trainer_run(&trainer, block);
            if (status != CFR_STATUS_SUCCESS)
                goto runtime_error;
            completed += block;
            if (!print_report(&trainer, &store, &start)) {
                status = CFR_STATUS_IO_ERROR;
                goto runtime_error;
            }
        }
    }

    if (options->evaluate) {
        EvaluationMetrics metrics;

        status = mus_evaluation_metrics(&store, &metrics);
        if (status != CFR_STATUS_SUCCESS)
            goto runtime_error;
        if (!print_evaluation(trainer.training_iterations, &metrics)) {
            status = CFR_STATUS_IO_ERROR;
            goto runtime_error;
        }
    }
    if (options->save_path != NULL) {
        status = save_checkpoint(options->save_path, &trainer);
        if (status != CFR_STATUS_SUCCESS) {
            fprintf(stderr, "error: no se pudo guardar '%s': %s\n",
                    options->save_path, status_name(status));
            goto cleanup;
        }
    }
    if (options->export_path != NULL) {
        status = export_strategy(options->export_path, &trainer);
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
