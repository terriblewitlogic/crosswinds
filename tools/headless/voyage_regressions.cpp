#include "game.h"
#include "economy_fixtures.h"
#include <cstdio>
#include <cstring>

static bool writesSucceed = true;
bool save_write() { return writesSucceed; }
bool save_begin() { return true; }
bool save_exists() { return false; }
bool save_read() { return false; }
bool save_reset() { return true; }
bool save_selftest() { return true; }
void audio_begin() {}
void audio_tick() {}
void audio_notice() {}
void audio_alert() {}

static int checks, failures;
static void check(bool passed, const char* name) {
  ++checks;
  if (!passed) { ++failures; std::printf("FAIL: %s\n", name); }
}

// Historical signed-order fixtures intentionally use economy v1. Production
// companies start in v2; merchant_regressions covers its owned-cargo rules.
static void signed_campaign(uint32_t seed) {
  historical_campaign(seed, 1);
}

static void arrive(uint8_t ship = 0) {
  int guard = 0;
  while (g_ships[ship].state == SHIP_SAILING && guard++ < 20000) sim_advance_minutes(10);
  check(g_ships[ship].booking.state == BOOKING_SETTLED, "booked voyage settles without intervention");
  check(!g_incident.active, "standard voyage never blocks for an incident");
}

static void starter_fixtures() {
  signed_campaign(51);
  VoyageQuote q = sim_voyage_offer(0, 1, VOYAGE_FREIGHT, GOOD_TEXTILES, 4);
  check(q.code == SIM_OK && q.minutes == 1200 && q.payNow == 40 && q.payout == 100 && q.profit == 60,
        "guided Textiles freight is 2 min, pay 40, bank 100, earn 60");
  check(q.buyerPayment == 84 && q.bonus == 16, "freight full-hold bonus floors once at twenty percent");
  check(sim_voyage_dispatch(0, q) && g_campaign.coins == 460, "one dispatch prepays actual freight cost");
  Ship sailingBefore = g_ships[0]; Campaign planningBefore = g_campaign;
  PortState planningPorts[MAX_PORTS]; memcpy(planningPorts, g_ports, sizeof(planningPorts));
  VoyageQuote plan = sim_return_voyage_quote(0, 0, VOYAGE_TRADE, GOOD_GRAIN, 4);
  check(plan.code == SIM_OK && plan.planningOnly && plan.origin == 1 && plan.destination == 0 && plan.profit == 64,
        "sailing player can plan the useful Grain return from its future port");
  check(sim_voyage_dispatch(0, plan).code == SIM_INVALID &&
        !memcmp(&sailingBefore, &g_ships[0], sizeof(sailingBefore)) &&
        !memcmp(&planningBefore, &g_campaign, sizeof(planningBefore)) &&
        !memcmp(planningPorts, g_ports, sizeof(planningPorts)),
        "return planning never reserves stock or money and cannot dispatch an active ship");
  sim_advance_minutes(1190);
  check(g_ships[0].state == SHIP_SAILING && g_campaign.coins == 460, "arrival never settles early");
  sim_advance_minutes(10);
  check(g_campaign.coins == 560 && g_campaign.deliveredUnits == 4 && g_ships[0].booking.profit == 60,
        "first arrival automatically banks and credits four delivered units");
  int32_t once = g_campaign.coins;
  for (int i = 0; i < 5; ++i) { sim_after_load(); sim_advance_minutes(60); }
  check(g_campaign.coins == once && g_campaign.deliveredUnits == 4,
        "reload and revisiting arrival never pays or credits twice");
  // Loading above exercises the real v2 migration. Select the historical
  // pricing fixture explicitly before constructing another old signed order.
  g_campaign.economyVersion = 1;
  q = sim_voyage_offer(0, 0, VOYAGE_TRADE, GOOD_GRAIN, 4);
  check(q.code == SIM_OK && q.minutes == 1800 && q.cargoCost == 72 && q.sailingCost == 32 &&
        q.payout == 168 && q.profit == 64, "Grain return books exact 3 minute +64 fixture");
  check(sim_voyage_dispatch(0, q) && g_campaign.coins == 456, "Grain cargo and sailing are one payment");
  arrive();
  check(g_campaign.coins == 624 && g_campaign.deliveredUnits == 8, "Grain trade advances the same company progress");
  q = sim_voyage_offer(0, 2, VOYAGE_TRADE, GOOD_IRONWARE, 4);
  check(q.minutes == 2400 && q.cargoCost == 180 && q.sailingCost == 56 && q.payout == 351 && q.profit == 115,
        "Ironware third voyage remains 4 min after captain levels up");
  check(sim_voyage_dispatch(0, q) && g_campaign.coins == 388, "third departure preserves the stated reserve");
  arrive();
  check(g_campaign.coins == 739 && g_campaign.deliveredUnits == 12 && g_ports[3].unlocked,
        "three paid deliveries open New Albion at twelve units");
  signed_campaign(52);
  q = sim_voyage_offer(0, 1, VOYAGE_TRADE, GOOD_TEXTILES, 4);
  check(q.payNow == 180 && q.payout == 260 && q.profit == 80 && q.bonus == 20,
        "merchant Textiles pays a bonus only on margin, not cargo principal");
  q = sim_voyage_offer(0, 1, VOYAGE_TRADE, GOOD_TEXTILES, 3);
  check(!q.bonus && q.payNow == 145 && q.profit == 35,
        "partial load stays editable and profitable without a hidden eighty-percent bonus");
  q = sim_voyage_offer(0, 1, VOYAGE_FREIGHT, GOOD_TEXTILES, 1);
  check(q.profit < 0 && q.code != SIM_OK && !sim_voyage_dispatch(0, q),
        "an unprofitable partial standard order cannot be sent as recommended work");
  signed_campaign(53);
  const uint8_t circuit[] = {1,0,2,0,1,0,2}; uint32_t minutes = 0;
  for (uint8_t destination : circuit) {
    q = sim_voyage_offer(0, destination, VOYAGE_TRADE, NO_PORT, 4);
    minutes += q.minutes;
    check(sim_voyage_dispatch(0, q), "merchant opening circuit retains useful booked full loads");
    arrive();
  }
  check(g_campaign.coins == 1110 && minutes == 22 * 600 && g_campaign.deliveredUnits == 28,
        "seven merchant voyages earn the operational second Sloop at twenty-two minutes with 210 reserve");
}

