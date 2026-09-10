#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "mus_text_strategy.h"

/* Generic and score export rows fit here, including fifteen discard actions. */
#define LINE_CAPACITY 4096
#define CACHE_CAPACITY 256

typedef struct {
    bool valid, found;
    InfoSetKey key;
    size_t count;
    Probability probabilities[MUS_GAME_MAX_LEGAL_ACTIONS];
} CacheEntry;

struct MusTextStrategy {
    FILE *stream;
    off_t first_row, file_size;
    CacheEntry cache[CACHE_CAPACITY];
};

/* The file must stay immutable while it is open. No file-size-proportional storage. */
static Status read_line(FILE *stream, char line[LINE_CAPACITY + 1]) {
    off_t start = ftello(stream);
    if (start < 0)
        return CFR_STATUS_IO_ERROR;
    if (!fgets(line, LINE_CAPACITY + 1, stream))
        return ferror(stream) ? CFR_STATUS_IO_ERROR : CFR_STATUS_NOT_FOUND;
    off_t end = ftello(stream);
    if (end < 0)
        return CFR_STATUS_IO_ERROR;
    size_t length = strlen(line);
    if (!length || (off_t)length != end - start || line[length - 1] != '\n')
        return CFR_STATUS_FORMAT_ERROR;
    return CFR_STATUS_SUCCESS;
}

static Status seek_file(FILE *stream, off_t offset) {
    return fseeko(stream, offset, SEEK_SET) == 0 ? CFR_STATUS_SUCCESS
                                                 : CFR_STATUS_IO_ERROR;
}

static Status row_key(const char *line, InfoSetKey *key) {
    const char *prefix = "infoset key=";
    size_t length = strlen(prefix);
    if (strncmp(line, prefix, length) || !isdigit((unsigned char)line[length]))
        return CFR_STATUS_FORMAT_ERROR;
    errno = 0;
    char *end;
    long long value = strtoll(line + length, &end, 10);
    if (errno || value < 0 || value > INT64_MAX || *end != ' ')
        return CFR_STATUS_FORMAT_ERROR;
    *key = (InfoSetKey)value;
    return CFR_STATUS_SUCCESS;
}

Status mus_text_strategy_open_schema(const char *path, const char *expected_schema,
                                      size_t expected_iterations, MusTextStrategy **out) {
    if (!path || !expected_schema || !out)
        return CFR_STATUS_INVALID_ARGUMENT;
    FILE *stream = fopen(path, "rb");
    if (!stream)
        return CFR_STATUS_IO_ERROR;
    struct stat attributes;
    Status status = CFR_STATUS_SUCCESS;
    if (fstat(fileno(stream), &attributes) || !S_ISREG(attributes.st_mode) ||
        attributes.st_size <= 0)
        status = CFR_STATUS_FORMAT_ERROR;
    char line[LINE_CAPACITY + 1];
    if (status == CFR_STATUS_SUCCESS)
        status = read_line(stream, line);
    if (status == CFR_STATUS_NOT_FOUND)
        status = CFR_STATUS_FORMAT_ERROR;
    const char *prefix = "mus-strategy version=1 schema=";
    if (status == CFR_STATUS_SUCCESS) {
        if (strncmp(line, prefix, strlen(prefix)))
            status = CFR_STATUS_FORMAT_ERROR;
        else {
            char *schema = line + strlen(prefix);
            char *end = strchr(schema, ' ');
            if (!end)
                status = CFR_STATUS_FORMAT_ERROR;
            else {
                *end = '\0';
                if (strcmp(schema, expected_schema))
                    status = CFR_STATUS_INCOMPATIBLE_GAME;
                if (status == CFR_STATUS_SUCCESS && expected_iterations != 0) {
                    const char *field = "variant=mccfr-external training_iterations=";
                    const char *tail = end + 1;
                    if (strncmp(tail, field, strlen(field)) ||
                        !isdigit((unsigned char)tail[strlen(field)])) {
                        status = CFR_STATUS_FORMAT_ERROR;
                    } else {
                        errno = 0;
                        uintmax_t iterations = strtoumax(tail + strlen(field), &end, 10);
                        if (errno || *end != ' ')
                            status = CFR_STATUS_FORMAT_ERROR;
                        else if (iterations != expected_iterations)
                            status = CFR_STATUS_INCOMPATIBLE_GAME;
                    }
                }
            }
        }
    }
    off_t first_row = status == CFR_STATUS_SUCCESS ? ftello(stream) : -1;
    if (status == CFR_STATUS_SUCCESS && first_row < 0)
        status = CFR_STATUS_IO_ERROR;
    if (status == CFR_STATUS_SUCCESS && first_row < attributes.st_size) {
        InfoSetKey first_key;
        status = read_line(stream, line);
        if (status == CFR_STATUS_SUCCESS)
            status = row_key(line, &first_key);
        if (status == CFR_STATUS_SUCCESS)
            status = seek_file(stream, attributes.st_size - 1);
        if (status == CFR_STATUS_SUCCESS && fgetc(stream) != '\n')
            status = CFR_STATUS_FORMAT_ERROR;
    }
    if (status != CFR_STATUS_SUCCESS) {
        fclose(stream);
        return status;
    }
    MusTextStrategy *strategy = calloc(1, sizeof(*strategy));
    if (!strategy) {
        fclose(stream);
        return CFR_STATUS_OUT_OF_MEMORY;
    }
    strategy->stream = stream;
    strategy->first_row = first_row;
    strategy->file_size = attributes.st_size;
    *out = strategy;
    return CFR_STATUS_SUCCESS;
}

