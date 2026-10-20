#include "game.h"
#include "economy_fixtures.h"
#include "x4_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// These scenarios exercise the optional legacy contract/spot-market rules.
// The booked-voyage campaign is covered by voyage_regressions and its matrix.
// Title and returnQuiet are cleared because the X4 mutes arrival reports while the title is up.
static void legacy_campaign(uint32_t seed) { historical_campaign(seed, 0); g_ui.title = 0; g_ui.returnQuiet = 0; }

bool save_write() { return true; }
bool save_begin() { return true; }
bool save_exists() { return false; }
bool save_read() { return false; }
bool save_reset() { return true; }
bool save_selftest() { return true; }
void audio_begin() {}
void audio_tick() {}
void audio_notice() {}
void audio_alert() {}

static int failures = 0;

static void check(bool value, const char* message) {
  if (!value) { ++failures; ::printf("FAIL: %s\n", message); }
}

static int endurance(const Ship& s) {
  int e = SHIP_CLASS_DEFS[s.classId].enduranceDays;
  for (int i = 0; i < 2; ++i) if (s.modules[i] == MODULE_LONG_CASKS + 1) e += 6;
  return e;
}

static int path_to(const Ship& ship, uint8_t target, uint8_t* out) {
  uint8_t q[MAX_PORTS], prev[MAX_PORTS], seen[MAX_PORTS];
  memset(prev, NO_PORT, sizeof(prev));
  memset(seen, 0, sizeof(seen));
  int head = 0, tail = 0;
  q[tail++] = ship.currentPort;
  seen[ship.currentPort] = 1;
  while (head < tail) {
    uint8_t p = q[head++];
    if (p == target) break;
    for (int i = 0; i < LANE_COUNT; ++i) {
      int n = -1;
      if (LANE_DEFS[i].a == p) n = LANE_DEFS[i].b;
      else if (LANE_DEFS[i].b == p) n = LANE_DEFS[i].a;
      if (n < 0 || seen[n] || !g_ports[n].unlocked) continue;
      if (LANE_DEFS[i].days > endurance(ship)) continue;
      if (SHIP_CLASS_DEFS[ship.classId].draft > PORT_DEFS[n].portClass) continue;
      seen[n] = 1; prev[n] = p; q[tail++] = (uint8_t)n;
    }
  }
  if (!seen[target]) return 0;
  uint8_t reverse[MAX_PORTS];
  int n = 0;
  for (uint8_t p = target; p != ship.currentPort && n < MAX_PORTS; p = prev[p]) reverse[n++] = p;
  if (n > MAX_ROUTE_LEGS) return 0;
  for (int i = 0; i < n; ++i) out[i] = reverse[n - i - 1];
  return n;
}

static bool invariant_ok() {
  if (g_campaign.coins < 0 || g_campaign.recoveryDebt < 0 ||
      g_campaign.shipCount < 1 || g_campaign.shipCount > MAX_SHIPS) return false;
  if (!(g_campaign.regionMask & 1)) return false;
  if (g_campaign.regionMask & ~g_campaign.chartMask) return false;
  if (g_incidentQueueCount > MAX_INCIDENTS) return false;
  if (g_attentionCount > MAX_ATTENTION || g_campaign.captainCount > MAX_CAPTAINS) return false;
  for (int p = 0; p < MAX_PORTS; ++p) {
    if (g_ports[p].warehouseCount > MAX_WAREHOUSE_ITEMS) return false;
    for (int g = 0; g < GOOD_COUNT; ++g) if (g_ports[p].stock[g] > 200) return false;
    for (int j = 0; j < MAX_CONTRACTS_PER_PORT; ++j) {
      const Contract& c = g_contracts[p][j];
      if (c.active > 2 || c.origin >= MAX_PORTS || c.destination >= MAX_PORTS || c.good >= GOOD_COUNT) return false;
    }
  }
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    const Ship& s = g_ships[i];
    if (s.classId >= SHIP_CLASS_COUNT || s.state > SHIP_DISABLED || s.currentPort >= MAX_PORTS ||
        s.fromPort >= MAX_PORTS || s.toPort >= MAX_PORTS || s.cargoCount > MAX_CARGO_ITEMS ||
        s.routeCount > MAX_ROUTE_LEGS ||
        s.routeTemplate.stopCount > MAX_ROUTE_LEGS ||
        s.routePos > s.routeCount || s.condition > 100 ||
        s.morale > 100 || sim_ship_used_capacity(s) > sim_ship_capacity(s) ||
        sim_ship_passenger_load(s) > sim_ship_berths(s)) return false;
    if (s.captainId != NO_SHIP && (s.captainId >= MAX_CAPTAINS ||
        !g_captains[s.captainId].active || g_captains[s.captainId].assignedShip != i))
      return false;
    for (int c = 0; c < s.cargoCount; ++c)
      if (s.cargo[c].good >= GOOD_COUNT || !s.cargo[c].quantity) return false;
  }
  // Stage locks: nothing a locked feature owns may exist while it is locked.
  if (!sim_feature_unlocked(FEATURE_EVENTS) && g_campaign.worldEventType != EVENT_NONE) return false;
  if (!sim_feature_unlocked(FEATURE_INCIDENTS) && (g_incident.active || g_incidentQueueCount)) return false;
  for (int p = 0; p < MAX_PORTS; ++p)
    for (int j = 0; j < MAX_CONTRACTS_PER_PORT; ++j) {
      const Contract& c = g_contracts[p][j];
      if (!c.active) continue;
      if (c.kind == JOB_CHARTER && !sim_feature_unlocked(FEATURE_CHARTERS)) return false;
      if (c.kind == JOB_PASSENGER && !sim_feature_unlocked(FEATURE_PASSENGERS)) return false;
    }
  for (int i = 0; i < g_attentionCount; ++i)
    if (!attention_kind_open(g_attention[i].kind)) return false;
  for (int i = 0; i < g_campaign.captainCount; ++i) {
    const CaptainProfile& c = g_captains[i];
    if (c.active && (c.level < 1 || c.level > BALANCE.maxCaptainLevel ||
        c.loyalty > 100 || c.scarCount > BALANCE.maxCaptainScars)) return false;
  }
  if (g_incident.active && g_incident.shipIndex >= g_campaign.shipCount) return false;
  for (int i = 0; i < g_incidentQueueCount; ++i)
    if (!g_incidentQueue[i].active || g_incidentQueue[i].shipIndex >= g_campaign.shipCount ||
        g_incidentQueue[i].kind > INCIDENT_TRADE || g_incidentQueue[i].risk > RISK_DANGEROUS)
      return false;
  return true;
}

static bool dispatch_work(uint8_t shipIndex) {
  Ship& s = g_ships[shipIndex];
  if (s.state != SHIP_DOCKED && s.state != SHIP_ARRIVED) return false;
  g_ui.selectedShip = shipIndex;
  g_ui.selectedPort = s.currentPort;

  // Sell opportunistic cargo first.
  for (int good = 0; good < GOOD_COUNT; ++good)
    while (sim_market_sell(shipIndex, (uint8_t)good)) {}

  uint8_t destination = NO_PORT;
  uint8_t path[MAX_ROUTE_LEGS];
  int pathCount = 0;
  // Loading a contract compacts the port's board (open offers first), so a
  // slot index is only valid until the next load: read what is needed before
  // loading, and re-examine the same slot after a successful load.
  for (int slot = 0; slot < MAX_CONTRACTS_PER_PORT; ++slot) {
    const Contract& c = g_contracts[s.currentPort][slot];
    if (c.active != 1) continue;
    uint8_t wanted = c.destination;
    pathCount = path_to(s, wanted, path);
    if (!pathCount) continue;
    if (sim_load_contract(shipIndex, s.currentPort, (uint8_t)slot)) {
      destination = wanted;
      break;
    }
  }
  if (destination != NO_PORT) {
    for (int slot = 0; slot < MAX_CONTRACTS_PER_PORT; ++slot) {
      const Contract& c = g_contracts[s.currentPort][slot];
      if (c.active == 1 && c.destination == destination &&
          sim_load_contract(shipIndex, s.currentPort, (uint8_t)slot)) --slot;
    }
  } else {
    // Reposition to a reachable neighboring port so a sparse board cannot stall.
    for (int lane = 0; lane < LANE_COUNT; ++lane) {
      int p = LANE_DEFS[lane].a == s.currentPort ? LANE_DEFS[lane].b :
              (LANE_DEFS[lane].b == s.currentPort ? LANE_DEFS[lane].a : -1);
      if (p < 0 || !g_ports[p].unlocked) continue;
      pathCount = path_to(s, (uint8_t)p, path);
      if (pathCount) { destination = (uint8_t)p; break; }
    }
  }
  if (destination == NO_PORT || !pathCount) return false;

  // One speculative unit from a local producer if room and solvent.
  uint8_t good = PORT_DEFS[s.currentPort].produced[0];
  sim_market_buy(shipIndex, good);

  sim_route_clear();
  for (int i = 0; i < pathCount; ++i) if (!sim_route_add(path[i])) return false;
  return sim_dispatch(shipIndex);
}

static bool staff_ship(uint8_t shipIndex) {
  if (shipIndex >= g_campaign.shipCount || sim_ship_captain(shipIndex)) return false;
  Ship& ship = g_ships[shipIndex];
  if (ship.state != SHIP_DOCKED && ship.state != SHIP_ARRIVED) return false;
  for (int i = 0; i < g_campaign.captainCount; ++i) {
    if (g_captains[i].active && g_captains[i].assignedShip == NO_SHIP &&
        g_captains[i].currentPort == ship.currentPort)
      return sim_assign_captain((uint8_t)i, shipIndex);
  }
  g_ui.selectedPort = ship.currentPort;
  SimResult hired = sim_hire_captain(0);
  return hired && sim_assign_captain((uint8_t)hired.value, shipIndex);
}

static uint64_t simulation_fingerprint() {
  uint64_t hash = 1469598103934665603ull;
  auto bytes = [&hash](const void* value, size_t size) {
    const uint8_t* data = (const uint8_t*)value;
    for (size_t i = 0; i < size; ++i) { hash ^= data[i]; hash *= 1099511628211ull; }
  };
  bytes(&g_campaign, sizeof(g_campaign)); bytes(g_ports, sizeof(g_ports));
  bytes(g_contracts, sizeof(g_contracts)); bytes(g_ships, sizeof(g_ships));
  bytes(&g_incident, sizeof(g_incident)); bytes(&g_incidentOutcome, sizeof(g_incidentOutcome));
  bytes(g_incidentQueue, sizeof(g_incidentQueue)); bytes(&g_incidentQueueCount, sizeof(g_incidentQueueCount));
  bytes(g_journal, sizeof(g_journal)); bytes(&g_journalHead, sizeof(g_journalHead));
  bytes(&g_journalCount, sizeof(g_journalCount)); bytes(g_attention, sizeof(g_attention));
  bytes(&g_attentionCount, sizeof(g_attentionCount)); bytes(g_captains, sizeof(g_captains));
  bytes(&g_routePreparation, sizeof(g_routePreparation)); bytes(&g_ui, sizeof(g_ui));
  return hash;
}

