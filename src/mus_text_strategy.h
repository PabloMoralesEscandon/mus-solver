#ifndef MUS_TEXT_STRATEGY_H
#define MUS_TEXT_STRATEGY_H

#include "mus_solver/mus_game.h"

typedef struct MusTextStrategy MusTextStrategy;

/* Opens an immutable, key-sorted export from mus_ordago_strategy_write_text. */
Status mus_text_strategy_open(const char *path, MusTextStrategy **out);
/* Require the full schema, including the score cell and continuation hash.
 * Validate the complete header and the presence of rows against zero/nonzero
 * information_sets. Nonzero iterations also requires the matching MCCFR count.
 * Opening does not count every row in a nonempty file. */
Status mus_text_strategy_open_schema(const char *path, const char *schema,
                                      size_t iterations, MusTextStrategy **out);
void mus_text_strategy_close(MusTextStrategy *strategy);
/* Reads only the requested row. The caller supplies the key's ordered actions. */
Status mus_text_strategy_find(MusTextStrategy *strategy, InfoSetKey key,
                              const MusOrdagoInformationSet *information,
                              const Action *actions, size_t count,
                              Probability *probabilities);

#endif
