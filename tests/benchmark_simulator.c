/* Build with the simulator sources and -O2 -lm. Arguments:
 * players(2|4) complete_matches(0|1) betting(0|1) repetitions. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "mus_log.h"
#include "mus_sim.h"

static int cut_mus(const Mano *hand, int player, int mano,
                   const int scores[2], void *context) {
    (void)hand;
    (void)player;
    (void)mano;
    (void)scores;
    (void)context;
    return 0;
}

static int no_discard(const Mano *hand, int player, int cards[4], void *context) {
    (void)hand;
    (void)player;
    (void)cards;
    (void)context;
    return 1;
}

static AccionEnviteMus fixed_bet(const Mano *hand, int player, int mano,
    const int scores[2], Ronda phase, const EnviteMus *wager, void *context) {
    (void)hand;
    (void)player;
    (void)mano;
    (void)scores;
    (void)context;
    if (wager->estado == ENVITE_AL_PASO)
        return (AccionEnviteMus){.tipo = ACCION_ENVIDAR, .cantidadTotal = 2};
    return (AccionEnviteMus){.tipo = phase == CHICA
        ? ACCION_NO_QUERER : ACCION_QUERER};
}

int main(int argc, char **argv) {
    if (argc != 5) return 2;
    const int players = atoi(argv[1]);
    const int complete_matches = atoi(argv[2]);
    const int betting = atoi(argv[3]);
    const int repetitions = atoi(argv[4]);
    if ((players != 2 && players != 4) || repetitions <= 0 ||
        (complete_matches != 0 && complete_matches != 1) ||
        (betting != 0 && betting != 1)) return 2;
    PartidaMus game = {0};
    if (iniciarPartidaMusConJugadores(&game, players)) return 3;
    EstrategiaMus strategies[4] = {{0}};
    for (int i = 0; i < players; i++)
        strategies[i] = (EstrategiaMus){cut_mus, no_discard, fixed_bet, NULL};
    srand(7);
    fijarNivelLog(LOG_SILENCIO);
    uint64_t checksum = 0;
    uint64_t rounds = 0;
    for (int i = 0; i < repetitions; i++) {
        game.tantos[0] = game.tantos[1] = 0;
        game.mano = i % players;
        int result;
        do {
            result = betting ? simularRondaMusConEstrategias(&game, strategies)
                             : simularRondaMus(&game);
            rounds++;
            if (result < 0) return 4;
        } while (complete_matches && !result);
        checksum = checksum * UINT64_C(1099511628211) +
            (unsigned)(game.tantos[0] * 41 + game.tantos[1] + result * 1681);
    }
    printf("{\"rounds\":%llu,\"checksum\":%llu}\n",
           (unsigned long long)rounds, (unsigned long long)checksum);
    return destruirPartidaMus(&game);
}