static void booking_transactions() {
  signed_campaign(61);
  Campaign beforeCampaign = g_campaign;
  PortState beforePorts[MAX_PORTS]; memcpy(beforePorts, g_ports, sizeof(beforePorts));
  Ship beforeShip = g_ships[0]; UiState beforeUi = g_ui;
  for (int n = 0; n < 50; ++n) {
    VoyageQuote q = sim_voyage_offer(0, 1, n % 2 ? VOYAGE_FREIGHT : VOYAGE_TRADE, NO_PORT, (uint8_t)(1 + n % 4));
    (void)q; uint8_t destinations[MAX_PORTS]; sim_voyage_destinations(0, destinations, MAX_PORTS);
  }
  check(!memcmp(&beforeCampaign, &g_campaign, sizeof(beforeCampaign)) &&
        !memcmp(beforePorts, g_ports, sizeof(beforePorts)) && !memcmp(&beforeShip, &g_ships[0], sizeof(beforeShip)) &&
        !memcmp(&beforeUi, &g_ui, sizeof(beforeUi)), "repeated offers/navigation consume no RNG, stock, money or UI state");
  VoyageQuote q = sim_voyage_offer(0, 1, VOYAGE_TRADE, GOOD_TEXTILES, 4);
  writesSucceed = false;
  check(sim_voyage_dispatch(0, q).code == SIM_SAVE_FAILED, "failed persistence rejects dispatch");
  writesSucceed = true;
  check(!memcmp(&beforeCampaign, &g_campaign, sizeof(beforeCampaign)) &&
        !memcmp(beforePorts, g_ports, sizeof(beforePorts)) && !memcmp(&beforeShip, &g_ships[0], sizeof(beforeShip)),
        "failed dispatch rolls back stock, cash, signed demand and ship state");
  --g_ports[0].stock[GOOD_TEXTILES];
  // A stock movement that leaves the same available full order need not
  // requote identical terms. A finite buyer reservation always does.
  g_ports[1].bookingUsed[GOOD_TEXTILES] = 10;
  check(sim_voyage_dispatch(0, q).code == SIM_STALE_PREVIEW && g_campaign.coins == 500,
        "demand conflict asks for a fresh review before spending");

  signed_campaign(62); g_campaign.coins = 10000; g_campaign.deliveredUnits = 24; g_campaign.rank = 2;
  for (int i = 0; i < 3; ++i) check(sim_buy_ship(0), "staffed fleet fixture purchases a Sloop");
  q = sim_voyage_offer(0, 1, VOYAGE_FREIGHT, GOOD_TEXTILES, 4);
  VoyageQuote other = sim_voyage_offer(1, 1, VOYAGE_FREIGHT, GOOD_TEXTILES, 4);
  check(sim_voyage_dispatch(0, q), "first ship claims a buyer's demand");
  check(sim_voyage_dispatch(1, other).code == SIM_STALE_PREVIEW, "second ship's stale copy cannot double reserve");
  for (uint8_t i = 1; i < 3; ++i) {
    other = sim_voyage_offer(i, 1, VOYAGE_FREIGHT, GOOD_TEXTILES, 4);
    check(sim_voyage_dispatch(i, other), "fresh distinct buyer units can be booked by another ship");
  }
  other = sim_voyage_offer(3, 1, VOYAGE_FREIGHT, GOOD_TEXTILES, 4);
  check(other.code == SIM_CONTRACT_UNAVAILABLE && !other.availableQuantity,
        "fleet cannot reserve more than finite destination demand");
  for (int i = 0; i < 10; ++i) sim_voyage_offer(3, 1, VOYAGE_FREIGHT, GOOD_TEXTILES, 4);
  check(g_ports[1].bookingUsed[GOOD_TEXTILES] == 12, "opening a board never refreshes demand");
  sim_advance_minutes(3600);
  other = sim_voyage_offer(3, 1, VOYAGE_FREIGHT, GOOD_TEXTILES, 4);
  check(other.code == SIM_OK, "elapsed six minute demand refresh serves the waiting ship");
  check(g_campaign.deliveredUnits == 36, "simultaneous booked arrivals each credit only their four units");
  int32_t docked = g_campaign.coins;
  sim_advance_minutes(1440 * 30);
  check(g_campaign.coins == docked, "a docked company incurs no absence charges");
}

