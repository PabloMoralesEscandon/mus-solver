#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <json-c/json.h>
#include "mus_solver/mus_adapter.h"

#define MAX_LINE (1024U * 1024U)

static const char *status_code(Status s) {
    switch (s) {
    case CFR_STATUS_SUCCESS:
        return "success";
    case CFR_STATUS_INVALID_ARGUMENT:
        return "invalid_observation";
    case CFR_STATUS_ILLEGAL_ACTION:
        return "illegal_action";
    case CFR_STATUS_BUFFER_TOO_SMALL:
        return "capacity_exceeded";
    case CFR_STATUS_NUMERIC_ERROR:
        return "invalid_strategy_data";
    case CFR_STATUS_OUT_OF_MEMORY:
        return "out_of_memory";
    case CFR_STATUS_NOT_FOUND:
        return "not_found";
    case CFR_STATUS_IO_ERROR:
        return "io_error";
    case CFR_STATUS_FORMAT_ERROR:
        return "invalid_checkpoint";
    case CFR_STATUS_INCOMPATIBLE_GAME:
        return "incompatible_checkpoint";
    }
    return "unknown_error";
}

static bool fields(json_object *obj, const char *const *names, size_t count) {
    if (!obj || !json_object_is_type(obj, json_type_object))
        return false;
    json_object_object_foreach(obj, key, value) {
        (void)value;
        bool found = false;
        for (size_t i = 0; i < count; ++i)
            if (strcmp(key, names[i]) == 0)
                found = true;
        if (!found)
            return false;
    }
    return true;
}
static bool integer(json_object *obj, int64_t low, int64_t high, int64_t *out) {
    if (!obj || !json_object_is_type(obj, json_type_int))
        return false;
    int64_t v = json_object_get_int64(obj);
    if (v < low || v > high)
        return false;
    *out = v;
    return true;
}
static bool int_field(json_object *obj, const char *key, int64_t low, int64_t high,
                      bool required, int64_t *out) {
    json_object *value;
    if (!json_object_object_get_ex(obj, key, &value))
        return !required;
    return integer(value, low, high, out);
}
static bool bool_field(json_object *obj, const char *key, bool *out) {
    json_object *value;
    if (!json_object_object_get_ex(obj, key, &value))
        return true;
    if (!json_object_is_type(value, json_type_boolean))
        return false;
    *out = json_object_get_boolean(value) != 0;
    return true;
}
static bool int_array(json_object *array, size_t count, int64_t low, int64_t high,
                      int64_t *out) {
    if (!array || !json_object_is_type(array, json_type_array) ||
        json_object_array_length(array) != count)
        return false;
    for (size_t i = 0; i < count; ++i)
        if (!integer(json_object_array_get_idx(array, i), low, high, &out[i]))
            return false;
    return true;
}
static bool string_equals(json_object *obj, const char *text) {
    return obj && json_object_is_type(obj, json_type_string) &&
           (size_t)json_object_get_string_len(obj) == strlen(text) &&
           strcmp(json_object_get_string(obj), text) == 0;
}

static bool parse_action(json_object *obj, MusBotAction *out, bool event) {
    static const char *const keys[] = {"type", "amount", "mask", "player"};
    if (!fields(obj, keys, event ? 4 : 3))
        return false;
    json_object *type, *unused;
    if (!json_object_object_get_ex(obj, "type", &type))
        return false;
    bool found = false;
    for (int kind = MUS_BOT_PASS; kind <= MUS_BOT_DISCARD; ++kind) {
        if (string_equals(type, mus_bot_action_name((MusBotActionKind)kind))) {
            *out = (MusBotAction){.kind = (MusBotActionKind)kind};
            found = true;
            break;
        }
    }
    if (!found)
        return false;
    if (out->kind == MUS_BOT_RAISE)
        return !json_object_object_get_ex(obj, "mask", &unused) &&
               int_field(obj, "amount", 1, MUS_ADAPTER_MAX_AMOUNT, true, &out->amount);
    if (out->kind == MUS_BOT_DISCARD)
        return !json_object_object_get_ex(obj, "amount", &unused) &&
               int_field(obj, "mask", 1, 15, true, &out->amount);
    return !json_object_object_get_ex(obj, "amount", &unused) &&
           !json_object_object_get_ex(obj, "mask", &unused);
}

