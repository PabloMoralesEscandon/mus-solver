#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "baraja_espanola.h"
#include "mus.h"
#include "mus_solver/mus_four.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
    exit(EXIT_FAILURE); \
} } while (0)

static const int card_numbers[8] = {AS, CUATRO, CINCO, SEIS, SIETE, SOTA, CABALLO, REY};
static const unsigned int deck_counts[8] = {8, 4, 4, 4, 4, 4, 4, 8};
static uint64_t rng = 7;

static unsigned int random_number(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (unsigned int)(rng >> 32);
}

static GameState *raw(MusFourGameState *state) { return (GameState *)state; }

static void init_table(MusScoreTable *table) {
    CHECK(mus_score_table_init(table, 40) == CFR_STATUS_SUCCESS);
    table->four_player = true;
    for (unsigned int a = 0; a < 40; ++a) {
        for (unsigned int b = 0; b < 40; ++b) {
            table->cells[a][b] = (MusScoreCell){.ready = true,
                /* An asymmetric continuation detects a missing seat rotation
                 * independently of a missing utility sign change. */
                .value = (2.0 * (double)a - (double)b + 3.0) / 100.0,
                .iterations = 1, .evaluation_samples = 1};
        }
    }
}

static void apply(const MusScoreGame *game, MusFourGameState *state, Action action) {
    CHECK(cfr_game_apply_action(&game->game, raw(state), action) == CFR_STATUS_SUCCESS);
    CHECK(cfr_game_validate_state(&game->game, raw(state)) == CFR_STATUS_SUCCESS);
}

static void deal(const MusScoreGame *game, MusFourGameState *state,
                 const uint8_t hands[4][8]) {
    CHECK(mus_four_score_game_state_init(state, game) == CFR_STATUS_SUCCESS);
    size_t count = 0;
    while (state->model.phase == MUS_FOUR_PHASE_DEAL) {
        CHECK(++count < 40);
        const unsigned int player = state->model.current_player;
        unsigned int card = 0;
        while (card < 8 && state->model.hands[player][card] == hands[player][card]) ++card;
        CHECK(card < 8);
        apply(game, state, (Action)card);
    }
    CHECK(state->model.phase == MUS_FOUR_PHASE_MUS);
    CHECK(memcmp(state->model.hands, hands, sizeof(state->model.hands)) == 0);
}

static void make_simulator_hands(const MusFourGameState *state,
                                  Carta cards[4][4], Mano hands[4]) {
    for (unsigned int p = 0; p < 4; ++p) {
        size_t n = 0;
        for (unsigned int c = 0; c < 8; ++c) {
            for (unsigned int k = 0; k < state->model.hands[p][c]; ++k) {
                CHECK(n < 4);
                cards[p][n++] = (Carta){.numero = card_numbers[c], .palo = OROS};
            }
        }
        CHECK(n == 4);
        hands[p] = (Mano){.cartas = cards[p], .tamano = n};
    }
}

static void compare_passive_count(const MusFourGameState *state) {
    Carta cards[4][4];
    Mano hands[4];
    make_simulator_hands(state, cards, hands);
    PartidaMus reference = {.numeroJugadores = 4, .mano = 0};
    memcpy(reference.manos, hands, sizeof(hands));
    CHECK(puntuarGrande(&reference) == 0);
    CHECK(puntuarChica(&reference) == 0);
    CHECK(puntuarPares(&reference) == 0);
    CHECK(puntuarJuegoOPunto(&reference) == 0);
    for (unsigned int team = 0; team < 2; ++team)
        CHECK((int)state->model.settled_scores[team] == reference.tantos[team]);
}

static void check_chance(const MusScoreGame *game, MusFourGameState *state,
                         Action *actions, Probability *probabilities, size_t *count) {
    CHECK(cfr_game_chance_outcomes(&game->game, raw(state), actions, probabilities,
                                   16, count) == CFR_STATUS_SUCCESS);
    CHECK(*count > 0 && *count <= 8);
    unsigned int available[8], total = 0;
    for (unsigned int c = 0; c < 8; ++c) {
        unsigned int used = state->model.out_of_play[c];
        for (unsigned int p = 0; p < 4; ++p) used += state->model.hands[p][c];
        CHECK(used <= deck_counts[c]);
        available[c] = deck_counts[c] - used;
        total += available[c];
    }
    if (total == 0) {
        for (unsigned int c = 0; c < 8; ++c) {
            available[c] = state->model.out_of_play[c];
            total += available[c];
        }
    }
    CHECK(total > 0);
    double sum = 0;
    for (size_t i = 0; i < *count; ++i) {
        CHECK(actions[i] >= 0 && actions[i] < 8);
        const double expected = (double)available[actions[i]] / (double)total;
        CHECK(fabs(probabilities[i] - expected) < 1e-12);
        CHECK(probabilities[i] > 0);
        sum += probabilities[i];
    }
    CHECK(fabs(sum - 1) < 1e-12);
}

