#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "cfr/info_node.h"
#include "cfr/info_store.h"
#include "mus_solver/mus_game.h"
#include "mus_solver/mus_strategy.h"

typedef struct {
    FILE *stream;
    bool raises_to_eight;
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
    Probability strategy[3];
    const char *action_0;
    const char *action_1;
    const char *action_2 = NULL;
    const char *current_history;
    size_t expected_action_count;
    Status status;

    if (node == NULL || writer == NULL || writer->stream == NULL) {
        return CFR_STATUS_INVALID_ARGUMENT;
    }
    status = writer->raises_to_eight
                 ? mus_raises_to_eight_game_information_set_decode(
                       node->key, &information)
                 : mus_single_bet_game_information_set_decode(
                       node->key, &information);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    expected_action_count =
        writer->raises_to_eight &&
                information.bet_stage == MUS_BET_RESPONSE &&
                information.current_wager < MUS_RAISES_TO_EIGHT_MAX_WAGER
            ? 3
            : 2;
    if (node->action_count != expected_action_count)
        return CFR_STATUS_INVALID_ARGUMENT;
    status = cfr_info_node_average_strategy(node, strategy, 3);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (information.bet_stage == MUS_BET_RESPONSE) {
        action_0 = "no_querer";
        action_1 = "querer";
        if (expected_action_count == 3) {
            action_2 = information.current_wager == 2 ? "subir_a_4"
                                                      : "subir_a_8";
        }
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
        const char *outcome_name =
            writer->raises_to_eight
                ? mus_raises_to_eight_game_outcome_name(
                      information.outcomes[lance],
                      information.outcome_wagers[lance])
                : mus_single_bet_game_outcome_name(
                      information.outcomes[lance]);

        if (fputs(outcome_name, writer->stream) == EOF)
            return CFR_STATUS_IO_ERROR;
    }
    if (information.bet_stage == MUS_BET_FIRST_TO_ACT) {
        current_history = "inicio";
    } else if (information.bet_stage == MUS_BET_SECOND_AFTER_PASS) {
        current_history = "paso_mano";
    } else if (!writer->raises_to_eight) {
        current_history = information.bettor == CFR_PLAYER_0
                              ? "envite_mano"
                              : "paso_mano-envite_postre";
    } else if (information.bettor == CFR_PLAYER_0) {
        current_history = information.current_wager == 2
                              ? "envite_2_mano"
                          : information.current_wager == 4
                              ? "paso-envite_2_postre-subida_4_mano"
                              : "envite_2_mano-subida_4_postre-subida_8_mano";
    } else {
        current_history = information.current_wager == 2
                              ? "paso-envite_2_postre"
                          : information.current_wager == 4
                              ? "envite_2_mano-subida_4_postre"
                              : "paso-envite_2_postre-subida_4_mano-subida_8_postre";
    }
    if (fprintf(writer->stream, " actual=%s %s=%.17g %s=%.17g",
                current_history, action_0, strategy[0], action_1,
                strategy[1]) < 0) {
        return CFR_STATUS_IO_ERROR;
    }
    if (action_2 != NULL &&
        fprintf(writer->stream, " %s=%.17g", action_2, strategy[2]) < 0)
        return CFR_STATUS_IO_ERROR;
    if (fputc('\n', writer->stream) == EOF)
        return CFR_STATUS_IO_ERROR;
    return CFR_STATUS_SUCCESS;
}

static Status write_strategy(FILE *stream, const Trainer *trainer,
                             const Game *expected_game,
                             bool raises_to_eight) {
    StrategyWriter writer = {
        .stream = stream,
        .raises_to_eight = raises_to_eight,
    };
    InfoStoreStats stats;
    Status status;

    if (stream == NULL || trainer == NULL || trainer->game == NULL ||
        trainer->store == NULL || trainer->game != expected_game) {
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

Status mus_single_bet_strategy_write_text(FILE *stream,
                                          const Trainer *trainer) {
    return write_strategy(stream, trainer, mus_single_bet_game_descriptor(),
                          false);
}

Status mus_raises_to_eight_strategy_write_text(FILE *stream,
                                               const Trainer *trainer) {
    return write_strategy(stream, trainer,
                          mus_raises_to_eight_game_descriptor(), true);
}

Status mus_strategy_write_text(FILE *stream, const Trainer *trainer) {
    return mus_single_bet_strategy_write_text(stream, trainer);
}
