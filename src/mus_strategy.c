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
    bool ordago;
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

static size_t ordago_expected_action_count(
    const MusOrdagoInformationSet *information) {
    if (information->phase == MUS_PHASE_MUS_PLAYER_0 ||
        information->phase == MUS_PHASE_MUS_PLAYER_1)
        return 2;
    if (information->phase == MUS_PHASE_DISCARD_PLAYER_0 ||
        information->phase == MUS_PHASE_DISCARD_PLAYER_1)
        return MUS_GAME_DISCARD_MASK_COUNT;
    if (information->bet_stage != MUS_BET_RESPONSE)
        return 6;
    switch (information->current_wager) {
    case 2:
        return 6;
    case 4:
        return 5;
    case 7:
        return 4;
    case 10:
        return 3;
    case MUS_ORDAGO_WAGER:
        return 2;
    default:
        return 0;
    }
}

static Status write_ordago_wager_history(FILE *stream, uint8_t history) {
    static const uint8_t wagers[5] = {2, 4, 7, 10, MUS_ORDAGO_WAGER};
    bool first = true;

    for (int index = 0; index < 5; index += 1) {
        if ((history & (uint8_t)(1U << index)) == 0)
            continue;
        if (!first && fputc('-', stream) == EOF)
            return CFR_STATUS_IO_ERROR;
        if (wagers[index] == MUS_ORDAGO_WAGER) {
            if (fputs("ordago", stream) == EOF)
                return CFR_STATUS_IO_ERROR;
        } else if (fprintf(stream, "%u", wagers[index]) < 0) {
            return CFR_STATUS_IO_ERROR;
        }
        first = false;
    }
    return CFR_STATUS_SUCCESS;
}