static void provenance_and_reserved_debt() {
  signed_campaign(71); g_campaign.coins = 10000;
  check(sim_market_buy(0, GOOD_TEXTILES), "spot trade can buy a provenance lot");
  check(sim_market_sell(0, GOOD_TEXTILES) && !g_campaign.deliveredUnits, "local resale never earns delivery progress");
  for (int i = 0; i < 3; ++i) check(sim_market_buy(0, GOOD_TEXTILES), "buy actual spot units for sailing");
  check(sim_transfer_to_warehouse(0, 0) && sim_transfer_from_warehouse(0, 0), "warehouse transfer preserves provenance");
  check(!g_campaign.deliveredUnits && !g_ships[0].cargo[0].sailed, "loading and warehouse movement never counts as a voyage");
  g_ui.selectedShip = 0; sim_route_clear(); check(sim_route_add(1) && sim_dispatch(0), "spot cargo sails by legacy speculative route");
  g_ships[0].incidentArmed = 0;
  while (g_ships[0].state == SHIP_SAILING) sim_advance_minutes(10);
  check(g_ships[0].cargo[0].sailed && g_ships[0].cargo[0].sourcePort == 0,
        "physical port arrival marks sailed source provenance");
  // Force a profitable exact sale fixture; use actual existing cost basis,
  // not an arbitrary XP flag, and preserve it through a partial warehouse lot.
  g_ships[0].cargo[0].paidUnitPrice = 1; g_ships[0].cargo[0].contractId = 2;
  check(sim_market_sell(0, GOOD_TEXTILES) && g_campaign.deliveredUnits == 1,
        "one positive inter-port spot sale earns exactly one delivered unit");
  check(sim_transfer_to_warehouse(0, 0) && sim_transfer_from_warehouse(0, 0), "partially sold lot retains origin and remaining exact basis");
  check(sim_market_sell(0, GOOD_TEXTILES) && sim_market_sell(0, GOOD_TEXTILES) && g_campaign.deliveredUnits == 3,
        "only remaining spot units can earn subsequent progress");

  signed_campaign(72); g_campaign.coins = 10000; g_campaign.deliveredUnits = 24; g_campaign.rank = 2;
  check(sim_buy_ship(0), "second staffed ship for debt reservation fixture");
  g_campaign.recoveryDebt = 25;
  VoyageQuote q = sim_voyage_offer(0, 1, VOYAGE_FREIGHT, GOOD_TEXTILES, 4);
  check(q.withheld == 25 && sim_voyage_dispatch(0, q), "signed freight reserves its exact legacy debt withholding");
  Ship& spot = g_ships[1]; CargoItem item{}; item.kind = CARGO_MARKET;
  item.good = GOOD_TEXTILES; item.quantity = 1; item.paidUnitPrice = 1; item.sourcePort = 1; item.sailed = 1;
  item.destination = item.projectRegion = NO_PORT; spot.cargo[0] = item; spot.cargoCount = 1;
  MarketQuote sale = sim_market_quote(1, GOOD_TEXTILES, false, 1);
  check(!sale.withholding && sim_market_sell(1, GOOD_TEXTILES) && g_campaign.recoveryDebt == 25,
        "spot income cannot consume debt already reserved by a signed buyer");
  Contract legacy{}; legacy.id = 987; legacy.active = 2; legacy.origin = 0; legacy.destination = 1;
  legacy.good = GOOD_TEXTILES; legacy.quantity = 1; legacy.reward = 100; legacy.kind = JOB_FREIGHT;
  legacy.projectRegion = NO_PORT; legacy.deadlineMinute = 100000;
  g_contracts[0][0] = legacy; item = {}; item.kind = CARGO_CONTRACT; item.contractId = legacy.id;
  item.quantity = 1; item.good = GOOD_TEXTILES; item.destination = 1; item.projectRegion = NO_PORT;
  spot.cargo[0] = item; spot.cargoCount = 1;
  g_ui.selectedShip = 1; sim_route_clear(); check(sim_route_add(1) && sim_dispatch(1), "legacy order dispatch alongside signed debt reservation");
  spot.incidentArmed = 0; spot.legTotalMinutes = 10;
  int32_t before = g_campaign.coins; sim_advance_minutes(10);
  check(g_campaign.coins - before == 100 && g_campaign.recoveryDebt == 25,
        "legacy arrival also leaves reserved debt for the promised signed payment");
  arrive(0);
  check(g_campaign.recoveryDebt == 0 && g_ships[0].booking.arrivalCoins == 75,
        "signed arrival repays the reserved debt once and honors its reviewed cash");
}

