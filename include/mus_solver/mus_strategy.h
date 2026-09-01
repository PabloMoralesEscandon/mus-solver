#ifndef MUS_SOLVER_MUS_STRATEGY_H
#define MUS_SOLVER_MUS_STRATEGY_H

#include <stdio.h>

#include "cfr/trainer.h"

/* Writes the visited average strategy with decoded mus labels. */
Status mus_strategy_write_text(FILE *stream, const Trainer *trainer);

/* Explicit writer for the original single wager of two stones. */
Status mus_single_bet_strategy_write_text(FILE *stream,
                                          const Trainer *trainer);

/* Writer for the format with double raises capped at eight stones. */
Status mus_raises_to_eight_strategy_write_text(FILE *stream,
                                               const Trainer *trainer);

#endif
