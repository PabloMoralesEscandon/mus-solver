#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mus_solver/mus_game.h"
#include "mus_solver/mus_score.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #c); exit(EXIT_FAILURE); } } while (0)
#define OK(c) CHECK((c) == CFR_STATUS_SUCCESS)
static const Game *public_game(void) { return mus_ordago_public_game_descriptor(); }
static GameState *gs(MusGameState *s) { return mus_game_state_as_game_state(s); }
static void apply(const Game *g, MusGameState *s, Action a) {
    OK(cfr_game_apply_action(g, gs(s), a));
}
static size_t menu(const Game *g, MusGameState *s, Action *a) {
    size_t n; OK(cfr_game_legal_actions(g, gs(s), a, MUS_GAME_MAX_LEGAL_ACTIONS, &n));
    return n;
}
static InfoSetKey key(const Game *g, MusGameState *s) {
    InfoSetKey k; OK(cfr_game_information_set_key(g, gs(s), &k)); return k;
}
static void deal(const Game *g, MusGameState *s, const uint8_t h0[8], const uint8_t h1[8]) {
    for (int p = 0; p < 2; ++p)
        for (int c = 0; c < 7; ++c) apply(g, s, p == 0 ? h0[c] : h1[c]);
    CHECK(s->model.phase == MUS_PHASE_MUS_PLAYER_0);
}
static void close_pass(const Game *g, MusGameState *s) {
    const int phase = s->model.phase;
    while (s->model.phase == phase) apply(g, s, MUS_ACTION_PASS);
}
static const uint8_t own[8] = {2,0,0,0,0,0,0,2};
static const uint8_t rivals[2][8] = {{0,2,0,0,2,0,0,0}, {1,0,0,0,0,0,0,3}};

static void check_codec(MusGameState *s) {
    const InfoSetKey k = key(public_game(), s);
    InfoSetKey encoded = -1;
    MusOrdagoInformationSet info;
    OK(mus_ordago_public_game_information_set_decode(k, &info));
    CHECK(!info.score_enabled);
    OK(mus_ordago_public_game_information_set_encode(&info, &encoded));
    CHECK(k == encoded);
    Action a[MUS_GAME_MAX_LEGAL_ACTIONS], b[MUS_GAME_MAX_LEGAL_ACTIONS];
    size_t count;
    const size_t n = menu(public_game(), s, a);
    OK(mus_ordago_game_information_set_legal_actions(&info, b, MUS_GAME_MAX_LEGAL_ACTIONS, &count));
    CHECK(n == count && memcmp(a, b, n * sizeof(*a)) == 0);
}

static void test_report_regressions(void) {
    MusScoreTable table; MusScoreGame score;
    OK(mus_score_table_init(&table, 40));
    OK(mus_score_game_init(&score, &table, 0, 0));
    InfoSetKey keys[3][2][2] = {{{0}}};
    Action menus[2][MUS_GAME_MAX_LEGAL_ACTIONS]; size_t counts[2];
    for (int f = 0; f < 3; ++f) for (int scenario = 0; scenario < 2; ++scenario)
        for (int r = 0; r < 2; ++r) {
            MusGameState s;
            const Game *g = f == 0 ? mus_ordago_game_descriptor() : f == 1 ? public_game() : &score.game;
            if (f == 0) OK(mus_ordago_game_state_init(&s));
            else if (f == 1) OK(mus_ordago_public_game_state_init(&s));
            else OK(mus_score_game_state_init(&s, &score));
            deal(g, &s, own, rivals[r]); apply(g, &s, MUS_ACTION_NO_MUS);
            apply(g, &s, scenario == 0 ? MUS_ACTION_BET_TEN : MUS_ACTION_BET_TWO);
            apply(g, &s, MUS_ACTION_ACCEPT);
            if (scenario == 0) { apply(g, &s, MUS_ACTION_BET_TEN); apply(g, &s, MUS_ACTION_ACCEPT); }
            else close_pass(g, &s);
            CHECK(s.model.phase == MUS_PHASE_PARES);
            keys[f][scenario][r] = key(g, &s);
            if (f == 1) { check_codec(&s); counts[r] = menu(g, &s, menus[r]); }
        }
    for (int scenario = 0; scenario < 2; ++scenario) {
        CHECK(keys[0][scenario][0] != keys[0][scenario][1]);
        CHECK(keys[1][scenario][0] == keys[1][scenario][1]);
        CHECK(keys[1][scenario][0] == keys[2][scenario][0]);
        CHECK(keys[2][scenario][0] == keys[2][scenario][1]);
    }
    CHECK(counts[0] == counts[1] && memcmp(menus[0], menus[1], counts[0] * sizeof(Action)) == 0);
#if MUS_GAME_MAX_MUSES == 1
    CHECK(keys[0][0][0] == 511864500 && keys[0][0][1] == 511854660);
#else
    CHECK(keys[0][0][0] == 9610132436LL && keys[0][0][1] == 9610120956LL);
#endif
}

