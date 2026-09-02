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

static uint8_t ordago_minimum_response_increment(
    const MusOrdagoInformationSet *information) {
    if (information->previous_wager == 1)
        return information->current_wager;
    return (uint8_t)(information->current_wager -
                     information->previous_wager);
}

static bool ordago_response_increment_is_legal(
    const MusOrdagoInformationSet *information, uint8_t increment) {
    return increment >= ordago_minimum_response_increment(information) &&
           (int)information->prior_wager_total +
                   (int)information->current_wager + increment <=
               MUS_ORDAGO_WAGER;
}

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
        information->phase == MUS_PHASE_DISCARD_PLAYER_1) {
        size_t count = 0;

        for (unsigned int mask = 1; mask < MUS_GAME_DISCARD_MASK_COUNT;
             mask += 1) {
            if (mus_ordago_game_discard_mask_is_canonical(
                    information->hand, mask)) {
                count += 1;
            }
        }
        return count;
    }
    static const uint8_t increments[4] = {2, 4, 7, 10};

    if (information->bet_stage != MUS_BET_RESPONSE) {
        size_t count = 2;

        for (int index = 0; index < 4; index += 1) {
            if ((int)information->prior_wager_total + increments[index] <=
                MUS_ORDAGO_WAGER) {
                count += 1;
            }
        }
        return count;
    }
    if (information->current_wager_is_ordago)
        return 2;
    size_t count = 2;

    for (int index = 0; index < 4; index += 1) {
        if (ordago_response_increment_is_legal(information,
                                                increments[index])) {
            count += 1;
        }
    }
    return count + 1;
}

static const char *ordago_strength_name(MusLanceStrength strength) {
    switch (strength) {
    case MUS_LANCE_STRENGTH_NONE:
        return "ninguno";
    case MUS_LANCE_STRENGTH_PLAYER_0:
        return "mano";
    case MUS_LANCE_STRENGTH_PLAYER_1:
        return "postre";
    case MUS_LANCE_STRENGTH_BOTH:
        return "ambos";
    case MUS_LANCE_STRENGTH_PARES_SKIPPED_NONE:
        return "pares_sin_jugadores";
    case MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_0:
        return "pares_solo_mano";
    case MUS_LANCE_STRENGTH_PARES_SKIPPED_PLAYER_1:
        return "pares_solo_postre";
    }
    return "desconocido";
}

static Status write_ordago_hand(FILE *stream,
                                 const MusOrdagoInformationSet *information) {
    if (information->hand_kind == MUS_ORDAGO_HAND_EXACT) {
        for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT;
             card_class += 1) {
            if (card_class > 0 && fputc(',', stream) == EOF)
                return CFR_STATUS_IO_ERROR;
            if (fprintf(stream, "%s:%u",
                        mus_game_class_name((MusCardClass)card_class),
                        information->hand[card_class]) < 0) {
                return CFR_STATUS_IO_ERROR;
            }
        }
        return CFR_STATUS_SUCCESS;
    }
    if (information->hand_kind == MUS_ORDAGO_HAND_GRANDE) {
        if (fprintf(stream, "reyes:%u,siguiente:",
                    information->king_count) < 0)
            return CFR_STATUS_IO_ERROR;
        return fputs(information->has_highest_non_king
                         ? mus_game_class_name(
                               information->highest_non_king)
                         : "ninguna",
                     stream) == EOF
                   ? CFR_STATUS_IO_ERROR
                   : CFR_STATUS_SUCCESS;
    }
    if (information->hand_kind == MUS_ORDAGO_HAND_CHICA) {
        if (fprintf(stream, "pitos:%u,siguiente:",
                    information->pito_count) < 0)
            return CFR_STATUS_IO_ERROR;
        return fputs(information->has_lowest_non_pito
                         ? mus_game_class_name(
                               information->lowest_non_pito)
                         : "ninguna",
                     stream) == EOF
                   ? CFR_STATUS_IO_ERROR
                   : CFR_STATUS_SUCCESS;
    }
    if (information->hand_kind == MUS_ORDAGO_HAND_PARES) {
        static const char *const pair_names[3] = {
            "pareja", "medias", "duples",
        };

        if (fprintf(stream, "%s:%s", pair_names[information->pairs_type],
                    mus_game_class_name(information->pairs_value)) < 0) {
            return CFR_STATUS_IO_ERROR;
        }
        if (information->has_second_pairs_value &&
            fprintf(stream, ",segunda:%s",
                    mus_game_class_name(
                        information->second_pairs_value)) < 0) {
            return CFR_STATUS_IO_ERROR;
        }
        return CFR_STATUS_SUCCESS;
    }
    if (information->hand_kind == MUS_ORDAGO_HAND_JUEGO)
        return fprintf(stream, "juego:%u", information->game_value) < 0
                   ? CFR_STATUS_IO_ERROR
                   : CFR_STATUS_SUCCESS;
    return fprintf(stream, "punto:%u", information->point_value) < 0
               ? CFR_STATUS_IO_ERROR
               : CFR_STATUS_SUCCESS;
}

