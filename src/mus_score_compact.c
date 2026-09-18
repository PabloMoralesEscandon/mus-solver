#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zstd.h>

#include "mus_solver/mus_score_compact.h"

/* File layout, little-endian throughout:
 *
 *   header   magic, version, schema, iterations, node count, block count,
 *            index offset, and an FNV-1a hash of the preceding header bytes
 *   blocks   Zstandard frames, each holding up to BLOCK_NODES records:
 *            LEB128 key delta, one byte action count, then one uint16 per
 *            action whose values sum to 65535
 *   index    one entry per block: first key, offset, compressed and raw sizes,
 *            followed by an FNV-1a hash of the index
 *
 * The checkpoint reader is deliberately independent of C-CFR: it streams the
 * frozen v1 checkpoint format, whose records are already sorted by key.
 */
#define MAGIC "MUSSCMP\0"
#define MAGIC_LENGTH 8
#define SCHEMA_CAPACITY 256
#define HEADER_CAPACITY (MAGIC_LENGTH + 4 + 4 + SCHEMA_CAPACITY + 8 + 8 + 8 + 8 + 8)
#define INDEX_ENTRY 24
#define MAX_ACTIONS MUS_GAME_MAX_LEGAL_ACTIONS
#define QUANTIZED_TOTAL 65535u
#define STREAM_BUFFER (1u << 20)
#define CACHED_BLOCKS 4
#define CHECKPOINT_MAGIC "CFRCKPT\0"

typedef struct {
    int64_t first_key;
    uint64_t offset;
    uint32_t compressed, raw;
} IndexEntry;

typedef struct {
    bool valid;
    uint64_t block;
    size_t length;
    unsigned char *data;
} CachedBlock;

struct MusScoreCompact {
    FILE *file;
    char schema[SCHEMA_CAPACITY];
    size_t iterations;
    uint64_t node_count, block_count;
    IndexEntry *index;
    ZSTD_DCtx *zstd;
    CachedBlock cache[CACHED_BLOCKS];
    size_t next_cache;
    size_t largest_raw;
};

static uint64_t hash_bytes(const unsigned char *bytes, size_t length) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void put_u32(unsigned char *out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out[i] = (unsigned char)(value >> (8 * i));
}

static void put_u64(unsigned char *out, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
        out[i] = (unsigned char)(value >> (8 * i));
}

static uint32_t get_u32(const unsigned char *in) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= (uint32_t)in[i] << (8 * i);
    return value;
}

static uint64_t get_u64(const unsigned char *in) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= (uint64_t)in[i] << (8 * i);
    return value;
}

static size_t put_leb128(unsigned char *out, uint64_t value) {
    size_t length = 0;
    do {
        unsigned char byte = (unsigned char)(value & 0x7f);
        value >>= 7;
        if (value)
            byte |= 0x80;
        out[length++] = byte;
    } while (value);
    return length;
}

static bool get_leb128(const unsigned char *in, size_t available, size_t *position,
                       uint64_t *value_out) {
    uint64_t value = 0;
    unsigned shift = 0;
    while (*position < available) {
        const unsigned char byte = in[(*position)++];
        if (shift > 63)
            return false;
        value |= (uint64_t)(byte & 0x7f) << shift;
        if (!(byte & 0x80)) {
            *value_out = value;
            return true;
        }
        shift += 7;
    }
    return false;
}

/* Reflected CRC32, the checksum the checkpoint format appends. This running
 * value omits the final inversion, so reading the stored checksum into it
 * leaves the residue below. */
#define CRC_RESIDUE UINT32_C(0xdebb20e3)

static uint32_t crc32_update(uint32_t crc, const unsigned char *bytes, size_t length) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t value = i;
            for (unsigned bit = 0; bit < 8; ++bit)
                value = (value & 1) ? (value >> 1) ^ UINT32_C(0xedb88320) : value >> 1;
            table[i] = value;
        }
        ready = true;
    }
    for (size_t i = 0; i < length; ++i)
        crc = (crc >> 8) ^ table[(crc ^ bytes[i]) & 0xff];
    return crc;
}

/* Streaming reader for the frozen checkpoint format. */
typedef struct {
    FILE *file;
    ZSTD_DStream *zstd;
    ZSTD_inBuffer input;
    unsigned char compressed[STREAM_BUFFER], decoded[STREAM_BUFFER];
    size_t position, length;
    uint32_t crc;
    bool done;
} CheckpointReader;

