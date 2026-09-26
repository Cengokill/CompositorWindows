#include <stddef.h>
#include <stdint.h>
// Returns 1 on success, 0 when no source patch exists, -1 on allocation failure.
int content_fill(uint8_t *rgba, size_t stride, const uint8_t *mask, size_t maskStride, int width, int height);
// Windows cancellation adapter: original arithmetic/order retained; -2 means cancelled.
int content_fill_cancellable(uint8_t *pixels, size_t stride, const uint8_t *mask, size_t ms, int w, int h, int (*cancelled)(void *), void *context);