static Status write_ordago_discard_action(
    FILE *stream, const MusOrdagoInformationSet *information,
    unsigned int mask, Probability probability) {
    unsigned int position = 0;
    bool first = true;

    if (fputs(" cambiar_", stream) == EOF)
        return CFR_STATUS_IO_ERROR;
    for (int card_class = 0; card_class < MUS_GAME_CLASS_COUNT;
         card_class += 1) {
        const unsigned int count = information->hand[card_class];
        unsigned int selected = 0;

        for (unsigned int copy = 0; copy < count; copy += 1) {
            selected += (mask >> (position + copy)) & 1U;
        }
        position += count;
        if (selected == 0)
            continue;
        if (!first && fputc('+', stream) == EOF)
            return CFR_STATUS_IO_ERROR;
        if (fprintf(stream, "%s:%u",
                    mus_game_class_name((MusCardClass)card_class),
                    selected) < 0) {
            return CFR_STATUS_IO_ERROR;
        }
        first = false;
    }
    return fprintf(stream, "=%.17g", probability) < 0
               ? CFR_STATUS_IO_ERROR
               : CFR_STATUS_SUCCESS;
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
    if (write_ordago_hand(writer->stream, &information) !=
        CFR_STATUS_SUCCESS)
        return CFR_STATUS_IO_ERROR;
    if (fprintf(writer->stream, " fase=%s muses=%u descartes=",
                mus_game_phase_name(information.phase),
                information.mus_count) < 0)
        return CFR_STATUS_IO_ERROR;
    for (int round = 0; round < MUS_GAME_MAX_MUSES; round += 1) {
        if ((round > 0 && fputc(',', writer->stream) == EOF) ||
            fprintf(writer->stream, "%u/%u",
                    information.discard_counts[round][0],
                    information.discard_counts[round][1]) < 0) {
            return CFR_STATUS_IO_ERROR;
        }
    }
    if (fputs(" corte=", writer->stream) == EOF)
        return CFR_STATUS_IO_ERROR;
    if (!information.mus_stopped) {
        if (fputs("limite_o_pendiente", writer->stream) == EOF)
            return CFR_STATUS_IO_ERROR;
    } else if (fputs(information.mus_stop_player == CFR_PLAYER_0 ? "mano"
                                                                 : "postre",
                     writer->stream) == EOF) {
        return CFR_STATUS_IO_ERROR;
    }
    if (fputs(" fuerza_previa=", writer->stream) == EOF)
        return CFR_STATUS_IO_ERROR;
    if (information.previous_strength_count == 0 &&
        fputs("ninguna", writer->stream) == EOF) {
        return CFR_STATUS_IO_ERROR;
    }
    for (int lance = 0; lance < information.previous_strength_count;
         lance += 1) {
        if (lance > 0 && fputc(',', writer->stream) == EOF)
            return CFR_STATUS_IO_ERROR;
        if (fputs(ordago_strength_name(
                      information.previous_strengths[lance]),
                  writer->stream) == EOF) {
            return CFR_STATUS_IO_ERROR;
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
        size_t strategy_index = 0;

        for (unsigned int mask = 1; mask < MUS_GAME_DISCARD_MASK_COUNT;
             mask += 1) {
            if (!mus_ordago_game_discard_mask_is_canonical(
                    information.hand, mask)) {
                continue;
            }
            if (write_ordago_discard_action(
                    writer->stream, &information, mask,
                    strategy[strategy_index]) != CFR_STATUS_SUCCESS) {
                return CFR_STATUS_IO_ERROR;
            }
            strategy_index += 1;
        }
        return fputc('\n', writer->stream) == EOF ? CFR_STATUS_IO_ERROR
                                                   : CFR_STATUS_SUCCESS;
    }
    if (information.bet_stage != MUS_BET_RESPONSE) {
        static const uint8_t increments[4] = {2, 4, 7, 10};
        size_t strategy_index = 1;

        if (fprintf(writer->stream, " acumulado_previo=%u %s pasar=%.17g",
                    information.prior_wager_total,
                    information.bet_stage == MUS_BET_FIRST_TO_ACT
                        ? "actual=inicio"
                        : "actual=paso_mano",
                    strategy[0]) < 0) {
            return CFR_STATUS_IO_ERROR;
        }
        for (int index = 0; index < 4; index += 1) {
            if ((int)information.prior_wager_total + increments[index] >
                MUS_ORDAGO_WAGER) {
                continue;
            }
            if (fprintf(writer->stream, " envidar_%u=%.17g",
                        increments[index], strategy[strategy_index]) < 0) {
                return CFR_STATUS_IO_ERROR;
            }
            strategy_index += 1;
        }
        if (fprintf(writer->stream, " ordago=%.17g",
                    strategy[strategy_index]) < 0)
            return CFR_STATUS_IO_ERROR;
    } else {
        static const uint8_t increments[4] = {2, 4, 7, 10};
        size_t strategy_index = 2;

        if (fprintf(writer->stream, " acumulado_previo=%u",
                    information.prior_wager_total) < 0 ||
            (information.current_wager_is_ordago
                 ? fprintf(writer->stream,
                           " actual=ordago cubierto=%u",
                           information.previous_wager)
                 : fprintf(writer->stream,
                           " actual=%u cubierto=%u",
                           information.current_wager,
                           information.previous_wager)) < 0 ||
            fprintf(writer->stream, " no_querer=%.17g querer=%.17g",
                    strategy[0], strategy[1]) < 0) {
            return CFR_STATUS_IO_ERROR;
        }
        for (int index = 0; index < 4; index += 1) {
            if (!ordago_response_increment_is_legal(
                    &information, increments[index])) {
                continue;
            }
            if (fprintf(writer->stream, " envidar_%u=%.17g",
                        increments[index], strategy[strategy_index]) < 0) {
                return CFR_STATUS_IO_ERROR;
            }
            strategy_index += 1;
        }
        if (!information.current_wager_is_ordago &&
            fprintf(writer->stream, " ordago=%.17g",
                    strategy[strategy_index]) < 0) {
            return CFR_STATUS_IO_ERROR;
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
