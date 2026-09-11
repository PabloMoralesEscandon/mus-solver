#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../app/match_evaluation.h"
#include "mus_sim.h"
#include "mus_log.h"

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "match check failed at line %d: %s\n", __LINE__, #x); failures++; } } while (0)
typedef enum { PASS, TWENTY, DEJE, DEJE_ORDAGO, MUS, ACCEPT_ORDAGO, INVALID } Script;
typedef struct {
    Script script;
    uint8_t retained[2][MUS_GAME_CLASS_COUNT];
    bool retained_set[2];
    size_t pending_twenty;
    size_t dejes;
    size_t discards;
    size_t after_mus;
    size_t skipped_pares[3];
} ScriptContext;
static bool betting(MusGamePhase phase) {
    return phase >= MUS_PHASE_GRANDE && phase <= MUS_PHASE_PUNTO;
}
static Status scripted(void *raw, const MusAdapterObservation *o, MusAdapterDecision *d) {
    ScriptContext *context = raw;
    CHECK(o->has_hand_start_scores && o->has_pending_wager_total);
    unsigned int cards = 0;
    for (size_t i = 0; i < MUS_GAME_CLASS_COUNT; ++i) cards += o->hand[i];
    CHECK(cards == 4);
    CHECK(o->known_hand_scores[0] == o->match_scores[0] - o->hand_start_scores[0]);
    CHECK(o->known_hand_scores[1] == o->match_scores[1] - o->hand_start_scores[1]);
    MusBotAction action = {MUS_BOT_PASS, 0};
    if (context->script == INVALID) {
        *d = (MusAdapterDecision){.method = MUS_ADAPTER_FALLBACK, .selected = {MUS_BOT_RAISE, 41}};
        return CFR_STATUS_SUCCESS;
    }
    if (o->phase == MUS_PHASE_MUS_PLAYER_0 || o->phase == MUS_PHASE_MUS_PLAYER_1) {
        action.kind = context->script == MUS && o->mus_count == 0 ? MUS_BOT_MUS : MUS_BOT_NO_MUS;
        CHECK(o->history_count == 0 && o->previous_strength_count == 0);
        if (o->mus_count == 0 && o->player == CFR_PLAYER_0) {
            context->retained_set[0] = context->retained_set[1] = false;
        }
    } else if (o->phase == MUS_PHASE_DISCARD_PLAYER_0 || o->phase == MUS_PHASE_DISCARD_PLAYER_1) {
        CHECK(context->script == MUS && o->mus_count == 0);
        if (o->player == CFR_PLAYER_1) CHECK(o->discard_counts[0][0] == 2);
        CHECK(o->discard_counts[0][o->player] == 0);
        action = (MusBotAction){MUS_BOT_DISCARD, 3};
        memcpy(context->retained[o->player], o->hand, sizeof(o->hand));
        unsigned int remaining = 2;
        for (size_t i = 0; i < MUS_GAME_CLASS_COUNT && remaining; ++i) {
            const unsigned int remove = o->hand[i] < remaining ? o->hand[i] : remaining;
            context->retained[o->player][i] -= (uint8_t)remove;
            remaining -= remove;
        }
        context->retained_set[o->player] = true;
        context->discards++;
    } else if (betting(o->phase)) {
        const unsigned int phase = o->phase == MUS_PHASE_GRANDE ? 0U
            : o->phase == MUS_PHASE_CHICA ? 1U : o->phase == MUS_PHASE_PARES ? 2U : 3U;
        CHECK(o->previous_strength_count == phase);
        /* Rejected grande has a fully public payment. The accepted chica
         * becomes pending; passes also leave a hidden winner to count later. */
        CHECK(o->hand_scores_complete == (phase == 0 ||
            ((context->script == DEJE || context->script == DEJE_ORDAGO) && phase == 1)));
        if (phase == 3 && o->previous_strengths[2] >= MUS_LANCE_STRENGTH_PARES_SKIPPED_NONE) {
            const MusLanceStrength declaration = o->previous_strengths[2];
            bool own_pairs = false;
            for (size_t c = 0; c < MUS_GAME_CLASS_COUNT; ++c) own_pairs |= o->hand[c] >= 2;
            const MusLanceStrength own_declaration = o->player == CFR_PLAYER_0
                ? MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_0 : MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_1;
            CHECK(own_pairs == (declaration == own_declaration));
            CHECK(declaration <= MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_1);
            context->skipped_pares[declaration - MUS_LANCE_STRENGTH_PARES_SKIPPED_NONE]++;
        }
        if (context->script == TWENTY) {
            CHECK(o->pending_wager_total == (phase == 0 ? 0 : phase == 1 ? 10 : 20));
            CHECK(o->prior_wager_total == o->pending_wager_total);
            CHECK(o->match_scores[0] == o->hand_start_scores[0]);
            CHECK(o->match_scores[1] == o->hand_start_scores[1]);
            if (phase > 0) CHECK(o->previous_strengths[0] == MUS_LANCE_STRENGTH_BOTH);
            if (phase > 1) { CHECK(o->previous_strengths[1] == MUS_LANCE_STRENGTH_BOTH); context->pending_twenty++; }
            if (phase < 2) action = o->history_count == 0 ? (MusBotAction){MUS_BOT_RAISE, 10}
                                                                       : (MusBotAction){MUS_BOT_ACCEPT, 0};
        } else if (context->script == DEJE || context->script == DEJE_ORDAGO) {
            const int paid = context->script == DEJE ? 2 : 4;
            if (phase == 0) {
                action = o->history_count == 0 ? (MusBotAction){MUS_BOT_RAISE, paid}
                    : o->history_count == 1 ? (context->script == DEJE ? (MusBotAction){MUS_BOT_RAISE, 3}
                                                                                      : (MusBotAction){MUS_BOT_ORDAGO, 0})
                                           : (MusBotAction){MUS_BOT_REJECT, 0};
            } else {
                CHECK(o->match_scores[0] == o->hand_start_scores[0]);
                CHECK(o->match_scores[1] == o->hand_start_scores[1] + paid);
                CHECK(o->previous_strengths[0] == MUS_LANCE_STRENGTH_BOTH);
                CHECK(o->pending_wager_total == (phase == 1 ? 0 : 10));
                CHECK(o->prior_wager_total == paid + o->pending_wager_total);
                context->dejes++;
                if (phase == 1) action = o->history_count == 0 ? (MusBotAction){MUS_BOT_RAISE, 10}
                                                                           : (MusBotAction){MUS_BOT_ACCEPT, 0};
            }
        } else if (context->script == ACCEPT_ORDAGO) {
            CHECK(phase == 0);
            action.kind = o->history_count == 0 ? MUS_BOT_ORDAGO : MUS_BOT_ACCEPT;
        } else {
            CHECK(o->pending_wager_total == 0 && o->prior_wager_total == 0);
            CHECK(o->match_scores[0] == o->hand_start_scores[0]);
            CHECK(o->match_scores[1] == o->hand_start_scores[1]);
        }
    }
    if (o->mus_count == 1) {
        CHECK(context->script == MUS && o->discard_counts[0][0] == 2 && o->discard_counts[0][1] == 2);
        CHECK(context->retained_set[o->player]);
        for (size_t i = 0; i < MUS_GAME_CLASS_COUNT; ++i) CHECK(o->hand[i] >= context->retained[o->player][i]);
        context->after_mus++;
    }
    bool found = false;
    for (size_t i = 0; i < o->legal_action_count; ++i)
        found |= action.kind == o->legal_actions[i].kind && action.amount == o->legal_actions[i].amount;
    CHECK(found);
    *d = (MusAdapterDecision){.method = MUS_ADAPTER_FALLBACK, .selected = action};
    return CFR_STATUS_SUCCESS;
}
static int cut(const Mano *a, int b, int c, const int d[2], void *e) {
    (void)a; (void)b; (void)c; (void)d; (void)e; return 0;
}
static int no_discard(const Mano *a, int b, int c[4], void *d) {
    (void)a; (void)b; (void)c; (void)d; return 1;
}
static AccionEnviteMus pass(const Mano *a, int b, int c, const int d[2], Ronda e, const EnviteMus *f, void *g) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g;
    return (AccionEnviteMus){ACCION_PASAR, 0};
}
static void check_reference(const MusMatchOptions *options, const MusMatchResult *expected) {
    PartidaMus game = {0}; CHECK(iniciarPartidaMusConJugadores(&game, 2) == 0);
    const EstrategiaMus policies[4] = {{cut,no_discard,pass,NULL}, {cut,no_discard,pass,NULL}, {0}, {0}};
    srand(options->seed);
    size_t wins[2] = {0}, hands = 0; uint64_t checksum = 0;
    for (size_t i = 0; i < options->matches; ++i) {
        game.mano = (int)(i % 2); game.tantos[0] = options->initial_scores[0]; game.tantos[1] = options->initial_scores[1];
        int winner;
        do { winner = simularRondaMusConEstrategias(&game, policies); hands++; CHECK(winner >= 0); } while (winner == 0);
        CHECK(winner >= 1 && winner <= 2); if (winner < 1 || winner > 2) break;
        wins[winner - 1]++;
        checksum = checksum * UINT64_C(1099511628211) + (uint64_t)(game.tantos[0]*41+game.tantos[1]+winner*1681);
    }
    CHECK(expected->wins[0] == wins[0] && expected->wins[1] == wins[1]);
    CHECK(expected->hands == hands && expected->checksum == checksum);
    CHECK(destruirPartidaMus(&game) == 0);
}
int main(void) {
    MusMatchResult *result = calloc(1, sizeof(*result));
    MusMatchResult *before = calloc(1, sizeof(*before));
    if (!result || !before) return 2;
    MusMatchOptions options = {.matches = 12, .seed = 7, .max_hands_per_match = 1000};
    ScriptContext context = {0};
    const MusMatchPolicy policies[2] = {{scripted,&context,true}, {scripted,&context,true}};
    if (MUS_GAME_MAX_MUSES != 1) {
        CHECK(mus_match_evaluate(&options, policies, result) == CFR_STATUS_INCOMPATIBLE_GAME);
    } else {
        for (Script script = PASS; script <= ACCEPT_ORDAGO; ++script) {
            context = (ScriptContext){.script = script};
            CHECK(mus_match_evaluate(&options, policies, result) == CFR_STATUS_SUCCESS);
            CHECK(result->matches == options.matches && result->wins[0] + result->wins[1] == options.matches);
            if (script == PASS) {
                check_reference(&options, result);
                for (size_t i = 0; i < 3; ++i) CHECK(context.skipped_pares[i] > 0);
            }
            if (script == TWENTY) CHECK(context.pending_twenty > 0);
            if (script == DEJE || script == DEJE_ORDAGO) CHECK(context.dejes > 0);
            if (script == MUS) CHECK(context.discards > 0 && context.after_mus > 0);
            if (script == ACCEPT_ORDAGO) CHECK(result->hands == result->matches);
            for (size_t p = 0; p < 2; ++p) {
                uint64_t phases = 0, scores = 0, actions = 0, methods = 0, strategies = 0;
                for (size_t m = 0; m < MUS_MATCH_METHOD_COUNT; ++m) {
                    methods += result->methods[p][m];
                    for (size_t phase = 0; phase < MUS_MATCH_PHASE_COUNT; ++phase) phases += result->phases[p][phase][m];
                    for (size_t a = 0; a < 40; ++a) for (size_t b = 0; b < 40; ++b) scores += result->scores[p][a][b][m];
                    for (size_t a = 0; a <= MUS_BOT_DISCARD; ++a) actions += result->actions[p][a][m];
                }
                for (size_t s = 0; s < 4; ++s) strategies += result->strategies[p][s];
                CHECK(methods == result->decisions[p] && phases == methods && scores == methods && actions == methods && strategies == methods);
            }
        }
        *before = *result;
        context.script = INVALID;
        fijarNivelLog(LOG_RESULTADO);
        CHECK(mus_match_evaluate(&options, policies, result) == CFR_STATUS_INVALID_ARGUMENT);
        CHECK(obtenerNivelLog() == LOG_RESULTADO);
        fijarNivelLog(LOG_SILENCIO);
        CHECK(memcmp(result, before, sizeof(*result)) == 0);
        context.script = PASS; options.max_hands_per_match = 1;
        CHECK(mus_match_evaluate(&options, policies, result) == CFR_STATUS_NOT_FOUND);
        CHECK(memcmp(result, before, sizeof(*result)) == 0);
    }
    free(result); free(before);
    if (failures) return 1;
    puts("All deployed-match host checks passed.");
    return 0;
}
