#include "game.h"
#include "economy_fixtures.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

// A simulation client, not a second economy implementation. Actions use the
// public game API and its visible feature gates. No state, cash or RNG boosts.
// These scenarios exercise the optional legacy contract/spot-market rules.
// The booked-voyage campaign is covered by voyage_regressions and its matrix.
static void legacy_campaign(uint32_t seed) { historical_campaign(seed, 0); }

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

enum Policy { FIRST, CONTRACTS, WANTS, MIXED, EAGER, CASUAL, MARGIN, POLICY_COUNT };
static const char* const NAMES[] = {"first_contracts", "best_contracts", "wants_trader",
  "prudent_mixed", "eager_fleet", "casual_mixed", "margin_trader"};
static constexpr uint32_t HORIZON = 216000;  // 360 real minutes at 1x
static constexpr uint32_t CHECKPOINTS[] = {18000, 36000, 72000, 144000, HORIZON};
static constexpr uint32_t STALL_WINDOW = 18000;  // 30 real minutes

struct Stats {
  uint32_t rng = 1, reliefClaims = 0, repairs = 0, repairSpend = 0;
  uint32_t buys = 0, sells = 0, dispatches = 0, disabledEvents = 0;
  int64_t marketSpend = 0, marketReceipts = 0, marketNetReceipts = 0;
  int64_t reliefCash = 0;
  int64_t debtWithheld = 0;
  int64_t maxCashError = 0;
  int32_t maxDebt = 0, minCash = 500;
  uint32_t firstDelivery = 0, secondShip = 0, stage3 = 0, firstCharter = 0;
  uint32_t noProgress = 0, stallMinutes = 0, maxNoProgress = 0;
  uint32_t invariantFailures = 0, decisionRounds = 0;
};
static bool verifyPlans = false;