static void test_count_and_ordago(void) {
    const uint8_t rival[8] = {0,0,2,0,0,0,2,0};
    MusGameState s; const Game *g = public_game();
    OK(mus_ordago_public_game_state_init(&s)); deal(g, &s, own, rival);
    apply(g, &s, MUS_ACTION_NO_MUS);
    const Action grande[] = {MUS_ACTION_BET_TWO, MUS_ACTION_BET_FOUR, MUS_ACTION_BET_FOUR,
        MUS_ACTION_BET_TEN, MUS_ACTION_BET_TEN, MUS_ACTION_ACCEPT};
    for (size_t i = 0; i < sizeof(grande) / sizeof(*grande); ++i) apply(g, &s, grande[i]);
    CHECK(s.model.prior_wager_total == 30 && s.model.pending_lances == 1);
    apply(g, &s, MUS_ACTION_BET_FOUR); apply(g, &s, MUS_ACTION_BET_FOUR); apply(g, &s, MUS_ACTION_ACCEPT);
    CHECK(s.model.prior_wager_total == 38 && s.model.pending_lances == 3);
    CHECK(s.model.paid_scores[0] == 0 && s.model.settled_scores[0] == 0);
    close_pass(g, &s); close_pass(g, &s);
    CHECK(s.model.phase == MUS_PHASE_TERMINAL);
    CHECK(s.model.settled_scores[0] == 40 && s.model.settled_scores[1] == 1);
    double value; OK(cfr_game_terminal_utility(g, gs(&s), CFR_PLAYER_0, &value)); CHECK(value == 39);
    OK(cfr_game_validate_state(g, gs(&s)));

    OK(mus_ordago_public_game_state_init(&s)); deal(g, &s, own, rivals[1]);
    apply(g, &s, MUS_ACTION_NO_MUS); apply(g, &s, MUS_ACTION_PASS);
    apply(g, &s, MUS_ACTION_BET_TWO); apply(g, &s, MUS_ACTION_REJECT);
    CHECK(s.model.paid_scores[1] == 1 && s.model.pending_lances == 0 && s.model.prior_wager_total == 0);
    apply(g, &s, MUS_ACTION_ORDAGO); apply(g, &s, MUS_ACTION_ACCEPT);
    CHECK(s.model.phase == MUS_PHASE_TERMINAL);
    /* Two aces win chica; the losing player's earlier deje disappears. */
    CHECK(s.model.settled_scores[0] == 40 && s.model.settled_scores[1] == 0);
    OK(cfr_game_validate_state(g, gs(&s)));
}

/* Explicit observation grouping. Remove every card-derived value that is not
 * revealed, and also remove accounting caches: the public history determines
 * payments, so a hidden dependency in those caches must not split a group.
 * Strength fields stay: betting actions determine them publicly. */
