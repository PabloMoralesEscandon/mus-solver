#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cfr/checkpoint.h"
#include "mus_solver/mus_adapter.h"
#include "mus_solver/mus_score.h"
#include "mus_solver/mus_score_compact.h"
#include "mus_text_strategy.h"

/* At most two owners of each numeric increment and five numeric wagers. */
#define TEMPLATE_CAPACITY 512

typedef struct {
    uint8_t total, covered, masks[2], count;
    Player player, bettor;
    bool ordago;
} BetTemplate;

typedef struct {
    int64_t total, covered, increment;
    size_t count;
    uint8_t masks[2];
    Player actor, bettor;
    MusBetStage stage;
    bool ordago, representable;
} RealBet;

typedef struct {
    MusOrdagoInformationSet information;
    InfoSetKey key;
    int64_t distance;
    size_t mismatch;
} Candidate;

struct MusAdapter {
    const InfoStore *store;
    InfoStore owned_store;
    MusTextStrategy *text_strategy;
    bool public_generic;
    MusScoreTable *score_table;
    char *score_directory;
    MusTextStrategy *score_text;
    /* Compact policies replace the text export when enabled; one cell open. */
    bool score_compact_files;
    MusScoreCompact *score_compact;
    int open_scores[2];
    MusAdapterConfig config;
    uint64_t rng;
    BetTemplate templates[TEMPLATE_CAPACITY];
    size_t template_count;
};

static const int increments[] = {2, 4, 7, 10};
static int64_t difference(int64_t a, int64_t b) { return a > b ? a - b : b - a; }
static bool valid_player(Player p) { return p == CFR_PLAYER_0 || p == CFR_PLAYER_1; }
static bool mus_phase(MusGamePhase p) {
    return p == MUS_PHASE_MUS_PLAYER_0 || p == MUS_PHASE_MUS_PLAYER_1;
}
static bool discard_phase(MusGamePhase p) {
    return p == MUS_PHASE_DISCARD_PLAYER_0 || p == MUS_PHASE_DISCARD_PLAYER_1;
}
static int phase_index(MusGamePhase p) {
    switch (p) {
    case MUS_PHASE_GRANDE:
        return 0;
    case MUS_PHASE_CHICA:
        return 1;
    case MUS_PHASE_PARES:
        return 2;
    case MUS_PHASE_JUEGO:
    case MUS_PHASE_PUNTO:
        return 3;
    default:
        return -1;
    }
}
static bool same_action(MusBotAction a, MusBotAction b) {
    return a.kind == b.kind && a.amount == b.amount;
}
static bool action_valid(MusBotAction a) {
    if (a.kind < MUS_BOT_PASS || a.kind > MUS_BOT_DISCARD)
        return false;
    if (a.kind == MUS_BOT_RAISE)
        return a.amount > 0 && a.amount <= MUS_ADAPTER_MAX_AMOUNT;
    if (a.kind == MUS_BOT_DISCARD)
        return a.amount > 0 && a.amount < 16;
    return a.amount == 0;
}
static bool is_legal(const MusAdapterObservation *o, MusBotAction a) {
    for (size_t i = 0; i < o->legal_action_count; ++i)
        if (same_action(o->legal_actions[i], a))
            return true;
    return false;
}

MusAdapterConfig mus_adapter_default_config(void) {
    return (MusAdapterConfig){true, 8.0, 0.5, 2.0};
}

static Status add_template(MusAdapter *a, BetTemplate t) {
    if (a->template_count >= TEMPLATE_CAPACITY)
        return CFR_STATUS_BUFFER_TOO_SMALL;
    a->templates[a->template_count++] = t;
    return CFR_STATUS_SUCCESS;
}

static Status build_templates(MusAdapter *a, BetTemplate t, int minimum) {
    Status status = add_template(a, t);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    BetTemplate ordago = t;
    ordago.total = 40;
    ordago.covered = t.total;
    ordago.ordago = true;
    ordago.bettor = t.player;
    ordago.player = t.bettor;
    status = add_template(a, ordago);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (t.count == MUS_ORDAGO_MAX_NUMERIC_WAGERS)
        return CFR_STATUS_SUCCESS;
    for (int i = 0; i < 4; ++i) {
        int increment = increments[i];
        if (increment == 7 || increment < minimum || (t.masks[t.player] & (1U << i)) ||
            t.total + increment >= 40)
            continue;
        BetTemplate next = t;
        next.covered = t.total;
        next.total = (uint8_t)(t.total + increment);
        next.masks[t.player] |= (uint8_t)(1U << i);
        next.count++;
        next.bettor = t.player;
        next.player = t.bettor;
        status = build_templates(a, next, increment);
        if (status != CFR_STATUS_SUCCESS)
            return status;
    }
    return CFR_STATUS_SUCCESS;
}

Status mus_adapter_create(const InfoStore *store, const MusAdapterConfig *config,
                          uint64_t seed, MusAdapter **out) {
    MusAdapterConfig c = config ? *config : mus_adapter_default_config();
    if (store == NULL || out == NULL || !isfinite(c.score_offset) ||
        c.score_offset <= 0 || !isfinite(c.score_factor_min) ||
        !isfinite(c.score_factor_max) || c.score_factor_min <= 0 ||
        c.score_factor_min > 1 || c.score_factor_max < 1)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (MUS_GAME_MAX_MUSES != 1)
        return CFR_STATUS_INCOMPATIBLE_GAME;
    MusAdapter *a = calloc(1, sizeof(*a));
    if (a == NULL)
        return CFR_STATUS_OUT_OF_MEMORY;
    a->store = store;
    a->config = c;
    a->rng = seed;
    Status status = CFR_STATUS_SUCCESS;
    for (int player = 0; player < 2 && status == CFR_STATUS_SUCCESS; ++player) {
        BetTemplate t = {.player = (Player)(1 - player),
                         .bettor = (Player)player,
                         .total = 40,
                         .covered = 1,
                         .ordago = true};
        status = add_template(a, t);
        for (int i = 0; i < 4 && status == CFR_STATUS_SUCCESS; ++i) {
            t = (BetTemplate){.player = (Player)(1 - player),
                              .bettor = (Player)player,
                              .total = (uint8_t)increments[i],
                              .covered = 1,
                              .count = 1};
            t.masks[player] = (uint8_t)(1U << i);
            status = build_templates(a, t, increments[i]);
        }
    }
    if (status != CFR_STATUS_SUCCESS) {
        free(a);
        return status;
    }
    *out = a;
    return CFR_STATUS_SUCCESS;
}

