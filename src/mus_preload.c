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

static bool public_source(const MusScorePreloadOptions *options) {
    return options->source->game->context == mus_ordago_public_game_descriptor()->context;
}

static Status decode_source(const MusScorePreloadOptions *options, InfoSetKey key,
                            MusOrdagoInformationSet *information) {
    return public_source(options)
        ? mus_ordago_public_game_information_set_decode(key, information)
        : mus_ordago_game_information_set_decode(key, information);
}

static Status preload_node(const InfoNode *source, void *raw_context) {
    PreloadContext *context = raw_context;
    context->stats.source_nodes += 1;
    MusOrdagoInformationSet information;
    Action old_actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    Action new_actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    Probability average[MUS_GAME_MAX_LEGAL_ACTIONS];
    Utility prior[MUS_GAME_MAX_LEGAL_ACTIONS];
    size_t old_count = 0, new_count = 0;
    Status status = decode_source(context->options, source->key, &information);
    if (status != CFR_STATUS_SUCCESS)
        return CFR_STATUS_FORMAT_ERROR;
    const bool later = information.hand_kind != MUS_ORDAGO_HAND_EXACT &&
                       information.phase != MUS_PHASE_GRANDE;
    if (!public_source(context->options) && later) {
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
    InfoSetKey key;
    status = public_source(context->options)
        ? mus_score_information_set_from_ordago_public(context->game, source->key, &key)
        : mus_score_information_set_from_ordago(context->game, source->key, 0, 0, &key);
    if (status == CFR_STATUS_NOT_FOUND) {
        context->stats.skipped_illegal_nodes += 1;
        return CFR_STATUS_SUCCESS;
    }
    if (status != CFR_STATUS_SUCCESS)
        return status;
    status = mus_score_game_information_set_decode(
        key, context->game->continuation->target, &information);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    status = mus_ordago_game_information_set_legal_actions(
        &information, new_actions, MUS_GAME_MAX_LEGAL_ACTIONS, &new_count);
    if (status != CFR_STATUS_SUCCESS)
        return status;

    /* Match action identifiers and normalize over the destination's menu. */
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
    if (mass == 0)
        context->stats.uniform_after_filter_nodes += 1;
    if (later)
        context->stats.imported_later_nodes += 1;
    if (shared < new_count)
        context->stats.expanded_action_nodes += 1;
    return CFR_STATUS_SUCCESS;
}

static Status validate_preload(const MusScoreGame *game,
                               const MusScorePreloadOptions *options,
                               InfoStore *destination) {
    if (game == NULL || !mus_score_game_matches(&game->game) ||
        game->continuation == NULL || game->continuation->four_player ||
        game->continuation->target == 0 ||
        game->continuation->target > MUS_ORDAGO_WAGER ||
        game->initial_scores[0] >= game->continuation->target ||
        game->initial_scores[1] >= game->continuation->target ||
        options == NULL || options->source == NULL || destination == NULL ||
        !isfinite(options->weight) || options->weight <= 0 ||
        !isfinite(options->uniform_mix) || options->uniform_mix <= 0 || options->uniform_mix > 1)
        return CFR_STATUS_INVALID_ARGUMENT;
    const Trainer *source = options->source;
    const Game *expected = source->game != NULL && public_source(options)
        ? mus_ordago_public_game_descriptor() : mus_ordago_game_descriptor();
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
    return cfr_info_store_get_stats(destination, &store_stats);
}

Status mus_score_preload_ordago(const MusScoreGame *game,
                               const MusScorePreloadOptions *options,
                               InfoStore *destination,
                               MusScorePreloadStats *stats) {
    if (stats != NULL)
        *stats = (MusScorePreloadStats){0};
    Status status = validate_preload(game, options, destination);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    PreloadContext context = {.game = game, .options = options, .destination = destination};
    status = cfr_info_store_visit_sorted(options->source->store, preload_node, &context);
    if (stats != NULL)
        *stats = context.stats;
    return status;
}

static Status initialize_from_ordago(InfoNode *node, void *raw_context) {
    MusScorePrior *context = raw_context;
    const MusScorePreloadOptions *options = &context->options;
    MusOrdagoInformationSet information;
    InfoSetKey candidates[MUS_ORDAGO_WAGER + 1];
    Action actions[MUS_GAME_MAX_LEGAL_ACTIONS];
    Utility prior[MUS_GAME_MAX_LEGAL_ACTIONS] = {0};
    bool shared[MUS_GAME_MAX_LEGAL_ACTIONS] = {false};
    size_t candidate_count = 0, count = 0;
    Status status = mus_score_game_information_set_decode(node->key,
        context->game->continuation->target, &information);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    const bool later = information.hand_kind != MUS_ORDAGO_HAND_EXACT &&
                       information.phase != MUS_PHASE_GRANDE;
    status = mus_ordago_game_information_set_legal_actions(&information, actions,
        MUS_GAME_MAX_LEGAL_ACTIONS, &count);
    if (status != CFR_STATUS_SUCCESS || count != node->action_count)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (public_source(options)) {
        status = mus_score_information_set_to_ordago_public(context->game, node->key, &candidates[0]);
        candidate_count = 1;
    } else {
        status = mus_score_information_set_ordago_candidates(context->game, node->key,
            candidates, MUS_ORDAGO_WAGER + 1, &candidate_count);
    }
    if (status != CFR_STATUS_SUCCESS)
        return status;
    bool found = false;
    for (size_t c = 0; c < candidate_count; ++c) {
        const InfoNode *source;
        status = cfr_info_store_find_const(options->source->store, candidates[c], &source);
        if (status == CFR_STATUS_NOT_FOUND)
            continue;
        if (status != CFR_STATUS_SUCCESS)
            return status;
        Action old_actions[MUS_GAME_MAX_LEGAL_ACTIONS];
        size_t old_count = 0;
        status = decode_source(options, source->key, &information);
        if (status == CFR_STATUS_SUCCESS)
            status = mus_ordago_game_information_set_legal_actions(&information, old_actions,
                MUS_GAME_MAX_LEGAL_ACTIONS, &old_count);
        if (status != CFR_STATUS_SUCCESS || source->action_count != old_count)
            return CFR_STATUS_FORMAT_ERROR;
        /* The source is immutable. With legacy candidates, summing strategy
         * mass marginalizes the settled-score feature by accumulated weight. */
        for (size_t j = 0; j < old_count; ++j) {
            const double mass = source->strategy_sums[j];
            if (!isfinite(mass) || mass < 0)
                return CFR_STATUS_NUMERIC_ERROR;
            if (mass > 0) found = true;
            for (size_t i = 0; i < count; ++i) {
                if (actions[i] != old_actions[j]) continue;
                shared[i] = true;
                prior[i] += mass;
                if (!isfinite(prior[i]))
                    return CFR_STATUS_NUMERIC_ERROR;
            }
        }
    }
    if (!found) {
        if (context->stats != NULL)
            __atomic_fetch_add(&context->stats->missing_source_nodes, 1, __ATOMIC_RELAXED);
        return CFR_STATUS_SUCCESS;
    }
    double mass = 0, total = 0;
    bool expanded = false;
    for (size_t i = 0; i < count; ++i) {
        mass += prior[i];
        expanded |= !shared[i];
    }
    if (!isfinite(mass))
        return CFR_STATUS_NUMERIC_ERROR;
    for (size_t i = 0; i < count; ++i) {
        const double probability = mass > 0 ? prior[i] / mass : 1.0 / (double)count;
        prior[i] = options->weight * ((1 - options->uniform_mix) * probability +
                                      options->uniform_mix / (double)count);
        total += prior[i];
        if (!isfinite(total) || prior[i] <= 0)
            return CFR_STATUS_NUMERIC_ERROR;
    }
    status = cfr_info_node_apply_deltas(node, prior, prior, count);
    if (status == CFR_STATUS_SUCCESS && context->stats != NULL) {
        __atomic_fetch_add(&context->stats->imported_nodes, 1, __ATOMIC_RELAXED);
        if (mass == 0)
            __atomic_fetch_add(&context->stats->uniform_after_filter_nodes, 1, __ATOMIC_RELAXED);
        if (later)
            __atomic_fetch_add(&context->stats->imported_later_nodes, 1, __ATOMIC_RELAXED);
        if (expanded)
            __atomic_fetch_add(&context->stats->expanded_action_nodes, 1, __ATOMIC_RELAXED);
    }
    return status;
}

Status mus_score_prior_attach(MusScorePrior *prior, const MusScoreGame *game,
                              const MusScorePreloadOptions *options,
                              InfoStore *destination, MusScorePreloadStats *stats) {
    if (prior == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    Status status = validate_preload(game, options, destination);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    InfoStoreStats source_stats, destination_stats;
    status = cfr_info_store_get_stats(options->source->store, &source_stats);
    if (status == CFR_STATUS_SUCCESS)
        status = cfr_info_store_get_stats(destination, &destination_stats);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    *prior = (MusScorePrior){.game = game, .options = *options, .stats = stats};
    if (stats != NULL)
        *stats = (MusScorePreloadStats){.source_nodes = source_stats.size,
                                      .existing_nodes = destination_stats.size};
    return cfr_info_store_set_initializer(destination, initialize_from_ordago, prior);
}