static MusGameModel observation(const MusGameModel *m, int observer) {
    MusGameModel o = *m;
    memset(o.hands[1 - observer], 0, sizeof(o.hands[0]));
    memset(o.out_of_play, 0, sizeof(o.out_of_play));
    o.deal_class = o.deal_remaining = o.deal_later_available = 0;
    for (int i = 0; i < MUS_GAME_MAX_MUSES; ++i) o.discard_masks[i][1 - observer] = 0;
    memset(o.settled_scores, 0, sizeof(o.settled_scores));
    memset(o.paid_scores, 0, sizeof(o.paid_scores));
    o.pending_lances = o.prior_wager_total = 0;
    if (!o.pairs_revealed) memset(o.pairs, 0, sizeof(o.pairs));
    if (!o.game_revealed) memset(o.game, 0, sizeof(o.game));
    return o;
}
typedef struct { MusGameModel observation; InfoSetKey key; size_t count;
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS]; } Observation;
static Observation observations[4096];
static size_t comparisons;
static bool seen[MUS_PHASE_REDRAW_PLAYER_1 + 1];
static void compare_observation(MusGameState *s, int observer, size_t *used) {
    Actor actor; OK(cfr_game_current_actor(public_game(), gs(s), &actor));
    if (actor.kind != CFR_ACTOR_PLAYER || (int)actor.player != observer) return;
    seen[s->model.phase] = true;
    check_codec(s);
    Observation current = {0}; current.observation = observation(&s->model, observer);
    current.key = key(public_game(), s); current.count = menu(public_game(), s, current.actions);
    for (size_t i = 0; i < *used; ++i) {
        if (memcmp(&observations[i].observation, &current.observation, sizeof(MusGameModel)) != 0) continue;
        CHECK(observations[i].key == current.key && observations[i].count == current.count);
        CHECK(memcmp(observations[i].actions, current.actions, current.count * sizeof(Action)) == 0);
        ++comparisons; return;
    }
    CHECK(*used < sizeof(observations) / sizeof(*observations));
    observations[(*used)++] = current;
}
static void test_hidden_hands(void) {
    const uint8_t hands[2][8] = {{2,0,0,0,0,0,0,2}, {1,0,0,0,0,0,0,3}};
    const int capacities[8] = {8,4,4,4,4,4,4,8};
    for (int observer = 0; observer < 2; ++observer) for (int h = 0; h < 2; ++h)
    for (int muses = 0; muses <= MUS_GAME_MAX_MUSES; ++muses) for (int scenario = 0; scenario < 6; ++scenario) {
        size_t used = 0;
        for (int a = 0; a < 8; ++a) for (int b = a; b < 8; ++b)
        for (int c = b; c < 8; ++c) for (int d = c; d < 8; ++d) {
            uint8_t rival[8] = {0}; ++rival[a]; ++rival[b]; ++rival[c]; ++rival[d];
            bool compatible = true;
            for (int i = 0; i < 8; ++i) if (hands[h][i] + rival[i] > capacities[i]) compatible = false;
            if (!compatible) continue;
            MusGameState s; OK(mus_ordago_public_game_state_init(&s));
            deal(public_game(), &s, observer == 0 ? hands[h] : rival, observer == 0 ? rival : hands[h]);
            size_t steps = 0;
            while (s.model.phase != MUS_PHASE_TERMINAL) {
                CHECK(++steps < 150); compare_observation(&s, observer, &used);
                const int phase = s.model.phase; Action action = MUS_ACTION_PASS;
                if (phase == MUS_PHASE_MUS_PLAYER_0 || phase == MUS_PHASE_MUS_PLAYER_1)
                    action = s.model.mus_count < muses ? MUS_ACTION_MUS : MUS_ACTION_NO_MUS;
                else if (phase == MUS_PHASE_DISCARD_PLAYER_0 || phase == MUS_PHASE_DISCARD_PLAYER_1)
                    action = MUS_ACTION_DISCARD_MASK(1);
                else if (phase == MUS_PHASE_REDRAW_PLAYER_0 || phase == MUS_PHASE_REDRAW_PLAYER_1) {
                    const int player = phase == MUS_PHASE_REDRAW_PLAYER_0 ? 0 : 1;
                    int chosen = observer == player ? 0 : 7;
                    while (chosen > 0 && capacities[chosen] - s.model.hands[0][chosen] -
                           s.model.hands[1][chosen] - s.model.out_of_play[chosen] == 0) --chosen;
                    action = s.model.deal_class == chosen ? MUS_ACTION_TAKE_1 : MUS_ACTION_TAKE_0;
                } else if (s.model.bet_stage == MUS_BET_RESPONSE) {
                    action = scenario == 3 || scenario == 5
                                 ? MUS_ACTION_REJECT : MUS_ACTION_ACCEPT;
                    if (scenario == 4) {
                        MusOrdagoInformationSet info;
                        OK(mus_ordago_public_game_information_set_decode(key(public_game(), &s), &info));
                        if (info.numeric_wager_count == 1) {
                            Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
                            const size_t n = menu(public_game(), &s, actions);
                            for (size_t i = 0; i < n; ++i)
                                if (actions[i] == MUS_ACTION_BET_FOUR) action = actions[i];
                        }
                    }
                }
                else if (scenario != 0) {
                    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS]; const size_t n = menu(public_game(), &s, actions);
                    const Action desired = scenario == 5 ? MUS_ACTION_ORDAGO
                        : scenario == 2 ? MUS_ACTION_BET_TEN : MUS_ACTION_BET_TWO;
                    for (size_t i = 0; i < n; ++i) if (actions[i] == desired) action = desired;
                }
                apply(public_game(), &s, action);
            }
            OK(cfr_game_validate_state(public_game(), gs(&s)));
        }
    }
    CHECK(comparisons > 1000);
    for (int p = MUS_PHASE_GRANDE; p <= MUS_PHASE_PUNTO; ++p) CHECK(seen[p]);
    for (int p = MUS_PHASE_MUS_PLAYER_0; p <= MUS_PHASE_DISCARD_PLAYER_1; ++p) CHECK(seen[p]);
}

