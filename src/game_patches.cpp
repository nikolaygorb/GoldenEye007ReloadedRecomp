// Ported xenia-canary game patches for GE007R retail (415608AF):
// https://github.com/xenia-canary/game-patches/blob/main/patches/415608AF%20-%20GoldenEye%20007%20Reloaded.patch.toml
//
// The patches change instructions, which can't be written into guest memory: the code is
// already recompiled, so changed bytes are never executed. Each one is a [[midasm_hook]]
// in default_functions.toml instead: the hook runs before the original instruction and,
// when its cvar is on, applies the patched instruction and jumps past the original.
#include <cstdint>

#include <rex/ppc/context.h>

#include "game_cvars.h"

namespace
{
  // Max anisotropy level forced at both sampler setup sites (0x10 = 16x).
  constexpr int64_t kMaxAnisotropy = 16;
} // namespace

// 8248CB40: beq cr6,0x8248d8f8 -> b 0x8248d8f8 (skips the lens flare draw)
bool DisableLensFlaresHook()
{
  return REXCVAR_GET(disable_lens_flares);
}