static void check_information(const MusScoreGame *game, MusFourGameState *state,
                              MusFourGameState *other) {
    InfoSetKey key = -1, encoded = -1;
    MusFourInformationSet information;
    Action actions[16], decoded_actions[16];
    size_t count = 0, decoded_count = 0;
    CHECK(cfr_game_information_set_key(&game->game, raw(state), &key) == CFR_STATUS_SUCCESS);
    CHECK(key >= 0);
    CHECK(mus_four_score_information_set_decode(key, &information) == CFR_STATUS_SUCCESS);
    CHECK(mus_four_score_information_set_encode(&information, &encoded) == CFR_STATUS_SUCCESS);
    CHECK(encoded == key);
    CHECK(cfr_game_legal_actions(&game->game, raw(state), actions, 16, &count) == CFR_STATUS_SUCCESS);
    CHECK(mus_four_score_information_set_legal_actions(&information, decoded_actions, 16,
                                                      &decoded_count) == CFR_STATUS_SUCCESS);
    CHECK(count == decoded_count && memcmp(actions, decoded_actions, count * sizeof(Action)) == 0);

    /* Swap an opponent's cards with the teammate's cards. Preserve every
     * pairs/game declaration that has become public in this phase. */
    const unsigned int partner = state->model.current_player ^ 2U;
    const unsigned int opponent = state->model.current_player ^ 1U;
    const bool same_pairs = (state->hand_summaries[partner].pairs_points != 0) ==
                            (state->hand_summaries[opponent].pairs_points != 0);
    const bool same_game = (state->hand_summaries[partner].game_points != 0) ==
                           (state->hand_summaries[opponent].game_points != 0);
    if (state->model.phase != MUS_FOUR_PHASE_DISCARD &&
        (state->model.phase < MUS_FOUR_PHASE_PARES || same_pairs) &&
        (state->model.phase < MUS_FOUR_PHASE_JUEGO || same_game)) {
        memcpy(other, state, sizeof(*other));
        uint8_t hand[8];
        memcpy(hand, other->model.hands[partner], sizeof(hand));
        memcpy(other->model.hands[partner], other->model.hands[opponent], sizeof(hand));
        memcpy(other->model.hands[opponent], hand, sizeof(hand));
        const MusHandSummary summary = other->hand_summaries[partner];
        other->hand_summaries[partner] = other->hand_summaries[opponent];
        other->hand_summaries[opponent] = summary;
        CHECK(cfr_game_validate_state(&game->game, raw(other)) == CFR_STATUS_SUCCESS);
        CHECK(cfr_game_information_set_key(&game->game, raw(other), &encoded) == CFR_STATUS_SUCCESS);
        CHECK(encoded == key);
    }
}

static void check_utility(const MusScoreGame *game, MusFourGameState *state) {
    double utility[4];
    for (unsigned int p = 0; p < 4; ++p) {
        CHECK(cfr_game_terminal_utility(&game->game, raw(state), (Player)p,
                                         &utility[p]) == CFR_STATUS_SUCCESS);
        CHECK(isfinite(utility[p]) && utility[p] >= -1 && utility[p] <= 1);
    }
    CHECK(utility[0] == utility[2] && utility[1] == utility[3] && utility[0] == -utility[1]);
    const unsigned int a = state->model.initial_scores[0] + state->model.settled_scores[0];
    const unsigned int b = state->model.initial_scores[1] + state->model.settled_scores[1];
    if (a >= 40) CHECK(utility[0] == 1);
    else if (b >= 40) CHECK(utility[0] == -1);
    else CHECK(utility[0] == -state->continuation->cells[b][a].value);
}

static void random_properties(MusScoreTable *table) {
    MusFourGameState *state = malloc(sizeof(*state));
    MusFourGameState *other = malloc(sizeof(*other));
    CHECK(state != NULL && other != NULL);
    for (unsigned int trial = 0; trial < 1200; ++trial) {
        MusScoreGame game;
        const bool passive = trial < 400;
        CHECK(mus_score_game_init(&game, table, passive ? 0 : random_number() % 40,
                                   passive ? 0 : random_number() % 40) == CFR_STATUS_SUCCESS);
        CHECK(mus_four_score_game_state_init(state, &game) == CFR_STATUS_SUCCESS);
        size_t steps = 0;
        while (state->model.phase != MUS_FOUR_PHASE_TERMINAL) {
            CHECK(++steps < MUS_FOUR_UNDO_CAPACITY);
            Actor actor;
            Action actions[16];
            Probability probabilities[16];
            size_t count = 0;
            CHECK(cfr_game_current_actor(&game.game, raw(state), &actor) == CFR_STATUS_SUCCESS);
            Action action;
            if (actor.kind == CFR_ACTOR_CHANCE) {
                check_chance(&game, state, actions, probabilities, &count);
                action = actions[random_number() % count];
            } else {
                CHECK((unsigned int)actor.player < 4);
                check_information(&game, state, other);
                CHECK(cfr_game_legal_actions(&game.game, raw(state), actions, 16, &count) == CFR_STATUS_SUCCESS);
                action = passive ? (state->model.phase == MUS_FOUR_PHASE_MUS ? MUS_ACTION_NO_MUS
                                                                                         : MUS_ACTION_PASS)
                                 : actions[random_number() % count];
            }
            const MusFourGameModel before = state->model;
            MusHandSummary summaries[4];
            memcpy(summaries, state->hand_summaries, sizeof(summaries));
            const size_t depth = state->undo_count;
            apply(&game, state, action);
            CHECK(cfr_game_undo_action(&game.game, raw(state)) == CFR_STATUS_SUCCESS);
            CHECK(memcmp(&state->model, &before, sizeof(before)) == 0);
            CHECK(memcmp(state->hand_summaries, summaries, sizeof(summaries)) == 0);
            CHECK(state->undo_count == depth);
            apply(&game, state, action);
        }
        if (passive) compare_passive_count(state);
        check_utility(&game, state);
    }
    free(other);
    free(state);
}