static void growth_and_relief() {
  signed_campaign(81); g_campaign.coins = 1099; g_campaign.deliveredUnits = 24; g_campaign.rank = 2;
  check(sim_buy_ship(0).code == SIM_NO_FUNDS, "900 coin ship requires the visible 200 operating reserve");
  g_campaign.coins = 1100;
  writesSucceed = false;
  check(sim_buy_ship(0).code == SIM_SAVE_FAILED && g_campaign.coins == 1100 &&
        g_campaign.shipCount == 1 && g_campaign.captainCount == 1,
        "failed staffed ship save restores coins, captain slot and fleet");
  writesSucceed = true;
  check(sim_buy_ship(0) && g_campaign.coins == 200 && sim_ship_captain(1), "ship package includes its working captain");
  check(!sim_recovery_available(), "a funded working fleet cannot extract relief");
  signed_campaign(82); g_campaign.coins = 0; g_campaign.recoveryDebt = 73;
  check(sim_recovery_available(), "cashless company has a protected recovery delivery");
  writesSucceed = false;
  check(sim_claim_recovery().code == SIM_SAVE_FAILED && g_campaign.coins == 0 && g_ships[0].state == SHIP_DOCKED,
        "failed relief save cannot grant repairs or start an unsaved voyage");
  writesSucceed = true;
  check(sim_claim_recovery() && g_campaign.coins == 0 && g_ships[0].state == SHIP_SAILING,
        "relief prepays a protected job without a spendable advance");
  check(g_campaign.recoveryDebt == 73 && !sim_recovery_available() && !sim_claim_recovery(),
        "relief preserves old debt and only one recovery job can run");
  check(g_ships[0].booking.arrivalCoins * 600 <= g_ships[0].legTotalMinutes * 16,
        "recovery earning rate is capped below normal coastal freight");
  arrive();
  check(g_campaign.coins >= 80 && g_campaign.recoveryDebt == 73, "protected arrival leaves a real operating budget without increasing debt");
  uint8_t destinations[MAX_PORTS]; uint8_t n = sim_voyage_destinations(0, destinations, MAX_PORTS); bool ordinary = false;
  for (uint8_t i = 0; i < n; ++i)
    if (sim_voyage_offer(0, destinations[i], VOYAGE_FREIGHT, NO_PORT, 0).code == SIM_OK) ordinary = true;
  check(ordinary && !sim_recovery_available(), "one recovery job permits an ordinary profitable freight departure");
  signed_campaign(820); g_campaign.coins = 0; g_ships[0].captainId = NO_SHIP;
  for (int i = 0; i < MAX_CAPTAINS; ++i) {
    g_captains[i] = g_captains[0]; g_captains[i].active = 1;
    g_captains[i].assignedShip = NO_SHIP; g_captains[i].currentPort = 1;
  }
  g_campaign.captainCount = MAX_CAPTAINS;
  check(sim_recovery_available() && sim_claim_recovery() && sim_ship_captain(0),
        "a full roster of idle captains abroad cannot strand protected recovery without crew");

  signed_campaign(83); g_campaign.deliveredUnits = 48; g_campaign.rank = 3; g_campaign.coins = 10000; g_ports[3].unlocked = 1;
  VoyageQuote q = sim_voyage_offer(0, 3, VOYAGE_TRADE, GOOD_IRONWARE, 4);
  check(q.code == SIM_OK && q.projectRegion == 1 && q.minutes == 4800,
        "New Albion dock buys actual Ironware by its eight-minute coastal Sloop route");
  check(sim_voyage_dispatch(0, q), "project cargo purchases and sails under the same signed rules"); arrive();
  check(g_campaign.projectCargo[1][1] == 4 && g_campaign.projectCargo[1][0] == 0,
        "only the actual delivered project material advances works");
  check(sim_project_contribute(1).code == SIM_PROJECT_INCOMPLETE, "coins alone cannot skip project deliveries");
  g_campaign.projectCargo[1][1] = 11;
  g_ships[0].currentPort = 0; g_ships[0].fromPort = 0; g_ships[0].toPort = 0;
  q = sim_voyage_offer(0, 3, VOYAGE_TRADE, GOOD_IRONWARE, 0);
  check(q.code == SIM_OK && q.quantity == 1 && q.payNow == 141 && q.arrivalCoins == 90 && q.profit == -51 && !q.bonus,
        "one remaining project crate shows its actual cost and deliberate loss without a hidden subsidy");
  check(sim_voyage_dispatch(0, q), "the final partial project order remains executable"); arrive();
  check(g_campaign.projectCargo[1][1] == 12, "partial project completion credits its one remaining crate only");
  for (uint8_t region = 1; region < REGION_COUNT; ++region) {
    g_campaign.projectCargo[region][0] = g_campaign.projectCargo[region][1] = sim_project_target(region);
    g_campaign.coins = sim_project_cost(region) + 200;
    uint8_t regionsBefore = g_campaign.regionMask;
    int32_t coinsBefore = g_campaign.coins;
    writesSucceed = false;
    check(sim_project_contribute(region).code == SIM_SAVE_FAILED && g_campaign.regionMask == regionsBefore &&
          g_campaign.coins == coinsBefore && !g_ports[region * 4].unlocked,
          "failed project persistence restores investment and locked coastline");
    writesSucceed = true;
    check(sim_project_contribute(region) && (g_campaign.regionMask & (1u << region)) &&
          (g_campaign.chartMask & (1u << region)), "each single project opens its whole region and its chart together");
    check(sim_project_contribute(region).code == SIM_ALREADY_OWNED, "a completed project cannot charge a second contribution");
  }
  check(g_campaign.regionMask == 0x3f && g_ports[23].unlocked, "regional works progression reaches all twenty-four ports");
}

