// Where R SMB is installed. C++98: shared with the gcc2-compatible Network add-on.
#pragma once
#include <stddef.h>
#include <unistd.h>

namespace rsmb {
// Haiku's own lookup order: user non-packaged, user packages, system
// non-packaged, system packages (pkgman installs here).
inline const char* ProgramPath()
{
    static const char* const candidates[] = {
        "/boot/home/config/non-packaged/apps/RSMB/RSMB",
        "/boot/home/config/apps/RSMB/RSMB",
        "/boot/system/non-packaged/apps/RSMB/RSMB",
        "/boot/system/apps/RSMB/RSMB",
    };
    const size_t count = sizeof(candidates) / sizeof(candidates[0]);
    for (size_t i = 0; i < count; ++i) {
        if (access(candidates[i], X_OK) == 0)
            return candidates[i];
    }
    return candidates[count - 1];
}
} // namespace rsmb
