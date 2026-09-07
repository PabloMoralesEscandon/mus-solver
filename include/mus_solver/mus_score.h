#ifndef MUS_SOLVER_MUS_SCORE_H
#define MUS_SOLVER_MUS_SCORE_H

#include <stdio.h>
#include "cfr/trainer.h"
#include "mus_solver/mus_game.h"

#define MUS_SCORE_SCHEMA_ID "mus.ordago-score/v1"

typedef struct {
    bool ready;
    double value;
    size_t iterations;
    size_t evaluation_samples;
    uint64_t training_seed;
} MusScoreCell;

/* Index 0 is always mano. Publish complete diagonals after evaluation.
 * Use one InfoStore per score cell. The initial score is constant in that store.
 * The table must remain unchanged while its game trains or evaluates. */
struct MusScoreTable {
    uint8_t target;
    MusScoreCell cells[MUS_ORDAGO_WAGER][MUS_ORDAGO_WAGER];
};

typedef struct {
    Game game;
    const MusScoreTable *continuation;
    uint8_t initial_scores[MUS_GAME_PLAYER_COUNT];
    char schema[256];
} MusScoreGame;

typedef struct {
    size_t iterations;
    size_t evaluation_samples;
    uint64_t seed;
    size_t reserve_information_sets;
} MusScoreTrainingOptions;

/* Save the strategy before the coordinator publishes its value. */
typedef Status (*MusScoreSaveCell)(const MusScoreGame *game,
                                  const Trainer *trainer,
                                  const MusScoreCell *cell, void *context);
typedef Status (*MusScoreSaveDiagonal)(const MusScoreTable *table, void *context);

Status mus_score_table_init(MusScoreTable *table, unsigned int target);
Status mus_score_table_read(FILE *stream, MusScoreTable *table);
Status mus_score_table_write(FILE *stream, const MusScoreTable *table);
Status mus_score_game_init(MusScoreGame *game, const MusScoreTable *table,
                           unsigned int a, unsigned int b);
Status mus_score_game_state_init(MusGameState *state, const MusScoreGame *game);
Status mus_score_game_information_set_decode(
    InfoSetKey key, unsigned int target, MusOrdagoInformationSet *information);
Status mus_score_terminal_utility(const MusGameState *state, Player player,
                                  Utility *utility);
/* Generates a schema with the cell identity and all higher continuations. */
Status mus_score_game_set_schema(MusScoreGame *game);
/* Samples complete hands under the average strategy. Does not update CFR. */
Status mus_score_evaluate(const MusScoreGame *game, const InfoStore *store,
                          size_t samples, uint64_t seed, double *value);
/* Resolves all missing diagonals down to minimum_sum, inclusive. */
Status mus_score_train(MusScoreTable *table, unsigned int minimum_sum,
                       const MusScoreTrainingOptions *options,
                       MusScoreSaveCell save_cell,
                       MusScoreSaveDiagonal save_diagonal, void *context);
Status mus_score_strategy_write_text(FILE *stream, const Trainer *trainer);

#endif