static uint64_t planning_fingerprint() {
  uint64_t hash = 1469598103934665603ull;
  auto mix = [&hash](const void* data, size_t count) {
    const uint8_t* bytes = (const uint8_t*)data;
    for (size_t i = 0; i < count; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
  };
  mix(&g_campaign, sizeof(g_campaign)); mix(g_ships, sizeof(g_ships));
  mix(g_ports, sizeof(g_ports)); mix(g_contracts, sizeof(g_contracts));
  mix(g_captains, sizeof(g_captains)); mix(&g_ui, sizeof(g_ui));
  mix(&g_incident, sizeof(g_incident)); mix(g_incidentQueue, sizeof(g_incidentQueue));
  mix(&g_incidentQueueCount, sizeof(g_incidentQueueCount));
  return hash;
}

static int64_t cash_error(const Stats& s) {
  const int64_t expectedCash = 500 + (int64_t)g_campaign.contractRevenue - g_campaign.operatingCosts -
    g_campaign.capitalSpending + g_campaign.incidentNet - s.marketSpend + s.marketReceipts +
    s.reliefCash - s.debtWithheld;
  return g_campaign.coins - expectedCash;
}

static uint32_t policy_random(Stats& stats) {
  uint32_t x = stats.rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return stats.rng = x ? x : 1;
}
static bool at_port(const Ship& ship) {
  return ship.state == SHIP_DOCKED || ship.state == SHIP_ARRIVED || ship.state == SHIP_DISABLED;
}
static bool trader(Policy policy) { return policy == WANTS || policy == MARGIN; }
static bool trades(Policy policy) { return trader(policy) || policy == MIXED || policy == EAGER || policy == CASUAL; }
static int reserve(Policy policy) { return policy == EAGER ? 20 : 200; }

struct Paths { int days[MAX_PORTS]; uint8_t previous[MAX_PORTS]; };
static Paths paths_for(uint8_t index) {
  Paths paths{};
  bool visited[MAX_PORTS]{};
  const Ship& ship = g_ships[index];
  std::fill(paths.days, paths.days + MAX_PORTS, 30000);
  std::fill(paths.previous, paths.previous + MAX_PORTS, NO_PORT);
  paths.days[ship.currentPort] = 0;
  int endurance = SHIP_CLASS_DEFS[ship.classId].enduranceDays;
  for (uint8_t module : ship.modules) if (module == MODULE_LONG_CASKS + 1) endurance += 6;
  for (int count = 0; count < MAX_PORTS; ++count) {
    int from = -1;
    for (int p = 0; p < MAX_PORTS; ++p)
      if (!visited[p] && (from < 0 || paths.days[p] < paths.days[from])) from = p;
    if (from < 0 || paths.days[from] == 30000) break;
    visited[from] = true;
    for (int lane = 0; lane < LANE_COUNT; ++lane) {
      const SeaLaneDef& l = LANE_DEFS[lane];
      int to = l.a == from ? l.b : (l.b == from ? l.a : -1);
      if (to < 0 || !g_ports[to].unlocked || l.days > endurance ||
          SHIP_CLASS_DEFS[ship.classId].draft > PORT_DEFS[to].portClass) continue;
      int days = paths.days[from] + sim_leg_days(index, lane, (uint8_t)from);
      if (days < paths.days[to]) { paths.days[to] = days; paths.previous[to] = (uint8_t)from; }
    }
  }
  return paths;
}

struct Plan {
  uint8_t destination = NO_PORT, route[MAX_ROUTE_LEGS]{}, count = 0;
  uint32_t ids[MAX_CARGO_ITEMS]{};
  uint8_t jobs = 0;
  uint8_t goods[MAX_CARGO_ITEMS]{};
  uint8_t buyCount = 0;
  int tradeProfit = 0, cost = 0, days = 0;
  double score = -1e30;
  bool valid = false;
};
static bool route_to(const Paths& paths, uint8_t from, uint8_t to, Plan& plan) {
  if (from == to || paths.days[to] == 30000) return false;
  uint8_t reverse[MAX_PORTS]; int count = 0;
  for (uint8_t p = to; p != from && count < MAX_PORTS; p = paths.previous[p]) {
    if (p == NO_PORT) return false;
    reverse[count++] = p;
  }
  if (!count || count > MAX_ROUTE_LEGS) return false;
  plan.destination = to; plan.count = (uint8_t)count; plan.days = paths.days[to];
  for (int i = 0; i < count; ++i) plan.route[i] = reverse[count - i - 1];
  return true;
}
static bool fits(const Ship& ship, const CargoItem& item) {
  if (ship.cargoCount >= MAX_CARGO_ITEMS) return false;
  return item.jobKind == JOB_PASSENGER ?
    sim_ship_passenger_load(ship) + item.quantity <= sim_ship_berths(ship) :
    sim_ship_used_capacity(ship) + item.quantity <= sim_ship_capacity(ship);
}
static CargoItem cargo_for(const Contract& c) {
  CargoItem item{};
  item.contractId = c.id; item.good = c.good; item.quantity = c.quantity;
  item.destination = c.destination; item.kind = CARGO_CONTRACT;
  item.jobKind = c.kind; item.projectRegion = c.projectRegion;
  return item;
}
static bool project_priority(const Contract& c) {
  return c.kind == JOB_CHARTER && c.projectRegion < REGION_COUNT &&
    (g_campaign.chartMask & (1u << c.projectRegion)) &&
    g_campaign.charterProgress[c.projectRegion] < BALANCE.charterTarget[c.projectRegion];
}

// Current destination quotes are information available on the map, not a
// prediction of future events. Repeated units include their own stock impact.
static void add_trade(Ship& ship, uint8_t destination, int departureCost,
                      Policy policy, Plan& plan) {
  if (!sim_feature_unlocked(FEATURE_MARKET)) return;
  uint16_t source[GOOD_COUNT], target[GOOD_COUNT];
  std::memcpy(source, g_ports[ship.currentPort].stock, sizeof(source));
  std::memcpy(target, g_ports[destination].stock, sizeof(target));
  int available = g_campaign.coins - departureCost - reserve(policy);
  while (plan.buyCount < MAX_CARGO_ITEMS) {
    int chosen = -1, margin = 0, paid = 0;
    for (int good = 0; good < GOOD_COUNT; ++good) {
      if (policy == WANTS &&
          !((PORT_DEFS[ship.currentPort].produced[0] == good || PORT_DEFS[ship.currentPort].produced[1] == good) &&
            (PORT_DEFS[destination].demanded[0] == good || PORT_DEFS[destination].demanded[1] == good))) continue;
      CargoItem item{}; item.quantity = 1; item.kind = CARGO_MARKET; item.jobKind = JOB_FREIGHT;
      if (!source[good] || !fits(ship, item)) continue;
      int buy = content_market_price_at_stock(ship.currentPort, (uint8_t)good, true, source[good]);
      int sell = content_market_price_at_stock(destination, (uint8_t)good, false, target[good]);
      if (buy <= available && sell - buy > margin) { chosen = good; margin = sell - buy; paid = buy; }
    }
    if (chosen < 0) break;
    CargoItem item{}; item.quantity = 1; item.kind = CARGO_MARKET; item.jobKind = JOB_FREIGHT;
    item.good = (uint8_t)chosen; item.destination = NO_PORT; item.projectRegion = 0xff;
    item.paidUnitPrice = (uint16_t)paid;
    ship.cargo[ship.cargoCount++] = item;
    plan.goods[plan.buyCount++] = (uint8_t)chosen;
    plan.tradeProfit += margin; available -= paid; --source[chosen]; ++target[chosen];
  }
}

static Plan choose_plan(uint8_t index, Policy policy) {
  const Ship original = g_ships[index];
  const Paths paths = paths_for(index);
  const bool onlyTrade = trader(policy) && g_campaign.deliveries > 0;
  Plan best;
  // First-feasible deliberately follows the visible board's order.
  uint8_t destinations[MAX_PORTS]; int destinationCount = 0;
  bool included[MAX_PORTS]{};
  for (const Contract& c : g_contracts[original.currentPort])
    if (c.active == 1 && c.destination < MAX_PORTS && !included[c.destination]) {
      included[c.destination] = true; destinations[destinationCount++] = c.destination;
    }
  for (int p = 0; p < MAX_PORTS; ++p)
    if (!included[p]) destinations[destinationCount++] = (uint8_t)p;
  for (int d = 0; d < destinationCount; ++d) {
    const uint8_t destination = destinations[d];
    Plan candidate;
    if (!route_to(paths, original.currentPort, destination, candidate)) continue;
    Ship trial = original;
    bool carried = false, incompatible = false;
    for (const CargoItem& item : original.cargo) {
      if (&item >= original.cargo + original.cargoCount) break;
      if (item.kind == CARGO_CONTRACT) {
        if (item.destination == destination) carried = true; else incompatible = true;
      }
    }
    if (incompatible) continue;
    bool selected[MAX_CONTRACTS_PER_PORT]{};
    int priority = 0;
    if (!onlyTrade) {
      while (candidate.jobs < MAX_CARGO_ITEMS) {
        int slot = -1;
        for (int j = 0; j < MAX_CONTRACTS_PER_PORT; ++j) {
          const Contract& c = g_contracts[original.currentPort][j];
          if (selected[j] || c.active != 1 || c.destination != destination || !fits(trial, cargo_for(c))) continue;
          if (slot < 0 || (policy != FIRST &&
              ((project_priority(c) && !project_priority(g_contracts[original.currentPort][slot])) ||
               (project_priority(c) == project_priority(g_contracts[original.currentPort][slot]) &&
                c.reward > g_contracts[original.currentPort][slot].reward)))) slot = j;
        }
        if (slot < 0) break;
        const Contract& c = g_contracts[original.currentPort][slot];
        selected[slot] = true; candidate.ids[candidate.jobs++] = c.id;
        trial.cargo[trial.cargoCount++] = cargo_for(c);
        if (project_priority(c)) priority += c.quantity;
      }
    }
    // The actual estimator sees only a temporary ship. It consumes no RNG,
    // changes no contracts and returns cost including the selected captain.
    g_ships[index] = trial;
    RouteEstimate estimate = sim_estimate_route(index, candidate.route, candidate.count);
    g_ships[index] = original;
    if (!estimate.valid) continue;
    candidate.cost = estimate.cost;
    if (trades(policy) && (onlyTrade || candidate.jobs || carried))
      add_trade(trial, destination, estimate.cost, policy, candidate);
    if (!candidate.jobs && !carried && !candidate.buyCount) continue;
    int net = estimate.netProfit + candidate.tradeProfit;
    if (onlyTrade && net <= 0) continue;
    candidate.score = (double)net / std::max(1, candidate.days);
    if (priority) candidate.score += 10000.0 + priority;
    candidate.valid = true;
    if (!best.valid || candidate.score > best.score) best = candidate;
    if (policy == FIRST && (candidate.jobs || carried)) break;
  }
  return best;
}

static void sell_cargo(uint8_t index, Stats& stats) {
  if (!sim_feature_unlocked(FEATURE_MARKET)) return;
  for (int good = 0; good < GOOD_COUNT; ++good) {
    while (true) {
      MarketQuote quote = sim_market_quote(index, (uint8_t)good, false, 1);
      if (!quote.quantity || !sim_market_sell(index, (uint8_t)good)) break;
      ++stats.sells; stats.marketReceipts += quote.total; stats.marketNetReceipts += quote.treasuryDelta;
      stats.debtWithheld += quote.withholding;
    }
  }
}
static bool staff(uint8_t index) {
  if (sim_ship_captain(index)) return true;
  if (!sim_feature_unlocked(FEATURE_CAPTAINS)) return false;
  Ship& ship = g_ships[index];
  for (int i = 0; i < g_campaign.captainCount; ++i)
    if (g_captains[i].active && g_captains[i].assignedShip == NO_SHIP &&
        g_captains[i].currentPort == ship.currentPort)
      return !!sim_assign_captain((uint8_t)i, index);
  g_ui.selectedPort = ship.currentPort;
  SimResult hired = sim_hire_captain(0);
  return hired && sim_assign_captain((uint8_t)hired.value, index);
}
static void service_ship(uint8_t index, Policy policy, Stats& stats) {
  Ship& ship = g_ships[index];
  if (!at_port(ship)) return;
  g_ui.selectedShip = index; g_ui.selectedPort = ship.currentPort;
  if (ship.condition < (policy == EAGER ? 30 : 70) && sim_feature_unlocked(FEATURE_YARD)) {
    int cost = (100 - ship.condition) * BALANCE.repairCostPerPoint;
    if (g_campaign.coins >= cost + reserve(policy) || ship.state == SHIP_DISABLED) {
      if (sim_repair_ship(index)) { ++stats.repairs; stats.repairSpend += (uint32_t)cost; }
    }
  }
  if (ship.state == SHIP_DISABLED || !staff(index)) return;
  sell_cargo(index, stats);
  uint64_t fingerprint = verifyPlans ? planning_fingerprint() : 0;
  Plan plan = choose_plan(index, policy);
  if (verifyPlans && fingerprint != planning_fingerprint()) {
    ++stats.invariantFailures;
    std::fprintf(stderr, "planning mutated simulation seed=%u policy=%s worldMinute=%u\n",
      g_campaign.seed, NAMES[policy], g_campaign.worldMinutes);
  }
  if (!plan.valid) return;
  for (int j = 0; j < plan.jobs; ++j) {
    for (int slot = 0; slot < MAX_CONTRACTS_PER_PORT; ++slot)
      if (g_contracts[ship.currentPort][slot].active == 1 &&
          g_contracts[ship.currentPort][slot].id == plan.ids[j]) {
        sim_load_contract(index, ship.currentPort, (uint8_t)slot); break;
      }
  }
  for (int unit = 0; unit < plan.buyCount; ++unit) {
    MarketQuote quote = sim_market_quote(index, plan.goods[unit], true, 1);
    if (!quote.quantity || g_campaign.coins - quote.total < plan.cost + reserve(policy)) break;
    if (sim_market_buy(index, plan.goods[unit])) { ++stats.buys; stats.marketSpend += quote.total; }
  }
  sim_route_clear();
  for (int leg = 0; leg < plan.count; ++leg) if (!sim_route_add(plan.route[leg])) return;
  if (sim_dispatch(index)) ++stats.dispatches;
}

static void resolve_incidents(Policy policy, Stats& stats) {
  while (g_incident.active) {
    uint8_t option = 0;
    switch (g_incident.kind) {
      case INCIDENT_PIRATES: option = g_incident.risk == RISK_DANGEROUS ? 2 : 0; break;
      case INCIDENT_STORM: option = 0; break;
      case INCIDENT_REEF: option = 0; break;
      case INCIDENT_ILLNESS: option = 1; break;
      case INCIDENT_INSPECTION: option = 2; break;
      case INCIDENT_MUTINY: option = 0; break;
      case INCIDENT_RESCUE: option = 0; break;
      case INCIDENT_TRADE: option = 2; break;
      default: break;
    }
    if (policy == EAGER && (g_incident.kind == INCIDENT_STORM || g_incident.kind == INCIDENT_REEF) &&
        policy_random(stats) % 3 == 0) option = 2;
    uint8_t index = g_incident.shipIndex;
    sim_resolve_incident(option);
    if (g_ships[index].state == SHIP_DISABLED) ++stats.disabledEvents;
  }
}
static void invest(Policy policy) {
  if (trader(policy)) return;  // isolate the first ship's trading economics
  const int cashReserve = reserve(policy);
  const int goal = policy == EAGER ? 6 : 3;
  if (sim_feature_unlocked(FEATURE_FLEET) && sim_feature_unlocked(FEATURE_YARD) &&
      g_campaign.rank >= SHIP_CLASS_DEFS[1].rank && g_campaign.shipCount < goal) {
    g_ui.selectedPort = 0;
    CaptainProfile candidate{};
    if (sim_recruit_candidate(0, 0, &candidate)) {
      int staffingReserve = policy == EAGER ? 0 : candidate.hireCost;
      if (g_campaign.coins >= SHIP_CLASS_DEFS[1].cost + staffingReserve + cashReserve) {
        SimResult result = sim_buy_ship(1);
        if (result) staff((uint8_t)result.value);
      }
    }
  }
  if (!sim_feature_unlocked(FEATURE_CHARTERS)) return;
  for (int region = 1; region < REGION_COUNT; ++region) {
    if (g_campaign.regionMask & (1u << region)) continue;
    if (!(g_campaign.chartMask & (1u << region))) {
      if (g_campaign.coins >= BALANCE.chartCost[region] + cashReserve) sim_purchase_chart((uint8_t)region);
    } else if (g_campaign.coins >= BALANCE.charterCost[region] + cashReserve) {
      sim_purchase_charter((uint8_t)region);
    }
    break;
  }
}

static bool invariant() {
  if (g_campaign.coins < 0 || g_campaign.recoveryDebt < 0 ||
      !g_campaign.shipCount || g_campaign.shipCount > MAX_SHIPS || g_campaign.captainCount > MAX_CAPTAINS ||
      g_campaign.regionMask & ~g_campaign.chartMask || g_incidentQueueCount > MAX_INCIDENTS) return false;
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    const Ship& s = g_ships[i];
    if (s.classId >= SHIP_CLASS_COUNT || s.currentPort >= MAX_PORTS || s.state > SHIP_DISABLED ||
        s.fromPort >= MAX_PORTS || s.toPort >= MAX_PORTS || s.condition > 100 ||
        s.morale > 100 || s.cargoCount > MAX_CARGO_ITEMS || s.routeCount > MAX_ROUTE_LEGS ||
        sim_ship_used_capacity(s) > sim_ship_capacity(s) || sim_ship_passenger_load(s) > sim_ship_berths(s)) return false;
    if (s.captainId != NO_SHIP && (s.captainId >= MAX_CAPTAINS ||
        !g_captains[s.captainId].active || g_captains[s.captainId].assignedShip != i)) return false;
    for (int c = 0; c < s.cargoCount; ++c)
      if (!s.cargo[c].quantity || s.cargo[c].good >= GOOD_COUNT) return false;
  }
  return true;
}

