#ifndef MUS_SOLVER_MUS_EVALUATION_H
#define MUS_SOLVER_MUS_EVALUATION_H

#include "cfr/evaluation.h"
#include "cfr/info_store.h"
#include "mus_solver/mus_game.h"

/*
 * Evaluates the mus average strategy exactly without materializing the full
 * game tree. Missing information sets use a uniform policy.
 */
Status mus_evaluation_metrics(const InfoStore *store,
                              EvaluationMetrics *metrics_out);

/* Evaluates an already initialized subgame; primarily useful for diagnostics. */
Status mus_evaluation_metrics_from_state(const MusGameState *root,
                                         const InfoStore *store,
                                         EvaluationMetrics *metrics_out);

Status mus_single_bet_evaluation_metrics(const InfoStore *store,
                                         EvaluationMetrics *metrics_out);
Status mus_single_bet_evaluation_metrics_from_state(
    const MusSingleBetGameState *root, const InfoStore *store,
    EvaluationMetrics *metrics_out);

Status mus_raises_to_eight_evaluation_metrics(
    const InfoStore *store, EvaluationMetrics *metrics_out);
Status mus_raises_to_eight_evaluation_metrics_from_state(
    const MusRaisesToEightGameState *root, const InfoStore *store,
    EvaluationMetrics *metrics_out);

#endif
