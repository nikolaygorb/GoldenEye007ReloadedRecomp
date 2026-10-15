// game_cvars.h - Game-specific CVAR declarations
#pragma once

#include <string>

#include <rex/cvar.h>

REXCVAR_DECLARE(std::string, graphics_backend);
REXCVAR_DECLARE(bool, disable_lens_flares);
REXCVAR_DECLARE(bool, anisotropic_filtering_16x);
REXCVAR_DECLARE(int32_t, gpu_wait_mode);
REXCVAR_DECLARE(bool, dev_debug_runtime);
