#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "match_evaluation.h"
#include "mus_log.h"
#include "mus_sim.h"

/* Real numeric totals from two through forty suffice for this evaluator's
 * finite wager menu. The simulator validates each offered action. This menu
 * includes off-abstraction totals; a raise can increase an existing total by one. */
enum { MATCH_LEGAL_CAPACITY = 44, MATCH_HISTORY_CAPACITY = 44 };
typedef struct {
    PartidaMus *game;
    const MusMatchPolicy *policies;
    MusMatchResult *result;
    const MusMatchOptions *options;
    size_t match;
    size_t hand;
    int mano;
    int initial_scores[2];
    uint8_t mus_count;
    uint8_t discard_counts[1][2];
    Player mus_stop_player;
    int completed_lances;
    int active_lance;
    EnviteMus after_action;
    MusLanceStrength strengths[3];
    unsigned int active_strength;
    int64_t prior_total;
    int64_t pending_total;
    bool previous_scores_public;
    MusBotEvent history[MATCH_HISTORY_CAPACITY];
    size_t history_count;
    Status status;
} MatchHand;

const char *mus_match_phase_name(MusGamePhase phase) {
    switch (phase) {
    case MUS_PHASE_MUS_PLAYER_0: return "mus-mano";
    case MUS_PHASE_MUS_PLAYER_1: return "mus-postre";
    case MUS_PHASE_DISCARD_PLAYER_0: return "discard-mano";
    case MUS_PHASE_DISCARD_PLAYER_1: return "discard-postre";
    case MUS_PHASE_GRANDE: return "grande";
    case MUS_PHASE_CHICA: return "chica";
    case MUS_PHASE_PARES: return "pares";
    case MUS_PHASE_JUEGO: return "juego";
    case MUS_PHASE_PUNTO: return "punto";
    default: return "invalid";
    }
}
const char *mus_match_method_name(size_t method) {
    return method == MUS_MATCH_BASELINE_METHOD ? "baseline"
        : mus_adapter_method_name((MusAdapterMethod)method);
}
static Player relative_player(const MatchHand *hand, int absolute) {
    return (Player)((absolute + 2 - hand->mano) % 2);
}
static int lance_index(Ronda phase) {
    return phase == GRANDE ? 0 : phase == CHICA ? 1 : phase == PARES ? 2 : 3;
}
static MusGamePhase betting_phase(Ronda phase) {
    static const MusGamePhase phases[] = {MUS_PHASE_GRANDE, MUS_PHASE_CHICA,
        MUS_PHASE_PARES, MUS_PHASE_JUEGO, MUS_PHASE_PUNTO};
    return phases[phase];
}
static Status own_hand(const Mano *cards, uint8_t counts[MUS_GAME_CLASS_COUNT]) {
    if (cards == NULL || cards->cartas == NULL || cards->tamano != 4)
        return CFR_STATUS_INVALID_ARGUMENT;
    memset(counts, 0, MUS_GAME_CLASS_COUNT);
    for (size_t i = 0; i < 4; ++i) {
        const int value = valorMus(cards->cartas[i]);
        if (value < 0 || value >= MUS_GAME_CLASS_COUNT) return CFR_STATUS_INVALID_ARGUMENT;
        counts[value]++;
    }
    return CFR_STATUS_SUCCESS;
}
static MusAdapterObservation observation(const MatchHand *hand, int absolute, MusGamePhase phase,
                                         const int scores[2]) {
    MusAdapterObservation result = {.player = relative_player(hand, absolute), .phase = phase,
        .mus_count = hand->mus_count, .mus_stop_player = hand->mus_stop_player,
        .has_hand_start_scores = true, .has_pending_wager_total = true,
        .hand_scores_complete = hand->previous_scores_public};
    memcpy(result.discard_counts, hand->discard_counts, sizeof(hand->discard_counts));
    for (int p = 0; p < 2; ++p) {
        const int seat = (hand->mano + p) % 2;
        result.hand_start_scores[p] = hand->initial_scores[seat];
        result.match_scores[p] = scores[seat];
        result.known_hand_scores[p] = scores[seat] - hand->initial_scores[seat];
    }
    return result;
}
static bool is_legal(const MusAdapterObservation *o, MusBotAction action) {
    for (size_t i = 0; i < o->legal_action_count; ++i)
        if (o->legal_actions[i].kind == action.kind && o->legal_actions[i].amount == action.amount)
            return true;
    return false;
}
static Status choose(MatchHand *hand, int seat, const Mano *cards,
                     MusAdapterObservation *o, MusBotAction *action) {
    if (hand->status != CFR_STATUS_SUCCESS) return hand->status;
    Status status = own_hand(cards, o->hand);
    MusAdapterDecision decision = {0};
    if (status == CFR_STATUS_SUCCESS)
        status = hand->policies[seat].decide(hand->policies[seat].context, o, &decision);
    if (status != CFR_STATUS_SUCCESS) return hand->status = status;
    if (!is_legal(o, decision.selected) || decision.method < MUS_ADAPTER_EXACT ||
        decision.method > MUS_ADAPTER_MUS_LIMIT) return hand->status = CFR_STATUS_INVALID_ARGUMENT;
    const size_t method = hand->policies[seat].baseline ? MUS_MATCH_BASELINE_METHOD
                                                      : (size_t)decision.method;
    const size_t route = hand->policies[seat].baseline ? 3U : decision.score_strategy ? 2U
        : decision.public_strategy ? 1U : 0U;
    MusMatchResult *r = hand->result;
    r->decisions[seat]++;
    r->methods[seat][method]++;
    r->phases[seat][o->phase][method]++;
    r->scores[seat][o->hand_start_scores[0]][o->hand_start_scores[1]][method]++;
    r->actions[seat][decision.selected.kind][method]++;
    r->strategies[seat][route]++;
    if (hand->options->trace != NULL &&
        fprintf(hand->options->trace,
            "%zu,%zu,%d,%d,%s,%d,%d,%d,%d,%" PRId64 ",%s,%zu,%s,%" PRId64 "\n",
            hand->match, hand->hand, seat, hand->mano, mus_match_phase_name(o->phase),
            o->hand_start_scores[0], o->hand_start_scores[1], o->match_scores[0], o->match_scores[1],
            o->pending_wager_total, mus_match_method_name(method), route,
            mus_bot_action_name(decision.selected.kind), decision.selected.amount) < 0)
        return hand->status = CFR_STATUS_IO_ERROR;
    *action = decision.selected;
    return CFR_STATUS_SUCCESS;
}

