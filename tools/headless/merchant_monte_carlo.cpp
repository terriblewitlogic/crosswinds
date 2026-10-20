// Policy simulation, not persistence evidence: save_write is deliberately stubbed.
// All buys, routes, arrivals and explicit sales execute the production engine.
#include "game.h"
#include "encounter_content.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <map>
#include "atlas_content.h"

bool save_write() { return true; }
void audio_begin() {} void audio_tick() {} void audio_notice() {} void audio_alert() {}
static uint32_t policyRng;
static uint32_t roll() { policyRng ^= policyRng << 13; policyRng ^= policyRng >> 17;
  policyRng ^= policyRng << 5; return policyRng; }
static bool docked(const Ship& s) { return s.state == SHIP_DOCKED || s.state == SHIP_ARRIVED; }
struct Run {
  int incidents = 0;
  int64_t incidentCash = 0, incidentGoods[GOOD_COUNT]{}, opening[GOOD_COUNT]{};
  int failures = 0, trades = 0, losingSales = 0, trips = 0, packageAccepted = 0, stranded = 0;
  int32_t minCash = 500;
  int64_t purchases = 0, sales = 0, packageCash = 0, investments = 0, realizedProfit = 0;
  int64_t bought[GOOD_COUNT]{}, sold[GOOD_COUNT]{};
  int64_t finalCash = 0, finalBasis = 0, finalLiquidation = 0;
  int64_t promisedPaid = 0;
  int64_t atlasCash = 0;  // Sea Atlas region rewards, paid on arrival
  std::map<uint32_t, uint16_t> pendingPackages;
  uint32_t firstSale = 0, secondShip = 0;
  uint16_t lowPrice = UINT16_MAX, highPrice = 0;
};
static void inventory(uint8_t ship, uint8_t* counts) {
  memset(counts, 0, GOOD_COUNT);
  for (uint8_t i = 0; i < g_ships[ship].cargoCount; ++i) {
    const CargoItem& c = g_ships[ship].cargo[i];
    if (c.kind == CARGO_MARKET) counts[c.good] += c.quantity;
  }
}
static void verify(Run& run) {
  if (g_campaign.coins != 500 - run.purchases + run.sales + run.packageCash + run.atlasCash + run.incidentCash - run.investments ||
      g_campaign.coins < 0) ++run.failures;
  run.minCash = std::min(run.minCash, g_campaign.coins);
  int64_t held[GOOD_COUNT]{}; unsigned packages = 0;
  for (uint8_t ship = 0; ship < g_campaign.shipCount; ++ship) {
    const Ship& s = g_ships[ship];
    if (sim_ship_used_capacity(s) > sim_ship_capacity(s) || s.cargoCount > MAX_CARGO_ITEMS ||
        s.condition != 100 || s.state == SHIP_DISABLED) ++run.failures;
    for (uint8_t slot = 0; slot < s.cargoCount; ++slot) {
      const CargoItem& c = s.cargo[slot];
      if (c.kind == CARGO_MARKET) {
        if (c.good >= GOOD_COUNT || c.contractId >= c.quantity) ++run.failures;
        else held[c.good] += c.quantity;
      } else if (c.jobKind == JOB_PACKAGE) {
        ++packages;
        if (c.good != NO_PORT || c.quantity != 1 || c.paidUnitPrice) ++run.failures;
      } else ++run.failures;
    }
  }
  for (uint8_t good = 0; good < GOOD_COUNT; ++good)
    if (held[good] != run.opening[good] + run.bought[good] - run.sold[good] + run.incidentGoods[good]) ++run.failures;
  for (auto it = run.pendingPackages.begin(); it != run.pendingPackages.end();) {
    unsigned copies = 0;
    for (uint8_t ship = 0; ship < g_campaign.shipCount; ++ship)
      for (uint8_t slot = 0; slot < g_ships[ship].cargoCount; ++slot) {
        const CargoItem& c = g_ships[ship].cargo[slot];
        copies += c.jobKind == JOB_PACKAGE && c.contractId == it->first;
      }
    if (!copies) { run.promisedPaid += it->second; it = run.pendingPackages.erase(it); }
    else { if (copies != 1) ++run.failures; ++it; }
  }
  if (packages != run.pendingPackages.size() || run.packageCash != run.promisedPaid ||
      packages + g_campaign.contractsCompleted != (unsigned)run.packageAccepted) ++run.failures;
}
static bool apply_trade(Run& run, uint8_t ship, bool buying, const uint8_t* quantities) {
  auto quote = sim_merchant_trade_offer(ship, buying, quantities);
  if (quote.code != SIM_OK || !quote.quantity) return false;
  int32_t cash = g_campaign.coins;
  if (!sim_merchant_trade_apply(ship, quote)) { ++run.failures; return false; }
  if (g_campaign.coins != cash + quote.cashDelta) ++run.failures;
  if (buying) {
    run.purchases += quote.total;
    for (uint8_t good = 0; good < GOOD_COUNT; ++good) if (quantities[good]) {
      run.bought[good] += quantities[good];
      run.lowPrice = std::min(run.lowPrice, quote.unitPrices[good]);
      run.highPrice = std::max(run.highPrice, quote.unitPrices[good]);
    }
  } else {
    run.sales += quote.cashDelta; run.realizedProfit += quote.profit; ++run.trades;
    run.losingSales += quote.profit < 0;
    for (uint8_t good = 0; good < GOOD_COUNT; ++good) run.sold[good] += quantities[good];
    if (!run.firstSale) run.firstSale = g_campaign.worldMinutes / 600;
  }
  return true;
}
static bool sail(Run& run, uint8_t ship, uint8_t destination) {
  auto quote = sim_merchant_sail_offer(ship, destination);
  if (quote.code != SIM_OK) return false;
  int32_t cash = g_campaign.coins;
  if (!sim_merchant_sail(ship, quote)) { ++run.failures; return false; }
  if (g_campaign.coins != cash) ++run.failures;
  ++run.trips; return true;
}
static bool package_trip(Run& run, uint8_t ship) {
  auto offer = sim_package_offer(ship, 0);
  if (offer.code != SIM_OK) return false;
  const uint8_t destination = offer.destination;
  bool accepted = false;
  for (uint8_t tries = 0; tries < MAX_CONTRACTS_PER_PORT; ++tries) {
    bool found = false; uint8_t count = sim_package_count(ship);
    for (uint8_t row = 0; row < count; ++row) {
      auto next = sim_package_offer(ship, row);
      if (next.code != SIM_OK || next.destination != destination) continue;
      int32_t cash = g_campaign.coins;
      if (sim_package_accept(ship, next)) {
        if (cash != g_campaign.coins) ++run.failures;
        if (!run.pendingPackages.emplace(next.id, next.reward).second) ++run.failures;
        ++run.packageAccepted; accepted = found = true; break;
      }
    }
    if (!found) break;
  }
  return accepted && sail(run, ship, destination);
}
struct Choice { uint8_t good = NO_PORT, count = 0, destination = NO_PORT; double score = 0; };
static Choice thoughtful_purchase(uint8_t ship, const uint8_t* ports, uint8_t count) {
  Choice best; const Ship original = g_ships[ship];
  for (uint8_t good = 0; good < GOOD_COUNT; ++good) {
    uint8_t qty[GOOD_COUNT]{};
    MerchantTradeQuote buy{};
    for (int amount = sim_ship_capacity(original); amount > 0; --amount) {
      qty[good] = (uint8_t)amount; buy = sim_merchant_trade_offer(ship, true, qty);
      if (buy.code == SIM_OK) break;
    }
    if (buy.code != SIM_OK) continue;
    // A hypothetical owned lot uses the exact current reviewed purchase basis.
    // Only this ship copy is replaced, and restored before any real action.
    Ship hypothetical = original; CargoItem c{};
    c.kind = CARGO_MARKET; c.jobKind = JOB_FREIGHT; c.good = good; c.quantity = qty[good];
    c.paidUnitPrice = buy.total / c.quantity; c.contractId = buy.total % c.quantity;
    c.sourcePort = original.currentPort; c.destination = c.projectRegion = NO_PORT;
    hypothetical.cargo[0] = c; hypothetical.cargoCount = 1; g_ships[ship] = hypothetical;
    for (uint8_t row = 0; row < count; ++row) {
      auto route = sim_merchant_sail_offer(ship, ports[row]);
      if (route.code != SIM_OK || !route.minutes) continue;
      double score = (route.saleEstimate - buy.total) * 600.0 / route.minutes;
      if (score > best.score) best = Choice{good, c.quantity, ports[row], score};
    }
    g_ships[ship] = original;
  }
  return best;
}
static void decide(Run& run, int policy, int fleetCap) {
  while (g_incident.active) {
    uint8_t option = policy == 1 ? roll() % 3 : g_incident.kind == INCIDENT_TRADE ? 2 : 0;
    // An encounter that refuses the choice (no hold space, no matching goods)
    // takes the same fallback the game offers when its timer runs out.
    if (sim_merchant_incident_choice(option).code != SIM_OK) option = encounter_fallback(g_incident.kind);
    const uint8_t shipIndex = g_incident.shipIndex;
    if (!sim_merchant_incident_resolve(option)) { ++run.failures; break; }
    ++run.incidents; run.incidentCash += g_incidentOutcome.treasuryDelta;
    for (uint8_t i=0;i<g_incidentOutcome.cargoCount;++i) {
      const auto& change=g_incidentOutcome.cargo[i];
      if(change.kind==CARGO_MARKET && change.good<GOOD_COUNT) run.incidentGoods[change.good]+=change.quantityDelta;
    }
    // A carried parcel is a package taken on at sea; its fixed reward sits on
    // the board it was issued from, like any accepted offer.
    for (uint8_t slot = 0; slot < g_ships[shipIndex].cargoCount; ++slot) {
      const CargoItem& c = g_ships[shipIndex].cargo[slot];
      if (c.jobKind != JOB_PACKAGE || run.pendingPackages.count(c.contractId) || c.sourcePort >= MAX_PORTS) continue;
      for (const Contract& offer : g_contracts[c.sourcePort])
        if (offer.id == c.contractId) { run.pendingPackages.emplace(c.contractId, offer.reward); ++run.packageAccepted; }
    }
  }
  if (fleetCap > g_campaign.shipCount && g_campaign.coins >= sim_ship_package_cost(0) + 500 &&
      sim_feature_unlocked(FEATURE_YARD)) {
    g_ui.selectedPort = 0; int32_t before = g_campaign.coins;
    if (sim_buy_ship(0)) {
      run.investments += before - g_campaign.coins;
      if (!run.secondShip) run.secondShip = g_campaign.worldMinutes / 600;
    }
  }
  for (uint8_t ship = 0; ship < g_campaign.shipCount; ++ship) {
    if (!docked(g_ships[ship])) continue;
    uint8_t aboard[GOOD_COUNT]; inventory(ship, aboard);
    bool hasGoods = false; for (uint8_t good : aboard) hasGoods |= good != 0;
    // Real sale is a separate decision after arrival; no projected value is banked.
    if (hasGoods && !apply_trade(run, ship, false, aboard)) { ++run.failures; continue; }
    uint8_t ports[MAX_PORTS]; uint8_t count = sim_merchant_destinations(ship, ports, MAX_PORTS);
    if (!count) { ++run.stranded; continue; }
    if (policy == 2) { package_trip(run, ship); continue; }
    Choice choice;
    if (policy == 0) choice = thoughtful_purchase(ship, ports, count);
    else {
      uint8_t start = roll() % GOOD_COUNT;
      for (uint8_t n = 0; n < GOOD_COUNT; ++n) {
        uint8_t good = (start + n) % GOOD_COUNT; uint8_t amounts[GOOD_COUNT]{};
        amounts[good] = (uint8_t)(1 + roll() % sim_ship_capacity(g_ships[ship]));
        if (sim_merchant_trade_offer(ship, true, amounts).code == SIM_OK) {
          choice = Choice{good, amounts[good], ports[roll() % count], 1}; break;
        }
      }
    }
    if (choice.good == NO_PORT) { package_trip(run, ship); continue; }
    uint8_t amount[GOOD_COUNT]{}; amount[choice.good] = choice.count;
    if (apply_trade(run, ship, true, amount) && !sail(run, ship, choice.destination)) ++run.failures;
  }
}
static Run run_company(uint32_t seed, int policy) {
  sim_new_campaign(seed); g_campaign.onboardingStep = ONBOARD_DONE; policyRng = seed ^ (0x1234567u * (policy + 1));
  g_ui.returnQuiet = 1; Run run;
  // A new company starts with a gift aboard: owned goods with no purchase.
  for (uint8_t ship = 0; ship < g_campaign.shipCount; ++ship) {
    uint8_t aboard[GOOD_COUNT]; inventory(ship, aboard);
    for (uint8_t good = 0; good < GOOD_COUNT; ++good) run.opening[good] += aboard[good];
  }
  int attentionMinutes = seed % 2 ? 1 : 5, fleetCap = seed % 3 ? 1 : 3;
  for (unsigned minute = 0; minute < 120; ++minute) {
    if (minute % attentionMinutes == 0) decide(run, policy, fleetCap);
    verify(run); int32_t before = g_campaign.coins; uint8_t regions = g_atlas.regionsRewarded;
    sim_advance_minutes(600);
    const int32_t atlas = ATLAS_REGION_REWARD * __builtin_popcount((unsigned)(g_atlas.regionsRewarded & ~regions));
    run.atlasCash += atlas; run.packageCash += g_campaign.coins - before - atlas;
    if (g_campaign.coins < before) ++run.failures;
    verify(run);
  }
  run.finalCash = g_campaign.coins;
  for (uint8_t ship = 0; ship < g_campaign.shipCount; ++ship) {
    for (uint8_t slot = 0; slot < g_ships[ship].cargoCount; ++slot) {
      const CargoItem& c = g_ships[ship].cargo[slot];
      if (c.kind == CARGO_MARKET) run.finalBasis += c.quantity * c.paidUnitPrice + c.contractId;
    }
    uint8_t destination = docked(g_ships[ship]) ? g_ships[ship].currentPort : g_ships[ship].merchant.destination;
    auto estimated = sim_merchant_sale_estimate(ship, destination);
    if (estimated.code == SIM_OK) run.finalLiquidation += estimated.cashDelta;
  }
  return run;
}
int main(int argc, char** argv) {
  unsigned seeds = argc > 1 ? (unsigned)std::strtoul(argv[1], nullptr, 10) : 100;
  if (!seeds || seeds > 1000) return 2;
  const char* policies[] = {"thoughtful", "random", "package_only"};
  int failures = 0;
  std::printf("policy,seed,cash,inventory_basis,liquidation_estimate,investments,merchant_profit,package_cash,trades,losing_sales,trips,min_cash,first_sale_min,second_ship_min,stranded,failures\n");
  for (int policy = 0; policy < 3; ++policy) {
    int64_t cash = 0, profit = 0, packages = 0, held = 0, investment = 0;
    int incidents = 0; int64_t incidentCash = 0;
    int losing = 0, trades = 0, trips = 0, insolvent = 0, stranded = 0;
    for (unsigned index = 0; index < seeds; ++index) {
      uint32_t seed = 51 + index * 7919; Run r = run_company(seed, policy); failures += r.failures;
      cash += r.finalCash; profit += r.realizedProfit; packages += r.packageCash;
      held += r.finalBasis; investment += r.investments; losing += r.losingSales; trades += r.trades;
      incidents += r.incidents; incidentCash += r.incidentCash; trips += r.trips; insolvent += r.minCash < 0; stranded += r.stranded;
      std::printf("%s,%u,%lld,%lld,%lld,%lld,%lld,%lld,%d,%d,%d,%d,%u,%u,%d,%d\n", policies[policy], seed,
        (long long)r.finalCash, (long long)r.finalBasis, (long long)r.finalLiquidation, (long long)r.investments,
        (long long)r.realizedProfit, (long long)r.packageCash, r.trades, r.losingSales, r.trips, r.minCash,
        r.firstSale, r.secondShip, r.stranded, r.failures);
    }
    std::fprintf(stderr, "%s: companies=%u meanCash=%.2f meanHeldCost=%.2f meanInvestment=%.2f meanMerchantProfit=%.2f meanPackageCash=%.2f trades=%d losingSales=%d trips=%d insolvent=%d stranded=%d incidents=%d meanIncidentCash=%.2f\n",
      policies[policy], seeds, cash / double(seeds), held / double(seeds), investment / double(seeds),
      profit / double(seeds), packages / double(seeds), trades, losing, trips, insolvent, stranded, incidents, incidentCash / double(seeds));
  }
  std::fprintf(stderr, "merchant Monte Carlo: %s (%u companies, 120 real minutes each; %d invariant failures; mocked saves)\n",
    failures ? "FAIL" : "PASS", seeds * 3, failures);
  return failures ? 1 : 0;
}
