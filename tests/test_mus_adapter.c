#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfr/checkpoint.h"
#include "mus_solver/mus_adapter.h"
#include "mus_solver/mus_strategy.h"

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
static InfoSetKey put(InfoStore *store, MusOrdagoInformationSet info, Action first,
                      double first_probability, Action second) {
    InfoSetKey key = -1;
    Action actions[16];
    size_t count = 0;
    OK(mus_ordago_game_information_set_encode(&info, &key));
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

int main(int argc, char **argv) {
    test_encoder();
    test_all_phases_and_boundaries();
    test_lookup_and_translation();
    test_score_and_fallback();
    test_missing_corrupt_and_legal_gaps();
    if (argc == 2)
        write_fixture(argv[1]);
    if (failures) {
        fprintf(stderr, "%d adapter checks failed\n", failures);
        return 1;
    }
    puts("All adapter checks passed.");
    return 0;
}