static Status parse_observation(json_object *obj, MusAdapterObservation *o,
                                MusBotEvent **events, MusBotAction **legal) {
    static const char *const keys[] = {"id",
                                       "player",
                                       "phase",
                                       "hand",
                                       "mus_count",
                                       "discard_counts",
                                       "mus_stop_player",
                                       "previous_strengths",
                                       "prior_wager_total",
                                       "known_hand_scores",
                                       "hand_scores_complete",
                                       "match_scores",
                                       "hand_start_scores",
                                       "pending_wager_total",
                                       "history",
                                       "legal_actions"};
    if (!fields(obj, keys, sizeof(keys) / sizeof(keys[0])))
        return CFR_STATUS_INVALID_ARGUMENT;
    json_object *value;
    if (json_object_object_get_ex(obj, "id", &value) && value &&
        !json_object_is_type(value, json_type_int) &&
        !json_object_is_type(value, json_type_string))
        return CFR_STATUS_INVALID_ARGUMENT;
    int64_t number = 0;
    if (!int_field(obj, "player", 0, 1, true, &number))
        return CFR_STATUS_INVALID_ARGUMENT;
    o->player = (Player)number;
    if (!json_object_object_get_ex(obj, "phase", &value))
        return CFR_STATUS_INVALID_ARGUMENT;
    const MusGamePhase phases[] = {MUS_PHASE_GRANDE,          MUS_PHASE_CHICA,
                                   MUS_PHASE_PARES,           MUS_PHASE_JUEGO,
                                   MUS_PHASE_PUNTO,           MUS_PHASE_MUS_PLAYER_0,
                                   MUS_PHASE_DISCARD_PLAYER_0};
    const char *const names[] = {"grande", "chica", "pares",  "juego",
                                 "punto",  "mus",   "discard"};
    bool found = false;
    for (size_t i = 0; i < sizeof(phases) / sizeof(phases[0]); ++i) {
        if (string_equals(value, names[i])) {
            o->phase = phases[i];
            if (i >= 5 && o->player == CFR_PLAYER_1)
                o->phase = (MusGamePhase)(o->phase + 1);
            found = true;
            break;
        }
    }
    if (!found)
        return CFR_STATUS_INVALID_ARGUMENT;
    int64_t numbers[8];
    if (!json_object_object_get_ex(obj, "hand", &value) ||
        !int_array(value, 8, 0, 4, numbers))
        return CFR_STATUS_INVALID_ARGUMENT;
    for (int i = 0; i < 8; ++i)
        o->hand[i] = (uint8_t)numbers[i];
    number = 0;
    if (!int_field(obj, "mus_count", 0, 1, false, &number))
        return CFR_STATUS_INVALID_ARGUMENT;
    o->mus_count = (uint8_t)number;
    number = 0;
    if (!int_field(obj, "mus_stop_player", 0, 1, false, &number))
        return CFR_STATUS_INVALID_ARGUMENT;
    o->mus_stop_player = (Player)number;
    if (json_object_object_get_ex(obj, "discard_counts", &value)) {
        if (!int_array(value, 2, 0, 4, numbers))
            return CFR_STATUS_INVALID_ARGUMENT;
        for (int p = 0; p < 2; ++p)
            o->discard_counts[0][p] = (uint8_t)numbers[p];
    }
    if (json_object_object_get_ex(obj, "previous_strengths", &value)) {
        if (!json_object_is_type(value, json_type_array))
            return CFR_STATUS_INVALID_ARGUMENT;
        size_t count = json_object_array_length(value);
        if (count > 3 || !int_array(value, count, 0, 6, numbers))
            return CFR_STATUS_INVALID_ARGUMENT;
        o->previous_strength_count = (uint8_t)count;
        for (size_t i = 0; i < count; ++i)
            o->previous_strengths[i] = (MusLanceStrength)numbers[i];
    }
    if (!int_field(obj, "prior_wager_total", 0, MUS_ADAPTER_MAX_AMOUNT, false,
                   &o->prior_wager_total))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (json_object_object_get_ex(obj, "known_hand_scores", &value)) {
        if (!int_array(value, 2, 0, 40, numbers))
            return CFR_STATUS_INVALID_ARGUMENT;
        for (int p = 0; p < 2; ++p)
            o->known_hand_scores[p] = (int)numbers[p];
    }
    if (!bool_field(obj, "hand_scores_complete", &o->hand_scores_complete))
        return CFR_STATUS_INVALID_ARGUMENT;
    if (json_object_object_get_ex(obj, "match_scores", &value)) {
        if (!int_array(value, 2, 0, 39, numbers))
            return CFR_STATUS_INVALID_ARGUMENT;
        for (int p = 0; p < 2; ++p)
            o->match_scores[p] = (int)numbers[p];
    }
    if (json_object_object_get_ex(obj, "hand_start_scores", &value)) {
        if (!int_array(value, 2, 0, 39, numbers))
            return CFR_STATUS_INVALID_ARGUMENT;
        o->has_hand_start_scores = true;
        for (int p = 0; p < 2; ++p)
            o->hand_start_scores[p] = (int)numbers[p];
    }
    if (json_object_object_get_ex(obj, "pending_wager_total", &value)) {
        if (!integer(value, 0, MUS_ADAPTER_MAX_AMOUNT, &o->pending_wager_total))
            return CFR_STATUS_INVALID_ARGUMENT;
        o->has_pending_wager_total = true;
    }
    if (json_object_object_get_ex(obj, "history", &value)) {
        if (!json_object_is_type(value, json_type_array))
            return CFR_STATUS_INVALID_ARGUMENT;
        o->history_count = json_object_array_length(value);
        if (o->history_count) {
            *events = calloc(o->history_count, sizeof(**events));
            if (!*events)
                return CFR_STATUS_OUT_OF_MEMORY;
        }
        o->history = *events;
        for (size_t i = 0; i < o->history_count; ++i) {
            json_object *e = json_object_array_get_idx(value, i);
            if (!parse_action(e, &(*events)[i].action, true) ||
                !int_field(e, "player", 0, 1, true, &number))
                return CFR_STATUS_INVALID_ARGUMENT;
            (*events)[i].player = (Player)number;
        }
    }
    if (!json_object_object_get_ex(obj, "legal_actions", &value) ||
        !json_object_is_type(value, json_type_array))
        return CFR_STATUS_INVALID_ARGUMENT;
    o->legal_action_count = json_object_array_length(value);
    if (!o->legal_action_count)
        return CFR_STATUS_INVALID_ARGUMENT;
    *legal = calloc(o->legal_action_count, sizeof(**legal));
    if (!*legal)
        return CFR_STATUS_OUT_OF_MEMORY;
    o->legal_actions = *legal;
    for (size_t i = 0; i < o->legal_action_count; ++i)
        if (!parse_action(json_object_array_get_idx(value, i), &(*legal)[i], false))
            return CFR_STATUS_INVALID_ARGUMENT;
    return CFR_STATUS_SUCCESS;
}

