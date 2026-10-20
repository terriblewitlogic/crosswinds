#include "Arduino.h"
#include "../../crosswinds.ino"
#include "../headless/economy_fixtures.h"
#include "../../x4.h"
#include "../../x4_routes.h"
#include "../../x4_time.h"
#include "../../voyage_ui.h"

#include <cstdlib>
#include <cctype>
#include <sys/stat.h>
#include <string>
#include <vector>

// Tests finish on the SDL worker. Normal exit destroys global SDL monitor
// containers while the main thread can still be iterating them. Flush evidence
// and leave process cleanup to the OS instead of racing global destructors.
[[noreturn]] static void host_test_exit(int code) {
  ::fflush(nullptr);
  std::_Exit(code);
}

struct ScriptTap { uint32_t ms; int x, y; };
static ScriptTap taps[96];
static int tapCount = 0;
static bool builtinTouchTest = false, builtinTouchHeld = false;
static bool builtinTouchRejectedAtSea = false;
static int builtinTouchNext = 0;
static uint32_t builtinTouchStarted = 0, builtinTouchReadyAt = 0;

static void parse_taps() {
  const char* text = getenv("SH_TAPS");
  if (!text) return;
  while (*text && tapCount < 96) {
    ScriptTap t{};
    if (sscanf(text, "%u:%d,%d", &t.ms, &t.x, &t.y) == 3) taps[tapCount++] = t;
    const char* next = strchr(text, ';');
    if (!next) break;
    text = next + 1;
  }
}

static void schedule_tap(uint32_t at, int x, int y) {
  if (tapCount < (int)(sizeof(taps) / sizeof(taps[0]))) taps[tapCount++] = ScriptTap{at, x, y};
}

// Scripted buttons for the Xteink X4 preview: SH_KEYS="ms:K;ms:K;..." where K
// is U/D/L/R (rocker and front arrows), O (confirm), B (back) or P (power).
struct ScriptKey { uint32_t ms; uint8_t button; };
static ScriptKey keys[200];
static int keyCount = 0;
static uint32_t x4TestCheckAt = 0;
static bool x4BuiltinKeys = false;
static uint32_t x4BuiltinPressStart = 0;
static int x4BuiltinNextKey = 0;
static uint8_t x4BuiltinPhase = 0, x4BuiltinReleasePolls = 0;
static uint32_t x4BuiltinPressBefore = 0, x4BuiltinReadyAt = 0;
static uint8_t x4MarketSeen = 0, x4MarketTrades = 0;
// Optional, host-only manual QA input. Commands are consumed one at a time
// through the same debounced physical-button path as the reader itself.
static const char* x4LiveDir = nullptr;
static unsigned x4LiveCommand = 1;
static uint8_t x4LiveButton = 0, x4LivePhase = 0, x4LiveReleasePolls = 0;
static uint32_t x4LivePressBefore = 0, x4LiveStarted = 0, x4LiveDuration = 0;
static bool x4LiveDone = false;
static const char* x4LiveResult = "ready";

static uint8_t live_qa_buttons(uint32_t now) {
  if (x4LivePhase == 1) {
    bool serviced = x4_press_count() != x4LivePressBefore;
    bool elapsed = x4LiveDuration && now - x4LiveStarted >= x4LiveDuration;
    bool timeout = now - x4LiveStarted >= 60000;
    if ((x4LiveDuration ? elapsed : serviced) || timeout) {
      x4LiveResult = serviced ? "serviced" : timeout ? "timeout" : "unserviced";
      x4LivePhase = 2; x4LiveReleasePolls = 1;
    } else return (uint8_t)(1u << x4LiveButton);
  } else if (x4LivePhase == 2 && ++x4LiveReleasePolls >= 2) {
    x4LivePhase = 0; x4LiveDone = true;
  }
  return 0;
}

static int key_code(char c) {
  switch (toupper((unsigned char)c)) {
    case 'U': return X4_BTN_UP;
    case 'D': return X4_BTN_DOWN;
    case 'L': return X4_BTN_LEFT;
    case 'R': return X4_BTN_RIGHT;
    case 'O': case 'C': return X4_BTN_CONFIRM;
    case 'B': return X4_BTN_BACK;
    case 'P': return X4_BTN_POWER;
    default: return -1;
  }
}

static void schedule_key(uint32_t at, int button) {
  if (button >= 0 && keyCount < (int)(sizeof(keys) / sizeof(keys[0])))
    keys[keyCount++] = ScriptKey{at, (uint8_t)button};
}

static void parse_keys() {
  const char* text = getenv("SH_KEYS");
  if (!text) return;
  while (*text && keyCount < (int)(sizeof(keys) / sizeof(keys[0]))) {
    unsigned ms = 0; char k = 0;
    if (sscanf(text, "%u:%c", &ms, &k) == 2) schedule_key(ms, key_code(k));
    const char* next = strchr(text, ';');
    if (!next) break;
    text = next + 1;
  }
}

uint8_t sh_test_buttons() {
  uint32_t now = millis();
  uint8_t mask = 0;
  if (x4LiveDir) {
    mask = live_qa_buttons(now);
  } else if (x4BuiltinKeys) {
    // Built-in journeys wait for the real input layer to service a press and
    // see two released polls before sending another. This survives slow SDL
    // rendering and e-paper-like blocking without changing the game clock.
    if (x4BuiltinPhase == 2) {
      if (++x4BuiltinReleasePolls >= 2) x4BuiltinPhase = 0;
    } else if (x4BuiltinPhase == 1) {
      if (x4_press_count() != x4BuiltinPressBefore) {
        ++x4BuiltinNextKey;
        x4BuiltinPhase = 2; x4BuiltinReleasePolls = 1;
        x4BuiltinReadyAt = now + 40;
      } else mask = (uint8_t)(1u << keys[x4BuiltinNextKey].button);
    } else if (x4BuiltinNextKey < keyCount && now >= x4BuiltinReadyAt &&
               (x4BuiltinNextKey || now >= keys[0].ms)) {
      x4BuiltinPhase = 1;
      x4BuiltinPressBefore = x4_press_count();
      mask = (uint8_t)(1u << keys[x4BuiltinNextKey].button);
    }
  } else {
    // Explicit SH_KEYS scripts retain their documented wall-clock timing.
    for (int i = 0; i < keyCount; ++i)
      if (now >= keys[i].ms && now < keys[i].ms + 140) mask |= (uint8_t)(1u << keys[i].button);
  }
  if (getenv("SH_TRACE_KEYS")) {
    static int previousMask = -1;
    if (previousMask != mask) {
      ::printf("script keys: ms %lu mask %u count %d handled %lu asleep %d\n", (unsigned long)now,
               mask, keyCount, (unsigned long)x4_press_count(), board_x4_asleep());
      fflush(stdout);
      previousMask = mask;
    }
  }
  return mask;
}

// Physical button that performs an action, via the mapping table in x4.h.
static int x4_button_for(uint8_t action) {
  for (uint8_t b = 0; b < X4_BTN_COUNT; ++b)
    if (X4_ACTION_OF_BUTTON[b] == action) return b;
  return -1;
}

// Button-driven counterpart of the touch input test: walks the guided first
// voyage with the focus cursor, loads a contract, flips ports with the
// rocker, zooms the map, plots and sails a route, changes the game speed,
// saves, and finally has ROUTE refused for a ship at sea.
static bool schedule_x4_test() {
  save_reset();
  sim_new_campaign(42);
  render_frame();
  int compatibleRow = -1, presses = 0;
  const Ship& ship = g_ships[0];
  for (int row = 0; row < CARD_VISIBLE_ROWS; ++row) {
    const Contract& c = g_contracts[0][row];
    if (c.active != 1) continue;
    // The hold already carries the new-company gift; count only free space.
    bool fits = c.kind == JOB_PASSENGER ? c.quantity <= sim_ship_berths(ship) - sim_ship_passenger_load(ship) :
                                         c.quantity <= sim_ship_capacity(ship) - sim_ship_used_capacity(ship);
    if (fits) { compatibleRow = row; break; }
    ++presses;  // active rows before the compatible one are focus stops
  }
  if (compatibleRow < 0) return false;
  uint32_t t = millis() + 600;  // after the first frame, so a slow start cannot miss a key
  auto act = [&](uint8_t action) { schedule_key(t, x4_button_for(action)); t += 220; };
  // Stage 0 column: PORT|MAP, JOBS, ROUTE, NOTICES. BACK at the top level opens
  // the menu; the rest of the column arrives as the company advances.
  act(X4_ACT_DOWN);                                 // the title ignores everything but OK
  act(X4_ACT_OK);                                   // title -> harbour, focus JOBS
  act(X4_ACT_OK);                                   // open JOBS
  for (int i = 0; i < presses; ++i) act(X4_ACT_DOWN);
  act(X4_ACT_OK);                                   // load the contract
  act(X4_ACT_BACK);                                 // close JOBS, focus ROUTE (guidance)
  act(X4_ACT_UP); act(X4_ACT_UP);                   // up to MAP
  act(X4_ACT_OK);                                   // harbour -> world map
  act(X4_ACT_PAGE_NEXT); act(X4_ACT_PAGE_PREV);     // rocker: next port, back again
  act(X4_ACT_DOWN); act(X4_ACT_DOWN);               // ROUTE
  act(X4_ACT_OK);                                   // ROUTE card
  act(X4_ACT_OK);                                   // first neighbour, focus jumps to SAIL
  act(X4_ACT_OK);                                   // SAIL
  act(X4_ACT_BACK);                                 // top level: menu
  act(X4_ACT_DOWN); act(X4_ACT_DOWN);               // game speed row
  act(X4_ACT_OK);                                   // 1x -> 2x
  act(X4_ACT_BACK);                                 // close
  act(X4_ACT_BACK);                                 // menu again
  act(X4_ACT_UP); act(X4_ACT_UP); act(X4_ACT_UP);   // wrap to CLOSE, JOURNAL, then SAVE
  act(X4_ACT_OK);                                   // save now
  act(X4_ACT_BACK);                                 // close: the cursor returns to ROUTE
  act(X4_ACT_OK);                                   // refused while at sea
  x4TestCheckAt = t + 180000;                       // initial watchdog; serviced presses extend it
  return true;
}

static bool x4_context_selftest() {
  historical_campaign(42, 1);
  g_campaign.stageSeen = 4; g_campaign.legacyFeatureMask = 0xffff; g_campaign.coins = 100000;
  g_campaign.tutorialDismissed = 1; g_ui.title = 0;
  g_ui.card = CARD_CHARTER;
  const int32_t homeCoins = g_campaign.coins;
  input_test_touch(X4_CHARTER_ROWS[0].x + 20, X4_CHARTER_ROWS[0].y + 20);
  if (g_campaign.coins != homeCoins || g_campaign.chartMask != 1 ||
      g_campaign.regionMask != 1) return false;  // home charter is already open
  for (int region = 1; region < REGION_COUNT; ++region) {
    const HitRegion& r = X4_CHARTER_ROWS[region];
    input_test_touch(r.x + 20, r.y + 20);
    if (!(g_campaign.chartMask & (1u << region))) return false;
    g_campaign.charterProgress[region] = BALANCE.charterTarget[region];
    input_test_touch(r.x + 20, r.y + 20);
    if (!(g_campaign.regionMask & (1u << region))) return false;
  }
  historical_campaign(42, 1);
  g_campaign.stageSeen = 3; g_campaign.legacyFeatureMask = 0xffff; g_campaign.coins = 10000;
  g_campaign.tutorialDismissed = 1; g_ui.title = 0;
  g_ports[0].officeLevel = 3;
  g_ui.card = CARD_CAPTAINS; g_ui.captainView = 2;
  input_test_touch(X4_RECRUIT_ROWS[1].x + 30, X4_RECRUIT_ROWS[1].y + 80);
  if (g_ui.selectedCaptain != 1) return false;
  input_test_touch(X4_RECRUIT_ROWS[0].x + 30, X4_RECRUIT_ROWS[0].y + 170);
  if (g_ui.selectedCaptain != 0) return false;
  historical_campaign(42, 1);
  g_campaign.stageSeen = 3; g_campaign.legacyFeatureMask = 0xffff;
  g_campaign.tutorialDismissed = 1;
  g_ui.title = 0;
  auto openShip = [&](uint8_t state) {
    g_ships[0].state = state;
    g_ships[0].currentPort = g_ships[0].fromPort = 0;
    g_ships[0].toPort = 1;
    g_ui.card = CARD_FLEET; g_ui.page = 0;
    input_test_touch(CARD_ROWS[0].x + 20, CARD_ROWS[0].y + 10);
  };
  openShip(SHIP_DOCKED);
  if (g_ui.card != CARD_NONE || g_ui.scene != SCENE_PORT || g_ui.selectedPort != 0) return false;
  openShip(SHIP_SAILING);
  if (g_ui.card != CARD_NONE || g_ui.scene != SCENE_WORLD || g_ui.selectedPort != 1) return false;
  openShip(SHIP_DISABLED);
  if (g_ui.card != CARD_SHIPYARD || g_ui.page != 2 || g_ui.selectedPort != 0) return false;
  g_incident = Incident{1, 0, INCIDENT_RESCUE, RISK_EVEN};
  openShip(SHIP_AWAITING_DECISION);
  if (g_ui.card != CARD_INCIDENT) return false;
  g_incident.shipIndex = 1;
  openShip(SHIP_AWAITING_DECISION);
  if (g_ui.card != CARD_INBOX) return false;  // never open another ship's decision
  g_incident.shipIndex = 0;
  g_ships[0].legTotalMinutes = 7200; g_ships[0].legElapsedMinutes = 1800;
  g_ui.card = CARD_INCIDENT;
  input_test_touch(INCIDENT_OPTIONS[0].x + 20, INCIDENT_OPTIONS[0].y + 10);
  if (g_ui.card != CARD_OUTCOME || !g_incidentOutcome.valid || g_incidentOutcome.shipIndex != 0) return false;
  input_test_touch(CARD_FOOTER[1].x + 20, CARD_FOOTER[1].y + 10);
  if (g_ui.card != CARD_OUTCOME) return false;  // absent NEXT DECISION does nothing
  input_test_touch(CARD_FOOTER[2].x + 20, CARD_FOOTER[2].y + 10);
  return g_ui.card == CARD_NONE;
}

