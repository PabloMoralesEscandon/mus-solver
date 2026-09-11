#ifndef MUS_SOLVER_MUS_TRAINING_H
#define MUS_SOLVER_MUS_TRAINING_H

#include "cfr/trainer.h"

/*
 * Train one strategy with independent workers that share its InfoStore.
 *
 * trainer must be initialized with a canonical descriptor returned by
 * mus_game_descriptor, mus_single_bet_game_descriptor,
 * mus_raises_to_eight_game_descriptor, mus_ordago_game_descriptor, or
 * mus_ordago_public_game_descriptor, or with
 * a game accepted by mus_score_game_matches. Its state must be a valid
 * MusGameState. The helper checks the descriptor before copying the state.
 * The caller retains ownership of trainer, game, state, and store. The game
 * and any continuation table must remain unchanged throughout this call.
 * The caller must not otherwise access the mutable trainer, state, or store
 * while this call runs. Auxiliary workers receive independent state copies.
 *
 * thread_count and report_every must be positive. More than one requested
 * thread requires external-sampling MCCFR and prepares the store for
 * concurrent access. The active count is limited by iterations and
 * report_every. iterations is a total across workers, not a per-worker count.
 * Zero iterations succeed without preparing the store or calling callback.
 *
 * callback may be null. Otherwise, the caller thread invokes callback after
 * each report_every total iterations and after the final partial block. All
 * workers pause while callback observes a temporary aggregate trainer. The
 * callback must not retain that trainer pointer or modify borrowed objects.
 * completed counts iterations added by this call. The aggregate trainer's
 * counters include earlier training, and its RNG is the primary worker's RNG.
 * A callback error stops the workers and becomes the returned status.
 *
 * On return, trainer retains the primary worker's RNG and all committed
 * worker counters, including counters committed before a traversal error.
 * Auxiliary RNG states are discarded, as in legacy parallel ordago training.
 * Scheduling can change shared-store updates, so parallel replay is not
 * deterministic. After a traversal error, restore the primary state to its
 * root before training again, as required by cfr_trainer_run.
 * Allocation or thread resource failures return CFR_STATUS_OUT_OF_MEMORY.
 * Other thread setup failures return CFR_STATUS_INVALID_ARGUMENT. If thread
 * creation fails, no worker starts training.
 */
Status mus_training_run(Trainer *trainer, size_t iterations,
                         size_t thread_count, size_t report_every,
                         TrainerProgressCallback callback, void *context);

#endif