static Status write_ordago_node(const InfoNode *node,
                                StrategyWriter *writer) {
    MusOrdagoInformationSet information;
    Probability strategy[MUS_GAME_MAX_LEGAL_ACTIONS];
    size_t expected_action_count;
    Status status;

    status = mus_ordago_game_information_set_decode(node->key,
                                                     &information);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    expected_action_count = ordago_expected_action_count(&information);
    if (expected_action_count == 0 ||
        node->action_count != expected_action_count)
        return CFR_STATUS_INVALID_ARGUMENT;
    status = cfr_info_node_average_strategy(
        node, strategy, MUS_GAME_MAX_LEGAL_ACTIONS);
    if (status != CFR_STATUS_SUCCESS)
        return status;
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
    if (fprintf(writer->stream,
                " fase=%s muses=%u descartes=%u/%u,%u/%u corte=",
                mus_game_phase_name(information.phase), information.mus_count,
                information.discard_counts[0][0],
                information.discard_counts[0][1],
                information.discard_counts[1][0],
                information.discard_counts[1][1]) < 0)
        return CFR_STATUS_IO_ERROR;
    if (!information.mus_stopped) {
        if (fputs("limite_o_pendiente", writer->stream) == EOF)
            return CFR_STATUS_IO_ERROR;
    } else if (fputs(information.mus_stop_player == CFR_PLAYER_0 ? "mano"
                                                                 : "postre",
                     writer->stream) == EOF) {
        return CFR_STATUS_IO_ERROR;
    }
    if (fputs(" historial=", writer->stream) == EOF)
        return CFR_STATUS_IO_ERROR;
    for (int lance = 0; lance < MUS_GAME_LANCE_COUNT; lance += 1) {
        if (lance > 0 && fputc(',', writer->stream) == EOF)
            return CFR_STATUS_IO_ERROR;
        if (fputs(mus_ordago_game_outcome_name(
                      information.outcomes[lance],
                      information.outcome_wagers[lance]),
                  writer->stream) == EOF)
            return CFR_STATUS_IO_ERROR;
        if (information.outcome_wager_histories[lance] != 0) {
            if (fputc('[', writer->stream) == EOF ||
                write_ordago_wager_history(
                    writer->stream,
                    information.outcome_wager_histories[lance]) !=
                    CFR_STATUS_SUCCESS ||
                fputc(']', writer->stream) == EOF) {
                return CFR_STATUS_IO_ERROR;
            }
        }
    }

    if (information.phase == MUS_PHASE_MUS_PLAYER_0 ||
        information.phase == MUS_PHASE_MUS_PLAYER_1) {
        if (fprintf(writer->stream, " mus=%.17g no_mus=%.17g\n",
                    strategy[0], strategy[1]) < 0)
            return CFR_STATUS_IO_ERROR;
        return CFR_STATUS_SUCCESS;
    }
    if (information.phase == MUS_PHASE_DISCARD_PLAYER_0 ||
        information.phase == MUS_PHASE_DISCARD_PLAYER_1) {
        for (int mask = 0; mask < MUS_GAME_DISCARD_MASK_COUNT; mask += 1) {
            if (fprintf(writer->stream, " cambiar_%d%d%d%d=%.17g",
                        (mask >> 3) & 1, (mask >> 2) & 1,
                        (mask >> 1) & 1, mask & 1, strategy[mask]) < 0)
                return CFR_STATUS_IO_ERROR;
        }
        return fputc('\n', writer->stream) == EOF ? CFR_STATUS_IO_ERROR
                                                   : CFR_STATUS_SUCCESS;
    }
    if (information.bet_stage != MUS_BET_RESPONSE) {
        static const char *const labels[6] = {
            "pasar", "envidar_2", "envidar_4", "envidar_7",
            "envidar_10", "ordago",
        };

        if (fputs(information.bet_stage == MUS_BET_FIRST_TO_ACT
                      ? " actual=inicio"
                      : " actual=paso_mano",
                  writer->stream) == EOF)
            return CFR_STATUS_IO_ERROR;
        for (size_t index = 0; index < 6; index += 1) {
            if (fprintf(writer->stream, " %s=%.17g", labels[index],
                        strategy[index]) < 0)
                return CFR_STATUS_IO_ERROR;
        }
    } else {
        static const uint8_t wagers[5] = {2, 4, 7, 10, MUS_ORDAGO_WAGER};
        size_t strategy_index = 2;

        if (fputs(" actual=", writer->stream) == EOF ||
            write_ordago_wager_history(
                writer->stream, information.current_wager_history) !=
                CFR_STATUS_SUCCESS ||
            fprintf(writer->stream, " no_querer=%.17g querer=%.17g",
                    strategy[0], strategy[1]) < 0)
            return CFR_STATUS_IO_ERROR;
        for (int index = 0; index < 5; index += 1) {
            if (wagers[index] <= information.current_wager)
                continue;
            if (wagers[index] == MUS_ORDAGO_WAGER) {
                if (fprintf(writer->stream, " ordago=%.17g",
                            strategy[strategy_index]) < 0)
                    return CFR_STATUS_IO_ERROR;
            } else if (fprintf(writer->stream, " subir_a_%u=%.17g",
                               wagers[index], strategy[strategy_index]) < 0) {
                return CFR_STATUS_IO_ERROR;
            }
            strategy_index += 1;
        }
    }
    return fputc('\n', writer->stream) == EOF ? CFR_STATUS_IO_ERROR
                                               : CFR_STATUS_SUCCESS;
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
    if (writer->ordago)
        return write_ordago_node(node, writer);
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
                             bool raises_to_eight, bool ordago) {
    StrategyWriter writer = {
        .stream = stream,
        .raises_to_eight = raises_to_eight,
        .ordago = ordago,
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
                          false, false);
}

Status mus_raises_to_eight_strategy_write_text(FILE *stream,
                                               const Trainer *trainer) {
    return write_strategy(stream, trainer,
                          mus_raises_to_eight_game_descriptor(), true,
                          false);
}

Status mus_ordago_strategy_write_text(FILE *stream, const Trainer *trainer) {
    return write_strategy(stream, trainer, mus_ordago_game_descriptor(),
                          false, true);
}

Status mus_strategy_write_text(FILE *stream, const Trainer *trainer) {
    return mus_single_bet_strategy_write_text(stream, trainer);
}