static void recovery_reviews_and_fittings() {
  signed_campaign(91); g_campaign.coins = 0;
  g_ships[0].state = SHIP_DISABLED; g_ships[0].condition = 12;
  Campaign campaignBefore = g_campaign; Ship shipBefore = g_ships[0];
  CaptainProfile captainsBefore[MAX_CAPTAINS]; memcpy(captainsBefore, g_captains, sizeof(captainsBefore));
  UiState uiBefore = g_ui;
  VoyageQuote relief = sim_recovery_quote();
  check(relief.code == SIM_OK && relief.mode == VOYAGE_RELIEF && relief.shipIndex == 0 &&
        relief.payNow == 0 && relief.arrivalCoins == 80 && relief.minutes == 3000,
        "Harbor Work review shows its exact free departure, five minutes and eighty coin payment");
  check(!memcmp(&campaignBefore, &g_campaign, sizeof(campaignBefore)) &&
        !memcmp(&shipBefore, &g_ships[0], sizeof(shipBefore)) &&
        !memcmp(captainsBefore, g_captains, sizeof(captainsBefore)) && !memcmp(&uiBefore, &g_ui, sizeof(uiBefore)),
        "reviewing minimum repair and captain makes no live changes");
  ++g_captains[0].level;
  check(sim_claim_recovery(relief).code == SIM_STALE_PREVIEW && g_ships[0].condition == 12 && !g_campaign.coins,
        "changed protected-job preparation requires a fresh review before restoration");
  relief = sim_recovery_quote();
  check(sim_claim_recovery(relief) && g_ships[0].condition == 50 &&
        g_ships[0].booking.arrivalCoins == relief.arrivalCoins && g_ships[0].legTotalMinutes == relief.legMinutes[0],
        "confirmed protected work applies precisely its reviewed restoration and travel terms");

  signed_campaign(92); g_campaign.coins = 0; g_campaign.shipCount = 0;
  memset(g_captains, 0, sizeof(g_captains)); g_campaign.captainCount = 0;
  relief = sim_recovery_quote();
  check(relief.code == SIM_OK && !g_campaign.shipCount && !g_campaign.captainCount,
        "a lost company's proposed relief ship and captain are only shadows during review");
  check(sim_claim_recovery(relief) && g_campaign.shipCount == 1 && sim_ship_captain(0),
        "confirmed zero-fleet relief creates one working ship and captain");

  signed_campaign(93); g_campaign.coins = 100000; g_campaign.deliveredUnits = 48; g_campaign.rank = 3;
  check(sim_ship_package_cost(0) == 900 && sim_ship_package_cost(1) == 1600 && !sim_ship_package_cost(255),
        "package price API reports the actual staffed Sloop and Schooner prices safely");
  for (int i = 1; i < MAX_SHIPS; ++i) check(sim_buy_ship(0), "fill all twelve fleet slots with operational Sloops");
  for (int i = 0; i < MAX_SHIPS; ++i) {
    g_ships[i].modules[0] = MODULE_COPPER + 1; g_ships[i].modules[1] = MODULE_STORM_RIG + 1;
  }
  for (int p = 0; p < MAX_PORTS; ++p) g_ports[p].unlocked = p < 16;
  g_campaign.regionMask = g_campaign.chartMask = 0x0f;
  Ship& ship = g_ships[0]; ship.currentPort = ship.fromPort = ship.toPort = 12;
  sim_ship_captain(0)->currentPort = 12;
  VoyageQuote before = sim_voyage_offer(0, 0, VOYAGE_FREIGHT, GOOD_TIMBER, 0);
  check(before.code == SIM_BAD_ROUTE && g_campaign.shipCount == MAX_SHIPS,
        "full Sloop fleet fixture really cannot cross the western Monsoon gap");
  int32_t coinsBefore = g_campaign.coins, capitalBefore = g_campaign.capitalSpending;
  writesSucceed = false;
  check(sim_replace_module(0, 0, MODULE_LONG_CASKS).code == SIM_SAVE_FAILED &&
        g_campaign.coins == coinsBefore && ship.modules[0] == MODULE_COPPER + 1,
        "failed fitting save preserves the old fitting and treasury");
  writesSucceed = true;
  check(sim_replace_module(0, 0, MODULE_LONG_CASKS) && g_campaign.coins == coinsBefore - 420 &&
        g_campaign.capitalSpending == capitalBefore + 420 && ship.modules[0] == MODULE_LONG_CASKS + 1 &&
        ship.modules[1] == MODULE_STORM_RIG + 1,
        "replacing one named fitting charges 420 without a refund or touching its other slot");
  VoyageQuote after = sim_voyage_offer(0, 0, VOYAGE_FREIGHT, GOOD_TIMBER, 0);
  check(after.code == SIM_OK && after.capacity == 3 && after.routeCount <= MAX_ROUTE_LEGS,
        "the same twelve-ship fleet can trade across the gap after a long-cask replacement");
  check(sim_replace_module(0, 1, MODULE_LONG_CASKS).code == SIM_ALREADY_OWNED,
        "a replacement cannot duplicate the same fitting in both slots");
  check(sim_voyage_dispatch(0, after) && sim_replace_module(0, 1, MODULE_COPPER).code == SIM_BAD_SHIP_STATE,
        "a reviewed voyage cannot be refitted while sailing");

  signed_campaign(94); g_campaign.coins = 10000;
  CargoItem item{}; item.kind = CARGO_MARKET; item.good = GOOD_GRAIN; item.quantity = 4;
  item.sourcePort = 0; item.destination = item.projectRegion = NO_PORT;
  g_ships[0].cargo[0] = item; g_ships[0].cargoCount = 1;
  check(sim_replace_module(0, 0, MODULE_LONG_CASKS).code == SIM_NO_CAPACITY && g_campaign.coins == 10000,
        "a capacity-reducing fitting cannot silently discard cargo");
  g_ships[0].modules[0] = MODULE_BERTHS + 1;
  g_ships[0].cargo[0].kind = CARGO_CONTRACT; g_ships[0].cargo[0].jobKind = JOB_PASSENGER;
  g_ships[0].cargo[0].quantity = 2;
  check(sim_replace_module(0, 0, MODULE_COPPER).code == SIM_NO_BERTHS,
        "replacing extra berths cannot displace passengers");
  g_ships[0].cargoCount = 0; g_ships[0].modules[0] = MODULE_LONG_CASKS + 1;
  g_ships[0].currentPort = g_ships[0].fromPort = g_ships[0].toPort = 21;
  for (int p = 0; p < MAX_PORTS; ++p) g_ports[p].unlocked = 1;
  check(sim_replace_module(0, 0, MODULE_COPPER).code == SIM_ENDURANCE_LIMIT,
        "removing long casks cannot strand a Sloop at Westwind without a legal exit");
  g_ships[0].currentPort = g_ships[0].fromPort = g_ships[0].toPort = 23;
  check(sim_replace_module(0, 0, MODULE_COPPER).code == SIM_ENDURANCE_LIMIT,
        "a nominal Far Horizon exit is insufficient when no reachable port buys its goods");
  g_ui.selectedPort = 23; g_campaign.deliveredUnits = 48; g_campaign.rank = 3;
  check(sim_ship_purchase_block(0) == SIM_ENDURANCE_LIMIT && g_campaign.coins == 10000,
        "a ready-to-sail package cannot be sold at a port where that hull cannot earn from any voyage");
  g_ports[23].shipyardLevel = 1;
  check(sim_ship_purchase_block(1) == SIM_OK, "Far Horizon still sells its usable longer-range Schooner package");
}