static void x4_time_tests() {
  struct SpeedCase { uint8_t speed; uint16_t daySeconds, voyageSeconds; };
  static const SpeedCase speeds[] = {
    {1, 144, 720}, {2, 72, 360}, {4, 36, 180}, {8, 18, 90}, {16, 9, 45}
  };
  legacy_campaign(155);
  uint64_t before = simulation_fingerprint();
  char text[32];
  for (const SpeedCase& c : speeds) {
    check(x4_real_seconds(1440, c.speed) == c.daySeconds &&
          x4_real_seconds(7200, c.speed) == c.voyageSeconds,
          "real duration agrees with each supported speed for one day and the first voyage");
    x4_format_duration(text, sizeof(text), x4_real_seconds(7200, c.speed));
    x4_format_time_delta(text, sizeof(text), -360, c.speed);
  }
  check(simulation_fingerprint() == before,
        "duration conversions and formatting leave simulation, UI, history and RNG unchanged");
  check(x4_real_seconds(0, 1) == 0 && x4_real_seconds(7200, 0) == 720,
        "zero duration stays zero and zero speed uses normal pacing");
  check(x4_real_seconds(1, 16) == 1 && x4_real_seconds(159, 16) == 1 &&
        x4_real_seconds(160, 16) == 1 && x4_real_seconds(161, 16) == 2,
        "partial seconds round up without over-rounding exact seconds");
  check(x4_real_seconds(UINT32_MAX, 1) == 429496730u &&
        x4_real_seconds(UINT32_MAX, 16) == 26843546u &&
        x4_real_seconds(UINT32_MAX, UINT8_MAX) == 1684301u,
        "duration ceiling does not overflow at maximum duration or speed");

  struct FormatCase { uint32_t seconds; const char* full; const char* compact; };
  static const FormatCase formats[] = {
    {0, "0s", "0s"}, {45, "45s", "45s"}, {59, "59s", "59s"},
    {60, "1m 00s", "1m"}, {61, "1m 01s", "2m"}, {90, "1m 30s", "2m"},
    {720, "12m 00s", "12m"}, {721, "12m 01s", "13m"},
    {UINT32_MAX, "71582788m 15s", "71582789m"}
  };
  for (const FormatCase& c : formats) {
    x4_format_duration(text, sizeof(text), c.seconds);
    check(!strcmp(text, c.full), "full duration keeps exact minutes and seconds");
    x4_format_duration(text, sizeof(text), c.seconds, true);
    check(!strcmp(text, c.compact), "compact duration rounds minutes up and preserves sub-minute seconds");
  }
  struct DeltaCase { int32_t worldMinutes; uint8_t speed; const char* text; };
  static const DeltaCase deltas[] = {
    {720, 1, "+1m 12s"}, {-360, 1, "-36s"}, {0, 16, "0s"},
    {720, 16, "+5s"}, {-1, 16, "-1s"}, {1, 0, "+1s"},
    {INT32_MIN, 1, "-3579139m 25s"}
  };
  for (const DeltaCase& c : deltas) {
    x4_format_time_delta(text, sizeof(text), c.worldMinutes, c.speed);
    check(!strcmp(text, c.text), "incident time formatting keeps sign, rounded duration and INT32_MIN safe");
  }
  struct { char before; char text[4]; char after; } bounded{'L', {}, 'R'};
  x4_format_duration(bounded.text, sizeof(bounded.text), UINT32_MAX);
  check(bounded.before == 'L' && bounded.after == 'R' && !strcmp(bounded.text, "715"),
        "duration formatting truncates safely inside a short caller buffer");
  x4_format_time_delta(bounded.text, sizeof(bounded.text), INT32_MIN, 1);
  check(bounded.before == 'L' && bounded.after == 'R' && !strcmp(bounded.text, "-35"),
        "signed formatting truncates safely inside a short caller buffer");
  bounded.text[0] = 'Q';
  x4_format_duration(bounded.text, 0, 720);
  x4_format_time_delta(bounded.text, 0, -360, 1);
  x4_format_duration(nullptr, 0, 720);
  x4_format_time_delta(nullptr, 0, -360, 1);
  check(bounded.text[0] == 'Q', "zero-capacity duration formatting writes nothing");

  // Drive the actual simulation at one-second intervals used by crosswinds.ino.
  // No armed incident can interrupt this baseline voyage at any speed.
  for (const SpeedCase& c : speeds) {
    legacy_campaign(156);
    g_campaign.stageSeen = 3; g_campaign.tutorialDismissed = 1;
    g_campaign.timeScale = c.speed;
    Ship& ship = g_ships[0];
    ship.state = SHIP_SAILING; ship.fromPort = ship.currentPort = 0; ship.toPort = 1;
    ship.routeCount = 1; ship.routePos = 0; ship.route[0] = 1;
    ship.legElapsedMinutes = 0; ship.legTotalMinutes = 7200;
    ship.incidentArmed = 0; ship.cargoCount = 0;
    uint32_t tick = SIM_WORLD_MINUTES_PER_REAL_SECOND * c.speed;
    for (uint32_t second = 1; second < c.voyageSeconds; ++second) sim_advance_minutes(tick);
    check(ship.state == SHIP_SAILING && ship.legElapsedMinutes == 7200u - tick,
          "one-second simulation pacing leaves a voyage sailing one second before its ETA");
    sim_advance_minutes(tick);
    check(ship.state == SHIP_ARRIVED && ship.currentPort == 1 && g_campaign.worldMinutes == 7200u,
          "the actual voyage arrives at its displayed duration for every supported speed");
  }

  legacy_campaign(157);
  g_campaign.stageSeen = 3; g_campaign.tutorialDismissed = 1;
  g_campaign.shipCount = 2;
  g_ships[1] = g_ships[0]; g_ships[1].id = 2; g_ships[1].captainId = NO_SHIP;
  for (int i = 0; i < 2; ++i) {
    g_ships[i].state = i ? SHIP_SAILING : SHIP_AWAITING_DECISION;
    g_ships[i].legElapsedMinutes = 1000; g_ships[i].legTotalMinutes = 7200;
    g_ships[i].incidentArmed = 0;
  }
  g_incident = Incident{1, 0, INCIDENT_STORM, RISK_EVEN};
  g_campaign.paused = 1;
  sim_advance_minutes(0);  // settle derived stage/board state before the clock assertion
  before = simulation_fingerprint();
  sim_advance_minutes(600);
  check(simulation_fingerprint() == before,
        "paused clock advances no world time, ships, deadlines, history or RNG");
  g_campaign.paused = 0;
  uint32_t worldBefore = g_campaign.worldMinutes;
  uint32_t deadlineBefore = g_contracts[0][0].deadlineMinute;
  sim_advance_minutes(60);
  check(g_campaign.worldMinutes == worldBefore + 60 && g_ships[0].legElapsedMinutes == 1000 &&
        g_ships[0].state == SHIP_AWAITING_DECISION && g_incident.active &&
        g_ships[1].legElapsedMinutes == 1060 && g_ships[1].state == SHIP_SAILING,
        "waiting for orders stops only that ship while the world and other voyages advance");
  check(deadlineBefore > worldBefore + 60 && g_contracts[0][0].deadlineMinute == deadlineBefore &&
        x4_real_seconds(deadlineBefore - g_campaign.worldMinutes, 1) ==
          x4_real_seconds(deadlineBefore - worldBefore, 1) - 6,
        "a decision wait consumes real deadline time without extending the contract");
}

static MarketQuote check_quote_execution(uint8_t good, bool buying, uint8_t requested) {
  uint64_t before = simulation_fingerprint();
  MarketQuote quote = sim_market_quote(0, good, buying, requested);
  sim_market_quote(0, good, buying, requested);
  check(simulation_fingerprint() == before, "repeated market quotes leave all simulation, UI, RNG and history unchanged");
  int32_t coins = g_campaign.coins, debt = g_campaign.recoveryDebt, total = 0;
  int quantity = 0;
  SimResultCode stop = SIM_OK;
  for (int i = 0; i < requested; ++i) {
    uint16_t unitPrice = content_market_price(g_ships[0].currentPort, good, buying);
    SimResult result = buying ? sim_market_buy(0, good) : sim_market_sell(0, good);
    if (!result) { stop = result.code; break; }
    total += unitPrice;
    ++quantity;
  }
  check(quote.quantity == quantity && quote.total == total && quote.stopReason == stop,
        "market quote matches incremental execution quantity, gross total and first refusal");
  check(quote.treasuryDelta == g_campaign.coins - coins &&
        quote.withholding == debt - g_campaign.recoveryDebt,
        "market quote matches exact treasury change and debt repayment");
  return quote;
}

static CargoItem market_cargo(uint8_t good, uint8_t quantity, uint16_t price = 0) {
  CargoItem cargo{};
  cargo.good = good; cargo.quantity = quantity; cargo.paidUnitPrice = price;
  cargo.destination = NO_PORT; cargo.kind = CARGO_MARKET;
  cargo.jobKind = JOB_FREIGHT; cargo.projectRegion = 0xFF;
  return cargo;
}

static void market_quote_tests() {
  for (int good = 0; good < GOOD_COUNT; ++good) {
    for (int quantity = 1; quantity <= 5; ++quantity) {
      for (int buy = 0; buy < 2; ++buy) {
        legacy_campaign(141);
        g_campaign.coins = 100000;
        g_campaign.recoveryDebt = 17;
        g_campaign.worldEventType = good & 1 ? EVENT_SHORTAGE : EVENT_GLUT;
        g_campaign.worldEventRegion = 0; g_campaign.worldEventGood = (uint8_t)good;
        g_ports[0].stock[good] = 12;
        Ship& ship = g_ships[0]; ship.classId = 4;
        if (!buy) {
          // A contract stack must be skipped, then two market lots with
          // different costs are sold in their existing first-match order.
          ship.cargo[0] = market_cargo((uint8_t)good, 1);
          ship.cargo[0].kind = CARGO_CONTRACT; ship.cargo[0].destination = 1;
          ship.cargo[1] = market_cargo((uint8_t)good, 2);
          ship.cargo[2] = market_cargo((uint8_t)good, 3, 600);
          ship.cargoCount = 3;
        }
        MarketQuote quote = check_quote_execution((uint8_t)good, buy != 0, (uint8_t)quantity);
        check(quote.quantity == quantity && quote.stopReason == SIM_OK,
              "one-to-five-unit quote fills when funds, stock and hold permit");
      }
    }
  }

  legacy_campaign(142);
  g_campaign.coins = 100000; g_ships[0].classId = 4;
  g_ports[0].stock[GOOD_PORCELAIN] = 12;
  MarketQuote moving = sim_market_quote(0, GOOD_PORCELAIN, true, 5);
  check(moving.total > 5 * content_market_price(0, GOOD_PORCELAIN, true),
        "bulk quote includes rising stock-pressure prices, not first price times quantity");
  check_quote_execution(GOOD_PORCELAIN, true, 5);

  legacy_campaign(143);
  g_campaign.coins = 100000; g_ships[0].classId = 4;
  g_ports[0].stock[GOOD_GRAIN] = 2;
  MarketQuote limited = check_quote_execution(GOOD_GRAIN, true, 5);
  check(limited.quantity == 2 && limited.stopReason == SIM_CARGO_UNAVAILABLE,
        "bulk quote reports the stock-limited partial trade");

  legacy_campaign(144);
  g_campaign.coins = 100000; g_ships[0].classId = 4;
  g_campaign.coins = sim_market_quote(0, GOOD_TEA, true, 2).total;
  limited = check_quote_execution(GOOD_TEA, true, 5);
  check(limited.quantity == 2 && limited.stopReason == SIM_NO_FUNDS,
        "bulk quote stops precisely when the next unit cannot be afforded");

  legacy_campaign(145);
  g_campaign.coins = 100000;
  g_ships[0].cargo[0] = market_cargo(GOOD_TIMBER, 3); g_ships[0].cargoCount = 1;
  limited = check_quote_execution(GOOD_TEA, true, 5);
  check(limited.quantity == 1 && limited.stopReason == SIM_NO_CAPACITY,
        "bulk quote respects the remaining hold");

  legacy_campaign(146);
  g_campaign.coins = 100000;
  Ship& ship = g_ships[0]; ship.classId = 4;
  ship.modules[0] = MODULE_CHARTER_PENNANT + 1; ship.modules[1] = MODULE_BERTHS + 1;
  for (int i = 0; i < MAX_CARGO_ITEMS; ++i) {
    ship.cargo[i] = market_cargo(GOOD_GRAIN, 1);
    if (i >= 13) { ship.cargo[i].kind = CARGO_CONTRACT; ship.cargo[i].jobKind = JOB_PASSENGER; }
  }
  ship.cargoCount = MAX_CARGO_ITEMS;
  check(sim_ship_used_capacity(ship) < sim_ship_capacity(ship), "full-stack fixture still has hold space");
  limited = check_quote_execution(GOOD_GRAIN, true, 5);
  check(!limited.quantity && limited.stopReason == SIM_NO_CAPACITY,
        "quote preserves the existing full-stack refusal even for a mergeable good");

  legacy_campaign(147);
  g_ships[0].cargo[0] = market_cargo(GOOD_MEDICINES, 2); g_ships[0].cargoCount = 1;
  g_campaign.recoveryDebt = 1;
  limited = check_quote_execution(GOOD_MEDICINES, false, 5);
  check(limited.quantity == 2 && limited.withholding == 1 &&
        limited.treasuryDelta == limited.total - 1 && limited.stopReason == SIM_CARGO_UNAVAILABLE,
        "sale quote caps withholding at remaining debt and stops at actual market inventory");
  g_ships[0].cargo[0] = market_cargo(GOOD_MEDICINES, 2);
  g_ships[0].cargo[0].kind = CARGO_CONTRACT; g_ships[0].cargoCount = 1;
  limited = check_quote_execution(GOOD_MEDICINES, false, 1);
  check(!limited.quantity && limited.stopReason == SIM_CARGO_UNAVAILABLE,
        "contract cargo is never presented as sellable inventory");

  g_ships[0].state = SHIP_SAILING;
  limited = check_quote_execution(GOOD_TEA, true, 5);
  check(!limited.quantity && limited.stopReason == SIM_BAD_SHIP_STATE,
        "quote refuses a ship that is no longer docked");
  g_ships[0].state = SHIP_DISABLED;
  check(sim_market_quote(0, GOOD_TEA, false, 1).stopReason == SIM_BAD_SHIP_STATE,
        "quote refuses a disabled ship");
  check(sim_market_quote(NO_SHIP, GOOD_TEA, true, 1).stopReason == SIM_INVALID &&
        sim_market_quote(0, GOOD_COUNT, true, 1).stopReason == SIM_INVALID,
        "quote validates ship and good IDs");
}

