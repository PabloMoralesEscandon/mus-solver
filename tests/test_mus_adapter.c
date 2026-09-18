#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfr/checkpoint.h"
#include "mus_solver/mus_adapter.h"
#include "mus_solver/mus_score.h"
#include "mus_solver/mus_strategy.h"
#include "mus_solver/mus_simplified.h"

static int failures;
#define CHECK(x)                                                                       \
    do {                                                                               \
        if (!(x)) {                                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                    \
            ++failures;                                                                \
        }                                                                              \
    } while (0)
#define OK(x) CHECK((x) == CFR_STATUS_SUCCESS)
static const uint8_t hand[8] = {0, 0, 0, 0, 0, 0, 0, 4};
static const MusBotAction opening[] = {
    {MUS_BOT_PASS, 0},  {MUS_BOT_RAISE, 2},  {MUS_BOT_RAISE, 4}, {MUS_BOT_RAISE, 7},
    {MUS_BOT_RAISE, 8}, {MUS_BOT_RAISE, 10}, {MUS_BOT_ORDAGO, 0}};
static const MusBotAction response[] = {
    {MUS_BOT_REJECT, 0}, {MUS_BOT_ACCEPT, 0}, {MUS_BOT_RAISE, 2},  {MUS_BOT_RAISE, 4},
    {MUS_BOT_RAISE, 7},  {MUS_BOT_RAISE, 8},  {MUS_BOT_RAISE, 10}, {MUS_BOT_ORDAGO, 0}};

static MusAdapterObservation observation(void) {
    MusAdapterObservation o = {.player = CFR_PLAYER_0,
                               .phase = MUS_PHASE_GRANDE,
                               .hand_scores_complete = true,
                               .legal_actions = opening,
                               .legal_action_count =
                                   sizeof(opening) / sizeof(opening[0])};
    memcpy(o.hand, hand, sizeof(hand));
    return o;
}
static MusOrdagoInformationSet info_base(MusGamePhase phase, Player player) {
    MusOrdagoInformationSet info = {
        .player = player, .phase = phase, .mus_stopped = true};
    info.previous_strength_count =
        phase == MUS_PHASE_GRANDE                                ? 0
        : phase == MUS_PHASE_CHICA                               ? 1
        : phase == MUS_PHASE_PARES                               ? 2
        : (phase == MUS_PHASE_JUEGO || phase == MUS_PHASE_PUNTO) ? 3
                                                                 : 0;
    if (phase >= MUS_PHASE_MUS_PLAYER_0)
        info.mus_stopped = false;
    const uint8_t pitos[8] = {4};
    OK(mus_ordago_game_information_set_set_hand(
        &info, phase == MUS_PHASE_PUNTO ? pitos : hand));
    return info;
}
static MusOrdagoInformationSet offered(int total, int covered, Player player, int mask0,
                                       int mask1, int count, bool ordago) {
    MusOrdagoInformationSet info = info_base(MUS_PHASE_GRANDE, player);
    info.bet_stage = MUS_BET_RESPONSE;
    info.bettor = (Player)(1 - player);
    info.current_wager = (uint8_t)total;
    info.previous_wager = (uint8_t)covered;
    info.used_wager_action_masks[0] = (uint8_t)mask0;
    info.used_wager_action_masks[1] = (uint8_t)mask1;
    info.numeric_wager_count = (uint8_t)count;
    info.current_wager_is_ordago = ordago;
    return info;
}
static InfoSetKey put_format(InfoStore *store, MusOrdagoInformationSet info,
                             bool public_format, Action first,
                             double first_probability, Action second) {
    InfoSetKey key = -1;
    Action actions[16];
    size_t count = 0;
    OK(info.score_enabled ? mus_score_game_information_set_encode(&info, 40, &key)
        : public_format ? mus_ordago_public_game_information_set_encode(&info, &key)
                        : mus_ordago_game_information_set_encode(&info, &key));
    OK(mus_ordago_game_information_set_legal_actions(&info, actions, 16, &count));
    InfoNode *node = NULL;
    OK(cfr_info_store_get_or_create(store, key, count, &node));
    if (!node)
        return key;
    for (size_t i = 0; i < count; ++i)
        node->strategy_sums[i] = (actions[i] == first ? first_probability : 0) +
                                 (actions[i] == second ? 1 - first_probability : 0);
    return key;
}
static InfoSetKey put(InfoStore *store, MusOrdagoInformationSet info, Action first,
                      double first_probability, Action second) {
    return put_format(store, info, false, first, first_probability, second);
}
static double probability(const MusAdapterDecision *d, MusBotActionKind kind,
                          int amount) {
    for (size_t i = 0; i < d->choice_count; ++i)
        if (d->choices[i].action.kind == kind && d->choices[i].action.amount == amount)
            return d->choices[i].probability;
    return 0;
}
static void check_distribution(const MusAdapterObservation *o,
                               const MusAdapterDecision *d) {
    double sum = 0;
    bool selected = false;
    CHECK(d->lookup_count <= 64);
    CHECK(d->source_count <= 2);
    for (size_t i = 0; i < d->choice_count; ++i) {
        bool legal = false;
        for (size_t j = 0; j < o->legal_action_count; ++j)
            if (d->choices[i].action.kind == o->legal_actions[j].kind &&
                d->choices[i].action.amount == o->legal_actions[j].amount)
                legal = true;
        CHECK(legal);
        CHECK(isfinite(d->choices[i].probability) && d->choices[i].probability >= 0);
        sum += d->choices[i].probability;
        if (d->selected.kind == d->choices[i].action.kind &&
            d->selected.amount == d->choices[i].action.amount)
            selected = true;
    }
    CHECK(fabs(sum - 1) < 1e-12);
    CHECK(selected);
}
static MusAdapter *create(InfoStore *store, bool scores) {
    MusAdapter *a = NULL;
    MusAdapterConfig config = mus_adapter_default_config();
    config.score_adjustment = scores;
    OK(mus_adapter_create(store, &config, 123, &a));
    return a;
}

