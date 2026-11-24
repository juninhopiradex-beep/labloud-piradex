#pragma once
#include "Core/Licensing.h"

namespace secrets
{
inline constexpr lx::lic::PwEntry kPasswords[] =
{
    { "master", lx::lic::Role::Master,
      "e415cd31171475f708ed3b39c94a5bc0",
      "a8bbbcc96a5235c43d2e0e501d726c48a29542f72e6f806fda8ecaf15f4fae91", 0, 0, 0 },

    { "beta-01", lx::lic::Role::Beta,
      "8301db52947ba0e558b0975e1c255a97",
      "3f5b82e7894ee3659e461c9ae69fdb9aaea3a4a05e28b892953972cdabebb9a4", 2026, 12, 31 },
    { "beta-02", lx::lic::Role::Beta,
      "48837a4867ed1d6cdfa32af97a386a35",
      "6ae6816466d230896759bfcc0ca438969373dd9f0e92fb27c84bfde3e2df2510", 2026, 12, 31 },
    { "beta-03", lx::lic::Role::Beta,
      "178bf352b0b1273f63f5a70809d61ff9",
      "3fb22b6d221d7b73d9dd0d4a380de034b597535b4774b09c786b6fb87e2b17a4", 2026, 12, 31 },
};
inline constexpr int kPasswordCount = (int) (sizeof (kPasswords) / sizeof (kPasswords[0]));
} // namespace secrets