/* Replay actions through the simulator's independent wager and scoring APIs.
 * The reference never reads solver outcomes, paid_scores, settled_scores,
 * current_wager, previous_wager or cached hand summaries. An offset converts
 * the simulator's fixed target of 40 into the requested match target. */
typedef struct {
    PartidaMus game;
    Carta cards[4][4];
    EnviteMus wagers[5];
    unsigned int passes;
    unsigned int rejections;
    unsigned int initial[2];
    unsigned int target;
    Ronda phase;
    bool ready;
    bool finished;
} ScoreOracle;

typedef struct {
    unsigned int accepted[5];
    unsigned int rejected[5];
    unsigned int rejected_raises;
    unsigned int rejected_ordagos;
    unsigned int accepted_ordagos;
    unsigned int ordago_winners;
    unsigned int final_counts;
    unsigned int immediate_wins;
} ScoringCoverage;

static ScoringCoverage scoring_coverage;

static Ronda simulator_phase(unsigned int phase) {
    switch (phase) {
    case MUS_FOUR_PHASE_GRANDE: return GRANDE;
    case MUS_FOUR_PHASE_CHICA: return CHICA;
    case MUS_FOUR_PHASE_PARES: return PARES;
    case MUS_FOUR_PHASE_JUEGO: return JUEGO;
    case MUS_FOUR_PHASE_PUNTO: return PUNTO;
    default: CHECK(false); return GRANDE;
    }
}

static unsigned int oracle_eligible(ScoreOracle *oracle, Ronda phase) {
    unsigned int eligible = 0;
    for (unsigned int seat = 0; seat < 4; ++seat) {
        bool has = true;
        if (phase == PARES) has = tantosPares(oracle->game.manos[seat]) > 0;
        if (phase == JUEGO) has = tantosJuego(oracle->game.manos[seat]) > 0;
        if (has) eligible |= 1U << seat;
    }
    return eligible;
}

static int oracle_winner(ScoreOracle *oracle, Ronda phase) {
    switch (phase) {
    case GRANDE: return ganadorGrandeConJugadores(oracle->game.manos, 4, 0);
    case CHICA: return ganadorChicaConJugadores(oracle->game.manos, 4, 0);
    case PARES: return ganadorParConJugadores(oracle->game.manos, 4, 0);
    case JUEGO: return ganadorJuegoConJugadores(oracle->game.manos, 4, 0);
    case PUNTO: return ganadorPuntoConJugadores(oracle->game.manos, 4, 0);
    }
    CHECK(false);
    return -1;
}

static bool oracle_match_finished(const ScoreOracle *oracle) {
    return oracle->game.tantos[0] == 40 || oracle->game.tantos[1] == 40;
}

static void oracle_count(ScoreOracle *oracle) {
    ++scoring_coverage.final_counts;
    const Ronda last = equipoTieneJuego(oracle->game.manos, 4, 0) ||
                       equipoTieneJuego(oracle->game.manos, 4, 1) ? JUEGO : PUNTO;
    const Ronda rounds[4] = {GRANDE, CHICA, PARES, last};
    for (unsigned int i = 0; i < 4 && !oracle_match_finished(oracle); ++i) {
        const Ronda phase = rounds[i];
        const EnviteMus *wager = &oracle->wagers[phase];
        const bool rejected = wager->estado == ENVITE_RECHAZADO ||
                              wager->estado == ORDAGO_RECHAZADO;
        int result = 0;
        if (rejected) {
            /* registrarEnviteMus already paid the rejection. Only the
             * winning bidder's intrinsic points remain to be counted. */
            if (phase == PARES)
                result = puntuarParesDePareja(&oracle->game, wager->parejaApostadora, 0);
            else if (phase == JUEGO || phase == PUNTO)
                result = puntuarJuegoOPuntoDePareja(&oracle->game, phase,
                                                   wager->parejaApostadora, 0);
        } else {
            switch (phase) {
            case GRANDE: result = puntuarGrande(&oracle->game); break;
            case CHICA: result = puntuarChica(&oracle->game); break;
            case PARES: result = puntuarPares(&oracle->game); break;
            case JUEGO:
            case PUNTO: result = puntuarJuegoOPunto(&oracle->game); break;
            }
        }
        CHECK(result >= 0);
    }
    oracle->finished = true;
}

static void oracle_advance(ScoreOracle *oracle) {
    oracle->passes = oracle->rejections = 0;
    if (oracle_match_finished(oracle)) {
        ++scoring_coverage.immediate_wins;
        oracle->finished = true;
        return;
    }
    switch (oracle->phase) {
    case GRANDE: oracle->phase = CHICA; return;
    case CHICA: oracle->phase = PARES; break;
    case PARES: oracle->phase = JUEGO; break;
    case JUEGO:
    case PUNTO: oracle_count(oracle); return;
    }
    if (oracle->phase == PARES) {
        const int team0 = equipoTienePares(oracle->game.manos, 4, 0);
        const int team1 = equipoTienePares(oracle->game.manos, 4, 1);
        CHECK(team0 >= 0 && team1 >= 0);
        if (team0 && team1) return;
        oracle->phase = JUEGO;
    }
    const int team0 = equipoTieneJuego(oracle->game.manos, 4, 0);
    const int team1 = equipoTieneJuego(oracle->game.manos, 4, 1);
    CHECK(team0 >= 0 && team1 >= 0);
    if (team0 && team1) return;
    if (!team0 && !team1) oracle->phase = PUNTO;
    else oracle_count(oracle);
}

