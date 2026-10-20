#include "x4_button_events.h"
#include <cstdio>
#include <cstdlib>

static void require(bool result, const char* message) {
  if (result) return;
  std::fprintf(stderr, "x4 button events: FAIL: %s\n", message);
  std::exit(1);
}

struct Fixture {
  X4ButtonEvents events;
  uint32_t now = 0;
  void hold(uint8_t mask, uint32_t duration) {
    for (uint32_t elapsed = 0; elapsed < duration; elapsed += 10, now += 10)
      events.sample(now, mask);
  }
  void tap(uint8_t button) { hold((uint8_t)(1u << button), 80); hold(0, 80); }
};

// Match the device pipeline: filter raw POWER, retain the other GPIO bits,
// then feed the shared edge queue. No consumer runs during simulated stamps.
struct WakeFixture {
  static constexpr uint8_t POWER = 1u << X4ButtonEvents::POWER_BUTTON;
  X4PowerWakeGuard guard;
  X4ButtonEvents events;
  uint32_t now = 0;
  void hold(uint8_t rawMask, uint32_t duration) {
    for (uint32_t elapsed = 0; elapsed < duration; elapsed += 10, now += 10) {
      uint8_t mask = (uint8_t)(rawMask & ~POWER);
      if (guard.sample(now, (rawMask & POWER) != 0)) mask |= POWER;
      events.sample(now, mask);
    }
  }
  void ready() { guard.beginInteractive(); }
  void release() { hold(0, X4PowerWakeGuard::RELEASE_MS + 10); }
};

static void wake_power_regressions() {
  constexpr uint8_t POWER = WakeFixture::POWER;
  WakeFixture held;
  held.hold(POWER, 5000);
  require(held.events.take() == -1, "held wake never queues POWER during startup stamps");
  held.ready();
  held.hold(POWER, 3000);
  held.hold(0, 10); held.hold(POWER, 200);
  held.hold(0, X4PowerWakeGuard::RELEASE_MS - 10); held.hold(POWER, 200);
  require(held.events.take() == -1, "brief or nearly complete release bounce cannot arm held wake");
  held.hold(POWER | (1u << 3), 80);
  require(held.events.take() == 3 && held.events.take() == -1,
          "ordinary navigation works while a held wake POWER remains suppressed");
  held.release(); held.hold(POWER, 80);
  require(held.events.take() == 6 && held.events.take() == -1,
          "stable post-startup release permits exactly one fresh POWER press");
  held.hold(POWER, 3000);
  require(held.events.take() == -1, "accepted POWER held through redraw does not repeat");

  WakeFixture shortTap;
  shortTap.hold(POWER, 40); shortTap.hold(0, 3000);
  require(shortTap.events.take() == -1, "short wake tap released before setup cannot queue shutdown");
  shortTap.ready();
  shortTap.hold(0, X4PowerWakeGuard::RELEASE_MS - 10); shortTap.hold(POWER, 100);
  require(shortTap.events.take() == -1, "pre-ready release time cannot prematurely arm POWER");
  shortTap.release(); shortTap.hold(POWER, 80);
  require(shortTap.events.take() == 6 && shortTap.events.take() == -1,
          "fresh POWER works after a short-tap boot and a full ready release");

  WakeFixture impatient;
  impatient.hold(0, 2000); impatient.hold(POWER, 80); impatient.hold(0, 100);
  impatient.hold(POWER, 80); impatient.ready(); impatient.hold(POWER, 2000);
  require(impatient.events.take() == -1,
          "repeated POWER attempts during startup cannot replay after the final stamp");
  impatient.release();
  impatient.hold(POWER | (1u << 2) | (1u << 5), 80);
  require(impatient.events.take() == 6 && impatient.events.take() == 2 &&
          impatient.events.take() == 5 && impatient.events.take() == -1,
          "fresh POWER retains priority without losing simultaneous ordinary navigation");

  // Re-entering an interactive session must forget a prior release/arming.
  impatient.ready(); impatient.hold(POWER, 500);
  require(impatient.events.take() == -1, "a new interactive session suppresses a previously armed held POWER");
  impatient.release(); impatient.hold(POWER, 80); impatient.events.discardOrdinary();
  require(impatient.events.take() == 6 && impatient.events.take() == -1,
          "normal screen transitions preserve deliberate post-wake POWER");

  WakeFixture wrap;
  wrap.now = UINT32_MAX - 40; wrap.ready(); wrap.release(); wrap.hold(POWER, 80);
  require(wrap.events.take() == 6 && wrap.events.take() == -1,
          "wake release qualification survives millisecond wrap");
}

