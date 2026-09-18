#define _POSIX_C_SOURCE 200809L

/* Convert ordago-score checkpoints into compact policy files for play.
 *
 *   mus-score-compact CHECKPOINT OUTPUT
 *   mus-score-compact --table VALUES.TXT --checkpoints DIR --output DIR
 *
 * The table form converts every published cell, skipping outputs that already
 * exist, and reports one JSON object per cell. Sources are never modified. */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "mus_solver/mus_score.h"
#include "mus_solver/mus_score_compact.h"

static const char *status_text(Status status) {
    switch (status) {
    case CFR_STATUS_SUCCESS: return "success";
    case CFR_STATUS_INVALID_ARGUMENT: return "invalid_argument";
    case CFR_STATUS_OUT_OF_MEMORY: return "out_of_memory";
    case CFR_STATUS_IO_ERROR: return "io_error";
    case CFR_STATUS_FORMAT_ERROR: return "format_error";
    case CFR_STATUS_INCOMPATIBLE_GAME: return "incompatible_game";
    case CFR_STATUS_NUMERIC_ERROR: return "numeric_error";
    case CFR_STATUS_NOT_FOUND: return "not_found";
    default: return "error";
    }
}

static int convert(const char *checkpoint, const char *output, unsigned int a,
                   unsigned int b, bool report) {
    uint64_t nodes = 0, written = 0;
    const Status status = mus_score_compact_write(checkpoint, output, &nodes, &written);
    if (status != CFR_STATUS_SUCCESS) {
        fprintf(stderr, "error: %s -> %s: %s\n", checkpoint, output, status_text(status));
        return 1;
    }
    struct stat information;
    const long long bytes = stat(output, &information) == 0 ? (long long)information.st_size : -1;
    if (report)
        printf("{\"cell\":\"%u:%u\",\"source_nodes\":%" PRIu64 ",\"stored_nodes\":%" PRIu64
               ",\"bytes\":%lld}\n", a, b, nodes, written, bytes);
    else
        printf("{\"source_nodes\":%" PRIu64 ",\"stored_nodes\":%" PRIu64 ",\"bytes\":%lld}\n",
               nodes, written, bytes);
    (void)fflush(stdout);
    return 0;
}

static int verify(const char *checkpoint, const char *compact) {
    uint64_t checked = 0, mismatched = 0;
    double largest = 0;
    const Status status =
        mus_score_compact_verify(checkpoint, compact, &checked, &mismatched, &largest);
    if (status != CFR_STATUS_SUCCESS) {
        fprintf(stderr, "error: verificacion %s: %s\n", compact, status_text(status));
        return 1;
    }
    printf("{\"checked_nodes\":%" PRIu64 ",\"mismatched_nodes\":%" PRIu64
           ",\"largest_difference\":%.9g}\n", checked, mismatched, largest);
    (void)fflush(stdout);
    return mismatched ? 1 : 0;
}

static int convert_table(const char *values_path, const char *checkpoints, const char *output) {
    FILE *stream = fopen(values_path, "r");
    if (stream == NULL) {
        fprintf(stderr, "error: no se pudo abrir '%s'\n", values_path);
        return 1;
    }
    MusScoreTable *table = malloc(sizeof(*table));
    Status status = table == NULL ? CFR_STATUS_OUT_OF_MEMORY
                                  : mus_score_table_read(stream, table);
    if (fclose(stream) != 0 && status == CFR_STATUS_SUCCESS)
        status = CFR_STATUS_IO_ERROR;
    if (status != CFR_STATUS_SUCCESS) {
        fprintf(stderr, "error: tabla invalida: %s\n", status_text(status));
        free(table);
        return 1;
    }
    int failures = 0;
    for (unsigned int a = 0; a < table->target; ++a) {
        for (unsigned int b = 0; b < table->target; ++b) {
            if (!table->cells[a][b].ready || !mus_score_table_contains(table, a, b))
                continue;
            char source[4096], destination[4096];
            (void)snprintf(source, sizeof(source), "%s/score-%02u-%02u.cfr.zst",
                           checkpoints, a, b);
            (void)snprintf(destination, sizeof(destination), "%s/score-%02u-%02u.mscz",
                           output, a, b);
            struct stat existing;
            if (stat(destination, &existing) == 0)
                continue;
            failures += convert(source, destination, a, b, true);
        }
    }
    free(table);
    return failures ? 1 : 0;
}

int main(int argc, char **argv) {
    const char *values = NULL, *checkpoints = NULL, *output = NULL;
    bool verifying = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--table") && i + 1 < argc)
            values = argv[++i];
        else if (!strcmp(argv[i], "--checkpoints") && i + 1 < argc)
            checkpoints = argv[++i];
        else if (!strcmp(argv[i], "--output") && i + 1 < argc)
            output = argv[++i];
        else if (!strcmp(argv[i], "--verify"))
            verifying = true;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("Uso: %s CHECKPOINT SALIDA\n"
                   "     %s --verify CHECKPOINT COMPACTO\n"
                   "     %s --table values.txt --checkpoints DIR --output DIR\n",
                   argv[0], argv[0], argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: opcion desconocida '%s'\n", argv[i]);
            return 2;
        }
    }
    if (values != NULL || checkpoints != NULL || output != NULL) {
        if (values == NULL || checkpoints == NULL || output == NULL) {
            fprintf(stderr, "error: --table, --checkpoints y --output se usan juntos\n");
            return 2;
        }
        return convert_table(values, checkpoints, output);
    }
    if (argc != (verifying ? 4 : 3)) {
        fprintf(stderr, "Uso: %s [--verify] CHECKPOINT SALIDA\n", argv[0]);
        return 2;
    }
    if (verifying)
        return verify(argv[2], argv[3]);
    return convert(argv[1], argv[2], 0, 0, false);
}