static void oracle_initialize(ScoreOracle *oracle, const MusScoreGame *game,
                               const MusFourGameState *state) {
    *oracle = (ScoreOracle){.target = game->continuation->target,
                            .phase = GRANDE, .ready = true};
    oracle->game.numeroJugadores = 4;
    make_simulator_hands(state, oracle->cards, oracle->game.manos);
    for (unsigned int team = 0; team < 2; ++team) {
        oracle->initial[team] = game->initial_scores[team];
        oracle->game.tantos[team] = (int)(40 - oracle->target + oracle->initial[team]);
    }
    for (unsigned int phase = 0; phase < 5; ++phase)
        CHECK(iniciarEnviteMus(&oracle->wagers[phase]) == 0);
}

static void oracle_action(ScoreOracle *oracle, unsigned int seat, Action action) {
    CHECK(oracle->ready && !oracle->finished && seat < 4);
    const Ronda phase = oracle->phase;
    EnviteMus *wager = &oracle->wagers[phase];
    const unsigned int eligible = oracle_eligible(oracle, phase);
    CHECK((eligible & (1U << seat)) != 0);
    const int team = (int)(seat % 2);
    if (action == MUS_ACTION_PASS) {
        CHECK(wager->estado == ENVITE_AL_PASO);
        oracle->passes |= 1U << seat;
        if (oracle->passes == eligible) {
            CHECK(registrarEnviteMus(&oracle->game, phase, wager) == 0);
            oracle_advance(oracle);
        }
        return;
    }
    if (action == MUS_ACTION_REJECT) {
        CHECK(wager->estado == ENVITE_PENDIENTE || wager->estado == ORDAGO_PENDIENTE);
        CHECK(team != wager->parejaApostadora);
        oracle->rejections |= 1U << seat;
        const unsigned int team_members = eligible & (team == 0 ? 5U : 10U);
        if (oracle->rejections == team_members) {
            CHECK(noQuererEnviteMus(wager, team) == 0);
            ++scoring_coverage.rejected[phase];
            scoring_coverage.rejected_raises += wager->cantidadAnterior > 1;
            scoring_coverage.rejected_ordagos += wager->estado == ORDAGO_RECHAZADO;
            CHECK(registrarEnviteMus(&oracle->game, phase, wager) >= 0);
            oracle_advance(oracle);
        }
        return;
    }
    if (action == MUS_ACTION_ACCEPT) {
        CHECK(quererEnviteMus(wager, team) == 0);
        if (wager->estado == ORDAGO_ACEPTADO) {
            ++scoring_coverage.accepted_ordagos;
            const int winner = oracle_winner(oracle, phase);
            CHECK(winner >= 0 && winner < 4);
            scoring_coverage.ordago_winners |= 1U << (unsigned int)winner;
            CHECK(resolverOrdagoMus(&oracle->game, phase, wager) == winner % 2 + 1);
            oracle->finished = true;
        } else {
            ++scoring_coverage.accepted[phase];
            CHECK(registrarEnviteMus(&oracle->game, phase, wager) == 0);
            oracle_advance(oracle);
        }
        return;
    }
    oracle->rejections = 0;
    if (action == MUS_ACTION_ORDAGO) {
        CHECK(ordagoMus(wager, team) == 0);
        return;
    }
    int increment = 0;
    switch (action) {
    case MUS_ACTION_BET_TWO: increment = 2; break;
    case MUS_ACTION_BET_FOUR: increment = 4; break;
    case MUS_ACTION_BET_SEVEN: increment = 7; break;
    case MUS_ACTION_BET_TEN: increment = 10; break;
    default: CHECK(false);
    }
    const int total = (wager->estado == ENVITE_AL_PASO ? 0 : wager->cantidad) + increment;
    CHECK(envidarMus(wager, team, total) == 0);
}

static void oracle_compare(const ScoreOracle *oracle, const MusFourGameState *state) {
    CHECK(oracle->finished == (state->model.phase == MUS_FOUR_PHASE_TERMINAL));
    if (!oracle->finished) CHECK(oracle->phase == simulator_phase(state->model.phase));
    for (unsigned int team = 0; team < 2; ++team) {
        const int expected = oracle->game.tantos[team] -
            (int)(40 - oracle->target + oracle->initial[team]);
        if ((int)state->model.settled_scores[team] != expected) {
            fprintf(stderr, "Scoring oracle mismatch: target=%u initial=%u:%u phase=%d team=%u expected=%d actual=%u\n",
                    oracle->target, oracle->initial[0], oracle->initial[1],
                    (int)oracle->phase, team, expected, state->model.settled_scores[team]);
            CHECK(false);
        }
    }
}

static void scored_apply(const MusScoreGame *game, MusFourGameState *state,
                          ScoreOracle *oracle, Action action) {
    if (oracle->ready) {
        CHECK(oracle->phase == simulator_phase(state->model.phase));
        oracle_action(oracle, state->model.current_player, action);
    }
    apply(game, state, action);
    if (!oracle->ready && state->model.phase == MUS_FOUR_PHASE_GRANDE)
        oracle_initialize(oracle, game, state);
    if (oracle->ready) oracle_compare(oracle, state);
}

