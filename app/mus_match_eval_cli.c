#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "match_evaluation.h"

typedef enum { POLICY_NONE, POLICY_TEXT, POLICY_RAW, POLICY_ZSTD } PolicyFormat;
typedef struct {
    PolicyFormat format;
    const char *path;
    const char *table;
    const char *directory;
} PolicyInput;
typedef struct {
    PolicyInput inputs[2];
    const char *baseline;
    const char *trace_path;
    MusMatchOptions match;
    bool no_adjustment;
} Options;

static void usage(FILE *stream) {
    fputs("Usage: mus-match-eval --strategy-a FILE [--strategy-b FILE | --opponent passive|three]\n"
          "  --strategy-a/b FILE          Sorted TXT policy.\n"
          "  --load-a/b FILE              Raw checkpoint policy.\n"
          "  --load-compressed-a/b FILE   Zstandard checkpoint policy.\n"
          "  --score-table-a/b FILE       Optional values.txt for that policy.\n"
          "  --score-strategies-a/b DIR   Matching score TXT directory.\n"
          "  --matches N                 Complete matches (default 100).\n"
          "  --seed N                    Simulator seed, at most UINT_MAX (default 7).\n"
          "  --score A:B                 Initial scores of policies A and B (default 0:0).\n"
          "  --max-hands N               Abort if a match reaches this limit (default 1000).\n"
          "  --trace FILE                Write public decisions to a new CSV file.\n"
          "  --no-score-adjustment       Disable the generic ordago score heuristic.\n"
          "  --help                      Show this help.\n"
          "The first mano alternates between A and B. Matches use two players, one mus,\n"
          "and engine-validated numeric totals from 2 through 40. The default opponent\n"
          "is passive. Output reports observed wins and deployed-policy coverage.\n", stream);
}
static bool number(const char *text, uintmax_t maximum, uintmax_t *value) {
    if (text == NULL || *text == '\0') return false;
    for (const char *p = text; *p; ++p) if (*p < '0' || *p > '9') return false;
    errno = 0; char *end;
    const uintmax_t parsed = strtoumax(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > maximum) return false;
    *value = parsed; return true;
}
static bool parse(int argc, char **argv, Options *options) {
    *options = (Options){.match = {.matches = 100, .seed = 7, .max_hands_per_match = 1000}};
    unsigned int seen = 0;
    for (int i = 1; i < argc; ++i) {
        const char *name = argv[i];
        if (strcmp(name, "--no-score-adjustment") == 0) {
            if (options->no_adjustment) return false;
            options->no_adjustment = true; continue;
        }
        if (++i == argc || argv[i][0] == '\0') return false;
        const char *value = argv[i];
        const size_t length = strlen(name);
        const int seat = length >= 2 && name[length - 2] == '-' ? name[length - 1] - 'a' : -1;
        if (seat >= 0 && seat <= 1) {
            PolicyInput *input = &options->inputs[seat];
            if (strncmp(name, "--strategy-", 11) == 0 || strncmp(name, "--load-", 7) == 0) {
                PolicyFormat format;
                if (length == 12 && strncmp(name, "--strategy-", 11) == 0) format = POLICY_TEXT;
                else if (length == 8 && strncmp(name, "--load-", 7) == 0) format = POLICY_RAW;
                else if (length == 19 && strncmp(name, "--load-compressed-", 18) == 0) format = POLICY_ZSTD;
                else return false;
                if (input->format != POLICY_NONE) return false;
                input->format = format; input->path = value;
            } else if (length == 15 && strncmp(name, "--score-table-", 14) == 0) {
                if (input->table) return false;
                input->table = value;
            } else if (length == 20 && strncmp(name, "--score-strategies-", 19) == 0) {
                if (input->directory) return false;
                input->directory = value;
            } else return false;
        } else if (strcmp(name, "--opponent") == 0) {
            if (options->baseline || (strcmp(value, "passive") != 0 && strcmp(value, "three") != 0)) return false;
            options->baseline = value;
        } else if (strcmp(name, "--trace") == 0) {
            if (options->trace_path) return false;
            options->trace_path = value;
        } else if (strcmp(name, "--score") == 0) {
            if (seen & 1U) return false;
            seen |= 1U;
            const char *colon = strchr(value, ':');
            if (!colon || colon == value || colon - value > 2) return false;
            char left[3] = {0}; memcpy(left, value, (size_t)(colon - value));
            uintmax_t a, b;
            if (!number(left, 39, &a) || !number(colon + 1, 39, &b)) return false;
            options->match.initial_scores[0] = (int)a; options->match.initial_scores[1] = (int)b;
        } else {
            const unsigned int flag = strcmp(name, "--matches") == 0 ? 2U
                : strcmp(name, "--seed") == 0 ? 4U : strcmp(name, "--max-hands") == 0 ? 8U : 0U;
            uintmax_t parsed;
            if (!flag || (seen & flag) || !number(value, flag == 4U ? UINT_MAX : SIZE_MAX, &parsed) ||
                (flag != 4U && parsed == 0)) return false;
            seen |= flag;
            if (flag == 2U) options->match.matches = (size_t)parsed;
            else if (flag == 4U) options->match.seed = (unsigned int)parsed;
            else options->match.max_hands_per_match = (size_t)parsed;
        }
    }
    if (options->inputs[0].format == POLICY_NONE ||
        (options->baseline && options->inputs[1].format != POLICY_NONE)) return false;
    for (int p = 0; p < 2; ++p)
        if ((options->inputs[p].table == NULL) != (options->inputs[p].directory == NULL) ||
            (options->inputs[p].table && options->inputs[p].format == POLICY_NONE)) return false;
    if (options->inputs[1].format == POLICY_NONE && !options->baseline) options->baseline = "passive";
    return true;
}
static Status adapter_decide(void *context, const MusAdapterObservation *observation,
                             MusAdapterDecision *decision) {
    return mus_adapter_decide(context, observation, decision);
}
static Status baseline_decide(void *context, const MusAdapterObservation *o, MusAdapterDecision *d) {
    const bool three = strcmp(context, "three") == 0;
    MusBotAction desired = {MUS_BOT_PASS, 0};
    if (o->phase == MUS_PHASE_MUS_PLAYER_0 || o->phase == MUS_PHASE_MUS_PLAYER_1)
        desired.kind = three && o->mus_count == 0 ? MUS_BOT_MUS : MUS_BOT_NO_MUS;
    else if (o->phase == MUS_PHASE_DISCARD_PLAYER_0 || o->phase == MUS_PHASE_DISCARD_PLAYER_1)
        desired = o->legal_actions[0];
    else {
        bool pending = false, ordago = false;
        int64_t total = 0;
        for (size_t i = 0; i < o->history_count; ++i) {
            if (o->history[i].action.kind == MUS_BOT_RAISE) { total += o->history[i].action.amount; pending = true; }
            if (o->history[i].action.kind == MUS_BOT_ORDAGO) { ordago = true; pending = true; }
        }
        desired.kind = pending ? (three ? MUS_BOT_ACCEPT : MUS_BOT_REJECT) : MUS_BOT_PASS;
        if (three && !ordago && total < 9) desired = (MusBotAction){MUS_BOT_RAISE, 3};
    }
    for (size_t i = 0; i < o->legal_action_count; ++i)
        if (o->legal_actions[i].kind == desired.kind && o->legal_actions[i].amount == desired.amount) {
            *d = (MusAdapterDecision){.method = MUS_ADAPTER_FALLBACK, .selected = desired};
            return CFR_STATUS_SUCCESS;
        }
    return CFR_STATUS_INVALID_ARGUMENT;
}
static Status load_policy(const PolicyInput *input, const MusAdapterConfig *config, uint64_t seed,
                           MusAdapter **adapter) {
    Status status = input->format == POLICY_TEXT
        ? mus_adapter_open_text(input->path, config, seed, adapter)
        : mus_adapter_load(input->path, input->format == POLICY_ZSTD, config, seed, adapter);
    if (status == CFR_STATUS_SUCCESS && input->table != NULL)
        status = mus_adapter_enable_score_text(*adapter, input->table, input->directory);
    return status;
}
static bool report(const Options *options, const MusMatchResult *r, double seconds) {
    if (printf("{\"scope\":\"deployed-adapter-matches\",\"players\":2,\"muses\":1,"
        "\"numeric_total_max\":40,\"score_adjustment\":%s,\"seed\":%u,\"initial_scores\":[%d,%d],\"matches\":%zu,"
        "\"wins\":[%zu,%zu],\"win_rate_a\":%.17g,\"hands\":%zu,\"seconds\":%.9g,"
        "\"decisions\":[%" PRIu64 ",%" PRIu64 "],\"checksum\":%" PRIu64 ",\"policies\":[\"adapter\",\"%s\"],"
        "\"coverage\":[",
        options->no_adjustment ? "false" : "true", options->match.seed,
        options->match.initial_scores[0], options->match.initial_scores[1],
        r->matches, r->wins[0], r->wins[1], (double)r->wins[0] / (double)r->matches, r->hands,
        seconds, r->decisions[0], r->decisions[1], r->checksum,
        options->baseline ? options->baseline : "adapter") < 0) return false;
    bool first = true;
    for (int p = 0; p < 2; ++p) {
        for (size_t method = 0; method < MUS_MATCH_METHOD_COUNT; ++method) {
            for (size_t phase = 0; phase < MUS_MATCH_PHASE_COUNT; ++phase) {
                const uint64_t count = r->phases[p][phase][method];
                if (!count) continue;
                if (printf("%s{\"policy\":%d,\"dimension\":\"phase\",\"phase\":\"%s\",\"method\":\"%s\",\"count\":%" PRIu64 "}",
                    first ? "" : ",", p, mus_match_phase_name((MusGamePhase)phase), mus_match_method_name(method), count) < 0) return false;
                first = false;
            }
            for (size_t a = 0; a < 40; ++a)
                for (size_t b = 0; b < 40; ++b) {
                    const uint64_t count = r->scores[p][a][b][method];
                    if (!count) continue;
                    if (printf("%s{\"policy\":%d,\"dimension\":\"start-score\",\"score\":[%zu,%zu],\"method\":\"%s\",\"count\":%" PRIu64 "}",
                        first ? "" : ",", p, a, b, mus_match_method_name(method), count) < 0) return false;
                    first = false;
                }
            for (size_t action = 0; action <= MUS_BOT_DISCARD; ++action) {
                const uint64_t count = r->actions[p][action][method];
                if (!count) continue;
                if (printf("%s{\"policy\":%d,\"dimension\":\"action\",\"action\":\"%s\",\"method\":\"%s\",\"count\":%" PRIu64 "}",
                    first ? "" : ",", p, mus_bot_action_name((MusBotActionKind)action), mus_match_method_name(method), count) < 0) return false;
                first = false;
            }
        }
    }
    return printf("],\"strategy_counts\":[[%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "],"
        "[%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "]],"
        "\"strategy_order\":[\"ordago-deprecated\",\"ordago\",\"ordago-score\",\"baseline\"]}\n",
        r->strategies[0][0],r->strategies[0][1],r->strategies[0][2],r->strategies[0][3],
        r->strategies[1][0],r->strategies[1][1],r->strategies[1][2],r->strategies[1][3]) >= 0 && fflush(stdout) == 0;
}
int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) { usage(stdout); return 0; }
    Options options;
    if (!parse(argc, argv, &options)) { usage(stderr); return 2; }
    MusAdapterConfig config = mus_adapter_default_config();
    config.score_adjustment = !options.no_adjustment;
    MusAdapter *adapters[2] = {NULL, NULL};
    MusMatchPolicy policies[2] = {{0}};
    Status status = CFR_STATUS_SUCCESS;
    for (int p = 0; p < 2 && status == CFR_STATUS_SUCCESS; ++p) {
        if (p == 1 && options.baseline) {
            policies[p] = (MusMatchPolicy){baseline_decide, (void *)options.baseline, true};
        } else {
            status = load_policy(&options.inputs[p], &config,
                options.match.seed ^ (p == 0 ? UINT64_C(0x8f3f73b5cf1c9ade) : UINT64_C(0x52a13d289b4e6017)), &adapters[p]);
            policies[p] = (MusMatchPolicy){adapter_decide, adapters[p], false};
        }
    }
    if (status == CFR_STATUS_SUCCESS && options.trace_path) {
        options.match.trace = fopen(options.trace_path, "wx");
        if (!options.match.trace) status = CFR_STATUS_IO_ERROR;
    }
    MusMatchResult *result = calloc(1, sizeof(*result));
    if (!result) status = CFR_STATUS_OUT_OF_MEMORY;
    struct timespec start, end;
    if (status == CFR_STATUS_SUCCESS && clock_gettime(CLOCK_MONOTONIC, &start) != 0) status = CFR_STATUS_IO_ERROR;
    if (status == CFR_STATUS_SUCCESS) status = mus_match_evaluate(&options.match, policies, result);
    if (status == CFR_STATUS_SUCCESS && clock_gettime(CLOCK_MONOTONIC, &end) != 0) status = CFR_STATUS_IO_ERROR;
    if (options.match.trace && fclose(options.match.trace) != 0) status = CFR_STATUS_IO_ERROR;
    if (status == CFR_STATUS_SUCCESS) {
        const double seconds = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1e9;
        if (!report(&options, result, seconds)) status = CFR_STATUS_IO_ERROR;
    }
    free(result);
    mus_adapter_destroy(adapters[0]); mus_adapter_destroy(adapters[1]);
    if (status != CFR_STATUS_SUCCESS) fprintf(stderr, "Match evaluation failed: %d\n", (int)status);
    return status == CFR_STATUS_SUCCESS ? 0 : 1;
}