static void route_undo_tests() {
  legacy_campaign(148);
  sim_route_clear();
  uint64_t before = simulation_fingerprint();
  sim_route_undo();
  check(simulation_fingerprint() == before, "undo of an empty route is a no-op");
  for (uint8_t port = 0; port < MAX_PORTS; ++port) {
    before = simulation_fingerprint();
    SimResult allowed = sim_route_can_add(port);
    check(simulation_fingerprint() == before, "route append validation leaves the simulation and UI unchanged");
    SimResult actual = sim_route_add(port);
    check(allowed.code == actual.code, "route append affordance agrees with execution");
    if (actual) sim_route_undo();
  }
  check(sim_route_add(1) && sim_route_add(2), "undo fixture has two connected draft legs");
  check(sim_route_template_save(0), "undo fixture saves a route template");
  RouteTemplate saved = g_ships[0].routeTemplate;
  g_ui.routeScroll = 3; g_ui.routePrepArmed = 1;
  g_routePreparation.computed = 1; g_routePreparation.valid = 1;
  sim_route_undo();
  check(g_ui.routeCount == 1 && g_ui.routeDraft[0] == 1 && g_ui.routeDraft[1] == NO_PORT &&
        !g_ui.routeScroll && !g_ui.routePrepArmed && !g_routePreparation.computed && !g_routePreparation.valid,
        "undo removes only the final leg and invalidates its preparation");
  check(!memcmp(&saved, &g_ships[0].routeTemplate, sizeof(saved)), "undo preserves the saved template");
  sim_route_undo();
  check(!g_ui.routeCount && sim_route_recall() && g_ui.routeCount == 2,
        "recall restores the complete saved template after undoing the draft");
}

static int cargo_quantity(const Ship& ship, uint8_t kind, uint8_t jobKind, uint8_t good) {
  int quantity = 0;
  for (int i = 0; i < ship.cargoCount; ++i) {
    const CargoItem& cargo = ship.cargo[i];
    if (cargo.kind == kind && cargo.jobKind == jobKind && cargo.good == good)
      quantity += cargo.quantity;
  }
  return quantity;
}

static void incident_outcome_tests() {
  for (int kind = 0; kind < 8; ++kind) {
    for (int option = 0; option < 3; ++option) {
      legacy_campaign(149);
      g_campaign.stageSeen = 4;
      Ship& ship = g_ships[0];
      ship.state = SHIP_AWAITING_DECISION; ship.condition = 40; ship.morale = 50;
      ship.crew = 10; ship.legElapsedMinutes = 2400; ship.legTotalMinutes = 5000;
      ship.cargo[0] = market_cargo(GOOD_MEDICINES, 1);
      ship.cargo[1] = market_cargo(GOOD_GRAIN, 1);
      ship.cargo[1].kind = CARGO_CONTRACT; ship.cargo[1].jobKind = JOB_PASSENGER;
      ship.cargo[2] = market_cargo(GOOD_TIMBER, 1); ship.cargoCount = 3;
      g_captains[0].xp = BALANCE.captainXpPerLevel - 1;
      g_captains[0].loyalty = 99;
      g_incident = Incident{1, 0, (uint8_t)kind, RISK_DANGEROUS};
      Ship before = ship;
      CaptainProfile captainBefore = g_captains[0];
      int32_t coinsBefore = g_campaign.coins;
      sim_resolve_incident((uint8_t)option);
      const IncidentOutcome& outcome = g_incidentOutcome;
      check(outcome.valid && outcome.serial == 1 && outcome.shipIndex == 0 && outcome.kind == kind &&
            outcome.option == option && outcome.risk == RISK_DANGEROUS &&
            !strcmp(outcome.shipName, before.name), "incident receipt keeps the resolved decision and ship identity");
      check(outcome.treasuryDelta == g_campaign.coins - coinsBefore &&
            outcome.hullDelta == (int)ship.condition - before.condition && outcome.hullAfter == ship.condition &&
            outcome.moraleDelta == (int)ship.morale - before.morale && outcome.moraleAfter == ship.morale &&
            outcome.crewDelta == (int)ship.crew - before.crew && outcome.crewAfter == ship.crew &&
            outcome.delayMinutes == max(0, (int)ship.legTotalMinutes - ship.legElapsedMinutes) -
                                    max(0, (int)before.legTotalMinutes - before.legElapsedMinutes),
            "incident receipt records actual bounded hull, morale, crew, cash and travel changes");
      check(outcome.captainId == before.captainId && !strcmp(outcome.captainName, captainBefore.name) &&
            outcome.captainTrait == captainBefore.trait &&
            outcome.captainXpDelta == g_captains[0].xp - captainBefore.xp &&
            outcome.captainLevelBefore == captainBefore.level && outcome.captainLevelAfter == g_captains[0].level &&
            outcome.captainLoyaltyDelta == (int)g_captains[0].loyalty - captainBefore.loyalty &&
            outcome.captainLoyaltyAfter == g_captains[0].loyalty,
            "incident receipt preserves captain identity, XP, level and actual clamped loyalty change");
      uint8_t expectedScars = 0;
      for (int i = 0; i < g_captains[0].scarCount; ++i) expectedScars |= (uint8_t)(1u << g_captains[0].scars[i]);
      check(outcome.captainNewScarMask == expectedScars, "incident receipt identifies scars actually earned");
      int expectedChanges = 0;
      for (uint8_t cargoKind = 0; cargoKind < 2; ++cargoKind) {
        for (uint8_t job = 0; job < 4; ++job) {
          for (uint8_t good = 0; good < GOOD_COUNT; ++good) {
            int delta = cargo_quantity(ship, cargoKind, job, good) - cargo_quantity(before, cargoKind, job, good);
            if (!delta) continue;
            ++expectedChanges;
            int reported = 0;
            for (int i = 0; i < outcome.cargoCount; ++i) {
              const IncidentCargoChange& change = outcome.cargo[i];
              if (change.kind == cargoKind && change.jobKind == job && change.good == good)
                reported += change.quantityDelta;
            }
            check(reported == delta, "incident receipt distinguishes goods, mail, passengers and contract cargo");
          }
        }
      }
      check(outcome.cargoCount == expectedChanges, "incident receipt contains only actual cargo changes");
    }
  }

  legacy_campaign(150);
  g_campaign.stageSeen = 4; g_campaign.shipCount = 2;
  g_ships[1] = g_ships[0]; g_ships[1].id = 2; g_ships[1].captainId = NO_SHIP;
  snprintf(g_ships[1].name, sizeof(g_ships[1].name), "Second Ship");
  for (int i = 0; i < 2; ++i) {
    g_ships[i].state = SHIP_AWAITING_DECISION;
    g_ships[i].legTotalMinutes = 5000; g_ships[i].legElapsedMinutes = 2000;
  }
  g_incident = Incident{1, 0, INCIDENT_STORM, RISK_EVEN};
  g_incidentQueue[0] = Incident{1, 1, INCIDENT_REEF, RISK_EVEN}; g_incidentQueueCount = 1;
  sim_resolve_incident(0);
  check(g_incident.active && g_incident.shipIndex == 1 && g_incident.kind == INCIDENT_REEF &&
        g_incidentOutcome.shipIndex == 0 && g_incidentOutcome.kind == INCIDENT_STORM &&
        g_incidentOutcome.delayMinutes == -360,
        "queued incident activation cannot overwrite the just-completed receipt");
  IncidentOutcome first = g_incidentOutcome;
  snprintf(g_ships[0].name, sizeof(g_ships[0].name), "Changed Later");
  g_ui.selectedShip = 1;
  check(!memcmp(&first, &g_incidentOutcome, sizeof(first)), "later selection and ship edits leave the receipt snapshot stable");
  sim_resolve_incident(1);
  check(g_incidentOutcome.serial == first.serial + 1 && g_incidentOutcome.shipIndex == 1 &&
        g_incidentOutcome.kind == INCIDENT_REEF && g_incidentOutcome.treasuryDelta == -35 &&
        g_incidentOutcome.delayMinutes == 180 && g_incidentOutcome.captainId == NO_SHIP,
        "next resolved decision replaces the receipt with its own identity and actual outcome");
  sim_after_load();
  check(!g_incidentOutcome.valid, "incident receipts are cleared on load instead of inventing historical detail");

  legacy_campaign(151);
  g_campaign.stageSeen = 4;
  Ship& disabled = g_ships[0]; disabled.state = SHIP_AWAITING_DECISION; disabled.condition = 1;
  disabled.cargo[0] = market_cargo(GOOD_TEA, 1); disabled.cargoCount = 1;
  g_incident = Incident{1, 0, INCIDENT_PIRATES, RISK_DANGEROUS};
  sim_resolve_incident(1);  // Fighting always costs at least ten hull.
  check(g_incidentOutcome.disabled && g_incidentOutcome.hullAfter == 25 &&
        g_incidentOutcome.cargoCount == 1 && g_incidentOutcome.cargo[0].quantityDelta == -1,
        "disabled receipt identifies recovery hull and all cargo loss rather than claiming successful passage");

  legacy_campaign(152);
  g_campaign.stageSeen = 4; g_campaign.coins = 0;
  Ship& mailShip = g_ships[0]; mailShip.state = SHIP_AWAITING_DECISION;
  mailShip.cargo[0] = market_cargo(GOOD_GRAIN, 2);
  mailShip.cargo[0].kind = CARGO_CONTRACT; mailShip.cargo[0].jobKind = JOB_MAIL; mailShip.cargoCount = 1;
  g_incident = Incident{1, 0, INCIDENT_PIRATES, RISK_EVEN};
  sim_resolve_incident(2);
  check(g_incidentOutcome.treasuryDelta == 0 && g_incidentOutcome.cargoCount == 1 &&
        g_incidentOutcome.cargo[0].quantityDelta == -2 && g_incidentOutcome.cargo[0].jobKind == JOB_MAIL,
        "unaffordable passage reports forfeited mail rather than grain or an imaginary payment");
  legacy_campaign(153);
  check(!g_incidentOutcome.valid && !g_incidentOutcome.serial, "new campaigns clear the transient incident receipt");
}

