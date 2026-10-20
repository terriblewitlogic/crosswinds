// Button edges survive a slow e-paper redraw. This state machine has no
// platform or game dependencies; its owner supplies synchronization.
#pragma once
#include <stdint.h>

// The gesture that wakes the reader belongs to startup, even if the player
// holds or presses Power again while the e-paper image is still refreshing.
class X4PowerWakeGuard {
 public:
  static constexpr uint32_t RELEASE_MS = 80;

  void beginInteractive() {
    ready_ = true;
    armed_ = false;
    releasing_ = false;
  }

  bool sample(uint32_t now, bool pressed) {
    if (!ready_) return false;
    if (armed_) return pressed;
    if (pressed) releasing_ = false;
    else if (!releasing_) { releasing_ = true; releasedAt_ = now; }
    else if ((uint32_t)(now - releasedAt_) >= RELEASE_MS) armed_ = true;
    return false;
  }

 private:
  uint32_t releasedAt_ = 0;
  bool ready_ = false, armed_ = false, releasing_ = false;
};

class X4ButtonEvents {
 public:
  static constexpr uint8_t BUTTON_COUNT = 7;
  static constexpr uint8_t POWER_BUTTON = 6;
  static constexpr uint8_t CAPACITY = 16;
  static constexpr uint32_t DEBOUNCE_MS = 20;

  void sample(uint32_t now, uint8_t mask) {
    for (uint8_t button = 0; button < BUTTON_COUNT; ++button) {
      uint8_t bit = (uint8_t)(1u << button);
      if ((mask ^ candidate_) & bit) {
        candidate_ ^= bit;
        changedAt_[button] = now;
      }
      if (!((candidate_ ^ stable_) & bit) ||
          (uint32_t)(now - changedAt_[button]) < DEBOUNCE_MS) continue;
      stable_ ^= bit;
      if (!(stable_ & bit)) continue;  // release arms the next physical press
      if (button == POWER_BUTTON) powerPending_ = true;
      else if (count_ < CAPACITY) {
        queue_[(head_ + count_) % CAPACITY] = button;
        ++count_;
      } else if (dropped_ != UINT32_MAX) ++dropped_;
    }
  }

  int take() {
    if (powerPending_) { powerPending_ = false; return POWER_BUTTON; }
    if (!count_) return -1;
    uint8_t button = queue_[head_];
    head_ = (uint8_t)((head_ + 1) % CAPACITY);
    --count_;
    return button;
  }

  // Screen transitions invalidate ordinary actions. Also absorb an incipient
  // press still being debounced, so held OK cannot activate the next screen.
  // Power keeps its independent candidate and committed event.
  void discardOrdinary() {
    head_ = 0; count_ = 0;
    stable_ |= (uint8_t)(candidate_ & ~(1u << POWER_BUTTON));
  }
  uint32_t dropped() const { return dropped_; }

  bool pending(uint8_t button) const {
    if (button == POWER_BUTTON) return powerPending_;
    for (uint8_t i = 0; i < count_; ++i)
      if (queue_[(head_ + i) % CAPACITY] == button) return true;
    return false;
  }

 private:
  uint32_t changedAt_[BUTTON_COUNT]{};
  uint8_t candidate_ = 0, stable_ = 0;
  uint8_t queue_[CAPACITY]{};
  uint8_t head_ = 0, count_ = 0;
  bool powerPending_ = false;
  uint32_t dropped_ = 0;
};
