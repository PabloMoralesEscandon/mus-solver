#ifndef MUS_SCORE_RECOVERY_H
#define MUS_SCORE_RECOVERY_H

#include "mus_solver/mus_score.h"

typedef struct {
    const char *directory;
    const char *source_directory;
    const MusScoreTable *source_table;
    const char *prior_path;
    bool prior_compressed;
    size_t iterations;
    size_t evaluation_samples;
    uint64_t seed;
    size_t threads;
    size_t threads_per_cell;
    size_t reserve_information_sets;
    size_t report_every;
    double prior_weight;
    double uniform_mix;
} ScoreRecoveryOptions;

typedef struct {
    ScoreRecoveryOptions options;
    FILE *journal;
    uint64_t prior_hash;
    char *records[MUS_ORDAGO_WAGER][MUS_ORDAGO_WAGER];
} ScoreRecovery;

/* Open after acquiring the destination's exclusive lock. Source files must
 * remain immutable for the run. Close after all workers have stopped. */
Status score_recovery_open(ScoreRecovery *recovery, const ScoreRecoveryOptions *options);
Status score_recovery_close(ScoreRecovery *recovery);
/* Called by the coordinator after publishing the checkpoint. */
Status score_recovery_save(ScoreRecovery *recovery, const MusScoreGame *game,
                          const MusScoreCell *cell, const char *checkpoint);
/* MusScoreRecoverCell callback. Reads immutable records loaded at startup. */
Status score_recovery_load(const MusScoreGame *game, MusScoreCell *cell, void *context);

#endif
