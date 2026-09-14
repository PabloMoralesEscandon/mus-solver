#ifndef MUS_SOLVER_MUS_FOUR_H
#define MUS_SOLVER_MUS_FOUR_H

#include "mus_solver/mus_score.h"

#define MUS_FOUR_PLAYER_COUNT 4
#define MUS_FOUR_UNDO_CAPACITY 256
#if MUS_GAME_MAX_MUSES == 1
#define MUS_FOUR_STRATEGY_SCHEMA_ID "mus.four-partnership-score/v3-muses-1"
#else
#define MUS_FOUR_STRATEGY_SCHEMA_ID "mus.four-partnership-score/v3-muses-2"
#endif

typedef enum {
    MUS_FOUR_PHASE_DEAL,
    MUS_FOUR_PHASE_MUS,
    MUS_FOUR_PHASE_DISCARD,
    MUS_FOUR_PHASE_REDRAW,
    MUS_FOUR_PHASE_GRANDE,
    MUS_FOUR_PHASE_CHICA,
    MUS_FOUR_PHASE_PARES,
    MUS_FOUR_PHASE_JUEGO,
    MUS_FOUR_PHASE_PUNTO,
    MUS_FOUR_PHASE_TERMINAL
} MusFourPhase;

/* Ordered team proposals. Only the winning numeric proposal consumes a
 * wager action. Equal proposals select the first responding seat. */
typedef enum {
    MUS_FOUR_RESPONSE_NONE,
    MUS_FOUR_RESPONSE_REJECT,
    MUS_FOUR_RESPONSE_ACCEPT,
    MUS_FOUR_RESPONSE_BET_TWO,
    MUS_FOUR_RESPONSE_BET_FOUR,
    MUS_FOUR_RESPONSE_BET_TEN,
    MUS_FOUR_RESPONSE_ORDAGO
} MusFourResponse;

/* Seats 0 and 2 form team 0. Seats 1 and 3 form team 1. Seat 0 is mano.
 * Numeric wagers are increments. Every eligible teammate responds to the
 * original offer; the greater proposal becomes the team's action. */
typedef struct {
    uint8_t phase;
    uint8_t current_player;
    uint8_t hands[MUS_FOUR_PLAYER_COUNT][MUS_GAME_CLASS_COUNT];
    uint8_t out_of_play[MUS_GAME_CLASS_COUNT];
    uint8_t deal_class; /* Last drawn class. Chance actions are classes 0..7. */
    uint8_t deal_remaining; /* Cards still needed by current_player. */
    uint8_t mus_count;
    uint8_t discard_counts[MUS_GAME_MAX_MUSES][MUS_FOUR_PLAYER_COUNT];
    uint8_t discard_masks[MUS_GAME_MAX_MUSES][MUS_FOUR_PLAYER_COUNT];
    bool mus_stopped;
    uint8_t mus_stop_player;
    uint8_t score_target;
    uint8_t initial_scores[2];
    uint8_t settled_scores[2];
    uint8_t paid_scores[2];
    uint8_t prior_wager_total;
    uint8_t eligible_mask;
    uint8_t passed_mask;
    /* The first teammate's public proposal, while current_player responds.
     * Its author is current_player's teammate. The offer stays unchanged. */
    uint8_t pending_response;
    /* Most recent resolved reply, retained across lances until another reply
     * resolves. The second proposal belongs to the first player's teammate;
     * NONE means that only the first player was eligible. Both NONE means
     * no reply has resolved. Earlier resolved replies are abstracted away. */
    uint8_t last_response_player;
    uint8_t last_responses[2];
    uint8_t bettor;
    uint8_t current_wager;
    uint8_t previous_wager;
    bool current_wager_is_ordago;
    uint8_t used_wager_action_masks[MUS_FOUR_PLAYER_COUNT];
    uint8_t current_strength;
    uint8_t outcomes[MUS_GAME_LANCE_COUNT];
    uint8_t outcome_wagers[MUS_GAME_LANCE_COUNT];
    uint8_t outcome_bettors[MUS_GAME_LANCE_COUNT];
    uint8_t outcome_strengths[MUS_GAME_LANCE_COUNT];
} MusFourGameModel;

typedef struct {
    MusFourGameModel model;
    const MusScoreTable *continuation;
    MusHandSummary hand_summaries[MUS_FOUR_PLAYER_COUNT];
    size_t undo_count;
    MusFourGameModel undo_history[MUS_FOUR_UNDO_CAPACITY];
} MusFourGameState;

/* An explicit public abstraction. No field reads another seat's private
 * cards. Current declarations include all eligible seats. Prior strengths
 * use the same team-level categories as the two-player score game. */
typedef struct {
    uint8_t phase;
    uint8_t player;
    uint8_t hand[MUS_GAME_CLASS_COUNT];
    /* Exact multiset rank for mus/discard. Betting uses the same grande,
     * chica, pares, juego and punto abstractions as the score version. */
    uint16_t hand_code;
    uint8_t mus_count;
    uint8_t discard_counts[MUS_GAME_MAX_MUSES][MUS_FOUR_PLAYER_COUNT];
    bool mus_stopped;
    uint8_t mus_stop_player;
    uint8_t scores[2];
    uint8_t prior_wager_total;
    uint8_t previous_strengths[3];
    uint8_t eligible_mask;
    uint8_t bettor;
    uint8_t current_wager;
    uint8_t previous_wager;
    bool current_wager_is_ordago;
    uint8_t pending_response;
    uint8_t last_response_player;
    uint8_t last_responses[2];
    uint8_t used_wager_action_masks[MUS_FOUR_PLAYER_COUNT];
} MusFourInformationSet;

Status mus_four_score_game_init(MusScoreGame *game, const MusScoreTable *table,
                                unsigned int a, unsigned int b);
bool mus_four_score_game_matches(const Game *game);
Status mus_four_score_game_state_init(MusFourGameState *state,
                                      const MusScoreGame *game);
Status mus_four_score_game_observe(const MusFourGameState *state,
                                   MusFourInformationSet *information);
Status mus_four_score_information_set_encode(
    const MusFourInformationSet *information, InfoSetKey *key);
Status mus_four_score_information_set_decode(
    InfoSetKey key, MusFourInformationSet *information);
Status mus_four_score_information_set_legal_actions(
    const MusFourInformationSet *information, Action *actions,
    size_t capacity, size_t *required_count);
Status mus_four_score_strategy_write_text(FILE *stream, const Trainer *trainer);

#endif