static void test_encoder(void) {
    /* Every exact hand and its valid lance abstractions round-trip. */
    size_t hands = 0;
    for (int a = 0; a < 8; ++a)
        for (int b = a; b < 8; ++b)
            for (int c = b; c < 8; ++c)
                for (int d = c; d < 8; ++d) {
                    uint8_t cards[8] = {0};
                    cards[a]++;
                    cards[b]++;
                    cards[c]++;
                    cards[d]++;
                    ++hands;
                    const MusGamePhase phases[] = {MUS_PHASE_MUS_PLAYER_0,
                                                   MUS_PHASE_MUS_PLAYER_1,
                                                   MUS_PHASE_DISCARD_PLAYER_0,
                                                   MUS_PHASE_DISCARD_PLAYER_1,
                                                   MUS_PHASE_GRANDE,
                                                   MUS_PHASE_CHICA,
                                                   MUS_PHASE_PARES,
                                                   MUS_PHASE_JUEGO,
                                                   MUS_PHASE_PUNTO};
                    for (size_t j = 0; j < sizeof(phases) / sizeof(phases[0]); ++j) {
                        MusOrdagoInformationSet info = {0}, decoded;
                        info.phase = phases[j];
                        info.player = j == 1 || j == 3 ? CFR_PLAYER_1 : CFR_PLAYER_0;
                        info.mus_stopped = j >= 4;
                        info.previous_strength_count = j < 5    ? 0
                                                       : j == 5 ? 1
                                                       : j == 6 ? 2
                                                                : 3;
                        if (j == 3)
                            info.discard_counts[0][0] = 2;
                        if (mus_ordago_game_information_set_set_hand(&info, cards) !=
                            CFR_STATUS_SUCCESS)
                            continue;
                        InfoSetKey key, second;
                        OK(mus_ordago_game_information_set_encode(&info, &key));
                        OK(mus_ordago_game_information_set_decode(key, &decoded));
                        OK(mus_ordago_game_information_set_encode(&decoded, &second));
                        CHECK(key == second);
                        /* Summary encoders use the observed fields, even when
                         * the original cards or inactive fields are stale. */
                        MusOrdagoInformationSet stale = info;
                        if (stale.hand_kind != MUS_ORDAGO_HAND_EXACT)
                            memset(stale.hand, 255, sizeof(stale.hand));
                        if (!stale.has_highest_non_king)
                            stale.highest_non_king = (MusCardClass)999;
                        if (!stale.has_lowest_non_pito)
                            stale.lowest_non_pito = (MusCardClass)-999;
                        if (!stale.has_second_pairs_value)
                            stale.second_pairs_value = (MusCardClass)999;
                        OK(mus_ordago_game_information_set_encode(&stale, &second));
                        CHECK(key == second);
                        OK(mus_ordago_public_game_information_set_encode(&info, &key));
                        OK(mus_ordago_public_game_information_set_encode(&stale, &second));
                        CHECK(key == second);
                        info.score_enabled = stale.score_enabled = true;
                        OK(mus_score_game_information_set_encode(&info, 40, &key));
                        OK(mus_score_game_information_set_encode(&stale, 40, &second));
                        CHECK(key == second);
                    }
                }
    CHECK(hands == 330);
    MusOrdagoInformationSet bad = offered(6, 4, CFR_PLAYER_0, 1, 1, 3, false);
    InfoSetKey key = 42;
    CHECK(mus_ordago_game_information_set_encode(&bad, &key) ==
          CFR_STATUS_INVALID_ARGUMENT);
    CHECK(key == 42);
    bad = info_base(MUS_PHASE_GRANDE, CFR_PLAYER_0);
    bad.mus_stop_player = (Player)257;
    CHECK(mus_ordago_game_information_set_encode(&bad, &key) ==
          CFR_STATUS_INVALID_ARGUMENT);
    bad.mus_stop_player = CFR_PLAYER_0;
    bad.king_count = 5;
    CHECK(mus_ordago_game_information_set_encode(&bad, &key) ==
          CFR_STATUS_INVALID_ARGUMENT);
    const MusGamePhase invalid_phases[] = {MUS_PHASE_GRANDE, MUS_PHASE_CHICA,
        MUS_PHASE_PARES, MUS_PHASE_PUNTO, MUS_PHASE_PUNTO, MUS_PHASE_JUEGO,
        MUS_PHASE_MUS_PLAYER_0};
    for (size_t i = 0; i < sizeof(invalid_phases) / sizeof(invalid_phases[0]); ++i) {
        bad = info_base(invalid_phases[i], CFR_PLAYER_0);
        switch (i) {
        case 0:
            bad.king_count = 3;
            bad.has_highest_non_king = true;
            bad.highest_non_king = MUS_CLASS_KINGS;
            break;
        case 1:
            bad.pito_count = 3;
            bad.has_lowest_non_pito = true;
            bad.lowest_non_pito = MUS_CLASS_PITOS;
            break;
        case 2:
            bad.pairs_value = MUS_CLASS_PITOS;
            bad.second_pairs_value = MUS_CLASS_KINGS;
            break;
        case 3: bad.point_value = 3; break;
        case 4: bad.point_value = 31; break;
        case 5: bad.game_value = 38; break;
        case 6: memset(bad.hand, 255, sizeof(bad.hand)); break;
        }
        key = 42;
        CHECK(mus_ordago_game_information_set_encode(&bad, &key) == CFR_STATUS_INVALID_ARGUMENT);
        CHECK(key == 42);
        CHECK(mus_ordago_public_game_information_set_encode(&bad, &key) == CFR_STATUS_INVALID_ARGUMENT);
        CHECK(key == 42);
        bad.score_enabled = true;
        CHECK(mus_score_game_information_set_encode(&bad, 40, &key) == CFR_STATUS_INVALID_ARGUMENT);
        CHECK(key == 42);
    }
}