Status mus_text_strategy_open(const char *path, MusTextStrategy **out) {
    return mus_text_strategy_open_schema(path, MUS_ORDAGO_STRATEGY_SCHEMA_ID, 0, out);
}

void mus_text_strategy_close(MusTextStrategy *strategy) {
    if (strategy) {
        fclose(strategy->stream);
        free(strategy);
    }
}

static const char *action_name(Action action) {
    switch (action) {
    case MUS_ACTION_PASS:
        return "pasar";
    case MUS_ACTION_BET_TWO:
        return "envidar_2";
    case MUS_ACTION_BET_FOUR:
        return "envidar_4";
    case MUS_ACTION_BET_SEVEN:
        return "envidar_7";
    case MUS_ACTION_BET_TEN:
        return "envidar_10";
    case MUS_ACTION_REJECT:
        return "no_querer";
    case MUS_ACTION_ACCEPT:
        return "querer";
    case MUS_ACTION_ORDAGO:
        return "ordago";
    case MUS_ACTION_MUS:
        return "mus";
    case MUS_ACTION_NO_MUS:
        return "no_mus";
    default:
        return NULL;
    }
}

static Status discard_name(char name[128], const MusOrdagoInformationSet *info,
                           Action action) {
    if (action <= MUS_ACTION_DISCARD_MASK_0 || action > MUS_ACTION_DISCARD_MASK(15))
        return CFR_STATUS_FORMAT_ERROR;
    unsigned int mask = MUS_ACTION_DISCARD_MASK_VALUE(action), position = 0;
    size_t used = strlen("cambiar_");
    memcpy(name, "cambiar_", used + 1);
    bool first = true;
    for (int card = 0; card < MUS_GAME_CLASS_COUNT; ++card) {
        unsigned int selected = 0;
        for (unsigned int copy = 0; copy < info->hand[card]; ++copy)
            selected += (mask >> (position + copy)) & 1U;
        position += info->hand[card];
        if (!selected)
            continue;
        int length = snprintf(name + used, 128 - used, "%s%s:%u", first ? "" : "+",
                              mus_game_class_name((MusCardClass)card), selected);
        if (length < 0 || (size_t)length >= 128 - used)
            return CFR_STATUS_FORMAT_ERROR;
        used += (size_t)length;
        first = false;
    }
    return first ? CFR_STATUS_FORMAT_ERROR : CFR_STATUS_SUCCESS;
}

static Status parse_policy(char *line, const MusOrdagoInformationSet *info,
                           const Action *actions, size_t count,
                           Probability *probabilities) {
    size_t length = strlen(line);
    line[length - 1] = '\0';
    double sum = 0;
    /* The exporter writes exactly these named actions, in the game API's order. */
    for (size_t index = count; index > 0; --index) {
        char *token = strrchr(line, ' ');
        if (!token)
            return CFR_STATUS_FORMAT_ERROR;
        *token++ = '\0';
        char name[128];
        const char *expected = action_name(actions[index - 1]);
        if (!expected) {
            Status status = discard_name(name, info, actions[index - 1]);
            if (status != CFR_STATUS_SUCCESS)
                return status;
            expected = name;
        }
        size_t name_length = strlen(expected);
        if (strncmp(token, expected, name_length) || token[name_length] != '=')
            return CFR_STATUS_FORMAT_ERROR;
        char *end;
        errno = 0;
        double value = strtod(token + name_length + 1, &end);
        if (end == token + name_length + 1 || *end || !isfinite(value) || value < 0 ||
            value > 1 || (errno == ERANGE && value == 0))
            return CFR_STATUS_NUMERIC_ERROR;
        probabilities[index - 1] = value;
        sum += value;
    }
    return fabs(sum - 1) <= 1e-9 ? CFR_STATUS_SUCCESS : CFR_STATUS_NUMERIC_ERROR;
}

