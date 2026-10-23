# Settings

Two hand-authored, checked-in config files that the `goldeneye007reloadedrecomp`
binary loads automatically at startup, plus a downloaded controller mapping
database and this README.

| File | Covers |
|---|---|
| [`hardware.toml`](hardware.toml) | Renderer/backend, window, vsync, resolution, debug output, experimental gameplay patches |
| [`mapping.toml`](mapping.toml) | Input backend selection (gamepad/keyboard+mouse) |
| [`gamecontrollerdb.txt`](gamecontrollerdb.txt) | SDL gamepad mappings for controllers it doesn't already recognize - not hand-authored, see below |

Split in two on purpose: rendering/perf tweaks and input tweaks are separate
concerns you'll want to touch independently, and both stay tiny because
they only list the cvars this project actually cares about, not the full
~80-flag surface the SDK exposes.

**Format: flat `key = value` only, no `[Table]` headers.** Confirmed via
`rex::cvar::SerializeToTOML()`, which itself dumps a flat list - the loader
reads root-level keys, it does not recurse into nested tables. A key put
under a `[Section]` header is silently ignored (found the hard way: values
under `[Display]`/`[GPU]` headers never took effect until the headers were
removed).

## How the binary consumes them

`ReXApp` (the SDK base class) already auto-loads a single `<exe_dir>/<name>.toml`
config file via `rex::cvar::LoadConfig()`. `Goldeneye007reloadedrecompApp::
OnConfigurePaths()` in [`src/goldeneye007reloadedrecomp_app.h`](../src/goldeneye007reloadedrecomp_app.h)
redirects that single slot to `settings/hardware.toml`, then loads both files
a **second time** in `OnPostSetup()`. Both paths are resolved by walking up
from the exe's folder until an `assets/` directory is found, so it works from
any build preset's output directory.

The second pass exists because not every cvar is registered when
`OnConfigurePaths` runs: window/fullscreen/resolution/monitor (`rex::ui`)
are available immediately, but GPU-backend cvars like `vsync`,
`resolution_scale`, `d3d12_debug` (`rex::graphics`) only register once the
runtime/GPU backend spins up, which happens later. `LoadConfig()` is a
one-shot read, not a live watch, so without the second pass those keys would
be silently dropped (confirmed via `rex::cvar::GetFlagSource()` returning
`kDefault` for them after the first pass, `kConfig` after the second).
Re-applying an already-set value on the second pass is a harmless no-op.

## Why `game_data_root` isn't a key in `hardware.toml`

Unlike every other cvar here, `game_data_root` is read by the SDK (`ReXApp::
SetupEnvironment()`) *before* any config file loads at all - so a
`game_data_root = "..."` line in `hardware.toml` would silently have no
effect, the same one-shot-too-early problem as the two-pass cvars above but
with no later pass to catch it. Instead, `OnConfigurePaths()` defaults
`paths.game_data_root` directly to `<repo_root>/assets` in code whenever
`--game_data_root`/`REX_GAME_DATA_ROOT` didn't already set it, so
`goldeneye007reloadedrecomp.exe` with no flags at all just works from this repo
layout. `--game_data_root <path>` still overrides it for a game dump kept
elsewhere.

Precedence (highest wins), same as any other cvar:

```
--flag=value (CLI)  >  REX_FLAG=value (env var)  >  settings/*.toml  >  compiled-in default
```

So a `--vsync=false` on the command line, or `REX_VSYNC=false` in the
environment, always overrides whatever is in `hardware.toml`.

## Caveat: the in-game Settings overlay writes to `hardware.toml`

The SDK's built-in Settings overlay's "Save to config" button writes *every*
modified cvar into the one path `OnConfigurePaths` redirected
(`hardware.toml`), including anything you changed at runtime through the
overlay. If you experiment there, expect `git diff` to show more than the
hand-picked keys below - `git checkout -- settings/hardware.toml` reverts it
to the committed defaults.

## Caveat: engine hotkeys aren't in `mapping.toml`

`mapping.toml` only covers static-flag input cvars (table below). The
console/debug/achievements overlay toggle keys ("Keybinds" category) are
registered by the SDK *after* these files load (once the window and
overlays exist), so a key added to `mapping.toml` for one of those would
silently have no effect. Rebind those from the in-game Settings overlay
instead - that persists through the same save mechanism described above.

## Keyboard, mouse, and gamepad all work at once