// Navigation checks the plan promised: BACK inside the captain detail view
// steps back to the roster instead of closing, and OK on the route's price
// row points at the rocker while the rocker still changes the price.
static bool schedule_x4_nav_test() {
  if (!x4_context_selftest()) return false;
  save_reset();
  historical_campaign(42, 1);
  g_campaign.stageSeen = 3; g_campaign.legacyFeatureMask = 0xffff;          // FLEET, CAPTAINS and route rules are open
  g_campaign.tutorialDismissed = 1;  // no guidance jumps
  g_ships[0].routeTemplate.buyGood = GOOD_GRAIN;
  g_ships[0].routeTemplate.maxBuyPrice = 30;
  render_frame();
  int fleet = x4_column_slot(X4_CMD_FLEET), route = x4_column_slot(X4_CMD_ROUTE);
  if (fleet < 0 || route < 0 || route >= fleet) return false;
  int candidates = 0;
  for (int p = 1; p < MAX_PORTS; ++p)
    if (g_ports[p].unlocked && content_lane_between(0, (uint8_t)p) >= 0) ++candidates;
  if (!candidates || candidates >= ROUTE_VISIBLE_ROWS) return false;
  uint32_t t = millis() + 600;
  auto act = [&](uint8_t action) { schedule_key(t, x4_button_for(action)); t += 220; };
  act(X4_ACT_OK);                                    // title -> harbour, cursor at the top
  for (int i = 0; i < fleet; ++i) act(X4_ACT_DOWN);  // FLEET
  act(X4_ACT_OK);                                    // fleet card: ship row, PAGE, CAPTAINS, CLOSE
  act(X4_ACT_DOWN); act(X4_ACT_DOWN);                // CAPTAINS
  act(X4_ACT_OK);                                    // roster
  act(X4_ACT_OK);                                    // captain detail
  act(X4_ACT_BACK);                                  // back to the roster (not CLOSE)
  act(X4_ACT_BACK);                                  // roster's CLOSE: harbour, cursor on FLEET
  int market = x4_column_slot(X4_CMD_MARKET);
  if (market < 0 || market >= route) return false;
  for (int i = 0; i < fleet - market; ++i) act(X4_ACT_UP);  // MARKET
  act(X4_ACT_OK);                                    // market card, grain selected
  // Every good is reachable with the front buttons. The last two previously
  // had no physical path after the rocker became an immediate trade shortcut.
  for (int i = 0; i < GOOD_MEDICINES; ++i) act(X4_ACT_DOWN);
  act(X4_ACT_PAGE_NEXT); act(X4_ACT_PAGE_PREV);       // buy/sell Medicines, up to five
  act(X4_ACT_DOWN);                                  // Porcelain
  act(X4_ACT_PAGE_NEXT); act(X4_ACT_PAGE_PREV);       // buy/sell Porcelain
  act(X4_ACT_DOWN); act(X4_ACT_OK);                  // BUY footer purchases one Porcelain
  act(X4_ACT_UP);                                    // Porcelain row, same selected good
  act(X4_ACT_PAGE_PREV);                             // sell that last unit
  for (int i = 0; i < GOOD_PORCELAIN; ++i) act(X4_ACT_UP);
  act(X4_ACT_PAGE_NEXT);                             // rocker buys up to five: fills the sloop's hold
  act(X4_ACT_BACK);                                  // close, cursor on MARKET
  for (int i = 0; i < route - market; ++i) act(X4_ACT_DOWN);  // ROUTE
  act(X4_ACT_OK);                                    // route card, page 0
  for (int i = 0; i < candidates; ++i) act(X4_ACT_DOWN);   // "Route rules and preparation"
  act(X4_ACT_OK);                                    // page 1
  for (int i = 0; i < 4; ++i) act(X4_ACT_DOWN);      // maximum purchase price
  act(X4_ACT_OK);                                    // must not change the price
  act(X4_ACT_PAGE_NEXT);                             // +10
  x4TestCheckAt = t + 180000;
  return true;
}


static bool schedule_input_test() {
  builtinTouchTest = true;
  save_reset();
  sim_new_campaign(42);
  render_frame();
  int compatibleRow = -1;
  const Ship& ship = g_ships[0];
  for (int row = 0; row < CARD_VISIBLE_ROWS; ++row) {
    const Contract& c = g_contracts[0][row];
    // The hold already carries the new-company gift; count only free space.
    bool fits = c.kind == JOB_PASSENGER ? c.quantity <= sim_ship_berths(ship) - sim_ship_passenger_load(ship) :
                                         c.quantity <= sim_ship_capacity(ship) - sim_ship_used_capacity(ship);
    if (c.active == 1 && fits) { compatibleRow = row; break; }
  }
  if (compatibleRow < 0) return false;
  uint32_t base = millis();
  schedule_tap(base + 100, 120, 80);                         // title
  schedule_tap(base + 300, 90, ACTIONBAR_Y + 7);             // jobs
  schedule_tap(base + 500, 50, CARD_ROW_Y + compatibleRow * CARD_ROW_H + 5);
  schedule_tap(base + 700, 193, 128);                        // close jobs
  schedule_tap(base + 900, 30, ACTIONBAR_Y + 7);             // map
  schedule_tap(base + 1100, 170, 20);                        // zoom 1x -> 2x
  schedule_tap(base + 1300, 170, 20);                        // zoom 2x -> 4x
  schedule_tap(base + 1500, 170, 20);                        // zoom 4x -> 1x
  schedule_tap(base + 1700, 90, ACTIONBAR_Y + 7);            // route
  schedule_tap(base + 1900, 50, CARD_ROW_Y + 5);             // first neighbor
  schedule_tap(base + 2100, 120, 128);                       // sail
  schedule_tap(base + 2300, 90, ACTIONBAR_Y + 7);            // reject route while at sea
  schedule_tap(base + 2500, 210, ACTIONBAR_Y + 7);           // menu
  schedule_tap(base + 2700, 50, CARD_ROW_Y + 2 * CARD_ROW_H + 5); // speed 1x -> 2x
  schedule_tap(base + 2900, 193, 128);                       // close, clearing arm
  schedule_tap(base + 3100, 210, ACTIONBAR_Y + 7);           // menu again
  schedule_tap(base + 3300, 50, 128);                              // save now
  schedule_tap(base + 3500, 193, 128);                       // close
  return true;
}

bool sh_test_touch(int32_t* x, int32_t* y) {
  uint32_t now = millis();
  if (builtinTouchTest) {
    // The built-in journey must observe every down/up pair, even if a host
    // render takes longer than one of the original 140 ms touch windows.
    // Explicit SH_TAPS scripts below retain their wall-clock semantics.
    if (builtinTouchNext >= tapCount) return false;
    if (builtinTouchHeld && now - builtinTouchStarted >= 140) {
      // Inspect the rejection when it is produced. A slow host may correctly
      // expire the transient notice before the later menu/save taps finish.
      if (builtinTouchNext == 11)
        builtinTouchRejectedAtSea = g_ui.noticeCode == SIM_BAD_SHIP_STATE &&
          g_ui.card == CARD_NONE && g_ships[0].state == SHIP_SAILING;
      builtinTouchHeld = false; ++builtinTouchNext;
      builtinTouchReadyAt = now + 60;
      return false;
    }
    if (!builtinTouchHeld) {
      if (now < builtinTouchReadyAt || (!builtinTouchNext && now < taps[0].ms)) return false;
      builtinTouchHeld = true; builtinTouchStarted = now;
    }
    *x = taps[builtinTouchNext].x * SCALE;
    *y = taps[builtinTouchNext].y * SCALE;
    return true;
  }
  for (int i = 0; i < tapCount; ++i) {
    if (now >= taps[i].ms && now < taps[i].ms + 140) {
      *x = taps[i].x * SCALE;
      *y = taps[i].y * SCALE;
      return true;
    }
  }
  return false;
}