static void random_scoring_oracle(void) {
    MusFourGameState *state = malloc(sizeof(*state));
    CHECK(state != NULL);
    static const unsigned int targets[] = {3, 7, 15, 40};
    for (unsigned int trial = 0; trial < 2400; ++trial) {
        MusScoreTable table;
        const unsigned int target = targets[trial % 4];
        CHECK(mus_score_table_init(&table, target) == CFR_STATUS_SUCCESS);
        table.four_player = true;
        MusScoreGame game;
        /* Half the hands begin at zero so the final lances get substantial
         * numeric wager coverage. Others exercise wins during payment. */
        const unsigned int a = trial % 8 < 4 ? 0 : random_number() % target;
        const unsigned int b = trial % 8 < 4 ? 0 : random_number() % target;
        CHECK(mus_score_game_init(&game, &table, a, b) == CFR_STATUS_SUCCESS);
        CHECK(mus_four_score_game_state_init(state, &game) == CFR_STATUS_SUCCESS);
        ScoreOracle oracle = {0};
        size_t steps = 0;
        while (state->model.phase != MUS_FOUR_PHASE_TERMINAL) {
            CHECK(++steps < MUS_FOUR_UNDO_CAPACITY);
            Actor actor;
            Action actions[16];
            size_t count;
            CHECK(cfr_game_current_actor(&game.game, raw(state), &actor) == CFR_STATUS_SUCCESS);
            Action action;
            if (actor.kind == CFR_ACTOR_CHANCE) {
                Probability probabilities[16];
                check_chance(&game, state, actions, probabilities, &count);
                double draw = (double)random_number() / ((double)UINT32_MAX + 1.0);
                size_t index = 0;
                while (index + 1 < count && draw >= probabilities[index])
                    draw -= probabilities[index++];
                action = actions[index];
            } else {
                CHECK(cfr_game_legal_actions(&game.game, raw(state), actions, 16, &count) == CFR_STATUS_SUCCESS);
                Action filtered[16];
                size_t filtered_count = 0;
                for (size_t i = 0; i < count; ++i)
                    if (trial % 6 == 0 || actions[i] != MUS_ACTION_ORDAGO)
                        filtered[filtered_count++] = actions[i];
                CHECK(filtered_count != 0);
                action = filtered[random_number() % filtered_count];
                /* Some policies pass earlier lances, preserving budget for
                 * independent rejection and accepted-bet checks later. */
                if (oracle.ready && (unsigned int)oracle.phase < trial % 5 &&
                    oracle.wagers[oracle.phase].estado == ENVITE_AL_PASO)
                    action = MUS_ACTION_PASS;
            }
            scored_apply(&game, state, &oracle, action);
        }
        CHECK(oracle.ready && oracle.finished);
    }
    for (unsigned int phase = 0; phase < 5; ++phase) {
        CHECK(scoring_coverage.accepted[phase] > 0);
        CHECK(scoring_coverage.rejected[phase] > 0);
    }
    CHECK(scoring_coverage.rejected_raises > 0);
    CHECK(scoring_coverage.rejected_ordagos > 0);
    CHECK(scoring_coverage.accepted_ordagos > 0);
    CHECK(scoring_coverage.final_counts > 0 && scoring_coverage.immediate_wins > 0);
    free(state);
}

static void scored_start(const MusScoreGame *game, MusFourGameState *state,
                          const uint8_t hands[4][8], ScoreOracle *oracle) {
    deal(game, state, hands);
    *oracle = (ScoreOracle){0};
    scored_apply(game, state, oracle, MUS_ACTION_NO_MUS);
}

static void scored_pass_until(const MusScoreGame *game, MusFourGameState *state,
                               ScoreOracle *oracle, Ronda phase) {
    while (!oracle->finished && oracle->phase != phase)
        scored_apply(game, state, oracle, MUS_ACTION_PASS);
    CHECK(!oracle->finished && oracle->phase == phase);
}

static void scored_pass_to_end(const MusScoreGame *game, MusFourGameState *state,
                               ScoreOracle *oracle) {
    while (!oracle->finished) scored_apply(game, state, oracle, MUS_ACTION_PASS);
}