static void consecutive_arrival_tests() {
  legacy_campaign(154);
  g_campaign.stageSeen = 3;
  g_campaign.tutorialDismissed = 1;
  g_campaign.coins = 10000;
  g_campaign.shipCount = 2;
  g_ships[1] = g_ships[0];
  g_ships[1].id = 2;
  g_ships[1].captainId = NO_SHIP;
  snprintf(g_ships[1].name, sizeof(g_ships[1].name), "Second Arrival");
  memset(g_contracts, 0, sizeof(g_contracts));
  for (int i = 0; i < 2; ++i) {
    const uint8_t destination = (uint8_t)(i + 1);
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
  auto arrival_notice = [](uint8_t shipIndex) {
    for (int i = 0; i < g_attentionCount; ++i)
      if (g_attention[i].kind == ATTENTION_ARRIVAL && g_attention[i].shipIndex == shipIndex)
        return i;
    return -1;
  };
  auto report_is = [](uint8_t shipIndex, uint8_t port, uint8_t units, int32_t coins) {
    return g_ui.card == CARD_ARRIVAL && g_ui.arrivalSummaryVisible &&
           g_ui.arrivalSummaryShip == shipIndex && g_ui.arrivalSummaryPort == port &&
           g_ui.arrivalSummaryDelivered == units && g_ui.arrivalSummaryCoins == coins &&
           !g_ui.arrivalSummaryWon && g_ui.selectedShip == shipIndex && g_ui.selectedPort == port;
  };
  g_ui.title = 0; g_ui.scene = SCENE_WORLD; g_ui.card = CARD_HELP;
  g_ui.selectedShip = 1; g_ui.selectedPort = 0;
  sim_advance_minutes(60);
  check(g_ships[0].state == SHIP_ARRIVED && g_ships[0].voyageEarned == 100 &&
        g_ships[0].voyageDelivered == 2 && g_ships[1].state == SHIP_SAILING,
        "first consecutive-arrival fixture delivers its actual contract");
  check(g_ui.card == CARD_HELP && g_ui.scene == SCENE_WORLD &&
        g_ui.selectedShip == 1 && g_ui.selectedPort == 0,
        "an ordinary arrival does not open a report or hijack the current card and selection");
  int firstNotice = arrival_notice(0);
  check(firstNotice >= 0, "the first completed voyage posts its own arrival notice");
  if (firstNotice < 0) return;
  attention_select((uint8_t)firstNotice);
  check(report_is(0, 1, 2, 100), "the first arrival notice opens its ship, port and actual voyage totals");
  sim_advance_minutes(60);
  check(g_ships[1].state == SHIP_ARRIVED && g_ships[1].voyageEarned == 270 &&
        g_ships[1].voyageDelivered == 3 && g_campaign.contractsCompleted == 2 &&
        g_campaign.deliveries == 5 && g_campaign.contractRevenue == 370,
        "the second consecutive arrival delivers its distinct actual contract");
  check(report_is(0, 1, 2, 100),
        "a second ship arriving preserves the first open report and selected ship and port");
  int secondNotice = arrival_notice(1);
  check(secondNotice >= 0, "the second completed voyage posts a separate arrival notice");
  if (secondNotice < 0) return;
  g_ui.card = CARD_INBOX;
  attention_select((uint8_t)secondNotice);
  check(report_is(1, 2, 3, 270), "the second arrival notice opens its own ship and voyage totals");
  g_ui.card = CARD_INBOX;
  attention_select((uint8_t)firstNotice);
  check(report_is(0, 1, 2, 100), "reopening the first notice restores its report after viewing the second ship");
  g_ui.card = CARD_INBOX;
  attention_select((uint8_t)secondNotice);
  check(report_is(1, 2, 3, 270), "switching arrival notices never borrows another ship's totals");

  // Dispatch resets the ship's voyage counters, so an older notice must not
  // present those new-voyage counters as the report of a completed arrival.
  g_ui.selectedShip = 0; g_ui.selectedPort = 1; g_ui.card = CARD_NONE;
  sim_route_clear();
  check(sim_route_add(0) && sim_dispatch(0), "the first arrival ship can begin its next voyage");
  check(g_ships[0].state == SHIP_SAILING && !g_ships[0].voyageEarned && !g_ships[0].voyageDelivered,
        "a new departure resets only that ship's voyage totals");
  g_ui.card = CARD_INBOX;
  attention_select((uint8_t)firstNotice);
  check(g_ui.card == CARD_FLEET && g_ui.selectedShip == 0,
        "a notice from a departed voyage opens its ship instead of inventing a zero-value arrival report");
  g_ui.card = CARD_INBOX;
  attention_select((uint8_t)secondNotice);
  check(report_is(1, 2, 3, 270), "another ship's departure does not invalidate the second arrival report");
}

static void docked_ship_selection_tests() {
  legacy_campaign(0xD0C4u);
  g_campaign.shipCount = 2;
  g_campaign.stageSeen = 2;
  g_ships[1] = g_ships[0];
  g_ships[0].state = SHIP_DISABLED;
  g_ui.selectedShip = 0; g_ui.selectedPort = 0;
  UiState before = g_ui;
  check(sim_selected_docked_ship() == 1,
        "a selected disabled ship cannot shadow a healthy trading ship at the same port");
  check(sim_selected_docked_ship(true) == 0,
        "harbor artwork preserves the selected damaged ship in its berth");
  check(memcmp(&before, &g_ui, sizeof(before)) == 0,
        "docked ship lookup never mutates selection during rendering");
  int oldDisabledCargo = g_ships[0].cargoCount;
  int oldHealthyCargo = g_ships[1].cargoCount;
  check(sim_market_buy((uint8_t)sim_selected_docked_ship(), GOOD_GRAIN),
        "the healthy same-port fallback can actually buy market cargo");
  check(g_ships[0].cargoCount == oldDisabledCargo && g_ships[1].cargoCount > oldHealthyCargo,
        "fallback market cargo goes only to the operational vessel");
  g_ships[0].state = SHIP_DOCKED;
  check(sim_selected_docked_ship() == 0, "a healthy selected ship retains trade priority");
  g_ui.selectedShip = 1;
  check(sim_selected_docked_ship() == 1, "trade selection does not jump to the first ship");
  g_ships[1].state = SHIP_ARRIVED;
  check(sim_selected_docked_ship() == 1, "an arrived ship can trade immediately");
  g_ships[0].state = SHIP_DISABLED;
  g_ships[1].state = SHIP_DISABLED;
  check(sim_selected_docked_ship() < 0, "a port with only damaged ships cannot trade");
  g_ships[1].state = SHIP_DOCKED; g_ships[1].currentPort = 1;
  check(sim_selected_docked_ship() < 0, "an operational ship at another port is not a fallback");
  g_ships[1].currentPort = 0; g_ships[1].state = SHIP_SAILING;
  check(sim_selected_docked_ship() < 0, "a departing ship's old currentPort cannot enable trading");
  g_ships[1].state = SHIP_AWAITING_DECISION;
  check(sim_selected_docked_ship() < 0, "a ship awaiting a decision cannot trade");
}

static void unit_tests() {
  x4_time_tests();
  market_quote_tests();
  docked_ship_selection_tests();
  route_undo_tests();
  incident_outcome_tests();
  consecutive_arrival_tests();
  for (int code = 0; code < SIM_RESULT_COUNT; ++code)
    check(sim_result_text((SimResultCode)code)[0] != 0, "every structured result has player-facing text");
  check(LANE_COUNT > 30 && LANE_COUNT <= MAX_LANES, "lane count in bounds");
  for (int i = 0; i < LANE_COUNT; ++i) {
    check(LANE_DEFS[i].a < MAX_PORTS && LANE_DEFS[i].b < MAX_PORTS, "lane endpoint valid");
    check(LANE_DEFS[i].a != LANE_DEFS[i].b && LANE_DEFS[i].days > 0, "lane has distance");
  }
  check(content_lane_between(3, 21) >= 0, "authored lane crosses wrapped map edge");
  uint32_t timerBeforeWrap = 0xFFFFFFF0u, timerAfterWrap = 0x20u;
  check(timerAfterWrap - timerBeforeWrap == 48u, "unsigned active timer survives wraparound");

  legacy_campaign(42);
  check(sim_stage() == 0, "new company starts at stage 0");
  check(!sim_feature_unlocked(FEATURE_MARKET) && !sim_feature_unlocked(FEATURE_FLEET),
        "market and fleet wait for later stages");
  check(sim_feature_stage(FEATURE_CHARTERS) == 4, "charters open at stage 4");
  check(g_campaign.shipCount == 1 && g_campaign.coins == 500, "new campaign baseline");
  check(g_campaign.capitalSpending == 0 && g_campaign.incidentNet == 0 &&
        g_ships[0].voyageEarned == 0 && g_ships[0].voyageDelivered == 0,
        "new campaign starts its ledger aggregates and voyage totals at zero");
  check(g_ports[0].unlocked && g_ports[1].unlocked && g_ports[2].unlocked && !g_ports[4].unlocked,
        "initial ports and charter lock");
  check(content_market_price(0, PORT_DEFS[0].produced[0], true) > 0, "market price positive");
  uint16_t neutralPrice = content_market_price(0, GOOD_GRAIN, true);
  g_campaign.worldEventType = EVENT_SHORTAGE;
  g_campaign.worldEventRegion = 0;
  g_campaign.worldEventGood = GOOD_GRAIN;
  check(content_market_price(0, GOOD_GRAIN, true) > neutralPrice, "shortage raises matching price");
  g_campaign.worldEventType = EVENT_NONE;

  int seasonalLane = 5;
  int windAB = sim_lane_wind((uint8_t)seasonalLane, LANE_DEFS[seasonalLane].a);
  int windBA = sim_lane_wind((uint8_t)seasonalLane, LANE_DEFS[seasonalLane].b);
  check(windAB == -windBA, "directional wind reverses by heading");
  g_campaign.worldMinutes = 90u * 1440u;
  check(sim_lane_wind((uint8_t)seasonalLane, LANE_DEFS[seasonalLane].a) != windAB,
        "season changes authored wind");
  g_campaign.worldMinutes = 0;

  g_ports[0].stock[GOOD_GRAIN] = 0;
  sim_advance_minutes(1440);
  check(g_ports[0].stock[GOOD_GRAIN] == 1, "daily stock drifts toward target");

  legacy_campaign(43);
  g_ports[3].unlocked = 1;
  sim_route_clear();
  check(!sim_route_add(3), "route rejects leg beyond endurance");
  g_ships[0].modules[0] = MODULE_LONG_CASKS + 1;
  check(sim_ship_capacity(g_ships[0]) == SHIP_CLASS_DEFS[0].hold - 1, "long casks consume hold");
  check(sim_route_add(3), "long casks extend endurance");
  sim_route_clear();
  g_ships[0].classId = 4;
  check(!sim_route_add(1), "deep draft rejects shallow port");

  legacy_campaign(430);
  g_campaign.coins = 5000;
  CargoItem fullHold{};
  fullHold.good = GOOD_GRAIN; fullHold.quantity = 4; fullHold.destination = NO_PORT;
  fullHold.kind = CARGO_MARKET; fullHold.jobKind = JOB_FREIGHT; fullHold.projectRegion = 0xFF;
  g_ships[0].cargo[0] = fullHold; g_ships[0].cargoCount = 1;
  check(sim_buy_module(MODULE_LONG_CASKS).code == SIM_NO_CAPACITY && !g_ships[0].modules[0],
        "capacity-reducing module rejects an overloaded projection");

  legacy_campaign(44);
  memset(g_contracts, 0, sizeof(g_contracts));
  Contract& deadline = g_contracts[0][0];
  deadline.id = 900; deadline.origin = 0; deadline.destination = 1;
  deadline.good = GOOD_GRAIN; deadline.quantity = 4; deadline.reward = 100;
  deadline.deadlineMinute = 1; deadline.kind = JOB_FREIGHT; deadline.active = 2;
  CargoItem cargo{};
  cargo.contractId = deadline.id; cargo.good = deadline.good; cargo.quantity = 4;
  cargo.destination = 1; cargo.kind = CARGO_CONTRACT; cargo.jobKind = JOB_FREIGHT;
  cargo.projectRegion = 0xFF;
  g_ships[0].cargo[0] = cargo; g_ships[0].cargoCount = 1;
  uint8_t directRoute[1] = { 1 };
  RouteEstimate standard = sim_estimate_route(0, directRoute, 1);
  check(standard.valid && standard.revenue == 125, "standard contract keeps reward and consolidation bonus");
  deadline.kind = JOB_MAIL; g_ships[0].cargo[0].jobKind = JOB_MAIL;
  RouteEstimate rush = sim_estimate_route(0, directRoute, 1);
  check(rush.revenue == 100, "late mail loses only its rush premium");
  g_captains[g_ships[0].captainId].trait = TRAIT_MERCHANT;
  g_captains[g_ships[0].captainId].level = 2;
  check(sim_estimate_route(0, directRoute, 1).revenue == 110, "merchant specialty improves payout");
  g_captains[g_ships[0].captainId].trait = TRAIT_NAVIGATOR;
  g_captains[g_ships[0].captainId].level = 1;
  g_ships[0].classId = 3;
  deadline.kind = JOB_FREIGHT; deadline.reward = 100; deadline.quantity = 6;
  g_ships[0].cargo[0].jobKind = JOB_FREIGHT; g_ships[0].cargo[0].quantity = 6;
  Contract& passenger = g_contracts[0][1];
  passenger.id = 902; passenger.origin = 0; passenger.destination = 1;
  passenger.good = GOOD_GRAIN; passenger.quantity = 1; passenger.reward = 100;
  passenger.kind = JOB_PASSENGER; passenger.active = 2;
  CargoItem passengerCargo{};
  passengerCargo.contractId = passenger.id; passengerCargo.good = passenger.good;
  passengerCargo.quantity = 1; passengerCargo.destination = 1;
  passengerCargo.kind = CARGO_CONTRACT; passengerCargo.jobKind = JOB_PASSENGER;
  passengerCargo.projectRegion = 0xFF;
  g_ships[0].cargo[1] = passengerCargo; g_ships[0].cargoCount = 2;
  check(sim_estimate_route(0, directRoute, 1).revenue == 200,
        "a partially used passenger pool prevents consolidation despite a full hold");
  g_ships[0].cargoCount = 1;
  check(sim_estimate_route(0, directRoute, 1).revenue == 125,
        "an unused passenger pool does not reduce freight consolidation eligibility");

  legacy_campaign(440);
  memset(g_contracts, 0, sizeof(g_contracts));
  Contract& forfeited = g_contracts[0][0];
  forfeited.id = 901; forfeited.origin = 0; forfeited.destination = 1;
  forfeited.good = GOOD_TIMBER; forfeited.quantity = 2; forfeited.reward = 80;
  forfeited.active = 2;
  CargoItem contractCargo{};
  contractCargo.contractId = forfeited.id; contractCargo.good = forfeited.good;
  contractCargo.quantity = forfeited.quantity; contractCargo.destination = 1;
  contractCargo.kind = CARGO_CONTRACT; contractCargo.jobKind = JOB_FREIGHT;
  contractCargo.projectRegion = 0xFF;
  g_ships[0].cargo[0] = contractCargo; g_ships[0].cargoCount = 1;
  g_campaign.coins = 0; g_ships[0].state = SHIP_AWAITING_DECISION;
  g_incident = Incident{1, 0, INCIDENT_PIRATES, RISK_DANGEROUS};
  sim_resolve_incident(2);
  check(forfeited.active == 0 && g_ships[0].cargoCount == 0,
        "jettisoning contract cargo releases its board record");

  legacy_campaign(439);
  CargoItem marketStack{};
  marketStack.good = GOOD_TIMBER; marketStack.quantity = 3; marketStack.kind = CARGO_MARKET;
  marketStack.jobKind = JOB_FREIGHT; marketStack.destination = NO_PORT;
  marketStack.projectRegion = 0xFF;
  g_ships[0].cargo[0] = marketStack; g_ships[0].cargoCount = 1;
  g_campaign.coins = 0; g_ships[0].state = SHIP_AWAITING_DECISION;
  g_incident = Incident{1, 0, INCIDENT_PIRATES, RISK_DANGEROUS};
  sim_resolve_incident(2);
  check(g_ships[0].cargoCount == 1 && g_ships[0].cargo[0].quantity == 2,
        "jettisoning market cargo removes one unit rather than the complete stack");

  legacy_campaign(441);
  uint32_t availableId = g_contracts[0][2].id;
  uint32_t loadedId = g_contracts[0][3].id;
  g_contracts[0][3].active = 2;
  sim_advance_minutes(1440);
  {
    // Boards are compacted (open offers first), so look the contracts up by id.
    bool availableGone = true, loadedKept = false;
    for (int i = 0; i < MAX_CONTRACTS_PER_PORT; ++i) {
      if (g_contracts[0][i].id == availableId && g_contracts[0][i].active) availableGone = false;
      if (g_contracts[0][i].id == loadedId && g_contracts[0][i].active == 2) loadedKept = true;
    }
    check(availableGone && loadedKept, "daily refresh replaces available jobs and preserves loaded jobs");
  }
  int projectCount = 0;
  for (int i = 0; i < 8; ++i) projectCount += g_contracts[0][i].kind == JOB_CHARTER;
  check(projectCount == 0, "no charter offer before charters open");
  g_campaign.stageSeen = 4;  // charters open at stage 4
  sim_generate_jobs(0, true);
  projectCount = 0;
  for (int i = 0; i < 8; ++i) projectCount += g_contracts[0][i].kind == JOB_CHARTER;
  check(projectCount == 1, "first frontier reserves exactly one project slot");
  g_campaign.regionMask = g_campaign.chartMask = 0x03;
  for (int p = 0; p < MAX_PORTS; ++p)
    if (PORT_DEFS[p].region == 1) g_ports[p].unlocked = 1;
  sim_generate_jobs(0, true);
  projectCount = 0;
  for (int i = 0; i < 8; ++i) projectCount += g_contracts[0][i].kind == JOB_CHARTER;
  check(projectCount == 2, "later frontiers reserve exactly two project slots");

  uint32_t pausedAt = g_campaign.worldMinutes;
  g_campaign.paused = 1;
  sim_advance_minutes(600);
  check(g_campaign.worldMinutes == pausedAt, "pause freezes campaign time");

  legacy_campaign(45);
  g_campaign.coins = 20000; g_campaign.rank = 5; g_ports[0].shipyardLevel = 3;
  check(sim_buy_ship(0) && sim_buy_ship(0), "fleet expands to three ships");
  check(g_campaign.shipCount == 3, "three-ship acceptance fleet");
  for (int i = 0; i < 3; ++i) {
    Ship& queued = g_ships[i];
    queued.state = SHIP_SAILING; queued.currentPort = 0; queued.fromPort = 0; queued.toPort = 1;
    queued.legElapsedMinutes = 0; queued.legTotalMinutes = 100;
    queued.incidentArmed = 1; queued.incidentType = INCIDENT_INSPECTION;
  }
  sim_advance_minutes(60);
  check(g_incident.active && g_incidentQueueCount == 2 && g_ui.card != CARD_INCIDENT &&
        attention_unread_count() >= 3,
        "simultaneous incidents queue in the inbox without stealing focus");
  while (g_incident.active) sim_resolve_incident(0);
  check(!g_incidentQueueCount && g_campaign.incidentsResolved == 3, "queued incidents resolve in order");

  g_attentionCount = 0;
  attention_add(ATTENTION_ARRIVAL, 0, 0, false);
  attention_add(ATTENTION_ARRIVAL, 0, 0, false);
  check(g_attentionCount == 1 && g_attention[0].unread,
        "duplicate informational attention entries coalesce and become unread");
  g_attention[0].unread = 0;
  for (int i = 1; i < MAX_ATTENTION; ++i)
    g_attention[i] = AttentionEntry{(uint32_t)(i + 1), (uint32_t)i,
      ATTENTION_EVENT, NO_SHIP, (uint8_t)i, 0, 1, 0, 0xFF};
  g_attentionCount = MAX_ATTENTION;
  attention_add(ATTENTION_INCIDENT, 0, 0, true);
  bool urgentRetained = false;
  for (int i = 0; i < g_attentionCount; ++i)
    urgentRetained |= g_attention[i].kind == ATTENTION_INCIDENT && g_attention[i].urgent;
  check(g_attentionCount == MAX_ATTENTION && urgentRetained,
        "full attention inbox evicts a read informational entry before an urgent incident");

  for (int i = 0; i < 2; ++i) {
    Ship& arriving = g_ships[i];
    arriving.state = SHIP_SAILING; arriving.currentPort = 0;
    arriving.fromPort = 0; arriving.toPort = 1;
    arriving.legElapsedMinutes = 60; arriving.legTotalMinutes = 120;
    arriving.incidentArmed = 0; arriving.routeCount = 1; arriving.routePos = 0;
    arriving.route[0] = 1;
  }
  sim_advance_minutes(60);
  check(g_ships[0].currentPort == 1 && g_ships[1].currentPort == 1,
        "simultaneous arrivals both complete");

  legacy_campaign(46);
  g_campaign.coins = 5000;
  uint8_t officeBefore = g_ports[0].officeLevel;
  check(sim_buy_port_upgrade(0) && g_ports[0].officeLevel == officeBefore + 1,
        "port office upgrade");
  check(sim_purchase_chart(1), "regional chart purchase");
  g_campaign.charterProgress[1] = 6;
  check(sim_purchase_charter(1), "regional charter purchase");
  check(g_ports[4].unlocked && g_ports[7].unlocked, "new region ports unlock");
  g_ships[0].routeTemplate.origin = 0;
  g_ships[0].routeTemplate.stops[0] = 1; g_ships[0].routeTemplate.stopCount = 1;
  g_ui.selectedShip = 0; sim_route_clear();
  check(sim_route_recall() && g_ui.routeCount == 1, "route template recall is available before victory");

  legacy_campaign(47);
  g_campaign.coins = 10000; g_campaign.rank = 5; g_ports[0].shipyardLevel = 3;
  check(sim_buy_ship(0), "second ship for transfer test");
  CargoItem transfer{};
  transfer.contractId = 1234; transfer.good = GOOD_TIMBER; transfer.quantity = 2;
  transfer.destination = 2; transfer.kind = CARGO_CONTRACT; transfer.jobKind = JOB_FREIGHT;
  transfer.projectRegion = 0xFF;
  g_ships[0].cargo[0] = transfer; g_ships[0].cargoCount = 1;
  check(sim_transfer_to_warehouse(0, 0) && sim_transfer_from_warehouse(1, 0),
        "contract transfers between ships through warehouse");
  g_ships[1].state = SHIP_AWAITING_DECISION;
  g_incident = Incident{1, 1, INCIDENT_PIRATES, RISK_EVEN};
  uint16_t incidentsBefore = g_campaign.incidentsResolved;
  sim_resolve_incident(2);
  check(g_campaign.incidentsResolved == incidentsBefore + 1 &&
        g_ships[1].state != SHIP_AWAITING_DECISION, "pirate choice returns ship to service");
  g_ui.selectedShip = 0;
  g_ui.selectedPort = 0;
  int32_t before = g_campaign.coins;
  uint8_t tradeGood = PORT_DEFS[0].produced[0];
  check(sim_market_buy(0, tradeGood), "market buy");
  check(sim_market_sell(0, tradeGood), "market sell");
  check(g_campaign.coins < before, "same-port trade loses spread");

  bool loaded = false;
  for (int i = 0; i < MAX_CONTRACTS_PER_PORT && !loaded; ++i) loaded = sim_load_contract(0, 0, i);
  check(loaded, "load first compatible contract");
  if (g_ships[0].cargoCount) {
    check(sim_transfer_to_warehouse(0, 0), "store layover cargo");
    check(sim_transfer_from_warehouse(0, 0), "retrieve layover cargo");
  }
  uint8_t target = g_ships[0].cargoCount ? g_ships[0].cargo[0].destination : 1;
  uint8_t route[MAX_ROUTE_LEGS];
  int n = path_to(g_ships[0], target, route);
  check(n > 0, "contract destination reachable");
  sim_route_clear();
  for (int i = 0; i < n; ++i) check(sim_route_add(route[i]), "route leg accepted");
  RouteEstimate estimate = sim_estimate_route(0, g_ui.routeDraft, g_ui.routeCount);
  check(estimate.valid && estimate.cost > 0, "route estimate valid");
  check(sim_dispatch(0), "dispatch succeeds");
  int guard = 0;
  while (g_ships[0].state != SHIP_DOCKED && g_ships[0].state != SHIP_ARRIVED && guard++ < 1000) {
    if (g_incident.active) sim_resolve_incident(0);
    sim_advance_minutes(360);
  }
  check(guard < 1000, "voyage terminates");
  check(invariant_ok(), "post-voyage invariants");

  legacy_campaign(99);
  g_ships[0].state = SHIP_DISABLED;
  g_ships[0].condition = 20;
  g_campaign.coins = 0;
  check(sim_claim_recovery(), "relief ship prevents deadlock");
  check(g_campaign.shipCount == 1 && g_ships[0].state == SHIP_DOCKED &&
        g_ships[0].condition == BALANCE.recoveryCondition && g_campaign.recoveryDebt > 0,
        "relief restores the cheapest disabled ship and records debt");

  legacy_campaign(100);
  g_campaign.coins = 0;
  CargoItem liquid{}; liquid.good = GOOD_PORCELAIN; liquid.quantity = 2;
  liquid.kind = CARGO_MARKET; liquid.jobKind = JOB_FREIGHT; liquid.paidUnitPrice = 1;
  liquid.destination = NO_PORT; liquid.projectRegion = 0xFF;
  g_ships[0].cargo[0] = liquid; g_ships[0].cargoCount = 1;
  check(sim_claim_recovery().code == SIM_DEBT_UNAVAILABLE,
        "saleable cargo prevents an unnecessary relief advance");

  legacy_campaign(101);
  g_ships[0].routeTemplate.origin = 0;
  g_ships[0].routeTemplate.stops[0] = 1;
  g_ships[0].routeTemplate.stopCount = 1;
  g_ships[0].routeTemplate.loadMode = ROUTE_LOAD_ENDPOINT;
  g_ships[0].routeTemplate.sellMode = ROUTE_SELL_PROFIT;
  g_ui.selectedShip = 0;
  check(sim_route_prepare_preview(0), "route preparation creates a valid preview");
  check(g_routePreparation.computed && g_routePreparation.etaDays > 0 &&
        g_routePreparation.holdUsed <= g_routePreparation.holdCapacity &&
        g_routePreparation.berthsUsed <= g_routePreparation.berthCapacity,
        "route preview exposes direction, capacity, ETA, risk, and treasury inputs");
  check(sim_route_prepare_apply(0) && g_ships[0].state == SHIP_DOCKED && g_ui.routeCount == 1,
        "route preparation applies without dispatching");
  check(sim_route_prepare_preview(0), "route preparation can be previewed again");
  ++g_ports[0].stock[GOOD_GRAIN];
  check(sim_route_prepare_apply(0).code == SIM_STALE_PREVIEW,
        "route preparation rejects a stale preview atomically");
  check(g_routePreparation.computed,
        "a stale route preparation is recomputed for review instead of discarded");

  legacy_campaign(102);
  uint16_t noDuty = GOOD_DEFS[PORT_DEFS[1].specialty].basePrice;
  check(PORT_DEFS[0].duty == 5 && content_market_price(0, PORT_DEFS[0].specialty, true) > 0 &&
        noDuty > 0, "port duties and specialties are authored");
  uint8_t historyBefore = g_ports[0].historyHead;
  sim_advance_minutes(1440);
  check(g_ports[0].historyHead == (historyBefore + 1) % 7, "daily market history advances");

  legacy_campaign(103);
  g_campaign.coins = 5000;
  g_ports[0].officeLevel = 2;
  CaptainProfile firstCandidate{}, secondCandidate{}, nextWeekCandidate{};
  check(sim_recruit_candidate(0, 0, &firstCandidate) &&
        sim_recruit_candidate(0, 1, &secondCandidate) &&
        strcmp(firstCandidate.name, secondCandidate.name),
        "level-two office exposes two deterministic candidates");
  ++g_campaign.recruitmentWeek;
  check(sim_recruit_candidate(0, 0, &nextWeekCandidate) &&
        (strcmp(firstCandidate.name, nextWeekCandidate.name) ||
         firstCandidate.trait != nextWeekCandidate.trait),
        "recruitment candidates refresh deterministically each week");
  --g_campaign.recruitmentWeek;
  SimResult hired = sim_hire_captain(0);
  check(hired && hired.value > 0 && g_captains[hired.value].assignedShip == NO_SHIP,
        "office recruitment creates a deterministic reserve captain");
  g_campaign.rank = 5; g_ports[0].shipyardLevel = 3;
  SimResult bought = sim_buy_ship(0);
  check(bought && !sim_ship_captain(1) && sim_dispatch(1).code == SIM_CAPTAIN_REQUIRED,
        "purchased ships remain docked until a captain is assigned");
  check(sim_assign_captain((uint8_t)hired.value, 1),
        "a docked reserve captain can be assigned to a ship");
  g_ships[1].condition = 40; g_ships[1].state = SHIP_AWAITING_DECISION;
  g_incident = Incident{1, 1, INCIDENT_STORM, RISK_DANGEROUS};
  sim_resolve_incident(0);
  check(g_captains[hired.value].loyalty <= 100 && g_captains[hired.value].scarCount == 1 &&
        g_captains[hired.value].scars[0] == SCAR_STORM,
        "dangerous incident survival updates loyalty and a bounded matching scar");

  legacy_campaign(104);
  g_campaign.coins = 0;
  check(sim_market_buy(0, GOOD_GRAIN).code == SIM_NO_FUNDS,
        "structured action result identifies insufficient funds");

  legacy_campaign(105);
  check(sim_tutorial_complete(TUTORIAL_ROUTE) && g_campaign.tutorialStep == TUTORIAL_PORT,
        "FTUE remembers an out-of-order route without skipping earlier guidance");
  check(sim_tutorial_complete(TUTORIAL_PORT) &&
        sim_tutorial_complete(TUTORIAL_CONTRACT) &&
        sim_tutorial_complete(TUTORIAL_CARGO) &&
        g_campaign.tutorialStep == TUTORIAL_DISPATCH,
        "FTUE advances across an already completed later milestone");
  g_ships[0].state = SHIP_SAILING;
  g_ships[0].routeCount = 1;
  sim_tutorial_reconcile();
  check(g_campaign.tutorialStep == TUTORIAL_ARRIVAL &&
        (g_campaign.tutorialFlags & (1u << TUTORIAL_DISPATCH)),
        "FTUE reconciles an early departure instead of requesting another sail");
  sim_tutorial_complete(TUTORIAL_FLEET);
  check(g_campaign.tutorialStep == TUTORIAL_ARRIVAL,
        "FTUE retains early fleet use while waiting on arrival");
  g_ships[0].state = SHIP_ARRIVED;
  g_ships[0].currentPort = 1;
  sim_tutorial_reconcile();
  check(g_campaign.tutorialStep == TUTORIAL_MARKET,
        "FTUE resumes at the next actionable step after arrival");
  sim_tutorial_complete(TUTORIAL_MARKET);
  check(g_campaign.tutorialStep == TUTORIAL_COUNT,
        "FTUE completes after all independently recorded milestones");

  // Stage locks hold across a load: an older save cannot bring back what
  // the derived stage hides, and an unlocked company keeps everything.
  {
    auto count_kind = [](uint8_t kind) {
      int n = 0;
      for (int i = 0; i < g_attentionCount; ++i) if (g_attention[i].kind == kind) ++n;
      return n;
    };
    auto open_charters = []() {
      int n = 0;
      for (int p = 0; p < MAX_PORTS; ++p)
        for (int i = 0; i < MAX_CONTRACTS_PER_PORT; ++i)
          if (g_contracts[p][i].active == 1 && g_contracts[p][i].kind == JOB_CHARTER) ++n;
      return n;
    };
    auto plant_locked_state = []() {
      g_campaign.worldEventType = EVENT_SHORTAGE; g_campaign.worldEventGood = GOOD_GRAIN;
      g_campaign.worldEventRegion = 0; g_campaign.worldEventDays = 3;
      attention_add(ATTENTION_EVENT, NO_SHIP, NO_PORT, false);
      Ship& ship = g_ships[0];
      ship.state = SHIP_AWAITING_DECISION; ship.incidentArmed = 1;
      ship.legTotalMinutes = 1440; ship.legElapsedMinutes = 720;
      g_incident = Incident{1, 0, INCIDENT_STORM, RISK_EVEN};
      attention_add(ATTENTION_INCIDENT, 0, 0, true);
      g_incidentQueue[0] = Incident{1, 0, INCIDENT_REEF, RISK_EVEN}; g_incidentQueueCount = 1;
      Contract& c = g_contracts[0][MAX_CONTRACTS_PER_PORT - 1];
      memset(&c, 0, sizeof(c));
      c.id = g_campaign.nextContractId++; c.origin = 0; c.destination = 1; c.good = GOOD_TIMBER;
      c.quantity = 2; c.kind = JOB_CHARTER; c.projectRegion = 1; c.reward = 300; c.active = 1;
    };
    legacy_campaign(7);
    plant_locked_state();
    sim_stage_reconcile(false); sim_scrub_locked_state();  // the legacy half of a load
    check(g_campaign.worldEventType == EVENT_NONE && g_campaign.worldEventDays == 0,
          "load clears a world event the stage has not opened");
    check(!g_incident.active && g_incidentQueueCount == 0 && !g_ships[0].incidentArmed &&
          g_ships[0].state == SHIP_SAILING, "load clears a locked incident and frees the ship");
    check(count_kind(ATTENTION_EVENT) == 0 && count_kind(ATTENTION_INCIDENT) == 0,
          "load drops notices of locked features");
    check(open_charters() == 0, "load clears charter offers before stage 4");
    for (int d = 0; d < 10; ++d) sim_advance_minutes(1440);
    check(count_kind(ATTENTION_EVENT) == 0, "no event notice follows while EVENT is locked");

    legacy_campaign(7);
    g_campaign.stageSeen = 4;  // everything below Mastery is open
    plant_locked_state();
    int eventsBefore = count_kind(ATTENTION_EVENT), incidentsBefore = count_kind(ATTENTION_INCIDENT);
    sim_stage_reconcile(false); sim_scrub_locked_state();  // the legacy half of a load
    check(g_campaign.worldEventType == EVENT_SHORTAGE && g_incident.active &&
          g_ships[0].state == SHIP_AWAITING_DECISION && open_charters() == 1 &&
          count_kind(ATTENTION_EVENT) == eventsBefore && count_kind(ATTENTION_INCIDENT) == incidentsBefore,
          "an unlocked company keeps its event, incident, charter offer and notices");

    int charterOffers = 0, eventRows = 0;
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
      legacy_campaign(seed);
      charterOffers += open_charters();
      if (seed <= 100) {
        for (int d = 0; d < 30; ++d) sim_advance_minutes(1440);
        eventRows += count_kind(ATTENTION_EVENT);
      }
    }
    check(charterOffers == 0, "no company starts with a charter offer");
    check(eventRows == 0, "no event notice before EVENT opens");

    // An accepted charter carried by an older save sails on as plain freight:
    // it pays, but it cannot credit the locked charter track or post a notice.
    legacy_campaign(11);
    uint8_t dest = 0xFF;
    for (uint8_t p = 1; p < 3 && dest == 0xFF; ++p) if (content_lane_between(0, p) >= 0) dest = p;
    check(dest != 0xFF, "home port has a neighbour for the charter test");
    Contract& taken = g_contracts[0][MAX_CONTRACTS_PER_PORT - 1];
    memset(&taken, 0, sizeof(taken));
    taken.id = g_campaign.nextContractId++; taken.origin = 0; taken.destination = dest;
    taken.good = GOOD_TIMBER; taken.quantity = 2; taken.kind = JOB_CHARTER; taken.projectRegion = 1;
    taken.reward = 300; taken.active = 2;
    CargoItem carried{};
    carried.good = GOOD_TIMBER; carried.quantity = 2; carried.destination = dest;
    carried.kind = CARGO_CONTRACT; carried.jobKind = JOB_CHARTER; carried.projectRegion = 1;
    carried.contractId = taken.id;
    g_ships[0].cargo[g_ships[0].cargoCount++] = carried;
    uint16_t progressBefore = g_campaign.charterProgress[1];
    sim_stage_reconcile(false); sim_scrub_locked_state();  // the legacy half of a load
    check(g_ships[0].cargo[0].projectRegion == 0xFF && g_ships[0].cargo[0].jobKind == JOB_FREIGHT,
          "load turns a carried locked charter into plain freight");
    sim_route_clear();
    check(sim_route_add(dest) && sim_dispatch(0), "the plain-freight charter still sails");
    sim_advance_minutes(g_ships[0].legTotalMinutes + 1);
    check(g_campaign.charterProgress[1] == progressBefore && count_kind(ATTENTION_CHARTER) == 0,
          "delivering it credits no locked charter track and posts no notice");

    // Notices hygiene: settled incidents clear, the cap leaves a trace, a
    // handled incident posts nothing, a stale arrival shows the ship, charter
    // notices keep their regions, and a promoted incident comes back unread.
    auto latest_journal = []() {
      return g_journalCount ? g_journal[(g_journalHead + MAX_JOURNAL - 1) % MAX_JOURNAL].text : "";
    };
    legacy_campaign(13);
    g_campaign.stageSeen = 2;
    for (int k = 0; k < 20; ++k) {
      g_ships[0].state = SHIP_AWAITING_DECISION;
      g_incident = Incident{1, 0, INCIDENT_STORM, RISK_EVEN};
      attention_add(ATTENTION_INCIDENT, 0, 0, true);
      sim_resolve_incident(0);
    }
    int urgentLeft = 0;
    for (int i = 0; i < g_attentionCount; ++i) urgentLeft += g_attention[i].urgent;
    for (int i = 0; i < g_attentionCount; ++i) g_attention[i].unread = 0;
    attention_clear_read();
    check(urgentLeft == 0 && g_attentionCount == 0, "twenty settled incidents leave no urgent rows and clear");

    legacy_campaign(13);
    for (int i = 0; i < MAX_ATTENTION; ++i)
      g_attention[i] = AttentionEntry{(uint32_t)(i + 1), (uint32_t)i, ATTENTION_EVENT, NO_SHIP,
                                      (uint8_t)i, 0, 1, 0, 0xFF};
    g_attentionCount = MAX_ATTENTION;
    attention_add(ATTENTION_INCIDENT, 0, 0, true);
    check(strstr(latest_journal(), "dropped") != nullptr, "dropping an unread notice leaves a journal line");

    legacy_campaign(13);
    g_campaign.stageSeen = 2;
    g_campaign.coins = 100000;
    check(sim_buy_ship(0), "second ship for the queue-overflow test");
    g_ships[0].state = SHIP_AWAITING_DECISION;
    g_incident = Incident{1, 0, INCIDENT_STORM, RISK_EVEN};
    for (int q = 0; q < MAX_INCIDENTS; ++q) g_incidentQueue[q] = Incident{1, 0, INCIDENT_REEF, RISK_EVEN};
    g_incidentQueueCount = MAX_INCIDENTS;
    Ship& second = g_ships[1];
    second.state = SHIP_SAILING; second.legTotalMinutes = 1440; second.legElapsedMinutes = 0;
    second.incidentArmed = 1; second.incidentType = INCIDENT_STORM;
    second.routeCount = 1; second.route[0] = dest; second.routePos = 0; second.toPort = dest;
    sim_advance_minutes(800);
    bool phantom = false;
    for (int i = 0; i < g_attentionCount; ++i)
      phantom |= g_attention[i].kind == ATTENTION_INCIDENT && g_attention[i].shipIndex == 1;
    check(second.state == SHIP_SAILING && !phantom, "a handled incident with a full queue posts no notice");

    legacy_campaign(13);
    g_ui.arrivalSummaryVisible = 0;
    attention_add(ATTENTION_ARRIVAL, 0, 0, false);
    attention_select(0);
    check(g_ui.card == CARD_FLEET, "a stale arrival notice shows the ship's fleet row");

    legacy_campaign(13);
    attention_add(ATTENTION_CHARTER, NO_SHIP, 0, false, 1);
    attention_add(ATTENTION_CHARTER, NO_SHIP, 0, false, 2);
    attention_add(ATTENTION_CHARTER, NO_SHIP, 0, false, 1);
    check(count_kind(ATTENTION_CHARTER) == 2, "charter notices for two regions stay apart, one region coalesces");

    legacy_campaign(13);
    g_campaign.stageSeen = 2;
    g_campaign.coins = 100000;
    check(sim_buy_ship(0), "second ship for the promotion test");
    g_ships[0].state = SHIP_AWAITING_DECISION; g_ships[1].state = SHIP_AWAITING_DECISION;
    g_incident = Incident{1, 0, INCIDENT_STORM, RISK_EVEN};
    g_incidentQueue[0] = Incident{1, 1, INCIDENT_STORM, RISK_EVEN}; g_incidentQueueCount = 1;
    attention_add(ATTENTION_INCIDENT, 0, 0, true);
    attention_add(ATTENTION_INCIDENT, 1, 0, true);
    for (int i = 0; i < g_attentionCount; ++i) if (g_attention[i].shipIndex == 1) g_attention[i].unread = 0;
    sim_resolve_incident(0);
    bool promotedUnread = false;
    for (int i = 0; i < g_attentionCount; ++i)
      if (g_attention[i].kind == ATTENTION_INCIDENT && g_attention[i].shipIndex == 1 && !g_attention[i].resolved)
        promotedUnread = g_attention[i].unread;
    check(g_incident.active && g_incident.shipIndex == 1 && promotedUnread,
          "a promoted queued incident brings its notice back unread");

    for (int i = 0; i < g_attentionCount; ++i) if (g_attention[i].shipIndex == 0) g_attention[i].unread = 0;
    int unreadRows = 0;
    for (int i = 0; i < g_attentionCount; ++i) unreadRows += g_attention[i].unread;
    g_ui.attentionUnreadOnly = 1;
    check(attention_visible_count() == unreadRows && unreadRows < g_attentionCount,
          "visible count follows the unread filter");
    g_ui.attentionUnreadOnly = 0;

    // An older save can hold a decision notice for a ship with no incident.
    legacy_campaign(13);
    g_campaign.stageSeen = 2;
    g_ships[0].state = SHIP_SAILING;
    attention_add(ATTENTION_INCIDENT, 0, 0, true);
    sim_after_load();
    check(count_kind(ATTENTION_INCIDENT) == 0, "load drops a decision notice with no incident behind it");
  }

  // Economy: the arrival report sums every leg, the preview nets out recovery
  // debt the way delivery does, and the ledger reconciles with the treasury.
  {
    auto plant = [](uint8_t dest, uint16_t reward) {
      Contract* slot = nullptr;
      for (int i = MAX_CONTRACTS_PER_PORT - 1; i >= 0 && !slot; --i)
        if (!g_contracts[0][i].active) slot = &g_contracts[0][i];
      memset(slot, 0, sizeof(*slot));
      slot->id = g_campaign.nextContractId++; slot->origin = 0; slot->destination = dest;
      slot->good = GOOD_GRAIN; slot->quantity = 1; slot->kind = JOB_FREIGHT; slot->projectRegion = 0xFF;
      slot->reward = reward; slot->active = 2;
      CargoItem item{};
      item.good = GOOD_GRAIN; item.quantity = 1; item.destination = dest; item.kind = CARGO_CONTRACT;
      item.jobKind = JOB_FREIGHT; item.projectRegion = 0xFF; item.contractId = slot->id;
      g_ships[0].cargo[g_ships[0].cargoCount++] = item;
    };
    legacy_campaign(17);  // stage 0: no incidents can interrupt the voyage
    uint8_t first = 0xFF, second = 0xFF;
    for (uint8_t p = 1; p < 3 && first == 0xFF; ++p) if (content_lane_between(0, p) >= 0) first = p;
    for (uint8_t p = 1; p < 3 && second == 0xFF; ++p)
      if (p != first && content_lane_between(first, p) >= 0) second = p;
    if (second == 0xFF) second = 0;
    plant(first, 100);
    plant(second, 150);
    sim_route_clear();
    check(sim_route_add(first) && sim_route_add(second) && sim_dispatch(0), "two-leg voyage sails");
    sim_advance_minutes(g_ships[0].legTotalMinutes + 1);
    check(g_ships[0].state == SHIP_SAILING && g_ships[0].voyageEarned == 100, "first stop banks its delivery");
    sim_advance_minutes(g_ships[0].legTotalMinutes + 1);
    check(g_ui.arrivalSummaryVisible && g_ui.arrivalSummaryCoins == 250 && g_ui.arrivalSummaryDelivered == 2,
          "the arrival report sums both legs");

    legacy_campaign(17);
    g_campaign.recoveryDebt = 1000;
    plant(first, 200);
    sim_route_clear();
    check(sim_route_add(first), "single-leg route for the debt test");
    RouteEstimate withDebt = sim_estimate_route(0, g_ui.routeDraft, g_ui.routeCount);
    check(withDebt.valid && withDebt.revenue == 200 && withDebt.netRevenue == 150 &&
          withDebt.netProfit == withDebt.profit - 50, "the preview nets out recovery-debt withholding");
    check(sim_dispatch(0), "debt voyage sails");
    sim_advance_minutes(g_ships[0].legTotalMinutes + 1);
    check(g_ui.arrivalSummaryCoins == 150 && g_campaign.recoveryDebt == 950, "delivery pays what the preview said");

    legacy_campaign(17);
    g_campaign.stageSeen = 4;
    g_campaign.coins = 100000;
    int32_t start = g_campaign.coins;
    g_ships[0].condition = 50;
    check(sim_buy_ship(0), "ledger test buys a ship");
    check(sim_repair_ship(0), "ledger test repairs a ship");
    check(sim_purchase_chart(1), "ledger test buys a chart");
    g_ships[0].state = SHIP_AWAITING_DECISION;
    g_incident = Incident{1, 0, INCIDENT_PIRATES, RISK_EVEN};
    sim_resolve_incident(2);
    int32_t expectedCapital = SHIP_CLASS_DEFS[0].cost + 50 * BALANCE.repairCostPerPoint + BALANCE.chartCost[1];
    check(g_campaign.capitalSpending == expectedCapital && g_campaign.incidentNet == -70,
          "capital spending and incident net record every coin they cover");
    check(g_campaign.coins - start == -g_campaign.capitalSpending + g_campaign.incidentNet,
          "the treasury reconciles with the ledger aggregates");

    legacy_campaign(17);
    g_campaign.coins = 0;
    g_ships[0].state = SHIP_AWAITING_DECISION;
    g_incident = Incident{1, 0, INCIDENT_TRADE, RISK_EVEN};
    sim_resolve_incident(0);
    check(strstr(g_journal[(g_journalHead + MAX_JOURNAL - 1) % MAX_JOURNAL].text, "could not afford") != nullptr,
          "a refused trade at sea says why");
  }

  // Incident arithmetic: the leg clock cannot wrap, mail and passengers carry
  // no hidden trade good, illness is worse with passengers aboard, and a
  // carried charter is named as one.
  {
    // Find the lane, direction and season where the slowest hull would need
    // the most days by the uncapped formula, then check the clock stops at 45.
    legacy_campaign(19);
    g_campaign.coins = 100000;
    g_ships[0].classId = 4;  // the slowest hull
    int captainBonus = 0;
    if (CaptainProfile* cp = sim_ship_captain(0))
      if (cp->trait == TRAIT_NAVIGATOR) captainBonus = 4 + cp->level * 2;
    int speed = SHIP_CLASS_DEFS[4].speed + captainBonus;
    int worstLane = -1, worstDays = 0, worstSeason = 0;
    uint8_t worstFrom = 0;
    for (int season = 0; season < 2; ++season) {
      g_campaign.worldMinutes = (uint32_t)season * 90u * 1440u;
      for (int i = 0; i < LANE_COUNT; ++i) {
        const SeaLaneDef& l = LANE_DEFS[i];
        if (l.days > SHIP_CLASS_DEFS[4].enduranceDays) continue;
        if (PORT_DEFS[l.a].portClass < SHIP_CLASS_DEFS[4].draft ||
            PORT_DEFS[l.b].portClass < SHIP_CLASS_DEFS[4].draft) continue;
        for (int dir = 0; dir < 2; ++dir) {
          uint8_t from = dir ? l.b : l.a;
          int wind = sim_lane_wind((uint8_t)i, from);
          int effective = speed * (100 + wind) / 100;
          if (effective < 50) effective = 50;
          int days = (l.days * 100 + effective - 1) / effective;
          if (days > worstDays) { worstDays = days; worstLane = i; worstFrom = from; worstSeason = season; }
        }
      }
    }
    check(worstDays > 45, "some lane asks the slowest hull for more than 45 uncapped days");
    g_campaign.worldMinutes = (uint32_t)worstSeason * 90u * 1440u;
    g_campaign.lastMarketDay = (uint16_t)(g_campaign.worldMinutes / 1440u);
    const SeaLaneDef& worst = LANE_DEFS[worstLane];
    uint8_t worstTo = worstFrom == worst.a ? worst.b : worst.a;
    g_ships[0].currentPort = worstFrom;
    g_ports[worst.a].unlocked = g_ports[worst.b].unlocked = 1;
    g_ui.selectedShip = 0; g_ui.selectedPort = worstFrom;
    sim_route_clear();
    check(sim_route_add(worstTo), "the worst lane is routable");
    RouteEstimate longHaul = sim_estimate_route(0, g_ui.routeDraft, g_ui.routeCount);
    check(longHaul.valid && sim_dispatch(0), "the worst voyage sails");
    check(g_ships[0].legTotalMinutes == 45u * 1440u && longHaul.days == 45,
          "the leg clock stops at 45 days where the formula asks for more");
    check(sim_leg_days(0, worstLane, worstFrom) == 45, "the route card's per-ship days agree with the voyage");
    attention_add(ATTENTION_INCIDENT, 0, 0, true);
    attention_add(ATTENTION_ARRIVAL, 0, 0, false);
    check(attention_urgent_unread_count() == 1 && attention_unread_count() == 2,
          "the urgent count singles out decisions");
    char progress[48];
    legacy_campaign(19);
    sim_stage_progress(3, progress, sizeof(progress));
    check(!strcmp(progress, "1 of 2 ships"), "stage progress counts ships");
    g_campaign.companyXp = 4;
    sim_stage_progress(2, progress, sizeof(progress));
    check(strstr(progress, "4 of ") != nullptr && strstr(progress, "XP to Broker") != nullptr, "stage progress counts XP toward the rank");

    // The Grand Charter's pennant is earned, then fitted for a hold, a gun and speed.
    legacy_campaign(19);
    g_campaign.coins = 100000; g_ui.selectedShip = 0;
    int holdBefore = sim_ship_capacity(g_ships[0]);
    check(sim_buy_module(MODULE_CHARTER_PENNANT).code == SIM_LOCKED, "the pennant is not for sale before the win");
    g_campaign.won = 1;
    check(sim_buy_module(MODULE_CHARTER_PENNANT) && sim_ship_capacity(g_ships[0]) == holdBefore + 1,
          "after the win the pennant fits and adds a hold");

    // Charter goods follow the frontier region's wants; a shortage lifts matching freight.
    legacy_campaign(19);
    g_campaign.stageSeen = 4;
    sim_generate_jobs(0, true);
    bool charterWanted = true;
    for (int j = 0; j < MAX_CONTRACTS_PER_PORT; ++j) {
      const Contract& c = g_contracts[0][j];
      if (c.active == 1 && c.kind == JOB_CHARTER && c.good != PORT_DEFS[c.projectRegion * 4].demanded[0]) charterWanted = false;
    }
    check(charterWanted, "a charter carries what the frontier region wants");
    legacy_campaign(19);
    uint32_t rngBefore = g_campaign.rng, idBefore = g_campaign.nextContractId;
    sim_generate_jobs(0, true);
    Contract plain[MAX_CONTRACTS_PER_PORT];
    memcpy(plain, g_contracts[0], sizeof(plain));
    g_campaign.rng = rngBefore; g_campaign.nextContractId = idBefore;
    g_campaign.worldEventType = EVENT_SHORTAGE; g_campaign.worldEventDays = 3; g_campaign.worldEventRegion = 0;
    bool lifted = false, others = true;
    for (int good = 0; good < GOOD_COUNT && !lifted; ++good) {
      g_campaign.worldEventGood = (uint8_t)good;
      g_campaign.rng = rngBefore; g_campaign.nextContractId = idBefore;
      sim_generate_jobs(0, true);
      for (int j = 0; j < MAX_CONTRACTS_PER_PORT; ++j) {
        const Contract& c = g_contracts[0][j];
        if (!c.active || c.id != plain[j].id) continue;
        bool match = c.kind == JOB_FREIGHT && c.good == good;
        if (match && c.reward > plain[j].reward) lifted = true;
        if (!match && c.reward != plain[j].reward) others = false;
      }
    }
    check(lifted && others, "a shortage pays more for matching freight and nothing else");
    g_campaign.worldEventType = EVENT_NONE; g_campaign.worldEventDays = 0;

    legacy_campaign(19);
    int hiddenGoods = 0;
    for (int p = 0; p < MAX_PORTS; ++p)
      for (int i = 0; i < MAX_CONTRACTS_PER_PORT; ++i) {
        const Contract& c = g_contracts[p][i];
        if (c.active && (c.kind == JOB_MAIL || c.kind == JOB_PASSENGER) && c.good != GOOD_GRAIN) ++hiddenGoods;
      }
    check(hiddenGoods == 0, "mail and passenger jobs carry no leftover trade good");

    auto press_on = [](bool passengers) {
      legacy_campaign(19);
      g_campaign.stageSeen = 2;
      Ship& ship = g_ships[0];
      ship.crew = 10; ship.morale = 50; ship.state = SHIP_AWAITING_DECISION;
      if (passengers) {
        CargoItem pax{};
        pax.quantity = 1; pax.destination = 1; pax.kind = CARGO_CONTRACT; pax.jobKind = JOB_PASSENGER;
        pax.projectRegion = 0xFF; pax.contractId = 0;
        ship.cargo[ship.cargoCount++] = pax;
      }
      g_incident = Incident{1, 0, INCIDENT_ILLNESS, RISK_EVEN};
      sim_resolve_incident(2);
      return (int)ship.crew * 1000 + ship.morale;
    };
    int alone = press_on(false), crowded = press_on(true);
    check(alone == 7038 && crowded == 6034, "illness costs more with passengers aboard");
    check(!strcmp(content_cargo_name(CARGO_CONTRACT, JOB_CHARTER, GOOD_TIMBER, false), "Charter cargo") &&
          !strcmp(content_cargo_name(CARGO_CONTRACT, JOB_CHARTER, GOOD_TIMBER, true), "Charter"),
          "carried charter cargo is named as a charter");

    // The shipyard card and the purchase agree on why a hull is not for sale.
    legacy_campaign(19);
    g_campaign.coins = 100000;
    g_campaign.rank = 1;    // a schooner is allowed by rank, so the yard is what blocks it
    g_ui.selectedPort = 1;  // a port with no shipyard yet
    check(sim_ship_purchase_block(1) == SIM_FACILITY_REQUIRED && sim_ship_purchase_block(0) == SIM_OK,
          "a level-0 port sells sloops only, and says why");
    g_ui.selectedPort = 0;
    g_campaign.coins = 10;
    check(sim_ship_purchase_block(0) == SIM_NO_FUNDS && sim_buy_ship(0).code == SIM_NO_FUNDS,
          "the purchase returns the same reason the card shows");

    // No facility sells a fourth level, even after the Grand Charter.
    legacy_campaign(19);
    g_campaign.coins = 100000; g_campaign.won = 1;
    g_ui.selectedPort = 0; g_ports[0].officeLevel = 3;
    check(sim_port_upgrade_max_level(0) == 3 && sim_buy_port_upgrade(0).code == SIM_FACILITY_REQUIRED,
          "the office stops at level 3 after the win");

    // Every warehouse purchase adds usable storage, including from a port
    // with no warehouse. Old saves with level 3 retain all twelve slots.
    auto fillWarehouse = []() {
      g_ports[0].warehouseCount = 0;
      int stored = 0;
      while (stored <= MAX_WAREHOUSE_ITEMS) {
        CargoItem item{}; item.good = GOOD_GRAIN; item.quantity = 1;
        item.kind = CARGO_MARKET; item.jobKind = JOB_FREIGHT;
        item.projectRegion = 0xFF;
        g_ships[0].cargo[0] = item; g_ships[0].cargoCount = 1;
        if (!sim_transfer_to_warehouse(0, 0)) break;
        ++stored;
      }
      return stored;
    };
    g_ports[0].warehouseLevel = 0;
    int previousCapacity = fillWarehouse();
    check(previousCapacity == 4, "level-0 warehouse stores four cargo stacks");
    for (int level = 1; level <= sim_port_upgrade_max_level(1); ++level) {
      int32_t beforeCoins = g_campaign.coins;
      SimResult upgraded = sim_buy_port_upgrade(1);
      int capacity = fillWarehouse();
      check(upgraded && g_ports[0].warehouseLevel == level && capacity > previousCapacity &&
            g_campaign.coins == beforeCoins - 280 * level,
            "each paid warehouse upgrade increases usable stack capacity");
      previousCapacity = capacity;
    }
    int32_t maxCoins = g_campaign.coins;
    check(sim_port_upgrade_max_level(1) == 2 && previousCapacity == MAX_WAREHOUSE_ITEMS &&
          sim_buy_port_upgrade(1).code == SIM_FACILITY_REQUIRED && g_campaign.coins == maxCoins,
          "full warehouse refuses a wasted upgrade without charging coins");
    g_ports[0].warehouseLevel = 3;
    check(fillWarehouse() == MAX_WAREHOUSE_ITEMS && g_ports[0].warehouseLevel == 3 &&
          sim_transfer_from_warehouse(0, MAX_WAREHOUSE_ITEMS - 1) &&
          sim_buy_port_upgrade(1).code == SIM_FACILITY_REQUIRED && g_campaign.coins == maxCoins,
          "grandfathered level-3 warehouse retains storage and transfers without another charge");
  }
}

