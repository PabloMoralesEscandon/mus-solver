#ifndef MUS_SOLVER_MUS_GAME_H
#define MUS_SOLVER_MUS_GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cfr/game.h"

#define MUS_GAME_PLAYER_COUNT 2
#define MUS_GAME_CLASS_COUNT 8
#define MUS_GAME_HAND_SIZE 4
#define MUS_GAME_LANCE_COUNT 4
#define MUS_GAME_UNDO_CAPACITY 160
#define MUS_GAME_MAX_LEGAL_ACTIONS 16
#define MUS_GAME_DISCARD_MASK_COUNT 16

#ifndef MUS_GAME_MAX_MUSES
#define MUS_GAME_MAX_MUSES 1
#endif

#if MUS_GAME_MAX_MUSES != 1 && MUS_GAME_MAX_MUSES != 2
#error "MUS_GAME_MAX_MUSES must be 1 or 2"
#endif

#if MUS_GAME_MAX_MUSES == 1
#define MUS_ORDAGO_STRATEGY_SCHEMA_ID                                   \
    "mus.heads-up-single-hand.ordago-with-mus/v6-muses-1"
#else
#define MUS_ORDAGO_STRATEGY_SCHEMA_ID                                   \
    "mus.heads-up-single-hand.ordago-with-mus/v6-muses-2"
#endif

#define MUS_SINGLE_BET_WAGER 2
#define MUS_RAISES_TO_EIGHT_MIN_WAGER 2
#define MUS_RAISES_TO_EIGHT_MAX_WAGER 8
#define MUS_ORDAGO_MIN_WAGER 2
#define MUS_ORDAGO_WAGER 40

typedef enum {
    MUS_CLASS_PITOS,
    MUS_CLASS_FOURS,
    MUS_CLASS_FIVES,
    MUS_CLASS_SIXES,
    MUS_CLASS_SEVENS,
    MUS_CLASS_JACKS,
    MUS_CLASS_KNIGHTS,
    MUS_CLASS_KINGS
} MusCardClass;

typedef enum {
    MUS_PHASE_DEAL_PLAYER_0,
    MUS_PHASE_DEAL_PLAYER_1,
    MUS_PHASE_GRANDE,
    MUS_PHASE_CHICA,
    MUS_PHASE_PARES,
    MUS_PHASE_JUEGO,
    MUS_PHASE_PUNTO,
    MUS_PHASE_TERMINAL,
    MUS_PHASE_MUS_PLAYER_0,
    MUS_PHASE_MUS_PLAYER_1,
    MUS_PHASE_DISCARD_PLAYER_0,
    MUS_PHASE_DISCARD_PLAYER_1,
    MUS_PHASE_REDRAW_PLAYER_0,
    MUS_PHASE_REDRAW_PLAYER_1
} MusGamePhase;

typedef enum {
    MUS_BET_FIRST_TO_ACT,
    MUS_BET_SECOND_AFTER_PASS,
    MUS_BET_RESPONSE
} MusBetStage;

typedef enum {
    MUS_LANCE_UNRESOLVED,
    MUS_LANCE_ALL_PASS,
    MUS_LANCE_FIRST_BET_REJECTED,
    MUS_LANCE_FIRST_BET_ACCEPTED,
    MUS_LANCE_SECOND_BET_REJECTED,
    MUS_LANCE_SECOND_BET_ACCEPTED,
    MUS_LANCE_SKIPPED
} MusLanceOutcome;

typedef enum {
    MUS_LANCE_STRENGTH_NONE,
    MUS_LANCE_STRENGTH_PLAYER_0,
    MUS_LANCE_STRENGTH_PLAYER_1,
    MUS_LANCE_STRENGTH_BOTH,
    MUS_LANCE_STRENGTH_PARES_SKIPPED_NONE,
    MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_0,
    MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_1
} MusLanceStrength;

typedef enum {
    MUS_PAIRS_PAIR,
    MUS_PAIRS_MEDIAS,
    MUS_PAIRS_DUPLES
} MusPairsType;

typedef enum {
    MUS_ORDAGO_HAND_EXACT,
    MUS_ORDAGO_HAND_GRANDE,
    MUS_ORDAGO_HAND_CHICA,
    MUS_ORDAGO_HAND_PARES,
    MUS_ORDAGO_HAND_JUEGO,
    MUS_ORDAGO_HAND_PUNTO
} MusOrdagoHandKind;