static Status checkpoint_refill(CheckpointReader *reader) {
    if (reader->done)
        return CFR_STATUS_FORMAT_ERROR;
    ZSTD_outBuffer out = {reader->decoded, STREAM_BUFFER, 0};
    while (!out.pos) {
        if (reader->input.pos == reader->input.size) {
            reader->input.size = fread(reader->compressed, 1, STREAM_BUFFER, reader->file);
            reader->input.pos = 0;
            if (!reader->input.size)
                return CFR_STATUS_FORMAT_ERROR;
        }
        const size_t remaining =
            ZSTD_decompressStream(reader->zstd, &out, &reader->input);
        if (ZSTD_isError(remaining))
            return CFR_STATUS_FORMAT_ERROR;
        if (!remaining) {
            reader->done = true;
            break;
        }
    }
    reader->position = 0;
    reader->length = out.pos;
    reader->crc = crc32_update(reader->crc, reader->decoded, out.pos);
    return out.pos ? CFR_STATUS_SUCCESS : CFR_STATUS_FORMAT_ERROR;
}

static Status checkpoint_bytes(CheckpointReader *reader, unsigned char *out, size_t size) {
    while (size) {
        if (reader->position == reader->length) {
            const Status status = checkpoint_refill(reader);
            if (status != CFR_STATUS_SUCCESS)
                return status;
        }
        size_t available = reader->length - reader->position;
        if (available > size)
            available = size;
        memcpy(out, reader->decoded + reader->position, available);
        reader->position += available;
        out += available;
        size -= available;
    }
    return CFR_STATUS_SUCCESS;
}

static Status checkpoint_u64(CheckpointReader *reader, uint64_t *value_out) {
    unsigned char bytes[8];
    const Status status = checkpoint_bytes(reader, bytes, sizeof(bytes));
    if (status != CFR_STATUS_SUCCESS)
        return status;
    *value_out = get_u64(bytes);
    return CFR_STATUS_SUCCESS;
}

static Status checkpoint_u32(CheckpointReader *reader, uint32_t *value_out) {
    unsigned char bytes[4];
    const Status status = checkpoint_bytes(reader, bytes, sizeof(bytes));
    if (status != CFR_STATUS_SUCCESS)
        return status;
    *value_out = get_u32(bytes);
    return CFR_STATUS_SUCCESS;
}

static Status checkpoint_double(CheckpointReader *reader, double *value_out) {
    uint64_t bits;
    const Status status = checkpoint_u64(reader, &bits);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    memcpy(value_out, &bits, sizeof(*value_out));
    if (!isfinite(*value_out))
        return CFR_STATUS_NUMERIC_ERROR;
    return CFR_STATUS_SUCCESS;
}

/* Consumes the stored checksum and confirms nothing follows it. */
static Status checkpoint_finish(CheckpointReader *reader) {
    unsigned char stored[4];
    Status status = checkpoint_bytes(reader, stored, sizeof(stored));
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (reader->crc != CRC_RESIDUE)
        return CFR_STATUS_FORMAT_ERROR;
    if (reader->position != reader->length)
        return CFR_STATUS_FORMAT_ERROR;
    ZSTD_outBuffer out = {reader->decoded, STREAM_BUFFER, 0};
    if (!reader->done) {
        if (reader->input.pos == reader->input.size) {
            reader->input.size = fread(reader->compressed, 1, STREAM_BUFFER, reader->file);
            reader->input.pos = 0;
        }
        const size_t remaining = ZSTD_decompressStream(reader->zstd, &out, &reader->input);
        if (ZSTD_isError(remaining) || out.pos)
            return CFR_STATUS_FORMAT_ERROR;
    }
    return CFR_STATUS_SUCCESS;
}