static int decide_mus(const Mano *cards, int seat, int mano, const int scores[2], void *context) {
    MatchHand *hand = context;
    if (mano != hand->mano) { hand->status = CFR_STATUS_INVALID_ARGUMENT; return -1; }
    const Player p = relative_player(hand, seat);
    MusAdapterObservation o = observation(hand, seat,
        p == CFR_PLAYER_0 ? MUS_PHASE_MUS_PLAYER_0 : MUS_PHASE_MUS_PLAYER_1, scores);
    const MusBotAction legal[] = {{MUS_BOT_MUS, 0}, {MUS_BOT_NO_MUS, 0}};
    o.legal_actions = legal; o.legal_action_count = 2;
    MusBotAction action;
    if (choose(hand, seat, cards, &o, &action) != CFR_STATUS_SUCCESS) return -1;
    if (action.kind == MUS_BOT_NO_MUS) { hand->mus_stop_player = p; return 0; }
    /* The deployed adapter enforces one mus. A custom baseline must obey it. */
    if (hand->mus_count != 0) { hand->status = CFR_STATUS_INVALID_ARGUMENT; return -1; }
    return 1;
}
static int choose_discards(const Mano *cards, int seat, int discarded[4], void *context) {
    MatchHand *hand = context;
    const Player p = relative_player(hand, seat);
    MusAdapterObservation o = observation(hand, seat,
        p == CFR_PLAYER_0 ? MUS_PHASE_DISCARD_PLAYER_0 : MUS_PHASE_DISCARD_PLAYER_1, hand->game->tantos);
    if (own_hand(cards, o.hand) != CFR_STATUS_SUCCESS) { hand->status = CFR_STATUS_INVALID_ARGUMENT; return 1; }
    MusBotAction legal[15]; size_t count = 0;
    for (unsigned int mask = 1; mask < 16; ++mask)
        if (mus_ordago_game_discard_mask_is_canonical(o.hand, mask))
            legal[count++] = (MusBotAction){MUS_BOT_DISCARD, (int64_t)mask};
    o.legal_actions = legal; o.legal_action_count = count;
    MusBotAction action;
    if (choose(hand, seat, cards, &o, &action) != CFR_STATUS_SUCCESS) return 1;
    unsigned int canonical = 0, discarded_count = 0;
    memset(discarded, 0, sizeof(int) * 4);
    /* Stable class order maps the canonical mask back to physical card slots. */
    for (int value = 0; value < MUS_GAME_CLASS_COUNT; ++value)
        for (size_t i = 0; i < 4; ++i)
            if (valorMus(cards->cartas[i]) == value) {
                discarded[i] = (int)(((uint64_t)action.amount >> canonical) & 1U);
                discarded_count += (unsigned int)discarded[i];
                canonical++;
            }
    hand->discard_counts[0][p] = (uint8_t)discarded_count;
    if (p == CFR_PLAYER_1) hand->mus_count = 1;
    return 0;
}