static void test_lookup_and_translation(void) {
    InfoStore store = {0};
    OK(cfr_info_store_init(&store));
    put(&store, info_base(MUS_PHASE_GRANDE, CFR_PLAYER_0), MUS_ACTION_BET_TEN, 0.6,
        MUS_ACTION_PASS);
    put(&store, offered(2, 1, CFR_PLAYER_0, 0, 1, 1, false), MUS_ACTION_ACCEPT, 0.2,
        MUS_ACTION_BET_FOUR);
    put(&store, offered(4, 1, CFR_PLAYER_0, 0, 2, 1, false), MUS_ACTION_ACCEPT, 0.8,
        MUS_ACTION_BET_FOUR);
    put(&store, offered(10, 1, CFR_PLAYER_0, 0, 8, 1, false), MUS_ACTION_ACCEPT, 1,
        MUS_ACTION_ACCEPT);
    put(&store, offered(4, 2, CFR_PLAYER_0, 1, 1, 2, false), MUS_ACTION_ACCEPT, 0.4,
        MUS_ACTION_BET_FOUR);
    put(&store, offered(6, 2, CFR_PLAYER_0, 1, 2, 2, false), MUS_ACTION_ACCEPT, 0.3,
        MUS_ACTION_BET_FOUR);
    put(&store, offered(8, 4, CFR_PLAYER_0, 1, 3, 3, false), MUS_ACTION_ACCEPT, 0.7,
        MUS_ACTION_BET_FOUR);
    MusAdapter *a = create(&store, false);
    MusAdapterObservation o = observation();
    MusAdapterDecision decision;
    OK(mus_adapter_decide(a, &o, &decision));
    CHECK(decision.method == MUS_ADAPTER_EXACT);
    CHECK(fabs(probability(&decision, MUS_BOT_RAISE, 10) - 0.6) < 1e-12);
    check_distribution(&o, &decision);
    o.match_scores[0] = o.match_scores[1] = 32;
    OK(mus_adapter_decide(a, &o, &decision));
    CHECK(fabs(probability(&decision, MUS_BOT_RAISE, 8) - 0.6) < 1e-12);
    CHECK(probability(&decision, MUS_BOT_RAISE, 10) == 0);
    o.match_scores[0] = o.match_scores[1] = 39;
    OK(mus_adapter_decide(a, &o, &decision));
    CHECK(probability(&decision, MUS_BOT_PASS, 0) == 1);
    o.match_scores[0] = o.match_scores[1] = 0;
    MusBotEvent events[100];
    events[0] = (MusBotEvent){CFR_PLAYER_0, {MUS_BOT_PASS, 0}};
    events[1] = (MusBotEvent){CFR_PLAYER_1, {MUS_BOT_RAISE, 3}};
    o.history = events;
    o.history_count = 2;
    o.legal_actions = response;
    o.legal_action_count = sizeof(response) / sizeof(response[0]);
    OK(mus_adapter_decide(a, &o, &decision));
    CHECK(decision.method == MUS_ADAPTER_INTERPOLATED);
    CHECK(decision.source_count == 2);
    CHECK(decision.sources[0].weight == 0.5 && decision.sources[1].weight == 0.5);
    CHECK(fabs(probability(&decision, MUS_BOT_ACCEPT, 0) - 0.5) < 1e-12);
    CHECK(fabs(probability(&decision, MUS_BOT_RAISE, 4) - 0.5) < 1e-12);
    check_distribution(&o, &decision);
    events[1].action.amount = 2;
    OK(mus_adapter_decide(a, &o, &decision));
    CHECK(decision.method == MUS_ADAPTER_EXACT);
    CHECK(fabs(probability(&decision, MUS_BOT_ACCEPT, 0) - 0.2) < 1e-12);
    events[1].action.amount = 1000;
    OK(mus_adapter_decide(a, &o, &decision));
    CHECK(decision.method == MUS_ADAPTER_INTERPOLATED && decision.source_count == 1);
    CHECK(decision.real_total == 1000);
    CHECK(probability(&decision, MUS_BOT_ACCEPT, 0) == 1);
    events[1].action.amount = 2;
    events[2] = (MusBotEvent){CFR_PLAYER_0, {MUS_BOT_RAISE, 2}};
    events[3] = (MusBotEvent){CFR_PLAYER_1, {MUS_BOT_RAISE, 2}};
    o.history_count = 4;
    OK(mus_adapter_decide(a, &o, &decision));
    CHECK(decision.method == MUS_ADAPTER_NEAREST);
    CHECK(decision.real_total == 6 && decision.real_covered == 4);
    CHECK(decision.source_count == 2);
    CHECK(fabs(probability(&decision, MUS_BOT_ACCEPT, 0) - 0.5) < 1e-12);
    CHECK(probability(&decision, MUS_BOT_RAISE, 4) > 0);
    check_distribution(&o, &decision);
    for (size_t i = 4; i < 100; ++i)
        events[i] = (MusBotEvent){(Player)(i % 2), {MUS_BOT_RAISE, 2}};
    for (size_t count = 6; count <= 100; count += 2) {
        o.history_count = count;
        OK(mus_adapter_decide(a, &o, &decision));
        CHECK(decision.real_total == (int64_t)(2 * (count - 1)));
        CHECK(decision.real_covered == decision.real_total - 2);
        check_distribution(&o, &decision);
    }
    o.history_count = 4;
    MusAdapterDecision before = decision;
    events[3].player = CFR_PLAYER_0;
    CHECK(mus_adapter_decide(a, &o, &decision) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(&before, &decision, sizeof(before)) == 0);
    mus_adapter_destroy(a);
    OK(cfr_info_store_destroy(&store));
}

