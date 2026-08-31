#ifndef MUS_SOLVER_MUS_STRATEGY_H
#define MUS_SOLVER_MUS_STRATEGY_H

#include <stdio.h>

#include "cfr/trainer.h"

/* Writes the visited average strategy with decoded mus labels. */
Status mus_strategy_write_text(FILE *stream, const Trainer *trainer);

#endif
