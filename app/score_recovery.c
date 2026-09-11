#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cfr/checkpoint.h"
#include "cfr/info_store.h"
#include "score_recovery.h"

#define RECORD_CAPACITY 2048
#define HASH_INITIAL UINT64_C(14695981039346656037)

/* FNV-1a detects accidental mismatches. It is not authentication. Checkpoint
 * loading also validates the checkpoint format, schema, and built-in checksum. */
static uint64_t hash_bytes(uint64_t hash, const char *bytes, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        hash ^= (unsigned char)bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static Status file_hash(const char *path, uint64_t *result) {
    FILE *stream = fopen(path, "rb");
    if (stream == NULL)
        return errno == ENOENT ? CFR_STATUS_NOT_FOUND : CFR_STATUS_IO_ERROR;
    char buffer[65536];
    size_t count;
    uint64_t hash = HASH_INITIAL;
    while ((count = fread(buffer, 1, sizeof(buffer), stream)) != 0)
        hash = hash_bytes(hash, buffer, count);
    Status status = ferror(stream) ? CFR_STATUS_IO_ERROR : CFR_STATUS_SUCCESS;
    if (fclose(stream) != 0) status = CFR_STATUS_IO_ERROR;
    if (status == CFR_STATUS_SUCCESS) *result = hash;
    return status;
}

static char *cell_path(const char *directory, const MusScoreGame *game) {
    const size_t length = strlen(directory);
    if (length > SIZE_MAX - 64) return NULL;
    char *path = malloc(length + 64);
    if (path != NULL)
        (void)snprintf(path, length + 64, "%s/score-%02u-%02u.cfr.zst", directory,
                       game->initial_scores[0], game->initial_scores[1]);
    return path;
}

static bool unsigned_number(const char *text, uintmax_t maximum, uintmax_t *result) {
    if (text[0] < '0' || text[0] > '9') return false;
    char *end;
    errno = 0;
    const uintmax_t number = strtoumax(text, &end, 10);
    if (errno != 0 || *end != '\0' || number > maximum) return false;
    *result = number;
    return true;
}

static bool hexadecimal_hash(const char *text, uint64_t *result) {
    if (strlen(text) != 16 || strspn(text, "0123456789abcdef") != 16) return false;
    char *end;
    errno = 0;
    const uintmax_t value = strtoumax(text, &end, 16);
    if (errno != 0 || *end != '\0' || value > UINT64_MAX) return false;
    *result = (uint64_t)value;
    return true;
}

static Status source_hash(const ScoreRecovery *recovery, const MusScoreGame *game,
                          uint64_t *hash) {
    *hash = 0;
    if (recovery->options.source_directory == NULL) return CFR_STATUS_SUCCESS;
    char *path = cell_path(recovery->options.source_directory, game);
    if (path == NULL) return CFR_STATUS_OUT_OF_MEMORY;
    Status status = file_hash(path, hash);
    free(path);
    return status == CFR_STATUS_NOT_FOUND ? CFR_STATUS_IO_ERROR : status;
}

static Status format_record(const ScoreRecovery *recovery, const MusScoreGame *game,
    const MusScoreCell *cell, uint64_t checkpoint_hash, char *text, size_t capacity) {
    uint64_t extension_hash;
    Status status = source_hash(recovery, game, &extension_hash);
    if (status != CFR_STATUS_SUCCESS) return status;
    const ScoreRecoveryOptions *o = &recovery->options;
    const MusScoreCell *source = o->source_table == NULL ? NULL
        : &o->source_table->cells[game->initial_scores[0]][game->initial_scores[1]];
    const int length = snprintf(text, capacity,
        "%u %u %a %zu %zu %" PRIu64 " %016" PRIx64
        " %u %a mus-score-recovery/v2 schema=%s added=%zu samples=%zu min_samples=%zu target_se=%a seed=%" PRIu64
        " threads=%zu threads_per_cell=%zu reserve=%zu report=%zu"
        " prior=%u compressed=%u prior_hash=%016" PRIx64 " weight=%a mix=%a"
        " extension=%u source_hash=%016" PRIx64 " source_iterations=%zu source_seed=%" PRIu64,
        game->initial_scores[0], game->initial_scores[1], cell->value,
        cell->iterations, cell->evaluation_samples, cell->training_seed, checkpoint_hash,
        cell->variance_known ? 1U : 0U, cell->sample_variance,
        game->schema, o->iterations, o->evaluation_samples, o->evaluation_min_samples,
        o->evaluation_target_se, o->seed,
        o->threads, o->threads_per_cell, o->reserve_information_sets, o->report_every,
        o->prior_path != NULL ? 1U : 0U, o->prior_compressed ? 1U : 0U,
        recovery->prior_hash, o->prior_weight, o->uniform_mix,
        o->source_directory != NULL ? 1U : 0U, extension_hash,
        source == NULL ? 0 : source->iterations, source == NULL ? 0 : source->training_seed);
    return length < 0 || (size_t)length >= capacity
        ? CFR_STATUS_IO_ERROR : CFR_STATUS_SUCCESS;
}

Status score_recovery_close(ScoreRecovery *recovery) {
    Status status = CFR_STATUS_SUCCESS;
    if (recovery->journal != NULL && fclose(recovery->journal) != 0)
        status = CFR_STATUS_IO_ERROR;
    for (size_t a = 0; a < MUS_ORDAGO_WAGER; ++a)
        for (size_t b = 0; b < MUS_ORDAGO_WAGER; ++b) free(recovery->records[a][b]);
    *recovery = (ScoreRecovery){0};
    return status;
}

/* Consume exactly one physical line, including malformed binary content.
 * The stream belongs to this caller; unlocked reads avoid a lock per byte. */
static bool read_record_line(FILE *stream, char *line, size_t capacity, bool *valid) {
    size_t length = 0;
    int byte;
    *valid = true;
    while ((byte = getc_unlocked(stream)) != EOF && byte != '\n') {
        if (byte == 0) *valid = false;
        if (length + 1 < capacity) line[length++] = (char)byte;
        else *valid = false;
    }
    line[length] = '\0';
    if (byte == EOF) *valid = false;
    return byte != EOF || length != 0;
}

Status score_recovery_open(ScoreRecovery *recovery, const ScoreRecoveryOptions *options) {
    *recovery = (ScoreRecovery){.options = *options};
    Status status = CFR_STATUS_SUCCESS;
    if (options->prior_path != NULL) {
        status = file_hash(options->prior_path, &recovery->prior_hash);
        if (status != CFR_STATUS_SUCCESS) return status;
    }
    const size_t length = strlen(options->directory);
    if (length > SIZE_MAX - 32) return CFR_STATUS_OUT_OF_MEMORY;
    char *path = malloc(length + 32);
    if (path == NULL) return CFR_STATUS_OUT_OF_MEMORY;
    (void)snprintf(path, length + 32, "%s/recovery.txt", options->directory);
    FILE *stream = fopen(path, "rb");
    if (stream == NULL && errno != ENOENT) status = CFR_STATUS_IO_ERROR;
    char line[RECORD_CAPACITY];
    bool valid;
    while (status == CFR_STATUS_SUCCESS && stream != NULL &&
           read_record_line(stream, line, sizeof(line), &valid)) {
        if (!valid) continue;
        char *checksum = strrchr(line, '\t');
        uint64_t hash;
        if (checksum == NULL || !hexadecimal_hash(checksum + 1, &hash)) continue;
        *checksum = '\0';
        if (hash_bytes(HASH_INITIAL, line, strlen(line)) != hash) continue;
        char first[32], second[32];
        uintmax_t a, b;
        if (sscanf(line, "%31s %31s", first, second) != 2 ||
            !unsigned_number(first, MUS_ORDAGO_WAGER - 1, &a) ||
            !unsigned_number(second, MUS_ORDAGO_WAGER - 1, &b)) continue;
        char *copy = strdup(line);
        if (copy == NULL) { status = CFR_STATUS_OUT_OF_MEMORY; break; }
        free(recovery->records[a][b]);
        recovery->records[a][b] = copy;
    }
    if (stream != NULL) {
        if (ferror(stream)) status = CFR_STATUS_IO_ERROR;
        if (fclose(stream) != 0) status = CFR_STATUS_IO_ERROR;
    }
    if (status == CFR_STATUS_SUCCESS) {
        recovery->journal = fopen(path, "ab");
        if (recovery->journal == NULL) status = CFR_STATUS_IO_ERROR;
    }
    free(path);
    if (status != CFR_STATUS_SUCCESS) (void)score_recovery_close(recovery);
    return status;
}

Status score_recovery_save(ScoreRecovery *recovery, const MusScoreGame *game,
                          const MusScoreCell *cell, const char *checkpoint) {
    uint64_t hash;
    Status status = file_hash(checkpoint, &hash);
    char text[RECORD_CAPACITY];
    if (status == CFR_STATUS_SUCCESS)
        status = format_record(recovery, game, cell, hash, text, sizeof(text));
    if (status != CFR_STATUS_SUCCESS) return status;
    /* A leading newline isolates any partial tail left by a killed writer.
     * Flush each completed record before reporting the cell as saved. This
     * supports process interruption, without promising power-loss durability. */
    if (fprintf(recovery->journal, "\n%s\t%016" PRIx64 "\n", text,
                hash_bytes(HASH_INITIAL, text, strlen(text))) < 0 ||
        fflush(recovery->journal) != 0) return CFR_STATUS_IO_ERROR;
    return CFR_STATUS_SUCCESS;
}

Status score_recovery_load(const MusScoreGame *game, MusScoreCell *cell, void *context) {
    const ScoreRecovery *recovery = context;
    const char *record = recovery->records[game->initial_scores[0]][game->initial_scores[1]];
    if (record == NULL) return CFR_STATUS_NOT_FOUND;
    char tokens[9][80];
    if (sscanf(record, "%79s %79s %79s %79s %79s %79s %79s %79s %79s", tokens[0], tokens[1],
               tokens[2], tokens[3], tokens[4], tokens[5], tokens[6], tokens[7], tokens[8]) != 9)
        return CFR_STATUS_NOT_FOUND;
    uintmax_t iterations, samples, seed, known;
    uint64_t stored_hash;
    if (!unsigned_number(tokens[3], SIZE_MAX, &iterations) || iterations == 0 ||
        !unsigned_number(tokens[4], SIZE_MAX, &samples) || samples == 0 ||
        !unsigned_number(tokens[5], UINT64_MAX, &seed) ||
        !hexadecimal_hash(tokens[6], &stored_hash) ||
        !unsigned_number(tokens[7], 1, &known)) return CFR_STATUS_NOT_FOUND;
    char *end;
    errno = 0;
    const double value = strtod(tokens[2], &end);
    if (errno != 0 || *end != '\0' || !isfinite(value) || value < -1 || value > 1)
        return CFR_STATUS_NOT_FOUND;
    MusScoreCell recovered = {.ready = true, .value = value, .iterations = (size_t)iterations,
        .evaluation_samples = (size_t)samples, .training_seed = (uint64_t)seed,
        .variance_known = known != 0};
    errno = 0;
    recovered.sample_variance = strtod(tokens[8], &end);
    if (errno != 0 || end == tokens[8] || *end != '\0' || !isfinite(recovered.sample_variance) ||
        recovered.sample_variance < 0 || recovered.sample_variance > 2.0 + 1e-12 ||
        (recovered.variance_known ? samples < 2 : recovered.sample_variance != 0))
        return CFR_STATUS_NOT_FOUND;
    char expected[RECORD_CAPACITY];
    Status status = format_record(recovery, game, &recovered, stored_hash, expected, sizeof(expected));
    if (status != CFR_STATUS_SUCCESS) return status;
    if (strcmp(record, expected) != 0) return CFR_STATUS_NOT_FOUND;
    char *path = cell_path(recovery->options.directory, game);
    if (path == NULL) return CFR_STATUS_OUT_OF_MEMORY;
    uint64_t actual_hash;
    status = file_hash(path, &actual_hash);
    if (status == CFR_STATUS_SUCCESS && stored_hash != actual_hash) status = CFR_STATUS_NOT_FOUND;
    MusGameState *state = NULL;
    InfoStore store = {0};
    Trainer trainer;
    FILE *stream = NULL;
    if (status == CFR_STATUS_SUCCESS) {
        state = malloc(sizeof(*state));
        if (state == NULL) status = CFR_STATUS_OUT_OF_MEMORY;
    }
    if (status == CFR_STATUS_SUCCESS) status = mus_score_game_state_init(state, game);
    if (status == CFR_STATUS_SUCCESS) {
        stream = fopen(path, "rb");
        if (stream == NULL) status = errno == ENOENT ? CFR_STATUS_NOT_FOUND : CFR_STATUS_IO_ERROR;
    }
    if (status == CFR_STATUS_SUCCESS)
        status = cfr_checkpoint_read_zstd(stream, &game->game,
            mus_game_state_as_game_state(state), &store, &trainer);
    if (status == CFR_STATUS_SUCCESS &&
        (trainer.variant != CFR_TRAINER_VARIANT_MCCFR_EXTERNAL ||
         trainer.training_iterations != recovered.iterations)) status = CFR_STATUS_FORMAT_ERROR;
    if (stream != NULL && fclose(stream) != 0) status = CFR_STATUS_IO_ERROR;
    (void)cfr_info_store_destroy(&store);
    free(state);
    free(path);
    if (status == CFR_STATUS_FORMAT_ERROR || status == CFR_STATUS_INCOMPATIBLE_GAME)
        return CFR_STATUS_NOT_FOUND;
    if (status != CFR_STATUS_SUCCESS) return status;
    if (printf("score-recovered score=%u:%u iterations=%zu evaluation_samples=%zu\n",
               game->initial_scores[0], game->initial_scores[1], recovered.iterations,
               recovered.evaluation_samples) < 0 || fflush(stdout) != 0) return CFR_STATUS_IO_ERROR;
    *cell = recovered;
    return CFR_STATUS_SUCCESS;
}