static void dump_ppm(const char* path) {
  std::vector<lgfx::rgb888_t> pixels((size_t)SCREEN_W * SCREEN_H);
  display.readRect(0, 0, SCREEN_W, SCREEN_H, pixels.data());
  FILE* f = fopen(path, "wb");
  if (!f) { ::printf("host: cannot write %s\n", path); return; }
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  for (const auto& p : pixels) {
    uint8_t rgb[3] = { p.r, p.g, p.b };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  ::printf("host: screenshot -> %s\n", path);
}

static void dump_ppm_native(const char* path) {
  std::vector<lgfx::rgb888_t> pixels((size_t)SCREEN_W * SCREEN_H);
  display.readRect(0, 0, SCREEN_W, SCREEN_H, pixels.data());
  FILE* f = fopen(path, "wb");
  if (!f) { ::printf("host: cannot write %s\n", path); return; }
  fprintf(f, "P6\n%d %d\n255\n", IW, IH);
  for (int y = 0; y < IH; ++y) for (int x = 0; x < IW; ++x) {
    const auto& p = pixels[(size_t)(y * SCALE) * SCREEN_W + x * SCALE];
    uint8_t rgb[3] = { p.r, p.g, p.b };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

#include "x4_buffered_input_test.h"
#include "x4_voyage_ui_test.h"
#include "x4_growth_ui_test.h"
#include "x4_standby_test.h"
#include "x4_return_ui_test.h"
#include "x4_dispatch_acceptance_test.h"
#include "x4_merchant_ui_test.h"
#include "x4_merchant_return_test.h"
#include "x4_startup_input_test.h"
#include "x4_watch_journey_test.h"
#include "x4_navigation_test.h"
#include "x4_goal_moment_test.h"
#include "x4_story_test.h"
#include "x4_atlas_test.h"
#include "x4_onboarding_test.h"
struct X4ManifestProbe {
  uint16_t afterKey;
  uint8_t page, scroll, cargo, warehouse;
  uint16_t cargoLast, warehouseLast;
  int8_t focusRow;
  const char* capture;
};
static X4ManifestProbe x4ManifestProbes[20];
static int x4ManifestProbeCount = 0, x4ManifestProbeNext = 0;
static bool x4ManifestProbesOk = true, x4ManifestReadonly = false;
static int x4ManifestReadonlyAfter = 0;

// Company -> Details -> Cargo & warehouse -> choose the ship -> its manifest:
// the merchant-economy path (input.cpp CARD_MORE, CARD_PORT_SHIPS purpose 2).
static bool schedule_x4_manifest_test() {
  save_reset(); sim_new_campaign(42);
  g_campaign.tutorialDismissed = 1; g_campaign.coins = 10000;
  g_campaign.deliveredUnits = 60;  // warehouse transfers open at 36 delivered units
  // A later company: an Indiaman's hold, and every charter open so its deep
  // draft has harbours to sail to once the cargo is checked at sea.
  g_campaign.regionMask = g_campaign.chartMask = (1u << REGION_COUNT) - 1;
  for (int p = 0; p < MAX_PORTS; ++p) g_ports[p].unlocked = 1;
  Ship& ship = g_ships[0]; ship.classId = 4;
  g_ports[0].warehouseLevel = 2;
  for (int i = 0; i < GOOD_COUNT; ++i) {  // replaces the starter Grain
    CargoItem item{}; item.good = (uint8_t)i; item.quantity = 1;
    item.kind = CARGO_MARKET; item.jobKind = JOB_FREIGHT; item.projectRegion = 0xFF;
    item.destination = item.sourcePort = NO_PORT;
    item.paidUnitPrice = (uint16_t)(100 + i); ship.cargo[i] = item;
    item.paidUnitPrice = (uint16_t)(200 + i); g_ports[0].warehouse[i] = item;
  }
  ship.cargoCount = g_ports[0].warehouseCount = GOOD_COUNT;
  g_ui.title = 0; g_ui.scene = SCENE_COMPANY; g_ui.card = CARD_NONE;
  render_frame();
  if (!input_test_focus_command(X4_CMD_LEDGER)) return false;  // DETAILS
  int cargo = -1;
  for (int i = 0; i < voyage_detail_count(); ++i)
    if (VOYAGE_DETAIL_CARDS[voyage_detail_index(i)] == CARD_MANIFEST) cargo = i;
  if (cargo < 0 || cargo >= CARD_VISIBLE_ROWS) return false;
  uint32_t t = millis() + 600;
  auto act = [&](uint8_t action) { schedule_key(t, x4_button_for(action)); t += 220; };
  auto probe = [&](uint8_t page, uint8_t scroll, uint8_t cargoCount, uint8_t warehouseCount,
                   uint16_t cargoLast, uint16_t warehouseLast, int row, const char* capture = nullptr) {
    x4ManifestProbes[x4ManifestProbeCount++] = {(uint16_t)keyCount, page, scroll,
      cargoCount, warehouseCount, cargoLast, warehouseLast, (int8_t)row, capture};
  };
  act(X4_ACT_OK); // Details
  for (int i = 0; i < cargo; ++i) act(X4_ACT_DOWN);
  act(X4_ACT_OK); // Cargo & warehouse asks which ship
  act(X4_ACT_OK); // the Petrel, first row
  probe(0, 0, 8, 8, 107, 207, 0);
  act(X4_ACT_PAGE_NEXT);
  probe(0, 6, 8, 8, 107, 207, 0, "cargo-second-page");
  act(X4_ACT_DOWN); act(X4_ACT_OK);  // transfer actual eighth cargo stack
  probe(0, 6, 7, 9, 106, 107, 0);
  act(X4_ACT_OK);                  // last stack on page disappears: clamp
  probe(0, 0, 6, 10, 105, 106, 0);
  act(X4_ACT_UP); act(X4_ACT_UP); act(X4_ACT_OK); // WAREHOUSE tab
  probe(1, 0, 6, 10, 105, 106, -1);
  act(X4_ACT_PAGE_NEXT);
  probe(1, 6, 6, 10, 105, 106, 0, "warehouse-second-page");
  act(X4_ACT_DOWN); act(X4_ACT_OK); // original warehouse eighth stack
  probe(1, 6, 7, 9, 207, 106, 1);
  act(X4_ACT_UP); act(X4_ACT_OK);   // original seventh stack
  probe(1, 6, 8, 8, 206, 106, 0);
  act(X4_ACT_OK);
  probe(1, 6, 9, 7, 107, 106, 0);
  act(X4_ACT_OK);
  probe(1, 0, 10, 6, 106, 205, 0, "warehouse-after-page-empties");
  act(X4_ACT_UP); act(X4_ACT_UP); act(X4_ACT_UP); act(X4_ACT_OK); // SHIP
  probe(0, 0, 10, 6, 106, 205, -1);
  act(X4_ACT_PAGE_NEXT);
  probe(0, 6, 10, 6, 106, 205, 0);
  act(X4_ACT_PAGE_PREV);
  probe(0, 0, 10, 6, 106, 205, 0);
  act(X4_ACT_BACK); // back to the ship selector
  x4ManifestReadonlyAfter = keyCount;
  act(X4_ACT_OK);  // the tick sails the same ship first; its cargo remains readable at sea
  act(X4_ACT_PAGE_NEXT);
  probe(0, 6, 10, 6, 106, 205, -1, "sailing-cargo-readonly");
  act(X4_ACT_OK); // SHIP footer must not transfer anything while sailing
  probe(0, 6, 10, 6, 106, 205, -1);
  x4TestCheckAt = t + 180000;
  return true;
}

static void tick_x4_manifest_test(uint32_t serviced) {
  while (x4ManifestProbeNext < x4ManifestProbeCount &&
         serviced >= x4ManifestProbes[x4ManifestProbeNext].afterKey) {
    const X4ManifestProbe& p = x4ManifestProbes[x4ManifestProbeNext++];
    HitRegion focus{};
    bool ok = g_ui.card == CARD_MANIFEST && g_ui.page == p.page && g_ui.routeScroll == p.scroll &&
      g_ships[0].cargoCount == p.cargo && g_ports[0].warehouseCount == p.warehouse &&
      g_ships[0].cargo[p.cargo - 1].paidUnitPrice == p.cargoLast &&
      g_ports[0].warehouse[p.warehouse - 1].paidUnitPrice == p.warehouseLast &&
      x4_focus_region(&focus) && (p.focusRow < 0 ||
        (focus.y == CARD_ROWS[p.focusRow].y && focus.id == p.focusRow));
    x4ManifestProbesOk &= ok;
    ::printf("x4 manifest probe %d: %s page=%u offset=%u cargo=%u warehouse=%u focus=%d,%d\n",
      x4ManifestProbeNext, ok ? "PASS" : "FAIL", g_ui.page, g_ui.routeScroll,
      g_ships[0].cargoCount, g_ports[0].warehouseCount, focus.x, focus.y);
    if (p.capture) if (const char* dir = getenv("SH_X4_MANIFEST_DIR")) {
      ::mkdir(dir, 0755); char path[512];
      snprintf(path, sizeof(path), "%s/%s.ppm", dir, p.capture); dump_ppm_native(path);
    }
    fflush(stdout);
  }
  if (!x4ManifestReadonly && serviced >= (uint32_t)x4ManifestReadonlyAfter) {
    x4ManifestReadonly = true;
    bool selector = g_ui.card == CARD_PORT_SHIPS && g_ui.captainView == 2;
    bool sailed = false;
    for (uint8_t port = 1; port < MAX_PORTS && !sailed; ++port) {
      MerchantSailQuote quote = sim_merchant_sail_offer(0, port);
      sailed = quote.code == SIM_OK && sim_merchant_sail(0, quote);
    }
    g_ships[0].incidentArmed = 0;
    ::printf("x4 manifest: Back %s the ship selector; ship %s\n", selector ? "returns to" : "MISSES",
             sailed ? "sails with its cargo" : "COULD NOT SAIL");
    x4ManifestProbesOk &= selector && sailed && input_test_focus_row(0);
  }
}

static void live_qa_capture(unsigned command, const char* result) {
  char path[1024], temporary[1040];
  snprintf(path, sizeof(path), "%s/%06u.ppm", x4LiveDir, command);
  dump_ppm_native(path);
  snprintf(path, sizeof(path), "%s/%06u.json", x4LiveDir, command);
  snprintf(temporary, sizeof(temporary), "%s.tmp", path);
  FILE* f = fopen(temporary, "w");
  if (!f) return;
  static const char* cards[] = {"none", "jobs", "manifest", "route", "market",
    "fleet", "ledger", "incident", "shipyard", "charter", "menu", "help",
    "inbox", "event", "captains", "tutorial", "journal", "more", "arrival",
    "progress", "outcome", "voyage", "return-details", "voyage-load", "voyage-departure"};
  HitRegion focus{};
  bool focused = x4_focus_region(&focus);
  fprintf(f, "{\"command\":%u,\"result\":\"%s\",\"wallMs\":%lu,\"pressCount\":%lu,"
    "\"title\":%u,\"scene\":%u,\"card\":%u,\"cardName\":\"%s\",\"page\":%u,"
    "\"selectedPort\":%u,\"selectedShip\":%u,\"selectedGood\":%u,\"coins\":%ld,"
    "\"worldMinutes\":%lu,\"timeScale\":%u,\"paused\":%u,\"stage\":%u,"
    "\"notice\":%u,\"routeCount\":%u,\"zoom\":%u,\"asleep\":%s,"
    "\"focus\":{\"valid\":%s,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d},\"ships\":[",
    command, result, (unsigned long)millis(), (unsigned long)x4_press_count(),
    g_ui.title, g_ui.scene, g_ui.card, g_ui.card < sizeof(cards) / sizeof(cards[0]) ? cards[g_ui.card] : "unknown",
    g_ui.page, g_ui.selectedPort, g_ui.selectedShip, g_ui.selectedGood, (long)g_campaign.coins,
    (unsigned long)g_campaign.worldMinutes, g_campaign.timeScale, g_campaign.paused, sim_stage(),
    g_ui.noticeCode, g_ui.routeCount, g_ui.mapZoom, board_x4_asleep() ? "true" : "false",
    focused ? "true" : "false", focus.x, focus.y, focus.w, focus.h);
  for (uint8_t i = 0; i < g_campaign.shipCount; ++i) {
    const Ship& s = g_ships[i];
    fprintf(f, "%s{\"index\":%u,\"state\":%u,\"port\":%u,\"from\":%u,\"to\":%u,"
      "\"elapsedMinutes\":%u,\"totalMinutes\":%u,\"usedCapacity\":%d,\"capacity\":%d,\"cargo\":[",
      i ? "," : "", i, s.state, s.currentPort, s.fromPort, s.toPort,
      s.legElapsedMinutes, s.legTotalMinutes, sim_ship_used_capacity(s), sim_ship_capacity(s));
    for (uint8_t c = 0; c < s.cargoCount; ++c) {
      const CargoItem& cargo = s.cargo[c];
      fprintf(f, "%s{\"kind\":%u,\"good\":%u,\"quantity\":%u,\"destination\":%u}",
        c ? "," : "", cargo.kind, cargo.good, cargo.quantity, cargo.destination);
    }
    fprintf(f, "]}");
  }
  const VoyageQuote& quote = x4_voyage_quote();
  fprintf(f, "],\"draft\":{\"destination\":%u,\"code\":%u,\"quantity\":%u,"
    "\"payNow\":%ld,\"arrivalCoins\":%ld,\"profit\":%ld,\"quantities\":[",
    quote.destination, quote.code, quote.quantity, (long)quote.payNow,
    (long)quote.arrivalCoins, (long)quote.profit);
  for (int good = 0; good < GOOD_COUNT; ++good)
    fprintf(f, "%s%u", good ? "," : "", quote.quantities[good]);
  fprintf(f, "]}}\n");
  fclose(f);
  ::rename(temporary, path); // A completed JSON signals that both outputs are ready.
  ::printf("live QA: %06u %s -> %s\n", command, result, path); fflush(stdout);
}

static void live_qa_poll() {
  if (!x4LiveDir || x4LivePhase || x4LiveDone) return;
  char path[1024], command[80]{};
  snprintf(path, sizeof(path), "%s/%06u.cmd", x4LiveDir, x4LiveCommand);
  FILE* f = fopen(path, "r");
  if (!f) return;
  bool read = fgets(command, sizeof(command), f) != nullptr;
  fclose(f);
  if (!read) return; // The producer should atomically rename complete command files.
  char token[24]{}; char physicalKey = 0; unsigned duration = 0;
  int fields = sscanf(command, "%23s %c %u", token, &physicalKey, &duration);
  if (!strcmp(token, "snapshot")) {
    x4LiveResult = "snapshot"; x4LiveDone = true; return;
  }
  int button = fields == 1 && strlen(token) == 1 ? key_code(token[0]) :
    fields == 3 && !strcmp(token, "tap") && duration > 0 && duration <= 60000 ? key_code(physicalKey) : -1;
  if (button < 0) {
    x4LiveResult = "invalid-command"; x4LiveDone = true; return;
  }
  x4LiveButton = (uint8_t)button; x4LiveDuration = fields == 3 ? duration : 0;
  x4LiveStarted = millis(); x4LivePressBefore = x4_press_count();
  x4LivePhase = 1; x4LiveReleasePolls = 0;
}

struct X4WatchTest {
  uint32_t started, phaseAt, pushes, pushAt, world, elapsed;
  int x, y, focusX, focusY, movingFrames, movedFrames, captures, pointX, pointY;
  uint8_t phase, port, downs;
};
static X4WatchTest x4Watch{};

static void watch_action(uint8_t action) {
  schedule_key(millis() + 100, x4_button_for(action));
  // A press queued after a long wait (a whole voyage) gets its own window.
  if (x4TestCheckAt < millis() + 60000) x4TestCheckAt = millis() + 60000;
}

static void watch_capture(const char* label, uint32_t now) {
  const char* dir = getenv("SH_X4_WATCH_DIR");
  if (!dir) return;
  ::mkdir(dir, 0755);
  char nativeDir[384], path[512];
  snprintf(nativeDir, sizeof(nativeDir), "%s/native", dir); ::mkdir(nativeDir, 0755);
  snprintf(path, sizeof(path), "%s/%02d_%s-%lums.ppm", nativeDir, x4Watch.captures++, label,
           (unsigned long)(now - x4Watch.started));
  dump_ppm_native(path);
  int x = 0, y = 0; bool visible = render_host_map_position(0, &x, &y);
  ::printf("watch capture: %s / wall %lums / world %lu / leg %u / pixel %d,%d / focus %u,%u / visible %d\n",
           path, (unsigned long)(now - x4Watch.started), (unsigned long)g_campaign.worldMinutes,
           g_ships[0].legElapsedMinutes, x, y, g_ui.mapFocusX, g_ui.mapFocusY, visible);
  fflush(stdout);
}

// Aim the cursor at a card footer with physical Down presses. Returns true
// once it is focused; until then it queues one Down and the caller waits.
static bool x4_drive_footer(int id, uint8_t* downs) {
  HitRegion want = voyage_footer_region(g_ui.card, id), have{};
  if (x4_focus_region(&have) && have.x == want.x && have.y == want.y && have.w == want.w) return true;
  ++*downs; watch_action(X4_ACT_DOWN);
  return false;
}

// One step of the merchant departure through the ship's own screens:
// Ship -> Sail -> destination -> voyage review -> Sail. Returns 1 once the
// ship is on Watch, 0 while a press is queued, -1 on an unexpected screen.
// NO_PORT chooses the first listed destination.
static int x4_drive_departure(uint8_t destination, uint8_t* downs, MerchantSailQuote* quote) {
  if (voyage_watch_screen() && g_ships[0].state == SHIP_SAILING) return 1;
  if (*downs > 12) return -1;
  if (g_ui.card == CARD_SHIP) {
    if (!input_test_focus_row(1)) return -1;                 // Sail
    watch_action(X4_ACT_OK); return 0;
  }
  if (g_ui.card == CARD_VOYAGE && g_ui.page == 0) {
    for (int row = 0; row < VOYAGE_DESTINATION_ROWS; ++row)
      if (x4_voyage_destination(row) >= 0 &&
          (destination == NO_PORT || x4_voyage_destination(row) == destination)) {
        if (!input_test_focus_row(row)) return -1;
        watch_action(X4_ACT_OK); return 0;
      }
    return -1;
  }
  if (g_ui.card == CARD_VOYAGE && g_ui.page == 2) {
    *quote = x4_merchant_sail_quote();
    if (quote->code != SIM_OK || (destination != NO_PORT && quote->destination != destination)) return -1;
    if (x4_drive_footer(1, downs)) watch_action(X4_ACT_OK);  // Sail
    return 0;
  }
  return -1;
}

// Merchant Watch at the real 1x clock: physical departure, live journey
// frames no faster than the e-paper interval, the World reference map and
// its port browsing, then pause and captain orders holding the ship still.
static bool schedule_x4_watch_test() {
  const char* scale = getenv("SH_TIME_SCALE");
  if (scale && strcmp(scale, "1")) return false;
  save_reset(); sim_new_campaign(4271);
  g_campaign.tutorialDismissed = 1; g_ui.title = 0;
  render_host_reset_map_presentation();
  if (!input_test_focus_command(X4_CMD_SHIP_0)) return false;
  render_request_full_refresh(); render_frame();
  keyCount = 0; x4Watch = {}; x4Watch.started = millis();
  watch_action(X4_ACT_OK);  // ship overview
  x4TestCheckAt = millis() + 180000;
  ::printf("x4 watch: merchant voyage, normal1x clock; 18s Watch,8s map browse,7s pause,7s orders; bounded180s\n");
  fflush(stdout);
  return true;
}

static void watch_snapshot(uint32_t now) {
  x4Watch.phaseAt = now; x4Watch.pushes = render_host_push_count();
  x4Watch.pushAt = render_host_last_push_ms();
  x4Watch.world = g_campaign.worldMinutes; x4Watch.elapsed = g_ships[0].legElapsedMinutes;
  render_host_map_position(0, &x4Watch.x, &x4Watch.y);
  watch_ship_point(watch_journey(g_ships[0]).fraction, &x4Watch.pointX, &x4Watch.pointY);
  x4Watch.focusX = g_ui.mapFocusX; x4Watch.focusY = g_ui.mapFocusY;
}

static int tick_x4_watch_test(uint32_t now, uint32_t presses) {
  auto fail = [&](const char* reason) {
    ::printf("x4 watch failure: %s / phase %u / keys %lu/%d / card %u page %u / scene %u / state %u / watch %d / paused %u\n",
             reason, x4Watch.phase, (unsigned long)presses, keyCount, g_ui.card, g_ui.page, g_ui.scene,
             g_ships[0].state, x4_watch_active(), g_campaign.paused);
    return -1;
  };
  if (g_campaign.timeScale != 1 || now - x4Watch.started > 180000) return fail("clock or timeout");
  if (presses < (uint32_t)keyCount) return 0;  // every phase acts on a serviced screen
  if (x4Watch.phase == 0) {
    MerchantSailQuote quote{};
    int step = x4_drive_departure(NO_PORT, &x4Watch.downs, &quote);
    if (step < 0) return fail("merchant departure");
    if (!step) return 0;
    if (x4_column_slot(X4_CMD_ZOOM) >= 0 || x4_column_slot(X4_CMD_WATCH_MAP) < 0 ||
        x4_column_slot(X4_CMD_WATCH_READY) < 0) return fail("Watch actions");
    g_ships[0].incidentArmed = 0; // no random event can interrupt the bounded movement sample
    x4Watch.phase = 1; watch_snapshot(now); watch_capture("departed", now); return 0;
  }
  if (x4Watch.phase == 1) {
    if (g_ui.noticeCode != SIM_OK || render_host_push_count() == x4Watch.pushes) return 0;
    x4Watch.phase = 2; watch_snapshot(now); watch_capture("live-start", now); return 0;
  }
  if (x4Watch.phase == 2) {
    if (!voyage_watch_screen()) return fail("Watch left without input");
    if (render_host_push_count() != x4Watch.pushes) {
      if (render_host_last_push_ms() - x4Watch.pushAt < 3000) return fail("update faster than3s");
      int x = 0, y = 0;
      watch_ship_point(watch_journey(g_ships[0]).fraction, &x, &y);
      if (x != x4Watch.pointX || y != x4Watch.pointY) ++x4Watch.movedFrames;
      ++x4Watch.movingFrames; x4Watch.pointX = x; x4Watch.pointY = y;
      x4Watch.pushes = render_host_push_count(); x4Watch.pushAt = render_host_last_push_ms();
      watch_capture("live", now);
    }
    if (now - x4Watch.phaseAt < 18000) return 0;
    int clockDifference = (int)((g_campaign.worldMinutes - x4Watch.world) / SIM_WORLD_MINUTES_PER_REAL_SECOND) -
      (int)((now - x4Watch.phaseAt) / 1000u);
    if (abs(clockDifference) > 2 || x4Watch.movingFrames < 3 || x4Watch.movedFrames < 2)
      return fail("normal-clock journey motion");
    if (!input_test_focus_command(X4_CMD_WATCH_MAP)) return fail("World map focus");
    watch_action(X4_ACT_OK); x4Watch.phase = 3; return 0;
  }
  if (x4Watch.phase == 3) {
    if (g_ui.scene != SCENE_WORLD || x4_watch_active() || g_ui.card != CARD_NONE ||
        render_map_following_ship()) return fail("World map is a reference view");
    x4Watch.port = g_ui.selectedPort;
    if (!input_test_focus_command(X4_CMD_PAGE_NEXT)) return fail("port browse focus");
    watch_action(X4_ACT_OK); x4Watch.phase = 4; return 0;
  }
  if (x4Watch.phase == 4) {
    if (g_ui.selectedPort == x4Watch.port) return fail("PORT > browses ports");
    x4Watch.port = g_ui.selectedPort;
    x4Watch.phase = 5; watch_snapshot(now); watch_capture("browse", now); return 0;
  }
  if (x4Watch.phase == 5) {
    if (render_map_following_ship() || g_ui.selectedPort != x4Watch.port ||
        g_ui.mapFocusX != x4Watch.focusX || g_ui.mapFocusY != x4Watch.focusY) return fail("browse snapped back");
    if (now - x4Watch.phaseAt < 8000) return 0;
    watch_action(X4_ACT_BACK); x4Watch.phase = 6; return 0;
  }
  if (x4Watch.phase == 6) {
    if (!voyage_watch_screen() || g_ui.selectedShip != 0) return fail("Back from the map resumes Watch");
    watch_capture("resumed", now);
    // Watch Back -> Fleet, Fleet Back -> Settings, Pause, Back -> Fleet.
    watch_action(X4_ACT_BACK); watch_action(X4_ACT_BACK); watch_action(X4_ACT_DOWN);
    watch_action(X4_ACT_OK); watch_action(X4_ACT_BACK);
    x4Watch.phase = 7; return 0;
  }
  if (x4Watch.phase == 7) {
    if (!g_campaign.paused || g_ui.card != CARD_NONE || g_ui.scene != SCENE_FLEET) return fail("pause controls");
    if (!input_test_focus_command(X4_CMD_SHIP_0)) return fail("sailing ship focus");
    watch_action(X4_ACT_OK); x4Watch.phase = 8; return 0;
  }
  if (x4Watch.phase == 8) {
    if (!voyage_watch_screen() || !g_campaign.paused) return fail("paused ship reopens Watch");
    x4Watch.phase = 9; watch_snapshot(now); watch_capture("paused", now); return 0;
  }
  if (x4Watch.phase == 9 || x4Watch.phase == 11) {
    int x = 0, y = 0, px = 0, py = 0; render_host_map_position(0, &x, &y);
    watch_ship_point(watch_journey(g_ships[0]).fraction, &px, &py);
    if (g_ships[0].legElapsedMinutes != x4Watch.elapsed || x != x4Watch.x || y != x4Watch.y ||
        px != x4Watch.pointX || py != x4Watch.pointY ||
        render_host_push_count() != x4Watch.pushes) return fail("paused/orders must stay still");
    if (now - x4Watch.phaseAt < 7000) return 0;
    if (x4Watch.phase == 9) {
      // Watch Back -> Fleet, Fleet Back -> Settings, Resume closes Settings.
      watch_action(X4_ACT_BACK); watch_action(X4_ACT_BACK); watch_action(X4_ACT_OK);
      x4Watch.phase = 10; return 0;
    }
    // An incident delay changes the schedule, not the ship's charted position.
    // Only this presentation fixture extends the leg; loop() is never advanced manually.
    g_ships[0].legTotalMinutes += 600;
    render_host_map_position(0, &x, &y);
    if (x != x4Watch.x || y != x4Watch.y) return fail("incident delay moved ship backward");
    g_incident = {}; g_ships[0].state = SHIP_SAILING;
    g_ui.dirty = 1; render_frame(); watch_capture("delay-resumed", millis());
    return 1;
  }
  if (x4Watch.phase == 10) {
    if (g_campaign.paused || g_ui.card != CARD_NONE) return fail("resume controls");
    if (g_ui.scene == SCENE_FLEET) {
      if (!input_test_focus_command(X4_CMD_SHIP_0)) return fail("resumed ship focus");
      watch_action(X4_ACT_OK); return 0;
    }
    if (!voyage_watch_screen()) return fail("resumed Watch");
    g_ships[0].state = SHIP_AWAITING_DECISION;
    g_incident = Incident{1,0,INCIDENT_STORM,RISK_EVEN};
    g_ui.dirty = 1; render_frame();
    x4Watch.phase = 11; watch_snapshot(millis()); watch_capture("orders", millis()); return 0;
  }
  return 0;
}

// The first merchant voyage at the actual 1x clock. Only setup creates the
// company; every player action below is a debounced physical press: the
// First Voyage tour, a free parcel for Gullhaven, Sail, and Open ship on arrival.
struct X4DeliveryTest {
  uint32_t deadline, reportAt, sailedAt, arrivedAt;
  int32_t sailCash, arrivalCash;
  PackageOffer parcel;
  MerchantSailQuote quote;
  uint8_t downs, pages;
  bool sailed, arrived, updated, opened;
};
static X4DeliveryTest x4Delivery{};

static int x4_grain_aboard() {
  int n = 0;
  for (int c = 0; c < g_ships[0].cargoCount; ++c)
    if (g_ships[0].cargo[c].kind == CARGO_MARKET && g_ships[0].cargo[c].good == GOOD_GRAIN)
      n += g_ships[0].cargo[c].quantity;
  return n;
}

static bool schedule_x4_delivery_test() {
  if (const char* scale = getenv("SH_TIME_SCALE")) if (strcmp(scale, "1")) return false;
  save_reset(); sim_new_campaign(42); render_frame();
  if (g_campaign.economyVersion != 2 || x4_grain_aboard() != 2) return false;
  x4Delivery = {};
  schedule_key(millis() + 600, x4_button_for(X4_ACT_OK));  // title
  x4TestCheckAt = millis() + 180000;
  x4Delivery.deadline = millis() + 300000;  // replaced by the quoted duration at Sail
  ::printf("x4 delivery: new company, First Voyage tour, free Gullhaven parcel, Sail at 1x\n");
  return true;
}

static int tick_x4_delivery_test(uint32_t now, uint32_t serviced) {
  X4DeliveryTest& d = x4Delivery;
  const Ship& ship = g_ships[0];
  auto fail = [&](const char* reason) {
    ::printf("x4 delivery failure: %s (card %u page %u scene %u state %u coins %ld grain %d used %d)\n",
             reason, g_ui.card, g_ui.page, g_ui.scene, ship.state, (long)g_campaign.coins,
             x4_grain_aboard(), sim_ship_used_capacity(ship));
    return -1;
  };
  if (g_campaign.timeScale != 1 || g_campaign.paused || board_x4_asleep() ||
      now >= d.deadline || ship.state == SHIP_DISABLED || g_incident.active) return fail("clock, sleep or incident");
  if (serviced < (uint32_t)keyCount) return now >= x4TestCheckAt ? fail("press not serviced") : 0;
  if (g_ui.title) return 0;
  if (!d.sailed) {
    if (d.downs > 12 || d.pages > 8) return fail("focus did not reach its target");
    if (g_ui.card == CARD_TUTORIAL) { watch_action(X4_ACT_OK); return 0; }
    if (g_ui.card == CARD_NONE && g_ui.scene == SCENE_FLEET) {
      if (g_campaign.coins != 500 || !g_campaign.tutorialDismissed) return fail("tour finished with a transaction");
      if (!input_test_focus_command(X4_CMD_SHIP_0)) return fail("ship focus");
      watch_action(X4_ACT_OK); return 0;
    }
    if (!d.parcel.id) {
      if (g_ui.card == CARD_SHIP) {
        if (!input_test_focus_row(0)) return fail("Trade focus");
        watch_action(X4_ACT_OK); return 0;
      }
      if (g_ui.card == CARD_TRADE) { watch_action(X4_ACT_PAGE_PREV); return 0; }  // Buy -> Parcels
      if (g_ui.card == CARD_PACKAGES && x4_merchant_editing()) {
        PackageOffer review = x4_merchant_selected_package();
        if (review.destination != 1 || review.reward == 0) return fail("parcel review");
        d.parcel = review; d.sailCash = g_campaign.coins;
        watch_action(X4_ACT_OK);    // Take free
        watch_action(X4_ACT_BACK);  // back to the ship
        return 0;
      }
      if (g_ui.card == CARD_PACKAGES) {
        for (int row = 0; row < 2; ++row)
          if (x4_merchant_package(row).id && x4_merchant_package(row).destination == 1) {
            if (!input_test_focus_row(row)) return fail("parcel focus");
            watch_action(X4_ACT_OK); return 0;
          }
        ++d.pages;
        if (!input_test_focus_row(6)) return fail("no Gullhaven parcel");
        watch_action(X4_ACT_OK); return 0;
      }
      return fail("unexpected screen before the parcel");
    }
    if (g_ui.card == CARD_PACKAGES || g_ui.card == CARD_TRADE) return fail("parcel not accepted");
    if (g_campaign.coins != d.sailCash || sim_ship_used_capacity(ship) != 3) return fail("parcel must be free and take one slot");
    int step = x4_drive_departure(1, &d.downs, &d.quote);
    if (step < 0) return fail("merchant departure");
    if (!step) return 0;
    if (d.quote.packagePayout != d.parcel.reward || g_campaign.coins != d.sailCash || x4_grain_aboard() != 2)
      return fail("Sail is free and carries the parcel and owned goods");
    d.sailed = true; d.sailedAt = d.reportAt = now;
    d.deadline = now + x4_real_seconds(d.quote.minutes, 1) * 1000u + 60000u;
    ::printf("x4 delivery: sailed for Gullhaven, %lu world minutes = %lu real seconds at 1x, parcel %u coins\n",
             (unsigned long)d.quote.minutes, (unsigned long)x4_real_seconds(d.quote.minutes, 1), d.parcel.reward);
    fflush(stdout);
    return 0;
  }
  if (!d.arrived) {
    if (ship.state == SHIP_SAILING) {
      if (g_campaign.coins != d.sailCash) return fail("nothing pays before arrival");
      if (now >= d.reportAt) {
        ::printf("x4 delivery: normal1x leg %u/%u world minutes, coins %ld\n",
          ship.legElapsedMinutes, ship.legTotalMinutes, (long)g_campaign.coins);
        fflush(stdout); d.reportAt = now + 30000;
      }
      return 0;
    }
    uint32_t expected = x4_real_seconds(d.quote.minutes, 1), actual = (now - d.sailedAt + 500) / 1000;
    if (ship.state != SHIP_ARRIVED || ship.currentPort != 1 || !voyage_watch_screen())
      return fail("arrival stays on Watch at Gullhaven");
    if (actual + 3 < expected || actual > expected + 3) return fail("voyage did not run at the 1x clock");
    if (g_campaign.coins != d.sailCash + d.parcel.reward || x4_grain_aboard() != 2 ||
        sim_ship_used_capacity(ship) != 2) return fail("only the parcel pays; merchandise stays aboard");
    d.arrived = true; d.arrivedAt = now; d.arrivalCash = g_campaign.coins;
    if (!input_test_focus_command(X4_CMD_WATCH_READY)) return fail("Open ship focus");
    watch_action(X4_ACT_OK); return 0;
  }
  // Two idle minutes of watching: per docs/X4_RETURN_FLOW.md the next press
  // opens Company update and is consumed. Its ship row opens the arrived boat.
  if (x4_return_report()) {
    if (d.updated || g_ui.scene != SCENE_COMPANY || g_campaign.coins != d.arrivalCash ||
        !input_test_focus_command(X4_CMD_SHIP_0)) return fail("update consumes the idle press");
    d.updated = true; watch_action(X4_ACT_OK); return 0;
  }
  if (!d.updated || g_ui.card != CARD_SHIP || g_ui.selectedShip != 0 || g_campaign.coins != d.arrivalCash ||
      ship.merchant.receiptUnread || x4_grain_aboard() != 2) return fail("Open ship shows the arrived boat once");
  if (now - d.arrivedAt < 2000) return 0;  // a further frame never repays the parcel
  d.opened = true;
  return g_campaign.coins == d.arrivalCash ? 1 : fail("parcel paid twice");
}

// Synthetic cartography probe, deliberately separate from the normal-clock
// watch journey and its screenshots. No production simulation tick runs here.
static bool run_x4_follow_boundary_test() {
  int recenters = 0, seamCrossings = 0;
  for (int fixture = 0; fixture < 2; ++fixture) {
    sim_new_campaign(0xF0110u + fixture);
    g_campaign.stageSeen = 4; g_campaign.legacyFeatureMask = 0xffff; g_campaign.tutorialDismissed = 1;
    g_ui.title = 0; g_ui.scene = SCENE_WORLD; g_ui.card = CARD_NONE;
    g_ui.mapZoom = 4; g_ui.selectedShip = 0;
    Ship& ship = g_ships[0];
    ship.state = SHIP_SAILING; ship.incidentArmed = 0;
    ship.currentPort = ship.fromPort = fixture ? 20 : 0;
    g_ui.selectedPort = ship.toPort = fixture ? 23 : 3;
    ship.legTotalMinutes = 10000; ship.legElapsedMinutes = 0;
    render_host_reset_map_presentation(); render_map_follow_selected_ship();
    render_request_full_refresh(); render_frame();
    int fixtureRecenters = 0;
    for (int elapsed = 100; elapsed <= 10000; elapsed += 100) {
      ship.legElapsedMinutes = (uint16_t)elapsed;
      int x = 0, y = 0;
      render_host_map_position(0, &x, &y);
      if (x >= MAP_X + MAP_DRAW_W / 5 && x < MAP_X + MAP_DRAW_W * 4 / 5 &&
          y >= MAP_Y + MAP_DRAW_H / 5 && y < MAP_Y + MAP_DRAW_H * 4 / 5) continue;
      int previousX = g_ui.mapFocusX, previousY = g_ui.mapFocusY;
      render_request_full_refresh(); render_frame(); // a due frame, not elapsed time
      if (!render_host_map_position(0, &x, &y) ||
          (g_ui.mapFocusX == previousX && g_ui.mapFocusY == previousY)) return false;
      if (abs((int)g_ui.mapFocusX - previousX) > MAP_W / 2) ++seamCrossings;
      ++recenters; ++fixtureRecenters;
      if (fixtureRecenters > 12) return false; // bounded camera work
      previousX = g_ui.mapFocusX; previousY = g_ui.mapFocusY;
      render_request_full_refresh(); render_frame();
      if (g_ui.mapFocusX != previousX || g_ui.mapFocusY != previousY) return false;
    }
    if (!fixtureRecenters) return false;
  }
  ::printf("x4 follow boundary: %d recenterings, %d seam crossings; stationary camera inside dead zone\n",
           recenters, seamCrossings);
  return seamCrossings > 0;
}

// Direct chart probes do not advance the clock or replace normal voyage footage.
static bool run_x4_route_geometry_test() {
  sim_new_campaign(0xA47C0u);
  g_ui.title = 0; g_ui.scene = SCENE_WORLD; g_ui.card = CARD_NONE;
  render_host_reset_map_presentation();
  const Campaign campaignBefore = g_campaign;
  const uint32_t pushCountBefore = render_host_push_count();
  const int worldWidth = MAP_DRAW_W * X4_ROUTE_SCALE;
  const int worldHeight = MAP_DRAW_H * X4_ROUTE_SCALE;
  const size_t pointCount = sizeof(X4_ROUTE_POINTS) / sizeof(X4_ROUTE_POINTS[0]);
  const size_t distanceCount = sizeof(X4_ROUTE_DISTANCES) / sizeof(X4_ROUTE_DISTANCES[0]);
  const size_t pathCount = sizeof(X4_ROUTE_PATHS) / sizeof(X4_ROUTE_PATHS[0]);
  const uint16_t fractions[] = {0, 1, 1250, 2500, 5000, 7500, 8750, 9999, 10000};
  const uint8_t zooms[] = {1, 2, 4};
  const uint8_t cameras[][2] = {{0, 0}, {105, 54}, {215, 107}};
  int projected = 0, raster = 0;
  auto fail = [](const char* reason, int lane, int fraction, int zoom, int camera) {
    ::printf("x4 route geometry failure: %s / lane %d fraction %d zoom %d camera %d\n",
             reason, lane, fraction, zoom, camera);
    return false;
  };
  auto wrapped_error = [](int32_t a, int32_t b) {
    int32_t d = abs(a - b);
    return min(d, MAP_DRAW_W * X4_ROUTE_SCALE - d);
  };
  if (pathCount < (size_t)LANE_COUNT || pathCount > MAX_LANES || pointCount != distanceCount)
    return fail("generated route table size", -1, 0, 0, 0);
  for (size_t lane = LANE_COUNT; lane < pathCount; ++lane)
    if (X4_ROUTE_PATHS[lane].offset || X4_ROUTE_PATHS[lane].count)
      return fail("unused generated lane is not empty", (int)lane, 0, 0, 0);
  for (int lane = 0; lane < LANE_COUNT; ++lane) {
    const SeaLaneDef& definition = LANE_DEFS[lane];
    const X4RouteSpan& span = X4_ROUTE_PATHS[lane];
    if (span.count < 2 || (size_t)span.offset + span.count > pointCount ||
        X4_ROUTE_DISTANCES[span.offset] != 0)
      return fail("generated route span", lane, 0, 0, 0);
    const X4RoutePoint& start = X4_ROUTE_POINTS[span.offset];
    const X4RoutePoint& end = X4_ROUTE_POINTS[span.offset + span.count - 1];
    if (start.x != X4_ROUTE_PORTS[definition.a].x || start.y != X4_ROUTE_PORTS[definition.a].y ||
        end.x != X4_ROUTE_PORTS[definition.b].x || end.y != X4_ROUTE_PORTS[definition.b].y)
      return fail("path endpoints differ from harbor anchors", lane, 0, 0, 0);
    for (int point = 0; point < span.count; ++point) {
      size_t at = span.offset + point;
      if (X4_ROUTE_POINTS[at].x >= worldWidth || X4_ROUTE_POINTS[at].y >= worldHeight ||
          (point && X4_ROUTE_DISTANCES[at] <= X4_ROUTE_DISTANCES[at - 1]))
        return fail("path point bounds or arc-distance order", lane, point, 0, 0);
    }
    for (int reverse = 0; reverse < 2; ++reverse) {
      const uint8_t from = reverse ? definition.b : definition.a;
      const uint8_t to = reverse ? definition.a : definition.b;
      for (uint16_t fraction : fractions) {
        int32_t wx = 0, wy = 0, oppositeX = 0, oppositeY = 0;
        int sx = 0, sy = 0;
        g_ui.mapZoom = 1; g_ui.mapFocusX = 0; g_ui.mapFocusY = 0;
        if (!render_host_route_point((uint8_t)lane, from, fraction, &wx, &wy, &sx, &sy) ||
            !render_host_route_point((uint8_t)lane, to, (uint16_t)(10000 - fraction),
                                     &oppositeX, &oppositeY, &sx, &sy))
          return fail("valid route rejected", lane, fraction, 1, 0);
        if (wx < 0 || wx >= worldWidth || wy < 0 || wy >= worldHeight ||
            wrapped_error(wx, oppositeX) > 1 || abs(wy - oppositeY) > 1)
          return fail("forward/reverse interpolation mismatch", lane, fraction, 1, 0);
        if (fraction == 0 || fraction == 10000) {
          const X4RoutePoint& anchor = X4_ROUTE_PORTS[fraction ? to : from];
          if (wx != anchor.x || wy != anchor.y)
            return fail("interpolated endpoint misses harbor", lane, fraction, 1, 0);
        }
        Ship& ship = g_ships[0];
        ship.state = SHIP_SAILING; ship.currentPort = ship.fromPort = from; ship.toPort = to;
        ship.legTotalMinutes = 10000; ship.legElapsedMinutes = fraction;
        const Ship shipBefore = ship;
        for (uint8_t zoom : zooms) {
          for (int camera = 0; camera < 3; ++camera) {
            g_ui.mapZoom = zoom;
            g_ui.mapFocusX = cameras[camera][0]; g_ui.mapFocusY = cameras[camera][1];
            int32_t projectedX = 0, projectedY = 0;
            if (!render_host_route_point((uint8_t)lane, from, fraction,
                                         &projectedX, &projectedY, &sx, &sy) ||
                projectedX != wx || projectedY != wy)
              return fail("camera or zoom changes world progress", lane, fraction, zoom, camera);
            const int sourceWidth = MAP_DRAW_W / zoom, sourceHeight = MAP_DRAW_H / zoom;
            const int centerX = zoom == 1 ? MAP_DRAW_W / 2 :
              (int)g_ui.mapFocusX * MAP_DRAW_W / MAP_W;
            const int top = zoom == 1 ? 0 : max(0, min(MAP_DRAW_H - sourceHeight,
              (int)g_ui.mapFocusY * MAP_DRAW_H / MAP_H - sourceHeight / 2));
            int dx = wx - centerX * X4_ROUTE_SCALE;
            if (dx > worldWidth / 2) dx -= worldWidth;
            if (dx < -worldWidth / 2) dx += worldWidth;
            const int expectedX = MAP_X + sourceWidth * zoom / 2 + dx * zoom / X4_ROUTE_SCALE;
            const int expectedY = MAP_Y + (wy - top * X4_ROUTE_SCALE) * zoom / X4_ROUTE_SCALE;
            int screenError = abs(sx - expectedX);
            screenError = min(screenError, abs(screenError - MAP_DRAW_W * zoom));
            if (screenError > 1 || abs(sy - expectedY) > 1)
              return fail("projection disagrees with chart crop", lane, fraction, zoom, camera);
            int shipX = 0, shipY = 0;
            render_host_map_position(0, &shipX, &shipY);
            if (shipX != sx || shipY != sy || memcmp(&ship, &shipBefore, sizeof(ship)))
              return fail("ship diverges from path or probe mutates voyage", lane, fraction, zoom, camera);
            ++projected;
          }
        }
      }
    }
    int32_t wx = 0, wy = 0; int sx = 0, sy = 0;
    uint8_t invalidFrom = 0;
    while (invalidFrom == definition.a || invalidFrom == definition.b) ++invalidFrom;
    if (render_host_route_point((uint8_t)lane, invalidFrom, 5000, &wx, &wy, &sx, &sy) ||
        render_host_route_progress_ink((uint8_t)lane, invalidFrom, 5000))
      return fail("unrelated departure port accepted", lane, 5000, 0, 0);
  }
  int32_t wx = 0, wy = 0; int sx = 0, sy = 0;
  if (render_host_route_point((uint8_t)LANE_COUNT, 0, 0, &wx, &wy, &sx, &sy) ||
      render_host_route_progress_ink((uint8_t)LANE_COUNT, 0, 0) ||
      render_host_route_point(0, LANE_DEFS[0].a, 10001, &wx, &wy, &sx, &sy) ||
      render_host_route_progress_ink(0, LANE_DEFS[0].a, 10001))
    return fail("invalid lane accepted", LANE_COUNT, 0, 0, 0);

  // Draw the actual progress stroke alone so no ship marker can conceal a gap.
  const uint8_t detailedPairs[][2] = {{0, 1}, {1, 4}, {9, 12}, {20, 23}};
  const uint16_t rasterFractions[] = {2500, 5000, 7500};
  for (int lane = 0; lane < LANE_COUNT; ++lane) {
    bool detailed = false;
    for (const auto& ports : detailedPairs)
      detailed |= content_lane_between(ports[0], ports[1]) == lane;
    for (int reverse = 0; reverse < 2; ++reverse) {
      uint8_t from = reverse ? LANE_DEFS[lane].b : LANE_DEFS[lane].a;
      for (uint16_t fraction : rasterFractions) {
        if (fraction != 5000 && !detailed) continue;
        render_host_route_point((uint8_t)lane, from, fraction, &wx, &wy, &sx, &sy);
        g_ui.mapFocusX = (uint8_t)(wx * MAP_W / worldWidth);
        g_ui.mapFocusY = (uint8_t)(wy * MAP_H / worldHeight);
        for (uint8_t zoom : zooms) {
          g_ui.mapZoom = zoom;
          if (!render_host_route_progress_ink((uint8_t)lane, from, fraction))
            return fail("active progress stroke misses its ship endpoint", lane, fraction, zoom, 0);
          ++raster;
        }
      }
    }
  }
  if (memcmp(&campaignBefore, &g_campaign, sizeof(g_campaign)) ||
      render_host_push_count() != pushCountBefore)
    return fail("geometry probe changes campaign or pushes screen", -1, 0, 0, 0);
  ::printf("x4 route geometry: %d lanes, %d projected ship/path probes, %d progress raster probes; "
           "forward/reverse anchors, zoom/camera invariance, invalid inputs and clock purity verified\n",
           LANE_COUNT, projected, raster);
  return true;
}

static bool gallery_wants_frame(const char* name) {
  const char* filter = getenv("SH_GALLERY_FILTER");
  if (!filter || !*filter) return true;
  while (*filter) {
    const char* end = strchr(filter, ',');
    if (!end) end = filter + strlen(filter);
    const char* begin = filter;
    while (begin < end && isspace((unsigned char)*begin)) ++begin;
    const char* trimmedEnd = end;
    while (trimmedEnd > begin && isspace((unsigned char)trimmedEnd[-1])) --trimmedEnd;
    size_t length = (size_t)(trimmedEnd - begin);
    if (length) {
      size_t nameLength = strlen(name);
      for (size_t offset = 0; offset + length <= nameLength; ++offset)
        if (!strncmp(name + offset, begin, length)) return true;
    }
    if (!*end) break;
    filter = end + 1;
  }
  return false;
}

static int galleryWritten = 0;
static void dump_gallery_frame(const char* dir, int index, const char* name) {
  if (!gallery_wants_frame(name)) return;
  char path[512], nativeDir[512];
  snprintf(path, sizeof(path), "%s/%02d_%s.ppm", dir, index, name);
  dump_ppm(path);
  snprintf(nativeDir, sizeof(nativeDir), "%s/native", dir);
  ::mkdir(nativeDir, 0755);
  snprintf(path, sizeof(path), "%s/%02d_%s.ppm", nativeDir, index, name);
  dump_ppm_native(path);
  ++galleryWritten;
}

static void seed_demo() {
  g_ui.title = 0;
  g_campaign.coins = 6400;
  g_campaign.rank = 3;
  g_campaign.stageSeen = STAGE_COUNT - 1; g_campaign.legacyFeatureMask = 0xffff;
  g_campaign.regionMask = 0x0F;
  for (int p = 0; p < 16; ++p) g_ports[p].unlocked = 1;
  for (int attempts = 0; attempts < 2 && g_campaign.shipCount < 3; ++attempts) {
    uint8_t cls = g_campaign.shipCount;
    g_ui.selectedPort = 0;
    g_ports[0].shipyardLevel = 3;
    g_ports[0].officeLevel = 3;
    sim_hire_captain((uint8_t)attempts);
    if (!sim_buy_ship(cls)) break;
  }
  g_ui.scene = SCENE_WORLD;
  g_ui.selectedShip = 0;
  g_ui.selectedPort = 4;
  sim_route_clear();
  sim_route_add(1);
  sim_dispatch(0);
  g_ui.selectedShip = 1;
  sim_route_clear();
  sim_route_add(3);
  sim_dispatch(1);
  g_ui.selectedShip = 2;
  g_ui.dirty = 1;
}

struct GalleryEntry { uint8_t scene, card, page; const char* name; };
static const GalleryEntry GALLERY[] = {
  { SCENE_PORT,  CARD_NONE,      0, "port" },
  { SCENE_PORT,  CARD_NONE,      0, "guided-port" },
  { SCENE_WORLD, CARD_NONE,      0, "world" },
  { SCENE_WORLD, CARD_NONE,      0, "ship-selected-map" },
  { SCENE_PORT,  CARD_JOBS,      0, "jobs" },
  { SCENE_PORT,  CARD_MARKET,    0, "market" },
  { SCENE_PORT,  CARD_MANIFEST,  0, "manifest" },
  { SCENE_WORLD, CARD_ROUTE,     0, "route" },
  { SCENE_WORLD, CARD_FLEET,     0, "fleet" },
  { SCENE_PORT,  CARD_LEDGER,    0, "ledger" },
  { SCENE_WORLD, CARD_INCIDENT,  0, "incident" },
  { SCENE_PORT,  CARD_SHIPYARD,  0, "shipyard" },
  { SCENE_PORT,  CARD_SHIPYARD,  1, "fittings" },
  { SCENE_PORT,  CARD_SHIPYARD,  2, "port-works" },
  { SCENE_PORT,  CARD_SHIPYARD,  2, "port-works-won" },  // the Charter pennant row after the win
  { SCENE_PORT,  CARD_CHARTER,   0, "charters" },
  { SCENE_WORLD, CARD_MENU,      0, "menu" },
  { SCENE_WORLD, CARD_PROGRESS,      0, "progress" },
  { SCENE_WORLD, CARD_HELP,      0, "help" },
  { SCENE_WORLD, CARD_INBOX,     0, "inbox" },
  { SCENE_WORLD, CARD_EVENT,     0, "event" },
  { SCENE_WORLD, CARD_CAPTAINS,  0, "captains" },
  { SCENE_WORLD, CARD_TUTORIAL,  0, "tutorial" },
  { SCENE_WORLD, CARD_ARRIVAL,   0, "arrival" },
  { SCENE_WORLD, CARD_ROUTE,     1, "route-preparation" },
  { SCENE_WORLD, CARD_ROUTE,     2, "route-preview" },
  { SCENE_WORLD, CARD_CAPTAINS,  0, "captain-detail" },
  { SCENE_WORLD, CARD_CAPTAINS,  0, "captain-recruitment" },
  { SCENE_WORLD, CARD_JOURNAL,   0, "journal" },
  { SCENE_WORLD, CARD_INBOX,     0, "inbox-full" },
  { SCENE_WORLD, CARD_NONE,      0, "world-zoom" },
  { SCENE_WORLD, CARD_NONE,      0, "world-zoom4" },
};

static void gallery_seed() {
  sim_new_campaign(0xC0FFEEu);
  render_host_reset_map_presentation();
  g_campaign.tutorialDismissed = 1;
  g_campaign.tutorialFlags = 0xFFu;
  g_campaign.tutorialStep = TUTORIAL_COUNT;
  g_ui.title = 0;
  g_campaign.coins = 6400;
  g_campaign.rank = 3;
  g_campaign.stageSeen = STAGE_COUNT - 1; g_campaign.legacyFeatureMask = 0xffff;
  g_campaign.regionMask = (1u << REGION_COUNT) - 1;
  g_campaign.chartMask = (1u << REGION_COUNT) - 1;
  for (int p = 0; p < MAX_PORTS; ++p) g_ports[p].unlocked = 1;
  g_ports[0].shipyardLevel = 3;
  g_ports[0].officeLevel = 3;
  g_ui.selectedPort = 0;
  g_ui.selectedShip = 0;
  for (int i = 0; i < MAX_CONTRACTS_PER_PORT; ++i) {
    if (sim_load_contract(0, 0, (uint8_t)i)) break;
  }
  sim_market_buy(0, GOOD_TEXTILES);
  sim_hire_captain(0);
  sim_buy_ship(1);
  sim_hire_captain(1);
  sim_buy_ship(2);
  g_campaign.coins = 6400;
  g_ui.selectedShip = 0;
  sim_route_clear();
  sim_route_add(1);
  sim_route_template_save(0);
  g_ships[0].routeTemplate.loadMode = ROUTE_LOAD_ENDPOINT;
  g_ships[0].routeTemplate.sellMode = ROUTE_SELL_PROFIT;
  g_ships[0].routeTemplate.buyGood = GOOD_TEXTILES;
  g_ships[0].routeTemplate.maxBuyPrice = content_market_price(0, GOOD_TEXTILES, true);
  attention_add(ATTENTION_ARRIVAL, 0, 0, false);
  attention_add(ATTENTION_EVENT, NO_SHIP, NO_PORT, false);
  g_campaign.worldEventType = EVENT_SHORTAGE;
  g_campaign.worldEventRegion = 0;
  g_campaign.worldEventGood = GOOD_TEA;
  g_campaign.worldEventDays = 4;
}

static void guided_gallery_seed() {
  sim_new_campaign(0xC0FFEEu);
  render_host_reset_map_presentation();
  g_campaign.tutorialFlags = (uint8_t)(1u << TUTORIAL_PORT);
  g_campaign.tutorialStep = TUTORIAL_CONTRACT;
  g_campaign.tutorialDismissed = 0;
  g_ui.title = 0;
  g_ui.scene = SCENE_PORT;
  g_ui.selectedPort = 0;
  g_ui.selectedShip = 0;
}

// Presentation fixtures are deterministic snapshots, separate from balance or
// save tests. Fill the real fixed pools so extreme names/counts are visible.
static void gallery_full_company() {
  gallery_seed();
  static const char* names[MAX_CAPTAINS] = {
    "Mara", "Ilyan", "Sabine", "Tomas", "Nadiya", "Corin", "Elena", "Bastien",
    "Anika", "Jules", "Rafi", "Maeve", "Odette", "Linnea", "Beatrice", "Christopher"
  };
  Ship base = g_ships[0];
  CaptainProfile captain = g_captains[base.captainId];
  g_campaign.shipCount = MAX_SHIPS;
  g_campaign.captainCount = MAX_CAPTAINS;
  for (int i = 0; i < MAX_CAPTAINS; ++i) {
    g_captains[i] = captain;
    CaptainProfile& c = g_captains[i];
    snprintf(c.name, sizeof(c.name), "%s", names[i]);
    c.active = 1; c.trait = (uint8_t)(i % 5); c.level = (uint8_t)(1 + i % 5);
    c.xp = (uint16_t)(7 + i * 13); c.loyalty = (uint8_t)(50 + i * 3);
    c.voyages = (uint16_t)(i * 19); c.deliveries = (uint16_t)(i * 41);
    c.rescues = (uint16_t)(i * 2); c.incidentsSurvived = (uint16_t)(i * 7);
    c.assignedShip = i < MAX_SHIPS ? (uint8_t)i : NO_SHIP;
    c.currentPort = (uint8_t)(i % MAX_PORTS);
    c.scarCount = (uint8_t)(i % 3);
    c.scars[0] = (uint8_t)(SCAR_STORM + i % 3);
    c.scars[1] = (uint8_t)(SCAR_STORM + (i + 1) % 3);
  }
  for (int i = 0; i < MAX_SHIPS; ++i) {
    g_ships[i] = base;
    Ship& s = g_ships[i];
    s.id = (uint32_t)(i + 1); s.captainId = (uint8_t)i; s.classId = (uint8_t)(i % SHIP_CLASS_COUNT);
    snprintf(s.name, sizeof(s.name), i == 0 ? "Wandering Star" : "Courier No. %d", i);
    s.currentPort = (uint8_t)(i % MAX_PORTS); s.state = SHIP_DOCKED;
    s.cargoCount = 0; s.routeCount = 0;
  }
  g_ships[1].state = SHIP_SAILING;
  g_ships[1].currentPort = g_ships[1].fromPort = 0; g_ships[1].toPort = 1;
  g_ships[1].route[0] = 1; g_ships[1].routeCount = 1;
  g_ships[1].legTotalMinutes = 7200; g_ships[1].legElapsedMinutes = 3600;
  g_ships[2].state = SHIP_AWAITING_DECISION;
  g_ships[2].currentPort = g_ships[2].fromPort = 0; g_ships[2].toPort = 2;
  g_ships[2].route[0] = 2; g_ships[2].routeCount = 1;
  g_ships[2].legTotalMinutes = 7200; g_ships[2].legElapsedMinutes = 2400;
  g_ships[3].state = SHIP_DISABLED; g_ships[3].condition = 18;
  g_incident = Incident{1, 2, INCIDENT_PIRATES, RISK_DANGEROUS};
  g_ui.selectedShip = 0; g_ui.selectedPort = 0;
}

static int run_x4_gallery_extras(const char* dir, int index) {
  auto emit = [&](const char* name) {
    int frameIndex = index++;  // filtering preserves the full gallery's names
    if (!gallery_wants_frame(name)) return;
    g_ui.dirty = 1;
    render_request_full_refresh();
    render_frame();
    dump_gallery_frame(dir, frameIndex, name);
  };
  char name[64];
  for (int stage = 0; stage < STAGE_COUNT; ++stage) {
    guided_gallery_seed();
    g_campaign.stageSeen = (uint8_t)stage; g_campaign.legacyFeatureMask = 0xffff;
    g_campaign.rank = (uint8_t)(stage < 2 ? 0 : stage < 4 ? 1 : stage - 2);
    if (stage) {
      g_campaign.tutorialDismissed = 1;
      g_campaign.tutorialFlags = 0xFF;
      if (stage == 1) g_campaign.tutorialFlags &= (uint8_t)~(1u << TUTORIAL_MARKET);
      g_campaign.deliveries = 1;
    }
    if (stage >= 3) {
      g_ships[1] = g_ships[0]; g_ships[1].id = 2; g_ships[1].captainId = 1;
      snprintf(g_ships[1].name, sizeof(g_ships[1].name), "Button");
      g_captains[1] = g_captains[0]; g_captains[1].assignedShip = 1;
      snprintf(g_captains[1].name, sizeof(g_captains[1].name), "Ilyan");
      g_campaign.shipCount = g_campaign.captainCount = 2;
    }
    snprintf(name, sizeof(name), "stage-%d-harbor", stage); emit(name);
    g_ui.card = CARD_PROGRESS;
    snprintf(name, sizeof(name), "stage-%d-progress", stage); emit(name);
  }
  for (int port = 0; port < MAX_PORTS; ++port) {
    gallery_seed();
    g_ui.scene = SCENE_PORT; g_ui.card = CARD_NONE;
    g_ui.selectedPort = g_ships[0].currentPort = (uint8_t)port;
    g_ships[0].classId = (uint8_t)(port % SHIP_CLASS_COUNT);
    g_ports[port].officeLevel = (uint8_t)(port % 4);
    g_ports[port].warehouseLevel = (uint8_t)((port + 1) % 4);
    g_ports[port].shipyardLevel = (uint8_t)((port + 2) % 4);
    snprintf(name, sizeof(name), "harbor-r%u-port%02d", PORT_DEFS[port].region, port); emit(name);
  }
  for (int kind = INCIDENT_PIRATES; kind <= INCIDENT_TRADE; ++kind) {
    gallery_seed();
    g_ui.selectedShip = 0;
    sim_dispatch(0);
    g_ships[0].state = SHIP_AWAITING_DECISION;
    g_ships[1].state = SHIP_AWAITING_DECISION;
    g_ships[1].fromPort = g_ships[1].currentPort = 0; g_ships[1].toPort = 2;
    g_ships[1].legTotalMinutes = 7200; g_ships[1].legElapsedMinutes = 1800;
    g_incident = Incident{1, 0, (uint8_t)kind, RISK_EVEN};
    g_incidentQueueCount = 1;
    g_incidentQueue[0] = Incident{1, 1, (uint8_t)((kind + 1) % 8), RISK_DANGEROUS};
    g_ui.card = CARD_INCIDENT;
    snprintf(name, sizeof(name), "incident-kind%d", kind); emit(name);
    sim_resolve_incident(0);
    g_ui.card = CARD_OUTCOME;
    snprintf(name, sizeof(name), "outcome-kind%d-queued", kind); emit(name);
  }
  for (int captain = 0; captain < MAX_CAPTAINS; ++captain) {
    gallery_full_company();
    g_ui.card = CARD_CAPTAINS; g_ui.captainView = 1; g_ui.selectedCaptain = (uint8_t)captain;
    snprintf(name, sizeof(name), "captain-dossier%02d", captain); emit(name);
  }
  for (int page = 0; page < (MAX_CAPTAINS + CARD_VISIBLE_ROWS - 1) / CARD_VISIBLE_ROWS; ++page) {
    gallery_full_company();
    g_ui.card = CARD_CAPTAINS; g_ui.captainView = 0; g_ui.page = (uint8_t)page;
    snprintf(name, sizeof(name), "captain-full-roster-page%d", page); emit(name);
  }
  for (int page = 0; page < (MAX_SHIPS + CARD_VISIBLE_ROWS - 1) / CARD_VISIBLE_ROWS; ++page) {
    gallery_full_company();
    g_ui.card = CARD_FLEET; g_ui.page = (uint8_t)page;
    snprintf(name, sizeof(name), "fleet-full-page%d", page); emit(name);
  }
  for (int good = GOOD_MEDICINES; good <= GOOD_PORCELAIN; ++good) {
    gallery_seed();
    g_ui.card = CARD_MARKET; g_ui.selectedGood = (uint8_t)good;
    snprintf(name, sizeof(name), "market-good%d", good); emit(name);
  }
  gallery_seed();
  sim_route_clear();
  for (int leg = 0; leg < MAX_ROUTE_LEGS; ++leg) sim_route_add((uint8_t)(leg % 2 ? 0 : 1));
  g_ui.card = CARD_ROUTE; emit("route-eight-legs");
  gallery_full_company();
  g_ui.card = CARD_SHIPYARD; g_ui.page = 2; g_ui.selectedShip = 3;
  g_ui.selectedPort = g_ships[3].currentPort;
  g_campaign.coins = 0; g_campaign.recoveryDebt = 4321;
  emit("disabled-ship-no-funds");
  g_ui.card = CARD_MARKET; g_ui.selectedGood = GOOD_PORCELAIN;
  emit("market-disabled-no-funds");
  gallery_full_company();
  g_ui.card = CARD_LEDGER;
  g_campaign.coins = 999999; g_campaign.contractRevenue = 1234567;
  g_campaign.marketProfit = -98765; g_campaign.recoveryDebt = 12345;
  emit("ledger-large-balances");
  gallery_seed();
  g_campaign.won = 1; g_campaign.rank = 5;
  g_campaign.visitedMask = (1u << REGION_COUNT) - 1;
  g_ui.card = CARD_ARRIVAL; g_ui.arrivalSummaryVisible = 1;
  g_ui.arrivalSummaryWon = 1; g_ui.arrivalSummaryShip = 0; g_ui.arrivalSummaryPort = 0;
  g_ui.arrivalSummaryDelivered = 12; g_ui.arrivalSummaryCoins = 1837;
  emit("grand-charter-homecoming");
  g_ui.card = CARD_PROGRESS; emit("grand-charter-progress");
  g_ui.card = CARD_CHARTER; emit("grand-charter-stamps");
  return index;
}

static int run_x4_consecutive_arrival_gallery(const char* dir, int index) {
  const char* names[] = { "arrival-consecutive-first-open", "arrival-consecutive-second-arrives",
                         "arrival-consecutive-second-open", "arrival-consecutive-first-reopened" };
  bool wanted = false;
  for (const char* name : names) wanted |= gallery_wants_frame(name);
  if (!wanted) return index + 4;
  sim_new_campaign(154);
  g_campaign.stageSeen = 3; g_campaign.legacyFeatureMask = 0xffff; g_campaign.tutorialDismissed = 1;
  g_campaign.coins = 10000; g_campaign.shipCount = 2;
  g_ships[1] = g_ships[0]; g_ships[1].id = 2; g_ships[1].captainId = NO_SHIP;
  snprintf(g_ships[1].name, sizeof(g_ships[1].name), "Second Arrival");
  memset(g_contracts, 0, sizeof(g_contracts));
  for (int i = 0; i < 2; ++i) {
    uint8_t destination = (uint8_t)(i + 1);
    Contract& contract = g_contracts[0][i];
    contract.id = g_campaign.nextContractId++;
    contract.origin = 0; contract.destination = destination;
    contract.good = GOOD_GRAIN; contract.quantity = (uint8_t)(i + 2);
    contract.kind = JOB_FREIGHT; contract.projectRegion = 0xFF;
    contract.reward = (uint16_t)(i ? 270 : 100); contract.active = 2;
    Ship& ship = g_ships[i];
    ship.state = SHIP_SAILING; ship.currentPort = ship.fromPort = 0;
    ship.toPort = destination; ship.route[0] = destination;
    ship.routeCount = 1; ship.routePos = 0; ship.incidentArmed = 0;
    ship.legElapsedMinutes = 0; ship.legTotalMinutes = (uint16_t)((i + 1) * 60);
    ship.voyageEarned = 0; ship.voyageDelivered = 0; ship.manifestBonus = 0;
    CargoItem cargo{};
    cargo.kind = CARGO_CONTRACT; cargo.jobKind = JOB_FREIGHT;
    cargo.good = GOOD_GRAIN; cargo.quantity = contract.quantity;
    cargo.destination = destination; cargo.projectRegion = 0xFF;
    cargo.contractId = contract.id;
    ship.cargo[0] = cargo; ship.cargoCount = 1;
  }
  auto selectNotice = [](uint8_t shipIndex) {
    for (int i = 0; i < g_attentionCount; ++i)
      if (g_attention[i].kind == ATTENTION_ARRIVAL && g_attention[i].shipIndex == shipIndex) {
        attention_select((uint8_t)i);
        return;
      }
    ::printf("host: missing consecutive arrival notice for ship %u\n", shipIndex);
    host_test_exit(1);
  };
  auto emit = [&](int frame, uint8_t shipIndex) {
    if (g_ui.card != CARD_ARRIVAL || g_ui.arrivalSummaryShip != shipIndex ||
        g_ui.arrivalSummaryPort != shipIndex + 1 || g_ui.arrivalSummaryDelivered != shipIndex + 2 ||
        g_ui.arrivalSummaryCoins != (shipIndex ? 270 : 100)) {
      ::printf("host: inconsistent %s fixture\n", names[frame]);
      host_test_exit(1);
    }
    if (!gallery_wants_frame(names[frame])) return;
    render_request_full_refresh();
    render_frame();
    dump_gallery_frame(dir, index + frame, names[frame]);
  };
  g_ui.title = 0; g_ui.scene = SCENE_WORLD; g_ui.card = CARD_HELP;
  g_ui.selectedShip = 1; g_ui.selectedPort = 0;
  // Deterministic gallery setup mirrors the consecutive-arrival regression;
  // the separate delivery journey continues to use only the real clock.
  sim_advance_minutes(60);
  selectNotice(0); emit(0, 0);
  sim_advance_minutes(60);
  emit(1, 0);  // the second ship's arrival preserves the report being read
  g_ui.card = CARD_INBOX; selectNotice(1); emit(2, 1);
  g_ui.card = CARD_INBOX; selectNotice(0); emit(3, 0);
  return index + 4;
}

static void run_gallery(const char* dir) {
  ::mkdir(dir, 0755);
  galleryWritten = 0;
  if (gallery_wants_frame("title")) {
    gallery_seed();
    g_ui.title = 1;
    g_ui.dirty = 1;
    render_frame();
    dump_gallery_frame(dir, 0, "title");
  }
  for (int i = 0; i < (int)(sizeof(GALLERY) / sizeof(GALLERY[0])); ++i) {
    const GalleryEntry& e = GALLERY[i];
    if (!gallery_wants_frame(e.name)) continue;
    if (!strcmp(e.name, "guided-port")) guided_gallery_seed();
    else gallery_seed();
    g_ui.scene = e.scene;
    g_ui.card = e.card;
    g_ui.page = e.page;
    if (e.card == CARD_INCIDENT) {
      g_incident.active = 1;
      g_incident.shipIndex = 0;
      g_incident.kind = INCIDENT_PIRATES;
      g_incident.risk = RISK_EVEN;
      g_ships[0].state = SHIP_AWAITING_DECISION;
      g_captains[g_ships[0].captainId].loyalty = 100;
      g_captains[g_ships[0].captainId].scars[0] = SCAR_PIRATE;
      g_captains[g_ships[0].captainId].scarCount = 1;
    }
    if (e.card == CARD_MENU) g_ui.confirmReset = 1;
    if (!strcmp(e.name, "route-preview")) {
      for (int cargo = 0; cargo < g_ships[0].cargoCount; ++cargo)
        if (g_ships[0].cargo[cargo].kind == CARGO_MARKET)
          g_ships[0].cargo[cargo].paidUnitPrice = 1;
      sim_route_prepare_preview(0);
      g_ui.page = 2;
    } else if (!strcmp(e.name, "ship-selected-map")) {
      g_ui.selectedShip = 0;
      sim_route_clear();
      sim_route_add(1);
      sim_dispatch(0);
      g_ships[0].legElapsedMinutes = g_ships[0].legTotalMinutes / 2;
      attention_add(ATTENTION_INCIDENT, 0, NO_PORT, true);
    } else if (!strcmp(e.name, "captain-detail")) {
      g_captains[0].xp = 39; g_captains[0].level = 5; g_captains[0].loyalty = 100;
      g_captains[0].scars[0] = SCAR_STORM; g_captains[0].scars[1] = SCAR_PIRATE;
      g_captains[0].scarCount = 2; g_captains[0].voyages = 999;
      g_captains[0].deliveries = 65535; g_captains[0].rescues = 99;
      g_captains[0].incidentsSurvived = 999;
      g_ui.captainView = 1; g_ui.selectedCaptain = 0;
    } else if (!strcmp(e.name, "captain-recruitment")) {
      g_ports[g_ui.selectedPort].officeLevel = 2;
      g_ui.captainView = 2; g_ui.selectedCaptain = 0;
    } else if (!strcmp(e.name, "arrival")) {
      g_ui.arrivalSummaryVisible = 1;
      g_ui.arrivalSummaryShip = 0;
      g_ui.arrivalSummaryPort = 1;
      g_ui.arrivalSummaryDelivered = 1;
      g_ui.arrivalSummaryCoins = 166;
    } else if (!strcmp(e.name, "inbox-full")) {
      g_attentionCount = MAX_ATTENTION;
      for (int a = 0; a < MAX_ATTENTION; ++a) {
        g_attention[a] = AttentionEntry{(uint32_t)(a + 1), (uint32_t)(a * 60),
          (uint8_t)(a % (ATTENTION_RECOVERY + 1)), NO_SHIP, (uint8_t)(a % MAX_PORTS),
          (uint8_t)(a % 3 == 0), 1, 0, 0xFF};
      }
      g_campaign.coins = 999999;
    } else if (!strcmp(e.name, "port-works-won")) {
      g_campaign.won = 1;
    } else if (!strcmp(e.name, "world-zoom")) {
      g_ui.mapZoom = 2; g_ui.mapFocusX = PORT_DEFS[4].x; g_ui.mapFocusY = PORT_DEFS[4].y;
    } else if (!strcmp(e.name, "world-zoom4")) {
      g_ui.mapZoom = 4; g_ui.mapFocusX = PORT_DEFS[4].x; g_ui.mapFocusY = PORT_DEFS[4].y;
    }
    if (e.card == CARD_LEDGER) {
      g_campaign.coins = 999999;
      g_campaign.recoveryDebt = 12345;
      g_campaign.marketProfit = -9876;
      g_campaign.contractRevenue = 1234567;
      g_campaign.operatingCosts = 765432;
    }
    g_ui.dirty = 1;
    if (!strcmp(e.name, "route-preparation")) input_test_focus_row(4);
    render_request_full_refresh();  // bypass the e-paper refresh throttle
    render_frame();
    dump_gallery_frame(dir, i + 1, e.name);
  }
  int frameCount = (int)(sizeof(GALLERY) / sizeof(GALLERY[0])) + 1;
  frameCount = run_x4_gallery_extras(dir, frameCount);
  if (gallery_wants_frame("sleep")) {
    render_sleep_screen();
    dump_gallery_frame(dir, frameCount, "sleep");
  }
  ++frameCount;
  for (int region = 0; region < REGION_COUNT; ++region) {
    char name[32];
    snprintf(name, sizeof(name), "charter-region-%d", region);
    int frameIndex = frameCount++;
    if (!gallery_wants_frame(name)) continue;
    gallery_seed();
    g_ui.card = CARD_CHARTER;
    if (!input_test_focus_row(region)) {
      ::printf("host: cannot focus %s\n", name);
      host_test_exit(1);
    }
    render_request_full_refresh();
    render_frame();
    dump_gallery_frame(dir, frameIndex, name);
  }
  frameCount = run_x4_consecutive_arrival_gallery(dir, frameCount);
  if (gallery_wants_frame("captain-recruitment-unavailable")) {
    gallery_seed();
    g_ports[g_ui.selectedPort].officeLevel = 1;
    g_ui.card = CARD_CAPTAINS; g_ui.captainView = 2; g_ui.selectedCaptain = 0;
    render_request_full_refresh();
    render_frame();
    dump_gallery_frame(dir, frameCount, "captain-recruitment-unavailable");
  }
  ++frameCount;
  for (int edge = 0; edge < 2; ++edge) {
    const char* name = edge ? "map-edge-labels" : "map-crowded-labels";
    int frameIndex = frameCount++;
    if (!gallery_wants_frame(name)) continue;
    gallery_seed();
    g_ui.scene = SCENE_WORLD; g_ui.card = CARD_NONE;
    g_ui.selectedPort = 0; g_ui.selectedShip = 0;
    g_ui.mapZoom = edge ? 4 : 2;
    g_ui.mapFocusX = edge ? 131 : 105;
    g_ui.mapFocusY = edge ? 18 : 36;
    g_attentionCount = 0;
    const uint16_t elapsed[] = {2000, 4000, 5500};
    const uint16_t edgeElapsed[] = {7100, 6500, 6200};
    for (int i = 0; i < 3; ++i) {
      Ship& ship = g_ships[i];
      ship.state = SHIP_AWAITING_DECISION;
      ship.currentPort = ship.fromPort = edge ? 1 : 0;
      ship.toPort = edge ? 0 : 1;
      ship.route[0] = ship.toPort; ship.routeCount = 1; ship.routePos = 0;
      ship.legTotalMinutes = 7200;
      ship.legElapsedMinutes = edge ? edgeElapsed[i] : elapsed[i];
      attention_add(ATTENTION_INCIDENT, (uint8_t)i, NO_PORT, true);
    }
    render_request_full_refresh(); render_frame();
    dump_gallery_frame(dir, frameIndex, name);
  }
  if (gallery_wants_frame("notices-full-count")) {
    gallery_seed();
    g_ui.scene = SCENE_PORT; g_ui.card = CARD_NONE;
    g_attentionCount = MAX_ATTENTION;
    for (int i = 0; i < MAX_ATTENTION; ++i)
      g_attention[i] = AttentionEntry{(uint32_t)(i + 1), 0, ATTENTION_INCIDENT,
        0, NO_PORT, (uint8_t)(i == 0), 1, 0, 0xFF};
    // Notices is a retained card, no longer a command on the port screen.
    // Capture this prepared capacity state directly, like other legacy cards.
    g_ui.card = CARD_INBOX; g_ui.page = 0;
    render_request_full_refresh(); render_frame();
    dump_gallery_frame(dir, frameCount, "notices-full-count");
  }
  ++frameCount;
  const char* followNames[] = {"map-follow-2x", "map-follow-4x", "map-follow-wrapped",
                               "map-follow-paused", "map-follow-orders"};
  for (int fixture = 0; fixture < 5; ++fixture) {
    int frameIndex = frameCount++;
    if (!gallery_wants_frame(followNames[fixture])) continue;
    gallery_seed();
    Ship& ship = g_ships[0];
    ship.state = fixture == 4 ? SHIP_AWAITING_DECISION : SHIP_SAILING;
    ship.currentPort = ship.fromPort = fixture == 2 ? 20 : 0;
    ship.toPort = fixture == 2 ? 23 : 1;
    ship.route[0] = ship.toPort; ship.routeCount = 1; ship.routePos = 0;
    ship.legTotalMinutes = fixture == 2 ? 18000 : 7200;
    ship.legElapsedMinutes = ship.legTotalMinutes / 2; ship.incidentArmed = 0;
    g_ui.scene = SCENE_WORLD; g_ui.card = CARD_NONE;
    g_ui.selectedShip = 0; g_ui.selectedPort = ship.toPort;
    g_ui.mapZoom = fixture == 0 ? 2 : 4;
    g_ui.mapFocusX = 0; g_ui.mapFocusY = 90; // deliberately stale/offscreen before following
    g_campaign.paused = fixture == 3;
    if (fixture == 4) g_incident = Incident{1,0,INCIDENT_STORM,RISK_EVEN};
    render_map_follow_selected_ship();
    render_request_full_refresh(); render_frame();
    int x = 0, y = 0;
    if (!render_map_following_ship() || !render_host_map_position(0, &x, &y)) {
      ::printf("host: follow target hidden in %s\n", followNames[fixture]); host_test_exit(1);
    }
    dump_gallery_frame(dir, frameIndex, followNames[fixture]);
  }
  struct RouteGalleryFixture { uint8_t from, to; uint16_t fraction; const char* name; };
  const RouteGalleryFixture routeViews[] = {
    {0, 1, 5000, "route-water-kingsfall-belmar"},
    {1, 2, 5000, "route-water-north-coast"},
    {0, 3, 4000, "route-water-atlantic"},
    {4, 10, 5000, "route-water-south-atlantic"},
    {9, 12, 5000, "route-water-cape"},
    {13, 16, 5000, "route-water-east-asia"},
    {20, 23, 5000, "route-water-pacific-wrap"},
    {23, 20, 5000, "route-water-pacific-reverse"}
  };
  for (const RouteGalleryFixture& fixture : routeViews) {
    int frameIndex = frameCount++;
    if (!gallery_wants_frame(fixture.name)) continue;
    gallery_seed();
    g_campaign.shipCount = 1; g_campaign.worldEventType = EVENT_NONE; g_campaign.worldEventDays = 0;
    g_ui.scene = SCENE_WORLD; g_ui.card = CARD_NONE; g_ui.mapZoom = 4;
    g_ui.selectedShip = 0; g_ui.selectedPort = fixture.to; g_ui.routeCount = 0;
    g_attentionCount = 0; g_incident = {};
    Ship& ship = g_ships[0];
    ship.state = SHIP_SAILING; ship.currentPort = ship.fromPort = fixture.from; ship.toPort = fixture.to;
    ship.routeCount = 1; ship.routePos = 0; ship.route[0] = fixture.to;
    ship.legTotalMinutes = 10000; ship.legElapsedMinutes = fixture.fraction; ship.incidentArmed = 0;
    render_map_follow_selected_ship();
    render_request_full_refresh(); render_frame();
    int x = 0, y = 0;
    if (!render_host_map_position(0, &x, &y)) {
      ::printf("host: route fixture ship is outside the chart in %s\n", fixture.name); host_test_exit(1);
    }
    dump_gallery_frame(dir, frameIndex, fixture.name);
  }
  for (int wrapped = 0; wrapped < 2; ++wrapped) {
    const char* name = wrapped ? "route-miniature-wrapped" : "route-miniature-coast";
    int frameIndex = frameCount++;
    if (!gallery_wants_frame(name)) continue;
    gallery_seed();
    g_campaign.shipCount = 1; g_campaign.worldEventType = EVENT_NONE; g_campaign.worldEventDays = 0;
    g_ui.scene = SCENE_WORLD; g_ui.card = CARD_ROUTE; g_ui.page = 0;
    g_ui.selectedShip = 0; g_ui.selectedPort = wrapped ? 20 : 0;
    g_attentionCount = 0; g_incident = {};
    Ship& ship = g_ships[0];
    ship.state = SHIP_DOCKED; ship.currentPort = ship.fromPort = g_ui.selectedPort;
    ship.toPort = ship.currentPort; ship.routeCount = ship.routePos = ship.cargoCount = 0;
    if (CaptainProfile* captain = sim_ship_captain(0)) captain->currentPort = ship.currentPort;
    sim_route_clear();
    if (!sim_route_add(wrapped ? 23 : 1) || !input_test_focus_row(0)) {
      ::printf("host: cannot prepare miniature fixture %s\n", name); host_test_exit(1);
    }
    render_request_full_refresh(); render_frame();
    dump_gallery_frame(dir, frameIndex, name);
  }
  struct ShoreGalleryFixture { uint8_t port, zoom; const char* name; };
  const ShoreGalleryFixture shoreViews[] = {
    {1, 1, "port-shore-belmar-1x"}, {1, 2, "port-shore-belmar-2x"},
    {1, 4, "port-shore-belmar-4x"}, {8, 4, "port-shore-solhaven-4x"},
    {10, 4, "port-shore-saint-orin-4x"}, {23, 4, "port-shore-far-horizon-4x"}
  };
  auto shore_capture = [&](uint8_t port, uint8_t zoom, const char* name) {
    int frameIndex = frameCount++;
    if (!gallery_wants_frame(name)) return;
    gallery_seed();
    g_campaign.shipCount = 1; g_campaign.worldEventType = EVENT_NONE; g_campaign.worldEventDays = 0;
    g_ui.scene = SCENE_WORLD; g_ui.card = CARD_NONE; g_ui.mapZoom = zoom;
    g_ui.selectedShip = 0; g_ui.selectedPort = port; g_ui.routeCount = 0;
    g_attentionCount = 0; g_incident = {};
    Ship& ship = g_ships[0];
    ship.state = SHIP_DOCKED; ship.currentPort = ship.fromPort = ship.toPort = port;
    ship.routeCount = ship.routePos = ship.cargoCount = 0;
    if (CaptainProfile* captain = sim_ship_captain(0)) captain->currentPort = port;
    render_map_stop_follow(); render_map_focus_port(port);
    render_request_full_refresh(); render_frame();
    dump_gallery_frame(dir, frameIndex, name);
  };
  for (const ShoreGalleryFixture& fixture : shoreViews)
    shore_capture(fixture.port, fixture.zoom, fixture.name);
  for (uint8_t port = 0; port < MAX_PORTS; ++port) {
    if (port == 1 || port == 8 || port == 10 || port == 23) continue;
    char slug[32], name[64];
    size_t length = 0;
    const char* source = PORT_DEFS[port].name;
    for (; source[length] && length + 1 < sizeof(slug); ++length)
      slug[length] = source[length] == ' ' ? '-' : (char)tolower((unsigned char)source[length]);
    slug[length] = 0;
    snprintf(name, sizeof(name), "port-shore-%s-4x", slug);
    shore_capture(port, 4, name);
  }
  // Append the caption variants so existing gallery frame numbers stay stable.
  const char* sleepNames[] = {"standby", "sleep-save-failed"};
  const char* sleepNotes[] = {"STANDBY", "SAVE FAILED"};
  for (int variant = 0; variant < 2; ++variant) {
    int frameIndex = frameCount++;
    if (!gallery_wants_frame(sleepNames[variant])) continue;
    render_sleep_screen(sleepNotes[variant]);
    dump_gallery_frame(dir, frameIndex, sleepNames[variant]);
  }
  ::printf("host: gallery -> %d of %d frames in %s\n", galleryWritten, frameCount, dir);
  host_test_exit(0);
}

#include "x4_screen_atlas.h"

static bool run_ui_selftest() {
  if (!render_host_ui_skin_selftest()) return false;
  sim_new_campaign(0x515549u);
  g_ui.title = 0;
  render_host_reset_notice_mask();
  for (int code = 1; code < SIM_RESULT_COUNT; ++code) {
    if (!sim_result_text((SimResultCode)code)[0]) return false;
    sim_set_notice((SimResultCode)code);
    render_frame();
  }
  uint32_t expected = 0;
  for (int code = 1; code < SIM_RESULT_COUNT; ++code) expected |= 1u << code;
  if ((render_host_notice_mask() & expected) != expected) return false;
  g_ui.mapZoom = 1;
  render_map_toggle_zoom(80, 70);
  if (g_ui.mapZoom != 2) return false;
  render_map_toggle_zoom(80, 70);
  if (g_ui.mapZoom != 4) return false;
  for (uint8_t level : {6, 8, 12}) {
    render_map_toggle_zoom(80, 70);
    if (g_ui.mapZoom != level) return false;
  }
  render_map_toggle_zoom(80, 70);
  if (g_ui.mapZoom != 1) return false;

  sim_new_campaign(0xA11CEu);
  g_ui.title = 0;
  if (!sim_tutorial_guided()) return false;
  if (!sim_tutorial_complete(TUTORIAL_PORT) ||
      g_campaign.tutorialStep != TUTORIAL_CONTRACT) return false;
  int contract = -1;
  for (int i = 0; i < MAX_CONTRACTS_PER_PORT; ++i)
    if (g_contracts[0][i].active == 1) { contract = i; break; }
  if (contract < 0 || !sim_load_contract(0, 0, (uint8_t)contract)) return false;
  sim_tutorial_complete(TUTORIAL_CONTRACT);
  sim_tutorial_complete(TUTORIAL_CARGO);
  if (g_campaign.tutorialStep != TUTORIAL_ROUTE) return false;
  if (!sim_route_add(1) || g_campaign.tutorialStep != TUTORIAL_ROUTE) return false;
  sim_tutorial_complete(TUTORIAL_ROUTE);
  if (!sim_dispatch(0)) return false;
  sim_tutorial_complete(TUTORIAL_DISPATCH);
  if (g_ships[0].state != SHIP_SAILING) return false;
  g_ui.scene = SCENE_WORLD;
  g_ui.mapZoom = 1;  // a new company starts at 2x; this check assumes the full chart
  int sx = 2 + PORT_DEFS[g_ships[0].fromPort].x * (VIEW_W - 4) / MAP_W;
  int sy = VIEW_Y + 3 + PORT_DEFS[g_ships[0].fromPort].y * (VIEW_H - 7) / MAP_H;
  if (render_map_ship_at(sx, sy) != 0) return false;
  g_ships[0].incidentArmed = 0;
  sim_advance_minutes(g_ships[0].legTotalMinutes + 1);
  if (!g_ui.arrivalSummaryVisible || g_ui.arrivalSummaryShip != 0) return false;
  if (sim_tutorial_guided() || g_ui.card != CARD_ARRIVAL) return false;
  return true;
}

extern char** environ;
static bool host_scripted_run() {
  for (char** e = environ; *e; ++e) {
    if (strncmp(*e, "SH_", 3)) continue;
    const char* eq = strchr(*e, '=');
    std::string name(*e, eq ? (size_t)(eq - *e) : strlen(*e));
    if (name.find("TEST") != std::string::npos || name == "SH_KEYS" || name == "SH_TAPS" ||
        name == "SH_X4_SCREEN_ATLAS" || name.rfind("SH_GALLERY", 0) == 0 || name == "SH_DEMO" ||
        name == "SH_LOAD_TEST" || name == "SH_POWER_TEST") return true;
  }
  return false;
}

static int user_func(bool* running) {
  parse_taps();
  parse_keys();
  setup();
  // Scripted runs fabricate progress (a fixture ship, bulk deliveries); a goal
  // moment would swallow their next key. Real play and its own test keep it.
  input_test_goal_moments(!host_scripted_run());
  sim_test_story(!host_scripted_run());
  if (getenv("SH_X4_GOAL_MOMENT_TEST")) host_test_exit(run_x4_goal_moment_test() ? 0 : 1);
  if (getenv("SH_X4_STORY_TEST")) host_test_exit(run_x4_story_test() ? 0 : 1);
  if (getenv("SH_X4_ATLAS_TEST")) host_test_exit(run_x4_atlas_test() ? 0 : 1);
  if (getenv("SH_X4_ONBOARDING_TEST")) host_test_exit(run_x4_onboarding_test() ? 0 : 1);
  if (const char* atlas = getenv("SH_X4_SCREEN_ATLAS")) host_test_exit(run_x4_screen_atlas(atlas) ? 0 : 1);
  if (getenv("SH_X4_NAVIGATION_TEST")) host_test_exit(run_x4_navigation_test() ? 0 : 1);
  if (getenv("SH_X4_MERCHANT_TEST")) host_test_exit(run_x4_merchant_ui_test() ? 0 : 1);
  if (getenv("SH_X4_WATCH_JOURNEY_TEST")) host_test_exit(run_x4_watch_journey_test() ? 0 : 1);
  if (getenv("SH_X4_MERCHANT_STANDBY_TEST")) host_test_exit(run_x4_merchant_standby_test() ? 0 : 1);
  if (getenv("SH_X4_MERCHANT_RETURN_TEST")) host_test_exit(run_x4_merchant_return_test() ? 0 : 1);
  if (getenv("SH_X4_STARTUP_INPUT_TEST")) host_test_exit(run_x4_startup_input_test() ? 0 : 1);
  if (getenv("SH_X4_DISPATCH_TEST")) host_test_exit(run_x4_dispatch_acceptance_test() ? 0 : 1);
  if (getenv("SH_X4_RETURN_TITLE_TEST")) host_test_exit(run_x4_return_title_boot_test() ? 0 : 1);
  if (getenv("SH_X4_RETURN_BOOT_TEST")) host_test_exit(run_x4_return_boot_ui_test() ? 0 : 1);
  if (getenv("SH_X4_RETURN_TEST")) host_test_exit(run_x4_return_ui_test() ? 0 : 1);
  if (getenv("SH_X4_STANDBY_TEST")) host_test_exit(run_x4_standby_test() ? 0 : 1);
  if (getenv("SH_X4_BUFFERED_INPUT_TEST")) host_test_exit(run_x4_buffered_input_test() ? 0 : 1);
  if (getenv("SH_X4_GROWTH_TEST")) host_test_exit(run_x4_growth_ui_test() ? 0 : 1);
  // The main X4 check is the current merchant journey; the signed-voyage
  // walk-through it replaced described a flow no X4 company can start.
  if (getenv("SH_X4_TEST") && !strcmp(getenv("SH_X4_TEST"), "1"))
    host_test_exit(run_x4_merchant_ui_test() ? 0 : 1);
  if (getenv("SH_X4_TEST") && !strcmp(getenv("SH_X4_TEST"), "nav"))
    host_test_exit(run_x4_details_navigation_test() ? 0 : 1);
  if (getenv("SH_SAVE_TEST")) host_test_exit(save_selftest() ? 0 : 1);
  if (getenv("SH_LOAD_TEST")) {
    // SH_FS_DIR holds a save written by an earlier build: it must resume.
    bool ok = g_bootResumed && g_campaign.worldMinutes > 0;  // a fresh company has neither
    ::printf("load selftest: %s (ships %u, coins %ld, day %lu, stage %u, capital %ld)\n",
             ok ? "PASS" : "FAIL", g_campaign.shipCount, (long)g_campaign.coins,
             (unsigned long)(g_campaign.worldMinutes / 1440u + 1), sim_stage(),
             (long)g_campaign.capitalSpending);
    host_test_exit(ok ? 0 : 1);
  }
  if (getenv("SH_UI_TEST")) {
    bool ok = run_ui_selftest();
    ::printf("ui selftest: %s\n", ok ? "PASS" : "FAIL");
    host_test_exit(ok ? 0 : 1);
  }
  if (const char* gallery = getenv("SH_GALLERY")) run_gallery(gallery);
  bool inputTest = getenv("SH_INPUT_TEST") != nullptr;
  uint32_t inputTestLoopDelay = inputTest && getenv("SH_INPUT_TEST_LOOP_DELAY_MS") ?
    (uint32_t)atoi(getenv("SH_INPUT_TEST_LOOP_DELAY_MS")) : 0;
  uint32_t inputTestStart = millis();
  if (inputTest && !schedule_input_test()) {
    ::printf("input selftest: no compatible contract\n");
    host_test_exit(1);
  }
  if (getenv("SH_X4_ROUTE_GEOMETRY_TEST")) {
    bool ok = run_x4_route_geometry_test();
    ::printf("x4 route geometry selftest: %s\n", ok ? "PASS" : "FAIL");
    host_test_exit(ok ? 0 : 1);
  }
  if (getenv("SH_X4_FOLLOW_TEST")) {
    bool ok = run_x4_follow_boundary_test();
    ::printf("x4 follow boundary selftest: %s\n", ok ? "PASS" : "FAIL");
    host_test_exit(ok ? 0 : 1);
  }
  if (const char* mode = getenv("SH_POWER_TEST")) {
    // SH_BATTERY sets the reading: "off" expects a save and power-off, "warn"
    // expects the warning notice and the game still awake.
    board_x4_tick();
    bool ok = !strcmp(mode, "off") ? board_x4_asleep()
                                   : (!board_x4_asleep() && g_ui.noticeCode == SIM_LOW_BATTERY);
    ::printf("power selftest (%s): %s\n", mode, ok ? "PASS" : "FAIL");
    host_test_exit(ok ? 0 : 1);
  }
  const char* x4Mode = getenv("SH_X4_TEST");
  bool x4Test = x4Mode != nullptr;
  bool x4Nav = x4Test && !strcmp(x4Mode, "nav");
  bool x4Manifest = x4Test && !strcmp(x4Mode, "manifest");
  bool x4DeliveryMode = x4Test && !strcmp(x4Mode, "delivery");
  bool x4WatchMode = x4Test && !strcmp(x4Mode, "watch");
  x4BuiltinKeys = x4Test;
  x4BuiltinPressStart = x4_press_count();
  if (x4Test && !(x4Nav ? schedule_x4_nav_test() :
                 x4Manifest ? schedule_x4_manifest_test() :
                 x4WatchMode ? schedule_x4_watch_test() :
                 x4DeliveryMode ? schedule_x4_delivery_test() : schedule_x4_test())) {
    ::printf("x4 selftest: no compatible contract\n");
    host_test_exit(1);
  }
  if (getenv("SH_DEMO")) seed_demo();
  if (const char* liveDir = getenv("SH_LIVE_QA_DIR")) {
    if (x4Test || keyCount || tapCount || getenv("SH_DEMO") || getenv("SH_SKIP_TITLE_MS") || getenv("SH_TIME_SCALE")) {
      ::printf("live QA: use an ordinary unaccelerated game without scripted input or fixtures\n");
      host_test_exit(1);
    }
    x4LiveDir = liveDir;
    ::mkdir(x4LiveDir, 0755);
    live_qa_capture(0, "ready");
  }
  uint32_t shotMs = getenv("SH_SHOT_MS") ? (uint32_t)atol(getenv("SH_SHOT_MS")) : 0;
  const char* shotPath = getenv("SH_SHOT_PATH") ? getenv("SH_SHOT_PATH") : "host_shot.ppm";
  bool exitAfter = getenv("SH_SHOT_EXIT") != nullptr;
  uint32_t skipTitle = getenv("SH_SKIP_TITLE_MS") ? (uint32_t)atol(getenv("SH_SKIP_TITLE_MS")) : 0;
  bool shotDone = shotMs == 0;
  bool skipDone = skipTitle == 0;
  uint32_t x4LastServiced = 0;
  while (*running) {
    if (inputTestLoopDelay) delay(min(inputTestLoopDelay, (uint32_t)1000));
    live_qa_poll();
    loop();
    if (x4LiveDone) {
      live_qa_capture(x4LiveCommand++, x4LiveResult);
      x4LiveDone = false;
    }
    uint32_t now = millis();
    if (!skipDone && now >= skipTitle) {
      g_ui.title = 0;
      g_ui.dirty = 1;
      skipDone = true;
    }
    if (!shotDone && now >= shotMs) {
      dump_ppm(shotPath);
      shotDone = true;
      if (exitAfter) host_test_exit(0);
    }
    uint32_t x4Serviced = x4_press_count() - x4BuiltinPressStart;
    if (x4Test && x4Serviced != x4LastServiced) {
      // Long scripted journeys are allowed to keep making progress through
      // blocking panel renders. A stuck press still has a bounded timeout.
      x4LastServiced = x4Serviced;
      x4TestCheckAt = now + 60000;
    }
    bool x4Finished = x4Test && (x4Serviced >= (uint32_t)keyCount || now >= x4TestCheckAt);
    if (x4Manifest) {
      tick_x4_manifest_test(x4Serviced);
      if (x4Finished) {
        bool ok = x4ManifestProbesOk && x4ManifestProbeNext == x4ManifestProbeCount &&
                  x4ManifestReadonly && g_ships[0].state == SHIP_SAILING;
        ::printf("x4 manifest selftest: %s (%d/%d probes, presses %lu/%d)\n", ok ? "PASS" : "FAIL",
                 x4ManifestProbeNext, x4ManifestProbeCount, (unsigned long)x4Serviced, keyCount);
        host_test_exit(ok ? 0 : 1);
      }
    }
    if (x4WatchMode) {
      int result = tick_x4_watch_test(now, x4Serviced);
      if (result) {
        ::printf("x4 watch selftest: %s (%d live frames,%d moved,1x speed,presses %lu/%d,captures %d)\n",
                 result > 0 ? "PASS" : "FAIL", x4Watch.movingFrames, x4Watch.movedFrames,
                 (unsigned long)x4Serviced, keyCount, x4Watch.captures);
        host_test_exit(result > 0 ? 0 : 1);
      }
    }
    if (x4DeliveryMode) {
      int result = tick_x4_delivery_test(now, x4Serviced);
      if (result) {
        ::printf("x4 delivery selftest: %s (sailed %d, arrived %d, opened %d, port %u, parcel %u, coins %ld, speed %u, presses %lu/%d)\n",
                 result > 0 ? "PASS" : "FAIL", x4Delivery.sailed, x4Delivery.arrived, x4Delivery.opened,
                 g_ships[0].currentPort, x4Delivery.parcel.reward, (long)g_campaign.coins,
                 g_campaign.timeScale, (unsigned long)x4Serviced, keyCount);
        host_test_exit(result > 0 ? 0 : 1);
      }
    }
    if (x4Nav && g_ui.card == CARD_MARKET && g_ui.selectedGood < GOOD_COUNT) {
      x4MarketSeen |= (uint8_t)(1u << g_ui.selectedGood);
      int medicines = 0, porcelain = 0;
      for (int c = 0; c < g_ships[0].cargoCount; ++c) {
        const CargoItem& cargo = g_ships[0].cargo[c];
        if (cargo.kind != CARGO_MARKET) continue;
        if (cargo.good == GOOD_MEDICINES) medicines += cargo.quantity;
        if (cargo.good == GOOD_PORCELAIN) porcelain += cargo.quantity;
      }
      if (medicines) x4MarketTrades |= 1;
      else if (x4MarketTrades & 1) x4MarketTrades |= 2;
      if (porcelain) x4MarketTrades |= 4;
      else if (x4MarketTrades & 4) x4MarketTrades |= 8;
    }
    if (x4Nav && x4Finished) {
      bool ok = g_ui.card == CARD_ROUTE && g_ui.page == 1 &&
                (render_host_notice_mask() & (1u << SIM_USE_ROCKER)) &&
                g_ships[0].routeTemplate.maxBuyPrice == 40 &&
                x4MarketSeen == 0xFF && x4MarketTrades == 0x0F &&
                sim_ship_used_capacity(g_ships[0]) == sim_ship_capacity(g_ships[0]);  // the rocker filled the hold
      ::printf("x4 nav selftest: %s (card %d page %d notice %d price %u goods %02x trades %02x presses %lu/%d, notices %08lx hold %d/%d)\n", ok ? "PASS" : "FAIL",
               g_ui.card, g_ui.page, g_ui.noticeCode, g_ships[0].routeTemplate.maxBuyPrice,
               x4MarketSeen, x4MarketTrades, (unsigned long)x4Serviced, keyCount,
               (unsigned long)render_host_notice_mask(), sim_ship_used_capacity(g_ships[0]),
               sim_ship_capacity(g_ships[0]));
      host_test_exit(ok ? 0 : 1);
    }
    if (x4Test && !x4DeliveryMode && !x4WatchMode && x4Finished) {
      bool ok = !g_ui.title && g_ui.card == CARD_NONE && g_ships[0].cargoCount > 0 &&
                g_ships[0].state == SHIP_SAILING && g_ships[0].routeCount == 1 &&
                !g_ui.confirmReset && g_ui.noticeCode == SIM_BAD_SHIP_STATE &&
                g_ui.mapZoom == 2 && g_campaign.timeScale == 2 && g_ui.scene == SCENE_WORLD;
      ::printf("x4 selftest: %s (card %d state %d route %d notice %d zoom %d speed %d)\n",
               ok ? "PASS" : "FAIL", g_ui.card, g_ships[0].state, g_ships[0].routeCount,
               g_ui.noticeCode, g_ui.mapZoom, g_campaign.timeScale);
      host_test_exit(ok ? 0 : 1);
    }
    if (inputTest && (builtinTouchNext == tapCount || now - inputTestStart >= 60000)) {
      bool ok = builtinTouchNext == tapCount && !g_ui.title && g_ui.card == CARD_NONE && g_ships[0].cargoCount > 0 &&
                g_ships[0].state == SHIP_SAILING && g_ships[0].routeCount == 1 &&
                !g_ui.confirmReset && builtinTouchRejectedAtSea &&
                g_ui.mapZoom == 2;  // three full zoom steps preserve the starting view
      ::printf("input selftest: %s (%d/%d sampled taps)\n", ok ? "PASS" : "FAIL", builtinTouchNext, tapCount);
      if (!ok) ::printf("input state: title=%u card=%u cargo=%u state=%u route=%u confirm=%u notice=%u zoom=%u tutorial=%u coins=%ld economy=%u rejectedAtSea=%u\n",
        g_ui.title, g_ui.card, g_ships[0].cargoCount, g_ships[0].state, g_ships[0].routeCount,
        g_ui.confirmReset, g_ui.noticeCode, g_ui.mapZoom, g_campaign.tutorialStep,
        (long)g_campaign.coins, g_campaign.economyVersion, builtinTouchRejectedAtSea);
      host_test_exit(ok ? 0 : 1);
    }
  }
  return 0;
}

int main(int, char**) {
  // Arrow keys already map to virtual GPIOs 36..39 inside LovyanGFX's SDL panel.
  lgfx::Panel_sdl::addKeyCodeMapping(SDLK_RETURN, 40);
  lgfx::Panel_sdl::addKeyCodeMapping(SDLK_KP_ENTER, 40);
  lgfx::Panel_sdl::addKeyCodeMapping(SDLK_SPACE, 40);
  lgfx::Panel_sdl::addKeyCodeMapping(SDLK_BACKSPACE, 41);
  lgfx::Panel_sdl::addKeyCodeMapping(SDLK_ESCAPE, 41);
  lgfx::Panel_sdl::addKeyCodeMapping(SDLK_p, 42);
  return lgfx::Panel_sdl::main(user_func);
}