The SDK always merges every input device assigned to a guest player rather
than picking one active source (`rex::input::MergeInto` - buttons OR'd,
sticks/triggers take whichever device pushed further), and the keyboard as a
gamepad ("MnK") driver marks itself a synthetic device that's always folded
into player one alongside any physical pad, never competing with it for a
slot. So there's no "backend" to choose between keyboard and gamepad the way
`input_backend` chooses between `sdl`/`xinput` - it's `mnk_mode`/`mnk_mouse`
turning the keyboard/mouse side on or off, independent of whatever pad is
also plugged in.

Per-action keybinds (`keybind_a`, `keybind_lstick_up`, ...) already ship with
sensible WASD-style defaults and aren't repeated in `mapping.toml` -
add any of them the same way as everything else here if you want to rebind
one. The full list isn't in the SDK headers (source-only, defined in the
SDK's `mnk_input_driver.cpp`) - the easiest way
to see every current default is `rex::cvar::SerializeToTOML()`'s dump, or the
in-game Settings overlay's control rebinding screen.

## `hardware.toml` reference

Main keys. "Value" is what the checked-in file sets; "Built-in default" is the compiled value used when the key is absent. Every ReXGlue (SDK) rendering option, with its values, is described in [`faq.md`](faq.md).


| Key | Type | Default | Effect |
|---|---|---|---|
| `gpu_plugin` | string | `"xenos"` | GPU emulation plugin to load. Set here rather than on the command line now - see root [README](../README.md#run). A `--gpu_plugin` flag would still override this file if you ever needed a different plugin. |
| `graphics_backend` | string | `"any"` | Graphics API backend: `"any"` (D3D12 first when both are compiled in), `"d3d12"`, or `"vulkan"`. Project-defined cvar (not from the SDK), wired up in `OnPreSetup()` in [`src/goldeneye007reloadedrecomp_app.h`](../src/goldeneye007reloadedrecomp_app.h) - it loads the plugin itself with the requested backend before the SDK's own auto-load (which only ever requests `"any"`) runs. Falls back to automatic selection with a warning if the requested backend isn't compiled into `rexgpu-xenos`. |
| `vsync` | bool | `true` | Vertical sync. |
| `resolution_scale` | int | `2` | Internal render-target supersampling, independent of window/guest resolution. |
| `async_shader_compilation` | bool | `true` | Compile shaders on a background thread instead of blocking the render thread - reduces hitches when new shaders are first seen. |
| `native_2x_msaa` | bool | `false` | Native 2x MSAA on the emulated render targets. |
| `anisotropic_override` | int | `1` | Forces anisotropic texture filtering, range `-1`-`5`: `-1` no override, `0` off, `1` 1x, `2` 2x, `3` 4x (SDK default), `4` 8x, `5` 16x. |
| `window_width` / `window_height` | int | `1920` / `1080` | Host window size in logical pixels (`0`-`8192`); overrides the size from `resolution`. |
| `fullscreen` | bool | `true` | Host window fullscreen. |
| `monitor` | int | `0` | Host monitor index for fullscreen. |
| `resolution` | string | `"1920x1080"` | One setting for both the video mode reported to the game (affects the game's own UI scale/aspect logic) and the window size: `""` (the title's own), `720p`, `1080p`, `1440p`, `4k` or `WIDTHxHEIGHT`. `window_width` / `window_height` override the window size from it. What the game renders at is `resolution_scale`. |
| `present_letterbox` | bool | `true` | Letterbox instead of stretch when window and guest aspect ratios differ. |
| `d3d12_debug` | bool | `false` | D3D12 debug layer. Leave off - noticeably slower. |
| `user_language` | int | `1` | Game language (Xbox locale ID, `1`-`12`, see the comment in [`hardware.toml`](hardware.toml)). SDK cvar, no hook needed: the game's own `XGetLanguage` (`sub_8265D7E8`) reads it through `ExGetXConfigSetting(XCONFIG_USER_LANGUAGE)`, and `sub_82392888` maps it to the game's language index. Values outside `1`-`12` fall back to English. Restart after changing it. |
| `gpu_wait_mode` | int | `1` | Render thread wait for GPU command-buffer space: `0` busy spin (original), `1` yield, `2` sleep 200us. Hook in [`src/render/gpu_wait_hook.cpp`](../src/render/gpu_wait_hook.cpp). |
| `dev_debug_runtime` | bool | `false` | Stub sweep and missing-function scan at startup (`logs/stub_sweep.txt`, `logs/missed_functions.txt`). Stubs silently swallow calls to unknown code, so turn it off when chasing crashes. |
| `max_local_users` | int | `2` | Local players that can be signed in, `1`-`4` (SDK default `1`). Players past the first sign in while a controller is assigned to them. See [`split-screen.md`](split-screen.md). |
| `keyboard_own_player` | bool | `false` | Automatic device mode: `true` makes keyboard and mouse player 1 on their own, gamepads start at player 2; `false` (SDK default) keeps keyboard and mouse together with pad 1 as player 1, pad 2 is player 2. Ignored once `input_player1` or `input_player2` is set. |
| `input_player1` / `input_player2` | string | `"auto"` (commented out) | Devices of players 1 and 2: `keyboard`, `pad1`-`pad4`, `pads`, `none`, `auto`, comma-separated. Setting either one switches to chosen devices (players 3-4 then get none). See [`split-screen.md`](split-screen.md). |
| `user_gamertag_2` | string | `""` (commented out) | Name shown for player 2 (at most 15 bytes); empty = the first player's name with `(2)`. |
| `disable_lens_flares` | bool | `false` | xenia-canary patch: skips the lens flare draw. |
| `anisotropic_filtering_16x` | bool | `false` | xenia-canary patch: forces 16x anisotropy at both sampler setup sites. |

## `mapping.toml` reference

| Key | Type | Default | Effect |
|---|---|---|---|
| `input_backend` | string | `"sdl"` | `""` is rejected by validation (logs a warning, falls back to default) - must be an explicit `"sdl"`, `"xinput"`, or `"nop"` (input disabled). |
| `hid_mappings_file` | string | `"gamecontrollerdb.txt"` | Extra SDL gamepad mappings loaded for controllers SDL doesn't already recognize, or `""` to skip. `gamecontrollerdb.txt` next to the exe is staged by `CMakeLists.txt` from [`gamecontrollerdb.txt`](gamecontrollerdb.txt) in this folder, sourced from [mdqinc/SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) - re-copy that file from upstream any time to refresh it. |
| `guide_button` | bool | `true` | Whether the controller Guide/PS button is routed to the guest. |
| `mnk_mode` | bool | `true` | Enables keyboard-as-controller input, merged in alongside any physical gamepad (see below). |
| `mnk_mouse` | bool | `true` | Routes mouse movement to the right stick when `mnk_mode` is on. Off means the right stick only comes from the `keybind_rstick_*` keys. |
| `mnk_sensitivity` | double | `1.0` | Mouse sensitivity for the right stick, range `0.01`-`10.0`. With `ge_mouse_look` on it only applies where native mouse look doesn't run (menus, vehicles). |
| `ge_mouse_look` | bool | `true` | Native mouse look: on foot and on mounted guns the mouse turns the camera directly, skipping the game's stick dead zone, response curve and aim friction. Hooks in [`src/mouse/mouse_look_hook.cpp`](../src/mouse/mouse_look_hook.cpp). Needs `mnk_mode` and `mnk_mouse`. |
| `ge_mouse_sensitivity` | double | `2.0` | Native mouse look: camera turn per mouse count = 0.022 deg x this. Range `0.01`-`100.0`. Independent of the in-game sensitivity option. |
| `ge_mouse_invert_x` / `ge_mouse_invert_y` | bool | `false` | Invert native mouse look axes. The in-game Invert option doesn't apply to it. |
| `ge_mouse_zoom_scaling` | bool | `true` | Scale native mouse look by `tan(fov/2) / tan(64deg/2)` while zoomed, so the picture moves under the mouse at the same rate. |
| `ge_mouse_zoom_sensitivity` | double | `1.0` | Extra factor on native mouse look while zoomed in. |
| `ge_mouse_look_log` | bool | `false` | Log the native mouse look hooks (dt, FOV, yaw rate before/after, pitch). |

Gamepad button layout itself (DualShock 4, DualSense, Xbox, Switch Pro, ...)
isn't a setting here - SDL auto-detects the controller and maps it to the
Xbox 360 layout the game expects.

## Adding more cvars

Any cvar declared with `REXCVAR_DECLARE` under
`thirdparty/rexglue-sdk/include/rex/**` can be added the same way: pick
the file matching its concern (hardware vs. input), add `key = value`. If it
needs to apply before the window/overlays exist (true for everything
currently listed), it belongs in one of these two files loaded from
`OnConfigurePaths`; if it's a Keybinds-category hotkey, see the caveat above
instead.

`graphics_backend` isn't an SDK cvar - it's defined with
`REXCVAR_DEFINE_STRING` in [`src/game_cvars.cpp`](../src/game_cvars.cpp),
registering at static-init time just like the SDK's own, so it loads fine on
the first pass. This is the pattern to follow for any other
project-specific toggle (e.g. a future ported gameplay patch): define it in
`src/game_cvars.cpp`, declare it in `src/game_cvars.h`, read it with `REXCVAR_GET(name)` wherever it's needed, and document it in
this table.
