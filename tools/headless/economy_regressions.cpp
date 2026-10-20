#include "game.h"
#include "economy_fixtures.h"
#include <cstdio>
#include <cstring>

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

static int failures;
static void check(bool ok, const char* reason) {
  if (!ok) { ++failures; std::printf("FAIL: %s\n", reason); }
}

static void open_world() {
  g_campaign.stageSeen = 5;
  g_campaign.rank = 5;
  g_campaign.regionMask = g_campaign.chartMask = 0x3f;
  for (int p = 0; p < MAX_PORTS; ++p) g_ports[p].unlocked = 1;
}

static void exact_market_accounting(bool exhaustive) {
  // Advanced-state fixtures isolate market accounting; they do not claim that
  // a starting campaign owns an Indiaman or has reached these ports organically.
  long cases = 0;
  for (int event = 0; event < 3; ++event) {
    for (int p = 0; p < MAX_PORTS; ++p) {
      int cls = PORT_DEFS[p].portClass == PORT_DEEPWATER ? 4 :
                PORT_DEFS[p].portClass == PORT_HARBOR ? 2 : 1;
      for (int good = 0; good < GOOD_COUNT; ++good) {
        for (int stock = 1; stock <= (exhaustive ? 64 : 32); ++stock) {
          int maxQuantity = stock < SHIP_CLASS_DEFS[cls].hold ? stock : SHIP_CLASS_DEFS[cls].hold;
          for (int quantity = exhaustive ? 1 : maxQuantity; quantity <= maxQuantity; ++quantity) {
            legacy_campaign(51); open_world();
            g_campaign.coins = 100000; g_campaign.recoveryDebt = 1000;
            g_campaign.worldEventType = event == 1 ? EVENT_SHORTAGE : event == 2 ? EVENT_GLUT : EVENT_NONE;
            g_campaign.worldEventGood = (uint8_t)good;
            g_campaign.worldEventRegion = PORT_DEFS[p].region;
            g_campaign.worldEventDays = 5;
            Ship& ship = g_ships[0]; ship.classId = (uint8_t)cls; ship.currentPort = (uint8_t)p;
            g_ports[p].stock[good] = (uint16_t)stock;
            for (int i = 0; i < quantity; ++i) check(sim_market_buy(0, (uint8_t)good), "sweep purchase succeeds");
            MarketQuote quote = sim_market_quote(0, (uint8_t)good, false, (uint8_t)quantity);
            int32_t beforeSale = g_campaign.coins;
            for (int i = 0; i < quantity; ++i) check(sim_market_sell(0, (uint8_t)good), "sweep sale succeeds");
            int32_t repaid = 1000 - g_campaign.recoveryDebt;
            check(g_campaign.marketProfit == g_campaign.coins - 100000 + repaid,
                  "realized profit equals actual purchase/sale cash before debt repayment");
            check(quote.treasuryDelta == g_campaign.coins - beforeSale && quote.withholding == repaid,
                  "sale quote preserves exact execution and debt allocation");
            check(!ship.cargoCount, "complete liquidation removes the stack");
            check(g_campaign.coins - 100000 + repaid <= 0, "instant round trip never creates cash");
            ++cases;
          }
        }
      }
    }
  }
  std::printf("Exact market accounting: %ld port/good/stock/event cases\n", cases);

  legacy_campaign(51); open_world(); g_campaign.coins = 100000;
  g_ships[0].classId = 4; g_ships[0].currentPort = 18; g_ports[18].stock[GOOD_GRAIN] = 15;
  for (int i = 0; i < 14; ++i) check(sim_market_buy(0, GOOD_GRAIN), "Spicehaven purchase");
  check(g_campaign.coins == 99593, "Spicehaven 14 grain cost 407");
  // Warehouse transfers preserve serialized cost precision, including the
  // remainder, without changing save v7's existing CargoItem representation.
  check(sim_transfer_to_warehouse(0, 0) && sim_transfer_from_warehouse(0, 0), "warehouse round trip preserves cargo");
  for (int i = 0; i < 14; ++i) check(sim_market_sell(0, GOOD_GRAIN), "Spicehaven liquidation");
  check(g_campaign.coins == 99934 && g_campaign.marketProfit == -66,
        "Spicehaven loss is reported as -66, never +19");

  // A legacy v7 stack has zero in the unused market contract field. Its
  // saved unit price remains its best-known basis; new purchases add exactly.
  legacy_campaign(52); open_world(); g_campaign.coins = 10000;
  CargoItem legacy{}; legacy.kind = CARGO_MARKET; legacy.quantity = 3;
  legacy.good = GOOD_GRAIN; legacy.paidUnitPrice = 17; legacy.destination = NO_PORT;
  g_ships[0].cargo[0] = legacy; g_ships[0].cargoCount = 1;
  check(sim_market_buy(0, GOOD_GRAIN), "purchase merges with legacy v7 market cargo");
  for (int i = 0; i < 4; ++i) check(sim_market_sell(0, GOOD_GRAIN), "legacy stack liquidation");
  check(g_campaign.marketProfit == g_campaign.coins - 10000 - 51,
        "legacy stack's saved cost is retained while merged purchase is exact");

  legacy_campaign(57); open_world(); g_campaign.coins = 10000;
  g_ships[0].classId = 4; g_ports[0].stock[GOOD_GRAIN] = 13;
  for (int i = 0; i < 7; ++i) check(sim_market_buy(0, GOOD_GRAIN), "interleaved first purchase");
  for (int i = 0; i < 3; ++i) check(sim_market_sell(0, GOOD_GRAIN), "partial sale allocates basis");
  check(sim_transfer_to_warehouse(0, 0) && sim_transfer_from_warehouse(0, 0), "partial stack survives warehouse transfer");
  for (int i = 0; i < 6; ++i) check(sim_market_buy(0, GOOD_GRAIN), "new purchase merges after partial sales");
  for (int i = 0; i < 10; ++i) check(sim_market_sell(0, GOOD_GRAIN), "interleaved final sale");
  check(g_campaign.marketProfit == g_campaign.coins - 10000 && !g_ships[0].cargoCount,
        "partial sales, warehouse storage and restocking conserve the original cash cost");
}