static Status checkpoint_header(CheckpointReader *reader, char *schema,
                                size_t *iterations_out, uint64_t *node_count_out) {
    unsigned char magic[MAGIC_LENGTH];
    Status status = checkpoint_bytes(reader, magic, sizeof(magic));
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (memcmp(magic, CHECKPOINT_MAGIC, sizeof(magic)) != 0)
        return CFR_STATUS_FORMAT_ERROR;
    uint32_t value;
    status = checkpoint_u32(reader, &value);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (value != 1)
        return CFR_STATUS_FORMAT_ERROR;
    status = checkpoint_u32(reader, &value);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (!value || value >= SCHEMA_CAPACITY)
        return CFR_STATUS_FORMAT_ERROR;
    const uint32_t schema_length = value;
    status = checkpoint_bytes(reader, (unsigned char *)schema, schema_length);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    schema[schema_length] = '\0';
    status = checkpoint_u32(reader, &value);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    if (value != 2) /* External-sampling MCCFR, as every score cell uses. */
        return CFR_STATUS_INCOMPATIBLE_GAME;
    uint64_t discarded;
    status = checkpoint_u64(reader, &discarded); /* RNG state. */
    if (status != CFR_STATUS_SUCCESS)
        return status;
    uint64_t iterations;
    status = checkpoint_u64(reader, &iterations);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    for (unsigned i = 0; i < 4 && status == CFR_STATUS_SUCCESS; ++i)
        status = checkpoint_u64(reader, &discarded); /* Training statistics. */
    if (status != CFR_STATUS_SUCCESS)
        return status;
    status = checkpoint_u64(reader, node_count_out);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    *iterations_out = (size_t)iterations;
    return CFR_STATUS_SUCCESS;
}

typedef struct {
    FILE *file;
    unsigned char raw[MUS_SCORE_COMPACT_BLOCK_NODES * (10 + 1 + 2 * MAX_ACTIONS)];
    size_t raw_length, nodes;
    unsigned char *compressed;
    size_t compressed_capacity;
    IndexEntry *index;
    uint64_t block_count, block_capacity;
    int64_t block_first_key, previous_key;
    uint64_t offset;
} Blocks;

static Status flush_block(Blocks *blocks) {
    if (!blocks->nodes)
        return CFR_STATUS_SUCCESS;
    const size_t bound = ZSTD_compressBound(blocks->raw_length);
    if (bound > blocks->compressed_capacity) {
        unsigned char *grown = realloc(blocks->compressed, bound);
        if (grown == NULL)
            return CFR_STATUS_OUT_OF_MEMORY;
        blocks->compressed = grown;
        blocks->compressed_capacity = bound;
    }
    const size_t written =
        ZSTD_compress(blocks->compressed, bound, blocks->raw, blocks->raw_length, 6);
    if (ZSTD_isError(written))
        return CFR_STATUS_IO_ERROR;
    if (blocks->block_count == blocks->block_capacity) {
        const uint64_t capacity = blocks->block_capacity ? blocks->block_capacity * 2 : 1024;
        IndexEntry *grown = realloc(blocks->index, (size_t)capacity * sizeof(*grown));
        if (grown == NULL)
            return CFR_STATUS_OUT_OF_MEMORY;
        blocks->index = grown;
        blocks->block_capacity = capacity;
    }
    blocks->index[blocks->block_count] = (IndexEntry){
        .first_key = blocks->block_first_key,
        .offset = blocks->offset,
        .compressed = (uint32_t)written,
        .raw = (uint32_t)blocks->raw_length,
    };
    if (fwrite(blocks->compressed, 1, written, blocks->file) != written)
        return CFR_STATUS_IO_ERROR;
    blocks->block_count += 1;
    blocks->offset += written;
    blocks->raw_length = 0;
    blocks->nodes = 0;
    return CFR_STATUS_SUCCESS;
}

static Status append_node(Blocks *blocks, int64_t key, size_t count,
                          const double *probabilities) {
    if (!blocks->nodes) {
        blocks->block_first_key = key;
        blocks->previous_key = key;
    }
    unsigned char *out = blocks->raw + blocks->raw_length;
    const uint64_t delta = blocks->nodes ? (uint64_t)(key - blocks->previous_key) : 0;
    size_t length = put_leb128(out, delta);
    out[length++] = (unsigned char)count;
    /* Largest-remainder quantization keeps the stored values summing exactly. */
    uint32_t quantized[MAX_ACTIONS];
    double remainder[MAX_ACTIONS];
    uint32_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        const double scaled = probabilities[i] * QUANTIZED_TOTAL;
        quantized[i] = (uint32_t)scaled;
        remainder[i] = scaled - (double)quantized[i];
        total += quantized[i];
    }
    while (total < QUANTIZED_TOTAL) {
        size_t best = 0;
        for (size_t i = 1; i < count; ++i)
            if (remainder[i] > remainder[best])
                best = i;
        quantized[best] += 1;
        remainder[best] = -1;
        total += 1;
    }
    for (size_t i = 0; i < count; ++i) {
        out[length++] = (unsigned char)(quantized[i] & 0xff);
        out[length++] = (unsigned char)(quantized[i] >> 8);
    }
    blocks->raw_length += length;
    blocks->previous_key = key;
    blocks->nodes += 1;
    if (blocks->nodes == MUS_SCORE_COMPACT_BLOCK_NODES)
        return flush_block(blocks);
    return CFR_STATUS_SUCCESS;
}

