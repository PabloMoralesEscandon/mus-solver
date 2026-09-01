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
#define MUS_GAME_UNDO_CAPACITY 40
#define MUS_GAME_MAX_LEGAL_ACTIONS 5

#define MUS_SINGLE_BET_WAGER 2
#define MUS_RAISES_TO_EIGHT_MIN_WAGER 2
#define MUS_RAISES_TO_EIGHT_MAX_WAGER 8

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
    MUS_PHASE_TERMINAL
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
    MUS_ACTION_TAKE_0 = 0,
    MUS_ACTION_TAKE_1,
    MUS_ACTION_TAKE_2,
    MUS_ACTION_TAKE_3,
    MUS_ACTION_TAKE_4,
    MUS_ACTION_PASS,
    MUS_ACTION_BET_TWO,
    MUS_ACTION_REJECT,
    MUS_ACTION_ACCEPT,
    MUS_ACTION_RAISE_DOUBLE
} MusGameAction;

typedef struct {
    MusGamePhase phase;
    uint8_t hands[MUS_GAME_PLAYER_COUNT][MUS_GAME_CLASS_COUNT];
    uint8_t deal_class;
    uint8_t deal_remaining;
    Player current_player;
    MusBetStage bet_stage;
    Player bettor;
    /* Offered amount awaiting a response; zero outside MUS_BET_RESPONSE. */
    uint8_t current_wager;
    MusLanceOutcome outcomes[MUS_GAME_LANCE_COUNT];
    /* Final offered amount; a rejected raise awards half of this amount. */
    uint8_t outcome_wagers[MUS_GAME_LANCE_COUNT];
    bool pairs_revealed;
    bool pairs[MUS_GAME_PLAYER_COUNT];
    bool game_revealed;
    bool game[MUS_GAME_PLAYER_COUNT];
} MusGameModel;

typedef struct {
    MusGameModel model;
    MusGameModel undo_history[MUS_GAME_UNDO_CAPACITY];
    size_t undo_count;
} MusGameState;

typedef struct {
    Player player;
    uint8_t hand[MUS_GAME_CLASS_COUNT];
    MusGamePhase phase;
    bool pairs_revealed;
    bool pairs[MUS_GAME_PLAYER_COUNT];
    bool game_revealed;
    bool game[MUS_GAME_PLAYER_COUNT];
    MusLanceOutcome outcomes[MUS_GAME_LANCE_COUNT];
    uint8_t outcome_wagers[MUS_GAME_LANCE_COUNT];
    MusBetStage bet_stage;
    Player bettor;
    uint8_t current_wager;
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

const char *mus_game_class_name(MusCardClass card_class);
const char *mus_game_phase_name(MusGamePhase phase);
const char *mus_game_outcome_name(MusLanceOutcome outcome);
const char *mus_single_bet_game_outcome_name(MusLanceOutcome outcome);
const char *mus_raises_to_eight_game_outcome_name(MusLanceOutcome outcome,
                                                  uint8_t wager);

#endif