static bool dispatch_any(uint8_t ship) {
  g_ui.selectedShip = ship;
  for (int p = 0; p < MAX_PORTS; ++p) {
    sim_route_clear();
    if (sim_route_add((uint8_t)p) && sim_dispatch(ship)) return true;
  }
  return false;
}

static void fleet_fixture(bool firstDisabled, bool secondDisabled, bool samePort) {
  legacy_campaign(51); open_world(); g_campaign.coins = 1200;
  g_ui.selectedPort = 0;
  check(sim_buy_ship(0), "second hull is purchased through the actual game API");
  // Reachable snapshot after buying an uncrewed hull while the original
  // captain sails elsewhere and is disabled. No new captain is invented.
  g_ships[0].state = firstDisabled ? SHIP_DISABLED : SHIP_DOCKED;
  g_ships[0].condition = firstDisabled ? 25 : 100;
  g_ships[0].currentPort = g_ships[0].fromPort = g_ships[0].toPort = samePort ? 0 : 1;
  g_captains[g_ships[0].captainId].currentPort = g_ships[0].currentPort;
  g_ships[1].state = secondDisabled ? SHIP_DISABLED : SHIP_DOCKED;
  g_ships[1].condition = secondDisabled ? 25 : 100;
  g_campaign.coins = 0;
}

static void recovery_regressions() {
  fleet_fixture(true, false, false);
  check(sim_recovery_available() && sim_claim_recovery(), "disabled captain-bearing ship qualifies despite remote uncrewed hull");
  check(g_ui.selectedShip == 0 && g_ships[0].state == SHIP_DOCKED && sim_ship_captain(0),
        "relief restores the captain-bearing Belmar ship");
  check(dispatch_any(0), "one relief claim permits immediate actual dispatch");

  fleet_fixture(false, true, false);
  // Transfer the only captain to ship 1 while both were at Kingsfall, then
  // represent ship 1 disabled elsewhere and ship 0 disabled at Kingsfall.
  uint8_t captain = g_ships[0].captainId;
  g_ships[0].captainId = NO_SHIP; g_ships[0].state = SHIP_DISABLED; g_ships[0].condition = 25;
  g_ships[0].currentPort = g_ships[0].fromPort = g_ships[0].toPort = 0;
  g_ships[1].captainId = captain;
  g_ships[1].currentPort = g_ships[1].fromPort = g_ships[1].toPort = 1;
  g_captains[captain].assignedShip = 1; g_captains[captain].currentPort = 1;
  check(sim_recovery_available() && sim_claim_recovery(), "all-disabled recovery ignores first uncrewed hull");
  check(g_ui.selectedShip == 1 && g_ships[0].state == SHIP_DISABLED && dispatch_any(1),
        "all-disabled fallback funds and dispatches the captain-bearing hull");

  fleet_fixture(true, false, true);
  check(sim_recovery_available() && sim_claim_recovery(), "local captain can crew an existing healthy hull");
  check(g_ui.selectedShip == 1 && g_ships[0].state == SHIP_DISABLED && sim_ship_captain(1) && dispatch_any(1),
        "local transfer saves needless hull restoration and is immediately usable");

  legacy_campaign(53); open_world();
  g_ships[0].classId = 4; g_ships[0].state = SHIP_DISABLED;
  g_ships[0].condition = 25; g_ships[0].currentPort = 21; g_captains[0].currentPort = 21;
  g_campaign.coins = 0;
  check(sim_recovery_available() && sim_claim_recovery() && dispatch_any(0),
        "distant heavy ship receives enough departure funds in one relief claim");
}

