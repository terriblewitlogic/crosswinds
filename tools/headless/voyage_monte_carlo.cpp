#include "game.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

bool save_write() { return true; }
void audio_begin() {}
void audio_tick() {}
void audio_notice() {}
void audio_alert() {}

static uint32_t policyRng;
static bool trace;
static uint32_t random_policy() {
  policyRng ^= policyRng << 13; policyRng ^= policyRng >> 17; policyRng ^= policyRng << 5;
  return policyRng;
}
static uint64_t state_hash() {
  uint64_t h = 1469598103934665603ull;
  auto bytes = [&](const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    while (n--) h = (h ^ *b++) * 1099511628211ull;
  };
  bytes(&g_campaign, sizeof(g_campaign)); bytes(g_ports, sizeof(g_ports));
  bytes(g_ships, sizeof(g_ships)); bytes(g_contracts, sizeof(g_contracts));
  bytes(g_captains, sizeof(g_captains));
  return h;
}
static bool at_port(const Ship& s) { return s.state == SHIP_DOCKED || s.state == SHIP_ARRIVED; }
static int project_remaining(uint8_t region, int material) {
  if (region >= REGION_COUNT) return 0;
  int remaining = sim_project_target(region) - g_campaign.projectCargo[region][material];
  uint8_t good = material ? GOOD_IRONWARE : GOOD_TIMBER;
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    const BookedVoyage& b = g_ships[i].booking;
    if (b.state == BOOKING_SAILING && b.projectRegion == region && b.good == good)
      remaining -= b.quantity - b.settledQuantity;
  }
  return remaining > 0 ? remaining : 0;
}
static bool makes_needed(uint8_t port, uint8_t region) {
  // A project pays for an inter-port delivery. Its own production cannot
  // supply that journey, even where the dock happens to make both materials.
  if (port == sim_project_port(region)) return false;
  const PortDef& p = PORT_DEFS[port];
  for (int material = 0; material < 2; ++material) {
    uint8_t good = material ? GOOD_IRONWARE : GOOD_TIMBER;
    if (project_remaining(region, material) && (p.produced[0] == good || p.produced[1] == good)) return true;
  }
  return false;
}
static int supply_hops(uint8_t from, uint8_t region) {
  int best = 1000;
  for (uint8_t p = 0; p < MAX_PORTS; ++p) if (g_ports[p].unlocked && makes_needed(p, region)) {
    int hops = content_hops(from, p, true);
    if (hops >= 0 && hops < best) best = hops;
  }
  return best;
}
struct Run {
  int failures = 0, reconciliation = 0, idleStall = 0, relief = 0;
  int dispatches = 0, partials = 0, refused = 0, freight = 0, trades = 0;
  int losingTrades = 0;
  int64_t spent = 0, banked = 0, investments = 0;
  int64_t pending = 0;
  int32_t minCash = 500;
  uint32_t firstArrival = 0, secondShip = 0, firstRegion = 0, allRegions = 0;
};
static void verify(Run& r) {
  int64_t expected = 500 - r.spent - r.investments + r.banked;
  if (expected != g_campaign.coins) ++r.reconciliation;
  if (g_campaign.coins < 0 || g_campaign.shipCount > MAX_SHIPS) ++r.failures;
  r.minCash = r.minCash < g_campaign.coins ? r.minCash : g_campaign.coins;
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    const Ship& s = g_ships[i]; const BookedVoyage& b = s.booking;
    if (s.condition > 100 || s.currentPort >= MAX_PORTS || s.cargoCount > MAX_CARGO_ITEMS ||
        sim_ship_used_capacity(s) > sim_ship_capacity(s)) ++r.failures;
    if (b.state == BOOKING_SAILING && ((b.mode != VOYAGE_TRADE && b.profit <= 0) || s.state != SHIP_SAILING ||
        b.arrivalCoins != b.payout - b.withheld || b.payNow != b.cargoCost + b.sailingCost)) ++r.failures;
    if (b.state == BOOKING_SETTLED && (b.settledQuantity != b.quantity || s.cargoCount)) ++r.failures;
  }
}
static void decide(Run& r, int policy, int fleetCap, bool novice) {
  uint8_t region = sim_project_region();
  if (region < REGION_COUNT && g_campaign.deliveredUnits >= 48) {
    int32_t before = g_campaign.coins;
    if (sim_project_contribute(region)) {
      r.investments += before - g_campaign.coins;
      if (trace) std::fprintf(stderr, "%.1fm region %u opens, cash %ld\n", g_campaign.worldMinutes/600.0, region, (long)g_campaign.coins);
    }
  }
  // Keep the first two coasters. The next slot is the longer-range six-space
  // Schooner revealed by the first region, rather than filling every slot with
  // a hull that cannot cross the later frontier.
  uint8_t purchaseClass = g_campaign.shipCount < 2 ? 0 : 1;
  bool frontierOpen = (g_campaign.regionMask & 2u) != 0;
  for (int i = 0; i < g_campaign.shipCount && g_campaign.shipCount < fleetCap; ++i) {
    if (purchaseClass == 1 && !frontierOpen) break;
    if (!at_port(g_ships[i])) continue;
    // Once works are visible, keep their contribution alongside the operating
    // reserve before buying a third or later ship. Caps are ceilings, not goals.
    if (g_campaign.shipCount >= 3 && region < REGION_COUNT && g_campaign.deliveredUnits >= 48 &&
        g_campaign.coins < sim_ship_package_cost(purchaseClass) + 200 + sim_project_cost(region)) continue;
    g_ui.selectedShip = (uint8_t)i; g_ui.selectedPort = g_ships[i].currentPort;
    int32_t before = g_campaign.coins;
    if (sim_buy_ship(purchaseClass)) { r.investments += before - g_campaign.coins;
      if (trace) std::fprintf(stderr, "%.1fm ship %u joins, cash %ld\n", g_campaign.worldMinutes/600.0, g_campaign.shipCount, (long)g_campaign.coins);
      break; }
  }
  region = sim_project_region();
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    Ship& ship = g_ships[i];
    if (!at_port(ship) || ship.cargoCount) continue;
    g_ui.selectedShip = (uint8_t)i; g_ui.selectedPort = ship.currentPort;
    VoyageQuote best{}; best.code = SIM_INVALID;
    double bestScore = -1e20;
    uint8_t destinations[MAX_PORTS];
    uint64_t beforePlan = state_hash();
    uint8_t count = sim_voyage_destinations((uint8_t)i, destinations, MAX_PORTS);
    const bool advancingProject = region < REGION_COUNT && g_campaign.deliveredUnits >= 48;
    const int currentSupplyHops = advancingProject ? supply_hops(ship.currentPort, region) : 1000;
    for (uint8_t d = 0; d < count; ++d) {
      uint8_t destination = destinations[d];
      for (uint8_t mode = VOYAGE_FREIGHT; mode <= VOYAGE_TRADE; ++mode) {
        if ((policy == 0 && mode != VOYAGE_FREIGHT) || (policy == 1 && mode != VOYAGE_TRADE)) continue;
        for (uint8_t good = 0; good < GOOD_COUNT; ++good) {
          VoyageQuote q = sim_voyage_offer((uint8_t)i, destination, mode, good, 0);
          if (q.code != SIM_OK) continue;
          double score = q.minutes ? q.profit * 600.0 / q.minutes : -1e20;
          if (policy == 2 && !g_campaign.completedVoyages && q.mode == VOYAGE_FREIGHT &&
              q.origin == 0 && q.destination == 1 && q.good == GOOD_TEXTILES) score += 100000;
          if (advancingProject && q.projectRegion == region) score += 10000 + q.quantity;
          else if (advancingProject && currentSupplyHops > 0) {
            int hops = supply_hops(destination, region);
            if (hops < currentSupplyHops) score += 1000.0 / (hops + 1);
          }
          // A modest diverse preference is seeded and known to the policy;
          // it does not inspect future events or order outcomes.
          if (novice) score *= 0.65 + (double)(random_policy() % 71) / 100.0;
          if (score > bestScore) { bestScore = score; best = q; }
        }
      }
    }
    if (state_hash() != beforePlan) ++r.failures;
    if (best.code == SIM_OK) {
      // Novices sometimes approve an incomplete trade despite its disclosed
      // loss. Freight retains its positive-fee requirement. Count intentional
      // losing trades separately from actual state/accounting failures.
      if (novice && (random_policy() % 3 == 0)) {
        uint8_t quantity = (uint8_t)(1 + random_policy() % best.quantity);
        VoyageQuote partial = sim_voyage_offer((uint8_t)i, best.destination, best.mode, best.good, quantity);
        if (partial.code == SIM_OK) best = partial;
        else ++r.refused;
      }
      int32_t before = g_campaign.coins;
      if (sim_voyage_dispatch((uint8_t)i, best)) {
        r.spent += before - g_campaign.coins; ++r.dispatches;
        if (trace && g_campaign.worldMinutes < 90000) std::fprintf(stderr,
          "%.1fm ship%d %u>%u %s qty%u +%ld dur%.1f project%u cash%ld units%lu materials%u/%u\n",
          g_campaign.worldMinutes/600.0, i, best.origin,best.destination,GOOD_DEFS[best.good].shortName,
          best.quantity,(long)best.profit,best.minutes/600.0,best.projectRegion,(long)g_campaign.coins,
          (unsigned long)g_campaign.deliveredUnits,g_campaign.projectCargo[1][0],g_campaign.projectCargo[1][1]);
        if (best.quantity < best.capacity) ++r.partials;
        if (best.mode == VOYAGE_TRADE) ++r.trades; else ++r.freight;
        if (best.mode == VOYAGE_TRADE && best.profit < 0) ++r.losingTrades;
      } else ++r.failures;
    }
  }
  if (sim_recovery_available()) {
    int32_t before = g_campaign.coins;
    if (sim_claim_recovery()) { ++r.relief; r.spent += before - g_campaign.coins; }
  }
}
static Run campaign(uint32_t seed, int policy, int fleetCap, int attentionMinutes, bool novice, int horizon) {
  sim_new_campaign(seed); g_campaign.onboardingStep = ONBOARD_DONE; g_ui.title = 0; g_ui.card = CARD_NONE;
  policyRng = seed ^ 0x924db34fu;
  Run r;
  uint32_t nextDecision = 0, lastProgress = 0, previousDelivered = 0;
  for (uint32_t minute = 0; minute < (uint32_t)horizon * 600u; minute += 60u) {
    if (minute >= nextDecision) {
      decide(r, policy, fleetCap, novice);
      nextDecision = minute + (uint32_t)attentionMinutes * 600u;
    }
    int32_t before = g_campaign.coins;
    sim_advance_minutes(60);
    r.banked += g_campaign.coins - before;
    if (g_campaign.deliveredUnits != previousDelivered) {
      previousDelivered = g_campaign.deliveredUnits; lastProgress = minute + 60;
      if (!r.firstArrival) r.firstArrival = minute + 60;
    }
    if (!r.secondShip && g_campaign.shipCount >= 2) r.secondShip = minute;
    if (!r.firstRegion && (g_campaign.regionMask & 2)) r.firstRegion = minute;
    if (!r.allRegions && g_campaign.regionMask == 0x3f) r.allRegions = minute;
    bool moving = false;
    for (int i = 0; i < g_campaign.shipCount; ++i) moving |= g_ships[i].state == SHIP_SAILING;
    if (!moving && minute > lastProgress + 30u * 600u) r.idleStall = 1;
    verify(r);
  }
  for (int i = 0; i < g_campaign.shipCount; ++i)
    if (g_ships[i].booking.state == BOOKING_SAILING) r.pending += g_ships[i].booking.arrivalCoins;
  return r;
}
int main(int argc, char** argv) {
  int seeds = argc > 1 ? std::atoi(argv[1]) : 100;
  int horizon = argc > 2 ? std::atoi(argv[2]) : 360;
  int totalFailures = 0;
  std::printf("policy,fleet_cap,attention_minutes,novice,seed,horizon_minutes,cash,delivered,ships,regions,first_arrival_minutes,second_ship_minutes,first_region_minutes,all_regions_minutes,min_cash,dispatches,freight,trades,partials,refused,relief,stall,pending,failures,reconciliation,losing_trades\n");
  const int caps[] = {3,6}, attention[] = {2,10,30};
  const char* names[] = {"freight","trade","mixed"};
  for (int policy = 0; policy < 3; ++policy) for (int cap : caps)
    for (int interval : attention) for (int novice = 0; novice < 2; ++novice)
      for (int seed = 0; seed < seeds; ++seed) {
        uint32_t seedValue = 1000u + (uint32_t)seed * 7919u;
        trace = std::getenv("SH_VOYAGE_TRACE") && policy == 2 && cap == 3 && interval == 2 && !novice && !seed;
        Run r = campaign(seedValue, policy, cap, interval, novice != 0, horizon);
        int regions = 0; for (int bit = 0; bit < REGION_COUNT; ++bit) regions += (g_campaign.regionMask >> bit) & 1;
        std::printf("%s,%d,%d,%d,%u,%d,%ld,%lu,%u,%d,%.2f,%.2f,%.2f,%.2f,%ld,%d,%d,%d,%d,%d,%d,%d,%lld,%d,%d,%d\n",
          names[policy],cap,interval,novice,seedValue,horizon,(long)g_campaign.coins,
          (unsigned long)g_campaign.deliveredUnits,g_campaign.shipCount,regions,
          r.firstArrival/600.0,r.secondShip/600.0,r.firstRegion/600.0,r.allRegions/600.0,(long)r.minCash,
          r.dispatches,r.freight,r.trades,r.partials,r.refused,r.relief,r.idleStall,(long long)r.pending,
          r.failures,r.reconciliation,r.losingTrades);
        totalFailures += r.failures + r.reconciliation;
      }
  std::fprintf(stderr, "Voyage matrix: %d companies, %d invariant/reconciliation failures\n", seeds * 36, totalFailures);
  return totalFailures ? 1 : 0;
}