static json_object *action_json(MusBotAction action) {
    json_object *obj = json_object_new_object();
    json_object_object_add(obj, "type",
                           json_object_new_string(mus_bot_action_name(action.kind)));
    if (action.kind == MUS_BOT_RAISE || action.kind == MUS_BOT_DISCARD)
        json_object_object_add(obj, action.kind == MUS_BOT_RAISE ? "amount" : "mask",
                               json_object_new_int64(action.amount));
    return obj;
}
static json_object *decision_json(const MusAdapterDecision *d) {
    json_object *out = json_object_new_object(), *diagnostic = json_object_new_object();
    json_object_object_add(out, "ok", json_object_new_boolean(true));
    json_object_object_add(out, "action", action_json(d->selected));
    json_object *choices = json_object_new_array();
    for (size_t i = 0; i < d->choice_count; ++i) {
        json_object *choice = action_json(d->choices[i].action);
        json_object_object_add(choice, "probability",
                               json_object_new_double(d->choices[i].probability));
        json_object_array_add(choices, choice);
    }
    json_object_object_add(out, "probabilities", choices);
    json_object_object_add(diagnostic, "method",
                           json_object_new_string(mus_adapter_method_name(d->method)));
    json_object_object_add(diagnostic, "reason",
                           d->reason ? json_object_new_string(d->reason) : NULL);
    json_object_object_add(diagnostic, "score_proxy",
                           json_object_new_boolean(d->score_proxy));
    json_object_object_add(diagnostic, "strategy",
                           json_object_new_string(d->score_strategy ? "ordago-score"
                               : d->public_strategy ? "ordago" : "ordago-deprecated"));
    json_object_object_add(diagnostic, "strategy_reason",
                           json_object_new_string(d->strategy_reason));
    json_object *scores = NULL;
    if (d->score_strategy) {
        scores = json_object_new_array();
        for (int p = 0; p < 2; ++p)
            json_object_array_add(scores, json_object_new_int(d->strategy_scores[p]));
    }
    json_object_object_add(diagnostic, "strategy_scores", scores);
    json_object_object_add(diagnostic, "ordago_factor",
                           json_object_new_double(d->ordago_factor));
    json_object_object_add(diagnostic, "real_total",
                           json_object_new_int64(d->real_total));
    json_object_object_add(diagnostic, "real_covered",
                           json_object_new_int64(d->real_covered));
    json_object_object_add(diagnostic, "lookup_count",
                           json_object_new_int64((int64_t)d->lookup_count));
    json_object *sources = json_object_new_array();
    for (size_t i = 0; i < d->source_count; ++i) {
        json_object *source = json_object_new_object();
        char key[32];
        snprintf(key, sizeof(key), "%" PRId64, d->sources[i].key);
        json_object_object_add(source, "key", json_object_new_string(key));
        json_object_object_add(source, "weight",
                               json_object_new_double(d->sources[i].weight));
        json_object_object_add(source, "distance",
                               json_object_new_int64(d->sources[i].distance));
        json_object_object_add(source, "total",
                               json_object_new_int(d->sources[i].total));
        json_object_object_add(source, "covered",
                               json_object_new_int(d->sources[i].covered));
        json_object_object_add(source, "prior_total",
                               json_object_new_int(d->sources[i].prior_total));
        json_object_object_add(source, "settled_score",
                               json_object_new_int(d->sources[i].settled_score));
        json_object_array_add(sources, source);
    }
    json_object_object_add(diagnostic, "sources", sources);
    json_object *adjustments = json_object_new_array();
    for (size_t i = 0; i < d->adjustment_count; ++i) {
        json_object *adjustment = json_object_new_object();
        json_object_object_add(
            adjustment, "source",
            json_object_new_int64((int64_t)d->adjustments[i].source));
        json_object_object_add(adjustment, "abstract_action",
                               json_object_new_int(d->adjustments[i].abstract_action));
        json_object_object_add(adjustment, "real_action",
                               action_json(d->adjustments[i].real_action));
        json_object_object_add(adjustment, "mass",
                               json_object_new_double(d->adjustments[i].mass));
        json_object_array_add(adjustments, adjustment);
    }
    json_object_object_add(diagnostic, "adjustments", adjustments);
    json_object_object_add(out, "diagnostic", diagnostic);
    return out;
}
static json_object *error_json(const char *code) {
    json_object *out = json_object_new_object();
    json_object_object_add(out, "ok", json_object_new_boolean(false));
    json_object_object_add(out, "error", json_object_new_string(code));
    return out;
}

