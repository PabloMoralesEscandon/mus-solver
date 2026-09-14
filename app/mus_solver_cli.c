#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>

#include "cfr/checkpoint.h"
#include "cfr/info_store.h"
#include "cfr/trainer.h"
#include "mus_solver/mus_evaluation.h"
#include "mus_solver/mus_game.h"
#include "mus_solver/mus_strategy.h"
#include "mus_solver/mus_score.h"
#include "mus_solver/mus_training.h"
#include "reservation_internal.h"
#include "score_recovery.h"

enum { EXIT_RUNTIME_ERROR = 1, EXIT_USAGE_ERROR = 2 };

typedef enum {
    CLI_FORMAT_SINGLE_BET,
    CLI_FORMAT_RAISES_TO_EIGHT,
    CLI_FORMAT_ORDAGO,
    CLI_FORMAT_ORDAGO_PUBLIC,
    CLI_FORMAT_ORDAGO_SCORE,
    CLI_FORMAT_ORDAGO_SCORE_FOUR
} CliFormat;

static bool is_score_format(CliFormat format) {
    return format == CLI_FORMAT_ORDAGO_SCORE || format == CLI_FORMAT_ORDAGO_SCORE_FOUR;
}

static const char *score_team_fields(bool four_player) {
    return four_player ? " players=4 teams=0+2:1+3" : "";
}

typedef struct {
    size_t iterations;
    size_t report_every;
    size_t reserve_information_sets;
    size_t threads;
    size_t threads_per_cell;
    uint64_t seed;
    const char *load_path;
    const char *save_path;
    const char *export_path;
    CliFormat format;
    bool iterations_set;
    bool report_every_set;
    bool reserve_information_sets_set;
    bool threads_set;
    bool threads_per_cell_set;
    bool seed_set;
    bool format_set;
    bool load_compressed;
    bool save_compressed;
    bool evaluate;
    bool help;
    const char *continuation_dir;
    const char *extend_continuation_dir;
    size_t score_target;
    size_t evaluation_samples;
    size_t evaluation_min_samples;
    double evaluation_target_se;
    unsigned int score_a;
    unsigned int score_b;
    bool score_set;
    bool score_region_set;
    unsigned int region_a;
    unsigned int region_b;
    bool target_set;
    bool evaluation_samples_set;
    bool evaluation_min_samples_set;
    bool evaluation_target_se_set;
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
               "  --threads N            Hilos de entrenamiento (1 por defecto).\n"
               "                          Versiones score: celdas de la misma diagonal;\n"
               "                          con un checkpoint, comparten su estrategia.\n"
               "  --threads-per-cell N   Hilos por estrategia al entrenar una tabla\n"
               "                          score (1 por defecto).\n"
               "  --seed N               Semilla de un entrenamiento nuevo (0 por "
               "defecto).\n"
               "  --format FORMAT        single-bet (por defecto) o "
               "raises-to-eight, ordago, ordago-public, ordago-score\n"
               "                          u ordago-score-four (parejas 0/2 y 1/3).\n"
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
               "  --continuation DIR     Tabla y checkpoints de una version score.\n"
               "  --extend-continuation DIR\n"
               "                          Carga cada checkpoint de DIR y suma --iterations.\n"
               "                          Recalcula continuaciones por diagonal y guarda\n"
               "                          en otro --continuation; conserva el origen.\n"
               "  --score A:B            Marcador inicial (0:0 por defecto).\n"
               "  --score-region A:B     Limita la tabla a continuaciones de A:B y B:A.\n"
               "                          Sin --score, empieza desde A:B.\n"
               "  --target N             Objetivo de una version score (40 por defecto).\n"
               "  --evaluation-samples N Manos para evaluar cada celda (10000).\n"
               "  --evaluation-min-samples N\n"
               "  --evaluation-target-se X\n"
               "                          Juntas: muestreo adaptativo desde N manos,\n"
               "                          hasta --evaluation-samples; error local estimado.\n"
               "                          En las versiones score, --iterations se aplica\n"
               "                          a cada celda pendiente; se guarda por diagonal.\n"
               "                          Con --load[-compressed], se suman solo\n"
               "                          al checkpoint del marcador elegido.\n"
               "                          Requiere --save[-compressed] fuera de DIR;\n"
               "                          conserva values.txt y admite --report-every.\n"
               "  --preload-ordago FILE   Precarga ordago u ordago-public.\n"
               "  --preload-ordago-compressed FILE\n"
               "                          Precarga desde un checkpoint Zstandard.\n"
               "  --preload-weight X     Peso inicial por nodo (1 por defecto).\n"
               "  --help, -h             Muestra esta ayuda.\n",
               program, program, program, program) >= 0;
}

static bool is_unsigned_decimal(const char *text) {
    if (text == NULL || *text == '\0')
        return false;
    for (; *text != '\0'; ++text) {
        if (*text < '0' || *text > '9')
            return false;
    }
    return true;
}

static bool parse_size(const char *text, size_t *value_out) {
    char *end = NULL;
    uintmax_t value;

    if (!is_unsigned_decimal(text))
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

    if (!is_unsigned_decimal(text))
        return false;
    errno = 0;
    value = strtoumax(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT64_MAX)
        return false;
    *value_out = (uint64_t)value;
    return true;
}