static void test_score_and_fallback(void) {
    InfoStore store = {0};
    OK(cfr_info_store_init(&store));
    put(&store, offered(40, 1, CFR_PLAYER_0, 0, 0, 0, true), MUS_ACTION_ACCEPT, 0.5,
        MUS_ACTION_REJECT);
    put(&store, info_base(MUS_PHASE_GRANDE, CFR_PLAYER_0), MUS_ACTION_ORDAGO, 0.5,
        MUS_ACTION_PASS);
    MusAdapter *a = create(&store, true), *b = create(&store, true);
    MusAdapterObservation o = observation();
    MusAdapterDecision d, e;
    for (int i = 0; i < 100; ++i) {
        OK(mus_adapter_decide(a, &o, &d));
        OK(mus_adapter_decide(b, &o, &e));
        CHECK(d.selected.kind == e.selected.kind &&
              d.selected.amount == e.selected.amount);
    }
    MusBotEvent events[] = {{CFR_PLAYER_0, {MUS_BOT_PASS, 0}},
                            {CFR_PLAYER_1, {MUS_BOT_ORDAGO, 0}}};
    MusBotAction legal[] = {{MUS_BOT_REJECT, 0}, {MUS_BOT_ACCEPT, 0}};
    o.history = events;
    o.history_count = 2;
    o.legal_actions = legal;
    o.legal_action_count = 2;
    o.match_scores[0] = 38;
    o.match_scores[1] = 10;
    OK(mus_adapter_decide(a, &o, &d));
    CHECK(d.ordago_factor == 0.5);
    CHECK(fabs(probability(&d, MUS_BOT_ACCEPT, 0) - 1.0 / 3) < 1e-12);
    o.match_scores[0] = 10;
    o.match_scores[1] = 38;
    OK(mus_adapter_decide(a, &o, &d));
    CHECK(d.ordago_factor == 2);
    CHECK(fabs(probability(&d, MUS_BOT_ACCEPT, 0) - 2.0 / 3) < 1e-12);
    check_distribution(&o, &d);
    o = observation();
    o.phase = MUS_PHASE_CHICA;
    o.previous_strength_count = 1;
    o.hand_scores_complete = false;
    OK(mus_adapter_decide(a, &o, &d));
    CHECK(d.method == MUS_ADAPTER_FALLBACK && d.score_proxy);
    CHECK(d.selected.kind == MUS_BOT_PASS);
    CHECK(d.lookup_count == 64);
    o.phase = MUS_PHASE_MUS_PLAYER_0;
    o.previous_strength_count = 0;
    MusBotAction mus[] = {{MUS_BOT_MUS, 0}, {MUS_BOT_NO_MUS, 0}};
    o.legal_actions = mus;
    o.legal_action_count = 2;
    OK(mus_adapter_decide(a, &o, &d));
    CHECK(d.method == MUS_ADAPTER_FALLBACK && d.selected.kind == MUS_BOT_NO_MUS);
    o.mus_count = 1;
    o.discard_counts[0][0] = 2;
    o.discard_counts[0][1] = 3;
    OK(mus_adapter_decide(a, &o, &d));
    CHECK(d.method == MUS_ADAPTER_MUS_LIMIT && d.lookup_count == 0);
    o.phase = MUS_PHASE_MUS_PLAYER_1;
    o.player = CFR_PLAYER_1;
    OK(mus_adapter_decide(a, &o, &d));
    CHECK(d.method == MUS_ADAPTER_MUS_LIMIT);
    o.mus_count = 0;
    o.phase = MUS_PHASE_DISCARD_PLAYER_1;
    MusBotAction discards[] = {
        {MUS_BOT_DISCARD, 15}, {MUS_BOT_DISCARD, 3}, {MUS_BOT_DISCARD, 1}};
    o.legal_actions = discards;
    o.legal_action_count = 3;
    o.discard_counts[0][0] = 2;
    OK(mus_adapter_decide(a, &o, &d));
    CHECK(d.method == MUS_ADAPTER_FALLBACK && d.selected.amount == 1);
    o.legal_actions = legal;
    o.legal_action_count = 2;
    CHECK(mus_adapter_decide(a, &o, &d) == CFR_STATUS_INVALID_ARGUMENT);
    mus_adapter_destroy(a);
    mus_adapter_destroy(b);
    OK(cfr_info_store_destroy(&store));
}

static void test_missing_corrupt_and_legal_gaps(void) {
    InfoStore store = {0};
    OK(cfr_info_store_init(&store));
    MusOrdagoInformationSet info = info_base(MUS_PHASE_GRANDE, CFR_PLAYER_0);
    InfoSetKey key = put(&store, info, MUS_ACTION_BET_FOUR, 0.5, MUS_ACTION_BET_TEN);
    MusAdapter *a = create(&store, false);
    MusAdapterObservation o = observation();
    MusBotAction gaps[] = {{MUS_BOT_PASS, 0}, {MUS_BOT_RAISE, 3}, {MUS_BOT_RAISE, 5}};
    o.legal_actions = gaps;
    o.legal_action_count = 3;
    MusAdapterDecision d;
    OK(mus_adapter_decide(a, &o, &d));
    CHECK(probability(&d, MUS_BOT_RAISE, 3) == 0.5);
    CHECK(probability(&d, MUS_BOT_RAISE, 5) == 0.5);
    InfoNode *node = NULL;
    OK(cfr_info_store_find(&store, key, &node));
    if (node) {
        for (size_t i = 0; i < node->action_count; ++i)
            node->strategy_sums[i] = 0;
        OK(mus_adapter_decide(a, &o, &d));
        CHECK(d.method == MUS_ADAPTER_FALLBACK && d.selected.kind == MUS_BOT_PASS);
        node->strategy_sums[0] = NAN;
        CHECK(mus_adapter_decide(a, &o, &d) == CFR_STATUS_NUMERIC_ERROR);
        node->strategy_sums[0] = 0;
    }
    mus_adapter_destroy(a);
    OK(cfr_info_store_destroy(&store));
}