static void emit(uint32_t seed, Policy policy, const Stats& s) {
  int64_t cargoValue = 0, fleetValue = 0;
  int disabled = 0, moving = 0, regions = 0;
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    const Ship& ship = g_ships[i];
    fleetValue += SHIP_CLASS_DEFS[ship.classId].cost * ship.condition / 100;
    disabled += ship.state == SHIP_DISABLED;
    moving += ship.state == SHIP_SAILING || ship.state == SHIP_AWAITING_DECISION;
    for (int c = 0; c < ship.cargoCount; ++c)
      if (ship.cargo[c].kind == CARGO_MARKET)
        cargoValue += ship.cargo[c].paidUnitPrice * ship.cargo[c].quantity + ship.cargo[c].contractId;
  }
  for (int p = 0; p < MAX_PORTS; ++p)
    for (int c = 0; c < g_ports[p].warehouseCount; ++c)
      if (g_ports[p].warehouse[c].kind == CARGO_MARKET)
        cargoValue += g_ports[p].warehouse[c].paidUnitPrice * g_ports[p].warehouse[c].quantity +
                      g_ports[p].warehouse[c].contractId;
  for (int region = 0; region < REGION_COUNT; ++region) regions += !!(g_campaign.regionMask & (1u << region));
  std::printf("%u,%s,%u,%d,%d,%u,%u,%u,%d,%d,%d,%u,%u,%u,%u,%d,%u,%u,%lld,%lld,%lld,%u,%u,%lld,%lld,%u,%u,%u,%u,%u,%u,%u,%u,%d,%d,%d,%u,%u,%u,%d,%lld,%u,%lld,%lld,%lld\n",
    seed, NAMES[policy], g_campaign.worldMinutes / 600u, g_campaign.coins, g_campaign.recoveryDebt,
    g_campaign.shipCount, g_campaign.operatingCosts, g_campaign.contractRevenue, g_campaign.marketProfit,
    g_campaign.capitalSpending, g_campaign.incidentNet, g_campaign.deliveries, g_campaign.contractsCompleted,
    g_campaign.incidentsResolved, s.reliefClaims, s.maxDebt, s.repairs, s.repairSpend,
    (long long)s.marketSpend, (long long)s.marketReceipts, (long long)s.marketNetReceipts,
    s.buys, s.sells, (long long)cargoValue, (long long)fleetValue, s.firstDelivery, s.secondShip,
    s.stage3, s.firstCharter, s.dispatches, s.disabledEvents, s.maxNoProgress, s.stallMinutes,
    disabled, moving, regions, g_campaign.rank, sim_stage(), s.invariantFailures, s.minCash,
    (long long)s.reliefCash, s.decisionRounds, (long long)s.debtWithheld,
    (long long)cash_error(s), (long long)s.maxCashError);
}