static bool parse_score_pair(const char *text, unsigned int *a, unsigned int *b) {
    const char *separator = strchr(text, ':');
    if (separator == NULL || separator == text || separator - text > 2) return false;
    char first[3] = {0};
    memcpy(first, text, (size_t)(separator - text));
    uint64_t left, right;
    if (!parse_seed(first, &left) || !parse_seed(separator + 1, &right) ||
        left >= MUS_ORDAGO_WAGER || right >= MUS_ORDAGO_WAGER) return false;
    *a = (unsigned int)left;
    *b = (unsigned int)right;
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
    *options = (CliOptions){.threads = 1, .threads_per_cell = 1, .score_target = 40,
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
        } else if (strcmp(argument, "--threads-per-cell") == 0) {
            if (options->threads_per_cell_set ||
                !option_value(argc, argv, &index, &value) ||
                !parse_size(value, &options->threads_per_cell)) {
                return false;
            }
            options->threads_per_cell_set = true;
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
            } else if (strcmp(value, "ordago-score-four") == 0) {
                options->format = CLI_FORMAT_ORDAGO_SCORE_FOUR;
            } else if (strcmp(value, "ordago-score") == 0) {
                options->format = CLI_FORMAT_ORDAGO_SCORE;
            } else if (strcmp(value, "ordago-public") == 0) {
                options->format = CLI_FORMAT_ORDAGO_PUBLIC;
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
        } else if (strcmp(argument, "--extend-continuation") == 0) {
            if (options->extend_continuation_dir != NULL ||
                !option_value(argc, argv, &index, &options->extend_continuation_dir) ||
                options->extend_continuation_dir[0] == '\0')
                return false;
        } else if (strcmp(argument, "--score") == 0) {
            if (options->score_set || !option_value(argc, argv, &index, &value) ||
                !parse_score_pair(value, &options->score_a, &options->score_b))
                return false;
            options->score_set = true;
        } else if (strcmp(argument, "--score-region") == 0) {
            if (options->score_region_set || !option_value(argc, argv, &index, &value) ||
                !parse_score_pair(value, &options->region_a, &options->region_b)) return false;
            options->score_region_set = true;
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
        } else if (strcmp(argument, "--evaluation-min-samples") == 0) {
            if (options->evaluation_min_samples_set || !option_value(argc, argv, &index, &value) ||
                !parse_size(value, &options->evaluation_min_samples)) return false;
            options->evaluation_min_samples_set = true;
        } else if (strcmp(argument, "--evaluation-target-se") == 0) {
            if (options->evaluation_target_se_set || !option_value(argc, argv, &index, &value) ||
                !parse_positive_double(value, &options->evaluation_target_se)) return false;
            options->evaluation_target_se_set = true;
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
    if (options->evaluation_min_samples_set != options->evaluation_target_se_set ||
        (options->evaluation_target_se_set &&
         (options->evaluation_min_samples < 2 ||
          options->evaluation_min_samples > options->evaluation_samples ||
          (!options->evaluate && (!options->iterations_set || options->load_path != NULL)))))
        return false;
    if (is_score_format(options->format)) {
        if (options->format == CLI_FORMAT_ORDAGO_SCORE_FOUR &&
            (options->preload_ordago_path != NULL || options->preload_weight_set)) {
            fprintf(stderr, "error: ordago-score-four no admite estrategias previas de dos jugadores\n");
            return false;
        }
        if (options->score_region_set) {
            if (options->region_a >= options->score_target ||
                options->region_b >= options->score_target) return false;
            if (!options->score_set) {
                options->score_a = options->region_a;
                options->score_b = options->region_b;
            }
        }
        if (options->extend_continuation_dir != NULL &&
            (!options->iterations_set || options->load_path != NULL || options->seed_set))
            return false;
        if (options->continuation_dir == NULL ||
            options->continuation_dir[0] == '\0' ||
            options->score_a >= options->score_target ||
            options->score_b >= options->score_target)
            return false;
        if (options->load_path != NULL) {
            if (options->load_path[0] == '\0' || options->threads_per_cell_set ||
                (options->preload_ordago_path != NULL && !options->iterations_set) ||
                (options->preload_weight_set && options->preload_ordago_path == NULL) ||
                (options->iterations_set && (options->seed_set || options->save_path == NULL)) ||
                (!options->iterations_set && (options->save_path != NULL ||
                                              options->report_every_set || options->threads != 1)) ||
                (!options->iterations_set && !options->evaluate && options->export_path == NULL))
                return false;
            if (options->iterations_set && !options->report_every_set)
                options->report_every = options->iterations;
            return true;
        }
        return options->save_path == NULL &&
               (options->iterations_set ||
                (options->threads == 1 && !options->threads_per_cell_set)) &&
               (options->preload_ordago_path == NULL || options->iterations_set) &&
               (!options->preload_weight_set || options->preload_ordago_path != NULL) &&
               (!options->report_every_set || options->iterations_set) &&
               (options->iterations_set || options->evaluate || options->export_path != NULL);
    }
    if (options->continuation_dir != NULL || options->extend_continuation_dir != NULL || options->score_set ||
        options->target_set || options->score_region_set || options->evaluation_samples_set ||
        options->evaluation_min_samples_set || options->evaluation_target_se_set ||
        options->preload_ordago_path != NULL || options->preload_weight_set ||
        options->threads_per_cell_set)
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
    if ((options->format != CLI_FORMAT_ORDAGO &&
         options->format != CLI_FORMAT_ORDAGO_PUBLIC) ||
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
    if (is_score_format(format)) {
        status = mus_score_strategy_write_text(stream, trainer);
    } else if (format == CLI_FORMAT_RAISES_TO_EIGHT) {
        status = mus_raises_to_eight_strategy_write_text(stream, trainer);
    } else if (format == CLI_FORMAT_ORDAGO_PUBLIC) {
        status = mus_ordago_public_strategy_write_text(stream, trainer);
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

static bool print_report(const Trainer *trainer, size_t thread_count,
                         const InfoStore *store,
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
    return printf("report iterations=%zu threads=%zu information_sets=%zu "
                  "traversals=%zu visited_nodes=%zu seconds=%.6f%s\n",
                  trainer->training_iterations, thread_count, store_stats.size,
                  trainer_stats.traversals, trainer_stats.visited_nodes,
                  seconds, score_team_fields(trainer->game->strategic_player_count == 4)) >= 0;
}

static bool print_evaluation(size_t iterations, CliFormat format,
                             const EvaluationMetrics *metrics) {
    const bool fixed_deals = format == CLI_FORMAT_ORDAGO || format == CLI_FORMAT_ORDAGO_PUBLIC;
    return printf(
               "evaluation scope=%s iterations=%zu "
               "profile_value_player_0=%.17g "
               "profile_value_player_1=%.17g "
               "best_response_value_player_0=%.17g "
               "best_response_value_player_1=%.17g "
               "improvement_player_0=%.17g improvement_player_1=%.17g "
               "nash_conv=%.17g exploitability=%.17g%s\n",
               fixed_deals ? "bounded-final-lances" : "root",
               iterations, metrics->profile_value_player_0,
               metrics->profile_value_player_1,
               metrics->best_response_value_player_0,
               metrics->best_response_value_player_1,
               metrics->improvement_player_0,
               metrics->improvement_player_1, metrics->nash_conv,
               metrics->exploitability,
               fixed_deals ? " deals=2 opponent_cards=fixed best_responses=per-deal root_bound=none"
                           : "") >= 0;
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

/* Historical interruption can leave a checkpoint before the first values.txt.
 * A new four-player table cannot establish ownership of those checkpoints. */
static Status check_new_four_score_directory(const char *path) {
    DIR *directory = opendir(path);
    if (directory == NULL) return CFR_STATUS_IO_ERROR;
    Status status = CFR_STATUS_SUCCESS;
    for (;;) {
        errno = 0;
        const struct dirent *entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0) status = CFR_STATUS_IO_ERROR;
            break;
        }
        const size_t length = strlen(entry->d_name);
        if (length >= 14 && strncmp(entry->d_name, "score-", 6) == 0 &&
            strcmp(entry->d_name + length - 8, ".cfr.zst") == 0) {
            status = CFR_STATUS_INCOMPATIBLE_GAME;
            break;
        }
    }
    if (closedir(directory) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    return status;
}

typedef struct {
    const CliOptions *options;
    const MusScorePreloadStats *preload_stats;
    ScoreRecovery *recovery;
} ScorePersistence;

static Status report_score_preload(const MusScoreGame *game,
                                   const MusScorePreloadStats *stats) {
    return printf("score-preload score=%u:%u source_nodes=%zu imported_nodes=%zu "
                  "existing_nodes=%zu expanded_action_nodes=%zu "
                  "missing_source_nodes=%zu imported_later_nodes=%zu "
                  "uniform_after_filter_nodes=%zu mode=lazy-all-lances\n",
                  game->initial_scores[0], game->initial_scores[1],
                  stats->source_nodes, stats->imported_nodes, stats->existing_nodes,
                  stats->expanded_action_nodes, stats->missing_source_nodes,
                  stats->imported_later_nodes, stats->uniform_after_filter_nodes) < 0
                      ? CFR_STATUS_IO_ERROR : CFR_STATUS_SUCCESS;
}

static Status report_score_progress(const MusScoreGame *game, const Trainer *trainer,
    size_t completed, const MusScorePreloadStats *preload, void *context) {
    (void)context;
    InfoStoreStats stats;
    Status status = cfr_info_store_get_stats(trainer->store, &stats);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (printf("score-report score=%u:%u iterations=%zu information_sets=%zu "
               "imported_nodes=%zu imported_later_nodes=%zu missing_source_nodes=%zu "
               "existing_nodes=%zu uniform_after_filter_nodes=%zu added_iterations=%zu%s\n",
               game->initial_scores[0], game->initial_scores[1], trainer->training_iterations, stats.size,
               preload->imported_nodes, preload->imported_later_nodes,
               preload->missing_source_nodes, preload->existing_nodes,
               preload->uniform_after_filter_nodes, completed,
               score_team_fields(game->continuation->four_player)) < 0 || fflush(stdout) != 0)
        return CFR_STATUS_IO_ERROR;
    return CFR_STATUS_SUCCESS;
}

static MusScoreEvaluationOptions score_evaluation_options(const CliOptions *options) {
    return (MusScoreEvaluationOptions){
        .min_samples = options->evaluation_min_samples_set ? options->evaluation_min_samples
                                                         : options->evaluation_samples,
        .max_samples = options->evaluation_samples,
        .target_standard_error = options->evaluation_target_se,
    };
}

static bool print_score_statistics(size_t samples, bool known, double variance,
                                    const CliOptions *options) {
    const double standard_error = known ? sqrt(variance / (double)samples) : 0;
    if (known) {
        if (printf(" uncertainty=local-sampling sample_variance=%.17g standard_error=%.17g "
                   "win_probability_se=%.17g", variance, standard_error, standard_error / 2) < 0)
            return false;
    } else if (printf(" uncertainty=local-sampling sample_variance=unknown "
                      "standard_error=unknown win_probability_se=unknown") < 0) return false;
    if (options != NULL && options->evaluation_target_se_set &&
        printf(" sampling=adaptive min_samples=%zu max_samples=%zu target_se=%.17g stop=%s",
            options->evaluation_min_samples, options->evaluation_samples, options->evaluation_target_se,
            known && samples >= options->evaluation_min_samples &&
                standard_error <= options->evaluation_target_se ? "estimated-target" : "sample-limit") < 0)
        return false;
    return putchar('\n') != EOF;
}

static bool score_sampling_matches(const MusScoreCell *cell, const CliOptions *options) {
    const MusScoreEvaluationOptions evaluation = score_evaluation_options(options);
    return cell->evaluation_samples >= evaluation.min_samples &&
        cell->evaluation_samples <= evaluation.max_samples &&
        (cell->evaluation_samples == evaluation.max_samples ||
         (cell->variance_known && evaluation.target_standard_error > 0 &&
          sqrt(cell->sample_variance / (double)cell->evaluation_samples) <= evaluation.target_standard_error));
}

static Status save_score_cell(const MusScoreGame *game, const Trainer *trainer,
                               const MusScoreCell *cell, void *context) {
    const ScorePersistence *persistence = context;
    const CliOptions *options = persistence->options;
    char *path = score_cell_path(options->continuation_dir,
                                game->initial_scores[0], game->initial_scores[1]);
    if (path == NULL)
        return CFR_STATUS_OUT_OF_MEMORY;
    Status status = save_checkpoint(path, trainer, true);
    if (status == CFR_STATUS_SUCCESS)
        status = score_recovery_save(persistence->recovery, game, cell, path);
    free(path);
    if (status == CFR_STATUS_SUCCESS && persistence->preload_stats != NULL)
        status = report_score_preload(game, persistence->preload_stats);
    if (status == CFR_STATUS_SUCCESS &&
        printf("score-cell score=%u:%u target=%u iterations=%zu "
               "evaluation_samples=%zu value=%.17g win_probability=%.17g%s",
               game->initial_scores[0], game->initial_scores[1],
               game->continuation->target, cell->iterations, cell->evaluation_samples,
               cell->value, (cell->value + 1) / 2,
               score_team_fields(game->continuation->four_player)) < 0)
        status = CFR_STATUS_IO_ERROR;
    if (status == CFR_STATUS_SUCCESS && !print_score_statistics(cell->evaluation_samples,
        cell->variance_known, cell->sample_variance, options)) status = CFR_STATUS_IO_ERROR;
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

/* Lock the directory inode so old or read-only sources need no extra file.
 * training.lock is diagnostic metadata only. The directory must stay in place
 * while in use. Closing this descriptor releases the kernel lock. */
static Status acquire_score_lock(const char *directory, bool exclusive, int *result) {
    const int descriptor = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0) {
        fprintf(stderr, "error: no se pudo abrir el directorio para bloquear '%s': %s\n",
                directory, strerror(errno));
        return CFR_STATUS_IO_ERROR;
    }
    if (flock(descriptor, (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB) != 0) {
        const int error = errno;
        if (error == EWOULDBLOCK || error == EAGAIN) {
            fprintf(stderr, "error: no se pudo adquirir el bloqueo '%s'; "
                            "hay otro entrenamiento, ampliacion o lector\n", directory);
            char *path = score_path(directory, "training.lock");
            if (path != NULL) { report_score_lock(path); free(path); }
        } else {
            fprintf(stderr, "error: el sistema no pudo bloquear '%s': %s\n", directory, strerror(error));
        }
        (void)close(descriptor);
        return CFR_STATUS_IO_ERROR;
    }
    Status status = CFR_STATUS_SUCCESS;
    if (exclusive) {
        char *path = score_path(directory, "training.lock");
        status = path == NULL ? CFR_STATUS_OUT_OF_MEMORY : CFR_STATUS_SUCCESS;
        FILE *stream = path == NULL ? NULL : fopen(path, "w");
        if (path != NULL)
            status = stream == NULL ? CFR_STATUS_IO_ERROR : write_score_lock(stream);
        if (stream != NULL && fclose(stream) != 0) status = CFR_STATUS_IO_ERROR;
        free(path);
    }
    if (status != CFR_STATUS_SUCCESS) {
        (void)close(descriptor);
        return status;
    }
    *result = descriptor;
    return CFR_STATUS_SUCCESS;
}

static bool print_score_resume(const MusScoreTable *table, const CliOptions *options,
                               const MusScoreTable *source) {
    size_t count = 0, mismatched = 0;
    size_t pending_by_diagonal[2 * MUS_ORDAGO_WAGER] = {0};
    size_t cell_limit = 0;
    size_t min_iterations = SIZE_MAX, max_iterations = 0;
    size_t min_samples = SIZE_MAX, max_samples = 0;
    for (unsigned int a = 0; a < table->target; a += 1) {
        for (unsigned int b = 0; b < table->target; b += 1) {
            const MusScoreCell *cell = &table->cells[a][b];
            if (!cell->ready) {
                if (a + b >= options->score_a + options->score_b &&
                    mus_score_table_contains(table, a, b)) {
                    const size_t pending = ++pending_by_diagonal[a + b];
                    if (pending > cell_limit)
                        cell_limit = pending;
                }
                continue;
            }
            count += 1;
            if (cell->iterations < min_iterations)
                min_iterations = cell->iterations;
            if (cell->iterations > max_iterations)
                max_iterations = cell->iterations;
            if (cell->evaluation_samples < min_samples)
                min_samples = cell->evaluation_samples;
            if (cell->evaluation_samples > max_samples)
                max_samples = cell->evaluation_samples;
            const uint64_t seed = source == NULL ? options->seed + a * MUS_ORDAGO_WAGER + b
                                                 : source->cells[a][b].training_seed;
            const size_t iterations = options->iterations +
                (source == NULL ? 0 : source->cells[a][b].iterations);
            if (cell->iterations != iterations ||
                !score_sampling_matches(cell, options) ||
                cell->training_seed != seed)
                mismatched += 1;
        }
    }
    if (cell_limit > options->threads)
        cell_limit = options->threads;
    if (printf("score-workers requested=%zu cell_limit=%zu threads_per_cell=%zu\n",
               options->threads, cell_limit, options->threads_per_cell) < 0)
        return false;
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

typedef struct {
    size_t threads;
    const struct timespec *start;
} CliTrainingProgress;

static Status report_training_progress(const Trainer *trainer, size_t completed,
                                       void *context) {
    const CliTrainingProgress *progress = context;
    (void)completed;
    return print_report(trainer, progress->threads, trainer->store, progress->start)
               ? CFR_STATUS_SUCCESS : CFR_STATUS_IO_ERROR;
}

static Status load_score_prior_source(const CliOptions *options,
                                       MusGameState **state, InfoStore *store,
                                       Trainer *trainer) {
    *state = malloc(sizeof(**state));
    Status status = *state == NULL ? CFR_STATUS_OUT_OF_MEMORY
                                   : mus_ordago_game_state_init(*state);
    if (status == CFR_STATUS_SUCCESS)
        status = load_checkpoint(options->preload_ordago_path, mus_ordago_game_descriptor(),
            mus_game_state_as_game_state(*state), store, trainer,
            options->preload_ordago_compressed);
    if (status == CFR_STATUS_INCOMPATIBLE_GAME) {
        status = mus_ordago_public_game_state_init(*state);
        if (status == CFR_STATUS_SUCCESS)
            status = load_checkpoint(options->preload_ordago_path, mus_ordago_public_game_descriptor(),
                mus_game_state_as_game_state(*state), store, trainer,
                options->preload_ordago_compressed);
    }
    if (status != CFR_STATUS_SUCCESS) {
        fprintf(stderr, "error: no se pudo precargar '%s': %s\n",
                options->preload_ordago_path, status_name(status));
        return status;
    }
    return printf("score-preload-source path=%s schema=%s source_iterations=%zu "
                  "weight=%.17g uniform_mix=%.17g mode=lazy-all-lances\n",
                  options->preload_ordago_path, trainer->game->strategy_schema_id,
                  trainer->training_iterations, options->preload_weight,
                  MUS_SCORE_PRELOAD_DEFAULT_UNIFORM_MIX) < 0
               ? CFR_STATUS_IO_ERROR : CFR_STATUS_SUCCESS;
}

/* Resume one checkpoint against an immutable snapshot of continuation values.
 * The selected cell need not be published. Only higher diagonals are required. */
static Status resolve_score_region(CliOptions *options, const MusScoreTable *table) {
    if (options->score_region_set) {
        const unsigned int low = options->region_a < options->region_b
                                     ? options->region_a : options->region_b;
        const unsigned int high = options->region_a < options->region_b
                                      ? options->region_b : options->region_a;
        if (!table->region_enabled || table->region_scores[0] != low ||
            table->region_scores[1] != high) {
            fprintf(stderr, "error: la region solicitada no coincide con la tabla\n");
            return CFR_STATUS_INCOMPATIBLE_GAME;
        }
    } else if (table->region_enabled && !options->score_set) {
        options->score_a = table->region_scores[0];
        options->score_b = table->region_scores[1];
    }
    if (!mus_score_table_contains(table, options->score_a, options->score_b)) {
        fprintf(stderr, "error: el marcador queda fuera de la region de la tabla\n");
        return CFR_STATUS_INCOMPATIBLE_GAME;
    }
    if (table->region_enabled) {
        size_t requested = 0, ready = 0;
        for (unsigned int a = 0; a < table->target; ++a) {
            for (unsigned int b = 0; b < table->target; ++b) {
                if (a + b >= options->score_a + options->score_b &&
                    mus_score_table_contains(table, a, b)) {
                    requested++;
                    ready += table->cells[a][b].ready ? 1U : 0U;
                }
            }
        }
        if (printf("score-region bounds=%u:%u requested_cells=%zu ready_cells=%zu\n",
                   table->region_scores[0], table->region_scores[1], requested, ready) < 0)
            return CFR_STATUS_IO_ERROR;
    }
    return CFR_STATUS_SUCCESS;
}

static int run_score_checkpoint(const CliOptions *options) {
    CliOptions resolved = *options;
    options = &resolved;
    MusScoreTable table;
    MusScoreGame game;
    InfoStore store = {0};
    Trainer trainer;
    GameState *state = NULL;
    InfoStore preload_store = {0};
    MusGameState *preload_state = NULL;
    Trainer preload_trainer;
    MusScorePrior prior;
    MusScorePreloadStats preload_stats;
    int continuation_lock = -1;
    const MusScorePreloadOptions preload = {
        .source = &preload_trainer, .weight = options->preload_weight,
        .uniform_mix = MUS_SCORE_PRELOAD_DEFAULT_UNIFORM_MIX,
    };
    char *path = score_path(options->continuation_dir, "values.txt");
    Status status = path == NULL ? CFR_STATUS_OUT_OF_MEMORY : CFR_STATUS_SUCCESS;
    if (status != CFR_STATUS_SUCCESS)
        goto done;
    status = check_score_output_path(options->continuation_dir, options->save_path);
    if (status == CFR_STATUS_SUCCESS)
        status = check_score_output_path(options->continuation_dir, options->export_path);
    if (status == CFR_STATUS_SUCCESS)
        status = acquire_score_lock(options->continuation_dir, false, &continuation_lock);
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
    if (table.target != options->score_target ||
        table.four_player != (options->format == CLI_FORMAT_ORDAGO_SCORE_FOUR)) {
        status = CFR_STATUS_INCOMPATIBLE_GAME;
        goto done;
    }
    status = resolve_score_region(&resolved, &table);
    if (status != CFR_STATUS_SUCCESS) goto done;
    const unsigned int sum = options->score_a + options->score_b;
    for (unsigned int a = 0; a < table.target; a += 1) {
        for (unsigned int b = 0; b < table.target; b += 1) {
            if (a + b > sum && mus_score_table_contains(&table, a, b) && !table.cells[a][b].ready) {
                fprintf(stderr, "error: falta el valor de continuacion %u:%u\n", a, b);
                status = CFR_STATUS_NOT_FOUND;
                goto done;
            }
        }
    }
    status = mus_score_game_init(&game, &table, options->score_a, options->score_b);
    if (status != CFR_STATUS_SUCCESS) goto done;
    state = malloc(mus_score_game_state_size(&game));
    if (state == NULL) {
        status = CFR_STATUS_OUT_OF_MEMORY;
        goto done;
    }
    status = mus_score_game_state_init_generic(state, &game);
    if (status == CFR_STATUS_SUCCESS)
        status = load_checkpoint(options->load_path, &game.game,
                                 state, &store,
                                 &trainer, options->load_compressed);
    if (status != CFR_STATUS_SUCCESS)
        goto done;
    const size_t initial_iterations = trainer.training_iterations;
    if (options->iterations > SIZE_MAX - initial_iterations) {
        status = CFR_STATUS_INVALID_ARGUMENT;
        goto done;
    }
    if (options->iterations_set && options->threads > 1) {
        if (trainer.variant != CFR_TRAINER_VARIANT_MCCFR_EXTERNAL) {
            status = CFR_STATUS_INVALID_ARGUMENT;
            goto done;
        }
        status = cfr_info_store_prepare_concurrent(&store);
    }
    if (status == CFR_STATUS_SUCCESS && options->reserve_information_sets_set)
        status = cfr_info_store_reserve(&store, options->reserve_information_sets);
    if (status != CFR_STATUS_SUCCESS)
        goto done;
    if (options->preload_ordago_path != NULL) {
        status = load_score_prior_source(options, &preload_state, &preload_store,
                                         &preload_trainer);
        if (status == CFR_STATUS_SUCCESS)
            status = mus_score_prior_attach(&prior, &game, &preload, &store, &preload_stats);
        if (status != CFR_STATUS_SUCCESS)
            goto done;
    }
    if (options->iterations_set) {
        struct timespec start;
        if (timespec_get(&start, TIME_UTC) != TIME_UTC) {
            status = CFR_STATUS_IO_ERROR;
            goto done;
        }
        CliTrainingProgress progress = {.threads = options->threads, .start = &start};
        status = mus_training_run(
            &trainer, options->iterations, options->threads, options->report_every,
            report_training_progress, &progress);
        if (status != CFR_STATUS_SUCCESS)
            goto done;
    }
    if (options->preload_ordago_path != NULL) {
        status = report_score_preload(&game, &preload_stats);
        if (status != CFR_STATUS_SUCCESS)
            goto done;
    }
    if (options->save_path != NULL)
        status = save_checkpoint(options->save_path, &trainer, options->save_compressed);
    if (status != CFR_STATUS_SUCCESS)
        goto done;
    if (printf("score-checkpoint score=%u:%u target=%u initial_iterations=%zu "
               "added_iterations=%zu total_iterations=%zu continuation=frozen%s\n",
               options->score_a, options->score_b, table.target, initial_iterations,
               options->iterations, trainer.training_iterations,
               score_team_fields(table.four_player)) < 0) {
        status = CFR_STATUS_IO_ERROR;
        goto done;
    }
    if (options->evaluate) {
        const MusScoreEvaluationOptions evaluation_options = score_evaluation_options(options);
        MusScoreEvaluation evaluation;
        status = mus_score_evaluate_with_options(&game, &store, &evaluation_options,
                                                  options->seed, &evaluation);
        if (status == CFR_STATUS_SUCCESS &&
            printf("evaluation scope=score-checkpoint-average-policy score=%u:%u "
                   "iterations=%zu samples=%zu estimate=fresh seed=%" PRIu64 " "
                   "profile_value_player_0=%.17g win_probability=%.17g",
                   options->score_a, options->score_b, trainer.training_iterations,
                   evaluation.samples, options->seed, evaluation.value, (evaluation.value + 1) / 2) < 0)
            status = CFR_STATUS_IO_ERROR;
        if (status == CFR_STATUS_SUCCESS && table.four_player &&
            printf(" profile_value_player_1=%.17g profile_value_player_2=%.17g "
                   "profile_value_player_3=%.17g%s", -evaluation.value, evaluation.value,
                   -evaluation.value, score_team_fields(true)) < 0)
            status = CFR_STATUS_IO_ERROR;
        if (status == CFR_STATUS_SUCCESS && !print_score_statistics(evaluation.samples,
            evaluation.variance_known, evaluation.sample_variance, options)) status = CFR_STATUS_IO_ERROR;
    }
    if (status == CFR_STATUS_SUCCESS && options->export_path != NULL)
        status = export_strategy(options->export_path, &trainer, options->format);
done:
    (void)cfr_info_store_destroy(&store);
    (void)cfr_info_store_destroy(&preload_store);
    free(preload_state);
    free(state);
    free(path);
    if (continuation_lock >= 0 && close(continuation_lock) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    if (status != CFR_STATUS_SUCCESS)
        fprintf(stderr, "error: checkpoint %s: %s\n",
            options->format == CLI_FORMAT_ORDAGO_SCORE_FOUR ? "ordago-score-four" : "ordago-score",
            status_name(status));
    return status == CFR_STATUS_SUCCESS ? 0 : EXIT_RUNTIME_ERROR;
}

static Status read_score_extension(const CliOptions *options, MusScoreTable *source,
                                    int *source_lock) {
    Status status = check_score_output_path(options->extend_continuation_dir, options->continuation_dir);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    char *path = score_path(options->extend_continuation_dir, "values.txt");
    if (path == NULL) {
        return CFR_STATUS_OUT_OF_MEMORY;
    }
    status = acquire_score_lock(options->extend_continuation_dir, false, source_lock);
    FILE *stream = status == CFR_STATUS_SUCCESS ? fopen(path, "r") : NULL;
    if (status == CFR_STATUS_SUCCESS)
        status = stream == NULL ? CFR_STATUS_IO_ERROR : mus_score_table_read(stream, source);
    if (stream != NULL && fclose(stream) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    free(path);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (source->target != options->score_target ||
        source->four_player != (options->format == CLI_FORMAT_ORDAGO_SCORE_FOUR))
        return CFR_STATUS_INCOMPATIBLE_GAME;
    return CFR_STATUS_SUCCESS;
}

/* Validate only the destination's requested cells, after loading its persisted
 * coverage. A restricted source cannot satisfy a larger destination region. */
static Status check_score_extension_coverage(const CliOptions *options,
    const MusScoreTable *source, const MusScoreTable *destination) {
    for (unsigned int a = 0; a < source->target; ++a) {
        for (unsigned int b = 0; b < source->target; ++b) {
            if (a + b < options->score_a + options->score_b ||
                !mus_score_table_contains(destination, a, b))
                continue;
            const MusScoreCell *cell = &source->cells[a][b];
            if (!cell->ready)
                return CFR_STATUS_NOT_FOUND;
            if (cell->iterations > SIZE_MAX - options->iterations)
                return CFR_STATUS_INVALID_ARGUMENT;
            char *path = score_cell_path(options->extend_continuation_dir, a, b);
            FILE *stream = path == NULL ? NULL : fopen(path, "rb");
            free(path);
            if (stream == NULL)
                return CFR_STATUS_IO_ERROR;
            if (fclose(stream) != 0)
                return CFR_STATUS_IO_ERROR;
        }
    }
    return CFR_STATUS_SUCCESS;
}

/* Compare immutable extension settings before accepting published destination
 * cells. Include the complete source table, so changed source continuations
 * cannot silently enter a partially completed extension. */
static Status check_score_extension_manifest(const CliOptions *options,
    const MusScoreTable *source, const MusScoreTable *destination) {
    char *path = score_path(options->continuation_dir, "extension.txt");
    if (path == NULL)
        return CFR_STATUS_OUT_OF_MEMORY;
    FILE *existing = fopen(path, "rb");
    const int open_error = errno;
    if (source == NULL) {
        free(path);
        if (existing == NULL)
            return open_error == ENOENT ? CFR_STATUS_SUCCESS : CFR_STATUS_IO_ERROR;
        (void)fclose(existing);
        fprintf(stderr, "error: este destino requiere --extend-continuation\n");
        return CFR_STATUS_INCOMPATIBLE_GAME;
    }
    if (existing == NULL && errno != ENOENT) {
        free(path);
        return CFR_STATUS_IO_ERROR;
    }
    Status status = check_score_output_path(options->continuation_dir, options->extend_continuation_dir);
    char *source_path = realpath(options->extend_continuation_dir, NULL);
    char *prior_path = options->preload_ordago_path == NULL ? strdup("")
        : realpath(options->preload_ordago_path, NULL);
    FILE *expected = tmpfile();
    if (source_path == NULL || prior_path == NULL || expected == NULL)
        status = CFR_STATUS_IO_ERROR;
    if (status == CFR_STATUS_SUCCESS &&
        fprintf(expected, "mus-score-extension version=1\nsource=%s\nadded_iterations=%zu\n"
                "evaluation_samples=%zu\nprior=%s\nprior_compressed=%u\nprior_weight=%.17g\n"
                "uniform_mix=%.17g\n",
                source_path, options->iterations, options->evaluation_samples,
                prior_path, options->preload_ordago_compressed ? 1U : 0U,
                options->preload_weight, MUS_SCORE_PRELOAD_DEFAULT_UNIFORM_MIX) < 0)
        status = CFR_STATUS_IO_ERROR;
    if (status == CFR_STATUS_SUCCESS && options->evaluation_target_se_set &&
        fprintf(expected, "evaluation_min_samples=%zu\nevaluation_target_se=%.17g\n",
            options->evaluation_min_samples, options->evaluation_target_se) < 0)
        status = CFR_STATUS_IO_ERROR;
    if (status == CFR_STATUS_SUCCESS)
        status = mus_score_table_write(expected, source);
    if (status == CFR_STATUS_SUCCESS && fflush(expected) != 0)
        status = CFR_STATUS_IO_ERROR;
    if (status == CFR_STATUS_SUCCESS && fseeko(expected, 0, SEEK_SET) != 0)
        status = CFR_STATUS_IO_ERROR;
    if (status == CFR_STATUS_SUCCESS) {
        if (existing != NULL) {
            int a, b;
            do {
                a = fgetc(expected);
                b = fgetc(existing);
                if (a != b) { status = CFR_STATUS_INCOMPATIBLE_GAME; break; }
            } while (a != EOF);
            if (ferror(expected) || ferror(existing))
                status = CFR_STATUS_IO_ERROR;
        } else {
            /* A destination with published cells must already have metadata. */
            if (destination->cells[destination->target - 1][destination->target - 1].ready)
                status = CFR_STATUS_INCOMPATIBLE_GAME;
            if (status == CFR_STATUS_SUCCESS) {
                FILE *output = fopen(path, "wbx");
                if (output == NULL) {
                    status = CFR_STATUS_IO_ERROR;
                } else {
                    int byte;
                    while ((byte = fgetc(expected)) != EOF) {
                        if (fputc(byte, output) == EOF) { status = CFR_STATUS_IO_ERROR; break; }
                    }
                    if (ferror(expected) || fflush(output) != 0)
                        status = CFR_STATUS_IO_ERROR;
                    if (fclose(output) != 0)
                        status = CFR_STATUS_IO_ERROR;
                    if (status != CFR_STATUS_SUCCESS)
                        (void)remove(path);
                }
            }
        }
    }
    if (existing != NULL && fclose(existing) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    if (expected != NULL) (void)fclose(expected);
    free(path);
    free(source_path);
    free(prior_path);
    if (status == CFR_STATUS_INCOMPATIBLE_GAME)
        fprintf(stderr, "error: el destino no corresponde a esta ampliacion\n");
    return status;
}

static Status load_score_extension_cell(const MusScoreGame *game, GameState *state,
    InfoStore *store, Trainer *trainer, void *context) {
    const CliOptions *options = context;
    char *path = score_cell_path(options->extend_continuation_dir,
        game->initial_scores[0], game->initial_scores[1]);
    if (path == NULL)
        return CFR_STATUS_OUT_OF_MEMORY;
    Status status = load_checkpoint(path, &game->game,
        state, store, trainer, true);
    if (status == CFR_STATUS_SUCCESS &&
        printf("score-extend score=%u:%u initial_iterations=%zu added_iterations=%zu "
               "source_schema=%s continuation=updated-by-diagonal\n",
               game->initial_scores[0], game->initial_scores[1],
               trainer->training_iterations, options->iterations, game->schema) < 0)
        status = CFR_STATUS_IO_ERROR;
    free(path);
    return status;
}

static int run_score(const CliOptions *options) {
    if (options->load_path != NULL)
        return run_score_checkpoint(options);
    CliOptions resolved = *options;
    options = &resolved;
    MusScoreTable source_table;
    const MusScoreExtension extension = {
        .table = &source_table, .load_cell_generic = load_score_extension_cell,
        .context = (void *)options,
    };
    MusScoreTable table;
    Status status = options->score_region_set
        ? mus_score_table_init_region(&table, (unsigned int)options->score_target,
                                      options->region_a, options->region_b)
        : mus_score_table_init(&table, (unsigned int)options->score_target);
    table.four_player = options->format == CLI_FORMAT_ORDAGO_SCORE_FOUR;
    InfoStore store = {0};
    bool store_initialized = false;
    GameState *state = NULL;
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
    ScoreRecovery recovery = {0};
    ScorePersistence persistence = {
        .options = options,
        .preload_stats = options->preload_ordago_path == NULL ? NULL : &preload_stats,
        .recovery = &recovery,
    };
    char *path = score_path(options->continuation_dir, "values.txt");
    int lock = -1, source_lock = -1;
    bool new_table = false;
    if (path == NULL) {
        status = CFR_STATUS_OUT_OF_MEMORY;
        goto done;
    }
    if (options->extend_continuation_dir != NULL) {
        status = read_score_extension(options, &source_table, &source_lock);
        if (status != CFR_STATUS_SUCCESS)
            goto done;
    }
    if (options->extend_continuation_dir == NULL && options->preload_ordago_path != NULL) {
        status = load_score_prior_source(options, &preload_state, &preload_store, &preload_trainer);
        if (status != CFR_STATUS_SUCCESS)
            goto done;
    }
    if (options->iterations_set) {
        if (mkdir(options->continuation_dir, 0777) != 0 && errno != EEXIST) {
            status = CFR_STATUS_IO_ERROR;
            goto done;
        }
    }
    status = acquire_score_lock(options->continuation_dir, options->iterations_set, &lock);
    if (status != CFR_STATUS_SUCCESS)
        goto done;
    FILE *stream = fopen(path, "r");
    if (stream != NULL) {
        status = mus_score_table_read(stream, &table);
        if (fclose(stream) != 0)
            status = CFR_STATUS_IO_ERROR;
        if (status != CFR_STATUS_SUCCESS)
            goto done;
        if (table.target != options->score_target ||
            table.four_player != (options->format == CLI_FORMAT_ORDAGO_SCORE_FOUR)) {
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
    } else {
        if (table.four_player) {
            status = check_new_four_score_directory(options->continuation_dir);
            if (status != CFR_STATUS_SUCCESS) goto done;
        }
        new_table = true;
    }
    status = resolve_score_region(&resolved, &table);
    if (status != CFR_STATUS_SUCCESS) goto done;
    if (options->iterations_set) {
        if (options->extend_continuation_dir != NULL) {
            status = check_score_extension_coverage(options, &source_table, &table);
            if (status != CFR_STATUS_SUCCESS) goto done;
        }
        status = check_score_extension_manifest(options,
            options->extend_continuation_dir == NULL ? NULL : &source_table, &table);
        if (status != CFR_STATUS_SUCCESS)
            goto done;
        if (options->extend_continuation_dir != NULL && options->preload_ordago_path != NULL) {
            status = load_score_prior_source(options, &preload_state, &preload_store, &preload_trainer);
            if (status != CFR_STATUS_SUCCESS)
                goto done;
        }
        /* Preserve the variant and explicit coverage if the first cell is interrupted. */
        if (new_table && (table.region_enabled || table.four_player)) {
            status = save_score_diagonal(&table, &persistence);
            if (status != CFR_STATUS_SUCCESS) goto done;
        }
        if (!print_score_resume(&table, options,
                options->extend_continuation_dir == NULL ? NULL : &source_table) || fflush(stdout) != 0) {
            status = CFR_STATUS_IO_ERROR;
            goto done;
        }
        const ScoreRecoveryOptions recovery_options = {
            .directory = options->continuation_dir,
            .source_directory = options->extend_continuation_dir,
            .source_table = options->extend_continuation_dir == NULL ? NULL : &source_table,
            .prior_path = options->preload_ordago_path,
            .prior_compressed = options->preload_ordago_compressed,
            .iterations = options->iterations,
            .evaluation_samples = options->evaluation_samples,
            .evaluation_min_samples = options->evaluation_min_samples,
            .evaluation_target_se = options->evaluation_target_se,
            .seed = options->seed,
            .threads = options->threads,
            .threads_per_cell = options->threads_per_cell,
            .reserve_information_sets = options->reserve_information_sets,
            .report_every = options->report_every_set ? options->report_every : 0,
            .prior_weight = options->preload_weight,
            .uniform_mix = MUS_SCORE_PRELOAD_DEFAULT_UNIFORM_MIX,
        };
        status = score_recovery_open(&recovery, &recovery_options);
        if (status != CFR_STATUS_SUCCESS) goto done;
        const MusScoreTrainingOptions training = {
            .extension = options->extend_continuation_dir == NULL ? NULL : &extension,
            .iterations = options->iterations,
            .evaluation_samples = options->evaluation_samples,
            .evaluation_min_samples = options->evaluation_min_samples,
            .evaluation_target_se = options->evaluation_target_se,
            .seed = options->seed,
            .reserve_information_sets = options->reserve_information_sets,
            .threads = options->threads,
            .threads_per_cell = options->threads_per_cell,
            .report_every = options->report_every_set ? options->report_every : 0,
            .progress = options->report_every_set ? report_score_progress : NULL,
            .preload = options->preload_ordago_path == NULL ? NULL : &preload,
            .preload_stats = persistence.preload_stats == NULL ? NULL : &preload_stats,
            .recover_cell = score_recovery_load,
            .recovery_context = &recovery,
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
    if (printf("score-table score=%u:%u target=%u value=%.17g win_probability=%.17g%s",
               options->score_a, options->score_b, table.target, cell->value,
               (cell->value + 1) / 2, score_team_fields(table.four_player)) < 0) {
        status = CFR_STATUS_IO_ERROR;
        goto done;
    }
    if (!print_score_statistics(cell->evaluation_samples, cell->variance_known,
                                cell->sample_variance, NULL)) {
        status = CFR_STATUS_IO_ERROR;
        goto done;
    }
    if (options->evaluate || options->export_path != NULL) {
        status = mus_score_game_init(&game, &table, options->score_a, options->score_b);
        if (status == CFR_STATUS_SUCCESS) {
            state = malloc(mus_score_game_state_size(&game));
            status = state == NULL ? CFR_STATUS_OUT_OF_MEMORY
                : mus_score_game_state_init_generic(state, &game);
        }
        if (status != CFR_STATUS_SUCCESS)
            goto done;
        char *cell_path = score_cell_path(options->continuation_dir, options->score_a,
                                          options->score_b);
        if (cell_path == NULL) {
            status = CFR_STATUS_OUT_OF_MEMORY;
            goto done;
        }
        status = load_checkpoint(cell_path, &game.game, state,
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
            const MusScoreEvaluationOptions evaluation_options = score_evaluation_options(options);
            MusScoreEvaluation evaluation;
            status = mus_score_evaluate_with_options(&game, &store, &evaluation_options,
                                                      options->seed, &evaluation);
            if (status != CFR_STATUS_SUCCESS)
                goto done;
            if (printf("evaluation scope=score-average-policy score=%u:%u "
                       "samples=%zu estimate=fresh seed=%" PRIu64 " "
                       "stored_value=%.17g stored_samples=%zu "
                       "profile_value_player_0=%.17g "
                       "profile_value_player_1=%.17g win_probability=%.17g",
                       options->score_a, options->score_b, evaluation.samples,
                       options->seed, cell->value, cell->evaluation_samples,
                       evaluation.value, -evaluation.value, (evaluation.value + 1) / 2) < 0 ||
                (table.four_player && printf(" profile_value_player_2=%.17g "
                    "profile_value_player_3=%.17g%s", evaluation.value, -evaluation.value,
                    score_team_fields(true)) < 0) ||
                !print_score_statistics(evaluation.samples, evaluation.variance_known,
                                        evaluation.sample_variance, options)) {
                status = CFR_STATUS_IO_ERROR;
                goto done;
            }
        }
        if (options->export_path != NULL)
            status = export_strategy(options->export_path, &trainer, options->format);
    }
done:
    if (score_recovery_close(&recovery) != CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    (void)cfr_info_store_destroy(&preload_store);
    free(preload_state);
    free(state);
    if (store_initialized)
        (void)cfr_info_store_destroy(&store);
    if (lock >= 0 && close(lock) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    if (source_lock >= 0 && close(source_lock) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    free(path);
    if (status != CFR_STATUS_SUCCESS)
        fprintf(stderr, "error: %s: %s\n",
            options->format == CLI_FORMAT_ORDAGO_SCORE_FOUR ? "ordago-score-four" : "ordago-score",
            status_name(status));
    return status == CFR_STATUS_SUCCESS ? 0 : EXIT_RUNTIME_ERROR;
}

static int run(const CliOptions *options) {
    if (is_score_format(options->format))
        return run_score(options);
    MusGameState *state = NULL;
    InfoStore store = {0};
    Trainer trainer;
    const Game *game;
    bool store_initialized = false;
    int result = EXIT_RUNTIME_ERROR;
    Status status;

    if (options->format == CLI_FORMAT_RAISES_TO_EIGHT) {
        game = mus_raises_to_eight_game_descriptor();
    } else if (options->format == CLI_FORMAT_ORDAGO_PUBLIC) {
        game = mus_ordago_public_game_descriptor();
    } else if (options->format == CLI_FORMAT_ORDAGO) {
        game = mus_ordago_game_descriptor();
    } else {
        game = mus_single_bet_game_descriptor();
    }
    state = malloc(sizeof(*state));
    if (state == NULL) {
        status = CFR_STATUS_OUT_OF_MEMORY;
        goto runtime_error;
    }
    if (options->format == CLI_FORMAT_RAISES_TO_EIGHT) {
        status = mus_raises_to_eight_game_state_init(state);
    } else if (options->format == CLI_FORMAT_ORDAGO_PUBLIC) {
        status = mus_ordago_public_game_state_init(state);
    } else if (options->format == CLI_FORMAT_ORDAGO) {
        status = mus_ordago_game_state_init(state);
    } else {
        status = mus_single_bet_game_state_init(state);
    }
    if (status != CFR_STATUS_SUCCESS)
        goto runtime_error;
    if (options->load_path != NULL) {
        status = load_checkpoint(options->load_path, game,
                                 mus_game_state_as_game_state(state),
                                 &store, &trainer,
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
            &trainer, game,
            mus_game_state_as_game_state(state), &store, options->seed);
        if (status != CFR_STATUS_SUCCESS)
            goto runtime_error;
    }
    if (options->threads > 1 &&
        trainer.variant != CFR_TRAINER_VARIANT_MCCFR_EXTERNAL) {
        fprintf(stderr,
                "error: el entrenamiento con varios hilos requiere un "
                "checkpoint MCCFR\n");
        goto cleanup;
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

        if (timespec_get(&start, TIME_UTC) != TIME_UTC) {
            status = CFR_STATUS_IO_ERROR;
            goto runtime_error;
        }
        CliTrainingProgress progress = {.threads = options->threads, .start = &start};
        status = mus_training_run(
            &trainer, options->iterations, options->threads, options->report_every,
            report_training_progress, &progress);
        if (status != CFR_STATUS_SUCCESS)
            goto runtime_error;
    }

    if (options->save_path != NULL) {
        status = save_checkpoint(options->save_path, &trainer,
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
        } else if (options->format == CLI_FORMAT_ORDAGO_PUBLIC) {
            status = mus_ordago_public_evaluation_metrics(&store, &metrics);
        } else if (options->format == CLI_FORMAT_ORDAGO) {
            status = mus_ordago_evaluation_metrics(&store, &metrics);
        } else {
            status = mus_single_bet_evaluation_metrics(&store, &metrics);
        }
        if (status != CFR_STATUS_SUCCESS)
            goto runtime_error;
        if (!print_evaluation(trainer.training_iterations,
                              options->format, &metrics)) {
            status = CFR_STATUS_IO_ERROR;
            goto runtime_error;
        }
    }
    if (options->export_path != NULL) {
        status = export_strategy(options->export_path, &trainer,
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
    free(state);
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
