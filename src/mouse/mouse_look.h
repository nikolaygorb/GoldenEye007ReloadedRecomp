// mouse_look.h - mouse motion for the native mouse look hooks (mouse_look_hook.cpp).
//
// The SDK's keyboard/mouse driver (mnk) turns the mouse into the right stick, which the game
// then runs through its dead zone, response curve and speed cap. While the look hooks run,
// MouseLook takes the mouse from mnk (rex::input::mnk::SetMouseLookActive(false)), locks the
// pointer itself and hands the raw motion to the hooks as an angle. When the hooks stop
// running (menus, cutscenes, vehicles) a watchdog gives the mouse back to mnk, so the
// emulated stick takes over there.
#pragma once

#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace rex::ui
{
  class ImGuiDrawer;
  class WindowedAppContext;
} // namespace rex::ui

namespace ge::mouse
{

  // Radians, in the look hooks' convention.
  struct Turn
  {
    double yaw;   // + = turn right
    double pitch; // + = look down (player+2140 grows downward)
  };

  class MouseLook final : public rex::ui::WindowInputListener, public rex::ui::WindowListener
  {
  public:
    static MouseLook &Get();

    MouseLook(const MouseLook &) = delete;
    MouseLook &operator=(const MouseLook &) = delete;

    // Call once, after the main window has opened. imgui is optional: when
    // given, the mouse is handed back while an overlay wants it.
    void Attach(rex::ui::Window *window, rex::ui::ImGuiDrawer *imgui = nullptr);

    // ge_mouse_look is on. Cheap; lets a hook skip its own work.
    bool Enabled() const;

    // Called by the look hook once per look update (guest thread). Drives the
    // hand-over from mnk and returns the mouse turn since the last call, or
    // nullopt while the pointer is not ours yet. extra_scale multiplies the
    // turn (zoom).
    std::optional<Turn> Consume(double extra_scale);

    bool HoldForSnap(double min_counts);

    // rex::ui::WindowInputListener
    void OnMouseMove(rex::ui::MouseEvent &e) override;

    // rex::ui::WindowListener
    void OnLostFocus(rex::ui::UISetupEvent &) override;
    void OnGotFocus(rex::ui::UISetupEvent &) override;
    void OnClosing(rex::ui::UIEvent &) override;

  private:
    MouseLook() = default;
    ~MouseLook();

    enum class Phase
    {
      kIdle,      // mnk owns the mouse
      kHandoff,   // mnk told to let go, waiting for it to do so
      kEngaging,  // capture request posted to the UI thread
      kEngaged,   // pointer locked, deltas flowing
      kReleasing, // release request posted to the UI thread
    };

    using Clock = std::chrono::steady_clock;
    using UiTask = std::function<void()>;

    UiTask BeginReleaseLocked();
    UiTask BeginEngageLocked();
    Turn TakeTurnLocked(double extra_scale);
    bool OverlayWantsMouse() const;

    // Call without mu_ held.
    void Post(UiTask task);

    void WatchdogLoop();

    rex::ui::Window *window_ = nullptr;
    rex::ui::WindowedAppContext *ui_ = nullptr;
    rex::ui::ImGuiDrawer *imgui_ = nullptr;

    mutable std::mutex mu_;
    Phase phase_ = Phase::kIdle;
    bool has_focus_ = true;
    // Set by the UI-thread engage task
    bool captured_ = false;
    rex::ui::Window::CursorVisibility saved_cursor_ = rex::ui::Window::CursorVisibility::kVisible;
    Clock::time_point handoff_since_{};
    Clock::time_point last_consume_{};

    double pending_dx_ = 0.0;
    double pending_dy_ = 0.0;

    std::thread watchdog_;
    std::condition_variable watchdog_cv_;
    bool watchdog_stop_ = false;
  };

} // namespace ge::mouse