int main() {
  // A user presses and releases ten times while the game thread is occupied
  // composing/pushing a frame. No consumer runs during the entire 1.6 seconds.
  Fixture blocked;
  for (int i = 0; i < 10; ++i) blocked.tap((uint8_t)(i % 6));
  for (int i = 0; i < 10; ++i)
    require(blocked.events.take() == i % 6, "complete taps during redraw retain FIFO order");
  require(blocked.events.take() == -1, "ten physical taps produce exactly ten actions");

  Fixture held;
  held.hold(2, 2000);
  require(held.events.take() == 1 && held.events.take() == -1, "long hold never repeats");
  held.hold(0, 80); held.hold(2, 80);
  require(held.events.take() == 1, "a release during redraw arms the same button again");

  Fixture bounce;
  bounce.hold(4, 10); bounce.hold(0, 10); bounce.hold(4, 10); bounce.hold(0, 40);
  require(bounce.events.take() == -1, "sub-debounce noise does not become navigation");
  bounce.hold(4, 80); bounce.hold(0, 10); bounce.hold(4, 100);
  require(bounce.events.take() == 2 && bounce.events.take() == -1,
          "brief release bounce cannot duplicate a held press");

  Fixture simultaneous;
  simultaneous.hold((1u << 2) | (1u << 5) | (1u << 6), 80);
  require(simultaneous.events.take() == 6, "POWER wins over simultaneous ordinary presses");
  require(simultaneous.events.take() == 2 && simultaneous.events.take() == 5,
          "front and rocker presses are both retained");
  require(simultaneous.events.take() == -1, "simultaneous presses are not repeated");

  Fixture transition;
  transition.tap(2); transition.hold((1u << 1) | (1u << 6), 80);
  transition.events.discardOrdinary();
  require(transition.events.take() == 6, "screen transition preserves pending POWER");
  require(transition.events.take() == -1, "screen transition drops queued navigation and OK");
  transition.hold(2, 400);
  require(transition.events.take() == -1, "transition does not synthesize a press from held OK");
  transition.hold(0, 80); transition.tap(1);
  require(transition.events.take() == 1, "new press after transition is accepted");

  Fixture incipient;
  incipient.hold((1u << 1) | (1u << 6), 10);
  incipient.events.discardOrdinary();
  incipient.hold((1u << 1) | (1u << 6), 80);
  require(incipient.events.take() == 6 && incipient.events.take() == -1,
          "transition absorbs in-flight OK debounce while POWER can complete");
  incipient.hold(0, 80); incipient.tap(1);
  require(incipient.events.take() == 1, "in-flight suppression rearms after release");

  Fixture overflow;
  for (int i = 0; i < 20; ++i) overflow.tap((uint8_t)(i % 6));
  overflow.tap(6);
  require(overflow.events.take() == 6, "POWER survives a full ordinary queue");
  require(overflow.events.dropped() == 4, "overflow drops only new excess events");
  for (int i = 0; i < X4ButtonEvents::CAPACITY; ++i)
    require(overflow.events.take() == i % 6, "overflow leaves prior queue intact");
  require(overflow.events.take() == -1, "overflow never corrupts or replays queue");
  overflow.tap(3);
  require(overflow.events.take() == 3, "queue recovers after overflow drains");

  Fixture wrap;
  wrap.now = UINT32_MAX - 15;
  wrap.tap(4);
  require(wrap.events.take() == 4 && wrap.events.take() == -1,
          "millisecond wrap preserves debounce timing");
  wake_power_regressions();
  std::puts("x4 button events: PASS (blocked redraw, no repeat, debounce, simultaneous, transition, overflow, timer wrap, startup POWER guard)");
}