static void test_budget_after_uncovered_ordago(void) {
    MusGameState s; const Game *g = public_game();
    OK(mus_ordago_public_game_state_init(&s)); deal(g, &s, own, rivals[0]);
    apply(g, &s, MUS_ACTION_NO_MUS);
    /* Accepted 27 + 12 leaves prior 39. Rejecting a fresh ordago at pares
     * pays one point, so max(paid) + prior reaches 40 before punto. */
    const Action actions[] = {MUS_ACTION_BET_SEVEN, MUS_ACTION_BET_TEN,
        MUS_ACTION_BET_TEN, MUS_ACTION_ACCEPT, MUS_ACTION_BET_TWO,
        MUS_ACTION_BET_TEN, MUS_ACTION_ACCEPT, MUS_ACTION_ORDAGO, MUS_ACTION_REJECT};
    for (size_t i = 0; i < sizeof(actions) / sizeof(*actions); ++i) apply(g, &s, actions[i]);
    CHECK(s.model.phase == MUS_PHASE_PUNTO && s.model.prior_wager_total == 39);
    CHECK(s.model.paid_scores[0] == 1);
    check_codec(&s);
    Action available[MUS_GAME_MAX_LEGAL_ACTIONS];
    CHECK(menu(g, &s, available) == 2);
    CHECK(available[0] == MUS_ACTION_PASS && available[1] == MUS_ACTION_ORDAGO);
}

static void test_paid_forty_before_final_lance(void) {
    MusGameState s; const Game *g = public_game();
    OK(mus_ordago_public_game_state_init(&s)); deal(g, &s, own, rivals[0]);
    apply(g, &s, MUS_ACTION_NO_MUS);
    /* Rejected 27 pays covered 17, a rejected ordago pays covered 22,
     * then rejecting an uncovered ordago pays one. There is no match target. */
    const Action actions[] = {MUS_ACTION_BET_SEVEN, MUS_ACTION_BET_TEN,
        MUS_ACTION_BET_TEN, MUS_ACTION_REJECT, MUS_ACTION_PASS,
        MUS_ACTION_BET_TWO, MUS_ACTION_BET_TEN, MUS_ACTION_BET_TEN,
        MUS_ACTION_ORDAGO, MUS_ACTION_REJECT, MUS_ACTION_ORDAGO, MUS_ACTION_REJECT};
    for (size_t i = 0; i < sizeof(actions) / sizeof(*actions); ++i) apply(g, &s, actions[i]);
    CHECK(s.model.phase == MUS_PHASE_PUNTO && s.model.paid_scores[0] == 40);
    check_codec(&s);
    MusOrdagoInformationSet score_info;
    CHECK(mus_score_game_information_set_decode(key(g, &s), 40, &score_info) == CFR_STATUS_INVALID_ARGUMENT);
    Action available[MUS_GAME_MAX_LEGAL_ACTIONS];
    CHECK(menu(g, &s, available) == 2);
    CHECK(available[0] == MUS_ACTION_PASS && available[1] == MUS_ACTION_ORDAGO);
    close_pass(g, &s);
    CHECK(s.model.phase == MUS_PHASE_TERMINAL);
    OK(cfr_game_validate_state(g, gs(&s)));
}

static void test_accepted_ordago_validation(void) {
    const Game *g = public_game();
    MusGameState s;
    OK(mus_ordago_public_game_state_init(&s)); deal(g, &s, own, rivals[0]);
    apply(g, &s, MUS_ACTION_NO_MUS);
    apply(g, &s, MUS_ACTION_ORDAGO); apply(g, &s, MUS_ACTION_ACCEPT);
    CHECK(s.model.phase == MUS_PHASE_TERMINAL);
    OK(cfr_game_validate_state(g, gs(&s)));
    s.model.phase = MUS_PHASE_CHICA;
    CHECK(cfr_game_validate_state(g, gs(&s)) == CFR_STATUS_INVALID_ARGUMENT);
    s.model.phase = MUS_PHASE_TERMINAL;
    s.model.outcomes[1] = MUS_LANCE_ALL_PASS;
    CHECK(cfr_game_validate_state(g, gs(&s)) == CFR_STATUS_INVALID_ARGUMENT);
}

