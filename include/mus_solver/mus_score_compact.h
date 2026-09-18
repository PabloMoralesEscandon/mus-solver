#ifndef MUS_SOLVER_MUS_SCORE_COMPACT_H
#define MUS_SOLVER_MUS_SCORE_COMPACT_H

#include <stdint.h>

#include "mus_solver/mus_game.h"

/* Compact per-cell average policies for play.
 *
 * A checkpoint stores regrets and strategy accumulators as binary64 and is far
 * too large to consult while playing; the ordered text export is larger still.
 * This format keeps only the average policy of each node with positive mass,
 * quantized, in key order, inside small Zstandard blocks with an index. A
 * query decompresses one block, so memory does not grow with the file.
 *
 * Nodes whose strategy mass is zero are omitted: absence of evidence, like the
 * binary store, rather than the uniform policy that the text export writes.
 */

#define MUS_SCORE_COMPACT_VERSION 1u
#define MUS_SCORE_COMPACT_BLOCK_NODES 512u

typedef struct MusScoreCompact MusScoreCompact;

/* Convert one ordago-score checkpoint into a compact policy file.
 * The source is read as a stream and is never modified. The destination must
 * not exist. The schema and iteration count are copied from the checkpoint. */
Status mus_score_compact_write(const char *checkpoint_path, const char *output_path,
                               uint64_t *nodes_out, uint64_t *written_out);

/* Open a compact file, validating its header, schema and iteration count.
 * Zero for expected_iterations skips that check. */
Status mus_score_compact_open(const char *path, const char *expected_schema,
                              size_t expected_iterations, MusScoreCompact **out);

void mus_score_compact_close(MusScoreCompact *compact);

/* Look up one information set. Probabilities follow the caller's action order,
 * which must match the action count stored for that node. Returns NOT_FOUND
 * when the node is absent, exactly like a node with zero accumulated mass. */
Status mus_score_compact_find(MusScoreCompact *compact, InfoSetKey key,
                              const Action *actions, size_t count,
                              Probability *probabilities);

/* Re-read a checkpoint and compare every positive-mass node with the compact
 * file. Reports the largest absolute probability difference and the number of
 * mismatching nodes; quantization alone stays below 1/65535. */
Status mus_score_compact_verify(const char *checkpoint_path, const char *compact_path,
                                uint64_t *checked_out, uint64_t *mismatched_out,
                                double *largest_difference_out);

/* Diagnostics for tests and tools. */
uint64_t mus_score_compact_node_count(const MusScoreCompact *compact);
const char *mus_score_compact_schema(const MusScoreCompact *compact);
size_t mus_score_compact_iterations(const MusScoreCompact *compact);

#endif
