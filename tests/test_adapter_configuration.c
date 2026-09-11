#include <stdio.h>
#include <stdlib.h>

#include "mus_solver/mus_adapter.h"

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__,          \
                    #condition);                                                \
            exit(EXIT_FAILURE);                                                 \
        }                                                                      \
    } while (0)

int main(void) {
    InfoStore store = {0};
    MusAdapter *adapter = NULL;
    CHECK(cfr_info_store_init(&store) == CFR_STATUS_SUCCESS);
#if MUS_GAME_MAX_MUSES != 1
    MusAdapter *const sentinel = (MusAdapter *)(void *)&store;
    adapter = sentinel;
#endif
    const Status status = mus_adapter_create(&store, NULL, 7, &adapter);
#if MUS_GAME_MAX_MUSES == 1
    CHECK(status == CFR_STATUS_SUCCESS && adapter != NULL);
    mus_adapter_destroy(adapter);
#else
    CHECK(status == CFR_STATUS_INCOMPATIBLE_GAME && adapter == sentinel);
    /* Configuration rejection precedes file access and preserves the output. */
    const char *path = "/mus-adapter-unsupported-configuration.cfr";
    for (int compressed = 0; compressed < 2; ++compressed) {
        CHECK(mus_adapter_load(path, compressed != 0, NULL, 7, &adapter) ==
              CFR_STATUS_INCOMPATIBLE_GAME);
        CHECK(adapter == sentinel);
    }
    CHECK(mus_adapter_open_text(path, NULL, 7, &adapter) ==
          CFR_STATUS_INCOMPATIBLE_GAME);
    CHECK(adapter == sentinel);
#endif
    CHECK(cfr_info_store_destroy(&store) == CFR_STATUS_SUCCESS);
    puts("All adapter configuration checks passed.");
    return EXIT_SUCCESS;
}
