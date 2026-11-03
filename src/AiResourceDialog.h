#pragma once

#include "AiResources.h"
#include <Windows.h>
#include <optional>

namespace ai {
std::optional<UserLimits> ShowResourceDialog(HWND owner, bool english, const UserLimits& initial,
    const std::filesystem::path& workRoot, ResourceProbe probe = {});
}
