#ifndef MUS_SOLVER_MUS_SCORE_H
#define MUS_SOLVER_MUS_SCORE_H

#include <stdio.h>
#include "cfr/trainer.h"
#include "mus_solver/mus_game.h"

#define MUS_SCORE_SCHEMA_ID "mus.ordago-score/v4"

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
    /* Borrow an ordago trainer whose store remains unchanged during preload. */
    const Trainer *source;
    /* Synthetic regret and average-strategy mass per imported node. */
    double weight;
    /* Mix with a uniform policy so newly legal actions have positive mass. */
    double uniform_mix;
} MusScorePreloadOptions;

#define MUS_SCORE_PRELOAD_DEFAULT_WEIGHT 1.0
#define MUS_SCORE_PRELOAD_DEFAULT_UNIFORM_MIX 0.05

typedef struct {
    size_t source_nodes;
    size_t imported_nodes;
    size_t existing_nodes;
    size_t expanded_action_nodes;
    size_t skipped_history_nodes;
    size_t skipped_illegal_nodes;
} MusScorePreloadStats;

typedef struct {
    size_t iterations;
    size_t evaluation_samples;
    uint64_t seed;
    size_t reserve_information_sets;
    /* Optional ordago prior, applied separately to each missing score cell. */
    const MusScorePreloadOptions *preload;
    /* Optional output for the most recent cell, available in save_cell. */
    MusScorePreloadStats *preload_stats;
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
/* Checks the game adapter before a caller interprets an opaque state. */
bool mus_score_game_matches(const Game *game);
Status mus_score_game_state_init(MusGameState *state, const MusScoreGame *game);
Status mus_score_game_information_set_decode(
    InfoSetKey key, unsigned int target, MusOrdagoInformationSet *information);
/* Convert only mus, discard, and grande keys. Both earned arguments must be
 * zero. Later lances and responses outside the destination wager budget return
 * NOT_FOUND. Legacy later-lance keys mix collected and pending amounts and
 * contain nonpublic showdown-derived scores. An error preserves result. */
Status mus_score_information_set_from_ordago(
    const MusScoreGame *game, InfoSetKey source_key,
    unsigned int earned_a, unsigned int earned_b, InfoSetKey *result);
/* Import mus, discard, and grande average policies into missing nodes of an
 * initialized store. Skip later lances and unreachable response nodes.
 * Map probabilities by action identity and seed synthetic positive regrets.
 * Do not copy source regrets, iteration counters, RNG, or continuation values.
 * Existing nodes are preserved. Requires exclusive access to destination.
 * An error may leave completed imports; stats reports those imports. */
Status mus_score_preload_ordago(const MusScoreGame *game,
                               const MusScorePreloadOptions *options,
                               InfoStore *destination,
                               MusScorePreloadStats *stats);
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
