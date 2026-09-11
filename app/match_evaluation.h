#ifndef MUS_MATCH_EVALUATION_H
#define MUS_MATCH_EVALUATION_H

#include <stdio.h>
#include "mus_solver/mus_adapter.h"

#define MUS_MATCH_PHASE_COUNT (MUS_PHASE_REDRAW_PLAYER_1 + 1)
#define MUS_MATCH_METHOD_COUNT (MUS_ADAPTER_MUS_LIMIT + 2)
#define MUS_MATCH_BASELINE_METHOD (MUS_MATCH_METHOD_COUNT - 1)

typedef Status (*MusMatchDecide)(void *context, const MusAdapterObservation *observation,
                               MusAdapterDecision *decision);
typedef struct {
    MusMatchDecide decide;
    void *context;
    bool baseline;
} MusMatchPolicy;

typedef struct {
    size_t matches;
    unsigned int seed;
    /* Scores belong to policies A and B. The starting mano alternates. */
    int initial_scores[2];
    size_t max_hands_per_match;
    /* Optional public decision trace. The caller owns this stream. */
    FILE *trace;
} MusMatchOptions;

typedef struct {
    size_t matches;
    size_t wins[2];
    size_t hands;
    uint64_t decisions[2];
    uint64_t methods[2][MUS_MATCH_METHOD_COUNT];
    uint64_t phases[2][MUS_MATCH_PHASE_COUNT][MUS_MATCH_METHOD_COUNT];
    uint64_t scores[2][40][40][MUS_MATCH_METHOD_COUNT];
    uint64_t actions[2][MUS_BOT_DISCARD + 1][MUS_MATCH_METHOD_COUNT];
    /* Legacy generic, public generic, trained score cell, baseline. */
    uint64_t strategies[2][4];
    uint64_t checksum;
} MusMatchResult;

/* Runs the actual two-player simulator and passes each policy only its own
 * cards and public observations. Single-threaded: the simulator uses rand().
 * An error preserves result; the optional trace can contain a partial run. */
Status mus_match_evaluate(const MusMatchOptions *options, const MusMatchPolicy policies[2],
                          MusMatchResult *result);
const char *mus_match_phase_name(MusGamePhase phase);
const char *mus_match_method_name(size_t method);

#endif
