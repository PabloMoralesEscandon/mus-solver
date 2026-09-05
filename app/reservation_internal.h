#ifndef MUS_CLI_RESERVATION_INTERNAL_H
#define MUS_CLI_RESERVATION_INTERNAL_H

#include <stddef.h>

/*
 * Use the existing estimate for the first 1,000 iterations. New information
 * sets accumulate more slowly after that phase. Cap the extra reservation so
 * a very long run cannot request an unbounded table at startup. This estimate
 * is a performance hint, not a limit on the number of nodes in the store.
 */
static inline size_t mus_cli_ordago_reservation(size_t iterations) {
    const size_t initial = iterations < 1000 ? iterations : 1000;
    const size_t additional = iterations - initial;
    const size_t bounded_additional = additional < 19000 ? additional : 19000;

    return initial * 12600 + bounded_additional * 1200;
}

#endif