struct BalanceStats {
  uint64_t stage3Minutes = 0;
  uint32_t stage3Count = 0;
  uint64_t secondShipMinutes = 0;
  uint64_t firstCharterMinutes = 0;
  uint64_t circumnavigationMinutes = 0;
  uint64_t allRegionsMinutes = 0;
  uint32_t secondShipCount = 0;
  uint32_t firstCharterCount = 0;
  uint32_t circumnavigationCount = 0;
  uint32_t openedRegionTotal = 0;
  uint32_t allRegionsCount = 0;
  uint32_t visitedAllCount = 0;
};

static bool run_company(uint32_t seed, uint32_t* deliveries, uint32_t* incidents,
                        BalanceStats* balance) {
  legacy_campaign(seed);
  int idleCycles = 0;
  bool sawSecondShip = false, sawCharter = false, sawAllRegions = false, sawWin = false;
  bool sawStage3 = false;
  for (int turn = 0; turn < 500; ++turn) {
    if (g_incident.active) sim_resolve_incident((uint8_t)(g_incident.risk == RISK_DANGEROUS ? 2 : 0));
    bool acted = false;
    for (int i = 0; i < g_campaign.shipCount; ++i) {
      if (g_ships[i].state == SHIP_DISABLED) continue;
      acted |= staff_ship((uint8_t)i);
      if (g_ships[i].state == SHIP_DOCKED || g_ships[i].state == SHIP_ARRIVED)
        acted |= dispatch_work((uint8_t)i);
    }
    int fleetGoal = g_campaign.regionMask == 1 ? 3 : 5;
    if (g_campaign.rank >= 1 && g_campaign.shipCount < fleetGoal &&
        g_campaign.coins >= SHIP_CLASS_DEFS[1].cost + 300) {
      g_ui.selectedPort = 0;
      SimResult bought = sim_buy_ship(1);
      acted |= bought;
      if (bought) acted |= staff_ship((uint8_t)bought.value);
    }
    for (int r = 1; r < REGION_COUNT; ++r) {
      if (g_campaign.regionMask & (1u << r)) continue;
      if (!(g_campaign.chartMask & (1u << r))) acted |= sim_purchase_chart((uint8_t)r);
      else acted |= sim_purchase_charter((uint8_t)r);
      break;
    }
    bool anyOperable = false, anySailing = false;
    for (int i = 0; i < g_campaign.shipCount; ++i) {
      anyOperable |= g_ships[i].state != SHIP_DISABLED;
      anySailing |= g_ships[i].state == SHIP_SAILING || g_ships[i].state == SHIP_AWAITING_DECISION;
    }
    if (!anyOperable) acted |= sim_claim_recovery();
    // A docked but penniless fleet claims relief too, as a player would; the
    // claim refuses itself while the company can still afford a departure.
    if (!acted && !anySailing) acted |= sim_claim_recovery();
    sim_advance_minutes(720);
    uint32_t activeMinutes = (uint32_t)(turn + 1) * 720u;
    uint32_t activeRealMinutes = (activeMinutes + 599u) / 600u;
    if (!sawStage3 && sim_stage() >= 3) {
      sawStage3 = true; ++balance->stage3Count;
      balance->stage3Minutes += activeRealMinutes;
    }
    if (!sawSecondShip && g_campaign.shipCount >= 2) {
      sawSecondShip = true; ++balance->secondShipCount;
      balance->secondShipMinutes += activeRealMinutes;
    }
    if (!sawCharter && (g_campaign.regionMask & ~1u)) {
      sawCharter = true; ++balance->firstCharterCount;
      balance->firstCharterMinutes += activeRealMinutes;
    }
    if (!sawAllRegions && g_campaign.regionMask == 0x3F) {
      sawAllRegions = true; ++balance->allRegionsCount;
      balance->allRegionsMinutes += activeRealMinutes;
    }
    if (!sawWin && g_campaign.won) {
      sawWin = true; ++balance->circumnavigationCount;
      balance->circumnavigationMinutes += activeRealMinutes;
    }
    if (!invariant_ok()) {
      ::printf("run_company invariant seed=%u turn=%d ships=%u captains=%u coins=%ld\n",
               seed, turn, g_campaign.shipCount, g_campaign.captainCount,
               (long)g_campaign.coins);
      return false;
    }
    if (!acted && !anySailing) ++idleCycles; else idleCycles = 0;
    if (idleCycles > 12) {
      ::printf("run_company stalled seed=%u turn=%d ships=%u captains=%u coins=%ld debt=%ld\n",
               seed, turn, g_campaign.shipCount, g_campaign.captainCount,
               (long)g_campaign.coins, (long)g_campaign.recoveryDebt);
      return false;
    }
  }
  *deliveries += g_campaign.deliveries;
  *incidents += g_campaign.incidentsResolved;
  int opened = 0;
  for (int r = 0; r < REGION_COUNT; ++r) if (g_campaign.regionMask & (1u << r)) ++opened;
  balance->openedRegionTotal += (uint32_t)opened;
  if (g_campaign.visitedMask == 0x3F) ++balance->visitedAllCount;
  return g_campaign.deliveries > 0;
}

