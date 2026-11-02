#pragma once

// Ignore the boot/wake hold and require a fresh, debounced press and release.
class ProPowerButton {
  bool armed = false;
  bool pressed = false;
 public:
  bool update(bool physicalDown, bool logicalDown, bool pressEdge, bool releaseEdge) {
    if (!armed) {
      if (!physicalDown && !logicalDown && !releaseEdge) armed = true;
      return false;
    }
    if (pressEdge) pressed = true;
    if (!releaseEdge || !pressed) return false;
    pressed = false;
    return true;
  }
};