static bool emit(json_object *out) {
    bool ok = puts(json_object_to_json_string_ext(out, JSON_C_TO_STRING_PLAIN)) >= 0 &&
              fflush(stdout) == 0;
    json_object_put(out);
    return ok;
}

int main(int argc, char **argv) {
    const char *path = NULL;
    const char *score_table = NULL, *score_strategies = NULL;
    bool compressed = true, text_strategy = false;
    uint64_t seed = 0;
    MusAdapterConfig config = mus_adapter_default_config();
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help")) {
            puts("Usage: mus-bot --strategy FILE.txt [--seed N] "
                 "[--no-score-adjustment]\n"
                 "       mus-bot --load-compressed FILE [--seed N] "
                 "[--no-score-adjustment]\n"
                 "       mus-bot --load FILE [--seed N] [--no-score-adjustment]\n"
                 "Use an ordago policy for new training (formerly ordago-public).\n"
                 "Historical ordago policies (ordago-deprecated) remain supported.\n"
                 "Add --score-table values.txt --score-strategies DIR for hybrid play.\n"
                 "Hybrid observations require hand_start_scores. Trained betting\n"
                 "observations also require pending_wager_total.\n"
                 "An ordago generic policy also requires these fields.\n"
                 "Read one observation per JSON line. Write one decision per JSON "
                 "line.\n"
                 "The maximum input line length is 1 MiB. See docs/adapter.md.");
            return 0;
        }
        if ((!strcmp(argv[i], "--strategy") || !strcmp(argv[i], "--load-compressed") ||
             !strcmp(argv[i], "--load")) &&
            i + 1 < argc && !path) {
            text_strategy = !strcmp(argv[i], "--strategy");
            compressed = !strcmp(argv[i], "--load-compressed");
            path = argv[++i];
        } else if (!strcmp(argv[i], "--score-table") && i + 1 < argc && !score_table) {
            score_table = argv[++i];
        } else if (!strcmp(argv[i], "--score-strategies") && i + 1 < argc && !score_strategies) {
            score_strategies = argv[++i];
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            char *end;
            const char *arg = argv[++i];
            errno = 0;
            unsigned long long parsed = strtoull(arg, &end, 10);
            if (*arg < '0' || *arg > '9' || *end || errno ||
                parsed > UINT64_MAX) {
                fputs("error: invalid_seed\n", stderr);
                return 2;
            }
            seed = (uint64_t)parsed;
        } else if (!strcmp(argv[i], "--no-score-adjustment"))
            config.score_adjustment = false;
        else {
            fputs("error: invalid_arguments\n", stderr);
            return 2;
        }
    }
    if ((score_table == NULL) != (score_strategies == NULL)) {
        fputs("error: score_table_and_strategies_required\n", stderr);
        return 2;
    }
    if (!path) {
        fputs("error: checkpoint_required\n", stderr);
        return 2;
    }
    if (MUS_GAME_MAX_MUSES != 1) {
        fputs("error: unsupported_mus_limit (mus-bot requires one mus per hand)\n", stderr);
        return 1;
    }
    MusAdapter *adapter = NULL;
    Status status = text_strategy
                        ? mus_adapter_open_text(path, &config, seed, &adapter)
                        : mus_adapter_load(path, compressed, &config, seed, &adapter);
    if (status == CFR_STATUS_SUCCESS && score_table != NULL)
        status = mus_adapter_enable_score_text(adapter, score_table, score_strategies);
    if (status != CFR_STATUS_SUCCESS) {
        fprintf(stderr, "error: %s\n", status_code(status));
        mus_adapter_destroy(adapter);
        return 1;
    }
    char *line = malloc(MAX_LINE + 2U);
    json_tokener *tokener = json_tokener_new_ex(32);
    if (!line || !tokener) {
        free(line);
        if (tokener)
            json_tokener_free(tokener);
        mus_adapter_destroy(adapter);
        return 1;
    }
    int exit_status = 0;
    /* Read bytes explicitly so embedded NULs and oversized lines cannot hide requests.
     */
    for (;;) {
        size_t length = 0;
        bool oversized = false, nul = false;
        int ch;
        while ((ch = fgetc(stdin)) != EOF && ch != '\n') {
            if (ch == 0)
                nul = true;
            if (length < MAX_LINE)
                line[length++] = (char)ch;
            else
                oversized = true;
        }
        if (ch == EOF && !length && !oversized)
            break;
        line[length++] = '\n';
        line[length] = '\0';
        json_object *out = NULL, *input = NULL;
        if (oversized || nul)
            out = error_json(oversized ? "line_too_long" : "invalid_json");
        else {
            json_tokener_reset(tokener);
            json_tokener_set_flags(tokener,
                                   JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
            input = json_tokener_parse_ex(tokener, line, (int)length);
            if (json_tokener_get_error(tokener) != json_tokener_success)
                out = error_json("invalid_json");
            else {
                MusAdapterObservation observation = {0};
                MusBotEvent *events = NULL;
                MusBotAction *legal = NULL;
                status = parse_observation(input, &observation, &events, &legal);
                MusAdapterDecision decision;
                if (status == CFR_STATUS_SUCCESS)
                    status = mus_adapter_decide(adapter, &observation, &decision);
                out = status == CFR_STATUS_SUCCESS ? decision_json(&decision)
                                                   : error_json(status_code(status));
                free(events);
                free(legal);
                json_object *id;
                if (input && json_object_is_type(input, json_type_object) &&
                    json_object_object_get_ex(input, "id", &id))
                    json_object_object_add(out, "id", json_object_get(id));
            }
        }
        json_object_put(input);
        if (!emit(out)) {
            exit_status = 1;
            break;
        }
    }
    if (ferror(stdin))
        exit_status = 1;
    free(line);
    json_tokener_free(tokener);
    mus_adapter_destroy(adapter);
    return exit_status;
}
