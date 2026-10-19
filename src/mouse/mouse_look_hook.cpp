// mouse_look_hook.cpp - mouse look straight into the player's look update.
//
// sub_821FDB18 is the look update of a player on foot or on a mounted gun (r31 = player).
// Only a player with a local pad gets past its start (sub_822218C8 returns its input block,
// player+328), and it returns early while the player has no look control (player+1364,
// +2618, +2662), so menus, cutscenes and dead players never reach the hooks. In it:
//   - sub_821EFF10: the full-stick turn speed, SpeedX/SpeedY[look sensitivity]
//     (0x827F04CC / 0x827F04F4, deg/s), scaled down while aiming by the FOV.
//   - sub_821F0388: the stick's response curve, Range/Scalar[look sensitivity]
//     (0x827F0234 / 0x827F025C) - the acceleration a mouse must not get.
//   - sub_82223208: aim friction, slows the rates near a target.
// The result is a yaw/pitch rate pair in rad/s at r1+104/+108. The yaw rate goes into the
// player's angular velocity (player+1008, .y), which sub_82217070 integrates with
// dt = player+428. The pitch (player+2140, radians, growing downward) is integrated in the
// function itself and clamped to the limits at r1+80/+88 from 0x821FDEE4 on.
//
// Mouse counts are an angle, not a rate: the yaw goes into the rate as angle / dt right
// after the friction, the pitch straight into player+2140 before the clamp. The curve, the
// friction and the in-game sensitivity are skipped; the pitch limits and aim magnetism
// (sub_82223358, added to the yaw rate later) still apply.
//
// player+2576 is the current FOV (radians; sub_821F59B0 blends it towards the weapon's
// zoom), 0x827F2854 the unzoomed one (64 deg). The turn is scaled by
// tan(fov / 2) / tan(unzoomed / 2), so the picture moves under the mouse at the same rate
// at the screen center whatever the zoom.
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "mouse_look.h"

REXCVAR_DEFINE_BOOL(ge_mouse_zoom_scaling, true, "Mouse",
                    "Scale mouse look with the camera's field of view, so zoomed aiming turns "
                    "the picture by the same amount per mouse count")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(ge_mouse_zoom_sensitivity, 1.0, "Mouse",
                      "Extra mouse look factor while zoomed in; the turn is already scaled to "
                      "the FOV")
    .range(0.01, 10.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(ge_mouse_look_log, false, "Mouse",
                    "Log the mouse look hooks (first calls, then every 240th) - needs log_level "
                    "info or lower")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  constexpr uint32_t kPlayerDeltaTime = 428;
  constexpr uint32_t kPlayerPitch = 2140;
  constexpr uint32_t kPlayerFov = 2576;
  constexpr uint32_t kStackYawRate = 104;
  constexpr uint32_t kUnzoomedFov = 0x827F2854;

  // Pitch taken by the yaw hook, applied by the pitch hook of the same look update.
  thread_local double g_pending_pitch = 0.0;
  thread_local bool g_pitch_pending = false;
  uint32_t g_log_counter = 0;

  uint8_t *Base()
  {
    return rex::Runtime::instance()->memory()->virtual_membase();
  }

  float LoadFloat(uint32_t address)
  {
    uint32_t v;
    std::memcpy(&v, Base() + address, 4);
    return std::bit_cast<float>(std::byteswap(v));
  }

  void StoreFloat(uint32_t address, float f)
  {
    const uint32_t v = std::byteswap(std::bit_cast<uint32_t>(f));
    std::memcpy(Base() + address, &v, 4);
  }

  bool PlausibleFov(double fov)
  {
    return std::isfinite(fov) && fov > 0.02 && fov < 3.0;
  }

  double ZoomScale(uint32_t player, double &fov_out)
  {
    fov_out = LoadFloat(player + kPlayerFov);
    if (!REXCVAR_GET(ge_mouse_zoom_scaling))
    {
      return 1.0;
    }
    const double unzoomed = LoadFloat(kUnzoomedFov);
    if (!PlausibleFov(fov_out) || !PlausibleFov(unzoomed))
    {
      return 1.0;
    }
    const double scale = std::tan(fov_out * 0.5) / std::tan(unzoomed * 0.5);
    if (scale >= 0.999)
    {
      return 1.0;
    }
    return std::clamp(scale * REXCVAR_GET(ge_mouse_zoom_sensitivity), 0.02, 1.0);
  }
} // namespace

// 0x821FDD64, right after sub_82223208 (aim friction): r1+104 = yaw rate (rad/s), about to
// be stored into the angular velocity.
void GeMouseLookYaw(PPCRegister &r31, PPCRegister &r1)
{
  g_pitch_pending = false;
  auto &look = ge::mouse::MouseLook::Get();
  if (!look.Enabled())
  {
    return;
  }
  const uint32_t player = r31.u32;
  const double dt = LoadFloat(player + kPlayerDeltaTime);
  if (!(dt > 1e-4 && dt < 0.5))
  {
    return; // the motion stays pending for the next update
  }

  double fov = 0.0;
  const double zoom = ZoomScale(player, fov);
  const auto turn = look.Consume(zoom);
  if (!turn)
  {
    return;
  }

  const uint32_t rate_address = r1.u32 + kStackYawRate;
  const float rate_in = LoadFloat(rate_address);
  const float rate_out = rate_in + static_cast<float>(turn->yaw / dt);
  StoreFloat(rate_address, rate_out);
  g_pending_pitch = turn->pitch;
  g_pitch_pending = true;

  if (REXCVAR_GET(ge_mouse_look_log) && (g_log_counter++ < 30 || g_log_counter % 240 == 0))
  {
    REXLOG_INFO("mouse look: player {:08X} dt {:.4f} fov {:.1f} zoom {:.2f} turn y={:.4f} "
                "p={:.4f} yaw rate {:.3f} -> {:.3f} pitch {:.3f}",
                player, dt, fov * 57.29578, zoom, turn->yaw, turn->pitch, rate_in, rate_out,
                LoadFloat(player + kPlayerPitch));
  }
}

// 0x821FDEE4, the pitch clamp every path of the look update ends in: player+2140 is loaded
// and clamped to the limits right after.
void GeMouseLookPitch(PPCRegister &r31)
{
  if (!g_pitch_pending)
  {
    return;
  }
  g_pitch_pending = false;
  const uint32_t address = r31.u32 + kPlayerPitch;
  StoreFloat(address, LoadFloat(address) + static_cast<float>(g_pending_pitch));
}