Status mus_adapter_create_public(const InfoStore *store, const MusAdapterConfig *config,
                                 uint64_t seed, MusAdapter **out) {
    const Status status = mus_adapter_create(store, config, seed, out);
    if (status == CFR_STATUS_SUCCESS)
        (*out)->public_generic = true;
    return status;
}

Status mus_adapter_load(const char *path, bool compressed,
                        const MusAdapterConfig *config, uint64_t seed,
                        MusAdapter **out) {
    if (path == NULL || out == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (MUS_GAME_MAX_MUSES != 1)
        return CFR_STATUS_INCOMPATIBLE_GAME;
    FILE *stream = fopen(path, "rb");
    if (stream == NULL)
        return CFR_STATUS_IO_ERROR;
    MusAdapter *a = NULL;
    InfoStore empty = {0};
    Status status = mus_adapter_create(&empty, config, seed, &a);
    if (status == CFR_STATUS_SUCCESS) {
        MusOrdagoGameState state;
        Trainer trainer;
        status = mus_ordago_game_state_init(&state);
        if (status == CFR_STATUS_SUCCESS) {
            status =
                compressed
                    ? cfr_checkpoint_read_zstd(
                          stream, mus_ordago_game_descriptor(),
                          mus_ordago_game_state_as_game_state(&state), &a->owned_store,
                          &trainer)
                    : cfr_checkpoint_read(stream, mus_ordago_game_descriptor(),
                                          mus_ordago_game_state_as_game_state(&state),
                                          &a->owned_store, &trainer);
        }
        if (status == CFR_STATUS_INCOMPATIBLE_GAME) {
            status = fseek(stream, 0, SEEK_SET) == 0 ? CFR_STATUS_SUCCESS : CFR_STATUS_IO_ERROR;
            if (status == CFR_STATUS_SUCCESS)
                status = mus_ordago_public_game_state_init(&state);
            if (status == CFR_STATUS_SUCCESS) {
                const Game *game = mus_ordago_public_game_descriptor();
                status = compressed
                    ? cfr_checkpoint_read_zstd(stream, game, mus_game_state_as_game_state(&state),
                                               &a->owned_store, &trainer)
                    : cfr_checkpoint_read(stream, game, mus_game_state_as_game_state(&state),
                                          &a->owned_store, &trainer);
            }
            a->public_generic = status == CFR_STATUS_SUCCESS;
        }
        a->store = &a->owned_store;
    }
    if (fclose(stream) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    if (status != CFR_STATUS_SUCCESS) {
        mus_adapter_destroy(a);
        return status;
    }
    *out = a;
    return CFR_STATUS_SUCCESS;
}

Status mus_adapter_open_text(const char *path, const MusAdapterConfig *config,
                             uint64_t seed, MusAdapter **out) {
    if (!path || !out)
        return CFR_STATUS_INVALID_ARGUMENT;
    MusAdapter *adapter = NULL;
    InfoStore unused = {0};
    Status status = mus_adapter_create(&unused, config, seed, &adapter);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    adapter->store = NULL;
    status = mus_text_strategy_open(path, &adapter->text_strategy);
    if (status == CFR_STATUS_INCOMPATIBLE_GAME) {
        status = mus_text_strategy_open_schema(path, MUS_ORDAGO_PUBLIC_STRATEGY_SCHEMA_ID,
                                               0, &adapter->text_strategy);
        adapter->public_generic = status == CFR_STATUS_SUCCESS;
    }
    if (status != CFR_STATUS_SUCCESS) {
        mus_adapter_destroy(adapter);
        return status;
    }
    *out = adapter;
    return CFR_STATUS_SUCCESS;
}

static char *score_text_path(const char *directory, int a, int b) {
    size_t length = strlen(directory);
    if (length > SIZE_MAX - 32)
        return NULL;
    char *path = malloc(length + 32);
    if (path != NULL)
        snprintf(path, length + 32, "%s/score-%02d-%02d.txt", directory, a, b);
    return path;
}

static Status open_score_text(const MusScoreTable *table, const char *directory,
                              int a, int b, MusTextStrategy **out) {
    MusScoreGame game;
    Status status = mus_score_game_init(&game, table, (unsigned int)a, (unsigned int)b);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    char *path = score_text_path(directory, a, b);
    if (path == NULL)
        return CFR_STATUS_OUT_OF_MEMORY;
    status = mus_text_strategy_open_schema(path, game.schema, table->cells[a][b].iterations, out);
    free(path);
    return status;
}

static char *score_compact_path(const char *directory, int a, int b) {
    size_t length = strlen(directory);
    if (length > SIZE_MAX - 32)
        return NULL;
    char *path = malloc(length + 32);
    if (path != NULL)
        snprintf(path, length + 32, "%s/score-%02d-%02d.mscz", directory, a, b);
    return path;
}

static Status open_score_compact(const MusScoreTable *table, const char *directory,
                                 int a, int b, MusScoreCompact **out) {
    MusScoreGame game;
    Status status = mus_score_game_init(&game, table, (unsigned int)a, (unsigned int)b);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    char *path = score_compact_path(directory, a, b);
    if (path == NULL)
        return CFR_STATUS_OUT_OF_MEMORY;
    status = mus_score_compact_open(path, game.schema, table->cells[a][b].iterations, out);
    free(path);
    return status;
}

static Status enable_score_table(MusAdapter *a, const char *values_path,
                                 const char *directory, bool compact) {
    if (a == NULL || a->score_table != NULL || values_path == NULL || directory == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    FILE *stream = fopen(values_path, "r");
    if (stream == NULL)
        return CFR_STATUS_IO_ERROR;
    MusScoreTable *table = malloc(sizeof(*table));
    Status status = table == NULL ? CFR_STATUS_OUT_OF_MEMORY
                                  : mus_score_table_read(stream, table);
    if (fclose(stream) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    if (status == CFR_STATUS_SUCCESS && (table->target != 40 || table->four_player))
        status = CFR_STATUS_INCOMPATIBLE_GAME;
    for (int x = 0; x < 40 && status == CFR_STATUS_SUCCESS; ++x) {
        for (int y = 0; y < 40 && status == CFR_STATUS_SUCCESS; ++y) {
            if (!table->cells[x][y].ready)
                continue;
            if (compact) {
                MusScoreCompact *policies = NULL;
                status = open_score_compact(table, directory, x, y, &policies);
                mus_score_compact_close(policies);
            } else {
                MusTextStrategy *text = NULL;
                status = open_score_text(table, directory, x, y, &text);
                mus_text_strategy_close(text);
            }
        }
    }
    char *copy = NULL;
    if (status == CFR_STATUS_SUCCESS) {
        size_t length = strlen(directory) + 1;
        copy = malloc(length);
        if (copy == NULL)
            status = CFR_STATUS_OUT_OF_MEMORY;
        else
            memcpy(copy, directory, length);
    }
    if (status != CFR_STATUS_SUCCESS) {
        free(table);
        free(copy);
        return status;
    }
    a->score_table = table;
    a->score_directory = copy;
    a->score_compact_files = compact;
    /* Trained cells already optimize the match, so the generic score bias never
     * applies with a table: only unknown bet amounts are approximated. */
    a->config.score_adjustment = false;
    return CFR_STATUS_SUCCESS;
}

Status mus_adapter_enable_score_text(MusAdapter *a, const char *values_path,
                                      const char *directory) {
    return enable_score_table(a, values_path, directory, false);
}

Status mus_adapter_enable_score_compact(MusAdapter *a, const char *values_path,
                                        const char *directory) {
    return enable_score_table(a, values_path, directory, true);
}

void mus_adapter_destroy(MusAdapter *a) {
    if (a != NULL) {
        mus_text_strategy_close(a->text_strategy);
        mus_text_strategy_close(a->score_text);
        mus_score_compact_close(a->score_compact);
        free(a->score_table);
        free(a->score_directory);
        (void)cfr_info_store_destroy(&a->owned_store);
        free(a);
    }
}

static Status encode_information(const MusAdapter *adapter,
                                  const MusOrdagoInformationSet *info, InfoSetKey *key) {
    return info->score_enabled ? mus_score_game_information_set_encode(info, 40, key)
        : adapter->public_generic ? mus_ordago_public_game_information_set_encode(info, key)
                                  : mus_ordago_game_information_set_encode(info, key);
}

static Status select_strategy(MusAdapter *a, const MusAdapterObservation *o,
                               MusOrdagoInformationSet *info, MusAdapterDecision *d) {
    d->strategy_reason = "score_table_disabled";
    if (a->score_table == NULL && !a->public_generic)
        return CFR_STATUS_SUCCESS;
    if (!o->has_hand_start_scores)
        return CFR_STATUS_INVALID_ARGUMENT;
    for (int p = 0; p < 2; ++p) {
        if (o->hand_start_scores[p] < 0 || o->hand_start_scores[p] > o->match_scores[p] ||
            ((mus_phase(o->phase) || discard_phase(o->phase) || o->phase == MUS_PHASE_GRANDE) &&
             o->hand_start_scores[p] != o->match_scores[p]))
            return CFR_STATUS_INVALID_ARGUMENT;
    }
    int x = o->hand_start_scores[0], y = o->hand_start_scores[1];
    const bool trained = a->score_table != NULL && a->score_table->cells[x][y].ready;
    if (a->score_table != NULL)
        d->strategy_reason = trained ? "trained_start_score" : "untrained_start_score";
    if (!trained && !a->public_generic)
        return CFR_STATUS_SUCCESS;
    if (phase_index(o->phase) >= 0 && !o->has_pending_wager_total)
        return CFR_STATUS_INVALID_ARGUMENT;
    if ((mus_phase(o->phase) || discard_phase(o->phase) || o->phase == MUS_PHASE_GRANDE) &&
        o->pending_wager_total != 0)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (trained && a->score_compact_files &&
        (a->score_compact == NULL || a->open_scores[0] != x || a->open_scores[1] != y)) {
        MusScoreCompact *compact = NULL;
        Status status = open_score_compact(a->score_table, a->score_directory, x, y, &compact);
        if (status != CFR_STATUS_SUCCESS)
            return status;
        mus_score_compact_close(a->score_compact);
        a->score_compact = compact;
        a->open_scores[0] = x;
        a->open_scores[1] = y;
    } else if (trained && !a->score_compact_files &&
               (a->score_text == NULL || a->open_scores[0] != x || a->open_scores[1] != y)) {
        MusTextStrategy *text = NULL;
        Status status = open_score_text(a->score_table, a->score_directory, x, y, &text);
        if (status != CFR_STATUS_SUCCESS)
            return status;
        mus_text_strategy_close(a->score_text);
        a->score_text = text;
        a->open_scores[0] = x;
        a->open_scores[1] = y;
    }
    d->score_strategy = trained;
    d->public_strategy = !trained;
    if (trained) {
        d->strategy_scores[0] = x;
        d->strategy_scores[1] = y;
    }
    info->score_enabled = trained;
    info->scores[0] = (uint8_t)(o->match_scores[0] - (trained ? 0 : x));
    info->scores[1] = (uint8_t)(o->match_scores[1] - (trained ? 0 : y));
    info->max_settled_score = info->scores[0] > info->scores[1]
        ? info->scores[0] : info->scores[1];
    info->prior_wager_total = (uint8_t)(o->pending_wager_total > 40 ? 40 : o->pending_wager_total);
    return CFR_STATUS_SUCCESS;
}

static Status replay(const MusAdapterObservation *o, size_t count, RealBet *r) {
    *r = (RealBet){
        .actor = CFR_PLAYER_0, .stage = MUS_BET_FIRST_TO_ACT, .representable = true};
    for (size_t i = 0; i < count; ++i) {
        MusBotEvent e = o->history[i];
        if (!valid_player(e.player) || e.player != r->actor ||
            !action_valid(e.action) || r->ordago)
            return CFR_STATUS_INVALID_ARGUMENT;
        if (e.action.kind == MUS_BOT_PASS) {
            if (r->stage != MUS_BET_FIRST_TO_ACT)
                return CFR_STATUS_INVALID_ARGUMENT;
            r->stage = MUS_BET_SECOND_AFTER_PASS;
            r->actor = CFR_PLAYER_1;
            continue;
        }
        if (e.action.kind != MUS_BOT_RAISE && e.action.kind != MUS_BOT_ORDAGO)
            return CFR_STATUS_INVALID_ARGUMENT; /* A completed lance has no next
                                                   decision. */
        r->covered = r->count ? r->total : 1;
        if (e.action.kind == MUS_BOT_ORDAGO) {
            r->ordago = true;
        } else {
            int index = -1;
            for (int j = 0; j < 4; ++j)
                if (increments[j] == e.action.amount)
                    index = j;
            if (r->total > MUS_ADAPTER_MAX_AMOUNT - e.action.amount)
                return CFR_STATUS_INVALID_ARGUMENT;
            if (index < 0 || r->count >= 5 ||
                (r->count &&
                 (e.action.amount == 7 || e.action.amount < r->increment)) ||
                (index >= 0 && (r->masks[e.player] & (1U << index))))
                r->representable = false;
            if (index >= 0)
                r->masks[e.player] |= (uint8_t)(1U << index);
            r->count++;
            r->total += e.action.amount;
            r->increment = e.action.amount;
        }
        r->stage = MUS_BET_RESPONSE;
        r->bettor = e.player;
        r->actor = (Player)(1 - e.player);
    }
    return CFR_STATUS_SUCCESS;
}

static void use_bet(MusOrdagoInformationSet *info, const RealBet *r) {
    info->bet_stage = r->stage;
    info->bettor = r->bettor;
    info->current_wager = (uint8_t)(r->ordago ? 40 : r->total);
    info->previous_wager = (uint8_t)r->covered;
    info->current_wager_is_ordago = r->ordago;
    info->numeric_wager_count = (uint8_t)r->count;
    memcpy(info->used_wager_action_masks, r->masks, sizeof(r->masks));
}

static Status observation_info(const MusAdapterObservation *o,
                               MusOrdagoInformationSet *info, RealBet *real) {
    if (o == NULL || !valid_player(o->player) || !valid_player(o->mus_stop_player) ||
        o->mus_count > 1 || o->previous_strength_count > 3 ||
        o->prior_wager_total < 0 || o->prior_wager_total > MUS_ADAPTER_MAX_AMOUNT ||
        o->pending_wager_total < 0 || o->pending_wager_total > MUS_ADAPTER_MAX_AMOUNT ||
        (o->history_count && o->history == NULL) || !o->legal_actions ||
        !o->legal_action_count)
        return CFR_STATUS_INVALID_ARGUMENT;
    for (int p = 0; p < 2; ++p)
        if (o->match_scores[p] < 0 || o->match_scores[p] >= 40 ||
            o->known_hand_scores[p] < 0 || o->known_hand_scores[p] > 40)
            return CFR_STATUS_INVALID_ARGUMENT;
    bool mus = mus_phase(o->phase), discard = discard_phase(o->phase);
    int previous = phase_index(o->phase);
    if (!mus && !discard && (previous < 0 || o->previous_strength_count != previous))
        return CFR_STATUS_INVALID_ARGUMENT;
    if ((mus || discard) &&
        (o->history_count || o->previous_strength_count || o->prior_wager_total ||
         o->known_hand_scores[0] || o->known_hand_scores[1] ||
         o->player != ((o->phase == MUS_PHASE_MUS_PLAYER_0 ||
                        o->phase == MUS_PHASE_DISCARD_PLAYER_0)
                           ? CFR_PLAYER_0
                           : CFR_PLAYER_1) ||
         (discard && o->mus_count)))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (previous == 0 &&
        (o->prior_wager_total || o->known_hand_scores[0] || o->known_hand_scores[1]))
        return CFR_STATUS_INVALID_ARGUMENT;
    *info = (MusOrdagoInformationSet){
        .player = o->player,
        .phase = o->phase,
        .mus_count = o->mus_count,
        .mus_stopped = o->mus_count == 0,
        .mus_stop_player = o->mus_stop_player,
        .previous_strength_count = o->previous_strength_count,
        .prior_wager_total =
            (uint8_t)(o->prior_wager_total > 40 ? 40 : o->prior_wager_total),
        .max_settled_score = (uint8_t)(o->known_hand_scores[0] > o->known_hand_scores[1]
                                           ? o->known_hand_scores[0]
                                           : o->known_hand_scores[1])};
    memcpy(info->discard_counts, o->discard_counts, sizeof(info->discard_counts));
    /* v9-muses-1 ends mus automatically after the first redraw. */
    for (int round = 0; round < o->mus_count; ++round)
        for (int p = 0; p < 2; ++p)
            if (o->discard_counts[round][p] < 1 || o->discard_counts[round][p] > 4)
                return CFR_STATUS_INVALID_ARGUMENT;
    for (int i = 0; i < o->previous_strength_count; ++i) {
        if (o->previous_strengths[i] < 0 ||
            o->previous_strengths[i] >= (i == 2 ? 7 : 4))
            return CFR_STATUS_INVALID_ARGUMENT;
        info->previous_strengths[i] = o->previous_strengths[i];
    }
    if (mus_ordago_game_information_set_set_hand(info, o->hand) != CFR_STATUS_SUCCESS)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (replay(o, o->history_count, real) != CFR_STATUS_SUCCESS ||
        (!mus && !discard && real->actor != o->player))
        return CFR_STATUS_INVALID_ARGUMENT;
    for (size_t i = 0; i < o->legal_action_count; ++i) {
        MusBotAction action = o->legal_actions[i];
        bool allowed;
        if (!action_valid(action))
            return CFR_STATUS_INVALID_ARGUMENT;
        if (mus)
            allowed = action.kind == MUS_BOT_MUS || action.kind == MUS_BOT_NO_MUS;
        else if (discard)
            allowed = action.kind == MUS_BOT_DISCARD &&
                      mus_ordago_game_discard_mask_is_canonical(
                          o->hand, (unsigned int)action.amount);
        else if (real->ordago)
            allowed = action.kind == MUS_BOT_ACCEPT || action.kind == MUS_BOT_REJECT;
        else if (real->stage == MUS_BET_RESPONSE)
            allowed = action.kind == MUS_BOT_ACCEPT || action.kind == MUS_BOT_REJECT ||
                      action.kind == MUS_BOT_RAISE || action.kind == MUS_BOT_ORDAGO;
        else
            allowed = action.kind == MUS_BOT_PASS || action.kind == MUS_BOT_RAISE ||
                      action.kind == MUS_BOT_ORDAGO;
        if (!allowed)
            return CFR_STATUS_INVALID_ARGUMENT;
    }
    return CFR_STATUS_SUCCESS;
}

static bool fallback_action(const MusAdapterObservation *o, const RealBet *r,
                            MusBotAction *out) {
    *out = (MusBotAction){.kind = mus_phase(o->phase)            ? MUS_BOT_NO_MUS
                                  : r->stage == MUS_BET_RESPONSE ? MUS_BOT_REJECT
                                                                 : MUS_BOT_PASS};
    if (discard_phase(o->phase)) {
        for (int mask = 1; mask < 16; ++mask) {
            *out = (MusBotAction){MUS_BOT_DISCARD, mask};
            if (is_legal(o, *out))
                return true;
        }
        return false;
    }
    return is_legal(o, *out);
}

static Status lookup(const MusAdapter *a, const Candidate *c, MusAdapterDecision *d,
                     Action *actions, Probability *probabilities, size_t *count) {
    if (d->lookup_count >= MUS_ADAPTER_MAX_LOOKUPS)
        return CFR_STATUS_NOT_FOUND;
    d->lookup_count++;
    Status status = mus_ordago_game_information_set_legal_actions(
        &c->information, actions, MUS_GAME_MAX_LEGAL_ACTIONS, count);
    if (status != CFR_STATUS_SUCCESS)
        return CFR_STATUS_FORMAT_ERROR;
    if (c->information.score_enabled && a->score_compact_files) {
        if (a->score_compact == NULL)
            return CFR_STATUS_NOT_FOUND;
        return mus_score_compact_find(a->score_compact, c->key, actions, *count,
                                      probabilities);
    }
    MusTextStrategy *text = c->information.score_enabled ? a->score_text : a->text_strategy;
    if (text)
        return mus_text_strategy_find(text, c->key, &c->information,
                                      actions, *count, probabilities);
    const InfoNode *node;
    status = cfr_info_store_find_const(a->store, c->key, &node);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (node->action_count != *count)
        return CFR_STATUS_FORMAT_ERROR;
    /* Zero average mass is absence of evidence, not a trained uniform policy. */
    double mass = 0;
    for (size_t i = 0; i < node->action_count; ++i) {
        if (!isfinite(node->strategy_sums[i]) || node->strategy_sums[i] < 0)
            return CFR_STATUS_NUMERIC_ERROR;
        mass += node->strategy_sums[i];
    }
    if (!isfinite(mass))
        return CFR_STATUS_NUMERIC_ERROR;
    if (mass == 0)
        return CFR_STATUS_NOT_FOUND;
    return cfr_info_node_average_strategy(node, probabilities,
                                          MUS_GAME_MAX_LEGAL_ACTIONS);
}

static MusBotAction translate_action(Action action, const MusAdapterObservation *o,
                                     const RealBet *r, MusBotAction fallback,
                                     bool score_strategy) {
    MusBotAction target = {.kind = MUS_BOT_PASS};
    switch (action) {
    case MUS_ACTION_PASS:
        break;
    case MUS_ACTION_ACCEPT:
        target.kind = MUS_BOT_ACCEPT;
        break;
    case MUS_ACTION_REJECT:
        target.kind = MUS_BOT_REJECT;
        break;
    case MUS_ACTION_ORDAGO:
        target.kind = MUS_BOT_ORDAGO;
        break;
    case MUS_ACTION_MUS:
        target.kind = MUS_BOT_MUS;
        break;
    case MUS_ACTION_NO_MUS:
        target.kind = MUS_BOT_NO_MUS;
        break;
    case MUS_ACTION_BET_TWO:
        target = (MusBotAction){MUS_BOT_RAISE, 2};
        break;
    case MUS_ACTION_BET_FOUR:
        target = (MusBotAction){MUS_BOT_RAISE, 4};
        break;
    case MUS_ACTION_BET_SEVEN:
        target = (MusBotAction){MUS_BOT_RAISE, 7};
        break;
    case MUS_ACTION_BET_TEN:
        target = (MusBotAction){MUS_BOT_RAISE, 10};
        break;
    default:
        target = (MusBotAction){MUS_BOT_DISCARD, MUS_ACTION_DISCARD_MASK_VALUE(action)};
        break;
    }
    if (target.kind != MUS_BOT_RAISE)
        return is_legal(o, target) ? target : fallback;
    int distance0 = 40 - o->match_scores[0], distance1 = 40 - o->match_scores[1];
    int64_t cap = score_strategy ? MUS_ADAPTER_MAX_AMOUNT
        : (distance0 > distance1 ? distance0 : distance1) - r->total;
    MusBotAction best = {MUS_BOT_RAISE, 0};
    int64_t best_distance = INT64_MAX;
    for (size_t i = 0; i < o->legal_action_count; ++i) {
        MusBotAction candidate = o->legal_actions[i];
        if (candidate.kind != MUS_BOT_RAISE || candidate.amount > cap)
            continue;
        int64_t delta = difference(candidate.amount, target.amount);
        if (delta < best_distance ||
            (delta == best_distance && candidate.amount < best.amount)) {
            best = candidate;
            best_distance = delta;
        }
    }
    if (best.amount)
        return best;
    target = (MusBotAction){.kind = r->stage == MUS_BET_RESPONSE ? MUS_BOT_ACCEPT
                                                                 : MUS_BOT_PASS};
    return is_legal(o, target) ? target : fallback;
}

static void add_policy(MusAdapterDecision *d, const Candidate *c, double weight,
                       const Action *actions, const Probability *p, size_t count,
                       const MusAdapterObservation *o, const RealBet *real,
                       MusBotAction fallback) {
    size_t source = d->source_count++;
    d->sources[source] = (MusAdapterSource){c->key,
                                            c->distance,
                                            weight,
                                            c->information.current_wager,
                                            c->information.previous_wager,
                                            c->information.prior_wager_total,
                                            c->information.max_settled_score};
    for (size_t i = 0; i < count; ++i) {
        if (p[i] == 0 || weight == 0)
            continue;
        MusBotAction action = translate_action(actions[i], o, real, fallback,
                                               d->score_strategy);
        size_t j = 0;
        while (j < d->choice_count && !same_action(d->choices[j].action, action))
            ++j;
        if (j == d->choice_count) {
            d->choices[j].action = action;
            d->choice_count++;
        }
        d->choices[j].probability += weight * p[i];
        size_t adjustment = d->adjustment_count++;
        d->adjustments[adjustment].source = source;
        d->adjustments[adjustment].abstract_action = actions[i];
        d->adjustments[adjustment].real_action = action;
        d->adjustments[adjustment].mass = weight * p[i];
    }
}

static int compare_candidates(const Candidate *a, const Candidate *b) {
    if (a->distance != b->distance)
        return a->distance < b->distance ? -1 : 1;
    if (a->mismatch != b->mismatch)
        return a->mismatch < b->mismatch ? -1 : 1;
    if (a->information.numeric_wager_count != b->information.numeric_wager_count)
        return a->information.numeric_wager_count < b->information.numeric_wager_count
                   ? -1
                   : 1;
    return a->key < b->key ? -1 : a->key != b->key;
}

static void insert_candidate(Candidate best[MUS_ADAPTER_MAX_LOOKUPS], size_t *count,
                             Candidate candidate) {
    size_t index = *count;
    if (index == MUS_ADAPTER_MAX_LOOKUPS) {
        if (compare_candidates(&candidate, &best[index - 1]) >= 0)
            return;
        --index;
    } else
        ++*count;
    while (index && compare_candidates(&candidate, &best[index - 1]) < 0) {
        best[index] = best[index - 1];
        --index;
    }
    best[index] = candidate;
}

static size_t mismatch(const RealBet *real, const BetTemplate *t) {
    size_t result =
        real->count > t->count ? real->count - t->count : t->count - real->count;
    for (int p = 0; p < 2; ++p)
        for (int bit = 0; bit < 4; ++bit)
            if ((real->masks[p] ^ t->masks[p]) & (1U << bit))
                ++result;
    return result;
}

static void nearest_candidates(const MusAdapter *a, const MusAdapterObservation *o,
                               const MusOrdagoInformationSet *base, const RealBet *real,
                               Candidate best[MUS_ADAPTER_MAX_LOOKUPS], size_t *count) {
    *count = 0;
    if ((base->score_enabled || a->public_generic) && o->pending_wager_total > 40)
        return;
    bool response = real->stage == MUS_BET_RESPONSE;
    size_t templates = response ? a->template_count : 1;
    for (size_t t_index = 0; t_index < templates; ++t_index) {
        BetTemplate t = response ? a->templates[t_index] : (BetTemplate){0};
        if (response && (t.player != o->player || t.ordago != real->ordago ||
                         ((t.covered == 1) !=
                          (real->ordago ? real->count == 0 : real->count == 1))))
            continue;
        Candidate c = {.information = *base, .mismatch = mismatch(real, &t)};
        if (response) {
            c.information.bet_stage = MUS_BET_RESPONSE;
            c.information.bettor = t.bettor;
            c.information.current_wager = t.total;
            c.information.previous_wager = t.covered;
            c.information.current_wager_is_ordago = t.ordago;
            c.information.numeric_wager_count = t.count;
            memcpy(c.information.used_wager_action_masks, t.masks, sizeof(t.masks));
        } else
            c.information.bet_stage = real->stage;
        int active =
            response ? (t.ordago ? (t.covered == 1 ? 0 : t.covered) : t.total) : 0;
        if (base->score_enabled || a->public_generic) {
            /* Scores and pending contracts are public. Never move them to find
             * another node. Only approximate the current betting sequence. */
            c.distance = difference(real->ordago ? 40 : real->total, t.total) +
                         difference(real->covered, t.covered);
            if (encode_information(a, &c.information, &c.key) == CFR_STATUS_SUCCESS)
                insert_candidate(best, count, c);
            continue;
        }
        const int upper = o->previous_strength_count ? 40 : 0;
        const int center_prior = o->prior_wager_total > upper
            ? upper : (int)o->prior_wager_total;
        const int center_score = base->max_settled_score > upper
            ? upper : base->max_settled_score;
        const int64_t minimum_distance =
            difference(real->ordago ? 40 : real->total, t.total) +
            difference(real->covered, t.covered) +
            difference(o->prior_wager_total, center_prior) +
            difference(base->max_settled_score, center_score);
        /* Visit Manhattan rings around the projection onto the bounded grid.
         * All points on a ring have the same distance. Finish ties before
         * stopping, since mismatch, wager count and key also determine order. */
        for (int radius = 0; radius <= 2 * upper; ++radius) {
            c.distance = minimum_distance + radius;
            if (*count == MUS_ADAPTER_MAX_LOOKUPS &&
                c.distance > best[*count - 1].distance)
                break;
            const int first = center_prior > radius ? center_prior - radius : 0;
            const int last = center_prior + radius < upper
                ? center_prior + radius : upper;
            for (int prior = first; prior <= last; ++prior) {
                const int delta = radius - abs(prior - center_prior);
                const int scores[2] = {center_score - delta, center_score + delta};
                for (int side = 0; side < (delta == 0 ? 1 : 2); ++side) {
                    const int score = scores[side];
                    if (score < 0 || score > upper ||
                        (active && score + prior + active >= 40))
                        continue;
                    c.information.prior_wager_total = (uint8_t)prior;
                    c.information.max_settled_score = (uint8_t)score;
                    if (encode_information(a, &c.information, &c.key) ==
                        CFR_STATUS_SUCCESS)
                        insert_candidate(best, count, c);
                }
            }
        }
    }
}

/* SplitMix64: local sampling never mutates the checkpoint's training stream. */
static double random_probability(uint64_t *state) {
    uint64_t z = (*state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return (double)((z ^ (z >> 31)) >> 11) * 0x1.0p-53;
}

static Status decide_impl(MusAdapter *a, const MusAdapterObservation *o,
                          MusAdapterDecision *d) {
    MusOrdagoInformationSet base;
    RealBet real;
    Status status = observation_info(o, &base, &real);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    status = select_strategy(a, o, &base, d);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    int64_t prior_total = d->score_strategy || d->public_strategy
        ? o->pending_wager_total : o->prior_wager_total;
    MusBotAction fallback;
    if (!fallback_action(o, &real, &fallback))
        return CFR_STATUS_INVALID_ARGUMENT;
    d->score_proxy = !d->score_strategy && !d->public_strategy && !o->hand_scores_complete &&
                     o->previous_strength_count > 0;
    d->ordago_factor = 1;
    d->real_total = real.total;
    d->real_covered = real.covered;
    d->method = MUS_ADAPTER_FALLBACK;
    d->reason = "no_trained_candidate";
    bool betting = phase_index(o->phase) >= 0;
    if (mus_phase(o->phase) && o->mus_count >= 1) {
        d->method = MUS_ADAPTER_MUS_LIMIT;
        d->reason = "one_mus_completed";
    } else {
        Candidate chosen[2];
        Action actions[2][MUS_GAME_MAX_LEGAL_ACTIONS];
        Probability probabilities[2][MUS_GAME_MAX_LEGAL_ACTIONS];
        size_t action_counts[2] = {0}, found = 0;
        double weights[2] = {1, 0};
        InfoSetKey attempted[3];
        size_t attempted_count = 0;
        Candidate exact = {.information = base};
        if (betting && real.representable && real.total < 40)
            use_bet(&exact.information, &real);
        bool exact_possible = !betting || (real.representable && real.total < 40 &&
                                           prior_total <= 40);
        if (exact_possible &&
            encode_information(a, &exact.information, &exact.key) ==
                CFR_STATUS_SUCCESS) {
            attempted[attempted_count++] = exact.key;
            status =
                lookup(a, &exact, d, actions[0], probabilities[0], &action_counts[0]);
            if (status == CFR_STATUS_SUCCESS) {
                chosen[found++] = exact;
                d->method = MUS_ADAPTER_EXACT;
            } else if (status != CFR_STATUS_NOT_FOUND)
                return status;
        }
        /* Interpolate only the last increment. The preceding history must fit v9. */
        if (!found && betting && o->history_count && !real.ordago &&
            prior_total <= 40 &&
            o->history[o->history_count - 1].action.kind == MUS_BOT_RAISE) {
            RealBet prefix;
            status = replay(o, o->history_count - 1, &prefix);
            if (status != CFR_STATUS_SUCCESS)
                return status;
            Candidate candidates[2];
            int amounts[2] = {-1, -1};
            bool repeated = false;
            for (int i = 0; i < 4; ++i)
                if (real.increment == increments[i] &&
                    (prefix.masks[prefix.actor] & (1U << i)))
                    repeated = true;
            if (prefix.representable && prefix.total < 40 && !repeated &&
                (!prefix.count || real.increment >= prefix.increment)) {
                MusOrdagoInformationSet before = base;
                before.player = prefix.actor;
                use_bet(&before, &prefix);
                Action allowed[MUS_GAME_MAX_LEGAL_ACTIONS];
                size_t allowed_count;
                if (mus_ordago_game_information_set_legal_actions(
                        &before, allowed, MUS_GAME_MAX_LEGAL_ACTIONS, &allowed_count) ==
                    CFR_STATUS_SUCCESS) {
                    for (size_t i = 0; i < allowed_count; ++i) {
                        int index = allowed[i] == MUS_ACTION_BET_TWO     ? 0
                                    : allowed[i] == MUS_ACTION_BET_FOUR  ? 1
                                    : allowed[i] == MUS_ACTION_BET_SEVEN ? 2
                                    : allowed[i] == MUS_ACTION_BET_TEN   ? 3
                                                                         : -1;
                        if (index < 0)
                            continue;
                        int amount = increments[index];
                        int side = amount <= real.increment ? 0 : 1;
                        if (amounts[side] >= 0 && (side == 0 ? amount <= amounts[side]
                                                             : amount >= amounts[side]))
                            continue;
                        RealBet mapped = prefix;
                        mapped.covered = prefix.count ? prefix.total : 1;
                        mapped.total += amount;
                        mapped.count++;
                        mapped.masks[prefix.actor] |= (uint8_t)(1U << index);
                        mapped.bettor = prefix.actor;
                        mapped.actor = o->player;
                        mapped.stage = MUS_BET_RESPONSE;
                        Candidate c = {.information = base,
                                       .distance =
                                           2 * difference(amount, real.increment)};
                        use_bet(&c.information, &mapped);
                        if (encode_information(
                                a, &c.information, &c.key) != CFR_STATUS_SUCCESS)
                            continue;
                        candidates[side] = c;
                        amounts[side] = amount;
                    }
                    /* A representable amount with missing data uses the general search.
                     */
                    if (amounts[0] != real.increment) {
                        for (int side = 0; side < 2; ++side) {
                            if (amounts[side] < 0)
                                continue;
                            attempted[attempted_count++] = candidates[side].key;
                            status =
                                lookup(a, &candidates[side], d, actions[found],
                                       probabilities[found], &action_counts[found]);
                            if (status == CFR_STATUS_SUCCESS) {
                                chosen[found++] = candidates[side];
                            } else if (status != CFR_STATUS_NOT_FOUND)
                                return status;
                        }
                        if (found) {
                            d->method = MUS_ADAPTER_INTERPOLATED;
                            if (found == 2) {
                                weights[0] = (double)(amounts[1] - real.increment) /
                                             (amounts[1] - amounts[0]);
                                weights[1] = 1 - weights[0];
                            }
                        }
                    }
                }
            }
        }
        if (!found && betting) {
            Candidate best[MUS_ADAPTER_MAX_LOOKUPS];
            size_t count;
            nearest_candidates(a, o, &base, &real, best, &count);
            for (size_t i = 0;
                 i < count && found < 2 && d->lookup_count < MUS_ADAPTER_MAX_LOOKUPS;
                 ++i) {
                bool tried = false;
                for (size_t j = 0; j < attempted_count; ++j)
                    if (attempted[j] == best[i].key)
                        tried = true;
                if (tried)
                    continue;
                status = lookup(a, &best[i], d, actions[found], probabilities[found],
                                &action_counts[found]);
                if (status == CFR_STATUS_SUCCESS)
                    chosen[found++] = best[i];
                else if (status != CFR_STATUS_NOT_FOUND)
                    return status;
            }
            if (found) {
                d->method = MUS_ADAPTER_NEAREST;
                if (found == 2) {
                    double sum =
                        (double)chosen[0].distance + (double)chosen[1].distance;
                    weights[0] = sum ? (double)chosen[1].distance / sum : 0.5;
                    weights[1] = 1 - weights[0];
                }
            }
        }
        if (found) {
            d->reason = NULL;
            for (size_t i = 0; i < found; ++i)
                add_policy(d, &chosen[i], weights[i], actions[i], probabilities[i],
                           action_counts[i], o, &real, fallback);
        }
    }
    if (!d->choice_count) {
        d->choices[0] = (MusBotChoice){fallback, 1};
        d->choice_count = 1;
    }
    if (a->config.score_adjustment && betting && !d->score_strategy) {
        double factor =
            ((40 - o->match_scores[o->player]) + a->config.score_offset) /
            ((40 - o->match_scores[1 - o->player]) + a->config.score_offset);
        if (factor < a->config.score_factor_min)
            factor = a->config.score_factor_min;
        if (factor > a->config.score_factor_max)
            factor = a->config.score_factor_max;
        d->ordago_factor = factor;
        for (size_t i = 0; i < d->choice_count; ++i)
            if (d->choices[i].action.kind == MUS_BOT_ORDAGO ||
                (real.ordago && d->choices[i].action.kind == MUS_BOT_ACCEPT))
                d->choices[i].probability *= factor;
    }
    double sum = 0;
    for (size_t i = 0; i < d->choice_count; ++i)
        sum += d->choices[i].probability;
    if (!isfinite(sum) || sum <= 0)
        return CFR_STATUS_NUMERIC_ERROR;
    for (size_t i = 0; i < d->choice_count; ++i)
        d->choices[i].probability /= sum;
    return CFR_STATUS_SUCCESS;
}

Status mus_adapter_decide(MusAdapter *a, const MusAdapterObservation *o,
                          MusAdapterDecision *out) {
    if (a == NULL || out == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    MusAdapterDecision result = {0};
    Status status = decide_impl(a, o, &result);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    double sample = random_probability(&a->rng), accumulated = 0;
    result.selected = result.choices[result.choice_count - 1].action;
    for (size_t i = 0; i < result.choice_count; ++i) {
        accumulated += result.choices[i].probability;
        if (sample < accumulated) {
            result.selected = result.choices[i].action;
            break;
        }
    }
    *out = result;
    return CFR_STATUS_SUCCESS;
}

const char *mus_bot_action_name(MusBotActionKind kind) {
    static const char *const names[] = {"pass",   "raise", "accept", "reject",
                                        "ordago", "mus",   "no_mus", "discard"};
    return kind >= MUS_BOT_PASS && kind <= MUS_BOT_DISCARD ? names[kind] : "unknown";
}
const char *mus_adapter_method_name(MusAdapterMethod method) {
    static const char *const names[] = {"exact", "interpolated", "nearest", "fallback",
                                        "mus_limit"};
    return method >= MUS_ADAPTER_EXACT && method <= MUS_ADAPTER_MUS_LIMIT
               ? names[method]
               : "unknown";
}
