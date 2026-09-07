#include <math.h>
#include <string.h>

#include "cfr/info_node.h"
#include "mus_solver/mus_score.h"

typedef struct {
    const MusScoreGame *game;
    const MusScorePreloadOptions *options;
    InfoStore *destination;
    MusScorePreloadStats stats;
} PreloadContext;

static Status preload_node(const InfoNode *source, void *raw_context) {
    PreloadContext *context = raw_context;
    context->stats.source_nodes += 1;
    MusOrdagoInformationSet information;
    Action old_actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    Action new_actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    Probability average[MUS_GAME_MAX_LEGAL_ACTIONS];
    Utility prior[MUS_GAME_MAX_LEGAL_ACTIONS];
    size_t old_count = 0, new_count = 0;
    Status status = mus_ordago_game_information_set_decode(source->key, &information);
    if (status != CFR_STATUS_SUCCESS)
        return CFR_STATUS_FORMAT_ERROR;
    if (information.hand_kind != MUS_ORDAGO_HAND_EXACT && information.phase != MUS_PHASE_GRANDE) {
        context->stats.skipped_history_nodes += 1;
        return CFR_STATUS_SUCCESS;
    }
    status = mus_ordago_game_information_set_legal_actions(
        &information, old_actions, MUS_GAME_MAX_LEGAL_ACTIONS, &old_count);
    if (status != CFR_STATUS_SUCCESS || source->action_count != old_count)
        return CFR_STATUS_FORMAT_ERROR;
    status = cfr_info_node_average_strategy(source, average, MUS_GAME_MAX_LEGAL_ACTIONS);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    information.score_enabled = true;
    status = mus_ordago_game_information_set_legal_actions(
        &information, new_actions, MUS_GAME_MAX_LEGAL_ACTIONS, &new_count);
    if (status != CFR_STATUS_SUCCESS)
        return status;

    /* Numeric menus can grow. Match action identifiers, never array indices. */
    double mass = 0;
    size_t shared = 0;
    for (size_t i = 0; i < new_count; i += 1) {
        prior[i] = 0;
        for (size_t j = 0; j < old_count; j += 1) {
            if (new_actions[i] == old_actions[j]) {
                prior[i] = average[j];
                mass += average[j];
                shared += 1;
                break;
            }
        }
    }
    double prior_mass = 0;
    for (size_t i = 0; i < new_count; i += 1) {
        const double probability = mass > 0 ? prior[i] / mass : 1.0 / (double)new_count;
        prior[i] = context->options->weight *
            ((1 - context->options->uniform_mix) * probability +
             context->options->uniform_mix / (double)new_count);
        prior_mass += prior[i];
        if (!isfinite(prior_mass) || prior[i] <= 0)
            return CFR_STATUS_NUMERIC_ERROR;
    }

    InfoSetKey key;
    status = mus_score_information_set_from_ordago(context->game, source->key, 0, 0, &key);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    InfoNode *node;
    status = cfr_info_store_find(context->destination, key, &node);
    if (status == CFR_STATUS_SUCCESS) {
        if (node->action_count != new_count)
            return CFR_STATUS_INVALID_ARGUMENT;
        context->stats.existing_nodes += 1;
        return CFR_STATUS_SUCCESS;
    }
    if (status != CFR_STATUS_NOT_FOUND)
        return status;
    status = cfr_info_store_get_or_create(context->destination, key, new_count, &node);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    status = cfr_info_node_apply_deltas(node, prior, prior, new_count);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    context->stats.imported_nodes += 1;
    if (shared < new_count)
        context->stats.expanded_action_nodes += 1;
    return CFR_STATUS_SUCCESS;
}

Status mus_score_preload_ordago(const MusScoreGame *game,
                               const MusScorePreloadOptions *options,
                               InfoStore *destination,
                               MusScorePreloadStats *stats) {
    if (stats != NULL)
        *stats = (MusScorePreloadStats){0};
    if (game == NULL || !mus_score_game_matches(&game->game) ||
        game->continuation == NULL || game->continuation->target == 0 ||
        game->continuation->target > MUS_ORDAGO_WAGER ||
        game->initial_scores[0] >= game->continuation->target ||
        game->initial_scores[1] >= game->continuation->target ||
        options == NULL || options->source == NULL || destination == NULL ||
        !isfinite(options->weight) || options->weight <= 0 ||
        !isfinite(options->uniform_mix) || options->uniform_mix <= 0 || options->uniform_mix > 1)
        return CFR_STATUS_INVALID_ARGUMENT;
    const Trainer *source = options->source;
    const Game *expected = mus_ordago_game_descriptor();
    if (source->game == NULL || source->game->context != expected->context ||
        source->game->operations != expected->operations ||
        source->game->trusted_operations != expected->trusted_operations ||
        source->game->strategic_player_count != expected->strategic_player_count ||
        source->game->max_legal_actions != expected->max_legal_actions ||
        source->game->strategy_schema_id == NULL ||
        strcmp(source->game->strategy_schema_id, expected->strategy_schema_id) != 0)
        return CFR_STATUS_INCOMPATIBLE_GAME;
    if (source->store == NULL || source->store == destination)
        return CFR_STATUS_INVALID_ARGUMENT;
    InfoStoreStats store_stats;
    Status status = cfr_info_store_get_stats(destination, &store_stats);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    PreloadContext context = {.game = game, .options = options, .destination = destination};
    status = cfr_info_store_visit_sorted(source->store, preload_node, &context);
    if (stats != NULL)
        *stats = context.stats;
    return status;
}
