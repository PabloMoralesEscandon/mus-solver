#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <string.h>

#include "cfr/info_node.h"
#include "cfr/info_store.h"
#include "mus_solver/mus_four.h"
#include "mus_simplified_internal.h"

static const uint8_t capacities[8] = {8, 4, 4, 4, 4, 4, 4, 8};
static const uint8_t values[8] = {1, 4, 5, 6, 7, 10, 10, 10};
static const uint8_t increments[4] = {2, 4, 7, 10};
static const Action wager_actions[4] = {
    MUS_ACTION_BET_TWO, MUS_ACTION_BET_FOUR,
    MUS_ACTION_BET_SEVEN, MUS_ACTION_BET_TEN
};
static const uint8_t game_values[8] = {33, 34, 35, 36, 37, 40, 32, 31};

/* Rank the reachable team masks. Teams alternate and cannot reuse an
 * increment. The executing seat is not part of this history. */
#define MASK_RADIX 51
#define PHASE_RADIX 16993
#define RESPONSE_STAGE_RADIX 8
/* Rank masks together with their score budgets, ordago flag and the last
 * resolved response. Numeric offers constrain the remaining score budget. */
#define BOARD_RADIX UINT64_C(5912626)
/* A single hand can reach 40 public points before its final lance. These
 * boards have no numeric wager masks: only pass and ordago remain legal. */
#define SINGLE_HAND_BOARD_RADIX (BOARD_RADIX + UINT64_C(81) * 40 * 2)
#if MUS_GAME_MAX_MUSES == 1
#define PUBLIC_MUS_RADIX 260
#define EXACT_KEY_BOUND UINT64_C(1650000)
#else
#define PUBLIC_MUS_RADIX 66564
#define EXACT_KEY_BOUND UINT64_C(2062500000)
#endif
#define BET_KEY_BOUND (UINT64_C(1) * PHASE_RADIX * PUBLIC_MUS_RADIX * \
                      BOARD_RADIX * 4 * RESPONSE_STAGE_RADIX * 2)
#define SINGLE_HAND_BET_KEY_BOUND (UINT64_C(1) * PHASE_RADIX * PUBLIC_MUS_RADIX * \
                                  SINGLE_HAND_BOARD_RADIX * 4 * RESPONSE_STAGE_RADIX * 2)
_Static_assert(SINGLE_HAND_BET_KEY_BOUND + EXACT_KEY_BOUND <= INT64_MAX,
               "Four-player keys fit int64_t");
static const bool single_hand_context = true;
_Static_assert(RESPONSE_STAGE_RADIX == MUS_FOUR_RESPONSE_ORDAGO + 2,
               "Response stages cover opening, first reply and all proposals");
/* Initial deal; unanimous mus, discards and redraws; and four lances with
 * three opening passes, an opening, and two replies per offer.
 * There can be five numeric offers followed by one ordago per lance. */
_Static_assert(16 + 24 * MUS_GAME_MAX_MUSES +
                   4 * (3 + 1 + 2 * (MUS_ORDAGO_MAX_NUMERIC_WAGERS + 1)) <=
                   MUS_FOUR_UNDO_CAPACITY,
               "Four-player undo holds both teammates' replies");
static uint8_t mask_codes[256];
static uint8_t masks_by_code[MASK_RADIX];
static uint8_t mask_totals[MASK_RADIX];
static uint8_t mask_counts[MASK_RADIX];
static uint8_t mask_last_responses[MASK_RADIX];
static unsigned int board_offsets[MASK_RADIX + 1];
static pthread_once_t mask_once = PTHREAD_ONCE_INIT;

static unsigned int sum_squares(unsigned int n) {
    return n * (n + 1) * (2 * n + 1) / 6;
}

static unsigned int memory_radix(unsigned int mask_code, bool ordago) {
    if (ordago)
        return mask_counts[mask_code] == 0 ? 1 : 12;
    return mask_counts[mask_code] <= 1 ? 1
                                       : 2U * mask_last_responses[mask_code];
}

static unsigned int score_radix(unsigned int mask_code) {
    return mask_counts[mask_code] == 0 ? 64000
                                       : sum_squares(40 - mask_totals[mask_code]);
}

static void visit_masks(unsigned int mask, int last, unsigned int minimum,
                         unsigned int count, unsigned int total) {
    mask_codes[mask] = 1;
    if (count == MUS_ORDAGO_MAX_NUMERIC_WAGERS)
        return;
    for (int team = 0; team < 2; ++team) {
        if (team == last)
            continue;
        for (unsigned int i = minimum; i < 4; ++i) {
            if (count != 0 && i == 2)
                continue;
            const unsigned int bit = 1U << (4U * (unsigned int)team + i);
            if ((mask & bit) == 0 && total + increments[i] < 40)
                visit_masks(mask | bit, team, i, count + 1,
                            total + increments[i]);
        }
    }
}

static void initialize_masks(void) {
    visit_masks(0, -1, 0, 0, 0);
    unsigned int code = 0;
    for (unsigned int mask = 0; mask < 256; ++mask) {
        if (mask_codes[mask] != 0) {
            if (code < MASK_RADIX)
                masks_by_code[code] = (uint8_t)mask;
            mask_codes[mask] = (uint8_t)(++code);
        }
    }
    for (unsigned int i = 0; i < MASK_RADIX; ++i) {
        const unsigned int mask = masks_by_code[i];
        unsigned int last = 0;
        for (unsigned int p = 0; p < 2; ++p)
            for (unsigned int w = 0; w < 4; ++w)
                if ((mask & (1U << (p * 4 + w))) != 0) {
                    mask_totals[i] += increments[w];
                    ++mask_counts[i];
                    if (increments[w] > last)
                        last = increments[w];
                }
        mask_last_responses[i] = (uint8_t)(last == 2 ? MUS_FOUR_RESPONSE_BET_TWO
            : last == 4 ? MUS_FOUR_RESPONSE_BET_FOUR
            : last == 10 ? MUS_FOUR_RESPONSE_BET_TEN : MUS_FOUR_RESPONSE_NONE);
        board_offsets[i + 1] = board_offsets[i] + score_radix(i) *
            (memory_radix(i, false) + memory_radix(i, true));
    }
}

static unsigned int bit_count(unsigned int mask) {
    unsigned int n = 0;
    while (mask != 0) {
        n += mask & 1U;
        mask >>= 1;
    }
    return n;
}

static unsigned int hand_count(const uint8_t hand[8]) {
    unsigned int n = 0;
    for (int c = 0; c < 8; ++c)
        n += hand[c];
    return n;
}

static unsigned int choose(unsigned int n, unsigned int k) {
    if (k > n)
        return 0;
    unsigned int result = 1;
    for (unsigned int i = 1; i <= k; ++i)
        result = result * (n - k + i) / i;
    return result;
}

static unsigned int exact_hand_code(const uint8_t hand[8]) {
    unsigned int result = 0, i = 0;
    for (unsigned int c = 0; c < 8; ++c)
        for (unsigned int copy = 0; copy < hand[c]; ++copy, ++i)
            result += choose(c + i, i + 1);
    return result;
}

static void exact_hand_decode(unsigned int code, uint8_t hand[8]) {
    memset(hand, 0, 8);
    unsigned int limit = 10;
    for (unsigned int k = 4; k != 0; --k) {
        while (choose(limit, k) > code)
            --limit;
        ++hand[limit - (k - 1)];
        code -= choose(limit, k);
        if (limit != 0)
            --limit;
    }
}

static MusHandSummary summarize(const uint8_t hand[8]) {
    MusHandSummary result = {0};
    if (hand_count(hand) != 4)
        return result;
    result.game_key = -1;
    int weight = 1;
    int high = -1, low = -1, type = 0;
    for (int c = 0; c < 8; ++c) {
        result.grande_key += hand[c] * weight;
        result.chica_key += hand[7 - c] * weight;
        result.point_value += hand[c] * values[c];
        weight *= 10;
        if (hand[c] == 4) {
            type = 3;
            high = low = c;
        } else if (hand[c] == 3) {
            type = 2;
            high = c;
        } else if (hand[c] == 2) {
            if (type == 1) {
                type = 3;
                low = high;
                high = c;
            } else {
                type = 1;
                high = c;
            }
        }
    }
    result.pairs_points = type;
    result.pairs_key = type == 3 ? (high + 1) * 1000 + (low + 1) * 100
                         : type == 2 ? (high + 1) * 10
                         : type == 1 ? high + 1 : 0;
    for (int i = 0; i < 8; ++i)
        if (result.point_value == game_values[i]) {
            result.game_key = i;
            result.game_points = result.point_value == 31 ? 3 : 2;
        }
    return result;
}

static void refresh_summary(MusFourGameState *state, unsigned int player) {
    state->hand_summaries[player] = summarize(state->model.hands[player]);
}

static bool betting(unsigned int phase) {
    return phase >= MUS_FOUR_PHASE_GRANDE && phase <= MUS_FOUR_PHASE_PUNTO;
}

static unsigned int lance_index(unsigned int phase) {
    return phase >= MUS_FOUR_PHASE_JUEGO ? 3 : phase - MUS_FOUR_PHASE_GRANDE;
}

static int summary_strength(const MusHandSummary *s, unsigned int phase) {
    switch (phase) {
    case MUS_FOUR_PHASE_GRANDE: return s->grande_key;
    case MUS_FOUR_PHASE_CHICA: return s->chica_key;
    case MUS_FOUR_PHASE_PARES: return s->pairs_key;
    case MUS_FOUR_PHASE_JUEGO: return s->game_key;
    default: return s->point_value;
    }
}

static unsigned int winning_team(const MusFourGameState *state,
                                  unsigned int phase) {
    unsigned int winner = 0;
    for (unsigned int p = 1; p < 4; ++p)
        if (summary_strength(&state->hand_summaries[p], phase) >
            summary_strength(&state->hand_summaries[winner], phase))
            winner = p;
    return winner % 2;
}

static bool pay(MusFourGameModel *m, unsigned int team, unsigned int amount,
                bool immediate) {
    const unsigned int cap = m->score_target == 0 ? 40 : m->score_target;
    unsigned int available = cap - m->initial_scores[team];
    unsigned int total = m->settled_scores[team] + amount;
    m->settled_scores[team] = (uint8_t)(total > available ? available : total);
    if (immediate)
        m->paid_scores[team] = m->settled_scores[team];
    if (m->score_target != 0 && total >= available) {
        m->phase = MUS_FOUR_PHASE_TERMINAL;
        return true;
    }
    return false;
}