static Status find_line(MusTextStrategy *strategy, InfoSetKey key, char *line) {
    FILE *stream = strategy->stream;
    off_t low = strategy->first_row, high = strategy->file_size;
    InfoSetKey lower_key = -1, upper_key = INT64_MAX;
    /* Low and high are row boundaries. The midpoint may be inside a row. */
    while (high - low > 2 * LINE_CAPACITY) {
        off_t midpoint = low + (high - low) / 2;
        Status status = seek_file(stream, midpoint);
        if (status != CFR_STATUS_SUCCESS)
            return status;
        status = read_line(stream, line); /* Discard the partial midpoint row. */
        if (status != CFR_STATUS_SUCCESS)
            return status == CFR_STATUS_NOT_FOUND ? CFR_STATUS_FORMAT_ERROR : status;
        off_t row_start = ftello(stream);
        if (row_start <= low || row_start >= high)
            return CFR_STATUS_FORMAT_ERROR;
        status = read_line(stream, line);
        if (status != CFR_STATUS_SUCCESS)
            return status == CFR_STATUS_NOT_FOUND ? CFR_STATUS_FORMAT_ERROR : status;
        InfoSetKey found;
        status = row_key(line, &found);
        if (status != CFR_STATUS_SUCCESS)
            return status;
        if (found <= lower_key || found >= upper_key)
            return CFR_STATUS_FORMAT_ERROR;
        if (found == key)
            return CFR_STATUS_SUCCESS;
        if (found < key) {
            low = ftello(stream);
            lower_key = found;
        } else {
            high = row_start;
            upper_key = found;
        }
    }
    Status status = seek_file(stream, low);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    while (low < high) {
        status = read_line(stream, line);
        if (status != CFR_STATUS_SUCCESS)
            return status == CFR_STATUS_NOT_FOUND ? CFR_STATUS_FORMAT_ERROR : status;
        InfoSetKey found;
        status = row_key(line, &found);
        if (status != CFR_STATUS_SUCCESS)
            return status;
        if (found <= lower_key || found >= upper_key)
            return CFR_STATUS_FORMAT_ERROR;
        if (found == key)
            return CFR_STATUS_SUCCESS;
        if (found > key)
            return CFR_STATUS_NOT_FOUND;
        lower_key = found;
        low = ftello(stream);
        if (low < 0)
            return CFR_STATUS_IO_ERROR;
    }
    return CFR_STATUS_NOT_FOUND;
}

Status mus_text_strategy_find(MusTextStrategy *strategy, InfoSetKey key,
                              const MusOrdagoInformationSet *information,
                              const Action *actions, size_t count,
                              Probability *probabilities) {
    if (!strategy || key < 0 || !information || !actions || !count ||
        count > MUS_GAME_MAX_LEGAL_ACTIONS || !probabilities)
        return CFR_STATUS_INVALID_ARGUMENT;
    size_t slot = (size_t)(((uint64_t)key * UINT64_C(11400714819323198485)) >> 56);
    CacheEntry *cache = &strategy->cache[slot];
    if (cache->valid && cache->key == key) {
        if (!cache->found)
            return CFR_STATUS_NOT_FOUND;
        if (cache->count != count)
            return CFR_STATUS_FORMAT_ERROR;
        memcpy(probabilities, cache->probabilities, count * sizeof(*probabilities));
        return CFR_STATUS_SUCCESS;
    }
    char line[LINE_CAPACITY + 1];
    Status status = find_line(strategy, key, line);
    Probability temporary[MUS_GAME_MAX_LEGAL_ACTIONS];
    if (status == CFR_STATUS_SUCCESS)
        status = parse_policy(line, information, actions, count, temporary);
    if (status == CFR_STATUS_SUCCESS || status == CFR_STATUS_NOT_FOUND) {
        *cache = (CacheEntry){.valid = true,
                              .found = status == CFR_STATUS_SUCCESS,
                              .key = key,
                              .count = count};
        if (status == CFR_STATUS_SUCCESS) {
            memcpy(cache->probabilities, temporary, count * sizeof(*temporary));
            memcpy(probabilities, temporary, count * sizeof(*temporary));
        }
    }
    return status;
}
