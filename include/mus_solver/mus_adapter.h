#ifndef MUS_SOLVER_MUS_ADAPTER_H
#define MUS_SOLVER_MUS_ADAPTER_H

#include "cfr/info_store.h"
#include "mus_solver/mus_game.h"

#define MUS_ADAPTER_MAX_RESULTS 32
#define MUS_ADAPTER_MAX_LOOKUPS 64
/* Bounds arithmetic, not the number or denominations of real wagers. */
#define MUS_ADAPTER_MAX_AMOUNT (INT64_MAX / 8)

typedef enum {
    MUS_BOT_PASS,
    MUS_BOT_RAISE,
    MUS_BOT_ACCEPT,
    MUS_BOT_REJECT,
    MUS_BOT_ORDAGO,
    MUS_BOT_MUS,
    MUS_BOT_NO_MUS,
    MUS_BOT_DISCARD
} MusBotActionKind;

typedef struct {
    MusBotActionKind kind;
    /* Increment for RAISE, canonical mask for DISCARD, zero otherwise. */
    int64_t amount;
} MusBotAction;

typedef struct {
    Player player; /* 0 is mano; 1 is postre. */
    MusBotAction action;
} MusBotEvent;

typedef struct {
    Player player;
    MusGamePhase phase;
    uint8_t hand[MUS_GAME_CLASS_COUNT];
    uint8_t mus_count;
    uint8_t discard_counts[MUS_GAME_MAX_MUSES][2];
    Player mus_stop_player; /* Used only when mus_count is zero. */
    MusLanceStrength previous_strengths[3];
    uint8_t previous_strength_count;
    int64_t prior_wager_total;
    int known_hand_scores[2]; /* Public points from this hand only, capped at 40. */
    bool hand_scores_complete;
    int match_scores[2]; /* Public current match scores, including awarded points. */
    /* Required with score tables or an ordago-public generic policy.
     * Fixed until the next hand. */
    bool has_hand_start_scores;
    int hand_start_scores[2];
    /* Accepted numeric wagers still pending from earlier lances. Excludes
     * dejes, intrinsic points, passed lances, and the current lance's offer.
     * Required for betting decisions in a trained score cell or with an
     * ordago-public generic policy. */
    bool has_pending_wager_total;
    int64_t pending_wager_total;
    const MusBotEvent *history; /* Current lance only. No hidden cards. */
    size_t history_count;
    const MusBotAction *legal_actions; /* Authoritative list from the real engine. */
    size_t legal_action_count;
} MusAdapterObservation;

typedef struct {
    bool score_adjustment;
    double score_offset;
    double score_factor_min;
    double score_factor_max;
} MusAdapterConfig;

typedef enum {
    MUS_ADAPTER_EXACT,
    MUS_ADAPTER_INTERPOLATED,
    MUS_ADAPTER_NEAREST,
    MUS_ADAPTER_FALLBACK,
    MUS_ADAPTER_MUS_LIMIT
} MusAdapterMethod;

typedef struct {
    InfoSetKey key;
    int64_t distance;
    double weight;
    uint8_t total, covered, prior_total, settled_score;
} MusAdapterSource;

typedef struct {
    MusBotAction action;
    double probability;
} MusBotChoice;

typedef struct {
    MusAdapterMethod method;
    const char *reason; /* Static string, or NULL. */
    bool score_proxy;
    bool score_strategy;
    bool public_strategy; /* Generic ordago-public, without a trained score cell. */
    int strategy_scores[2]; /* Initial cell scores; meaningful with score_strategy. */
    const char *strategy_reason; /* Static routing diagnostic. */
    double ordago_factor;
    int64_t real_total, real_covered;
    size_t lookup_count;
    MusAdapterSource sources[2];
    size_t source_count;
    /* One record per positive-mass abstract action, before final normalization. */
    struct {
        size_t source;
        Action abstract_action;
        MusBotAction real_action;
        double mass;
    } adjustments[MUS_ADAPTER_MAX_RESULTS];
    size_t adjustment_count;
    MusBotChoice choices[MUS_ADAPTER_MAX_RESULTS];
    size_t choice_count;
    MusBotAction selected;
    bool simplified_strategy;
} MusAdapterDecision;

typedef struct MusAdapter MusAdapter;

MusAdapterConfig mus_adapter_default_config(void);
/* Adapter creation and loading require MUS_GAME_MAX_MUSES == 1.
 * Other compiled limits return CFR_STATUS_INCOMPATIBLE_GAME without creating
 * an adapter; the output pointer is unchanged. The core solver can still be
 * built and used with two muses. */
/* Accept historical ordago, ordago-public and ordago-simplified schemas. On success, the adapter
 * owns the loaded checkpoint. No training occurs. */
Status mus_adapter_load(const char *path, bool compressed,
                        const MusAdapterConfig *config, uint64_t seed,
                        MusAdapter **adapter_out);
/* Opens a sorted text export. Reads rows on demand with a fixed 256-entry cache. */
Status mus_adapter_open_text(const char *path, const MusAdapterConfig *config,
                             uint64_t seed, MusAdapter **adapter_out);
/* Enable hybrid play using an immutable values.txt and score-AA-BB.txt exports.
 * Validate all published cells at setup. Keep at most one score file open in
 * addition to the generic backend. Queries select by hand_start_scores.
 * Unpublished cells keep the generic behavior. Trained cells never use the
 * generic score bias, even if their query requires interpolation or fallback.
 * Call once, before queries. An error preserves the adapter configuration. */
/* Enable hybrid play from compact per-cell policies (score-AA-BB.mscz), the
 * format produced by mus-score-compact. Same selection rules as the text
 * backend, with one cell open at a time. Enabling a table also turns the
 * generic score bias off for good: a trained cell already optimizes the match. */
Status mus_adapter_enable_score_compact(MusAdapter *adapter, const char *values_path,
                                        const char *directory);
Status mus_adapter_enable_score_text(MusAdapter *adapter, const char *values_path,
                                      const char *strategies_directory);
/* The caller must keep store alive and must not modify it during a query.
 * This entry point uses the historical ordago schema. A bare store does not
 * identify its format. Use create_public for ordago-public strategies and
 * create_simplified for ordago-simplified strategies. */
Status mus_adapter_create(const InfoStore *store, const MusAdapterConfig *config,
                          uint64_t seed, MusAdapter **adapter_out);
Status mus_adapter_create_public(const InfoStore *store, const MusAdapterConfig *config,
                                 uint64_t seed, MusAdapter **adapter_out);
Status mus_adapter_create_simplified(const InfoStore *store, const MusAdapterConfig *config,
                                     uint64_t seed, MusAdapter **adapter_out);
void mus_adapter_destroy(MusAdapter *adapter);
/* Queries are sequential. Errors preserve decision and the random stream. */
Status mus_adapter_decide(MusAdapter *adapter, const MusAdapterObservation *observation,
                          MusAdapterDecision *decision);
const char *mus_bot_action_name(MusBotActionKind kind);
const char *mus_adapter_method_name(MusAdapterMethod method);

#endif
