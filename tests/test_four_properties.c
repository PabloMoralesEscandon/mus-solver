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
 * current_wager, previous_wager, actor, bidder, wager masks or cached hand
 * summaries to derive its expectations. Simulador-Mus provides team-level
 * scoring; this reference additionally records the proposals and their seats
 * to check the individual turn and allowance rules. An offset converts the
 * simulator's fixed target of 40 into the requested match target. */
typedef struct {
    PartidaMus game;
    Carta cards[4][4];
    EnviteMus wagers[5];
    unsigned int passes;
    unsigned int responses;
    Action proposals[4];
    unsigned int current_player;
    unsigned int bettor;
    unsigned int offers;
    uint8_t used_wager_action_masks[4];
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
    unsigned int resolved_responses[7];
    unsigned int tied_responses;
    unsigned int first_proposal_wins;
    unsigned int second_proposal_wins;
    unsigned int different_allowances;
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

static unsigned int oracle_next(unsigned int eligible, unsigned int after) {
    CHECK(eligible != 0);
    for (unsigned int distance = 1; distance <= 4; ++distance) {
        const unsigned int seat = (after + distance) % 4;
        if ((eligible & (1U << seat)) != 0) return seat;
    }
    CHECK(false);
    return 0;
}

static void oracle_start_lance(ScoreOracle *oracle, Ronda phase) {
    oracle->phase = phase;
    oracle->passes = oracle->responses = oracle->offers = oracle->bettor = 0;
    memset(oracle->used_wager_action_masks, 0, sizeof(oracle->used_wager_action_masks));
    const unsigned int eligible = oracle_eligible(oracle, phase);
    if ((eligible & 5U) != 0 && (eligible & 10U) != 0)
        oracle->current_player = oracle_next(eligible, 3);
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
    oracle->passes = oracle->responses = 0;
    if (oracle_match_finished(oracle)) {
        ++scoring_coverage.immediate_wins;
        oracle->finished = true;
        return;
    }
    switch (oracle->phase) {
    case GRANDE: oracle_start_lance(oracle, CHICA); return;
    case CHICA: oracle_start_lance(oracle, PARES); break;
    case PARES: oracle_start_lance(oracle, JUEGO); break;
    case JUEGO:
    case PUNTO: oracle_count(oracle); return;
    }
    if (oracle->phase == PARES) {
        const int team0 = equipoTienePares(oracle->game.manos, 4, 0);
        const int team1 = equipoTienePares(oracle->game.manos, 4, 1);
        CHECK(team0 >= 0 && team1 >= 0);
        if (team0 && team1) return;
        oracle_start_lance(oracle, JUEGO);
    }
    const int team0 = equipoTieneJuego(oracle->game.manos, 4, 0);
    const int team1 = equipoTieneJuego(oracle->game.manos, 4, 1);
    CHECK(team0 >= 0 && team1 >= 0);
    if (team0 && team1) return;
    if (!team0 && !team1) oracle_start_lance(oracle, PUNTO);
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
    oracle_start_lance(oracle, GRANDE);
}

static unsigned int oracle_response_owner(const ScoreOracle *oracle) {
    /* Search the complete pair from strongest to weakest. For a tie, the
     * first eligible seat clockwise from the bidder owns the executed action.
     * This does not reuse the solver's incremental pending-response merge. */
    static const Action priority[] = {MUS_ACTION_ORDAGO, MUS_ACTION_BET_TEN,
        MUS_ACTION_BET_FOUR, MUS_ACTION_BET_TWO, MUS_ACTION_ACCEPT, MUS_ACTION_REJECT};
    for (size_t i = 0; i < sizeof(priority) / sizeof(priority[0]); ++i) {
        for (unsigned int distance = 1; distance <= 4; ++distance) {
            const unsigned int seat = (oracle->bettor + distance) % 4;
            if ((oracle->responses & (1U << seat)) != 0 &&
                oracle->proposals[seat] == priority[i]) return seat;
        }
    }
    CHECK(false);
    return 0;
}

static void oracle_action(ScoreOracle *oracle, Action action) {
    unsigned int seat = oracle->current_player;
    CHECK(oracle->ready && !oracle->finished && seat < 4);
    const Ronda phase = oracle->phase;
    EnviteMus *wager = &oracle->wagers[phase];
    const unsigned int eligible = oracle_eligible(oracle, phase);
    CHECK((eligible & (1U << seat)) != 0);
    const int team = (int)(seat % 2);
    if (wager->estado == ENVITE_PENDIENTE || wager->estado == ORDAGO_PENDIENTE) {
        CHECK(team != wager->parejaApostadora);
        CHECK((oracle->responses & (1U << seat)) == 0);
        oracle->proposals[seat] = action;
        oracle->responses |= 1U << seat;
        const unsigned int team_members = eligible & (team == 0 ? 5U : 10U);
        if (oracle->responses != team_members) {
            oracle->current_player = oracle_next(team_members & ~oracle->responses, seat);
            return;
        }
        seat = oracle_response_owner(oracle);
        action = oracle->proposals[seat];
        CHECK(oracle->offers > 0 && oracle->offers < 7);
        ++scoring_coverage.resolved_responses[oracle->offers];
        if (team_members == 5U || team_members == 10U) {
            const unsigned int first = oracle_next(team_members, oracle->bettor);
            const unsigned int second = first ^ 2U;
            scoring_coverage.tied_responses += oracle->proposals[first] == oracle->proposals[second];
            scoring_coverage.first_proposal_wins += seat == first;
            scoring_coverage.second_proposal_wins += seat == second;
            scoring_coverage.different_allowances +=
                oracle->used_wager_action_masks[first] != oracle->used_wager_action_masks[second];
        }
        oracle->responses = 0;
    }
    if (action == MUS_ACTION_PASS) {
        CHECK(wager->estado == ENVITE_AL_PASO);
        oracle->passes |= 1U << seat;
        if (oracle->passes == eligible) {
            CHECK(registrarEnviteMus(&oracle->game, phase, wager) == 0);
            oracle_advance(oracle);
        } else oracle->current_player = oracle_next(eligible & ~oracle->passes, seat);
        return;
    }
    if (action == MUS_ACTION_REJECT) {
        CHECK(wager->estado == ENVITE_PENDIENTE || wager->estado == ORDAGO_PENDIENTE);
        CHECK(team != wager->parejaApostadora);
        CHECK(noQuererEnviteMus(wager, team) == 0);
        ++scoring_coverage.rejected[phase];
        scoring_coverage.rejected_raises += wager->cantidadAnterior > 1;
        scoring_coverage.rejected_ordagos += wager->estado == ORDAGO_RECHAZADO;
        CHECK(registrarEnviteMus(&oracle->game, phase, wager) >= 0);
        oracle_advance(oracle);
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
    if (action == MUS_ACTION_ORDAGO) {
        CHECK(ordagoMus(wager, team) == 0);
        oracle->bettor = seat;
        ++oracle->offers;
        oracle->current_player = oracle_next(eligible & (team == 0 ? 10U : 5U), seat);
        return;
    }
    int increment = 0;
    unsigned int allowance = 0;
    switch (action) {
    case MUS_ACTION_BET_TWO: increment = 2; allowance = 1; break;
    case MUS_ACTION_BET_FOUR: increment = 4; allowance = 2; break;
    case MUS_ACTION_BET_SEVEN: increment = 7; allowance = 4; break;
    case MUS_ACTION_BET_TEN: increment = 10; allowance = 8; break;
    default: CHECK(false);
    }
    const int total = (wager->estado == ENVITE_AL_PASO ? 0 : wager->cantidad) + increment;
    CHECK(envidarMus(wager, team, total) == 0);
    CHECK((oracle->used_wager_action_masks[seat] & allowance) == 0);
    oracle->used_wager_action_masks[seat] |= (uint8_t)allowance;
    oracle->bettor = seat;
    ++oracle->offers;
    oracle->current_player = oracle_next(eligible & (team == 0 ? 10U : 5U), seat);
}

static void oracle_compare(const ScoreOracle *oracle, const MusFourGameState *state) {
    CHECK(oracle->finished == (state->model.phase == MUS_FOUR_PHASE_TERMINAL));
    if (!oracle->finished) CHECK(oracle->phase == simulator_phase(state->model.phase));
    CHECK(state->model.current_player == oracle->current_player);
    CHECK(state->model.bettor == oracle->bettor);
    CHECK(memcmp(state->model.used_wager_action_masks, oracle->used_wager_action_masks,
                 sizeof(oracle->used_wager_action_masks)) == 0);
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
        oracle_action(oracle, action);
    }
    apply(game, state, action);
    if (!oracle->ready && state->model.phase == MUS_FOUR_PHASE_GRANDE)
        oracle_initialize(oracle, game, state);
    if (oracle->ready) oracle_compare(oracle, state);
}

static void random_scoring_oracle(void) {
    /* Coverage below must come from the randomized replay itself. */
    scoring_coverage = (ScoringCoverage){0};
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
    CHECK(scoring_coverage.resolved_responses[1] > 0);
    CHECK(scoring_coverage.resolved_responses[2] > 0);
    CHECK(scoring_coverage.resolved_responses[3] > 0);
    CHECK(scoring_coverage.tied_responses > 0);
    CHECK(scoring_coverage.first_proposal_wins > 0);
    CHECK(scoring_coverage.second_proposal_wins > 0);
    CHECK(scoring_coverage.different_allowances > 0);
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
        CHECK(!oracle.finished);
        scored_apply(&game, state, &oracle, MUS_ACTION_REJECT);
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
    scored_apply(&game, state, &oracle, MUS_ACTION_REJECT);
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
        if (accept != 0) {
            apply(&game, state, MUS_ACTION_ACCEPT);
            apply(&game, state, MUS_ACTION_REJECT);
        } else {
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
    CHECK(state->model.phase == MUS_FOUR_PHASE_CHICA);
    apply(&game, state, MUS_ACTION_REJECT);
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

static bool menu_has(const MusScoreGame *game, MusFourGameState *state, Action wanted) {
    Action actions[16];
    size_t count = 0;
    CHECK(cfr_game_legal_actions(&game->game, raw(state), actions, 16, &count) == CFR_STATUS_SUCCESS);
    for (size_t i = 0; i < count; ++i)
        if (actions[i] == wanted) return true;
    return false;
}

static void chained_response_examples(MusScoreTable *table) {
    static const uint8_t hands[4][8] = {
        {4,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,4},
        {0,4,0,0,0,0,0,0}, {0,0,4,0,0,0,0,0}};
    typedef struct {
        unsigned int seat;
        Action action;
        unsigned int bettor, next, wager;
        uint8_t masks[4];
    } ReplyStep;
    /* Literal expected seats and allowance masks make these independent
     * fixtures a check on both the production merge and the oracle's merge.
     * The second offer gets unequal replies; the third gets a tie. After the
     * fifth numeric offer, an ordago still requires both teammates' replies. */
    static const ReplyStep second_higher[] = {
        {0, MUS_ACTION_BET_TWO,  0, 1,  2, { 1,0,0,0}},
        {1, MUS_ACTION_BET_TWO,  0, 3,  2, { 1,0,0,0}},
        {3, MUS_ACTION_BET_TWO,  1, 2,  4, { 1,1,0,0}},
        {2, MUS_ACTION_BET_TWO,  1, 0,  4, { 1,1,0,0}},
        {0, MUS_ACTION_BET_FOUR, 0, 1,  8, { 3,1,0,0}},
        {1, MUS_ACTION_BET_FOUR, 0, 3,  8, { 3,1,0,0}},
        {3, MUS_ACTION_BET_FOUR, 1, 2, 12, { 3,3,0,0}},
        {2, MUS_ACTION_BET_FOUR, 1, 0, 12, { 3,3,0,0}},
        {0, MUS_ACTION_BET_TEN,  0, 1, 22, {11,3,0,0}},
        {1, MUS_ACTION_ORDAGO,   0, 3, 22, {11,3,0,0}},
        {3, MUS_ACTION_ACCEPT,   1, 2, 40, {11,3,0,0}},
        {2, MUS_ACTION_REJECT,   1, 0, 40, {11,3,0,0}},
        {0, MUS_ACTION_ACCEPT,   1, 0, 40, {11,3,0,0}}
    };
    /* Rotate the opening seat. The second offer gets a tie; the third gets
     * ordago followed by a smaller numeric proposal. In both cases the first
     * responding seat owns the executed action and determines who goes next. */
    static const ReplyStep first_higher[] = {
        {0, MUS_ACTION_PASS,     0, 1,  0, {0,0,0,0}},
        {1, MUS_ACTION_BET_TWO,  1, 2,  2, {0,1,0,0}},
        {2, MUS_ACTION_BET_TWO,  1, 0,  2, {0,1,0,0}},
        {0, MUS_ACTION_BET_FOUR, 0, 1,  6, {2,1,0,0}},
        {1, MUS_ACTION_BET_FOUR, 0, 3,  6, {2,1,0,0}},
        {3, MUS_ACTION_BET_FOUR, 1, 2, 10, {2,3,0,0}},
        {2, MUS_ACTION_ORDAGO,   1, 0, 10, {2,3,0,0}},
        {0, MUS_ACTION_BET_TEN,  2, 3, 40, {2,3,0,0}},
        {3, MUS_ACTION_ACCEPT,   2, 1, 40, {2,3,0,0}},
        {1, MUS_ACTION_REJECT,   2, 1, 40, {2,3,0,0}}
    };
    const ReplyStep *scripts[] = {second_higher, first_higher};
    const size_t counts[] = {sizeof(second_higher) / sizeof(second_higher[0]),
                             sizeof(first_higher) / sizeof(first_higher[0])};
    MusFourGameState *state = malloc(sizeof(*state));
    CHECK(state != NULL);
    MusScoreGame game;
    CHECK(mus_score_game_init(&game, table, 0, 0) == CFR_STATUS_SUCCESS);
    for (unsigned int script = 0; script < 2; ++script) {
        ScoreOracle oracle;
        scored_start(&game, state, hands, &oracle);
        for (size_t step = 0; step < counts[script]; ++step) {
            const ReplyStep *reply = &scripts[script][step];
            CHECK(oracle.current_player == reply->seat);
            scored_apply(&game, state, &oracle, reply->action);
            CHECK(oracle.bettor == reply->bettor);
            CHECK(oracle.current_player == reply->next);
            CHECK(memcmp(oracle.used_wager_action_masks, reply->masks, sizeof(reply->masks)) == 0);
            CHECK(state->model.current_wager == reply->wager);
            CHECK(oracle.finished == (step + 1 == counts[script]));
            if (script == 0 && step == 2) CHECK(menu_has(&game, state, MUS_ACTION_BET_TWO));
            if (script == 0 && step == 3) {
                CHECK(!menu_has(&game, state, MUS_ACTION_BET_TWO));
                CHECK(menu_has(&game, state, MUS_ACTION_BET_FOUR));
            }
            if (script == 0 && step == 6) CHECK(menu_has(&game, state, MUS_ACTION_BET_FOUR));
            if (script == 0 && step == 7) {
                CHECK(!menu_has(&game, state, MUS_ACTION_BET_FOUR));
                CHECK(menu_has(&game, state, MUS_ACTION_BET_TEN));
            }
        }
    }
    free(state);
}

static void joint_response_examples(MusScoreTable *table) {
    static const uint8_t hands[4][8] = {
        {4,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,4},
        {0,4,0,0,0,0,0,0}, {0,0,4,0,0,0,0,0}};
    static const Action responses[] = {MUS_ACTION_REJECT, MUS_ACTION_ACCEPT,
        MUS_ACTION_BET_TWO, MUS_ACTION_BET_FOUR, MUS_ACTION_BET_TEN, MUS_ACTION_ORDAGO};
    static const uint8_t pending[] = {MUS_FOUR_RESPONSE_REJECT, MUS_FOUR_RESPONSE_ACCEPT,
        MUS_FOUR_RESPONSE_BET_TWO, MUS_FOUR_RESPONSE_BET_FOUR,
        MUS_FOUR_RESPONSE_BET_TEN, MUS_FOUR_RESPONSE_ORDAGO};
    static const unsigned int amounts[] = {0, 0, 2, 4, 10, 40};
    static const unsigned int masks[] = {0, 0, 1, 2, 8, 0};
    MusFourGameState *state = malloc(sizeof(*state));
    CHECK(state != NULL);
    MusScoreGame game;
    CHECK(mus_score_game_init(&game, table, 0, 0) == CFR_STATUS_SUCCESS);

    /* All 36 ordered response pairs, for each possible opening seat. A
     * pending proposal must preserve the original offer and both players'
     * wager allowances, even when the first player already said ordago. */
    for (unsigned int opener = 0; opener < 4; ++opener) {
        for (unsigned int first = 0; first < 6; ++first) {
            for (unsigned int second = 0; second < 6; ++second) {
                deal(&game, state, hands);
                apply(&game, state, MUS_ACTION_NO_MUS);
                for (unsigned int p = 0; p < opener; ++p) apply(&game, state, MUS_ACTION_PASS);
                apply(&game, state, MUS_ACTION_BET_TWO);
                const MusFourGameModel opening = state->model;
                const unsigned int first_seat = (opener + 1) % 4;
                const unsigned int second_seat = (opener + 3) % 4;
                CHECK(state->model.current_player == first_seat);
                apply(&game, state, responses[first]);
                CHECK(state->model.phase == MUS_FOUR_PHASE_GRANDE);
                CHECK(state->model.current_player == second_seat);
                CHECK(state->model.pending_response == pending[first]);
                CHECK(state->model.bettor == opener);
                CHECK(state->model.current_wager == 2 && state->model.previous_wager == 1);
                CHECK(!state->model.current_wager_is_ordago);
                CHECK(state->model.prior_wager_total == 0);
                CHECK(state->model.paid_scores[0] == 0 && state->model.paid_scores[1] == 0);
                CHECK(memcmp(state->model.used_wager_action_masks,
                             opening.used_wager_action_masks,
                             sizeof(opening.used_wager_action_masks)) == 0);
                for (unsigned int r = 0; r < 6; ++r) CHECK(menu_has(&game, state, responses[r]));
                CHECK(!menu_has(&game, state, MUS_ACTION_PASS));
                CHECK(!menu_has(&game, state, MUS_ACTION_BET_SEVEN));
                MusFourInformationSet observed, decoded;
                InfoSetKey key;
                CHECK(mus_four_score_game_observe(state, &observed) == CFR_STATUS_SUCCESS);
                CHECK(observed.pending_response == pending[first]);
                CHECK(cfr_game_information_set_key(&game.game, raw(state), &key) == CFR_STATUS_SUCCESS);
                CHECK(mus_four_score_information_set_decode(key, &decoded) == CFR_STATUS_SUCCESS);
                CHECK(decoded.pending_response == pending[first]);
                const MusFourGameModel proposal = state->model;
                apply(&game, state, responses[second]);
                CHECK(state->model.pending_response == MUS_FOUR_RESPONSE_NONE);
                const unsigned int best = first >= second ? first : second;
                const unsigned int bidder = first >= second ? first_seat : second_seat;
                if (best == 0) {
                    CHECK(state->model.phase == MUS_FOUR_PHASE_CHICA);
                    CHECK(state->model.paid_scores[opener % 2] == 1);
                } else if (best == 1) {
                    CHECK(state->model.phase == MUS_FOUR_PHASE_CHICA);
                    CHECK(state->model.prior_wager_total == 2);
                } else {
                    CHECK(state->model.phase == MUS_FOUR_PHASE_GRANDE);
                    CHECK(state->model.bettor == bidder);
                    CHECK(state->model.current_player == (bidder + 1) % 4);
                    CHECK(state->model.previous_wager == 2);
                    CHECK(state->model.current_wager == (best == 5 ? 40 : 2 + amounts[best]));
                    CHECK(state->model.current_wager_is_ordago == (best == 5));
                    uint8_t expected_masks[4];
                    memcpy(expected_masks, opening.used_wager_action_masks, sizeof(expected_masks));
                    expected_masks[bidder] |= (uint8_t)masks[best];
                    CHECK(memcmp(state->model.used_wager_action_masks,
                                 expected_masks, sizeof(expected_masks)) == 0);
                }
                CHECK(cfr_game_undo_action(&game.game, raw(state)) == CFR_STATUS_SUCCESS);
                CHECK(memcmp(&state->model, &proposal, sizeof(proposal)) == 0);
                CHECK(cfr_game_undo_action(&game.game, raw(state)) == CFR_STATUS_SUCCESS);
                CHECK(memcmp(&state->model, &opening, sizeof(opening)) == 0);
            }
        }
        for (unsigned int first = 0; first < 2; ++first) {
            for (unsigned int second = 0; second < 2; ++second) {
                deal(&game, state, hands);
                apply(&game, state, MUS_ACTION_NO_MUS);
                for (unsigned int p = 0; p < opener; ++p) apply(&game, state, MUS_ACTION_PASS);
                apply(&game, state, MUS_ACTION_ORDAGO);
                apply(&game, state, responses[first]);
                CHECK(state->model.phase == MUS_FOUR_PHASE_GRANDE);
                CHECK(state->model.current_player == (opener + 3) % 4);
                CHECK(menu_has(&game, state, MUS_ACTION_REJECT));
                CHECK(menu_has(&game, state, MUS_ACTION_ACCEPT));
                CHECK(!menu_has(&game, state, MUS_ACTION_ORDAGO));
                apply(&game, state, responses[second]);
                CHECK(state->model.phase == (first != 0 || second != 0
                      ? MUS_FOUR_PHASE_TERMINAL : MUS_FOUR_PHASE_CHICA));
            }
        }
    }

    /* Four successive +2 offers consume one allowance per seat. Both
     * teammates can still propose +4 for the fifth offer; only the winning
     * proposal consumes that final shared slot and its owner's allowance. */
    deal(&game, state, hands);
    apply(&game, state, MUS_ACTION_NO_MUS);
    apply(&game, state, MUS_ACTION_BET_TWO);
    for (unsigned int bidder = 1; bidder < 4; ++bidder) {
        CHECK(state->model.current_player == bidder);
        apply(&game, state, MUS_ACTION_BET_TWO);
        apply(&game, state, MUS_ACTION_REJECT);
        CHECK(state->model.bettor == bidder);
    }
    CHECK(state->model.current_wager == 8 && state->model.current_player == 0);
    apply(&game, state, MUS_ACTION_BET_FOUR);
    CHECK(menu_has(&game, state, MUS_ACTION_BET_FOUR));
    apply(&game, state, MUS_ACTION_BET_FOUR);
    CHECK(state->model.current_wager == 12 && state->model.bettor == 0);
    CHECK(state->model.used_wager_action_masks[0] == 3);
    CHECK(state->model.used_wager_action_masks[2] == 1);
    CHECK(!menu_has(&game, state, MUS_ACTION_BET_FOUR));
    CHECK(!menu_has(&game, state, MUS_ACTION_BET_TEN));
    CHECK(menu_has(&game, state, MUS_ACTION_ORDAGO));
    free(state);
}

static void single_eligible_response_examples(MusScoreTable *table) {
    static const uint8_t fixtures[2][4][8] = {
        {{2,1,1,0,0,0,0,0}, {0,0,0,0,1,1,0,2},
         {1,1,1,1,0,0,0,0}, {1,1,1,1,0,0,0,0}},
        {{1,0,0,0,0,1,1,1}, {0,0,1,0,1,0,0,2},
         {1,1,1,1,0,0,0,0}, {1,1,1,1,0,0,0,0}}
    };
    static const unsigned int phases[] = {MUS_FOUR_PHASE_PARES, MUS_FOUR_PHASE_JUEGO};
    static const Action responses[] = {MUS_ACTION_REJECT, MUS_ACTION_ACCEPT,
        MUS_ACTION_BET_TWO, MUS_ACTION_BET_FOUR, MUS_ACTION_BET_TEN, MUS_ACTION_ORDAGO};
    MusFourGameState *state = malloc(sizeof(*state));
    CHECK(state != NULL);
    MusScoreGame game;
    CHECK(mus_score_game_init(&game, table, 0, 0) == CFR_STATUS_SUCCESS);
    for (unsigned int fixture = 0; fixture < 2; ++fixture) {
        for (unsigned int r = 0; r < 6; ++r) {
            deal(&game, state, fixtures[fixture]);
            apply(&game, state, MUS_ACTION_NO_MUS);
            while (state->model.phase < phases[fixture]) apply(&game, state, MUS_ACTION_PASS);
            CHECK(state->model.phase == phases[fixture] && state->model.eligible_mask == 3);
            apply(&game, state, MUS_ACTION_BET_TWO);
            CHECK(state->model.current_player == 1);
            apply(&game, state, responses[r]);
            CHECK(state->model.pending_response == MUS_FOUR_RESPONSE_NONE);
            if (r < 2) CHECK(state->model.phase != phases[fixture]);
            else {
                CHECK(state->model.phase == phases[fixture]);
                CHECK(state->model.bettor == 1 && state->model.current_player == 0);
                apply(&game, state, MUS_ACTION_ACCEPT);
                CHECK(state->model.phase != phases[fixture]);
            }
        }
    }
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
    joint_response_examples(&table);
    chained_response_examples(&table);
    single_eligible_response_examples(&table);
    random_properties(&table);
    scoring_examples(&table);
    random_scoring_oracle();
    error_contracts(&table);
    puts("Four-player rules and properties passed (400 passive simulator comparisons, 1200 property hands, 2400 scoring replays).");
    return EXIT_SUCCESS;
}
