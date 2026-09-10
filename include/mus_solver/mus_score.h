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
    /* Borrow an ordago trainer whose store remains unchanged during use. */
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
    size_t missing_source_nodes;
    size_t imported_later_nodes;
} MusScorePreloadStats;

/* Keep this binding alive while its destination store can create nodes.
 * Read stats only when destination workers have stopped. */
typedef struct {
    const MusScoreGame *game;
    MusScorePreloadOptions options;
    MusScorePreloadStats *stats;
} MusScorePrior;

/* Called with this cell's workers paused. Different cells can call concurrently.
 * Borrowed objects must not be modified or retained by the callback. */
typedef Status (*MusScoreProgressCallback)(const MusScoreGame *game,
    const Trainer *trainer, size_t completed, const MusScorePreloadStats *stats,
    void *context);

/* Load a checkpoint against the source game's exact schema. The store starts
 * zeroed. Initialize the trainer with the supplied game, state, and store.
 * Different cells can load concurrently. An error can leave an owned store;
 * the coordinator destroys that store. Retain game and state only in trainer. */
typedef Status (*MusScoreLoadCell)(const MusScoreGame *game, MusGameState *state,
    InfoStore *store, Trainer *trainer, void *context);

typedef struct {
    /* Immutable source table, with every requested cell ready. */
    const MusScoreTable *table;
    MusScoreLoadCell load_cell;
    void *context;
} MusScoreExtension;

typedef struct {
    /* New iterations per cell. With extension, add to the loaded counter. */
    size_t iterations;
    size_t evaluation_samples;
    uint64_t seed;
    size_t reserve_information_sets;
    /* Optional ordago prior, applied separately to each missing score cell. */
    const MusScorePreloadOptions *preload;
    /* Optional output for the most recent cell, available in save_cell. */
    MusScorePreloadStats *preload_stats;
    /* Maximum concurrent cells in one diagonal. Zero and one run serially.
     * Each worker owns one CFR store. Save callbacks run on the caller in
     * score order, before the complete diagonal is published. */
    size_t threads;
    /* Threads that share each cell's strategy store. Zero and one use the
     * sequential traversal. Iterations are divided among these threads.
     * Multiple threads can produce different policies across runs because
     * the order of concurrent updates is not deterministic. */
    size_t threads_per_cell;
    /* Zero reports only at completion. Counts total iterations per cell. */
    size_t report_every;
    MusScoreProgressCallback progress;
    void *progress_context;
    /* Load prior score checkpoints before training each missing destination
     * cell. Preserve learning arrays, counters, and RNG. Rebind terminal
     * continuation values to the destination's completed higher diagonals.
     * The cell's rules and information keys stay unchanged. This carries old
     * regrets into a changed continuation model, rather than replaying all
     * iterations under the final fixed payoffs. */
    const MusScoreExtension *extension;
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
/* Encode public score information without changing its scores or pending wagers.
 * The owning cell's initial score is carried by its strategy file, not the key. */
Status mus_score_game_information_set_encode(
    const MusOrdagoInformationSet *information, unsigned int target, InfoSetKey *result);
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
/* Produce legacy candidate keys for any score decision. Collected points are
 * current scores minus the cell's initial scores. Restore the legacy prior
 * wager total by adding these payments to pending accepted wagers. Enumerate
 * the possible maximum settled scores without using an opponent's cards.
 * Other observed features remain unchanged. At most 41 keys are returned.
 * Success with count zero means the legacy budget excludes this response.
 * An error preserves keys and count. */
Status mus_score_information_set_ordago_candidates(
    const MusScoreGame *game, InfoSetKey key, InfoSetKey *keys,
    size_t capacity, size_t *count);
/* Seed every new node, in all lances, from compatible legacy average policies.
 * Combine their strategy sums, restrict by action identity, and apply the
 * configured uniform mixture. A missing source policy leaves a uniform node.
 * Existing nodes are preserved. Requires exclusive destination access during
 * attachment. The source and binding must outlive subsequent insertions.
 * Checkpoints retain seeded arrays but not this binding. Attach it again after
 * loading to initialize future nodes from the source. Evaluation and text
 * export do not create nodes or use this hook for missing decisions. */
Status mus_score_prior_attach(MusScorePrior *prior, const MusScoreGame *game,
                              const MusScorePreloadOptions *options,
                              InfoStore *destination, MusScorePreloadStats *stats);
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