int main(int argc, char** argv) {
  int seeds = argc > 1 ? atoi(argv[1]) : 1000;
  unit_tests();
  uint32_t deliveries = 0, incidents = 0;
  BalanceStats balance{};
  int anomalies = 0;
  for (int i = 0; i < seeds; ++i) {
    if (!run_company(1000u + (uint32_t)i * 7919u, &deliveries, &incidents, &balance)) {
      ++anomalies;
      if (anomalies <= 10) ::printf("ANOMALY seed=%u\n", 1000u + (uint32_t)i * 7919u);
    }
  }
  ::printf("Crosswinds headless: seeds=%d deliveries=%u incidents=%u anomalies=%d unit_failures=%d\n",
           seeds, deliveries, incidents, anomalies, failures);
  ::printf("Stages: stage3=%u avg_active_min=%llu\n", balance.stage3Count,
           (unsigned long long)(balance.stage3Count ? balance.stage3Minutes / balance.stage3Count : 0));
  if ((int)balance.stage3Count < seeds) {
    ::printf("STAGING: %d of %d companies never reached stage 3\n", seeds - (int)balance.stage3Count, seeds);
    ++failures;
  }
  ::printf("Milestones: second_ship=%u avg_active_min=%llu first_charter=%u avg_active_min=%llu "
           "all_regions=%u avg_active_min=%llu avg_regions=%.2f visited_all=%u "
           "circumnavigation=%u avg_active_min=%llu\n",
           balance.secondShipCount,
           (unsigned long long)(balance.secondShipCount ? balance.secondShipMinutes / balance.secondShipCount : 0),
           balance.firstCharterCount,
           (unsigned long long)(balance.firstCharterCount ? balance.firstCharterMinutes / balance.firstCharterCount : 0),
           balance.allRegionsCount,
           (unsigned long long)(balance.allRegionsCount ? balance.allRegionsMinutes / balance.allRegionsCount : 0),
           seeds ? (double)balance.openedRegionTotal / seeds : 0.0, balance.visitedAllCount,
           balance.circumnavigationCount,
           (unsigned long long)(balance.circumnavigationCount ? balance.circumnavigationMinutes / balance.circumnavigationCount : 0));
  return failures || anomalies ? 1 : 0;
}
