#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "cfr/info_node.h"
#include "mus_solver/mus_score.h"
#include "mus_solver/mus_training.h"

Status mus_score_table_init(MusScoreTable *table, unsigned int target) {
    if (table == NULL || target == 0 || target > MUS_ORDAGO_WAGER)
        return CFR_STATUS_INVALID_ARGUMENT;
    *table = (MusScoreTable){.target = (uint8_t)target};
    return CFR_STATUS_SUCCESS;
}

static bool valid_cell(const MusScoreCell *cell) {
    return isfinite(cell->value) && cell->value >= -1 && cell->value <= 1 &&
           cell->iterations > 0 && cell->evaluation_samples > 0;
}

/* A persisted table contains complete consecutive diagonals from the top. */
static bool valid_table(const MusScoreTable *table) {
    if (table == NULL || table->target == 0 || table->target > MUS_ORDAGO_WAGER)
        return false;
    bool missing = false;
    for (int sum = 2 * (table->target - 1); sum >= 0; sum -= 1) {
        int ready = 0;
        int count = 0;
        for (int a = 0; a < table->target; a += 1) {
            const int b = sum - a;
            if (b < 0 || b >= table->target)
                continue;
            const MusScoreCell *cell = &table->cells[a][b];
            count += 1;
            if (cell->ready) {
                if (missing || !valid_cell(cell))
                    return false;
                ready += 1;
            }
        }
        if (ready != 0 && ready != count)
            return false;
        if (ready == 0)
            missing = true;
    }
    return true;
}

Status mus_score_table_write(FILE *stream, const MusScoreTable *table) {
    if (stream == NULL || !valid_table(table))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (fprintf(stream, "%s base=%s target=%u orientation=mano utility=signed-one\n",
                MUS_SCORE_SCHEMA_ID, MUS_ORDAGO_STRATEGY_SCHEMA_ID,
                table->target) < 0)
        return CFR_STATUS_IO_ERROR;
    for (unsigned int a = 0; a < table->target; a += 1) {
        for (unsigned int b = 0; b < table->target; b += 1) {
            const MusScoreCell *cell = &table->cells[a][b];
            if (cell->ready &&
                fprintf(stream, "%u %u %.17g %zu %zu %" PRIu64 "\n", a, b, cell->value,
                        cell->iterations, cell->evaluation_samples, cell->training_seed) < 0)
                return CFR_STATUS_IO_ERROR;
        }
    }
    return ferror(stream) ? CFR_STATUS_IO_ERROR : CFR_STATUS_SUCCESS;
}

static bool parse_unsigned_token(const char *text, uintmax_t maximum,
                                  uintmax_t *value) {
    char *end;
    if (text[0] < '0' || text[0] > '9')
        return false;
    errno = 0;
    const uintmax_t parsed = strtoumax(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > maximum)
        return false;
    *value = parsed;
    return true;
}

