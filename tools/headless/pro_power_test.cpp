#include "../../pro_power_button.h"
#include <cassert>
int main() {
  ProPowerButton gate;
  assert(!gate.update(true, true, true, false));  // power held through boot
  assert(!gate.update(false, true, false, false));  // raw release before debounce
  assert(!gate.update(false, false, false, true));
  assert(!gate.update(false, false, false, false));  // now armed
  assert(!gate.update(true, true, true, false));
  assert(!gate.update(false, true, false, false));
  assert(gate.update(false, false, false, true));  // fresh complete press
  assert(!gate.update(false, false, false, true));  // cannot repeat without press
  ProPowerButton cold;
  assert(!cold.update(false, false, false, false));  // USB boot
  assert(!cold.update(true, true, true, false));
  assert(cold.update(false, false, false, true));
}