static void test_all_phases_and_boundaries(void) {
    InfoStore store = {0};
    OK(cfr_info_store_init(&store));
    const MusGamePhase phases[] = {MUS_PHASE_GRANDE, MUS_PHASE_CHICA, MUS_PHASE_PARES,
                                   MUS_PHASE_JUEGO, MUS_PHASE_PUNTO};
    for (int player = 0; player < 2; ++player) {
        for (size_t i = 0; i < 5; ++i) {
            MusOrdagoInformationSet info = info_base(phases[i], (Player)player);
            if (i == 4) {
                uint8_t pitos[8] = {4};
                OK(mus_ordago_game_information_set_set_hand(&info, pitos));
            }
            info.bet_stage = player ? MUS_BET_SECOND_AFTER_PASS : MUS_BET_FIRST_TO_ACT;
            info.mus_count = 1;
            info.mus_stopped = false;
            info.discard_counts[0][0] = 2;
            info.discard_counts[0][1] = 3;
            put(&store, info, MUS_ACTION_PASS, 0.6, MUS_ACTION_ORDAGO);
            MusAdapterObservation o = observation();
            o.phase = phases[i];
            o.player = (Player)player;
            o.mus_count = 1;
            memcpy(o.discard_counts, info.discard_counts, sizeof(o.discard_counts));
            o.previous_strength_count = info.previous_strength_count;
            if (i == 4) {
                memset(o.hand, 0, sizeof(o.hand));
                o.hand[0] = 4;
            }
            MusBotEvent pass = {CFR_PLAYER_0, {MUS_BOT_PASS, 0}};
            if (player) {
                o.history = &pass;
                o.history_count = 1;
            }
            MusAdapter *a = create(&store, false);
            MusAdapterDecision d;
            OK(mus_adapter_decide(a, &o, &d));
            CHECK(d.method == MUS_ADAPTER_EXACT);
            CHECK(fabs(probability(&d, MUS_BOT_PASS, 0) - 0.6) < 1e-12);
            check_distribution(&o, &d);
            mus_adapter_destroy(a);
        }
    }
    MusOrdagoInformationSet info = info_base(MUS_PHASE_PARES, CFR_PLAYER_0);
    CHECK(info.has_second_pairs_value);
    uint8_t pair[8] = {1, 1, 0, 0, 0, 0, 0, 2};
    OK(mus_ordago_game_information_set_set_hand(&info, pair));
    CHECK(!info.has_second_pairs_value);
    InfoSetKey key;
    OK(mus_ordago_game_information_set_encode(&info, &key));
    for (int player = 0; player < 2; ++player) {
        MusAdapterObservation o = observation();
        o.player = (Player)player;
        o.phase = player ? MUS_PHASE_MUS_PLAYER_1 : MUS_PHASE_MUS_PLAYER_0;
        info = info_base(o.phase, (Player)player);
        put(&store, info, MUS_ACTION_MUS, 0.75, MUS_ACTION_NO_MUS);
        MusBotAction legal[] = {{MUS_BOT_MUS, 0}, {MUS_BOT_NO_MUS, 0}};
        o.legal_actions = legal;
        o.legal_action_count = 2;
        MusAdapter *a = create(&store, false);
        MusAdapterDecision d;
        OK(mus_adapter_decide(a, &o, &d));
        CHECK(d.method == MUS_ADAPTER_EXACT);
        CHECK(probability(&d, MUS_BOT_MUS, 0) == 0.75);
        mus_adapter_destroy(a);
    }
    MusAdapter *a = create(&store, false);
    MusAdapterObservation o = observation();
    MusBotEvent overflow[] = {{CFR_PLAYER_0, {MUS_BOT_RAISE, MUS_ADAPTER_MAX_AMOUNT}},
                              {CFR_PLAYER_1, {MUS_BOT_RAISE, 1}}};
    o.history = overflow;
    o.history_count = 2;
    o.legal_actions = response;
    o.legal_action_count = sizeof(response) / sizeof(response[0]);
    MusAdapterDecision d;
    CHECK(mus_adapter_decide(a, &o, &d) == CFR_STATUS_INVALID_ARGUMENT);
    overflow[0].action.amount = 2;
    overflow[1].action = (MusBotAction){MUS_BOT_ACCEPT, 0};
    CHECK(mus_adapter_decide(a, &o, &d) == CFR_STATUS_INVALID_ARGUMENT);
    mus_adapter_destroy(a);
    OK(cfr_info_store_destroy(&store));
}

static void write_fixture(const char *path) {
    InfoStore store = {0};
    OK(cfr_info_store_init(&store));
    put(&store, info_base(MUS_PHASE_GRANDE, CFR_PLAYER_0), MUS_ACTION_BET_TEN, 0.6,
        MUS_ACTION_PASS);
    put(&store, offered(2, 1, CFR_PLAYER_0, 0, 1, 1, false), MUS_ACTION_ACCEPT, 0.2,
        MUS_ACTION_BET_FOUR);
    put(&store, offered(4, 1, CFR_PLAYER_0, 0, 2, 1, false), MUS_ACTION_ACCEPT, 0.8,
        MUS_ACTION_BET_FOUR);
    put(&store, offered(4, 2, CFR_PLAYER_0, 1, 1, 2, false), MUS_ACTION_ACCEPT, 0.4,
        MUS_ACTION_BET_FOUR);
    put(&store, offered(6, 2, CFR_PLAYER_0, 1, 2, 2, false), MUS_ACTION_ACCEPT, 0.3,
        MUS_ACTION_BET_FOUR);
    put(&store, offered(8, 4, CFR_PLAYER_0, 1, 3, 3, false), MUS_ACTION_ACCEPT, 0.7,
        MUS_ACTION_BET_FOUR);
    /* A large, variable-width export exercises seeks into partial rows. */
    for (int a = 0; a < 8; ++a)
        for (int b = a; b < 8; ++b)
            for (int c = b; c < 8; ++c)
                for (int d = c; d < 8; ++d) {
                    uint8_t cards[8] = {0};
                    cards[a]++;
                    cards[b]++;
                    cards[c]++;
                    cards[d]++;
                    for (int p = 0; p < 2; ++p) {
                        MusOrdagoInformationSet info = info_base(
                            p ? MUS_PHASE_MUS_PLAYER_1 : MUS_PHASE_MUS_PLAYER_0,
                            (Player)p);
                        OK(mus_ordago_game_information_set_set_hand(&info, cards));
                        put(&store, info, MUS_ACTION_MUS, 0.75, MUS_ACTION_NO_MUS);
                    }
                    MusOrdagoInformationSet discard =
                        info_base(MUS_PHASE_DISCARD_PLAYER_0, CFR_PLAYER_0);
                    OK(mus_ordago_game_information_set_set_hand(&discard, cards));
                    put(&store, discard, MUS_ACTION_DISCARD_MASK(1), 0.25,
                        MUS_ACTION_DISCARD_MASK(15));
                }
    MusOrdagoGameState state;
    Trainer trainer;
    OK(mus_ordago_game_state_init(&state));
    OK(cfr_trainer_init_mccfr(&trainer, mus_ordago_game_descriptor(),
                              mus_ordago_game_state_as_game_state(&state), &store, 0));
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file) {
        OK(cfr_checkpoint_write(file, &trainer));
        CHECK(fclose(file) == 0);
    }
    char *text_path = malloc(strlen(path) + 5);
    CHECK(text_path != NULL);
    if (text_path) {
        sprintf(text_path, "%s.txt", path);
        file = fopen(text_path, "w");
        CHECK(file != NULL);
        if (file) {
            OK(mus_ordago_strategy_write_text(file, &trainer));
            CHECK(fclose(file) == 0);
        }
        free(text_path);
    }
    OK(cfr_info_store_destroy(&store));
}

static MusOrdagoInformationSet score_info(MusOrdagoInformationSet info, int a, int b) {
    info.score_enabled = true;
    info.scores[0] = (uint8_t)a;
    info.scores[1] = (uint8_t)b;
    info.max_settled_score = (uint8_t)(a > b ? a : b);
    return info;
}