static void scoring_examples(MusScoreTable *table) {
    const uint8_t pairs_hands[4][8] = {
        {1, 2, 0, 1, 0, 0, 0, 0}, /* Pair of fours. */
        {0, 0, 0, 0, 0, 0, 2, 2}, /* Duples beat the bidding team. */
        {1, 0, 0, 3, 0, 0, 0, 0}, /* Medias, adds two intrinsic points. */
        {1, 0, 1, 0, 2, 0, 0, 0}};
    const uint8_t game_hands[4][8] = {
        {0, 0, 1, 0, 1, 0, 0, 2}, /* 32. */
        {1, 0, 0, 0, 0, 1, 1, 1}, /* 31 beats the bidding team. */
        {0, 0, 0, 0, 0, 2, 1, 1}, /* 40, adds two intrinsic points. */
        {0, 0, 0, 0, 2, 0, 0, 2}}; /* 34. */
    const uint8_t seat2_hands[4][8] = {
        {1, 1, 1, 1, 0, 0, 0, 0}, {0, 1, 1, 1, 1, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0, 4}, {0, 0, 1, 1, 1, 1, 0, 0}};
    const uint8_t seat3_hands[4][8] = {
        {1, 1, 1, 1, 0, 0, 0, 0}, {0, 1, 1, 1, 1, 0, 0, 0},
        {0, 0, 1, 1, 1, 1, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 4}};
    const uint8_t tied_hands[4][8] = {
        {0, 0, 0, 0, 0, 0, 0, 4}, {0, 0, 0, 0, 0, 0, 0, 4},
        {4, 0, 0, 0, 0, 0, 0, 0}, {4, 0, 0, 0, 0, 0, 0, 0}};
    MusFourGameState *state = malloc(sizeof(*state));
    CHECK(state != NULL);
    MusScoreGame game;
    CHECK(mus_score_game_init(&game, table, 0, 0) == CFR_STATUS_SUCCESS);
    ScoreOracle oracle;

    /* The stronger team rejects pares. The weaker bidder collects the deje
     * now and its own two members' intrinsic points at the final count. */
    scored_start(&game, state, pairs_hands, &oracle);
    scored_pass_until(&game, state, &oracle, PARES);
    CHECK(oracle_winner(&oracle, PARES) == 1);
    scored_apply(&game, state, &oracle, MUS_ACTION_BET_TWO);
    scored_apply(&game, state, &oracle, MUS_ACTION_REJECT);
    scored_apply(&game, state, &oracle, MUS_ACTION_REJECT);
    scored_pass_to_end(&game, state, &oracle);
    CHECK(oracle.game.tantos[0] == 5 && oracle.game.tantos[1] == 3);

    /* Repeat with a raise: the team holding duples rejects a six-point offer
     * and pays the covered two, rather than a one-point opening deje. */
    scored_start(&game, state, pairs_hands, &oracle);
    scored_pass_until(&game, state, &oracle, PARES);
    scored_apply(&game, state, &oracle, MUS_ACTION_PASS);
    scored_apply(&game, state, &oracle, MUS_ACTION_BET_TWO);
    scored_apply(&game, state, &oracle, MUS_ACTION_BET_FOUR);
    scored_apply(&game, state, &oracle, MUS_ACTION_REJECT);
    scored_apply(&game, state, &oracle, MUS_ACTION_REJECT);
    scored_pass_to_end(&game, state, &oracle);
    CHECK(oracle.game.tantos[0] == 6 && oracle.game.tantos[1] == 3);

    /* In juego, the team with 31 rejects. The bidder still counts the 32 and
     * 40 held by its two members. The losing 31 contributes no points. */
    scored_start(&game, state, game_hands, &oracle);
    scored_pass_until(&game, state, &oracle, JUEGO);
    CHECK(oracle_winner(&oracle, JUEGO) == 1);
    scored_apply(&game, state, &oracle, MUS_ACTION_BET_TWO);
    scored_apply(&game, state, &oracle, MUS_ACTION_REJECT);
    scored_apply(&game, state, &oracle, MUS_ACTION_REJECT);
    CHECK(oracle.finished);
    CHECK(oracle.game.tantos[0] >= 5);

    /* An accepted ordago uses all four hands, including a winner who did
     * not place or accept the bet. Prior collected points stay with teams. */
    const uint8_t (*winner_hands[3])[8] = {seat2_hands, seat3_hands, tied_hands};
    const unsigned int winners[3] = {2, 3, 0};
    for (unsigned int i = 0; i < 3; ++i) {
        CHECK(mus_score_game_init(&game, table, 11, 17) == CFR_STATUS_SUCCESS);
        scored_start(&game, state, winner_hands[i], &oracle);
        CHECK(oracle_winner(&oracle, GRANDE) == (int)winners[i]);
        scored_apply(&game, state, &oracle, MUS_ACTION_ORDAGO);
        scored_apply(&game, state, &oracle, MUS_ACTION_ACCEPT);
        CHECK(oracle.finished && oracle.game.tantos[winners[i] % 2] == 40);
        CHECK(oracle.game.tantos[1 - winners[i] % 2] == (winners[i] % 2 == 0 ? 17 : 11));
    }

    /* With a smaller target, accepted grande ends the final count before
     * team 0 can collect its better chica. The offset keeps the independent
     * simulator's threshold at 40. */
    MusScoreTable short_table;
    CHECK(mus_score_table_init(&short_table, 7) == CFR_STATUS_SUCCESS);
    short_table.four_player = true;
    CHECK(mus_score_game_init(&game, &short_table, 5, 5) == CFR_STATUS_SUCCESS);
    scored_start(&game, state, seat3_hands, &oracle);
    scored_apply(&game, state, &oracle, MUS_ACTION_BET_TWO);
    scored_apply(&game, state, &oracle, MUS_ACTION_ACCEPT);
    scored_pass_to_end(&game, state, &oracle);
    CHECK(oracle.game.tantos[1] == 40 && oracle.game.tantos[0] == 38);

    /* A rejection reaches the target immediately. The opposing team's best
     * grande never receives the point deferred from that lance. */
    CHECK(mus_score_game_init(&game, &short_table, 6, 6) == CFR_STATUS_SUCCESS);
    scored_start(&game, state, seat3_hands, &oracle);
    scored_apply(&game, state, &oracle, MUS_ACTION_BET_TWO);
    scored_apply(&game, state, &oracle, MUS_ACTION_REJECT);
    scored_apply(&game, state, &oracle, MUS_ACTION_REJECT);
    CHECK(oracle.finished && oracle.game.tantos[0] == 40 && oracle.game.tantos[1] == 39);

    CHECK(mus_score_game_init(&game, table, 39, 39) == CFR_STATUS_SUCCESS);
    scored_start(&game, state, seat3_hands, &oracle);
    scored_pass_to_end(&game, state, &oracle);
    CHECK(oracle.game.tantos[1] == 40 && oracle.game.tantos[0] == 39);
    free(state);
}