static void count_hand(MusFourGameState *state) {
    MusFourGameModel *m = &state->model;
    for (unsigned int lance = 0; lance < 4; ++lance) {
        unsigned int phase = MUS_FOUR_PHASE_GRANDE + lance;
        if (lance == 3) {
            bool game = false;
            for (int p = 0; p < 4; ++p)
                game |= state->hand_summaries[p].game_points != 0;
            phase = game ? MUS_FOUR_PHASE_JUEGO : MUS_FOUR_PHASE_PUNTO;
        }
        const unsigned int outcome = m->outcomes[lance];
        const bool rejected = outcome == MUS_LANCE_FIRST_BET_REJECTED ||
                              outcome == MUS_LANCE_SECOND_BET_REJECTED;
        const bool accepted = outcome == MUS_LANCE_FIRST_BET_ACCEPTED ||
                              outcome == MUS_LANCE_SECOND_BET_ACCEPTED;
        unsigned int team = rejected ? m->outcome_bettors[lance] % 2
                                      : winning_team(state, phase);
        unsigned int amount = accepted ? m->outcome_wagers[lance] : 0;
        if (phase == MUS_FOUR_PHASE_PARES || phase == MUS_FOUR_PHASE_JUEGO) {
            for (unsigned int p = team; p < 4; p += 2)
                amount += (unsigned int)(phase == MUS_FOUR_PHASE_PARES
                              ? state->hand_summaries[p].pairs_points
                              : state->hand_summaries[p].game_points);
        } else if (phase == MUS_FOUR_PHASE_PUNTO) {
            amount += 1;
        } else if (!rejected && !accepted) {
            amount = 1;
        }
        if (amount != 0 && pay(m, team, amount, false))
            return;
    }
    m->phase = MUS_FOUR_PHASE_TERMINAL;
}

static unsigned int next_seat(unsigned int mask, unsigned int after) {
    for (unsigned int n = 1; n <= 4; ++n) {
        const unsigned int p = (after + n) % 4;
        if ((mask & (1U << p)) != 0)
            return p;
    }
    return 4;
}

static void start_lance(MusFourGameState *state, unsigned int phase) {
    MusFourGameModel *m = &state->model;
    for (;;) {
        m->phase = (uint8_t)phase;
        m->eligible_mask = 15;
        if (phase == MUS_FOUR_PHASE_PARES || phase == MUS_FOUR_PHASE_JUEGO) {
            m->eligible_mask = 0;
            for (unsigned int p = 0; p < 4; ++p) {
                bool eligible = phase == MUS_FOUR_PHASE_PARES
                                    ? state->hand_summaries[p].pairs_points != 0
                                    : state->hand_summaries[p].game_points != 0;
                if (eligible)
                    m->eligible_mask |= (uint8_t)(1U << p);
            }
            if (phase == MUS_FOUR_PHASE_JUEGO && m->eligible_mask == 0) {
                phase = MUS_FOUR_PHASE_PUNTO;
                m->phase = (uint8_t)phase;
                m->eligible_mask = 15;
            }
        }
        m->passed_mask = 0;
        m->pending_response = MUS_FOUR_RESPONSE_NONE;
        m->last_response_player = 0;
        memset(m->last_responses, 0, sizeof(m->last_responses));
        m->bettor = m->current_wager = m->previous_wager = 0;
        m->current_wager_is_ordago = false;
        m->current_strength = MUS_LANCE_STRENGTH_NONE;
        memset(m->used_wager_action_masks, 0, sizeof(m->used_wager_action_masks));
        if ((m->eligible_mask & 5) != 0 && (m->eligible_mask & 10) != 0) {
            m->current_player = (uint8_t)next_seat(m->eligible_mask, 3);
            return;
        }
        const unsigned int lance = lance_index(phase);
        m->outcomes[lance] = MUS_LANCE_SKIPPED;
        m->outcome_strengths[lance] = m->eligible_mask == 0
            ? MUS_LANCE_STRENGTH_PARES_SKIPPED_NONE
            : (m->eligible_mask & 5) != 0
                ? MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_0
                : MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_1;
        if (phase >= MUS_FOUR_PHASE_JUEGO) {
            count_hand(state);
            return;
        }
        ++phase;
    }
}

static void finish_lance(MusFourGameState *state, unsigned int outcome) {
    MusFourGameModel *m = &state->model;
    const unsigned int phase = m->phase;
    const unsigned int lance = lance_index(phase);
    const bool accepted = outcome == MUS_LANCE_FIRST_BET_ACCEPTED ||
                          outcome == MUS_LANCE_SECOND_BET_ACCEPTED;
    const bool rejected = outcome == MUS_LANCE_FIRST_BET_REJECTED ||
                          outcome == MUS_LANCE_SECOND_BET_REJECTED;
    m->outcomes[lance] = (uint8_t)outcome;
    m->outcome_wagers[lance] = m->current_wager;
    m->outcome_bettors[lance] = m->bettor;
    m->outcome_strengths[lance] = m->current_strength;
    if (accepted && m->current_wager_is_ordago) {
        if (m->score_target == 0) {
            memset(m->settled_scores, 0, sizeof(m->settled_scores));
            memset(m->paid_scores, 0, sizeof(m->paid_scores));
        }
        pay(m, winning_team(state, phase), 40, true);
        m->phase = MUS_FOUR_PHASE_TERMINAL;
        return;
    }
    if (rejected && pay(m, m->bettor % 2, m->previous_wager, true))
        return;
    if (accepted)
        m->prior_wager_total += m->current_wager;
    if (phase >= MUS_FOUR_PHASE_JUEGO)
        count_hand(state);
    else
        start_lance(state, phase + 1);
}

static unsigned int compact_hand(unsigned int phase, const uint8_t hand[8],
                                  const MusHandSummary *summary) {
    if (phase == MUS_FOUR_PHASE_MUS || phase == MUS_FOUR_PHASE_DISCARD)
        return exact_hand_code(hand);
    if (phase == MUS_FOUR_PHASE_GRANDE) {
        int highest = 6;
        while (highest > 0 && hand[highest] == 0)
            --highest;
        return hand[7] == 4 ? 28 : hand[7] * 7U + (unsigned int)highest;
    }
    if (phase == MUS_FOUR_PHASE_CHICA) {
        unsigned int lowest = 1;
        while (lowest < 7 && hand[lowest] == 0)
            ++lowest;
        return hand[0] == 4 ? 28 : hand[0] * 7U + lowest - 1;
    }
    if (phase == MUS_FOUR_PHASE_PARES) {
        unsigned int high = 0, low = 0;
        for (unsigned int c = 0; c < 8; ++c)
            if (hand[c] >= 2) {
                low = high;
                high = c;
                if (hand[c] == 4)
                    low = high;
            }
        return summary->pairs_points == 3 ? 16 + high * (high + 1) / 2 + low
              : (summary->pairs_points == 2 ? 8 : 0) + high;
    }
    return phase == MUS_FOUR_PHASE_JUEGO ? (unsigned int)summary->game_key
                                          : (unsigned int)summary->point_value - 4;
}

static unsigned int all_masks(const uint8_t masks[2]) {
    unsigned int result = 0;
    for (unsigned int p = 0; p < 2; ++p)
        result |= (unsigned int)masks[p] << (p * 4);
    return result;
}

static unsigned int numeric_total(const uint8_t masks[2], unsigned int *last) {
    unsigned int total = 0;
    *last = 0;
    for (unsigned int p = 0; p < 2; ++p)
        for (unsigned int i = 0; i < 4; ++i)
            if ((masks[p] & (1U << i)) != 0) {
                total += increments[i];
                if (increments[i] > *last)
                    *last = increments[i];
            }
    return total;
}

static unsigned int response_rank(Action action) {
    switch (action) {
    case MUS_ACTION_REJECT: return MUS_FOUR_RESPONSE_REJECT;
    case MUS_ACTION_ACCEPT: return MUS_FOUR_RESPONSE_ACCEPT;
    case MUS_ACTION_BET_TWO: return MUS_FOUR_RESPONSE_BET_TWO;
    case MUS_ACTION_BET_FOUR: return MUS_FOUR_RESPONSE_BET_FOUR;
    case MUS_ACTION_BET_TEN: return MUS_FOUR_RESPONSE_BET_TEN;
    case MUS_ACTION_ORDAGO: return MUS_FOUR_RESPONSE_ORDAGO;
    default: return MUS_FOUR_RESPONSE_NONE;
    }
}

static Action response_action(unsigned int response) {
    static const Action actions[] = {
        MUS_ACTION_PASS, MUS_ACTION_REJECT, MUS_ACTION_ACCEPT,
        MUS_ACTION_BET_TWO, MUS_ACTION_BET_FOUR, MUS_ACTION_BET_TEN,
        MUS_ACTION_ORDAGO
    };
    return actions[response];
}

static Status observation_menu(const MusFourInformationSet *o,
                                Action *actions, size_t *count) {
    *count = 0;
    if (o->phase == MUS_FOUR_PHASE_MUS) {
        actions[(*count)++] = MUS_ACTION_MUS;
        actions[(*count)++] = MUS_ACTION_NO_MUS;
        return CFR_STATUS_SUCCESS;
    }
    if (o->phase == MUS_FOUR_PHASE_DISCARD) {
        for (unsigned int mask = 1; mask < 16; ++mask)
            if (mus_ordago_game_discard_mask_is_canonical(o->hand, mask))
                actions[(*count)++] = MUS_ACTION_DISCARD_MASK(mask);
        return *count != 0 ? CFR_STATUS_SUCCESS : CFR_STATUS_INVALID_ARGUMENT;
    }
    if (!betting(o->phase) || o->player >= 4)
        return CFR_STATUS_INVALID_ARGUMENT;
    const bool response = o->current_wager != 0;
    if (response) {
        actions[(*count)++] = MUS_ACTION_REJECT;
        actions[(*count)++] = MUS_ACTION_ACCEPT;
    } else {
        actions[(*count)++] = MUS_ACTION_PASS;
    }
    if (o->current_wager_is_ordago)
        return CFR_STATUS_SUCCESS;
    const unsigned int max_score = o->scores[0] > o->scores[1]
                                     ? o->scores[0] : o->scores[1];
    const unsigned int minimum = !response ? 0
        : o->previous_wager == 1 ? o->current_wager
                                : o->current_wager - o->previous_wager;
    const unsigned int count_used = bit_count(all_masks(o->used_team_wager_action_masks));
    const unsigned int team_used = o->used_team_wager_action_masks[o->player % 2];
    for (unsigned int i = 0; i < 4; ++i)
        if (count_used < MUS_ORDAGO_MAX_NUMERIC_WAGERS &&
            (!response || increments[i] != 7) &&
            increments[i] >= minimum &&
            (team_used & (1U << i)) == 0 &&
            max_score + o->prior_wager_total + o->current_wager + increments[i] < 40)
            actions[(*count)++] = wager_actions[i];
    actions[(*count)++] = MUS_ACTION_ORDAGO;
    return CFR_STATUS_SUCCESS;
}