static void bounded_routes_and_legacy_credit() {
  signed_campaign(95); g_campaign.coins = 10000;
  for (int p = 0; p < MAX_PORTS; ++p) g_ports[p].unlocked = 1;
  g_campaign.regionMask = g_campaign.chartMask = 0x3f;
  Ship& ship = g_ships[0]; ship.classId = 1; ship.currentPort = 6;
  VoyageQuote q = sim_voyage_offer(0, 22, VOYAGE_FREIGHT, GOOD_MEDICINES, 4);
  check(q.code == SIM_OK && q.routeCount <= 8,
        "Schooner can choose the legal eight-leg San Cordel to Valparra route");
  check(sim_voyage_dispatch(0, q), "long quoted voyage dispatches for an exact elapsed-time catch-up");
  sim_advance_minutes(q.minutes);
  check(g_ships[0].booking.state == BOOKING_SETTLED && g_ships[0].currentPort == 22,
        "one catch-up of the quoted duration carries partial steps between legs and settles on time");
  ship.classId = 2; ship.currentPort = 2;
  q = sim_voyage_offer(0, 16, VOYAGE_FREIGHT, GOOD_TIMBER, 4);
  check(q.code == SIM_OK && q.routeCount <= 8,
        "Brig can choose the legal eight-leg Northwatch to Jadeport route");

  signed_campaign(96); g_campaign.coins = 10000; g_campaign.deliveredUnits = 48;
  g_campaign.charterProgress[1] = 3;
  g_campaign.projectCargo[1][0] = 10; g_campaign.projectCargo[1][1] = 6;
  Contract legacy{}; legacy.id = 990; legacy.active = 2; legacy.origin = 0; legacy.destination = 1;
  legacy.good = GOOD_IRONWARE; legacy.quantity = 1; legacy.reward = 100; legacy.kind = JOB_CHARTER;
  legacy.projectRegion = 1; legacy.deadlineMinute = 100000; g_contracts[0][0] = legacy;
  CargoItem item{}; item.kind = CARGO_CONTRACT; item.contractId = legacy.id; item.quantity = 1;
  item.good = legacy.good; item.destination = 1; item.projectRegion = 1; item.jobKind = JOB_CHARTER;
  g_ships[0].cargo[0] = item; g_ships[0].cargoCount = 1;
  g_ui.selectedShip = 0; sim_route_clear(); check(sim_route_add(1) && sim_dispatch(0), "legacy charter cargo retains its actual voyage");
  g_ships[0].incidentArmed = 0; g_ships[0].legTotalMinutes = 10; sim_advance_minutes(10);
  check(g_campaign.charterProgress[1] == 4 && g_campaign.projectCargo[1][0] == 12 && g_campaign.projectCargo[1][1] == 8,
        "late legacy charter delivery adds its converted increment after new project materials");
  sim_after_load(); sim_advance_minutes(60);
  check(g_campaign.projectCargo[1][0] == 12 && g_campaign.projectCargo[1][1] == 8,
        "reloading cannot reapply a consumed legacy project increment");
}

