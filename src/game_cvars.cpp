#include "game_cvars.h"

REXCVAR_DEFINE_STRING(graphics_backend, "any", "GPU",
                      "Graphics API backend: any, d3d12, vulkan")
    .allowed({"any", "d3d12", "vulkan"});

REXCVAR_DEFINE_BOOL(disable_lens_flares, false, "Graphics",
                    "Disable lens flare effects");

REXCVAR_DEFINE_BOOL(anisotropic_filtering_16x, false, "Graphics",
                    "Force 16x anisotropic texture filtering");

REXCVAR_DEFINE_INT32(gpu_wait_mode, 1, "Performance",
                     "Pause between polls while the game waits for free command-buffer space: "
                     "0 = busy spin (original), 1 = yield the core, 2 = sleep 200us")
    .range(0, 2)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(dev_debug_runtime, false, "Debug",
                    "Enable runtime debug tools (stub sweep, missing function scan)");