Status mus_score_compact_write(const char *checkpoint_path, const char *output_path,
                               uint64_t *nodes_out, uint64_t *written_out) {
    if (checkpoint_path == NULL || output_path == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    struct stat existing;
    if (stat(output_path, &existing) == 0)
        return CFR_STATUS_INVALID_ARGUMENT;
    CheckpointReader *reader = calloc(1, sizeof(*reader));
    Blocks *blocks = calloc(1, sizeof(*blocks));
    if (reader == NULL || blocks == NULL) {
        free(reader);
        free(blocks);
        return CFR_STATUS_OUT_OF_MEMORY;
    }
    Status status = CFR_STATUS_SUCCESS;
    char schema[SCHEMA_CAPACITY] = {0};
    size_t iterations = 0;
    uint64_t source_nodes = 0, kept = 0;
    reader->file = fopen(checkpoint_path, "rb");
    reader->zstd = ZSTD_createDStream();
    if (reader->file == NULL || reader->zstd == NULL ||
        ZSTD_isError(ZSTD_initDStream(reader->zstd)))
        status = CFR_STATUS_IO_ERROR;
    reader->input.src = reader->compressed;
    reader->crc = UINT32_MAX;
    if (status == CFR_STATUS_SUCCESS)
        status = checkpoint_header(reader, schema, &iterations, &source_nodes);
    blocks->file = status == CFR_STATUS_SUCCESS ? fopen(output_path, "wb") : NULL;
    if (status == CFR_STATUS_SUCCESS && blocks->file == NULL)
        status = CFR_STATUS_IO_ERROR;

    const size_t schema_length = strlen(schema);
    unsigned char header[HEADER_CAPACITY] = {0};
    size_t header_length = 0;
    if (status == CFR_STATUS_SUCCESS) {
        memcpy(header, MAGIC, MAGIC_LENGTH);
        header_length = MAGIC_LENGTH;
        put_u32(header + header_length, MUS_SCORE_COMPACT_VERSION);
        header_length += 4;
        put_u32(header + header_length, (uint32_t)schema_length);
        header_length += 4;
        memcpy(header + header_length, schema, schema_length);
        header_length += schema_length;
        put_u64(header + header_length, (uint64_t)iterations);
        header_length += 8;
        /* Node count, block count, index offset and hash are filled in later. */
        memset(header + header_length, 0, 8 + 8 + 8 + 8);
        header_length += 8 + 8 + 8 + 8;
        if (fwrite(header, 1, header_length, blocks->file) != header_length)
            status = CFR_STATUS_IO_ERROR;
        blocks->offset = header_length;
    }

    double probabilities[MAX_ACTIONS];
    double strategy[MAX_ACTIONS];
    int64_t previous_key = 0;
    for (uint64_t index = 0; index < source_nodes && status == CFR_STATUS_SUCCESS; ++index) {
        uint64_t encoded, count;
        status = checkpoint_u64(reader, &encoded);
        if (status == CFR_STATUS_SUCCESS)
            status = checkpoint_u64(reader, &count);
        if (status != CFR_STATUS_SUCCESS)
            break;
        if (!count || count > MAX_ACTIONS) {
            status = CFR_STATUS_FORMAT_ERROR;
            break;
        }
        const int64_t key = (int64_t)encoded;
        if (index && key <= previous_key) {
            status = CFR_STATUS_FORMAT_ERROR;
            break;
        }
        previous_key = key;
        double discarded;
        for (uint64_t i = 0; i < count && status == CFR_STATUS_SUCCESS; ++i)
            status = checkpoint_double(reader, &discarded); /* Regrets. */
        double mass = 0;
        for (uint64_t i = 0; i < count && status == CFR_STATUS_SUCCESS; ++i) {
            status = checkpoint_double(reader, &strategy[i]);
            if (status == CFR_STATUS_SUCCESS && strategy[i] < 0)
                status = CFR_STATUS_NUMERIC_ERROR;
            if (status == CFR_STATUS_SUCCESS)
                mass += strategy[i];
        }
        if (status != CFR_STATUS_SUCCESS)
            break;
        if (mass <= 0 || !isfinite(mass))
            continue; /* Absence of evidence, like the binary store. */
        for (uint64_t i = 0; i < count; ++i)
            probabilities[i] = strategy[i] / mass;
        status = append_node(blocks, key, (size_t)count, probabilities);
        kept += 1;
    }
    if (status == CFR_STATUS_SUCCESS)
        status = checkpoint_finish(reader);
    if (status == CFR_STATUS_SUCCESS)
        status = flush_block(blocks);

    if (status == CFR_STATUS_SUCCESS) {
        const uint64_t index_offset = blocks->offset;
        unsigned char entry[INDEX_ENTRY];
        uint64_t index_hash = UINT64_C(1469598103934665603);
        for (uint64_t i = 0; i < blocks->block_count && status == CFR_STATUS_SUCCESS; ++i) {
            put_u64(entry, (uint64_t)blocks->index[i].first_key);
            put_u64(entry + 8, blocks->index[i].offset);
            put_u32(entry + 16, blocks->index[i].compressed);
            put_u32(entry + 20, blocks->index[i].raw);
            index_hash ^= hash_bytes(entry, sizeof(entry));
            index_hash *= UINT64_C(1099511628211);
            if (fwrite(entry, 1, sizeof(entry), blocks->file) != sizeof(entry))
                status = CFR_STATUS_IO_ERROR;
        }
        unsigned char tail[8];
        put_u64(tail, index_hash);
        if (status == CFR_STATUS_SUCCESS &&
            fwrite(tail, 1, sizeof(tail), blocks->file) != sizeof(tail))
            status = CFR_STATUS_IO_ERROR;
        if (status == CFR_STATUS_SUCCESS) {
            size_t position = MAGIC_LENGTH + 4 + 4 + schema_length + 8;
            put_u64(header + position, kept);
            put_u64(header + position + 8, blocks->block_count);
            put_u64(header + position + 16, index_offset);
            put_u64(header + position + 24, hash_bytes(header, position + 24));
            if (fseek(blocks->file, 0, SEEK_SET) != 0 ||
                fwrite(header, 1, header_length, blocks->file) != header_length)
                status = CFR_STATUS_IO_ERROR;
        }
    }
    if (blocks->file != NULL) {
        if (fflush(blocks->file) != 0 || fclose(blocks->file) != 0)
            status = status == CFR_STATUS_SUCCESS ? CFR_STATUS_IO_ERROR : status;
    }
    if (status != CFR_STATUS_SUCCESS)
        (void)remove(output_path);
    if (reader->file != NULL)
        (void)fclose(reader->file);
    ZSTD_freeDStream(reader->zstd);
    free(reader);
    free(blocks->index);
    free(blocks->compressed);
    free(blocks);
    if (status == CFR_STATUS_SUCCESS) {
        if (nodes_out != NULL)
            *nodes_out = source_nodes;
        if (written_out != NULL)
            *written_out = kept;
    }
    return status;
}

Status mus_score_compact_open(const char *path, const char *expected_schema,
                              size_t expected_iterations, MusScoreCompact **out) {
    if (path == NULL || out == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    MusScoreCompact *compact = calloc(1, sizeof(*compact));
    if (compact == NULL)
        return CFR_STATUS_OUT_OF_MEMORY;
    Status status = CFR_STATUS_SUCCESS;
    compact->file = fopen(path, "rb");
    compact->zstd = ZSTD_createDCtx();
    if (compact->file == NULL || compact->zstd == NULL)
        status = CFR_STATUS_IO_ERROR;
    unsigned char header[HEADER_CAPACITY];
    size_t fixed = MAGIC_LENGTH + 4 + 4;
    if (status == CFR_STATUS_SUCCESS && fread(header, 1, fixed, compact->file) != fixed)
        status = CFR_STATUS_FORMAT_ERROR;
    uint32_t schema_length = 0;
    if (status == CFR_STATUS_SUCCESS) {
        if (memcmp(header, MAGIC, MAGIC_LENGTH) != 0 ||
            get_u32(header + MAGIC_LENGTH) != MUS_SCORE_COMPACT_VERSION)
            status = CFR_STATUS_FORMAT_ERROR;
        schema_length = get_u32(header + MAGIC_LENGTH + 4);
        if (!schema_length || schema_length >= SCHEMA_CAPACITY)
            status = CFR_STATUS_FORMAT_ERROR;
    }
    if (status == CFR_STATUS_SUCCESS) {
        const size_t rest = schema_length + 8 + 8 + 8 + 8 + 8;
        if (fread(header + fixed, 1, rest, compact->file) != rest)
            status = CFR_STATUS_FORMAT_ERROR;
    }
    uint64_t index_offset = 0;
    if (status == CFR_STATUS_SUCCESS) {
        memcpy(compact->schema, header + fixed, schema_length);
        compact->schema[schema_length] = '\0';
        size_t position = fixed + schema_length;
        compact->iterations = (size_t)get_u64(header + position);
        compact->node_count = get_u64(header + position + 8);
        compact->block_count = get_u64(header + position + 16);
        index_offset = get_u64(header + position + 24);
        const uint64_t stored = get_u64(header + position + 32);
        if (stored != hash_bytes(header, position + 32))
            status = CFR_STATUS_FORMAT_ERROR;
    }
    if (status == CFR_STATUS_SUCCESS && expected_schema != NULL &&
        strcmp(expected_schema, compact->schema) != 0)
        status = CFR_STATUS_INCOMPATIBLE_GAME;
    if (status == CFR_STATUS_SUCCESS && expected_iterations &&
        expected_iterations != compact->iterations)
        status = CFR_STATUS_INCOMPATIBLE_GAME;
    if (status == CFR_STATUS_SUCCESS &&
        ((compact->block_count == 0) != (compact->node_count == 0)))
        status = CFR_STATUS_FORMAT_ERROR;
    if (status == CFR_STATUS_SUCCESS && compact->block_count) {
        if (compact->block_count > SIZE_MAX / sizeof(*compact->index))
            status = CFR_STATUS_FORMAT_ERROR;
        else {
            compact->index = malloc((size_t)compact->block_count * sizeof(*compact->index));
            if (compact->index == NULL)
                status = CFR_STATUS_OUT_OF_MEMORY;
        }
    }
    if (status == CFR_STATUS_SUCCESS && compact->block_count) {
        if (fseek(compact->file, (long)index_offset, SEEK_SET) != 0)
            status = CFR_STATUS_FORMAT_ERROR;
        uint64_t index_hash = UINT64_C(1469598103934665603);
        unsigned char entry[INDEX_ENTRY];
        for (uint64_t i = 0; i < compact->block_count && status == CFR_STATUS_SUCCESS; ++i) {
            if (fread(entry, 1, sizeof(entry), compact->file) != sizeof(entry)) {
                status = CFR_STATUS_FORMAT_ERROR;
                break;
            }
            index_hash ^= hash_bytes(entry, sizeof(entry));
            index_hash *= UINT64_C(1099511628211);
            compact->index[i] = (IndexEntry){
                .first_key = (int64_t)get_u64(entry),
                .offset = get_u64(entry + 8),
                .compressed = get_u32(entry + 16),
                .raw = get_u32(entry + 20),
            };
            if (i && compact->index[i].first_key <= compact->index[i - 1].first_key)
                status = CFR_STATUS_FORMAT_ERROR;
            if (compact->index[i].raw > compact->largest_raw)
                compact->largest_raw = compact->index[i].raw;
        }
        unsigned char tail[8];
        if (status == CFR_STATUS_SUCCESS &&
            (fread(tail, 1, sizeof(tail), compact->file) != sizeof(tail) ||
             get_u64(tail) != index_hash))
            status = CFR_STATUS_FORMAT_ERROR;
    }
    if (status != CFR_STATUS_SUCCESS) {
        mus_score_compact_close(compact);
        return status;
    }
    *out = compact;
    return CFR_STATUS_SUCCESS;
}

void mus_score_compact_close(MusScoreCompact *compact) {
    if (compact == NULL)
        return;
    if (compact->file != NULL)
        (void)fclose(compact->file);
    ZSTD_freeDCtx(compact->zstd);
    for (size_t i = 0; i < CACHED_BLOCKS; ++i)
        free(compact->cache[i].data);
    free(compact->index);
    free(compact);
}

static Status load_block(MusScoreCompact *compact, uint64_t block, CachedBlock **out) {
    for (size_t i = 0; i < CACHED_BLOCKS; ++i)
        if (compact->cache[i].valid && compact->cache[i].block == block) {
            *out = &compact->cache[i];
            return CFR_STATUS_SUCCESS;
        }
    const IndexEntry *entry = &compact->index[block];
    unsigned char *compressed = malloc(entry->compressed);
    if (compressed == NULL)
        return CFR_STATUS_OUT_OF_MEMORY;
    Status status = CFR_STATUS_SUCCESS;
    if (fseek(compact->file, (long)entry->offset, SEEK_SET) != 0 ||
        fread(compressed, 1, entry->compressed, compact->file) != entry->compressed)
        status = CFR_STATUS_IO_ERROR;
    CachedBlock *slot = &compact->cache[compact->next_cache];
    if (status == CFR_STATUS_SUCCESS && slot->data == NULL) {
        slot->data = malloc(compact->largest_raw);
        if (slot->data == NULL)
            status = CFR_STATUS_OUT_OF_MEMORY;
    }
    if (status == CFR_STATUS_SUCCESS) {
        const size_t written = ZSTD_decompressDCtx(compact->zstd, slot->data,
                                                   compact->largest_raw, compressed,
                                                   entry->compressed);
        if (ZSTD_isError(written) || written != entry->raw)
            status = CFR_STATUS_FORMAT_ERROR;
        else {
            slot->valid = true;
            slot->block = block;
            slot->length = written;
            compact->next_cache = (compact->next_cache + 1) % CACHED_BLOCKS;
            *out = slot;
        }
    }
    free(compressed);
    return status;
}

Status mus_score_compact_find(MusScoreCompact *compact, InfoSetKey key,
                              const Action *actions, size_t count,
                              Probability *probabilities) {
    (void)actions;
    if (compact == NULL || !count || count > MAX_ACTIONS || probabilities == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    if (!compact->block_count || key < compact->index[0].first_key)
        return CFR_STATUS_NOT_FOUND;
    uint64_t low = 0, high = compact->block_count - 1;
    while (low < high) {
        const uint64_t middle = low + (high - low + 1) / 2;
        if (compact->index[middle].first_key <= key)
            low = middle;
        else
            high = middle - 1;
    }
    CachedBlock *block = NULL;
    const Status status = load_block(compact, low, &block);
    if (status != CFR_STATUS_SUCCESS)
        return status;
    size_t position = 0;
    int64_t current = 0;
    bool first = true;
    while (position < block->length) {
        uint64_t delta;
        if (!get_leb128(block->data, block->length, &position, &delta))
            return CFR_STATUS_FORMAT_ERROR;
        current = first ? compact->index[low].first_key : current + (int64_t)delta;
        first = false;
        if (position >= block->length)
            return CFR_STATUS_FORMAT_ERROR;
        const size_t stored = block->data[position++];
        if (!stored || stored > MAX_ACTIONS ||
            position + 2 * stored > block->length)
            return CFR_STATUS_FORMAT_ERROR;
        if (current == key) {
            if (stored != count)
                return CFR_STATUS_FORMAT_ERROR;
            uint32_t total = 0;
            for (size_t i = 0; i < stored; ++i) {
                const uint32_t value = (uint32_t)block->data[position + 2 * i] |
                                       ((uint32_t)block->data[position + 2 * i + 1] << 8);
                probabilities[i] = (double)value / (double)QUANTIZED_TOTAL;
                total += value;
            }
            return total == QUANTIZED_TOTAL ? CFR_STATUS_SUCCESS : CFR_STATUS_FORMAT_ERROR;
        }
        if (current > key)
            return CFR_STATUS_NOT_FOUND;
        position += 2 * stored;
    }
    return CFR_STATUS_NOT_FOUND;
}

Status mus_score_compact_verify(const char *checkpoint_path, const char *compact_path,
                                uint64_t *checked_out, uint64_t *mismatched_out,
                                double *largest_difference_out) {
    if (checkpoint_path == NULL || compact_path == NULL)
        return CFR_STATUS_INVALID_ARGUMENT;
    CheckpointReader *reader = calloc(1, sizeof(*reader));
    if (reader == NULL)
        return CFR_STATUS_OUT_OF_MEMORY;
    char schema[SCHEMA_CAPACITY] = {0};
    size_t iterations = 0;
    uint64_t source_nodes = 0, checked = 0, mismatched = 0;
    double largest = 0;
    Status status = CFR_STATUS_SUCCESS;
    reader->file = fopen(checkpoint_path, "rb");
    reader->zstd = ZSTD_createDStream();
    if (reader->file == NULL || reader->zstd == NULL ||
        ZSTD_isError(ZSTD_initDStream(reader->zstd)))
        status = CFR_STATUS_IO_ERROR;
    reader->input.src = reader->compressed;
    reader->crc = UINT32_MAX;
    if (status == CFR_STATUS_SUCCESS)
        status = checkpoint_header(reader, schema, &iterations, &source_nodes);
    MusScoreCompact *compact = NULL;
    if (status == CFR_STATUS_SUCCESS)
        status = mus_score_compact_open(compact_path, schema, iterations, &compact);
    double strategy[MAX_ACTIONS], stored[MAX_ACTIONS];
    Action actions[MAX_ACTIONS] = {0};
    for (uint64_t index = 0; index < source_nodes && status == CFR_STATUS_SUCCESS; ++index) {
        uint64_t encoded, count;
        status = checkpoint_u64(reader, &encoded);
        if (status == CFR_STATUS_SUCCESS)
            status = checkpoint_u64(reader, &count);
        if (status != CFR_STATUS_SUCCESS)
            break;
        if (!count || count > MAX_ACTIONS) {
            status = CFR_STATUS_FORMAT_ERROR;
            break;
        }
        double discarded, mass = 0;
        for (uint64_t i = 0; i < count && status == CFR_STATUS_SUCCESS; ++i)
            status = checkpoint_double(reader, &discarded);
        for (uint64_t i = 0; i < count && status == CFR_STATUS_SUCCESS; ++i) {
            status = checkpoint_double(reader, &strategy[i]);
            if (status == CFR_STATUS_SUCCESS)
                mass += strategy[i];
        }
        if (status != CFR_STATUS_SUCCESS)
            break;
        const Status found = mus_score_compact_find(compact, (InfoSetKey)encoded, actions,
                                                    (size_t)count, stored);
        if (mass <= 0) {
            if (found != CFR_STATUS_NOT_FOUND)
                mismatched += 1;
            continue;
        }
        checked += 1;
        if (found != CFR_STATUS_SUCCESS) {
            mismatched += 1;
            continue;
        }
        bool differs = false;
        for (uint64_t i = 0; i < count; ++i) {
            const double difference = fabs(stored[i] - strategy[i] / mass);
            if (difference > largest)
                largest = difference;
            if (difference > 2.0 / QUANTIZED_TOTAL)
                differs = true;
        }
        if (differs)
            mismatched += 1;
    }
    if (status == CFR_STATUS_SUCCESS)
        status = checkpoint_finish(reader);
    mus_score_compact_close(compact);
    if (reader->file != NULL)
        (void)fclose(reader->file);
    ZSTD_freeDStream(reader->zstd);
    free(reader);
    if (status == CFR_STATUS_SUCCESS) {
        if (checked_out != NULL) *checked_out = checked;
        if (mismatched_out != NULL) *mismatched_out = mismatched;
        if (largest_difference_out != NULL) *largest_difference_out = largest;
    }
    return status;
}

uint64_t mus_score_compact_node_count(const MusScoreCompact *compact) {
    return compact == NULL ? 0 : compact->node_count;
}

const char *mus_score_compact_schema(const MusScoreCompact *compact) {
    return compact == NULL ? NULL : compact->schema;
}

size_t mus_score_compact_iterations(const MusScoreCompact *compact) {
    return compact == NULL ? 0 : compact->iterations;
}
