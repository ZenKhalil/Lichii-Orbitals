// SPDX-License-Identifier: 0BSD
#pragma once

#include <cstdlib>

namespace Dynarmic {
// An explicit startup diagnostic: other strings and an absent variable preserve host selection.
constexpr bool DisableWaitpkgOption(const char* value) noexcept {
    return value && value[0] == '1' && value[1] == '\0';
}

inline bool UseWaitpkgForExclusiveLock(bool host_supports_waitpkg) noexcept {
    // Sample once during JIT construction/emission. Changing the environment later is unsupported.
    static const bool disabled = DisableWaitpkgOption(std::getenv("EDEN_NX2_DISABLE_WAITPKG"));
    return host_supports_waitpkg && !disabled;
}
// Opt-in experiment: default remains one PAUSE. Only exclusive JIT calls
// request this; the shared emitter ignores it when WAITPKG is selected.
inline bool DoublePauseForExclusiveLock() noexcept {
    static const bool enabled = DisableWaitpkgOption(std::getenv("EDEN_NX2_EXCLUSIVE_PAUSE2"));
    return enabled;
}

} // namespace Dynarmic
