#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "mus_solver/mus_four.h"

#include "mus_solver/mus_game.h"
#include "mus_solver/mus_score.h"
#include "mus_solver/mus_training.h"

typedef struct TrainingControl TrainingControl;

typedef struct {
    TrainingControl *control;
    Trainer trainer;
    size_t iterations;
    size_t interval;
    size_t index;
    size_t ready_blocks;
    bool finished;
} TrainingWorker;

struct TrainingControl {
    atomic_bool start;
    atomic_bool cancel;
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_cond_t resume;
    TrainingWorker *workers;
    size_t active_count;
    size_t iterations;
    size_t report_every;
    size_t completed;
    size_t released_blocks;
    TrainerProgressCallback callback;
    void *context;
    bool stop;
    Status status;
};

static bool supported_game(const Game *game) {
    return game != NULL &&
        (game == mus_game_descriptor() ||
         game == mus_single_bet_game_descriptor() ||
         game == mus_raises_to_eight_game_descriptor() ||
         game == mus_ordago_game_descriptor() ||
         game == mus_ordago_public_game_descriptor() || mus_score_game_matches_any(game));
}

static size_t saturated_add(size_t left, size_t right) {
    return left > SIZE_MAX - right ? SIZE_MAX : left + right;
}

static uint64_t parallel_worker_seed(uint64_t base_seed, size_t worker_index) {
    uint64_t value =
        base_seed + UINT64_C(0x9e3779b97f4a7c15) * (uint64_t)worker_index;

    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

/* All workers must be paused or joined before their counters are read. */
static Trainer aggregate_workers(const TrainingControl *control) {
    Trainer aggregate = control->workers[0].trainer;

    for (size_t index = 1; index < control->active_count; index += 1) {
        const Trainer *worker = &control->workers[index].trainer;

        aggregate.training_iterations = saturated_add(
            aggregate.training_iterations, worker->training_iterations);
        aggregate.stats.iterations = saturated_add(
            aggregate.stats.iterations, worker->stats.iterations);
        aggregate.stats.traversals = saturated_add(
            aggregate.stats.traversals, worker->stats.traversals);
        aggregate.stats.visited_nodes = saturated_add(
            aggregate.stats.visited_nodes, worker->stats.visited_nodes);
        aggregate.stats.errors = saturated_add(
            aggregate.stats.errors, worker->stats.errors);
    }
    return aggregate;
}

static Status thread_status(int error) {
    return error == EAGAIN || error == ENOMEM
               ? CFR_STATUS_OUT_OF_MEMORY : CFR_STATUS_INVALID_ARGUMENT;
}

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
                                      ? remaining : control->report_every;
            Status status = CFR_STATUS_SUCCESS;

            if (control->callback != NULL) {
                const Trainer aggregate = aggregate_workers(control);

                status = control->callback(&aggregate, control->completed,
                                            control->context);
            }
            if (status != CFR_STATUS_SUCCESS) {
                stop_training_locked(control, status);
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
        &worker->trainer, worker->iterations, worker->interval,
        control->active_count > 1, report_training_progress, worker);

    (void)pthread_mutex_lock(&control->mutex);
    worker->finished = true;
    if (status != CFR_STATUS_SUCCESS)
        stop_training_locked(control, status);
    (void)pthread_cond_broadcast(&control->changed);
    (void)pthread_mutex_unlock(&control->mutex);
}

static void *run_training_worker(void *context) {
    TrainingWorker *worker = context;
    TrainingControl *control = worker->control;

    while (!atomic_load_explicit(&control->start, memory_order_acquire))
        (void)sched_yield();
    if (!atomic_load_explicit(&control->cancel, memory_order_acquire))
        train_worker(worker);
    return NULL;
}

typedef struct {
    TrainerProgressCallback callback;
    void *context;
} SequentialProgress;

static Status report_sequential_progress(const Trainer *trainer,
                                          size_t completed, void *context) {
    const SequentialProgress *progress = context;
    const Trainer aggregate = *trainer;

    return progress->callback(&aggregate, completed, progress->context);
}

