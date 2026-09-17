#ifndef MUS_SIMPLIFIED_INTERNAL_H
#define MUS_SIMPLIFIED_INTERNAL_H

#include "mus_solver/mus_simplified.h"

/* Base and schema must outlive the descriptor. Base must not be simplified. */
void mus_simplified_game_wrap(Game *game, const Game *base, const char *schema);

/* Check the simplified increment restrictions, not the base history format. */
bool mus_simplified_wager_masks_valid(const uint8_t *masks, size_t players);

#endif