static bool run(uint32_t seed, Policy policy) {
  legacy_campaign(seed);
  g_ui.title = 0; g_campaign.tutorialDismissed = 1;
  Stats stats{}; stats.rng = seed ^ (0x9e3779b9u * (uint32_t)(policy + 1));
  if (!stats.rng) stats.rng = 1;
  uint32_t checkpoint = 0;
  uint32_t lastDelivery = 0, lastSales = 0;
  for (uint32_t tick = 0; tick < HORIZON; tick += 60) {
    if (tick % (policy == CASUAL ? 1200u : 60u) == 0) {
      ++stats.decisionRounds;
      resolve_incidents(policy, stats);
      for (uint8_t i = 0; i < g_campaign.shipCount; ++i) service_ship(i, policy, stats);
      invest(policy);
      if (sim_recovery_available()) {
        SimResult result = sim_claim_recovery();
        if (result) { ++stats.reliefClaims; stats.reliefCash += result.value; }
      }
    }
    int32_t debtBeforeAdvance = g_campaign.recoveryDebt;
    sim_advance_minutes(60);
    if (g_campaign.recoveryDebt < debtBeforeAdvance)
      stats.debtWithheld += debtBeforeAdvance - g_campaign.recoveryDebt;
    stats.minCash = std::min(stats.minCash, g_campaign.coins);
    stats.maxDebt = std::max(stats.maxDebt, g_campaign.recoveryDebt);
    uint32_t now = g_campaign.worldMinutes;
    if (!stats.firstDelivery && g_campaign.deliveries) stats.firstDelivery = now;
    if (!stats.secondShip && g_campaign.shipCount > 1) stats.secondShip = now;
    if (!stats.stage3 && sim_stage() >= 3) stats.stage3 = now;
    if (!stats.firstCharter && (g_campaign.regionMask & ~1u)) stats.firstCharter = now;
    bool sailing = false;
    for (int i = 0; i < g_campaign.shipCount; ++i)
      sailing |= g_ships[i].state == SHIP_SAILING;  // unanswered incidents do not count as movement
    bool progress = sailing || lastDelivery != g_campaign.deliveries || lastSales != stats.sells;
    stats.noProgress = progress ? 0 : stats.noProgress + 60;
    stats.maxNoProgress = std::max(stats.maxNoProgress, stats.noProgress);
    if (stats.noProgress >= STALL_WINDOW) stats.stallMinutes += 60;
    lastDelivery = g_campaign.deliveries; lastSales = stats.sells;
    stats.maxCashError = std::max(stats.maxCashError, (int64_t)std::llabs(cash_error(stats)));
    if (!invariant() || stats.maxCashError || stats.invariantFailures) {
      ++stats.invariantFailures;
      std::fprintf(stderr, "invariant failure seed=%u policy=%s worldMinute=%u\n", seed, NAMES[policy], now);
      emit(seed, policy, stats); return false;
    }
    if (checkpoint < sizeof(CHECKPOINTS) / sizeof(CHECKPOINTS[0]) && now >= CHECKPOINTS[checkpoint]) {
      emit(seed, policy, stats); ++checkpoint;
    }
  }
  return true;
}