static void route_lots_and_incident_loss() {
  legacy_campaign(54); open_world(); g_campaign.coins = 10000;
  Ship& ship = g_ships[0]; ship.classId = 4;
  CargoItem expensive{}; expensive.kind = CARGO_MARKET; expensive.good = GOOD_GRAIN;
  expensive.quantity = 2; expensive.paidUnitPrice = 100;
  expensive.destination = NO_PORT; expensive.projectRegion = 0xff;
  CargoItem cheap = expensive; cheap.quantity = 2; cheap.paidUnitPrice = 1;
  ship.cargo[0] = expensive; ship.cargo[1] = cheap; ship.cargoCount = 2;
  ship.routeTemplate.origin = 0; ship.routeTemplate.stops[0] = 3;
  ship.routeTemplate.stopCount = 1; ship.routeTemplate.sellMode = ROUTE_SELL_PROFIT;
  ship.routeTemplate.buyGood = NO_PORT;
  check(sim_route_prepare_preview(0) && !g_routePreparation.soldUnits,
        "later profitable warehouse lot cannot force selling expensive first lot");
  check(sim_route_prepare_apply(0) && ship.cargoCount == 2 && g_campaign.coins == 10000,
        "route apply agrees with first-lot sale preview and preserves loss-making cargo");

  // Jettison is a real unit removal, so the remaining stack must retain only
  // its share of cost, not charge future sales for the lost unit as well.
  legacy_campaign(55); open_world(); g_campaign.coins = 10000;
  g_ports[0].stock[GOOD_GRAIN] = 12;
  for (int i = 0; i < 4; ++i) check(sim_market_buy(0, GOOD_GRAIN), "incident stack purchase");
  uint16_t lostCost = g_ships[0].cargo[0].paidUnitPrice;
  int32_t spent = 10000 - g_campaign.coins;
  g_incident.active = 1; g_incident.shipIndex = 0; g_incident.kind = INCIDENT_PIRATES;
  g_ships[0].state = SHIP_AWAITING_DECISION;
  g_campaign.coins = 0;
  sim_resolve_incident(2); // Cannot afford a bribe; jettisons one market unit.
  check(g_ships[0].cargo[0].quantity == 3, "pirate incident removes exactly one purchased unit");
  g_ships[0].state = SHIP_DOCKED; g_campaign.coins = 0; g_campaign.marketProfit = 0;
  for (int i = 0; i < 3; ++i) check(sim_market_sell(0, GOOD_GRAIN), "remaining incident cargo sells");
  check(g_campaign.marketProfit == g_campaign.coins - (spent - lostCost),
        "incident unit loss removes its exact allocated purchase basis");
}

static void office_regression() {
  legacy_campaign(56); open_world(); g_campaign.coins = 5000;
  g_ports[0].officeLevel = 2; g_campaign.won = 0;
  sim_generate_jobs(0, true);
  int before = 0; for (const Contract& c : g_contracts[0]) if (c.active == 1) ++before;
  check(before == 9, "level-two office has nine offers");
  check(sim_buy_port_upgrade(0) && g_campaign.coins == 4040 && g_ports[0].officeLevel == 3,
        "paid level-three office remains priced at 960");
  int after = 0; for (const Contract& c : g_contracts[0]) if (c.active == 1) ++after;
  check(after == 10, "final office upgrade supplies the tenth offer before winning");
}

int main(int argc, char** argv) {
  bool exhaustive = argc == 2 && !std::strcmp(argv[1], "--exhaustive");
  if (argc > 1 && !exhaustive) return 2;
  exact_market_accounting(exhaustive); recovery_regressions(); route_lots_and_incident_loss(); office_regression();
  std::printf("Economy regressions: failures=%d\n", failures);
  return failures ? 1 : 0;
}
