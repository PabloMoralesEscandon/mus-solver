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

/* Averages two fixed final-lance diagnostics (juego and punto). Both players'
 * hands are fixed within each deal, so each best response has no uncertainty
 * about the opponent's cards. Optimize separately, then average the metrics.
 * This is not root exploitability or a proven upper or lower bound on it. */
Status mus_ordago_evaluation_metrics(const InfoStore *store,
                                     EvaluationMetrics *metrics_out);
Status mus_ordago_evaluation_metrics_from_state(
    const MusOrdagoGameState *root, const InfoStore *store,
    EvaluationMetrics *metrics_out);

/* The same two fixed-deal diagnostics and limits, using public-deferred rules.
 * A larger imperfect-information diagnostic would need to aggregate weighted
 * compatible opponent hands at shared information sets before choosing the
 * best-response action. Averaging independent best responses does not do this. */
Status mus_ordago_public_evaluation_metrics(const InfoStore *store,
                                            EvaluationMetrics *metrics_out);
Status mus_ordago_simplified_evaluation_metrics(const InfoStore *store,
                                                EvaluationMetrics *metrics_out);
Status mus_ordago_public_evaluation_metrics_from_state(
    const MusOrdagoPublicGameState *root, const InfoStore *store,
    EvaluationMetrics *metrics_out);

#endif