int main(int argc, char** argv) {
  int seeds = argc > 1 ? std::atoi(argv[1]) : 1000;
  if (seeds < 1) return 2;
  int onlyPolicy = argc > 2 ? std::atoi(argv[2]) : -1;
  if (onlyPolicy < -1 || onlyPolicy >= POLICY_COUNT) return 2;
  verifyPlans = std::getenv("SH_ECONOMY_VERIFY_PLANS") != nullptr;
  std::fprintf(stderr, "Crosswinds economy Monte Carlo: seeds=%d policy=%d horizon_world_minutes=%u step_world_minutes=60 seed_formula=1000+i*7919\n", seeds, onlyPolicy, HORIZON);
  std::puts("seed,policy,real_minutes,cash,debt,ships,operating_costs,contract_revenue,market_profit,capital_spending,incident_net,delivered_units,contracts,incidents,relief_claims,max_debt,repairs,repair_spend,market_spend,market_gross_receipts,market_net_receipts,bought_units,sold_units,cargo_paid_value,fleet_condition_value,first_delivery_world_minute,second_ship_world_minute,stage3_world_minute,first_charter_world_minute,dispatches,disabled_events,max_no_progress_world_minutes,stall_world_minutes,disabled_ships,moving_ships,regions,rank,stage,invariant_failures,min_cash,relief_cash,decision_rounds,debt_withheld,cash_reconciliation_error,max_cash_reconciliation_error");
  int failures = 0;
  for (int policy = 0; policy < POLICY_COUNT; ++policy) {
    if (onlyPolicy >= 0 && policy != onlyPolicy) continue;
    for (int i = 0; i < seeds; ++i)
      if (!run(1000u + (uint32_t)i * 7919u, (Policy)policy)) ++failures;
    std::fprintf(stderr, "%s: %d seeds complete\n", NAMES[policy], seeds);
  }
  return failures ? 1 : 0;
}