static void test_paths_and_validation(void) {
    MusScoreTable table; MusScoreGame score;
    OK(mus_score_table_init(&table, 40)); OK(mus_score_game_init(&score, &table, 0, 0));
    uint32_t random = 17;
    for (int sample = 0; sample < 1000; ++sample) {
        MusGameState s, oracle; OK(mus_ordago_public_game_state_init(&s));
        OK(mus_score_game_state_init(&oracle, &score));
        const Game *g = public_game(); const GameOperations *ops = g->trusted_operations;
        while (s.model.phase != MUS_PHASE_TERMINAL) {
            Action a[MUS_GAME_MAX_LEGAL_ACTIONS], b[MUS_GAME_MAX_LEGAL_ACTIONS]; size_t nb;
            const size_t n = menu(g, &s, a);
            OK(ops->legal_actions(g->context, gs(&s), b, MUS_GAME_MAX_LEGAL_ACTIONS, &nb));
            CHECK(n == nb && memcmp(a, b, n * sizeof(Action)) == 0);
            Actor actor; OK(cfr_game_current_actor(g, gs(&s), &actor));
            if (actor.kind == CFR_ACTOR_PLAYER) {
                check_codec(&s);
                if (oracle.model.phase != MUS_PHASE_TERMINAL) CHECK(key(g, &s) == key(&score.game, &oracle));
            }
            const MusGameModel before = s.model; const size_t undo = s.undo_count;
            for (size_t i = 0; i < n; ++i) {
                MusGameState public_child = s; apply(g, &public_child, a[i]);
                OK(ops->apply_action(g->context, gs(&s), a[i]));
                CHECK(memcmp(&public_child.model, &s.model, sizeof(s.model)) == 0);
                OK(cfr_game_validate_state(g, gs(&s)));
                OK(ops->undo_action(g->context, gs(&s)));
                CHECK(memcmp(&before, &s.model, sizeof(before)) == 0 && undo == s.undo_count);
            }
            random = random * UINT32_C(1664525) + UINT32_C(1013904223);
            size_t choices = n;
            if ((sample & 1) != 0 && n > 1 && a[n - 1] == MUS_ACTION_ORDAGO) --choices;
            const Action chosen = a[random % choices];
            if (oracle.model.phase != MUS_PHASE_TERMINAL) apply(&score.game, &oracle, chosen);
            apply(g, &s, chosen);
        }
        while (s.undo_count != 0) { OK(ops->undo_action(g->context, gs(&s))); OK(cfr_game_validate_state(g, gs(&s))); }
    }
    MusGameState s; OK(mus_ordago_public_game_state_init(&s));
    CHECK(cfr_game_validate_state(mus_ordago_game_descriptor(), gs(&s)) == CFR_STATUS_INVALID_ARGUMENT);
    s.model.accounting_mode = MUS_ACCOUNTING_HISTORICAL;
    CHECK(cfr_game_validate_state(public_game(), gs(&s)) == CFR_STATUS_INVALID_ARGUMENT);
    OK(mus_ordago_public_game_state_init(&s)); s.model.score_target = 40;
    CHECK(cfr_game_validate_state(public_game(), gs(&s)) == CFR_STATUS_INVALID_ARGUMENT);
    OK(mus_ordago_public_game_state_init(&s)); s.model.initial_scores[0] = 1;
    CHECK(cfr_game_validate_state(public_game(), gs(&s)) == CFR_STATUS_INVALID_ARGUMENT);
    MusOrdagoInformationSet info, original; memset(&info, 0x5a, sizeof(info)); original = info;
    CHECK(mus_ordago_public_game_information_set_decode(-1, &info) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(&info, &original, sizeof(info)) == 0);
    OK(mus_ordago_public_game_state_init(&s)); deal(public_game(), &s, own, rivals[0]);
    apply(public_game(), &s, MUS_ACTION_NO_MUS);
    OK(mus_ordago_public_game_information_set_decode(key(public_game(), &s), &info));
    info.max_settled_score = info.scores[0] = 41;
    InfoSetKey encoded = -123;
    CHECK(mus_ordago_public_game_information_set_encode(&info, &encoded) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(encoded == -123);
}
int main(void) {
    test_report_regressions(); test_count_and_ordago(); test_hidden_hands(); test_budget_after_uncovered_ordago();
    test_paid_forty_before_final_lance(); test_accepted_ordago_validation(); test_paths_and_validation();
    printf("Public ordago tests passed (%zu hidden-hand comparisons).\n", comparisons);
    return EXIT_SUCCESS;
}