static void write_score_fixture(const char *directory) {
    MusScoreTable table;
    OK(mus_score_table_init(&table, 40));
    for (int a = 0; a < 40; ++a)
        for (int b = 0; b < 40; ++b)
            if (a + b >= 60)
                table.cells[a][b] = (MusScoreCell){.ready = true, .value = 0,
                    .iterations = 50000000, .evaluation_samples = 1000, .training_seed = 7};
    char path[4096];
    snprintf(path, sizeof(path), "%s/values.txt", directory);
    FILE *file = fopen(path, "w");
    CHECK(file != NULL);
    if (file == NULL)
        return;
    OK(mus_score_table_write(file, &table));
    CHECK(fclose(file) == 0);
    for (int a = 0; a < 40; ++a) {
        for (int b = 0; b < 40; ++b) {
            if (!table.cells[a][b].ready)
                continue;
            MusScoreGame game;
            MusGameState state;
            Trainer trainer;
            InfoStore store = {0};
            OK(mus_score_game_init(&game, &table, (unsigned int)a, (unsigned int)b));
            OK(mus_score_game_state_init(&state, &game));
            OK(cfr_info_store_init(&store));
            for (int p = 0; p < 2; ++p) {
                double aggressive = (p == 0 ? a >= b : b >= a) ? 0.25 : 0.75;
                const MusGamePhase phases[] = {MUS_PHASE_GRANDE, MUS_PHASE_CHICA,
                    MUS_PHASE_PARES, MUS_PHASE_JUEGO, MUS_PHASE_PUNTO};
                for (size_t phase = 0; phase < sizeof(phases) / sizeof(phases[0]); ++phase) {
                    MusOrdagoInformationSet info = score_info(info_base(phases[phase], (Player)p), a, b);
                    info.bet_stage = p == 0 ? MUS_BET_FIRST_TO_ACT : MUS_BET_SECOND_AFTER_PASS;
                    put(&store, info, MUS_ACTION_ORDAGO, aggressive, MUS_ACTION_PASS);
                    if (phases[phase] == MUS_PHASE_CHICA && a == 30 && b == 30 && p == 0) {
                        info.previous_strengths[0] = MUS_LANCE_STRENGTH_BOTH;
                        info.prior_wager_total = 4;
                        put(&store, info, MUS_ACTION_ORDAGO, 0.2, MUS_ACTION_PASS);
                        info.prior_wager_total = 0;
                        info.scores[0] = info.max_settled_score = 34;
                        put(&store, info, MUS_ACTION_ORDAGO, 0.7, MUS_ACTION_PASS);
                    }
                }
                MusOrdagoInformationSet mus = score_info(info_base(
                    p == 0 ? MUS_PHASE_MUS_PLAYER_0 : MUS_PHASE_MUS_PLAYER_1, (Player)p), a, b);
                put(&store, mus, MUS_ACTION_MUS, aggressive, MUS_ACTION_NO_MUS);
                MusOrdagoInformationSet discard = score_info(info_base(
                    p == 0 ? MUS_PHASE_DISCARD_PLAYER_0 : MUS_PHASE_DISCARD_PLAYER_1, (Player)p), a, b);
                if (p == 1)
                    discard.discard_counts[0][0] = 2;
                put(&store, discard, MUS_ACTION_DISCARD_MASK(1), aggressive,
                    MUS_ACTION_DISCARD_MASK(15));
                MusOrdagoInformationSet offered_info = score_info(
                    offered(40, 1, (Player)p, 0, 0, 0, true), a, b);
                put(&store, offered_info, MUS_ACTION_ACCEPT, aggressive, MUS_ACTION_REJECT);
            }
            if (a == 30 && b == 30) {
                const MusOrdagoInformationSet bets[] = {
                    offered(2, 1, CFR_PLAYER_0, 0, 1, 1, false),
                    offered(4, 1, CFR_PLAYER_0, 0, 2, 1, false),
                    offered(4, 2, CFR_PLAYER_0, 1, 1, 2, false),
                    offered(6, 2, CFR_PLAYER_0, 1, 2, 2, false)};
                const double weights[] = {0.2, 0.8, 0.4, 0.3};
                for (size_t i = 0; i < 4; ++i)
                    put(&store, score_info(bets[i], a, b), MUS_ACTION_ACCEPT, weights[i],
                        MUS_ACTION_ORDAGO);
            }
            if (a == 35 && b == 35) {
                MusOrdagoInformationSet info = score_info(
                    info_base(MUS_PHASE_GRANDE, CFR_PLAYER_0), a, b);
                info.mus_stop_player = CFR_PLAYER_1;
                put(&store, info, MUS_ACTION_BET_TWO, 0.6, MUS_ACTION_PASS);
            }
            OK(cfr_trainer_init_mccfr(&trainer, &game.game,
                mus_game_state_as_game_state(&state), &store, 7));
            trainer.training_iterations = 50000000;
            snprintf(path, sizeof(path), "%s/score-%02d-%02d.txt", directory, a, b);
            file = fopen(path, "w");
            CHECK(file != NULL);
            if (file != NULL) {
                OK(mus_score_strategy_write_text(file, &trainer));
                CHECK(fclose(file) == 0);
            }
            /* The same policies as a checkpoint, for the compact converter. */
            snprintf(path, sizeof(path), "%s/score-%02d-%02d.cfr.zst", directory, a, b);
            file = fopen(path, "wb");
            CHECK(file != NULL);
            if (file != NULL) {
                OK(cfr_checkpoint_write_zstd(file, &trainer));
                CHECK(fclose(file) == 0);
            }
            OK(cfr_info_store_destroy(&store));
        }
    }
}

static MusOrdagoInformationSet public_info(MusGamePhase phase, Player player) {
    MusOrdagoInformationSet info = info_base(phase, player);
    if (phase >= MUS_PHASE_GRANDE && phase <= MUS_PHASE_PUNTO) {
        info.bet_stage = player == CFR_PLAYER_0 ? MUS_BET_FIRST_TO_ACT : MUS_BET_SECOND_AFTER_PASS;
        if (info.previous_strength_count) {
            info.prior_wager_total = 2;
            info.previous_strengths[0] = MUS_LANCE_STRENGTH_BOTH;
        }
        if (info.previous_strength_count >= 2) {
            info.scores[0] = 1;
            info.previous_strengths[1] = MUS_LANCE_STRENGTH_PLAYER_0;
        }
        if (info.previous_strength_count == 3) {
            info.scores[1] = 2;
            info.previous_strengths[2] = MUS_LANCE_STRENGTH_BOTH;
        }
        info.max_settled_score = info.scores[0] > info.scores[1] ? info.scores[0] : info.scores[1];
    }
    if (phase == MUS_PHASE_DISCARD_PLAYER_1) info.discard_counts[0][0] = 2;
    return info;
}