Status mus_training_run(Trainer *trainer, size_t iterations,
                         size_t thread_count, size_t report_every,
                         TrainerProgressCallback callback, void *context) {
    if (trainer == NULL || !supported_game(trainer->game) ||
        trainer->state == NULL || trainer->store == NULL || thread_count == 0 ||
        report_every == 0 ||
        (trainer->variant != CFR_TRAINER_VARIANT_CFR &&
         trainer->variant != CFR_TRAINER_VARIANT_CFR_PLUS &&
         trainer->variant != CFR_TRAINER_VARIANT_MCCFR_EXTERNAL) ||
        (thread_count > 1 &&
         trainer->variant != CFR_TRAINER_VARIANT_MCCFR_EXTERNAL))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (iterations == 0)
        return CFR_STATUS_SUCCESS;
    if (thread_count == 1) {
        SequentialProgress progress = {callback, context};

        return callback == NULL
                   ? cfr_trainer_run(trainer, iterations)
                   : cfr_trainer_run_with_callback(
                         trainer, iterations, report_every, false,
                         report_sequential_progress, &progress);
    }

    Status status = cfr_game_validate_state(trainer->game, trainer->state);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    size_t active_count = thread_count < iterations ? thread_count : iterations;
    if (active_count > report_every)
        active_count = report_every;

    TrainingWorker *workers = calloc(active_count, sizeof(*workers));
    const size_t state_size = mus_four_score_game_matches(trainer->game)
        ? sizeof(MusFourGameState) : sizeof(MusGameState);
    unsigned char *states = NULL;
    pthread_t *thread_handles = NULL;
    if (active_count > 1) {
        states = calloc(active_count - 1, state_size);
        thread_handles = calloc(active_count - 1, sizeof(*thread_handles));
    }
    if (workers == NULL ||
        (active_count > 1 && (states == NULL || thread_handles == NULL))) {
        status = CFR_STATUS_OUT_OF_MEMORY;
        goto cleanup;
    }

    TrainingControl control = {
        .workers = workers, .active_count = active_count,
        .iterations = iterations, .report_every = report_every,
        .callback = callback, .context = context,
        .status = CFR_STATUS_SUCCESS,
    };
    atomic_init(&control.start, false);
    atomic_init(&control.cancel, false);
    workers[0].trainer = *trainer;
    const uint64_t seed_base = trainer->mccfr_rng.state;
    for (size_t index = 1; index < active_count; index += 1) {
        GameState *state = (GameState *)(states + (index - 1) * state_size);
        memcpy(state, trainer->state, state_size);
        status = cfr_trainer_init_mccfr(
            &workers[index].trainer, trainer->game,
            state, trainer->store,
            parallel_worker_seed(seed_base, index));
        if (status != CFR_STATUS_SUCCESS)
            goto cleanup;
    }
    status = cfr_info_store_prepare_concurrent(trainer->store);
    if (status != CFR_STATUS_SUCCESS)
        goto cleanup;

    int error = pthread_mutex_init(&control.mutex, NULL);
    if (error != 0) {
        status = thread_status(error);
        goto cleanup;
    }
    error = pthread_cond_init(&control.changed, NULL);
    if (error != 0) {
        status = thread_status(error);
        goto destroy_mutex;
    }
    error = pthread_cond_init(&control.resume, NULL);
    if (error != 0) {
        status = thread_status(error);
        goto destroy_changed;
    }

    const size_t full_blocks = iterations / report_every;
    const size_t remainder = iterations % report_every;
    for (size_t index = 0; index < active_count; index += 1) {
        const size_t interval = report_every / active_count +
                                (index < report_every % active_count ? 1 : 0);

        workers[index].control = &control;
        workers[index].iterations =
            full_blocks * interval + remainder / active_count +
            (index < remainder % active_count ? 1 : 0);
        workers[index].interval = interval;
        workers[index].index = index;
    }
    size_t created = 0;
    for (size_t index = 1; index < active_count; index += 1) {
        error = pthread_create(&thread_handles[index - 1], NULL,
                                run_training_worker, &workers[index]);
        if (error != 0)
            break;
        created += 1;
    }
    if (error != 0)
        atomic_store_explicit(&control.cancel, true, memory_order_release);
    atomic_store_explicit(&control.start, true, memory_order_release);
    if (error == 0)
        train_worker(&workers[0]);
    for (size_t index = 0; index < created; index += 1) {
        const int join_error = pthread_join(thread_handles[index], NULL);

        if (error == 0 && join_error != 0)
            error = join_error;
    }
    *trainer = aggregate_workers(&control);
    status = error != 0 ? thread_status(error) : control.status;
    (void)pthread_cond_destroy(&control.resume);
destroy_changed:
    (void)pthread_cond_destroy(&control.changed);
destroy_mutex:
    (void)pthread_mutex_destroy(&control.mutex);
cleanup:
    free(thread_handles);
    free(states);
    free(workers);
    return status;
}
