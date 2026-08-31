#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "cfr/info_node.h"
#include "cfr/info_store.h"
#include "mus_solver/mus_game.h"
#include "mus_solver/mus_strategy.h"

typedef struct {
    FILE *stream;
} StrategyWriter;

static const char *variant_name(TrainerVariant variant) {
    switch (variant) {
    case CFR_TRAINER_VARIANT_CFR:
        return "cfr";
    case CFR_TRAINER_VARIANT_CFR_PLUS:
        return "cfr-plus";
    case CFR_TRAINER_VARIANT_MCCFR_EXTERNAL:
        return "mccfr-external";
    }
    return "unknown";
}

static Status write_node(const InfoNode *node, void *raw_context) {
    StrategyWriter *writer = raw_context;
    MusInformationSet information;
    Probability strategy[2];
    const char *action_0;
    const char *action_1;
    Status status;

    if (node == NULL || writer == NULL || writer->stream == NULL ||
        node->action_count != 2) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    status = mus_game_information_set_decode(node->key, &information);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    status = cfr_info_node_average_strategy(node, strategy, 2);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (information.bet_stage == MUS_BET_RESPONSE) {
        action_0 = "no_querer";
        action_1 = "querer";
    } else {
        action_0 = "pasar";
        action_1 = "envidar_2";
    }

    if (fprintf(writer->stream,
                "infoset key=%" PRId64 " jugador=%d posicion=%s mano=",
                node->key, (int)information.player,
                information.player == CFR_PLAYER_0 ? "mano" : "postre") < 0)
        return CFR_STATUS_IO_ERROR;
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT;
         card_class += 1) {
        if (card_class > 0 && fputc(',', writer->stream) == EOF)
            return CFR_STATUS_IO_ERROR;
        if (fprintf(writer->stream, "%s:%u",
                    mus_game_class_name((MusCardClass)card_class),
                    information.hand[card_class]) < 0)
            return CFR_STATUS_IO_ERROR;
    }
    if (fprintf(writer->stream, " lance=%s pares=",
                mus_game_phase_name(information.phase)) < 0)
        return CFR_STATUS_IO_ERROR;
    if (!information.pairs_revealed) {
        if (fputs("ocultos", writer->stream) == EOF)
            return CFR_STATUS_IO_ERROR;
    } else if (fprintf(writer->stream, "%d/%d", information.pairs[0],
                       information.pairs[1]) < 0) {
        return CFR_STATUS_IO_ERROR;
    }
    if (fputs(" juego=", writer->stream) == EOF)
        return CFR_STATUS_IO_ERROR;
    if (!information.game_revealed) {
        if (fputs("oculto", writer->stream) == EOF)
            return CFR_STATUS_IO_ERROR;
    } else if (fprintf(writer->stream, "%d/%d", information.game[0],
                       information.game[1]) < 0) {
        return CFR_STATUS_IO_ERROR;
    }
    if (fputs(" historial=", writer->stream) == EOF)
        return CFR_STATUS_IO_ERROR;
    for (int lance = 0; lance < MUS_GAME_LANCE_COUNT; lance += 1) {
        if (lance > 0 && fputc(',', writer->stream) == EOF)
            return CFR_STATUS_IO_ERROR;
        if (fputs(mus_game_outcome_name(information.outcomes[lance]),
                  writer->stream) == EOF)
            return CFR_STATUS_IO_ERROR;
    }
    if (fprintf(writer->stream, " actual=%s %s=%.17g %s=%.17g\n",
                information.bet_stage == MUS_BET_FIRST_TO_ACT
                    ? "inicio"
                    : information.bet_stage == MUS_BET_SECOND_AFTER_PASS
                          ? "paso_mano"
                          : information.bettor == CFR_PLAYER_0
                                ? "envite_mano"
                                : "paso_mano-envite_postre",
                action_0, strategy[0], action_1, strategy[1]) < 0) {
        return CFR_STATUS_IO_ERROR;
    }
    return CFR_STATUS_SUCCESS;
}

Status mus_strategy_write_text(FILE *stream, const Trainer *trainer) {
    StrategyWriter writer = {.stream = stream};
    InfoStoreStats stats;
    Status status;

    if (stream == NULL || trainer == NULL || trainer->game == NULL ||
        trainer->store == NULL ||
        trainer->game != mus_game_descriptor()) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    status = cfr_info_store_get_stats(trainer->store, &stats);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (fprintf(stream,
                "mus-strategy version=1 schema=%s variant=%s "
                "training_iterations=%zu information_sets=%zu\n",
                trainer->game->strategy_schema_id,
                variant_name(trainer->variant), trainer->training_iterations,
                stats.size) < 0) {
        return CFR_STATUS_IO_ERROR;
    }
    status = cfr_info_store_visit_sorted(trainer->store, write_node, &writer);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    return ferror(stream) ? CFR_STATUS_IO_ERROR : CFR_STATUS_SUCCESS;
}
