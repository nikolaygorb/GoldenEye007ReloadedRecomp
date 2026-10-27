#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

#include <rex/ppc/func.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "mouse_look.h"

extern "C" REX_FUNC(__imp__sub_82217070);

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
  constexpr uint32_t kPlayerAimLock = 2618;
  constexpr uint32_t kPlayerAimLockCached = 2661;
  constexpr uint32_t kPlayerAimSnap = 2662;
  // Angular velocity (vec4, yaw = y), zeroed and set by the look update, integrated by
  // sub_82217070.
  constexpr uint32_t kPlayerYawRate = 1012;
  constexpr uint32_t kStackYawRate = 104;
  constexpr uint32_t kUnzoomedFov = 0x827F2854;

  // Look updates a mouse yaw may wait for the integrator before it is dropped (it would land
  // as one jump after a pause or a state without integration).
  constexpr int kMaxPendingYawUpdates = 8;

  // Mouse motion that ends an aim snap or lock, in counts (|dx| + |dy|): a little over the
  // jitter of pressing the aim button.
  constexpr double kSnapCancelCounts = 4.0;

  // Pitch taken by the yaw hook, applied by the pitch hook of the same look update.
  thread_local double g_pending_pitch = 0.0;
  thread_local bool g_pitch_pending = false;
  uint32_t g_log_counter = 0;

  // The yaw goes through the angular velocity, which the integrator (sub_82217070) turns
  // into rotation with its own dt - the one it then stores at player+428 for the next look
  // update. The look update only has the previous frame's dt, so turn / dt there comes out
  // scaled by the ratio of two frame times: jerks whenever the frame rate is uneven. The
  // look update still puts turn / dt into the velocity (the turn animation reads it), and
  // the integrator hook swaps that part for turn / its dt, so the rotation gets the turn
  // exactly.
  struct PendingYaw
  {
    uint32_t player = 0;
    double yaw = 0.0;   // radians not integrated yet
    float rate = 0.0f;  // what the look update put into the velocity for it
    int updates = 0;    // look updates since it was last integrated
  };
  std::mutex g_yaw_mutex;
  std::array<PendingYaw, 4> g_pending_yaw;

  PendingYaw *FindPendingYawLocked(uint32_t player, bool create)
  {
    PendingYaw *oldest = &g_pending_yaw[0];
    for (PendingYaw &p : g_pending_yaw)
    {
      if (p.player == player)
      {
        return &p;
      }
      if (!p.player || (oldest->player && p.updates > oldest->updates))
      {
        oldest = &p;
      }
    }
    if (!create)
    {
      return nullptr;
    }
    *oldest = PendingYaw{player};
    return oldest;
  }

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

// 0x821FDD00, lbz r11,2618(r31): the aim lock and snap checks, the look update returns
// right after while either is set. Cleared here as the game does on stick motion.
void GeMouseLookAimAssist(PPCRegister &r31)
{
  auto &look = ge::mouse::MouseLook::Get();
  if (!look.Enabled())
  {
    return;
  }
  {
    // The look update has zeroed the velocity and may return before the yaw hook.
    std::lock_guard lock(g_yaw_mutex);
    if (PendingYaw *p = FindPendingYawLocked(r31.u32, false))
    {
      p->rate = 0.0f;
    }
  }
  uint8_t *player = Base() + r31.u32;
  const bool lock = player[kPlayerAimLock] != 0;
  const bool snap = player[kPlayerAimSnap] != 0;
  if (!lock && !snap)
  {
    return;
  }
  if (look.HoldForSnap(kSnapCancelCounts))
  {
    player[kPlayerAimLock] = 0;
    player[kPlayerAimLockCached] = 0;
    player[kPlayerAimSnap] = 0;
    if (REXCVAR_GET(ge_mouse_look_log))
    {
      REXLOG_INFO("mouse look: player {:08X} aim {} ended by the mouse", r31.u32,
                  lock ? "lock" : "snap");
    }
  }
}

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
  {
    // The look update has just zeroed the velocity: no rate of ours is in it any more.
    std::lock_guard lock(g_yaw_mutex);
    if (PendingYaw *p = FindPendingYawLocked(player, false))
    {
      p->rate = 0.0f;
      if (p->yaw != 0.0 && ++p->updates > kMaxPendingYawUpdates)
      {
        p->yaw = 0.0;
      }
    }
  }

  double fov = 0.0;
  const double zoom = ZoomScale(player, fov);
  const auto turn = look.Consume(zoom);
  if (!turn)
  {
    return;
  }

  // Only for the turn animation; the integrator hook makes the rotation exact.
  const double dt = LoadFloat(player + kPlayerDeltaTime);
  const float rate = dt > 1e-4 && dt < 0.5 ? static_cast<float>(turn->yaw / dt) : 0.0f;
  const uint32_t rate_address = r1.u32 + kStackYawRate;
  const float rate_in = LoadFloat(rate_address);
  const float rate_out = rate_in + rate;
  StoreFloat(rate_address, rate_out);
  if (turn->yaw != 0.0)
  {
    std::lock_guard lock(g_yaw_mutex);
    PendingYaw *p = FindPendingYawLocked(player, true);
    p->yaw += turn->yaw;
    p->rate = rate;
    p->updates = 0;
  }
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

// sub_82217070 (r3 = player, f1 = dt): stores dt at player+428, then turns the player by the
// angular velocity x dt (through its physics body, or GetRotation / SetRotation, vtable
// +160 / +164). The mouse yaw's share of the velocity is swapped for yaw / this dt.
extern "C" REX_FUNC(sub_82217070)
{
  const uint32_t player = ctx.r3.u32;
  const double dt = ctx.f1.f64;
  float correction = 0.0f;
  if (dt > 1e-4 && dt < 0.5)
  {
    std::lock_guard lock(g_yaw_mutex);
    if (PendingYaw *p = FindPendingYawLocked(player, false); p && (p->yaw != 0.0 || p->rate != 0.0f))
    {
      correction = static_cast<float>(p->yaw / dt) - p->rate;
      p->yaw = 0.0;
      p->rate = 0.0f;
      p->updates = 0;
    }
  }
  if (correction != 0.0f)
  {
    const uint32_t address = player + kPlayerYawRate;
    StoreFloat(address, LoadFloat(address) + correction);
  }
  __imp__sub_82217070(ctx, base);
}