static Status information_set_legal_actions(
    const MusFourInformationSet *information, Action *actions,
    size_t capacity, size_t *required_count, bool single_hand) {
    if (information == NULL || actions == NULL || required_count == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    InfoSetKey key;
    const Status encoded = single_hand
        ? mus_four_information_set_encode(information, &key)
        : mus_four_score_information_set_encode(information, &key);
    if (encoded != CFR_STATUS_SUCCESS)
        return CFR_STATUS_INVALID_ARGUMENT;
    Action temporary[16];
    size_t count;
    Status status = observation_menu(information, temporary, &count);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (capacity < count) {
        *required_count = count;
        return CFR_STATUS_BUFFER_TOO_SMALL;
    }
    memcpy(actions, temporary, count * sizeof(*actions));
    *required_count = count;
    return CFR_STATUS_SUCCESS;
}

Status mus_four_score_information_set_legal_actions(
    const MusFourInformationSet *information, Action *actions,
    size_t capacity, size_t *required_count) {
    return information_set_legal_actions(information, actions, capacity, required_count, false);
}

Status mus_four_information_set_legal_actions(
    const MusFourInformationSet *information, Action *actions,
    size_t capacity, size_t *required_count) {
    return information_set_legal_actions(information, actions, capacity, required_count, true);
}

static Status observe(const MusFourGameState *state,
                       MusFourInformationSet *information) {
    if (state == NULL || information == NULL || state->model.current_player >= 4 ||
        (!betting(state->model.phase) && state->model.phase != MUS_FOUR_PHASE_MUS &&
         state->model.phase != MUS_FOUR_PHASE_DISCARD))
        return CFR_STATUS_INVALID_ARGUMENT;
    const MusFourGameModel *m = &state->model;
    MusFourInformationSet o = {0};
    o.phase = m->phase;
    o.player = m->current_player;
    o.hand_code = (uint16_t)compact_hand(m->phase, m->hands[o.player],
                                       &state->hand_summaries[o.player]);
    if (!betting(m->phase))
        memcpy(o.hand, m->hands[o.player], sizeof(o.hand));
    o.mus_count = m->mus_count;
    memcpy(o.discard_counts, m->discard_counts, sizeof(o.discard_counts));
    o.mus_stopped = m->mus_stopped;
    o.mus_stop_player = m->mus_stop_player;
    if (betting(m->phase)) {
        o.scores[0] = m->initial_scores[0] + m->paid_scores[0];
        o.scores[1] = m->initial_scores[1] + m->paid_scores[1];
        o.prior_wager_total = m->prior_wager_total;
        const unsigned int previous = lance_index(m->phase);
        memcpy(o.previous_strengths, m->outcome_strengths, previous);
        o.eligible_mask = m->eligible_mask;
        o.bettor = m->bettor;
        o.current_wager = m->current_wager;
        o.previous_wager = m->previous_wager;
        o.current_wager_is_ordago = m->current_wager_is_ordago;
        o.pending_response = m->pending_response;
        o.last_response_player = m->last_response_player;
        memcpy(o.last_responses, m->last_responses, sizeof(o.last_responses));
        for (unsigned int team = 0; team < 2; ++team)
            o.used_team_wager_action_masks[team] = m->used_wager_action_masks[team] |
                                                 m->used_wager_action_masks[team + 2];
    }
    *information = o;
    return CFR_STATUS_SUCCESS;
}

static Status public_mus_encode(const MusFourInformationSet *o,
                                 unsigned int *result) {
    if (o->mus_count > MUS_GAME_MAX_MUSES)
        return CFR_STATUS_INVALID_ARGUMENT;
    unsigned int history = 0, radix = 1, offset = 0;
    for (unsigned int r = 0; r < o->mus_count; ++r) {
        offset += 4 * radix;
        radix *= 256;
        for (unsigned int p = 0; p < 4; ++p) {
            const unsigned int count = o->discard_counts[r][p];
            if (count < 1 || count > 4)
                return CFR_STATUS_INVALID_ARGUMENT;
            history = history * 4 + count - 1;
        }
    }
    for (unsigned int r = o->mus_count; r < MUS_GAME_MAX_MUSES; ++r)
        for (unsigned int p = 0; p < 4; ++p)
            if (o->discard_counts[r][p] != 0)
                return CFR_STATUS_INVALID_ARGUMENT;
    if (o->mus_stopped) {
        if (o->mus_count >= MUS_GAME_MAX_MUSES || o->mus_stop_player >= 4)
            return CFR_STATUS_INVALID_ARGUMENT;
        *result = offset + o->mus_stop_player * radix + history;
    } else {
        if (o->mus_count != MUS_GAME_MAX_MUSES)
            return CFR_STATUS_INVALID_ARGUMENT;
        *result = offset + history;
    }
    return *result < PUBLIC_MUS_RADIX ? CFR_STATUS_SUCCESS
                                      : CFR_STATUS_INVALID_ARGUMENT;
}

static void public_mus_decode(unsigned int code, MusFourInformationSet *o) {
    unsigned int radix = 1;
    for (unsigned int r = 0; r < MUS_GAME_MAX_MUSES; ++r) {
        if (code < 4 * radix) {
            o->mus_count = (uint8_t)r;
            o->mus_stopped = true;
            o->mus_stop_player = (uint8_t)(code / radix);
            code %= radix;
            goto history;
        }
        code -= 4 * radix;
        radix *= 256;
    }
    o->mus_count = MUS_GAME_MAX_MUSES;
history:
    for (unsigned int r = o->mus_count; r != 0; --r)
        for (unsigned int p = 4; p != 0; --p) {
            o->discard_counts[r - 1][p - 1] = (uint8_t)(code % 4 + 1);
            code /= 4;
        }
}

static const unsigned int phase_offsets[5] = {0, 29, 145, 6801, 13969};
static const unsigned int hand_radices[5] = {29, 29, 52, 8, 27};

static Status phase_encode(const MusFourInformationSet *o, unsigned int *result) {
    if (!betting(o->phase))
        return CFR_STATUS_INVALID_ARGUMENT;
    const unsigned int phase = o->phase - MUS_FOUR_PHASE_GRANDE;
    if (o->hand_code >= hand_radices[phase])
        return CFR_STATUS_INVALID_ARGUMENT;
    unsigned int code = o->hand_code;
    for (unsigned int l = 0; l < lance_index(o->phase); ++l) {
        const unsigned int radix = l == 2 ? 7 : 4;
        if (o->previous_strengths[l] >= radix)
            return CFR_STATUS_INVALID_ARGUMENT;
        code = code * radix + o->previous_strengths[l];
    }
    if (o->phase == MUS_FOUR_PHASE_PARES || o->phase == MUS_FOUR_PHASE_JUEGO) {
        if (o->eligible_mask > 15 || (o->eligible_mask & (1U << o->player)) == 0 ||
            (o->eligible_mask & 5) == 0 || (o->eligible_mask & 10) == 0)
            return CFR_STATUS_INVALID_ARGUMENT;
        unsigned int others = 0, shift = 0;
        for (unsigned int p = 0; p < 4; ++p)
            if (p != o->player)
                others |= ((o->eligible_mask >> p) & 1U) << shift++;
        code = code * 8 + others;
    } else if (o->eligible_mask != 15) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    *result = phase_offsets[phase] + code;
    return CFR_STATUS_SUCCESS;
}

static void phase_decode(unsigned int code, MusFourInformationSet *o) {
    unsigned int phase = 4;
    while (code < phase_offsets[phase])
        --phase;
    code -= phase_offsets[phase];
    o->phase = (uint8_t)(MUS_FOUR_PHASE_GRANDE + phase);
    if (phase == 2 || phase == 3) {
        unsigned int others = code % 8;
        code /= 8;
        o->eligible_mask = (uint8_t)(1U << o->player);
        for (unsigned int p = 0; p < 4; ++p)
            if (p != o->player) {
                o->eligible_mask |= (uint8_t)((others & 1U) << p);
                others >>= 1;
            }
    } else {
        o->eligible_mask = 15;
    }
    for (unsigned int l = lance_index(o->phase); l != 0; --l) {
        const unsigned int radix = l == 3 ? 7 : 4;
        o->previous_strengths[l - 1] = (uint8_t)(code % radix);
        code /= radix;
    }
    o->hand_code = (uint16_t)code;
}

static Status response_memory_encode(const MusFourInformationSet *o,
                                      unsigned int mask_code,
                                      unsigned int *result) {
    const unsigned int first = o->last_responses[0];
    const unsigned int second = o->last_responses[1];
    if (o->last_response_player >= 4 || first > MUS_FOUR_RESPONSE_ORDAGO ||
        second > MUS_FOUR_RESPONSE_ORDAGO)
        return CFR_STATUS_INVALID_ARGUMENT;
    const unsigned int radix = memory_radix(mask_code, o->current_wager_is_ordago);
    if (radix == 1) {
        /* No reply has resolved in this lance before its first raise. */
        if (first != MUS_FOUR_RESPONSE_NONE || second != MUS_FOUR_RESPONSE_NONE ||
            o->last_response_player != 0)
            return CFR_STATUS_INVALID_ARGUMENT;
        *result = 0;
        return CFR_STATUS_SUCCESS;
    }
    /* After a raise, its winning proposal and author are already encoded
     * by the offer and bettor. Store the other proposal and response order. */
    const unsigned int winner = radix / 2;
    const unsigned int other = (o->last_response_player + 2U) % 4;
    if ((o->eligible_mask & (1U << o->last_response_player)) == 0 ||
        ((o->eligible_mask & (1U << other)) != 0) !=
            (second != MUS_FOUR_RESPONSE_NONE))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (o->last_response_player == o->bettor && first == winner && second <= winner) {
        *result = second;
    } else if (other == o->bettor && first > MUS_FOUR_RESPONSE_NONE &&
               first < winner && second == winner) {
        *result = winner + first;
    } else {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    return CFR_STATUS_SUCCESS;
}

static void response_memory_decode(unsigned int code, unsigned int mask_code,
                                    MusFourInformationSet *o) {
    const unsigned int radix = memory_radix(mask_code, o->current_wager_is_ordago);
    if (radix == 1) {
        return;
    }
    const unsigned int winner = radix / 2;
    if (code <= winner) {
        o->last_response_player = o->bettor;
        o->last_responses[0] = (uint8_t)winner;
        o->last_responses[1] = (uint8_t)code;
    } else {
        o->last_response_player = (uint8_t)((o->bettor + 2U) % 4);
        o->last_responses[0] = (uint8_t)(code - winner);
        o->last_responses[1] = (uint8_t)winner;
    }
}

static Status board_encode(const MusFourInformationSet *o, unsigned int mask_code,
                            unsigned int *result) {
    if (board_offsets[MASK_RADIX] != BOARD_RADIX)
        return CFR_STATUS_INVALID_ARGUMENT;
    unsigned int memory;
    if (response_memory_encode(o, mask_code, &memory) != CFR_STATUS_SUCCESS)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (o->scores[0] == 40 || o->scores[1] == 40) {
        if (mask_counts[mask_code] != 0)
            return CFR_STATUS_INVALID_ARGUMENT;
        const unsigned int pair = o->scores[0] == 40 ? o->scores[1] : 41U + o->scores[0];
        *result = (unsigned int)BOARD_RADIX + (pair * 40 + o->prior_wager_total) * 2 +
                  (o->current_wager_is_ordago ? 1U : 0U);
        return CFR_STATUS_SUCCESS;
    }
    unsigned int score;
    if (mask_counts[mask_code] == 0) {
        /* An opening ordago can be rejected after earlier accepted wagers.
         * Their unscored total need not fit the updated public score. */
        score = (o->scores[0] * 40U + o->scores[1]) * 40U + o->prior_wager_total;
    } else {
        const unsigned int limit = 40U - mask_totals[mask_code];
        if (o->prior_wager_total >= limit)
            return CFR_STATUS_INVALID_ARGUMENT;
        const unsigned int width = limit - o->prior_wager_total;
        if (o->scores[0] >= width || o->scores[1] >= width)
            return CFR_STATUS_INVALID_ARGUMENT;
        score = sum_squares(limit) - sum_squares(width) +
                o->scores[0] * width + o->scores[1];
    }
    unsigned int code = board_offsets[mask_code];
    if (o->current_wager_is_ordago)
        code += score_radix(mask_code) * memory_radix(mask_code, false);
    code += score * memory_radix(mask_code, o->current_wager_is_ordago) + memory;
    *result = code;
    return CFR_STATUS_SUCCESS;
}

static Status board_decode(unsigned int code, MusFourInformationSet *o) {
    if (code >= SINGLE_HAND_BOARD_RADIX || board_offsets[MASK_RADIX] != BOARD_RADIX)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (code >= BOARD_RADIX) {
        code -= (unsigned int)BOARD_RADIX;
        o->current_wager_is_ordago = code % 2 != 0; code /= 2;
        o->prior_wager_total = (uint8_t)(code % 40); code /= 40;
        o->scores[0] = (uint8_t)(code <= 40 ? 40 : code - 41);
        o->scores[1] = (uint8_t)(code <= 40 ? code : 40);
        return CFR_STATUS_SUCCESS;
    }
    unsigned int low = 0, high = MASK_RADIX;
    while (low + 1 < high) {
        const unsigned int middle = low + (high - low) / 2;
        if (board_offsets[middle] <= code)
            low = middle;
        else
            high = middle;
    }
    const unsigned int mask_code = low;
    code -= board_offsets[mask_code];
    const unsigned int numeric_block = score_radix(mask_code) * memory_radix(mask_code, false);
    o->current_wager_is_ordago = code >= numeric_block;
    if (o->current_wager_is_ordago)
        code -= numeric_block;
    const unsigned int radix = memory_radix(mask_code, o->current_wager_is_ordago);
    const unsigned int memory = code % radix;
    code /= radix;
    unsigned int mask = masks_by_code[mask_code];
    for (unsigned int p = 0; p < 2; ++p) {
        o->used_team_wager_action_masks[p] = (uint8_t)(mask % 16);
        mask /= 16;
    }
    if (mask_counts[mask_code] == 0) {
        o->prior_wager_total = (uint8_t)(code % 40); code /= 40;
        o->scores[1] = (uint8_t)(code % 40);
        o->scores[0] = (uint8_t)(code / 40);
    } else {
        unsigned int width = 40U - mask_totals[mask_code];
        while (code >= width * width) {
            code -= width * width;
            ++o->prior_wager_total;
            --width;
        }
        o->scores[0] = (uint8_t)(code / width);
        o->scores[1] = (uint8_t)(code % width);
    }
    response_memory_decode(memory, mask_code, o);
    return CFR_STATUS_SUCCESS;
}

static Status information_set_encode(
    const MusFourInformationSet *o, InfoSetKey *key, bool single_hand) {
    if (o == NULL || key == NULL || o->player >= 4 ||
        o->pending_response > MUS_FOUR_RESPONSE_ORDAGO)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (o->phase == MUS_FOUR_PHASE_MUS || o->phase == MUS_FOUR_PHASE_DISCARD) {
        if (o->mus_count >= MUS_GAME_MAX_MUSES || hand_count(o->hand) != 4 ||
            o->mus_stopped || o->pending_response != MUS_FOUR_RESPONSE_NONE ||
            o->last_response_player != 0 ||
            o->last_responses[0] != MUS_FOUR_RESPONSE_NONE ||
            o->last_responses[1] != MUS_FOUR_RESPONSE_NONE)
            return CFR_STATUS_INVALID_ARGUMENT;
        uint64_t code = exact_hand_code(o->hand);
        for (unsigned int r = 0; r < MUS_GAME_MAX_MUSES; ++r)
            for (unsigned int p = 0; p < 4; ++p) {
                const unsigned int count = o->discard_counts[r][p];
                const bool present = r < o->mus_count ||
                    (r == o->mus_count && o->phase == MUS_FOUR_PHASE_DISCARD && p < o->player);
                if ((present && (count < 1 || count > 4)) || (!present && count != 0))
                    return CFR_STATUS_INVALID_ARGUMENT;
                code = code * 5 + count;
            }
        code = code * MUS_GAME_MAX_MUSES + o->mus_count;
        code = code * 2 + (o->phase == MUS_FOUR_PHASE_DISCARD);
        code = code * 4 + o->player;
        *key = (InfoSetKey)code;
        return CFR_STATUS_SUCCESS;
    }
    unsigned int phase, mus;
    if (phase_encode(o, &phase) != CFR_STATUS_SUCCESS ||
        public_mus_encode(o, &mus) != CFR_STATUS_SUCCESS ||
        o->scores[0] > (single_hand ? 40 : 39) ||
        o->scores[1] > (single_hand ? 40 : 39) || o->prior_wager_total >= 40)
        return CFR_STATUS_INVALID_ARGUMENT;
    pthread_once(&mask_once, initialize_masks);
    for (unsigned int p = 0; p < 2; ++p)
        if (o->used_team_wager_action_masks[p] >= 16)
            return CFR_STATUS_INVALID_ARGUMENT;
    const unsigned int mask = all_masks(o->used_team_wager_action_masks);
    if (mask_codes[mask] == 0 || mask_codes[mask] > MASK_RADIX)
        return CFR_STATUS_INVALID_ARGUMENT;
    const bool response = o->current_wager != 0;
    unsigned int last;
    const unsigned int total = numeric_total(o->used_team_wager_action_masks, &last);
    const unsigned int expected_current = o->current_wager_is_ordago ? 40 : total;
    const unsigned int expected_previous = o->current_wager_is_ordago
        ? (total != 0 ? total : 1) : (total == last ? 1 : total - last);
    if (response) {
        if (o->bettor >= 4 || o->bettor % 2 == o->player % 2 ||
            (o->eligible_mask & (1U << o->bettor)) == 0 ||
            o->current_wager != expected_current ||
            o->previous_wager != expected_previous)
            return CFR_STATUS_INVALID_ARGUMENT;
        const unsigned int responders = o->eligible_mask & (o->player % 2 ? 10U : 5U);
        const unsigned int first = next_seat(responders, o->bettor);
        if (o->pending_response == MUS_FOUR_RESPONSE_NONE) {
            if (o->player != first)
                return CFR_STATUS_INVALID_ARGUMENT;
        } else {
            if (first != (o->player + 2U) % 4 ||
                (responders & (1U << o->player)) == 0)
                return CFR_STATUS_INVALID_ARGUMENT;
            /* The first proposal must have been legal for its author under
             * the original offer. Its wager mask has not been consumed. */
            MusFourInformationSet first_observation = *o;
            first_observation.player = (uint8_t)first;
            first_observation.pending_response = MUS_FOUR_RESPONSE_NONE;
            Action actions[16];
            size_t count;
            if (observation_menu(&first_observation, actions, &count) != CFR_STATUS_SUCCESS)
                return CFR_STATUS_INVALID_ARGUMENT;
            const Action proposal = response_action(o->pending_response);
            bool legal = false;
            for (size_t i = 0; i < count; ++i)
                legal = legal || actions[i] == proposal;
            if (!legal)
                return CFR_STATUS_INVALID_ARGUMENT;
        }
    } else if (mask != 0 || o->previous_wager != 0 ||
               o->current_wager_is_ordago || o->pending_response != MUS_FOUR_RESPONSE_NONE) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    unsigned int board;
    if (board_encode(o, mask_codes[mask] - 1U, &board) != CFR_STATUS_SUCCESS)
        return CFR_STATUS_INVALID_ARGUMENT;
    uint64_t code = phase;
    code = code * PUBLIC_MUS_RADIX + mus;
    code = code * (single_hand ? SINGLE_HAND_BOARD_RADIX : BOARD_RADIX) + board;
    code = code * 4 + o->player;
    code = code * RESPONSE_STAGE_RADIX + (response ? 1U + o->pending_response : 0);
    code = code * 2 + (response ? o->bettor / 2 : 0);
    if (code >= (single_hand ? SINGLE_HAND_BET_KEY_BOUND : BET_KEY_BOUND))
        return CFR_STATUS_INVALID_ARGUMENT;
    *key = (InfoSetKey)(code + EXACT_KEY_BOUND);
    return CFR_STATUS_SUCCESS;
}

static Status information_set_decode(
    InfoSetKey key, MusFourInformationSet *information, bool single_hand) {
    if (information == NULL || key < 0)
        return CFR_STATUS_INVALID_ARGUMENT;
    MusFourInformationSet o = {0};
    if ((uint64_t)key < EXACT_KEY_BOUND) {
        uint64_t code = (uint64_t)key;
        o.player = (uint8_t)(code % 4); code /= 4;
        o.phase = code % 2 ? MUS_FOUR_PHASE_DISCARD : MUS_FOUR_PHASE_MUS; code /= 2;
        o.mus_count = (uint8_t)(code % MUS_GAME_MAX_MUSES); code /= MUS_GAME_MAX_MUSES;
        for (unsigned int r = MUS_GAME_MAX_MUSES; r != 0; --r)
            for (unsigned int p = 4; p != 0; --p) {
                o.discard_counts[r - 1][p - 1] = (uint8_t)(code % 5);
                code /= 5;
            }
        if (code >= 330)
            return CFR_STATUS_INVALID_ARGUMENT;
        o.hand_code = (uint16_t)code;
        exact_hand_decode((unsigned int)code, o.hand);
    } else {
        uint64_t code = (uint64_t)key - EXACT_KEY_BOUND;
        if (code >= (single_hand ? SINGLE_HAND_BET_KEY_BOUND : BET_KEY_BOUND))
            return CFR_STATUS_INVALID_ARGUMENT;
        const unsigned int bettor_half = (unsigned int)(code % 2); code /= 2;
        const unsigned int stage = (unsigned int)(code % RESPONSE_STAGE_RADIX);
        code /= RESPONSE_STAGE_RADIX;
        o.player = (uint8_t)(code % 4); code /= 4;
        if (stage != 0)
            o.bettor = (uint8_t)(bettor_half * 2 + 1 - o.player % 2);
        else if (bettor_half != 0)
            return CFR_STATUS_INVALID_ARGUMENT;
        pthread_once(&mask_once, initialize_masks);
        const uint64_t board_radix = single_hand ? SINGLE_HAND_BOARD_RADIX : BOARD_RADIX;
        if (board_decode((unsigned int)(code % board_radix), &o) != CFR_STATUS_SUCCESS)
            return CFR_STATUS_INVALID_ARGUMENT;
        code /= board_radix;
        public_mus_decode((unsigned int)(code % PUBLIC_MUS_RADIX), &o); code /= PUBLIC_MUS_RADIX;
        phase_decode((unsigned int)code, &o);
        if (stage != 0) {
            unsigned int last;
            const unsigned int total = numeric_total(o.used_team_wager_action_masks, &last);
            o.pending_response = (uint8_t)(stage - 1);
            o.current_wager = (uint8_t)(o.current_wager_is_ordago ? 40 : total);
            o.previous_wager = (uint8_t)(o.current_wager_is_ordago
                ? (total != 0 ? total : 1) : (total == last ? 1 : total - last));
        }
    }
    InfoSetKey encoded;
    if (information_set_encode(&o, &encoded, single_hand) != CFR_STATUS_SUCCESS ||
        encoded != key)
        return CFR_STATUS_INVALID_ARGUMENT;
    *information = o;
    return CFR_STATUS_SUCCESS;
}

Status mus_four_score_information_set_encode(
    const MusFourInformationSet *information, InfoSetKey *key) {
    return information_set_encode(information, key, false);
}

Status mus_four_information_set_encode(
    const MusFourInformationSet *information, InfoSetKey *key) {
    return information_set_encode(information, key, true);
}

Status mus_four_score_information_set_decode(InfoSetKey key, MusFourInformationSet *information) {
    return information_set_decode(key, information, false);
}

Status mus_four_information_set_decode(InfoSetKey key, MusFourInformationSet *information) {
    return information_set_decode(key, information, true);
}

static Status validate(const void *context, const GameState *raw) {
    const bool single_hand = context == &single_hand_context;
    const MusFourGameState *s = (const MusFourGameState *)raw;
    if (s == NULL ||
        s->model.phase > MUS_FOUR_PHASE_TERMINAL || s->model.current_player >= 4 ||
        s->undo_count > MUS_FOUR_UNDO_CAPACITY ||
        s->model.mus_count > MUS_GAME_MAX_MUSES)
        return CFR_STATUS_INVALID_ARGUMENT;
    const MusFourGameModel *m = &s->model;
    if ((m->used_wager_action_masks[0] & m->used_wager_action_masks[2]) != 0 ||
        (m->used_wager_action_masks[1] & m->used_wager_action_masks[3]) != 0)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (single_hand) {
        if (s->continuation != NULL || m->score_target != 0 ||
            m->initial_scores[0] != 0 || m->initial_scores[1] != 0)
            return CFR_STATUS_INVALID_ARGUMENT;
    } else if (s->continuation == NULL || !s->continuation->four_player ||
               m->score_target == 0 || m->score_target > 40 ||
               m->score_target != s->continuation->target ||
               !mus_score_table_contains(s->continuation, m->initial_scores[0], m->initial_scores[1])) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (m->pending_response > MUS_FOUR_RESPONSE_ORDAGO ||
        (!betting(m->phase) && m->pending_response != MUS_FOUR_RESPONSE_NONE))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (m->last_response_player >= 4 ||
        m->last_responses[0] > MUS_FOUR_RESPONSE_ORDAGO ||
        m->last_responses[1] > MUS_FOUR_RESPONSE_ORDAGO ||
        (m->last_responses[0] == MUS_FOUR_RESPONSE_NONE &&
         (m->last_response_player != 0 || m->last_responses[1] != MUS_FOUR_RESPONSE_NONE)) ||
        (m->phase < MUS_FOUR_PHASE_GRANDE &&
         m->last_responses[0] != MUS_FOUR_RESPONSE_NONE))
        return CFR_STATUS_INVALID_ARGUMENT;
    const unsigned int cap = single_hand ? 40 : m->score_target;
    unsigned int finished_teams = 0;
    for (unsigned int t = 0; t < 2; ++t)
        if (m->initial_scores[t] >= cap ||
            m->settled_scores[t] > cap - m->initial_scores[t] ||
            m->paid_scores[t] > m->settled_scores[t])
            return CFR_STATUS_INVALID_ARGUMENT;
        else
            finished_teams += m->initial_scores[t] + m->settled_scores[t] >= cap;
    if (!single_hand &&
        (finished_teams > 1 || (finished_teams != 0 && m->phase != MUS_FOUR_PHASE_TERMINAL)))
        return CFR_STATUS_INVALID_ARGUMENT;
    for (unsigned int c = 0; c < 8; ++c) {
        unsigned int total = m->out_of_play[c];
        for (unsigned int p = 0; p < 4; ++p)
            total += m->hands[p][c];
        if (total > capacities[c])
            return CFR_STATUS_INVALID_ARGUMENT;
    }
    for (unsigned int p = 0; p < 4; ++p) {
        const unsigned int count = hand_count(m->hands[p]);
        if (count > 4 || ((betting(m->phase) || m->phase == MUS_FOUR_PHASE_MUS ||
                          m->phase == MUS_FOUR_PHASE_TERMINAL) && count != 4))
            return CFR_STATUS_INVALID_ARGUMENT;
        if (m->phase == MUS_FOUR_PHASE_DEAL &&
            (count != (p < m->current_player ? 4U : p > m->current_player ? 0U : count) ||
             (p == m->current_player && count >= 4)))
            return CFR_STATUS_INVALID_ARGUMENT;
        if (m->phase == MUS_FOUR_PHASE_DISCARD &&
            (p < m->current_player ? count >= 4 : count != 4))
            return CFR_STATUS_INVALID_ARGUMENT;
        if (m->phase == MUS_FOUR_PHASE_REDRAW &&
            (p < m->current_player ? count != 4 : count >= 4))
            return CFR_STATUS_INVALID_ARGUMENT;
        const MusHandSummary expected = summarize(m->hands[p]);
        if (memcmp(&expected, &s->hand_summaries[p], sizeof(expected)) != 0)
            return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (m->phase == MUS_FOUR_PHASE_DEAL || m->phase == MUS_FOUR_PHASE_REDRAW) {
        if (m->deal_remaining == 0 || m->deal_remaining > 4 ||
            m->deal_remaining != 4 - hand_count(m->hands[m->current_player]) ||
            m->deal_class >= 8 || m->mus_stopped ||
            (m->phase == MUS_FOUR_PHASE_DEAL && m->mus_count != 0) ||
            (m->phase == MUS_FOUR_PHASE_REDRAW && m->mus_count == 0))
            return CFR_STATUS_INVALID_ARGUMENT;
    }
    for (unsigned int r = 0; r < MUS_GAME_MAX_MUSES; ++r)
        for (unsigned int p = 0; p < 4; ++p) {
            const bool present = r < m->mus_count ||
                (m->phase == MUS_FOUR_PHASE_DISCARD && r == m->mus_count && p < m->current_player);
            if ((present && (m->discard_counts[r][p] < 1 || m->discard_counts[r][p] > 4 ||
                             m->discard_masks[r][p] == 0 || m->discard_masks[r][p] >= 16 ||
                             bit_count(m->discard_masks[r][p]) != m->discard_counts[r][p])) ||
                (!present && (m->discard_counts[r][p] != 0 || m->discard_masks[r][p] != 0)))
                return CFR_STATUS_INVALID_ARGUMENT;
        }
    if (betting(m->phase)) {
        unsigned int eligible = 15;
        if (m->phase == MUS_FOUR_PHASE_PARES || m->phase == MUS_FOUR_PHASE_JUEGO) {
            eligible = 0;
            for (unsigned int p = 0; p < 4; ++p) {
                const bool has = m->phase == MUS_FOUR_PHASE_PARES
                    ? s->hand_summaries[p].pairs_points != 0 : s->hand_summaries[p].game_points != 0;
                if (has)
                    eligible |= 1U << p;
            }
        } else if (m->phase == MUS_FOUR_PHASE_PUNTO) {
            for (unsigned int p = 0; p < 4; ++p)
                if (s->hand_summaries[p].game_points != 0)
                    return CFR_STATUS_INVALID_ARGUMENT;
        }
        if (m->eligible_mask != eligible || (m->passed_mask & ~eligible) != 0 ||
            m->settled_scores[0] != m->paid_scores[0] || m->settled_scores[1] != m->paid_scores[1])
            return CFR_STATUS_INVALID_ARGUMENT;
        if (m->current_wager == 0) {
            if (m->pending_response != MUS_FOUR_RESPONSE_NONE ||
                m->passed_mask != (eligible & ((1U << m->current_player) - 1U)))
                return CFR_STATUS_INVALID_ARGUMENT;
        } else if (m->bettor >= 4 || (eligible & (1U << m->bettor)) == 0) {
            return CFR_STATUS_INVALID_ARGUMENT;
        }
        unsigned int prior = 0;
        for (unsigned int l = 0; l < lance_index(m->phase); ++l) {
            const unsigned int outcome = m->outcomes[l];
            if (outcome <= MUS_LANCE_UNRESOLVED || outcome > MUS_LANCE_SKIPPED)
                return CFR_STATUS_INVALID_ARGUMENT;
            if (outcome == MUS_LANCE_FIRST_BET_ACCEPTED || outcome == MUS_LANCE_SECOND_BET_ACCEPTED)
                prior += m->outcome_wagers[l];
        }
        if (prior != m->prior_wager_total)
            return CFR_STATUS_INVALID_ARGUMENT;
    }
    if (betting(m->phase) || m->phase == MUS_FOUR_PHASE_MUS ||
        m->phase == MUS_FOUR_PHASE_DISCARD) {
        MusFourInformationSet o;
        InfoSetKey key;
        if (observe(s, &o) != CFR_STATUS_SUCCESS ||
            information_set_encode(&o, &key, single_hand) != CFR_STATUS_SUCCESS)
            return CFR_STATUS_INVALID_ARGUMENT;
    }
    return CFR_STATUS_SUCCESS;
}

Status mus_four_score_game_observe(const MusFourGameState *state,
                                   MusFourInformationSet *information) {
    Status status = validate(NULL, (const GameState *)state);
    return status == CFR_STATUS_SUCCESS ? observe(state, information) : status;
}

Status mus_four_game_observe(const MusFourGameState *state,
                             MusFourInformationSet *information) {
    Status status = validate(&single_hand_context, (const GameState *)state);
    return status == CFR_STATUS_SUCCESS ? observe(state, information) : status;
}

static Status is_terminal(const void *context, const GameState *raw, bool *result) {
    (void)context;
    const MusFourGameState *s = (const MusFourGameState *)raw;
    if (s == NULL || result == NULL || s->model.phase > MUS_FOUR_PHASE_TERMINAL)
        return CFR_STATUS_INVALID_ARGUMENT;
    *result = s->model.phase == MUS_FOUR_PHASE_TERMINAL;
    return CFR_STATUS_SUCCESS;
}

static Status current_actor(const void *context, const GameState *raw, Actor *result) {
    (void)context;
    const MusFourGameState *s = (const MusFourGameState *)raw;
    if (s == NULL || result == NULL || s->model.phase >= MUS_FOUR_PHASE_TERMINAL ||
        s->model.current_player >= 4)
        return CFR_STATUS_INVALID_ARGUMENT;
    *result = (Actor){
        .kind = s->model.phase == MUS_FOUR_PHASE_DEAL ||
                s->model.phase == MUS_FOUR_PHASE_REDRAW ? CFR_ACTOR_CHANCE : CFR_ACTOR_PLAYER,
        .player = (Player)s->model.current_player
    };
    return CFR_STATUS_SUCCESS;
}

/* A chance action is a card class, 0..7. Sequential draws have the same
 * unordered hand distribution as hypergeometric class-count dealing. */
static unsigned int stock_counts(const MusFourGameModel *m, uint8_t stock[8]) {
    unsigned int total = 0;
    for (unsigned int c = 0; c < 8; ++c) {
        unsigned int occupied = m->out_of_play[c];
        for (unsigned int p = 0; p < 4; ++p)
            occupied += m->hands[p][c];
        if (occupied > capacities[c])
            return 0;
        stock[c] = (uint8_t)(capacities[c] - occupied);
        total += stock[c];
    }
    if (total == 0) {
        for (unsigned int c = 0; c < 8; ++c) {
            stock[c] = m->out_of_play[c];
            total += stock[c];
        }
    }
    return total;
}

static Status chance_outcomes(const void *context, const GameState *raw,
    Action *actions, Probability *probabilities, size_t capacity, size_t *required_count) {
    (void)context;
    const MusFourGameState *s = (const MusFourGameState *)raw;
    if (s == NULL || actions == NULL || probabilities == NULL || required_count == NULL ||
        (s->model.phase != MUS_FOUR_PHASE_DEAL && s->model.phase != MUS_FOUR_PHASE_REDRAW))
        return CFR_STATUS_INVALID_ARGUMENT;
    uint8_t stock[8];
    const unsigned int total = stock_counts(&s->model, stock);
    if (total == 0)
        return CFR_STATUS_INVALID_ARGUMENT;
    size_t count = 0;
    for (unsigned int c = 0; c < 8; ++c)
        count += stock[c] != 0;
    if (capacity < count) {
        *required_count = count;
        return CFR_STATUS_BUFFER_TOO_SMALL;
    }
    count = 0;
    for (unsigned int c = 0; c < 8; ++c)
        if (stock[c] != 0) {
            actions[count] = (Action)c;
            probabilities[count++] = (double)stock[c] / total;
        }
    *required_count = count;
    return CFR_STATUS_SUCCESS;
}

static Status legal_actions(const void *context, const GameState *raw,
    Action *actions, size_t capacity, size_t *required_count) {
    const MusFourGameState *s = (const MusFourGameState *)raw;
    if (s == NULL || actions == NULL || required_count == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (s->model.phase == MUS_FOUR_PHASE_DEAL || s->model.phase == MUS_FOUR_PHASE_REDRAW) {
        Probability probabilities[16];
        return chance_outcomes(context, raw, actions, probabilities,
                               capacity > 16 ? 16 : capacity, required_count);
    }
    MusFourInformationSet o;
    Status status = observe(s, &o);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    Action temporary[16];
    size_t count;
    status = observation_menu(&o, temporary, &count);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (capacity < count) {
        *required_count = count;
        return CFR_STATUS_BUFFER_TOO_SMALL;
    }
    memcpy(actions, temporary, count * sizeof(*actions));
    *required_count = count;
    return CFR_STATUS_SUCCESS;
}

static Status chance_probability(const void *context, const GameState *raw,
                                  Action action, Probability *result) {
    if (result == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    Action actions[8];
    Probability probabilities[8];
    size_t count;
    Status status = chance_outcomes(context, raw, actions, probabilities, 8, &count);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    for (size_t i = 0; i < count; ++i)
        if (actions[i] == action) {
            *result = probabilities[i];
            return CFR_STATUS_SUCCESS;
        }
    return CFR_STATUS_ILLEGAL_ACTION;
}

static void apply_draw(MusFourGameState *s, unsigned int card) {
    MusFourGameModel *m = &s->model;
    unsigned int in_play = 0;
    for (unsigned int c = 0; c < 8; ++c) {
        in_play += m->out_of_play[c];
        for (unsigned int p = 0; p < 4; ++p)
            in_play += m->hands[p][c];
    }
    if (in_play == 40)
        memset(m->out_of_play, 0, sizeof(m->out_of_play));
    ++m->hands[m->current_player][card];
    m->deal_class = (uint8_t)card;
    --m->deal_remaining;
    refresh_summary(s, m->current_player);
    if (m->deal_remaining != 0)
        return;
    if (m->current_player < 3) {
        ++m->current_player;
        m->deal_remaining = (uint8_t)(4 - hand_count(m->hands[m->current_player]));
        return;
    }
    m->current_player = 0;
    if (m->mus_count < MUS_GAME_MAX_MUSES)
        m->phase = MUS_FOUR_PHASE_MUS;
    else
        start_lance(s, MUS_FOUR_PHASE_GRANDE);
}

static void apply_discard(MusFourGameState *s, unsigned int mask) {
    MusFourGameModel *m = &s->model;
    unsigned int position = 0;
    const unsigned int player = m->current_player;
    for (unsigned int c = 0; c < 8; ++c) {
        const unsigned int before = m->hands[player][c];
        for (unsigned int copy = 0; copy < before; ++copy, ++position)
            if ((mask & (1U << position)) != 0) {
                --m->hands[player][c];
                ++m->out_of_play[c];
            }
    }
    m->discard_masks[m->mus_count][player] = (uint8_t)mask;
    m->discard_counts[m->mus_count][player] = (uint8_t)bit_count(mask);
    refresh_summary(s, player);
    if (player < 3) {
        ++m->current_player;
    } else {
        ++m->mus_count;
        m->phase = MUS_FOUR_PHASE_REDRAW;
        m->current_player = 0;
        m->deal_remaining = (uint8_t)(4 - hand_count(m->hands[0]));
    }
}

static void apply_bet(MusFourGameState *s, Action action) {
    MusFourGameModel *m = &s->model;
    unsigned int player = m->current_player;
    if (action == MUS_ACTION_PASS) {
        m->passed_mask |= (uint8_t)(1U << player);
        const unsigned int remaining = m->eligible_mask & ~m->passed_mask;
        if (remaining == 0)
            finish_lance(s, MUS_LANCE_ALL_PASS);
        else
            m->current_player = (uint8_t)next_seat(remaining, player);
        return;
    }
    if (m->current_wager != 0) {
        const unsigned int teammate = (player + 2) % 4;
        if (m->pending_response == MUS_FOUR_RESPONSE_NONE &&
            (m->eligible_mask & (1U << teammate)) != 0) {
            /* Both seats answer the same offer. Publish the first proposal
             * without consuming a wager action or changing the menu. */
            m->pending_response = (uint8_t)response_rank(action);
            m->current_player = (uint8_t)teammate;
            return;
        }
        if (m->pending_response != MUS_FOUR_RESPONSE_NONE) {
            m->last_response_player = (uint8_t)teammate;
            m->last_responses[0] = m->pending_response;
            m->last_responses[1] = (uint8_t)response_rank(action);
        } else {
            m->last_response_player = (uint8_t)player;
            m->last_responses[0] = (uint8_t)response_rank(action);
            m->last_responses[1] = MUS_FOUR_RESPONSE_NONE;
        }
        if (m->pending_response != MUS_FOUR_RESPONSE_NONE &&
            m->pending_response >= response_rank(action)) {
            action = response_action(m->pending_response);
            player = teammate;
        }
        m->pending_response = MUS_FOUR_RESPONSE_NONE;
    }
    if (action == MUS_ACTION_REJECT) {
        finish_lance(s, m->bettor % 2 == 0 ? MUS_LANCE_FIRST_BET_REJECTED
                                           : MUS_LANCE_SECOND_BET_REJECTED);
        return;
    }
    if (action == MUS_ACTION_ACCEPT) {
        m->current_strength = MUS_LANCE_STRENGTH_BOTH;
        finish_lance(s, m->bettor % 2 == 0 ? MUS_LANCE_FIRST_BET_ACCEPTED
                                           : MUS_LANCE_SECOND_BET_ACCEPTED);
        return;
    }
    unsigned int increment = 0, index = 0;
    for (; index < 4; ++index)
        if (action == wager_actions[index]) {
            increment = increments[index];
            break;
        }
    const bool opening = m->current_wager == 0;
    m->previous_wager = opening ? 1 : m->current_wager;
    m->current_wager_is_ordago = action == MUS_ACTION_ORDAGO;
    if (m->current_wager_is_ordago)
        m->current_wager = 40;
    else {
        m->current_wager += (uint8_t)increment;
        m->used_wager_action_masks[player] |= (uint8_t)(1U << index);
    }
    m->bettor = (uint8_t)player;
    m->current_strength = opening
        ? (player % 2 == 0 ? MUS_LANCE_STRENGTH_PLAYER_0 : MUS_LANCE_STRENGTH_PLAYER_1)
        : MUS_LANCE_STRENGTH_BOTH;
    const unsigned int opponents = m->eligible_mask & (player % 2 ? 5 : 10);
    m->current_player = (uint8_t)next_seat(opponents, player);
}

static Status apply_action(const void *context, GameState *raw, Action action) {
    MusFourGameState *s = (MusFourGameState *)raw;
    if (s == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    Action actions[16];
    size_t count;
    Status status = legal_actions(context, raw, actions, 16, &count);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    bool found = false;
    for (size_t i = 0; i < count; ++i)
        found |= actions[i] == action;
    if (!found)
        return CFR_STATUS_ILLEGAL_ACTION;
    if (s->undo_count >= MUS_FOUR_UNDO_CAPACITY)
        return CFR_STATUS_BUFFER_TOO_SMALL;
    s->undo_history[s->undo_count++] = s->model;
    MusFourGameModel *m = &s->model;
    if (m->phase == MUS_FOUR_PHASE_DEAL || m->phase == MUS_FOUR_PHASE_REDRAW) {
        apply_draw(s, (unsigned int)action);
    } else if (m->phase == MUS_FOUR_PHASE_MUS) {
        if (action == MUS_ACTION_NO_MUS) {
            m->mus_stopped = true;
            m->mus_stop_player = m->current_player;
            start_lance(s, MUS_FOUR_PHASE_GRANDE);
        } else if (m->current_player < 3) {
            ++m->current_player;
        } else {
            m->phase = MUS_FOUR_PHASE_DISCARD;
            m->current_player = 0;
        }
    } else if (m->phase == MUS_FOUR_PHASE_DISCARD) {
        apply_discard(s, MUS_ACTION_DISCARD_MASK_VALUE(action));
    } else {
        apply_bet(s, action);
    }
    return CFR_STATUS_SUCCESS;
}

static Status undo_action(const void *context, GameState *raw) {
    (void)context;
    MusFourGameState *s = (MusFourGameState *)raw;
    if (s == NULL || s->undo_count == 0 || s->undo_count > MUS_FOUR_UNDO_CAPACITY)
        return CFR_STATUS_INVALID_ARGUMENT;
    const MusFourGameModel *previous = &s->undo_history[s->undo_count - 1];
    /* An action can change only the hand of the player who took it. */
    const unsigned int player = previous->current_player;
    const bool cards_changed = memcmp(s->model.hands[player],
        previous->hands[player], sizeof(previous->hands[player])) != 0;
    s->model = *previous;
    --s->undo_count;
    if (cards_changed)
        refresh_summary(s, player);
    return CFR_STATUS_SUCCESS;
}

static Status information_set_key(const void *context, const GameState *raw,
                                   InfoSetKey *result) {
    MusFourInformationSet o;
    Status status = observe((const MusFourGameState *)raw, &o);
    return status == CFR_STATUS_SUCCESS
        ? information_set_encode(&o, result, context == &single_hand_context) : status;
}

static Status terminal_utility(const void *context, const GameState *raw,
                                Player player, Utility *result) {
    const MusFourGameState *s = (const MusFourGameState *)raw;
    if (s == NULL || result == NULL || (int)player < 0 || (int)player >= 4 ||
        s->model.phase != MUS_FOUR_PHASE_TERMINAL)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (context == &single_hand_context) {
        if (s->continuation != NULL || s->model.score_target != 0 ||
            s->model.initial_scores[0] != 0 || s->model.initial_scores[1] != 0 ||
            s->model.settled_scores[0] > 40 || s->model.settled_scores[1] > 40)
            return CFR_STATUS_INVALID_ARGUMENT;
        const double value = (double)s->model.settled_scores[0] - s->model.settled_scores[1];
        *result = (int)player % 2 == 0 ? value : -value;
        return CFR_STATUS_SUCCESS;
    }
    if (s->continuation == NULL ||
        s->model.score_target == 0 || s->model.score_target > 40 ||
        s->model.score_target != s->continuation->target)
        return CFR_STATUS_INVALID_ARGUMENT;
    const MusFourGameModel *m = &s->model;
    const unsigned int a = m->initial_scores[0] + m->settled_scores[0];
    const unsigned int b = m->initial_scores[1] + m->settled_scores[1];
    if (a > m->score_target || b > m->score_target)
        return CFR_STATUS_INVALID_ARGUMENT;
    double value;
    if (a >= m->score_target && b >= m->score_target)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (a >= m->score_target)
        value = 1;
    else if (b >= m->score_target)
        value = -1;
    else {
        if (a + b <= (unsigned int)m->initial_scores[0] + m->initial_scores[1])
            return CFR_STATUS_INVALID_ARGUMENT;
        const MusScoreCell *next = &s->continuation->cells[b][a];
        if (!next->ready)
            return CFR_STATUS_NOT_FOUND;
        if (!isfinite(next->value) || next->value < -1 || next->value > 1 ||
            next->iterations == 0 || next->evaluation_samples == 0)
            return CFR_STATUS_NUMERIC_ERROR;
        value = -next->value;
    }
    *result = (int)player % 2 == 0 ? value : -value;
    return CFR_STATUS_SUCCESS;
}

static const GameOperations trusted_operations = {
    .is_terminal = is_terminal,
    .terminal_utility = terminal_utility,
    .current_actor = current_actor,
    .legal_actions = legal_actions,
    .apply_action = apply_action,
    .undo_action = undo_action,
    .chance_probability = chance_probability,
    .information_set_key = information_set_key,
    .validate_state = validate,
    .chance_outcomes = chance_outcomes
};

static Status public_is_terminal(const void *context, const GameState *state, bool *result) {
    Status status = validate(context, state);
    return status == CFR_STATUS_SUCCESS ? is_terminal(context, state, result) : status;
}

static Status public_terminal_utility(const void *context, const GameState *state,
                                      Player player, Utility *result) {
    Status status = validate(context, state);
    return status == CFR_STATUS_SUCCESS ? terminal_utility(context, state, player, result) : status;
}

static Status public_current_actor(const void *context, const GameState *state, Actor *result) {
    Status status = validate(context, state);
    return status == CFR_STATUS_SUCCESS ? current_actor(context, state, result) : status;
}

static Status public_legal_actions(const void *context, const GameState *state,
    Action *actions, size_t capacity, size_t *required_count) {
    Status status = validate(context, state);
    return status == CFR_STATUS_SUCCESS
        ? legal_actions(context, state, actions, capacity, required_count) : status;
}

static Status public_apply_action(const void *context, GameState *state, Action action) {
    Status status = validate(context, state);
    return status == CFR_STATUS_SUCCESS ? apply_action(context, state, action) : status;
}

static Status public_undo_action(const void *context, GameState *state) {
    Status status = validate(context, state);
    return status == CFR_STATUS_SUCCESS ? undo_action(context, state) : status;
}

static Status public_chance_probability(const void *context, const GameState *state,
                                        Action action, Probability *result) {
    Status status = validate(context, state);
    return status == CFR_STATUS_SUCCESS ? chance_probability(context, state, action, result) : status;
}

static Status public_information_set_key(const void *context, const GameState *state,
                                         InfoSetKey *result) {
    Status status = validate(context, state);
    return status == CFR_STATUS_SUCCESS ? information_set_key(context, state, result) : status;
}

static Status public_chance_outcomes(const void *context, const GameState *state,
    Action *actions, Probability *probabilities, size_t capacity, size_t *required_count) {
    Status status = validate(context, state);
    return status == CFR_STATUS_SUCCESS
        ? chance_outcomes(context, state, actions, probabilities, capacity, required_count) : status;
}

static const GameOperations four_operations = {
    .is_terminal = public_is_terminal,
    .terminal_utility = public_terminal_utility,
    .current_actor = public_current_actor,
    .legal_actions = public_legal_actions,
    .apply_action = public_apply_action,
    .undo_action = public_undo_action,
    .chance_probability = public_chance_probability,
    .information_set_key = public_information_set_key,
    .validate_state = validate,
    .chance_outcomes = public_chance_outcomes
};

const Game *mus_four_game_descriptor(void) {
    static const Game game = {
        .operations = &four_operations,
        .context = &single_hand_context,
        .strategic_player_count = 4,
        .max_legal_actions = 16,
        .strategy_schema_id = MUS_FOUR_SINGLE_HAND_STRATEGY_SCHEMA_ID,
        .trusted_operations = &trusted_operations
    };
    return &game;
}

Status mus_four_game_state_init(MusFourGameState *state) {
    if (state == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    memset(state, 0, offsetof(MusFourGameState, undo_history));
    state->model.phase = MUS_FOUR_PHASE_DEAL;
    state->model.deal_remaining = 4;
    return CFR_STATUS_SUCCESS;
}

bool mus_four_score_game_matches(const Game *game) {
    game = mus_simplified_game_base(game);
    return game != NULL && game->operations == &four_operations &&
           game->trusted_operations == &trusted_operations && game->context == NULL &&
           game->strategic_player_count == 4 && game->max_legal_actions == 16;
}

Status mus_four_score_game_init(MusScoreGame *game, const MusScoreTable *table,
                                unsigned int a, unsigned int b) {
    static const Game base = {
        .operations = &four_operations,
        .strategic_player_count = 4,
        .max_legal_actions = 16,
        .trusted_operations = &trusted_operations
    };
    if (game == NULL || table == NULL || !table->four_player ||
        table->target == 0 || table->target > 40 || a >= table->target || b >= table->target)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (!mus_score_table_contains(table, a, b))
        return CFR_STATUS_INVALID_ARGUMENT;
    memset(game, 0, sizeof(*game));
    game->continuation = table;
    game->initial_scores[0] = (uint8_t)a;
    game->initial_scores[1] = (uint8_t)b;
    game->game = base;
    game->game.strategy_schema_id = game->schema;
    Status status = mus_score_game_set_schema(game);
    if (status == CFR_STATUS_SUCCESS && table->simplified)
        mus_simplified_game_wrap(&game->game, &base, game->schema);
    return status;
}

Status mus_four_score_game_state_init(MusFourGameState *state, const MusScoreGame *game) {
    if (state == NULL || game == NULL || !mus_four_score_game_matches(&game->game) ||
        game->continuation == NULL || !game->continuation->four_player ||
        game->continuation->target == 0 || game->continuation->target > 40 ||
        game->initial_scores[0] >= game->continuation->target ||
        game->initial_scores[1] >= game->continuation->target)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (!mus_score_table_contains(game->continuation, game->initial_scores[0], game->initial_scores[1]))
        return CFR_STATUS_INVALID_ARGUMENT;
    memset(state, 0, offsetof(MusFourGameState, undo_history));
    state->continuation = game->continuation;
    state->model.phase = MUS_FOUR_PHASE_DEAL;
    state->model.deal_remaining = 4;
    state->model.score_target = game->continuation->target;
    memcpy(state->model.initial_scores, game->initial_scores, sizeof(game->initial_scores));
    return CFR_STATUS_SUCCESS;
}

static const char *action_name(Action action) {
    switch (action) {
    case MUS_ACTION_PASS: return "pasar";
    case MUS_ACTION_REJECT: return "no_querer";
    case MUS_ACTION_ACCEPT: return "querer";
    case MUS_ACTION_MUS: return "mus";
    case MUS_ACTION_NO_MUS: return "no_mus";
    case MUS_ACTION_BET_TWO: return "envidar_2";
    case MUS_ACTION_BET_FOUR: return "envidar_4";
    case MUS_ACTION_BET_SEVEN: return "envidar_7";
    case MUS_ACTION_BET_TEN: return "envidar_10";
    case MUS_ACTION_ORDAGO: return "ordago";
    default: return NULL;
    }
}

typedef struct {
    FILE *stream;
    uint8_t initial_scores[2];
    bool single_hand;
    bool simplified;
} FourStrategyWriter;

static Status write_hand(FILE *stream, const MusFourInformationSet *o) {
    if (o->phase == MUS_FOUR_PHASE_MUS || o->phase == MUS_FOUR_PHASE_DISCARD) {
        for (unsigned int c = 0; c < 8; ++c)
            if (fprintf(stream, "%s%s:%u", c == 0 ? "" : ",",
                        mus_game_class_name((MusCardClass)c), o->hand[c]) < 0)
                return CFR_STATUS_IO_ERROR;
    } else if (o->phase == MUS_FOUR_PHASE_GRANDE || o->phase == MUS_FOUR_PHASE_CHICA) {
        const bool grande = o->phase == MUS_FOUR_PHASE_GRANDE;
        const unsigned int count = o->hand_code / 7;
        const unsigned int next = o->hand_code % 7 + (grande ? 0U : 1U);
        if (fprintf(stream, "%s:%u,siguiente:%s", grande ? "reyes" : "pitos", count,
                    count == 4 ? "ninguna" : mus_game_class_name((MusCardClass)next)) < 0)
            return CFR_STATUS_IO_ERROR;
    } else if (o->phase == MUS_FOUR_PHASE_PARES) {
        if (o->hand_code < 16) {
            if (fprintf(stream, "%s:%s", o->hand_code < 8 ? "pareja" : "medias",
                        mus_game_class_name((MusCardClass)(o->hand_code % 8))) < 0)
                return CFR_STATUS_IO_ERROR;
        } else {
            const unsigned int code = o->hand_code - 16U;
            unsigned int high = 7;
            while (high * (high + 1) / 2 > code)
                --high;
            const unsigned int low = code - high * (high + 1) / 2;
            if (fprintf(stream, "duples:%s,segunda:%s", mus_game_class_name((MusCardClass)high),
                        mus_game_class_name((MusCardClass)low)) < 0)
                return CFR_STATUS_IO_ERROR;
        }
    } else if (fprintf(stream, "%s:%u", o->phase == MUS_FOUR_PHASE_JUEGO ? "juego" : "punto",
                       o->phase == MUS_FOUR_PHASE_JUEGO ? game_values[o->hand_code]
                                                         : o->hand_code + 4U) < 0) {
        return CFR_STATUS_IO_ERROR;
    }
    return CFR_STATUS_SUCCESS;
}

static Status write_node(const InfoNode *node, void *context) {
    FourStrategyWriter *writer = context;
    FILE *stream = writer->stream;
    MusFourInformationSet o;
    Action actions[16];
    Probability probabilities[16];
    size_t count;
    Status status = information_set_decode(node->key, &o, writer->single_hand);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    status = writer->simplified
        ? mus_simplified_four_information_set_legal_actions(&o, !writer->single_hand, actions, 16, &count)
        : information_set_legal_actions(&o, actions, 16, &count, writer->single_hand);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (node->action_count != count)
        return CFR_STATUS_INVALID_ARGUMENT;
    status = cfr_info_node_average_strategy(node, probabilities, 16);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    static const char *phases[] = {
        "reparto", "mus", "descarte", "robo", "grande", "chica", "pares", "juego", "punto", "terminal"
    };
    if (!betting(o.phase))
        memcpy(o.scores, writer->initial_scores, sizeof(o.scores));
    if (fprintf(stream, "infoset key=%" PRId64 " jugador=%u equipo=%u fase=%s mano_codigo=%u"
                " marcador=%u-%u muses=%u elegibles=%u apostador=%u actual=%u cubierto=%u",
                node->key, o.player, o.player % 2, phases[o.phase], o.hand_code,
                o.scores[0], o.scores[1], o.mus_count, o.eligible_mask, o.bettor,
                o.current_wager, o.previous_wager) < 0)
        return CFR_STATUS_IO_ERROR;
    if (fputs(" mano=", stream) == EOF || write_hand(stream, &o) != CFR_STATUS_SUCCESS ||
        fprintf(stream, " pendientes=%u usados_pareja=%u/%u fuerza_previa=%u/%u/%u"
                " respuesta_companero=%s corte=%s", o.prior_wager_total,
                o.used_team_wager_action_masks[0], o.used_team_wager_action_masks[1],
                o.previous_strengths[0], o.previous_strengths[1], o.previous_strengths[2],
                o.pending_response == MUS_FOUR_RESPONSE_NONE ? "ninguna"
                    : action_name(response_action(o.pending_response)),
                o.mus_stopped ? "jugador" : "limite_o_pendiente") < 0)
        return CFR_STATUS_IO_ERROR;
    if ((o.mus_stopped && fprintf(stream, "%u", o.mus_stop_player) < 0) ||
        fputs(" ultima_respuesta=", stream) == EOF)
        return CFR_STATUS_IO_ERROR;
    if (o.last_responses[0] == MUS_FOUR_RESPONSE_NONE) {
        if (fputs("ninguna", stream) == EOF)
            return CFR_STATUS_IO_ERROR;
    } else {
        if (fprintf(stream, "%u:%s", o.last_response_player,
                    action_name(response_action(o.last_responses[0]))) < 0)
            return CFR_STATUS_IO_ERROR;
        if (o.last_responses[1] != MUS_FOUR_RESPONSE_NONE &&
            fprintf(stream, "/%u:%s", (o.last_response_player + 2U) % 4,
                    action_name(response_action(o.last_responses[1]))) < 0)
            return CFR_STATUS_IO_ERROR;
    }
    if (fputs(" descartes=", stream) == EOF)
        return CFR_STATUS_IO_ERROR;
    for (unsigned int r = 0; r < MUS_GAME_MAX_MUSES; ++r)
        if (fprintf(stream, "%s%u/%u/%u/%u", r == 0 ? "" : ",",
                    o.discard_counts[r][0], o.discard_counts[r][1],
                    o.discard_counts[r][2], o.discard_counts[r][3]) < 0)
            return CFR_STATUS_IO_ERROR;
    for (size_t i = 0; i < count; ++i) {
        const char *name = action_name(actions[i]);
        int written;
        if (name != NULL)
            written = fprintf(stream, " %s=%.17g", name, probabilities[i]);
        else
            written = fprintf(stream, " cambiar_mascara_%u=%.17g",
                              MUS_ACTION_DISCARD_MASK_VALUE(actions[i]), probabilities[i]);
        if (written < 0)
            return CFR_STATUS_IO_ERROR;
    }
    return fputc('\n', stream) == EOF ? CFR_STATUS_IO_ERROR : CFR_STATUS_SUCCESS;
}

static Status strategy_write_text(FILE *stream, const Trainer *trainer, bool single_hand) {
    if (stream == NULL || trainer == NULL || trainer->state == NULL || trainer->store == NULL ||
        (single_hand ? mus_simplified_game_base(trainer->game) != mus_four_game_descriptor()
                     : !mus_four_score_game_matches(trainer->game)) ||
        cfr_game_validate_state(trainer->game, trainer->state) != CFR_STATUS_SUCCESS)
        return CFR_STATUS_INVALID_ARGUMENT;
    InfoStoreStats stats;
    Status status = cfr_info_store_get_stats(trainer->store, &stats);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    const char *variant = trainer->variant == CFR_TRAINER_VARIANT_CFR ? "cfr"
        : trainer->variant == CFR_TRAINER_VARIANT_CFR_PLUS ? "cfr-plus" : "mccfr-external";
    if (fprintf(stream, "mus-strategy version=1 schema=%s variant=%s training_iterations=%zu information_sets=%zu\n",
                trainer->game->strategy_schema_id, variant, trainer->training_iterations, stats.size) < 0)
        return CFR_STATUS_IO_ERROR;
    const MusFourGameState *state = (const MusFourGameState *)trainer->state;
    FourStrategyWriter writer = {.stream = stream, .single_hand = single_hand,
                                .simplified = mus_simplified_game_matches(trainer->game)};
    memcpy(writer.initial_scores, state->model.initial_scores, sizeof(writer.initial_scores));
    status = cfr_info_store_visit_sorted(trainer->store, write_node, &writer);
    return status != CFR_STATUS_SUCCESS ? status
         : ferror(stream) ? CFR_STATUS_IO_ERROR : CFR_STATUS_SUCCESS;
}

Status mus_four_score_strategy_write_text(FILE *stream, const Trainer *trainer) {
    return strategy_write_text(stream, trainer, false);
}

Status mus_four_strategy_write_text(FILE *stream, const Trainer *trainer) {
    return strategy_write_text(stream, trainer, true);
}
