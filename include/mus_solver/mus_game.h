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
#define MUS_GAME_UNDO_CAPACITY 32
#define MUS_GAME_MAX_LEGAL_ACTIONS 5

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
    MUS_ACTION_ACCEPT
} MusGameAction;

typedef struct {
    MusGamePhase phase;
    uint8_t hands[MUS_GAME_PLAYER_COUNT][MUS_GAME_CLASS_COUNT];
    uint8_t deal_class;
    uint8_t deal_remaining;
    Player current_player;
    MusBetStage bet_stage;
    Player bettor;
    MusLanceOutcome outcomes[MUS_GAME_LANCE_COUNT];
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
    MusBetStage bet_stage;
    Player bettor;
} MusInformationSet;

Status mus_game_state_init(MusGameState *state);
const Game *mus_game_descriptor(void);
GameState *mus_game_state_as_game_state(MusGameState *state);
const GameState *mus_game_state_as_game_state_const(const MusGameState *state);

Status mus_game_information_set_decode(InfoSetKey key,
                                       MusInformationSet *information_set);
const char *mus_game_class_name(MusCardClass card_class);
const char *mus_game_phase_name(MusGamePhase phase);
const char *mus_game_outcome_name(MusLanceOutcome outcome);

#endif