typedef enum {
    MUS_ACTION_TAKE_0 = 0,
    MUS_ACTION_TAKE_1,
    MUS_ACTION_TAKE_2,
    MUS_ACTION_TAKE_3,
    MUS_ACTION_TAKE_4,
    MUS_ACTION_PASS,
    MUS_ACTION_BET_TWO,
    MUS_ACTION_REJECT,
    MUS_ACTION_ACCEPT,
    MUS_ACTION_RAISE_DOUBLE,
    MUS_ACTION_MUS,
    MUS_ACTION_NO_MUS,
    MUS_ACTION_BET_FOUR,
    MUS_ACTION_BET_SEVEN,
    MUS_ACTION_BET_TEN,
    MUS_ACTION_ORDAGO,
    MUS_ACTION_DISCARD_MASK_0
} MusGameAction;

#define MUS_ACTION_DISCARD_MASK(mask)                                         \
    ((Action)(MUS_ACTION_DISCARD_MASK_0 + (mask)))
#define MUS_ACTION_DISCARD_MASK_VALUE(action)                                 \
    ((unsigned int)((action) - MUS_ACTION_DISCARD_MASK_0))

typedef struct {
    MusGamePhase phase;
    uint8_t hands[MUS_GAME_PLAYER_COUNT][MUS_GAME_CLASS_COUNT];
    uint8_t deal_class;
    uint8_t deal_remaining;
    uint8_t deal_later_available;
    uint8_t out_of_play[MUS_GAME_CLASS_COUNT];
    uint8_t mus_count;
    uint8_t discard_masks[MUS_GAME_MAX_MUSES][MUS_GAME_PLAYER_COUNT];
    uint8_t discard_counts[MUS_GAME_MAX_MUSES][MUS_GAME_PLAYER_COUNT];
    bool mus_stopped;
    Player mus_stop_player;
    /* Total offered amount awaiting a response; zero outside a response. */
    uint8_t current_wager;
    /* Amount covered before the current offer. */
    uint8_t previous_wager;
    /* Settled numeric wager total from completed lances, capped at ordago. */
    uint8_t prior_wager_total;
    bool current_wager_is_ordago;
    MusLanceStrength current_strength;
    Player current_player;
    MusBetStage bet_stage;
    Player bettor;
    MusLanceOutcome outcomes[MUS_GAME_LANCE_COUNT];
    /* Final offered amount and the amount covered before it. */
    uint8_t outcome_wagers[MUS_GAME_LANCE_COUNT];
    uint8_t outcome_previous_wagers[MUS_GAME_LANCE_COUNT];
    bool outcome_wager_is_ordago[MUS_GAME_LANCE_COUNT];
    MusLanceStrength outcome_strengths[MUS_GAME_LANCE_COUNT];
    bool pairs_revealed;
    bool pairs[MUS_GAME_PLAYER_COUNT];
    bool game_revealed;
    bool game[MUS_GAME_PLAYER_COUNT];
} MusGameModel;

typedef struct {
    int grande_key;
    int chica_key;
    int pairs_key;
    int pairs_points;
    int game_key;
    int game_points;
    int point_value;
} MusHandSummary;

typedef struct {
    MusGameModel model;
    MusHandSummary hand_summaries[MUS_GAME_PLAYER_COUNT];
    MusGameModel undo_history[MUS_GAME_UNDO_CAPACITY];
    MusHandSummary undo_hand_summaries[MUS_GAME_UNDO_CAPACITY]
                                           [MUS_GAME_PLAYER_COUNT];
    size_t undo_count;
} MusGameState;

typedef struct {
    Player player;
    uint8_t hand[MUS_GAME_CLASS_COUNT];
    MusGamePhase phase;
    uint8_t mus_count;
    uint8_t discard_counts[MUS_GAME_MAX_MUSES][MUS_GAME_PLAYER_COUNT];
    bool mus_stopped;
    Player mus_stop_player;
    bool pairs_revealed;
    bool pairs[MUS_GAME_PLAYER_COUNT];
    bool game_revealed;
    bool game[MUS_GAME_PLAYER_COUNT];
    MusLanceOutcome outcomes[MUS_GAME_LANCE_COUNT];
    uint8_t outcome_wagers[MUS_GAME_LANCE_COUNT];
    uint8_t outcome_previous_wagers[MUS_GAME_LANCE_COUNT];
    uint8_t outcome_wager_histories[MUS_GAME_LANCE_COUNT];
    MusBetStage bet_stage;
    Player bettor;
    uint8_t current_wager;
    uint8_t previous_wager;
    uint8_t current_wager_history;
} MusInformationSet;

/*
 * Explicit format names. MusSingleBet* is the original game: one wager of
 * two stones and no raises. MusRaisesToEight* allows double raises 2 -> 4 ->
 * 8. The aliases deliberately share the card-dealing representation while
 * their Game descriptors enforce different betting rules and infoset keys.
 */