static Status finish_previous_lances(MatchHand *hand, int next) {
    while (hand->completed_lances < next) {
        const int previous = hand->completed_lances;
        if (hand->active_lance == previous) {
            hand->strengths[previous] = (MusLanceStrength)hand->active_strength;
            const EnviteMus *w = &hand->after_action;
            if (w->estado == ENVITE_ACEPTADO) {
                hand->pending_total += w->cantidad;
                hand->prior_total += w->cantidad;
                hand->previous_scores_public = false;
            } else if (w->estado == ENVITE_RECHAZADO || w->estado == ORDAGO_RECHAZADO) {
                hand->prior_total += w->cantidadAnterior;
                if (previous == 2) hand->previous_scores_public = false;
            } else if (w->estado == ENVITE_AL_PASO) {
                hand->previous_scores_public = false;
            } else return CFR_STATUS_INVALID_ARGUMENT;
        } else if (previous == 2) {
            /* Pares eligibility was publicly declared when the engine skipped
             * that negotiation. Only the yes/no declaration enters observation. */
            const int a = tipoPares(hand->game->manos[hand->mano]);
            const int b = tipoPares(hand->game->manos[1 - hand->mano]);
            if (a < 0 || b < 0 || (a != NO_PAR && b != NO_PAR)) return CFR_STATUS_INVALID_ARGUMENT;
            hand->strengths[2] = a != NO_PAR ? MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_0
                : b != NO_PAR ? MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_1
                              : MUS_LANCE_STRENGTH_PARES_SKIPPED_NONE;
            if (a != NO_PAR || b != NO_PAR) hand->previous_scores_public = false;
        } else return CFR_STATUS_INVALID_ARGUMENT;
        hand->completed_lances++;
    }
    return CFR_STATUS_SUCCESS;
}
static AccionEnviteMus engine_action(const EnviteMus *wager, MusBotAction action) {
    switch (action.kind) {
    case MUS_BOT_PASS: return (AccionEnviteMus){ACCION_PASAR, 0};
    case MUS_BOT_RAISE: return (AccionEnviteMus){ACCION_ENVIDAR, wager->cantidad + (int)action.amount};
    case MUS_BOT_ACCEPT: return (AccionEnviteMus){ACCION_QUERER, 0};
    case MUS_BOT_REJECT: return (AccionEnviteMus){ACCION_NO_QUERER, 0};
    case MUS_BOT_ORDAGO: return (AccionEnviteMus){ACCION_ORDAGO, 0};
    default: return (AccionEnviteMus){ACCION_ENVIDAR, 0};
    }
}
static AccionEnviteMus decide_wager(const Mano *cards, int seat, int mano, const int scores[2],
                                   Ronda phase, const EnviteMus *wager, void *context) {
    MatchHand *hand = context;
    const AccionEnviteMus invalid = {ACCION_ENVIDAR, 0};
    if (mano != hand->mano || phase < GRANDE || phase > PUNTO) {
        hand->status = CFR_STATUS_INVALID_ARGUMENT; return invalid;
    }
    const int index = lance_index(phase);
    if (hand->active_lance != index) {
        hand->status = finish_previous_lances(hand, index);
        if (hand->status != CFR_STATUS_SUCCESS) return invalid;
        hand->history_count = 0;
        hand->active_strength = 0;
        hand->active_lance = index;
    }
    MusAdapterObservation o = observation(hand, seat, betting_phase(phase), scores);
    o.previous_strength_count = (uint8_t)index;
    memcpy(o.previous_strengths, hand->strengths, sizeof(hand->strengths));
    o.prior_wager_total = hand->prior_total;
    o.pending_wager_total = hand->pending_total;
    o.history = hand->history; o.history_count = hand->history_count;
    MusBotAction candidates[MATCH_LEGAL_CAPACITY]; size_t count = 0;
    candidates[count++] = (MusBotAction){wager->estado == ENVITE_AL_PASO ? MUS_BOT_PASS : MUS_BOT_REJECT, 0};
    if (wager->estado != ENVITE_AL_PASO) candidates[count++] = (MusBotAction){MUS_BOT_ACCEPT, 0};
    if (wager->estado != ORDAGO_PENDIENTE) {
        candidates[count++] = (MusBotAction){MUS_BOT_ORDAGO, 0};
        for (int total = 2; total <= 40; ++total)
            if (total > wager->cantidad)
                candidates[count++] = (MusBotAction){MUS_BOT_RAISE, total - wager->cantidad};
    }
    MusBotAction legal[MATCH_LEGAL_CAPACITY]; size_t legal_count = 0;
    for (size_t i = 0; i < count; ++i) {
        EnviteMus copy = *wager;
        if (aplicarAccionEnviteMus(&copy, seat, engine_action(wager, candidates[i])) == 0)
            legal[legal_count++] = candidates[i];
    }
    o.legal_actions = legal; o.legal_action_count = legal_count;
    MusBotAction action;
    if (choose(hand, seat, cards, &o, &action) != CFR_STATUS_SUCCESS) return invalid;
    const AccionEnviteMus chosen = engine_action(wager, action);
    hand->after_action = *wager;
    if (aplicarAccionEnviteMus(&hand->after_action, seat, chosen) != 0 ||
        hand->history_count >= MATCH_HISTORY_CAPACITY) {
        hand->status = CFR_STATUS_INVALID_ARGUMENT; return invalid;
    }
    if (action.kind == MUS_BOT_RAISE || action.kind == MUS_BOT_ACCEPT || action.kind == MUS_BOT_ORDAGO)
        hand->active_strength |= 1U << o.player;
    hand->history[hand->history_count++] = (MusBotEvent){o.player, action};
    return chosen;
}