static void rule_examples(MusScoreTable *table) {
    const uint8_t tied_hands[4][8] = {
        {0, 0, 0, 0, 0, 0, 0, 4}, {0, 0, 0, 0, 0, 0, 0, 4},
        {4, 0, 0, 0, 0, 0, 0, 0}, {4, 0, 0, 0, 0, 0, 0, 0}};
    const uint8_t split_hands[4][8] = {
        {4, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 4},
        {0, 4, 0, 0, 0, 0, 0, 0}, {0, 0, 4, 0, 0, 0, 0, 0}};
    const uint8_t point_hands[4][8] = {
        {1, 1, 1, 1, 0, 0, 0, 0}, {1, 1, 1, 1, 0, 0, 0, 0},
        {1, 1, 1, 1, 0, 0, 0, 0}, {1, 1, 1, 1, 0, 0, 0, 0}};
    MusFourGameState *state = malloc(sizeof(*state));
    CHECK(state != NULL);
    MusScoreGame game;
    CHECK(mus_score_game_init(&game, table, 0, 0) == CFR_STATUS_SUCCESS);
    deal(&game, state, tied_hands);
    apply(&game, state, MUS_ACTION_NO_MUS);
    while (state->model.phase != MUS_FOUR_PHASE_TERMINAL) apply(&game, state, MUS_ACTION_PASS);
    CHECK(state->model.settled_scores[0] == 10 && state->model.settled_scores[1] == 0);

    /* Force every permitted mus and discard all sixteen cards each round.
     * With two muses this exhausts the stock and exercises discard recycling. */
    deal(&game, state, tied_hands);
    unsigned int discarded = 0;
    while (state->model.phase != MUS_FOUR_PHASE_GRANDE) {
        if (state->model.phase == MUS_FOUR_PHASE_MUS) apply(&game, state, MUS_ACTION_MUS);
        else if (state->model.phase == MUS_FOUR_PHASE_DISCARD) {
            apply(&game, state, MUS_ACTION_DISCARD_MASK(15));
            ++discarded;
        } else {
            Action actions[16];
            Probability probabilities[16];
            size_t count = 0;
            CHECK(state->model.phase == MUS_FOUR_PHASE_REDRAW);
            check_chance(&game, state, actions, probabilities, &count);
            apply(&game, state, actions[0]);
        }
    }
    CHECK(discarded == 4 * MUS_GAME_MAX_MUSES);
    CHECK(state->model.mus_count == MUS_GAME_MAX_MUSES);

    deal(&game, state, split_hands);
    apply(&game, state, MUS_ACTION_NO_MUS);
    apply(&game, state, MUS_ACTION_BET_TWO);
    CHECK(state->model.current_player == 1);
    apply(&game, state, MUS_ACTION_REJECT);
    CHECK(state->model.current_player == 3 && state->model.paid_scores[0] == 0);
    apply(&game, state, MUS_ACTION_ACCEPT);
    CHECK(state->model.phase == MUS_FOUR_PHASE_CHICA && state->model.prior_wager_total == 2);
    CHECK(state->model.paid_scores[0] == 0 && state->model.paid_scores[1] == 0);

    deal(&game, state, split_hands);
    apply(&game, state, MUS_ACTION_NO_MUS);
    apply(&game, state, MUS_ACTION_BET_TWO);
    apply(&game, state, MUS_ACTION_REJECT);
    apply(&game, state, MUS_ACTION_REJECT);
    CHECK(state->model.phase == MUS_FOUR_PHASE_CHICA && state->model.paid_scores[0] == 1);

    for (unsigned int accept = 0; accept < 2; ++accept) {
        deal(&game, state, point_hands);
        apply(&game, state, MUS_ACTION_NO_MUS);
        while (state->model.phase != MUS_FOUR_PHASE_PUNTO) apply(&game, state, MUS_ACTION_PASS);
        apply(&game, state, MUS_ACTION_BET_TWO);
        if (accept != 0) apply(&game, state, MUS_ACTION_ACCEPT);
        else {
            apply(&game, state, MUS_ACTION_REJECT);
            apply(&game, state, MUS_ACTION_REJECT);
        }
        CHECK(state->model.phase == MUS_FOUR_PHASE_TERMINAL);
        CHECK(state->model.settled_scores[0] == (accept != 0 ? 5 : 4));
        CHECK(state->model.settled_scores[1] == 0);
    }

    CHECK(mus_score_game_init(&game, table, 39, 39) == CFR_STATUS_SUCCESS);
    deal(&game, state, split_hands);
    apply(&game, state, MUS_ACTION_NO_MUS);
    for (unsigned int p = 0; p < 4; ++p) apply(&game, state, MUS_ACTION_PASS);
    CHECK(state->model.phase == MUS_FOUR_PHASE_CHICA);
    CHECK(state->model.paid_scores[0] == 0 && state->model.paid_scores[1] == 0);
    apply(&game, state, MUS_ACTION_ORDAGO);
    apply(&game, state, MUS_ACTION_ACCEPT);
    CHECK(state->model.phase == MUS_FOUR_PHASE_TERMINAL);
    double utility;
    CHECK(cfr_game_terminal_utility(&game.game, raw(state), CFR_PLAYER_0, &utility) == CFR_STATUS_SUCCESS);
    CHECK(utility == 1); /* Chica ordago overrides pending grande for team 1. */

    deal(&game, state, split_hands);
    apply(&game, state, MUS_ACTION_NO_MUS);
    while (state->model.phase != MUS_FOUR_PHASE_TERMINAL) apply(&game, state, MUS_ACTION_PASS);
    CHECK(cfr_game_terminal_utility(&game.game, raw(state), CFR_PLAYER_0, &utility) == CFR_STATUS_SUCCESS);
    CHECK(utility == -1); /* Final counting starts at grande. */
    free(state);
}

