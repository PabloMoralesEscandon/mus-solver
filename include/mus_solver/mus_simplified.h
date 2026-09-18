#ifndef MUS_SOLVER_MUS_SIMPLIFIED_H
#define MUS_SOLVER_MUS_SIMPLIFIED_H

#include "mus_solver/mus_four.h"

#if MUS_GAME_MAX_MUSES == 1
#define MUS_SIMPLIFIED_STRATEGY_SCHEMA_ID "mus.heads-up-single-hand.ordago-simplified/v1-muses-1"
#define MUS_FOUR_SIMPLIFIED_STRATEGY_SCHEMA_ID "mus.four-partnership-single-hand.ordago-simplified/v2-muses-1"
#else
#define MUS_SIMPLIFIED_STRATEGY_SCHEMA_ID "mus.heads-up-single-hand.ordago-simplified/v1-muses-2"
#define MUS_FOUR_SIMPLIFIED_STRATEGY_SCHEMA_ID "mus.four-partnership-single-hand.ordago-simplified/v2-muses-2"
#endif

/* Use the corresponding public single-hand state initializer. The simplified
 * descriptor keeps the base observations and scoring, but changes the menus.
 * Numeric increments are 2, 4 and 10 and must strictly increase in each lance.
 * There is no additional action-count limit. Ordago remains available. */
const Game *mus_simplified_game_descriptor(bool four_player);
bool mus_simplified_game_matches(const Game *game);
const Game *mus_simplified_game_base(const Game *game);
Status mus_four_simplified_evaluate_with_options(const InfoStore *store,
    const MusScoreEvaluationOptions *options, uint64_t seed, MusScoreEvaluation *result);

/* Decode keys with the corresponding base codec, then use these menus.
 * Checkpoints and text strategies use separate simplified schemas. */
Status mus_simplified_ordago_information_set_legal_actions(
    const MusOrdagoInformationSet *information, Action *actions,
    size_t capacity, size_t *required_count);
Status mus_simplified_four_information_set_legal_actions(
    const MusFourInformationSet *information, bool score_enabled,
    Action *actions, size_t capacity, size_t *required_count);

#endif