static void matched_full_hold_returns() {
  signed_campaign(97); g_campaign.coins = 1000000;
  g_campaign.regionMask = g_campaign.chartMask = 0x3f;
  for (int p = 0; p < MAX_PORTS; ++p) {
    g_ports[p].unlocked = 1;
    for (int good = 0; good < GOOD_COUNT; ++good) g_ports[p].stock[good] = 200;
  }
  for (uint8_t cls = 0; cls < SHIP_CLASS_COUNT; ++cls) {
    int pairs = 0; int minimum = 100000, maximum = 0;
    Ship& ship = g_ships[0]; ship.classId = cls;
    for (uint8_t origin = 0; origin < MAX_PORTS; ++origin) {
      if (SHIP_CLASS_DEFS[cls].draft > PORT_DEFS[origin].portClass) continue;
      ship.currentPort = ship.fromPort = ship.toPort = origin;
      g_captains[0].currentPort = origin;
      for (uint8_t destination = 0; destination < MAX_PORTS; ++destination) {
        for (uint8_t good = 0; good < GOOD_COUNT; ++good) {
          uint8_t capacity = (uint8_t)sim_ship_capacity(ship);
          VoyageQuote freight = sim_voyage_offer(0, destination, VOYAGE_FREIGHT, good, capacity);
          VoyageQuote trade = sim_voyage_offer(0, destination, VOYAGE_TRADE, good, capacity);
          if (freight.code != SIM_OK || trade.code != SIM_OK) continue;
          ++pairs;
          int ratio = (int)(trade.profit * 10000 / freight.profit);
          if (ratio < minimum) minimum = ratio;
          if (ratio > maximum) maximum = ratio;
          check(trade.minutes == freight.minutes && trade.quantity == freight.quantity &&
                trade.profit * 100 >= freight.profit * 115 && trade.profit * 100 <= freight.profit * 135,
                "matched full-hold trade pays fifteen to thirty-five percent above supplied freight");
        }
      }
    }
    check(pairs > 0, "every ship class has matched profitable full-hold routes");
    std::printf("%s: %d matched routes; trade premium %.2f%% to %.2f%%\n", SHIP_CLASS_DEFS[cls].name,
                pairs, (minimum - 10000) / 100.0, (maximum - 10000) / 100.0);
  }
}

int main() {
  starter_fixtures(); booking_transactions(); provenance_and_reserved_debt(); growth_and_relief();
  recovery_reviews_and_fittings(); bounded_routes_and_legacy_credit();
  matched_full_hold_returns();
  std::printf("Booked voyage regressions: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
