// iOS builds use the existing MAME interpreter without executable-memory allocation.
#include "../src/rec/i960_native.h"

namespace i960rec {
bool build_native(runtime &, uint32_t (*)(void *, uint32_t), void *, uint32_t,
                  const hints &, native_stats *stats)
{
    if (stats) *stats = {};
    return false;
}
void free_native(void *) {}
} // namespace i960rec
