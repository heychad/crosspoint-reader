#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "QuickLockTrigger.h"
#include "ReaderShortcuts.h"

// Main-loop-owned physical contacts. Configured holds cannot also fire their release.
class HoldShortcutController {
 public:
  using Action = readershortcuts::Action;
  struct Contact {
    bool down = false, pressed = false, released = false;
    uint32_t durationMs = 0;
  };
  struct Binding {
    Action tap = Action::Default, hold = Action::Default;
    uint32_t holdMs = 400;
  };
  struct Frame {
    uint32_t now = 0, visit = 0;
    std::array<Contact, 3> contacts{};
    bool blocked = false;
  };
  struct Config {
    std::array<Binding, 3> bindings{};
  };
  struct Result {
    Action action = Action::Default;
    QuickLockTrigger trigger = QuickLockTrigger::None;
    bool consumed = false;
  };

  Result update(const Frame& frame, const Config& config) {
    Result result;
    const bool changed = initialized && visit != frame.visit;
    initialized = true;
    visit = frame.visit;
    for (size_t i = 0; i < states.size(); ++i) {
      const auto one = updateOne(states[i], frame.contacts[i], config.bindings[i], frame.now,
                                 frame.blocked || changed || result.action != Action::Default, i);
      if (result.action == Action::Default && one.action != Action::Default)
        result = one;
      else
        result.consumed = result.consumed || one.consumed;
    }
    return result;
  }

 private:
  struct State {
    Binding binding{};
    uint32_t started = 0;
    bool active = false, blocked = false, fired = false;
  };
  static Result updateOne(State& state, const Contact& contact, const Binding& binding, uint32_t now, bool block,
                          size_t index) {
    Result result;
    const bool configured = binding.tap != Action::Default || binding.hold != Action::Default;
    if (!configured && !state.active && !state.blocked) return result;
    if (block && (contact.down || contact.released || state.active)) {
      state.active = false;
      state.blocked = true;
    }
    if (state.blocked) {
      result.consumed = contact.down || contact.released;
      if (!contact.down) state = {};
      return result;
    }
    if (!state.active && configured && contact.down) {
      if (!contact.pressed) {
        state.blocked = true;
        result.consumed = true;
        return result;
      }
      state.active = true;
      state.started = now;
      state.binding = binding;
    }
    if (!state.active) return result;
    const auto held = std::max(now - state.started, contact.durationMs);
    const bool hasHold = state.binding.hold != Action::Default;
    if (!state.fired && hasHold && held >= state.binding.holdMs && (contact.down || contact.released)) {
      state.fired = true;
      result.action = state.binding.hold;
      result.trigger = index == 0   ? QuickLockTrigger::LongPower
                       : index == 1 ? QuickLockTrigger::LongBack
                                    : QuickLockTrigger::LongMenu;
      result.consumed = true;
    } else if (contact.released && !state.fired && state.binding.tap != Action::Default) {
      result.action = state.binding.tap;
      result.trigger = QuickLockTrigger::ShortPower;
      result.consumed = true;
    }
    // Configured holds own their press until release, before legacy readers see it.
    if (state.fired || contact.down) result.consumed = true;
    if (contact.released || !contact.down) state = {};
    return result;
  }
  std::array<State, 3> states{};
  uint32_t visit = 0;
  bool initialized = false;
};
static_assert(sizeof(HoldShortcutController) <= 128, "Review physical shortcut state growth");
