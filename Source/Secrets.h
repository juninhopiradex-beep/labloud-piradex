#pragma once
#include "Core/Licensing.h"

namespace secrets
{
inline constexpr lx::lic::PwEntry kPasswords[] =
{
    { "master", lx::lic::Role::Master,
      "bbe3a1c1985603b62c38a26c1b65e2ad",
      "8f296ecfc1b9c09e755d37f34142373c6b661e86894e7b13e466c2ecc199635b", 0, 0, 0 },

    { "beta-01", lx::lic::Role::Beta,
      "9be4ca4f3cbd463cd113626e7efca91e",
      "e0356543a5aeb17c40b56fb4eab55d03272e7860d799694e73e7ff189e70e475", 2026, 12, 31 },
};
inline constexpr int kPasswordCount = (int) (sizeof (kPasswords) / sizeof (kPasswords[0]));
} // namespace secrets