static void populate_public_fixture(InfoStore *store) {
    for (int p = 0; p < 2; ++p) {
        for (int phase = MUS_PHASE_GRANDE; phase <= MUS_PHASE_PUNTO; ++phase)
            put_format(store, public_info((MusGamePhase)phase, (Player)p), true,
                       MUS_ACTION_ORDAGO, 0.3, MUS_ACTION_PASS);
        put_format(store, public_info(p ? MUS_PHASE_MUS_PLAYER_1 : MUS_PHASE_MUS_PLAYER_0,
                                     (Player)p), true, MUS_ACTION_MUS, 0.75, MUS_ACTION_NO_MUS);
        put_format(store, public_info(p ? MUS_PHASE_DISCARD_PLAYER_1 : MUS_PHASE_DISCARD_PLAYER_0,
                                     (Player)p), true, MUS_ACTION_DISCARD_MASK(1), 0.25,
                   MUS_ACTION_DISCARD_MASK(15));
    }
    const MusOrdagoInformationSet offers[] = {
        offered(2, 1, CFR_PLAYER_0, 0, 1, 1, false),
        offered(4, 1, CFR_PLAYER_0, 0, 2, 1, false),
        offered(4, 2, CFR_PLAYER_0, 1, 1, 2, false),
        offered(6, 2, CFR_PLAYER_0, 1, 2, 2, false),
        offered(40, 1, CFR_PLAYER_0, 0, 0, 0, true)};
    const double mass[] = {0.2, 0.8, 0.4, 0.3, 0.6};
    for (size_t i = 0; i < sizeof(offers) / sizeof(*offers); ++i)
        put_format(store, offers[i], true, MUS_ACTION_ACCEPT, mass[i], MUS_ACTION_REJECT);
}

static void test_public_adapter(void) {
    InfoStore store = {0}; OK(cfr_info_store_init(&store)); populate_public_fixture(&store);
    MusAdapterConfig config = mus_adapter_default_config(); config.score_adjustment = false;
    MusAdapter *adapter = NULL, *reference = NULL;
    OK(mus_adapter_create_public(&store, &config, 7, &adapter));
    OK(mus_adapter_create_public(&store, &config, 7, &reference));
    MusBotEvent pass = {CFR_PLAYER_0, {MUS_BOT_PASS, 0}};
    const MusBotAction mus[] = {{MUS_BOT_MUS,0}, {MUS_BOT_NO_MUS,0}};
    const MusBotAction discard[] = {{MUS_BOT_DISCARD,1}, {MUS_BOT_DISCARD,15}};
    MusAdapterDecision d, r;
    for (int p = 0; p < 2; ++p) {
        const MusGamePhase phases[] = {MUS_PHASE_GRANDE, MUS_PHASE_CHICA, MUS_PHASE_PARES,
            MUS_PHASE_JUEGO, MUS_PHASE_PUNTO, p ? MUS_PHASE_MUS_PLAYER_1 : MUS_PHASE_MUS_PLAYER_0,
            p ? MUS_PHASE_DISCARD_PLAYER_1 : MUS_PHASE_DISCARD_PLAYER_0};
        for (size_t i = 0; i < sizeof(phases) / sizeof(*phases); ++i) {
            MusOrdagoInformationSet info = public_info(phases[i], (Player)p);
            MusAdapterObservation o = observation();
            o.player = (Player)p; o.phase = phases[i]; o.hand_scores_complete = false;
            if (o.phase == MUS_PHASE_PUNTO) { memset(o.hand, 0, sizeof(o.hand)); o.hand[0] = 4; }
            o.has_hand_start_scores = o.has_pending_wager_total = true;
            o.hand_start_scores[0] = 10; o.hand_start_scores[1] = 12;
            o.match_scores[0] = 10 + info.scores[0]; o.match_scores[1] = 12 + info.scores[1];
            o.pending_wager_total = info.prior_wager_total;
            o.previous_strength_count = info.previous_strength_count;
            memcpy(o.previous_strengths, info.previous_strengths, sizeof(o.previous_strengths));
            memcpy(o.discard_counts, info.discard_counts, sizeof(o.discard_counts));
            if (i < 5 && p) { o.history = &pass; o.history_count = 1; }
            if (i >= 5) { o.legal_actions = i == 5 ? mus : discard; o.legal_action_count = 2; }
            OK(mus_adapter_decide(adapter, &o, &d)); OK(mus_adapter_decide(reference, &o, &r));
            CHECK(d.method == MUS_ADAPTER_EXACT && d.public_strategy && !d.score_strategy && !d.score_proxy);
            CHECK(d.selected.kind == r.selected.kind && d.selected.amount == r.selected.amount);
            InfoSetKey expected; OK(mus_ordago_public_game_information_set_encode(&info, &expected));
            CHECK(d.sources[0].key == expected && d.source_count == 1);
            const MusBotActionKind action = i < 5 ? MUS_BOT_ORDAGO : i == 5 ? MUS_BOT_MUS : MUS_BOT_DISCARD;
            CHECK(fabs(probability(&d, action, i == 6 ? 1 : 0) - (i < 5 ? 0.3 : i == 5 ? 0.75 : 0.25)) < 1e-12);
            check_distribution(&o, &d);
            MusAdapterDecision before = d;
            o.has_hand_start_scores = false;
            CHECK(mus_adapter_decide(adapter, &o, &d) == CFR_STATUS_INVALID_ARGUMENT);
            CHECK(memcmp(&before, &d, sizeof(d)) == 0);
        }
    }
    MusAdapterObservation o = observation();
    o.has_hand_start_scores = o.has_pending_wager_total = true;
    MusBotEvent events[] = {{CFR_PLAYER_0, {MUS_BOT_PASS,0}}, {CFR_PLAYER_1, {MUS_BOT_RAISE,3}},
                           {CFR_PLAYER_0, {MUS_BOT_RAISE,2}}, {CFR_PLAYER_1, {MUS_BOT_RAISE,2}}};
    o.history = events; o.history_count = 2;
    o.legal_actions = response; o.legal_action_count = sizeof(response) / sizeof(*response);
    OK(mus_adapter_decide(adapter, &o, &d));
    CHECK(d.method == MUS_ADAPTER_INTERPOLATED && d.source_count == 2);
    CHECK(fabs(probability(&d, MUS_BOT_ACCEPT, 0) - 0.5) < 1e-12);
    events[1].action.amount = 2; o.history_count = 4;
    OK(mus_adapter_decide(adapter, &o, &d));
    CHECK(d.method == MUS_ADAPTER_NEAREST && d.source_count != 0);
    /* A missing decision must not borrow a different payment or pending wager. */
    o = observation(); o.phase = MUS_PHASE_PARES; o.previous_strength_count = 2;
    o.previous_strengths[0] = MUS_LANCE_STRENGTH_BOTH;
    o.previous_strengths[1] = MUS_LANCE_STRENGTH_PLAYER_0;
    o.has_hand_start_scores = o.has_pending_wager_total = true;
    o.match_scores[1] = 1; o.pending_wager_total = 2; /* Fixture paid 1:0 instead. */
    OK(mus_adapter_decide(adapter, &o, &d));
    CHECK(d.method == MUS_ADAPTER_FALLBACK && d.source_count == 0 && !d.score_proxy);
    o.match_scores[0] = 1; o.match_scores[1] = 0; o.pending_wager_total = 3;
    OK(mus_adapter_decide(adapter, &o, &d));
    CHECK(d.method == MUS_ADAPTER_FALLBACK && d.source_count == 0);
    o.pending_wager_total = 2;
    OK(mus_adapter_decide(adapter, &o, &d)); CHECK(d.method == MUS_ADAPTER_EXACT);
    o.has_pending_wager_total = false;
    CHECK(mus_adapter_decide(adapter, &o, &d) == CFR_STATUS_INVALID_ARGUMENT);
    mus_adapter_destroy(reference); mus_adapter_destroy(adapter); OK(cfr_info_store_destroy(&store));
}