static void error_contracts(MusScoreTable *table) {
    MusScoreGame game;
    CHECK(mus_score_game_init(&game, table, 0, 0) == CFR_STATUS_SUCCESS);
    MusFourGameState *state = calloc(1, sizeof(*state));
    MusFourGameState *before = malloc(sizeof(*before));
    CHECK(state != NULL && before != NULL);
    CHECK(mus_four_score_game_state_init(state, &game) == CFR_STATUS_SUCCESS);
    memcpy(before, state, sizeof(*state));
    CHECK(cfr_game_apply_action(&game.game, raw(state), 999) == CFR_STATUS_ILLEGAL_ACTION);
    CHECK(memcmp(before, state, sizeof(*state)) == 0);
    Action actions[16];
    Probability probabilities[16];
    for (unsigned int i = 0; i < 16; ++i) { actions[i] = 999; probabilities[i] = 99; }
    size_t count = 777;
    CHECK(cfr_game_chance_outcomes(&game.game, raw(state), actions, probabilities,
                                    0, &count) == CFR_STATUS_BUFFER_TOO_SMALL);
    CHECK(count == 8);
    for (unsigned int i = 0; i < 16; ++i) CHECK(actions[i] == 999 && probabilities[i] == 99);
    double value = 123;
    CHECK(cfr_game_terminal_utility(&game.game, raw(state), CFR_PLAYER_0,
                                     &value) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(value == 123);
    CHECK(cfr_game_chance_probability(&game.game, raw(state), 999,
                                       &value) == CFR_STATUS_ILLEGAL_ACTION);
    CHECK(value == 123);
    InfoSetKey key = 123;
    CHECK(cfr_game_information_set_key(&game.game, raw(state), &key) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(key == 123);

    state->undo_count = MUS_FOUR_UNDO_CAPACITY + 1;
    memcpy(before, state, sizeof(*state));
    CHECK(cfr_game_apply_action(&game.game, raw(state), 0) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(before, state, sizeof(*state)) == 0);
    CHECK(mus_four_score_game_state_init(state, &game) == CFR_STATUS_SUCCESS);
    state->model.phase = MUS_FOUR_PHASE_TERMINAL;
    state->model.score_target = 0;
    CHECK(cfr_game_terminal_utility(&game.game, raw(state), CFR_PLAYER_0,
                                     &value) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(value == 123);

    MusFourInformationSet information;
    memset(&information, 0x5a, sizeof(information));
    MusFourInformationSet saved = information;
    CHECK(mus_four_score_information_set_decode(-1, &information) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(&saved, &information, sizeof(saved)) == 0);
    CHECK(mus_four_score_information_set_decode(INT64_MAX, &information) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(&saved, &information, sizeof(saved)) == 0);
    information = (MusFourInformationSet){.phase = MUS_FOUR_PHASE_MUS, .player = 4,
                                         .hand = {4, 0, 0, 0, 0, 0, 0, 0}};
    count = 777;
    CHECK(mus_four_score_information_set_legal_actions(&information, actions, 16,
                                                       &count) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(count == 777 && actions[0] == 999);
    CHECK(mus_four_score_information_set_encode(&information, &key) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(key == 123);

    MusScoreTable restricted;
    CHECK(mus_score_table_init_region(&restricted, 40, 35, 35) == CFR_STATUS_SUCCESS);
    restricted.four_player = true;
    const MusScoreGame saved_game = game;
    CHECK(mus_four_score_game_init(&game, &restricted, 34, 39) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(&game, &saved_game, sizeof(game)) == 0);
    MusGameState *legacy = calloc(1, sizeof(*legacy));
    CHECK(legacy != NULL);
    CHECK(mus_score_game_state_init(legacy, &game) == CFR_STATUS_INVALID_ARGUMENT);
    CHECK(legacy->continuation == NULL && legacy->undo_count == 0);
    free(legacy);
    free(before);
    free(state);
}

int main(void) {
    MusScoreTable table;
    init_table(&table);
    rule_examples(&table);
    random_properties(&table);
    scoring_examples(&table);
    random_scoring_oracle();
    error_contracts(&table);
    puts("Four-player rules and properties passed (400 passive simulator comparisons, 1200 property hands, 2400 scoring replays).");
    return EXIT_SUCCESS;
}