Status mus_match_evaluate(const MusMatchOptions *options, const MusMatchPolicy policies[2],
                          MusMatchResult *result) {
    if (options == NULL || policies == NULL || result == NULL || options->matches == 0 ||
        options->max_hands_per_match == 0 || policies[0].decide == NULL || policies[1].decide == NULL ||
        options->initial_scores[0] < 0 || options->initial_scores[0] >= 40 ||
        options->initial_scores[1] < 0 || options->initial_scores[1] >= 40)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (MUS_GAME_MAX_MUSES != 1) return CFR_STATUS_INCOMPATIBLE_GAME;
    if (options->matches > SIZE_MAX / options->max_hands_per_match ||
        options->matches > UINT64_MAX / options->max_hands_per_match / 200)
        return CFR_STATUS_INVALID_ARGUMENT;
    MusMatchResult *temporary = calloc(1, sizeof(*temporary));
    if (temporary == NULL) return CFR_STATUS_OUT_OF_MEMORY;
    PartidaMus game = {0};
    Status status = iniciarPartidaMusConJugadores(&game, 2) == 0 ? CFR_STATUS_SUCCESS
                                                              : CFR_STATUS_OUT_OF_MEMORY;
    if (status != CFR_STATUS_SUCCESS) { free(temporary); return status; }
    const NivelLog old_level = obtenerNivelLog();
    fijarNivelLog(LOG_SILENCIO);
    srand(options->seed);
    if (options->trace != NULL && fputs("match,hand,policy,mano,phase,start_mano,start_postre,"
        "paid_mano,paid_postre,pending,method,strategy,action,amount\n", options->trace) == EOF)
        status = CFR_STATUS_IO_ERROR;
    for (size_t match = 0; match < options->matches && status == CFR_STATUS_SUCCESS; ++match) {
        game.tantos[0] = options->initial_scores[0]; game.tantos[1] = options->initial_scores[1];
        game.mano = (int)(match % 2);
        int winner = 0;
        size_t hands = 0;
        while (winner == 0 && hands < options->max_hands_per_match) {
            MatchHand hand = {.game = &game, .policies = policies, .result = temporary,
                .options = options, .match = match + 1, .hand = hands + 1, .mano = game.mano,
                .initial_scores = {game.tantos[0], game.tantos[1]}, .active_lance = -1,
                .previous_scores_public = true};
            const EstrategiaMus strategies[4] = {
                {decide_mus, choose_discards, decide_wager, &hand},
                {decide_mus, choose_discards, decide_wager, &hand}, {0}, {0}};
            winner = simularRondaMusConEstrategias(&game, strategies);
            hands++;
            if (winner < 0 || hand.status != CFR_STATUS_SUCCESS) {
                status = hand.status != CFR_STATUS_SUCCESS ? hand.status : CFR_STATUS_INVALID_ARGUMENT;
                break;
            }
        }
        if (status != CFR_STATUS_SUCCESS) break;
        if (winner < 1 || winner > 2) {
            fprintf(stderr, "Match %zu reached the %zu-hand limit.\n", match + 1,
                    options->max_hands_per_match);
            status = CFR_STATUS_NOT_FOUND;
            break;
        }
        temporary->matches++;
        temporary->wins[winner - 1]++;
        temporary->hands += hands;
        temporary->checksum = temporary->checksum * UINT64_C(1099511628211) +
            (uint64_t)(game.tantos[0] * 41 + game.tantos[1] + winner * 1681);
    }
    if (options->trace != NULL && fflush(options->trace) != 0) status = CFR_STATUS_IO_ERROR;
    fijarNivelLog(old_level);
    if (destruirPartidaMus(&game) != 0) status = CFR_STATUS_INVALID_ARGUMENT;
    if (status == CFR_STATUS_SUCCESS) *result = *temporary;
    free(temporary);
    return status;
}