static void test_simplified_adapter(void) {
    InfoStore store = {0}; OK(cfr_info_store_init(&store));
    MusAdapter *adapter = NULL;
    MusAdapterConfig config = mus_adapter_default_config(); config.score_adjustment = false;
    OK(mus_adapter_create_simplified(&store, &config, 7, &adapter));
    const MusBotEvent history[] = {{0, {MUS_BOT_PASS, 0}}, {1, {MUS_BOT_RAISE, 2}},
                                  {0, {MUS_BOT_RAISE, 4}}, {1, {MUS_BOT_RAISE, 10}}};
    MusOrdagoInformationSet infos[] = {info_base(MUS_PHASE_GRANDE, 0),
        offered(2, 1, 0, 0, 1, 1, false), offered(6, 2, 1, 2, 1, 2, false),
        offered(16, 6, 0, 2, 9, 3, false)};
    for (size_t i = 0; i < 4; ++i) {
        Action actions[MUS_GAME_MAX_LEGAL_ACTIONS]; size_t count;
        InfoSetKey key; InfoNode *node = NULL;
        OK(mus_ordago_public_game_information_set_encode(&infos[i], &key));
        OK(mus_simplified_ordago_information_set_legal_actions(&infos[i], actions,
                                                              MUS_GAME_MAX_LEGAL_ACTIONS, &count));
        OK(cfr_info_store_get_or_create(&store, key, count, &node));
        if (node == NULL) break;
        for (size_t a = 0; a < count; ++a)
            node->strategy_sums[a] = actions[a] == MUS_ACTION_ORDAGO ? 0.25
                : actions[a] == (i == 0 ? MUS_ACTION_PASS : MUS_ACTION_ACCEPT) ? 0.75 : 0;
        MusAdapterObservation o = observation();
        o.player = infos[i].player;
        o.has_hand_start_scores = o.has_pending_wager_total = true;
        o.history = history; o.history_count = i == 0 ? 0 : i + 1;
        if (i != 0) {
            o.legal_actions = response;
            o.legal_action_count = sizeof(response) / sizeof(*response);
        }
        MusAdapterDecision d;
        OK(mus_adapter_decide(adapter, &o, &d));
        CHECK(d.method == MUS_ADAPTER_EXACT && d.public_strategy && d.simplified_strategy);
        CHECK(fabs(probability(&d, MUS_BOT_ORDAGO, 0) - 0.25) < 1e-12);
        CHECK(fabs(probability(&d, i == 0 ? MUS_BOT_PASS : MUS_BOT_ACCEPT, 0) - 0.75) < 1e-12);
        check_distribution(&o, &d);
    }
    mus_adapter_destroy(adapter); OK(cfr_info_store_destroy(&store));
}

static void write_public_fixture(const char *path) {
    InfoStore store = {0}; OK(cfr_info_store_init(&store)); populate_public_fixture(&store);
    MusGameState state; Trainer trainer;
    OK(mus_ordago_public_game_state_init(&state));
    OK(cfr_trainer_init_mccfr(&trainer, mus_ordago_public_game_descriptor(),
                             mus_game_state_as_game_state(&state), &store, 0));
    char *filename = malloc(strlen(path) + 5); CHECK(filename != NULL);
    if (filename) {
        const char *suffixes[] = {"", ".zst", ".txt"};
        for (size_t i = 0; i < 3; ++i) {
            sprintf(filename, "%s%s", path, suffixes[i]);
            FILE *file = fopen(filename, "wb"); CHECK(file != NULL);
            if (file) {
                OK(i == 0 ? cfr_checkpoint_write(file, &trainer)
                    : i == 1 ? cfr_checkpoint_write_zstd(file, &trainer)
                             : mus_ordago_public_strategy_write_text(file, &trainer));
                CHECK(fclose(file) == 0);
            }
        }
        free(filename);
    }
    OK(cfr_info_store_destroy(&store));
}

int main(int argc, char **argv) {
    test_encoder();
    test_all_phases_and_boundaries();
    test_lookup_and_translation();
    test_score_and_fallback();
    test_missing_corrupt_and_legal_gaps();
    test_public_adapter();
    test_simplified_adapter();
    if (argc == 3 && strcmp(argv[1], "--public-fixture") == 0)
        write_public_fixture(argv[2]);
    else if (argc >= 2)
        write_fixture(argv[1]);
    if (argc == 3 && strcmp(argv[1], "--public-fixture") != 0)
        write_score_fixture(argv[2]);
    if (failures) {
        fprintf(stderr, "%d adapter checks failed\n", failures);
        return 1;
    }
    puts("All adapter checks passed.");
    return 0;
}