Status mus_score_table_read(FILE *stream, MusScoreTable *table) {
    char header[512];
    char expected[512];
    unsigned int target;
    int consumed = 0;
    MusScoreTable temporary;
    if (stream == NULL || table == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (fgets(header, sizeof(header), stream) == NULL ||
        sscanf(header, MUS_SCORE_SCHEMA_ID " base=" MUS_ORDAGO_STRATEGY_SCHEMA_ID
                       " target=%u orientation=mano utility=signed-one%n",
               &target, &consumed) != 1 || consumed == 0 ||
        mus_score_table_init(&temporary, target) != CFR_STATUS_SUCCESS)
        return CFR_STATUS_FORMAT_ERROR;
    (void)snprintf(expected, sizeof(expected),
                   "%s base=%s target=%u orientation=mano utility=signed-one\n",
                   MUS_SCORE_SCHEMA_ID, MUS_ORDAGO_STRATEGY_SCHEMA_ID, target);
    if (strcmp(expected, header) != 0)
        return CFR_STATUS_FORMAT_ERROR;
    while (fgets(header, sizeof(header), stream) != NULL) {
        char tokens[6][64];
        char extra;
        uintmax_t a, b, iterations, samples, seed;
        if (sscanf(header, "%63s %63s %63s %63s %63s %63s %c", tokens[0],
                   tokens[1], tokens[2], tokens[3], tokens[4], tokens[5], &extra) != 6 ||
            !parse_unsigned_token(tokens[0], target - 1, &a) ||
            !parse_unsigned_token(tokens[1], target - 1, &b) ||
            !parse_unsigned_token(tokens[3], SIZE_MAX, &iterations) ||
            !parse_unsigned_token(tokens[4], SIZE_MAX, &samples) ||
            !parse_unsigned_token(tokens[5], UINT64_MAX, &seed))
            return CFR_STATUS_FORMAT_ERROR;
        char *end;
        errno = 0;
        const double value = strtod(tokens[2], &end);
        if (errno != 0 || *end != '\0' || end == tokens[2])
            return CFR_STATUS_FORMAT_ERROR;
        const MusScoreCell cell = {
            .ready = true, .value = value, .iterations = (size_t)iterations,
            .evaluation_samples = (size_t)samples, .training_seed = (uint64_t)seed,
        };
        if (temporary.cells[a][b].ready || !valid_cell(&cell))
            return CFR_STATUS_FORMAT_ERROR;
        temporary.cells[a][b] = cell;
    }
    if (ferror(stream))
        return CFR_STATUS_IO_ERROR;
    if (!valid_table(&temporary))
        return CFR_STATUS_FORMAT_ERROR;
    *table = temporary;
    return CFR_STATUS_SUCCESS;
}

Status mus_score_terminal_utility(const MusGameState *state, Player player,
                                  Utility *utility) {
    if (state == NULL || utility == NULL ||
        (player != CFR_PLAYER_0 && player != CFR_PLAYER_1) ||
        state->model.phase != MUS_PHASE_TERMINAL || state->continuation == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    const MusGameModel *model = &state->model;
    const unsigned int a = model->initial_scores[0] + model->settled_scores[0];
    const unsigned int b = model->initial_scores[1] + model->settled_scores[1];
    if (model->score_target == 0 || model->score_target > MUS_ORDAGO_WAGER ||
        model->score_target != state->continuation->target)
        return CFR_STATUS_INVALID_ARGUMENT;
    double value;
    if (a >= model->score_target && b >= model->score_target)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (a >= model->score_target) {
        value = 1;
    } else if (b >= model->score_target) {
        value = -1;
    } else {
        if (a + b <= (unsigned int)(model->initial_scores[0] + model->initial_scores[1]))
            return CFR_STATUS_INVALID_ARGUMENT;
        const MusScoreCell *next = &state->continuation->cells[b][a];
        if (!next->ready)
            return CFR_STATUS_NOT_FOUND;
        if (!valid_cell(next))
            return CFR_STATUS_NUMERIC_ERROR;
        value = -next->value;
    }
    *utility = player == CFR_PLAYER_0 ? value : -value;
    return CFR_STATUS_SUCCESS;
}

Status mus_score_game_set_schema(MusScoreGame *game) {
    if (game == NULL || game->continuation == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    /* Hash textual doubles to keep the schema independent of byte order. */
    uint64_t hash = UINT64_C(14695981039346656037);
    const MusScoreTable *table = game->continuation;
    const unsigned int sum = game->initial_scores[0] + game->initial_scores[1];
    for (unsigned int a = 0; a < table->target; a += 1) {
        for (unsigned int b = 0; b < table->target; b += 1) {
            if (a + b <= sum)
                continue;
            const MusScoreCell *cell = &table->cells[a][b];
            char text[96];
            const int length = snprintf(text, sizeof(text), "%u,%u,%u,%.17g;",
                                         a, b, cell->ready ? 1U : 0U,
                                         cell->ready ? cell->value : 0.0);
            if (length < 0 || (size_t)length >= sizeof(text))
                return CFR_STATUS_INVALID_ARGUMENT;
            for (int i = 0; i < length; i += 1) {
                hash ^= (unsigned char)text[i];
                hash *= UINT64_C(1099511628211);
            }
        }
    }
    const int length = snprintf(game->schema, sizeof(game->schema),
                                "%s/base-%s/target-%u/score-%u-%u/mano/signed-one/V-%016" PRIx64,
                                MUS_SCORE_SCHEMA_ID, MUS_ORDAGO_STRATEGY_SCHEMA_ID,
                                table->target, game->initial_scores[0],
                                game->initial_scores[1], hash);
    return length > 0 && (size_t)length < sizeof(game->schema)
               ? CFR_STATUS_SUCCESS : CFR_STATUS_INVALID_ARGUMENT;
}

static double random_unit(uint64_t *state) {
    *state += UINT64_C(0x9e3779b97f4a7c15);
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    z ^= z >> 31;
    return (double)(z >> 11) * 0x1p-53;
}

Status mus_score_evaluate(const MusScoreGame *game, const InfoStore *store,
                          size_t samples, uint64_t seed, double *value) {
    if (game == NULL || store == NULL || value == NULL || samples == 0)
        return CFR_STATUS_INVALID_ARGUMENT;
    MusGameState *state = malloc(sizeof(*state));
    if (state == NULL)
        return CFR_STATUS_OUT_OF_MEMORY;
    const GameOperations *ops = game->game.trusted_operations;
    const void *context = game->game.context;
    double mean = 0;
    Status status = mus_score_game_state_init(state, game);
    if (status != CFR_STATUS_SUCCESS) {
        free(state);
        return status;
    }
    /* Only the active state needs resetting. Each action writes its undo
     * entry before that entry can be read. */
    unsigned char root[offsetof(MusGameState, undo_history)];
    memcpy(root, state, sizeof(root));
    for (size_t sample = 0; sample < samples; sample += 1) {
        memcpy(state, root, sizeof(root));
        state->undo_count = 0;
        GameState *raw = mus_game_state_as_game_state(state);
        while (state->model.phase != MUS_PHASE_TERMINAL) {
            Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
            Probability probabilities[MUS_GAME_MAX_LEGAL_ACTIONS];
            size_t count = 0;
            Actor actor;
            status = ops->current_actor(context, raw, &actor);
            if (status != CFR_STATUS_SUCCESS)
                break;
            if (actor.kind == CFR_ACTOR_CHANCE) {
                status = ops->chance_outcomes(context, raw, actions, probabilities,
                                              MUS_GAME_MAX_LEGAL_ACTIONS, &count);
            } else {
                InfoSetKey key;
                const InfoNode *node;
                status = ops->legal_actions(context, raw, actions,
                                            MUS_GAME_MAX_LEGAL_ACTIONS, &count);
                if (status != CFR_STATUS_SUCCESS)
                    break;
                status = ops->information_set_key(context, raw, &key);
                if (status != CFR_STATUS_SUCCESS)
                    break;
                status = cfr_info_store_find_const(store, key, &node);
                if (status == CFR_STATUS_NOT_FOUND) {
                    for (size_t i = 0; i < count; i += 1)
                        probabilities[i] = 1.0 / (double)count;
                    status = CFR_STATUS_SUCCESS;
                } else if (status == CFR_STATUS_SUCCESS) {
                    status = node->action_count == count
                                 ? cfr_info_node_average_strategy(node, probabilities, count)
                                 : CFR_STATUS_INVALID_ARGUMENT;
                }
            }
            if (status != CFR_STATUS_SUCCESS)
                break;
            double draw = random_unit(&seed);
            size_t selected = 0;
            while (selected + 1 < count && draw >= probabilities[selected]) {
                draw -= probabilities[selected];
                selected += 1;
            }
            status = ops->apply_action(context, raw, actions[selected]);
            if (status != CFR_STATUS_SUCCESS)
                break;
        }
        if (status != CFR_STATUS_SUCCESS)
            break;
        Utility utility;
        status = mus_score_terminal_utility(state, CFR_PLAYER_0, &utility);
        if (status != CFR_STATUS_SUCCESS)
            break;
        mean += (utility - mean) / (double)(sample + 1);
    }
    free(state);
    if (status == CFR_STATUS_SUCCESS)
        *value = mean;
    return status;
}

typedef struct {
    const MusScoreTable *table;
    const MusScoreTrainingOptions *options;
    unsigned int a;
    unsigned int b;
    MusScoreGame game;
    MusGameState state;
    InfoStore store;
    Trainer trainer;
    MusScoreCell cell;
    MusScorePreloadStats preload_stats;
    MusScorePrior prior;
    Status status;
} ScoreTrainingWorker;

static Status report_score_training(const Trainer *trainer, size_t completed,
                                     void *context) {
    const ScoreTrainingWorker *worker = context;
    return worker->options->progress(&worker->game, trainer, completed,
        &worker->preload_stats, worker->options->progress_context);
}

static void *train_score_cell(void *raw_worker) {
    ScoreTrainingWorker *worker = raw_worker;
    const MusScoreTrainingOptions *options = worker->options;
    const size_t threads = options->threads_per_cell == 0
                               ? 1 : options->threads_per_cell;
    uint64_t seed = options->seed +
        (uint64_t)(worker->a * MUS_ORDAGO_WAGER + worker->b);
    worker->store = (InfoStore){0};
    worker->trainer = (Trainer){0};
    worker->preload_stats = (MusScorePreloadStats){0};
    Status status = mus_score_game_init(&worker->game, worker->table,
                                        worker->a, worker->b);
    if (status == CFR_STATUS_SUCCESS && options->extension != NULL) {
        const MusScoreExtension *extension = options->extension;
        const MusScoreCell *source_cell = &extension->table->cells[worker->a][worker->b];
        MusScoreGame source_game;
        seed = source_cell->training_seed;
        status = mus_score_game_init(&source_game, extension->table, worker->a, worker->b);
        if (status == CFR_STATUS_SUCCESS)
            status = mus_score_game_state_init(&worker->state, &source_game);
        if (status == CFR_STATUS_SUCCESS)
            status = extension->load_cell(&source_game, &worker->state, &worker->store,
                                           &worker->trainer, extension->context);
        if (status == CFR_STATUS_SUCCESS &&
            (worker->trainer.game != &source_game.game ||
             worker->trainer.state != mus_game_state_as_game_state(&worker->state) ||
             worker->trainer.store != &worker->store ||
             worker->trainer.variant != CFR_TRAINER_VARIANT_MCCFR_EXTERNAL ||
             worker->trainer.training_iterations != source_cell->iterations))
            status = CFR_STATUS_INCOMPATIBLE_GAME;
        if (status == CFR_STATUS_SUCCESS) {
            /* Loading still enforces the old continuation hash. Only after a
             * valid load do we use the new payoff model with the same keys. */
            status = mus_score_game_state_init(&worker->state, &worker->game);
            worker->trainer.game = &worker->game.game;
        }
    } else if (status == CFR_STATUS_SUCCESS) {
        status = mus_score_game_state_init(&worker->state, &worker->game);
        if (status == CFR_STATUS_SUCCESS)
            status = cfr_info_store_init(&worker->store);
        if (status == CFR_STATUS_SUCCESS)
            status = cfr_trainer_init_mccfr(
                &worker->trainer, &worker->game.game,
                mus_game_state_as_game_state(&worker->state), &worker->store, seed);
    }
    /* Prepare before reservation or preload to avoid growing a temporary
     * sequential hash table that would immediately need conversion. */
    if (status == CFR_STATUS_SUCCESS && threads > 1)
        status = cfr_info_store_prepare_concurrent(&worker->store);
    if (status == CFR_STATUS_SUCCESS && options->reserve_information_sets != 0)
        status = cfr_info_store_reserve(&worker->store,
                                        options->reserve_information_sets);
    if (status == CFR_STATUS_SUCCESS && options->preload != NULL)
        status = mus_score_prior_attach(&worker->prior, &worker->game, options->preload,
                                         &worker->store, &worker->preload_stats);
    if (status == CFR_STATUS_SUCCESS)
        status = mus_training_run(&worker->trainer, options->iterations, threads,
            options->report_every == 0 ? options->iterations : options->report_every,
            options->progress == NULL ? NULL : report_score_training, worker);
    worker->cell = (MusScoreCell){
        .ready = true, .iterations = worker->trainer.training_iterations,
        .evaluation_samples = options->evaluation_samples, .training_seed = seed,
    };
    if (status == CFR_STATUS_SUCCESS)
        status = mus_score_evaluate(&worker->game, &worker->store,
                                    options->evaluation_samples,
                                    seed ^ UINT64_C(0xd1b54a32d192ed03),
                                    &worker->cell.value);
    worker->status = status;
    return NULL;
}

Status mus_score_train(MusScoreTable *table, unsigned int minimum_sum,
                       const MusScoreTrainingOptions *options,
                       MusScoreSaveCell save_cell,
                       MusScoreSaveDiagonal save_diagonal, void *context) {
    if (!valid_table(table) || options == NULL || options->iterations == 0 ||
        options->evaluation_samples == 0 ||
        minimum_sum > 2U * (unsigned int)(table->target - 1))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (options->extension != NULL) {
        const MusScoreExtension *extension = options->extension;
        if (extension->table == table || !valid_table(extension->table) ||
            extension->table->target != table->target || extension->load_cell == NULL)
            return CFR_STATUS_INVALID_ARGUMENT;
        for (unsigned int a = 0; a < table->target; ++a) {
            for (unsigned int b = 0; b < table->target; ++b) {
                if (a + b < minimum_sum)
                    continue;
                const MusScoreCell *source = &extension->table->cells[a][b];
                const MusScoreCell *destination = &table->cells[a][b];
                if (!source->ready)
                    return CFR_STATUS_NOT_FOUND;
                if (source->iterations > SIZE_MAX - options->iterations)
                    return CFR_STATUS_INVALID_ARGUMENT;
                if (destination->ready &&
                    (destination->iterations != source->iterations + options->iterations ||
                     destination->evaluation_samples != options->evaluation_samples ||
                     destination->training_seed != source->training_seed))
                    return CFR_STATUS_INCOMPATIBLE_GAME;
            }
        }
    }
    size_t worker_count = options->threads == 0 ? 1 : options->threads;
    if (worker_count > table->target)
        worker_count = table->target;
    ScoreTrainingWorker *workers = calloc(worker_count, sizeof(*workers));
    if (workers == NULL)
        return CFR_STATUS_OUT_OF_MEMORY;
    Status status = CFR_STATUS_SUCCESS;
    for (int sum = 2 * (table->target - 1); sum >= (int)minimum_sum; sum -= 1) {
        MusScoreCell batch[MUS_ORDAGO_WAGER] = {0};
        unsigned int scores[MUS_ORDAGO_WAGER];
        size_t count = 0;
        for (int a = 0; a < table->target; a += 1) {
            const int b = sum - a;
            if (b < 0 || b >= table->target || table->cells[a][b].ready)
                continue;
            scores[count++] = (unsigned int)a;
        }
        for (size_t offset = 0; offset < count; offset += worker_count) {
            const size_t remaining = count - offset;
            const size_t active = remaining < worker_count ? remaining : worker_count;
            pthread_t threads[MUS_ORDAGO_WAGER - 1];
            size_t created = 0;
            for (size_t i = 0; i < active; i += 1) {
                workers[i].table = table;
                workers[i].options = options;
                workers[i].a = scores[offset + i];
                workers[i].b = (unsigned int)sum - workers[i].a;
            }
            for (size_t i = 1; i < active; i += 1) {
                if (pthread_create(&threads[created], NULL, train_score_cell,
                                    &workers[i]) != 0)
                    break;
                created += 1;
            }
            (void)train_score_cell(&workers[0]);
            /* If a thread cannot start, the caller completes its cells. */
            for (size_t i = created + 1; i < active; i += 1)
                (void)train_score_cell(&workers[i]);
            for (size_t i = 0; i < created; i += 1)
                (void)pthread_join(threads[i], NULL);

            /* Callbacks run on the caller, in the same order as serial
             * training. Keep each store alive until its callback returns. */
            for (size_t i = 0; i < active; i += 1) {
                ScoreTrainingWorker *worker = &workers[i];
                if (status == CFR_STATUS_SUCCESS) {
                    if (options->preload_stats != NULL)
                        *options->preload_stats = worker->preload_stats;
                    status = worker->status;
                    if (status == CFR_STATUS_SUCCESS && save_cell != NULL)
                        status = save_cell(&worker->game, &worker->trainer,
                                            &worker->cell, context);
                    if (status == CFR_STATUS_SUCCESS)
                        batch[worker->a] = worker->cell;
                }
                (void)cfr_info_store_destroy(&worker->store);
            }
            if (status != CFR_STATUS_SUCCESS)
                goto done;
        }
        if (count == 0)
            continue;
        for (int a = 0; a < table->target; a += 1) {
            if (batch[a].ready)
                table->cells[a][sum - a] = batch[a];
        }
        if (save_diagonal != NULL) {
            status = save_diagonal(table, context);
            if (status != CFR_STATUS_SUCCESS)
                goto done;
        }
    }
done:
    free(workers);
    return status;
}