typedef MusGameModel MusSingleBetGameModel;
typedef MusGameState MusSingleBetGameState;
typedef MusInformationSet MusSingleBetInformationSet;

typedef MusGameModel MusRaisesToEightGameModel;
typedef MusGameState MusRaisesToEightGameState;
typedef MusInformationSet MusRaisesToEightInformationSet;

typedef MusGameModel MusOrdagoGameModel;
typedef MusGameState MusOrdagoGameState;

typedef struct {
    Player player;
    MusGamePhase phase;
    MusBetStage bet_stage;
    MusOrdagoHandKind hand_kind;
    /* Present only during mus and discard decisions. */
    uint8_t hand[MUS_GAME_CLASS_COUNT];
    /* Grande abstraction. */
    uint8_t king_count;
    MusCardClass highest_non_king;
    bool has_highest_non_king;
    /* Chica abstraction. */
    uint8_t pito_count;
    MusCardClass lowest_non_pito;
    bool has_lowest_non_pito;
    /* Pares abstraction. */
    MusPairsType pairs_type;
    MusCardClass pairs_value;
    MusCardClass second_pairs_value;
    bool has_second_pairs_value;
    /* Juego or punto abstraction. */
    uint8_t game_value;
    uint8_t point_value;
    uint8_t mus_count;
    uint8_t discard_counts[MUS_GAME_MAX_MUSES][MUS_GAME_PLAYER_COUNT];
    bool mus_stopped;
    Player mus_stop_player;
    MusLanceStrength previous_strengths[MUS_GAME_LANCE_COUNT - 1];
    uint8_t previous_strength_count;
    uint8_t prior_wager_total;
    Player bettor;
    uint8_t current_wager;
    uint8_t previous_wager;
    bool current_wager_is_ordago;
} MusOrdagoInformationSet;

Status mus_game_state_init(MusGameState *state);
const Game *mus_game_descriptor(void);
GameState *mus_game_state_as_game_state(MusGameState *state);
const GameState *mus_game_state_as_game_state_const(const MusGameState *state);

Status mus_game_information_set_decode(InfoSetKey key,
                                       MusInformationSet *information_set);

/* Explicit API for the original single wager of two stones. */
Status mus_single_bet_game_state_init(MusSingleBetGameState *state);
const Game *mus_single_bet_game_descriptor(void);
GameState *mus_single_bet_game_state_as_game_state(
    MusSingleBetGameState *state);
const GameState *mus_single_bet_game_state_as_game_state_const(
    const MusSingleBetGameState *state);
Status mus_single_bet_game_information_set_decode(
    InfoSetKey key, MusSingleBetInformationSet *information_set);

/* Explicit API for wagers and double raises capped at eight stones. */
Status mus_raises_to_eight_game_state_init(MusRaisesToEightGameState *state);
const Game *mus_raises_to_eight_game_descriptor(void);
GameState *mus_raises_to_eight_game_state_as_game_state(
    MusRaisesToEightGameState *state);
const GameState *mus_raises_to_eight_game_state_as_game_state_const(
    const MusRaisesToEightGameState *state);
Status mus_raises_to_eight_game_information_set_decode(
    InfoSetKey key, MusRaisesToEightInformationSet *information_set);

/* Additive wagers 2/4/7/10/ordago plus configurable mus rounds. */
Status mus_ordago_game_state_init(MusOrdagoGameState *state);
const Game *mus_ordago_game_descriptor(void);
GameState *mus_ordago_game_state_as_game_state(MusOrdagoGameState *state);
const GameState *mus_ordago_game_state_as_game_state_const(
    const MusOrdagoGameState *state);
Status mus_ordago_game_information_set_decode(
    InfoSetKey key, MusOrdagoInformationSet *information_set);
/* Returns the ordered legal actions represented by a decoded information set. */
Status mus_ordago_game_information_set_legal_actions(
    const MusOrdagoInformationSet *information_set, Action *actions,
    size_t capacity, size_t *required_count);
bool mus_ordago_game_discard_mask_is_canonical(
    const uint8_t hand[MUS_GAME_CLASS_COUNT], unsigned int mask);

const char *mus_game_class_name(MusCardClass card_class);
const char *mus_game_phase_name(MusGamePhase phase);
const char *mus_game_outcome_name(MusLanceOutcome outcome);
const char *mus_single_bet_game_outcome_name(MusLanceOutcome outcome);
const char *mus_raises_to_eight_game_outcome_name(MusLanceOutcome outcome,
                                                  uint8_t wager);
const char *mus_ordago_game_outcome_name(MusLanceOutcome outcome,
                                         uint8_t wager);

#endif
