// Real merchant transactions, real LittleFS generations, and legacy wire files.
#include "../../save.cpp"
#include "../../encounter_content.h"
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

void audio_begin() {} void audio_tick() {} void audio_notice() {} void audio_alert() {}
static int checks, failures;
static void check(bool ok, const char* message) {
  ++checks; if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
static void fresh(uint32_t seed = 51) {
  emitVersion = CH_VERSION; save_reset(); sim_new_campaign(seed); g_campaign.onboardingStep = ONBOARD_DONE;
  // Transaction fixtures use an empty hold; starter cargo is tested separately.
  g_ships[0].cargoCount = 0;
  memset(g_ships[0].cargo, 0, sizeof(g_ships[0].cargo));
  check(g_campaign.economyVersion == 2, "fresh company uses owned-goods economy");
  check(save_write(), "starting company commits");
}
static uint64_t state_hash() {
  uint64_t h = 1469598103934665603ull;
  auto bytes = [&](const void* data, size_t size) {
    const uint8_t* p = (const uint8_t*)data; while (size--) h = (h ^ *p++) * 1099511628211ull;
  };
  bytes(&g_campaign, sizeof(g_campaign)); bytes(g_ships, sizeof(g_ships));
  bytes(g_ports, sizeof(g_ports)); bytes(g_captains, sizeof(g_captains));
  bytes(g_contracts, sizeof(g_contracts)); bytes(&g_ui, sizeof(g_ui));
  bytes(g_journal, sizeof(g_journal)); bytes(g_attention, sizeof(g_attention));
  bytes(&g_incident, sizeof(g_incident)); return h;
}
static unsigned goods(uint8_t ship = 0) {
  unsigned count = 0; for (uint8_t i = 0; i < g_ships[ship].cargoCount; ++i)
    if (g_ships[ship].cargo[i].kind == CARGO_MARKET) count += g_ships[ship].cargo[i].quantity;
  return count;
}
static int32_t basis(uint8_t ship = 0) {
  int32_t result = 0; for (uint8_t i = 0; i < g_ships[ship].cargoCount; ++i) {
    const CargoItem& c = g_ships[ship].cargo[i];
    if (c.kind == CARGO_MARKET) result += c.quantity * c.paidUnitPrice + c.contractId;
  } return result;
}
static MerchantTradeQuote trade(bool buying, uint8_t good, uint8_t count, uint8_t ship = 0) {
  uint8_t quantities[GOOD_COUNT]{}; quantities[good] = count;
  return sim_merchant_trade_offer(ship, buying, quantities);
}
static void starter_cargo() {
  save_reset(); sim_new_campaign(51); g_campaign.onboardingStep = ONBOARD_DONE;
  check(goods() == 2 && g_ships[0].cargo[0].good == GOOD_GRAIN && !basis() &&
        g_campaign.coins == 500, "new company receives two free Grain and unchanged starting cash");
  check(PORT_DEFS[0].demanded[0] == GOOD_GRAIN || PORT_DEFS[0].demanded[1] == GOOD_GRAIN,
        "starting port wants the starter cargo");
  check(save_write() && save_read() && goods() == 2, "loading preserves rather than duplicates starter cargo");
  auto one = trade(false, GOOD_GRAIN, 1); int32_t cash = g_campaign.coins;
  check(one.code == SIM_OK && sim_merchant_trade_apply(0, one) && goods() == 1 &&
        g_campaign.coins == cash + one.cashDelta && !g_campaign.deliveredUnits,
        "starter cargo sells one unit without inventing a completed import");
  check(save_read() && goods() == 1, "partial starter sale persists without replenishment");
  auto rest = trade(false, GOOD_GRAIN, 1);
  check(sim_merchant_trade_apply(0, rest) && save_read() && !goods(),
        "sold starter cargo stays sold on reload");
  g_campaign.coins = 5000; g_campaign.deliveredUnits = 24;
  check(sim_buy_ship(0) && !g_ships[1].cargoCount, "later ships do not receive starter goods");
}
static void block_save(bool blocked) {
  std::string path = std::string(getenv("SH_FS_DIR")) + "/sh_company.tmp";
  check((blocked ? mkdir(path.c_str(), 0755) : rmdir(path.c_str())) == 0, "toggle deliberate save failure");
}
template<class Action> static void failed_commit(Action action, const char* label) {
  uint64_t before = state_hash(); block_save(true);
  check(action().code == SIM_SAVE_FAILED && state_hash() == before, label);
  block_save(false);
}

static void ownership_and_transactions() {
  fresh(); auto buy = trade(true, GOOD_TEXTILES, 3); uint64_t before = state_hash();
  check(sim_feature_unlocked(FEATURE_MARKET), "fresh merchant company can use Market before its first delivery");
  check(sim_voyage_offer(0, 1, VOYAGE_TRADE, GOOD_TEXTILES, 4).code == SIM_LOCKED &&
        sim_voyage_offer(0, 1, VOYAGE_FREIGHT, GOOD_TEXTILES, 4).code == SIM_LOCKED,
        "new merchant economy cannot book the retired automatic-sale or freight loop");
  check(buy.code == SIM_OK && buy.quantity == 3 && buy.cashDelta == -buy.total, "purchase quote is exact");
  auto alteredBuy = buy; ++alteredBuy.total;
  check(sim_merchant_trade_apply(0, alteredBuy).code != SIM_OK && state_hash() == before,
        "changed reviewed purchase totals are rejected even with the original fingerprint");
  auto routeBefore = sim_merchant_sail_offer(0, 1);
  check(state_hash() == before && routeBefore.code == SIM_OK, "quote browsing never purchases or reserves");
  failed_commit([&] { return sim_merchant_trade_apply(0, buy); }, "failed buy restores cash, stock, cargo and reports");
  int32_t initial = g_campaign.coins;
  check(sim_merchant_trade_apply(0, buy) && goods() == 3 && basis() == buy.total &&
        g_campaign.coins == initial - buy.total, "explicit buy creates owned goods with exact basis");
  before = state_hash();
  check(sim_merchant_trade_apply(0, buy).code != SIM_OK && state_hash() == before, "duplicate reviewed purchase cannot replay");
  check(save_read() && goods() == 3 && basis() == buy.total, "purchase and basis round-trip schema11");
  auto sail = sim_merchant_sail_offer(0, 1); before = state_hash();
  auto alteredSail = sail; ++alteredSail.saleEstimate;
  check(sim_merchant_sail(0, alteredSail).code != SIM_OK && state_hash() == before,
        "changed reviewed destination estimate requires a fresh review");
  auto estimate = sim_merchant_sale_estimate(0, 1);
  check(sail.code == SIM_OK && estimate.estimateOnly && state_hash() == before, "destination quote is a pure sale estimate");
  failed_commit([&] { return sim_merchant_sail(0, sail); }, "failed sailing restores route, cargo, cash and counters");
  int32_t paid = g_campaign.coins; uint8_t condition = g_ships[0].condition;
  check(sim_merchant_sail(0, sail) && g_campaign.coins == paid, "merchant sailing is free");
  check(save_read() && g_ships[0].merchant.state == MERCHANT_SAILING && goods() == 3,
        "in-flight owned cargo and merchant timing round-trip");
  g_ui.returnQuiet = 1; sim_advance_minutes(sail.minutes);
  check(g_ships[0].merchant.state == MERCHANT_ARRIVED && goods() == 3 && basis() == buy.total &&
        g_campaign.coins == paid && g_ships[0].condition == condition && !g_incident.active,
        "arrival retains goods and basis, credits no sale and causes no wear or incident");
  auto report = sim_return_report();
  check(!report.bookedCoins && !report.legacyCoins, "merchant arrival report never invents banked sales proceeds");
  check(save_read() && goods() == 3 && g_campaign.coins == paid, "arrival cargo survives reload");
  sim_advance_minutes(3600); check(goods() == 3 && g_campaign.coins == paid, "waiting never auto-sells goods");
  uint32_t receipt = g_ships[0].merchant.id; before = state_hash();
  check(sim_ack_arrival_receipt(0, receipt + 1).code == SIM_STALE_PREVIEW && state_hash() == before,
        "wrong merchant receipt identity cannot acknowledge another journey");
  failed_commit([&] { return sim_ack_arrival_receipt(0, receipt); },
        "failed merchant receipt acknowledgment restores unread flag and ledger watermark");
  check(sim_ack_arrival_receipt(0, receipt) && !g_ships[0].merchant.receiptUnread &&
        goods() == 3 && g_campaign.coins == paid && save_read() && !g_ships[0].merchant.receiptUnread,
        "exact merchant receipt acknowledges durably without selling its owned goods");
  auto sell = trade(false, GOOD_TEXTILES, 1);
  failed_commit([&] { return sim_merchant_trade_apply(0, sell); }, "failed sale restores basis, cash, stock and progress");
  int32_t oldBasis = basis(), profit = g_campaign.marketProfit;
  check(sim_merchant_trade_apply(0, sell) && goods() == 2 && g_campaign.coins == paid + sell.cashDelta &&
        oldBasis - basis() == sell.costBasis && g_campaign.marketProfit - profit == sell.profit,
        "explicit partial sale realizes only the reviewed cash and FIFO basis");
  before = state_hash(); check(sim_merchant_trade_apply(0, sell).code != SIM_OK && state_hash() == before,
        "duplicate sale cannot credit twice");
  auto rest = trade(false, GOOD_TEXTILES, 2); check(sim_merchant_trade_apply(0, rest), "remaining owned units sell explicitly");
  check(!goods() && !basis() && sell.costBasis + rest.costBasis == buy.total,
        "all partial sales reconcile the exact purchase basis");
  int32_t banked = g_campaign.coins; check(save_read() && !goods() && g_campaign.coins == banked,
        "sale removal and cash persist in one generation");
}

static void packages_and_capacity() {
  fresh(); auto offer = sim_package_offer(0, 0);
  check(offer.code == SIM_OK && offer.reward > 0, "finite paid package is available");
  auto changedReward = offer; ++changedReward.reward; uint64_t beforeReward = state_hash();
  check(sim_package_accept(0, changedReward).code != SIM_OK && state_hash() == beforeReward,
        "changed reviewed package reward cannot be accepted");
  uint64_t browseState = state_hash(); uint8_t offered = sim_package_count(0);
  for (int browse = 0; browse < 20; ++browse) {
    auto same = sim_package_offer(0, 0);
    check(same.id == offer.id && same.reward == offer.reward && sim_package_count(0) == offered,
          "browsing never replenishes or reprices finite package offers");
  }
  check(state_hash() == browseState, "package browsing is read only");
  failed_commit([&] { return sim_package_accept(0, offer); }, "failed acceptance restores package offer and ship");
  auto buy = trade(true, GOOD_TEXTILES, 3); check(sim_merchant_trade_apply(0, buy), "three merchant goods leave one package slot");
  offer = sim_package_offer(0, 0); int32_t cash = g_campaign.coins;
  check(sim_package_accept(0, offer) && goods() == 3 && sim_ship_used_capacity(g_ships[0]) == 4 &&
        g_campaign.coins == cash, "free package uses one slot beside owned goods");
  uint64_t before = state_hash(); check(sim_package_accept(0, offer).code != SIM_OK && state_hash() == before,
        "same package cannot be accepted twice");
  uint8_t packageSlot = NO_PORT;
  for (uint8_t i = 0; i < g_ships[0].cargoCount; ++i)
    if (g_ships[0].cargo[i].jobKind == JOB_PACKAGE) packageSlot = i;
  check(packageSlot != NO_PORT && g_ships[0].cargo[packageSlot].good == NO_PORT &&
        !g_ships[0].cargo[packageSlot].paidUnitPrice, "package has no market good or paid basis");
  g_ports[0].warehouseLevel = 1; before = state_hash();
  check(!sim_transfer_to_warehouse(0, packageSlot) && state_hash() == before, "package cannot be hoarded in warehouse");
  check(trade(true, GOOD_TEXTILES, 1).code != SIM_OK, "full mixed hold refuses another good");
  auto second = sim_package_offer(0, 0); before = state_hash();
  check(sim_package_accept(0, second).code != SIM_OK && state_hash() == before,
        "full hold cannot consume another finite package offer");
  check(save_write() && save_read() && goods() == 3 && valid_package_links(),
        "package promise and exact cargo link round-trip");
  uint32_t packageId = offer.id; uint16_t reward = offer.reward;
  g_campaign.coins = 0; g_campaign.recoveryDebt = 999;
  auto sail = sim_merchant_sail_offer(0, offer.destination);
  check(sail.code == SIM_OK && sim_merchant_sail(0, sail), "zero cash can sail a guaranteed package");
  g_ui.returnQuiet = 1; sim_advance_minutes(sail.minutes);
  check(g_campaign.coins == reward && g_campaign.recoveryDebt == 999 && goods() == 3 &&
        sim_ship_used_capacity(g_ships[0]) == 3 && g_ships[0].merchant.packageCoins == reward &&
        g_ships[0].merchant.packagesDelivered == 1, "package pays exact promise without tax, debt or merchant consumption");
  bool packageRemains = false;
  for (uint8_t i = 0; i < g_ships[0].cargoCount; ++i) packageRemains |= g_ships[0].cargo[i].contractId == packageId &&
    g_ships[0].cargo[i].jobKind == JOB_PACKAGE;
  check(!packageRemains && save_read() && g_campaign.coins == reward, "paid package is removed atomically");
  sim_advance_minutes(7200); check(g_campaign.coins == reward, "reloading and waiting never pays package twice");
}

static void mixed_and_malformed() {
  fresh(); uint8_t amounts[GOOD_COUNT]{}; amounts[GOOD_TEXTILES] = 2; amounts[GOOD_IRONWARE] = 2;
  auto quote = sim_merchant_trade_offer(0, true, amounts);
  check(quote.code == SIM_OK && quote.quantity == 4, "mixed merchant manifest preserves both chosen goods");
  failed_commit([&] { return sim_merchant_trade_apply(0, quote); }, "mixed buy is atomic across both goods");
  check(sim_merchant_trade_apply(0, quote) && goods() == 4 && basis() == quote.total,
        "mixed buy persists its exact total basis");
  uint8_t none[GOOD_COUNT]{}; uint64_t before = state_hash();
  check(sim_merchant_trade_offer(0, true, none).code != SIM_OK && state_hash() == before,
        "empty draft does not silently fill or charge");
  auto sail = sim_merchant_sail_offer(0, 1); check(sim_merchant_sail(0, sail), "mixed cargo sails");
  sim_advance_minutes(sail.minutes); check(goods() == 4, "both owned cargo types survive arrival");
  auto sell = sim_merchant_trade_offer(0, false, amounts); check(sim_merchant_trade_apply(0, sell) && !goods(),
        "mixed explicit sale removes exactly the reviewed types");
  fresh(); auto p = sim_package_offer(0, 0); check(sim_package_accept(0, p), "package validation fixture accepted");
  CargoItem valid = g_ships[0].cargo[0], bad = valid;
  check(valid_cargo(valid, false, 11) && !valid_cargo(valid, false, 10), "package is explicitly schema11 only");
  check(!valid_cargo(valid, true, 11), "package cannot decode in warehouse");
  bad.good = GOOD_TEXTILES; check(!valid_cargo(bad, false, 11), "package cannot pretend to be a market good");
  bad = valid; bad.quantity = 2; check(!valid_cargo(bad, false, 11), "package cannot represent two units in one offer");
  bad = valid; bad.kind = CARGO_MARKET; check(!valid_cargo(bad, false, 11), "package cannot masquerade as sellable cargo");
  bad = valid; bad.paidUnitPrice = 1; check(!valid_cargo(bad, false, 11), "package has no invented cost basis");
  bad = valid; bad.contractId++; g_ships[0].cargo[0] = bad;
  check(!valid_package_links(), "orphaned accepted promise is rejected");
  g_ships[0].cargo[0] = valid;
  for (uint8_t i = 0; i < MAX_CONTRACTS_PER_PORT; ++i) if (g_contracts[0][i].id == valid.contractId)
    g_contracts[0][i].active = 1;
  check(!valid_package_links(), "cargo cannot simultaneously remain an available package offer");
}

static void intermediate_and_donation() {
  fresh(); g_campaign.regionMask = g_campaign.chartMask = 3;
  g_campaign.deliveredUnits = 48; g_ships[0].classId = 1;
  for (uint8_t port = 0; port < 8; ++port) g_ports[port].unlocked = 1;
  g_ports[0].packageEpoch = UINT32_MAX; sim_generate_jobs(0, true);
  auto longRoute = sim_merchant_sail_offer(0, 5);
  check(longRoute.code == SIM_OK && longRoute.routeCount > 1, "long merchant route has intermediate landfalls");
  PackageOffer intermediate{}; intermediate.code = SIM_CONTRACT_UNAVAILABLE;
  for (uint8_t row = 0; row < sim_package_count(0); ++row) {
    auto candidate = sim_package_offer(0, row);
    if (candidate.destination == longRoute.route[0]) { intermediate = candidate; break; }
  }
  check(intermediate.code == SIM_OK && sim_package_accept(0, intermediate), "real package matches first stop of longer route");
  auto buy = trade(true, GOOD_TEXTILES, 2); check(sim_merchant_trade_apply(0, buy), "merchant goods share multileg trip");
  longRoute = sim_merchant_sail_offer(0, 5); check(sim_merchant_sail(0, longRoute), "multileg trip commits");
  int32_t cash = g_campaign.coins; uint32_t first = longRoute.legMinutes[0];
  g_ui.returnQuiet = 1; sim_advance_minutes(first + 123);
  check(g_ships[0].merchant.state == MERCHANT_SAILING && g_ships[0].routePos == 1 &&
        g_ships[0].legElapsedMinutes == 123 && g_campaign.coins == cash + intermediate.reward &&
        g_ships[0].merchant.packagesDelivered == 1 && goods() == 2,
        "catch-up pays intermediate package once and carries fractional time into next leg");
  check(save_write() && save_read() && g_ships[0].merchant.state == MERCHANT_SAILING &&
        g_ships[0].merchant.packageCoins == intermediate.reward && g_ships[0].legElapsedMinutes == 123,
        "accrued intermediate package credit and remaining travel round-trip");
  sim_advance_minutes(longRoute.minutes - first - 123);
  check(g_ships[0].merchant.state == MERCHANT_ARRIVED && g_ships[0].currentPort == 5 &&
        g_campaign.coins == cash + intermediate.reward && goods() == 2,
        "final landfall retains goods without paying intermediate package again");

  fresh(); g_campaign.deliveredUnits = 48; g_ports[3].unlocked = 1;
  buy = trade(true, GOOD_IRONWARE, 3); check(sim_merchant_trade_apply(0, buy), "project materials are first purchased as owned goods");
  auto route = sim_merchant_sail_offer(0, 3); check(sim_merchant_sail(0, route), "owned material travels to project port");
  sim_advance_minutes(route.minutes); cash = g_campaign.coins;
  check(!g_campaign.projectCargo[1][1] && goods() == 3, "arrival does not donate or sell materials automatically");
  failed_commit([&] { return sim_merchant_donate(0, GOOD_IRONWARE, 2); },
        "failed explicit donation restores material basis, progress, capital and cash");
  int32_t initialBasis = basis(), initialCapital = g_campaign.capitalSpending;
  check(sim_merchant_donate(0, GOOD_IRONWARE, 2) && g_campaign.coins == cash && goods() == 1 &&
        g_campaign.projectCargo[1][1] == 2 &&
        g_campaign.capitalSpending - initialCapital == initialBasis - basis(),
        "explicit imported-material donation removes exact basis with no sale proceeds");
  uint64_t before = state_hash();
  check(sim_merchant_donate(0, GOOD_IRONWARE, 2).code != SIM_OK && state_hash() == before,
        "same material cannot fund project twice");
  check(save_read() && goods() == 1 && g_campaign.projectCargo[1][1] == 2 && g_campaign.coins == cash,
        "donated units and project progress persist together");
}

static void stock_and_no_arbitrage() {
  const uint8_t ports[] = {0, 1, 2, 3}; const uint16_t levels[] = {4, 16, 64};
  for (uint8_t port : ports) for (uint8_t good = 0; good < GOOD_COUNT; ++good)
    for (uint16_t stock : levels) {
      fresh(100 + port * 8 + good); g_campaign.coins = 10000; g_campaign.legacyFeatureMask = 0xffff;
      g_ships[0].currentPort = g_ships[0].fromPort = g_ships[0].toPort = port;
      g_ports[port].unlocked = 1; g_ports[port].stock[good] = stock;
      auto buy = trade(true, good, 3); check(buy.code == SIM_OK, "boundary stock purchase is available");
      int32_t cash = g_campaign.coins; check(sim_merchant_trade_apply(0, buy), "boundary batch purchase commits");
      auto sell = trade(false, good, 3); check(sell.code == SIM_OK && sim_merchant_trade_apply(0, sell),
        "same-port liquidation commits");
      check(g_campaign.coins <= cash && !goods() && !basis() && g_ports[port].stock[good] == stock,
        "same-port instant round trip never creates money and restores physical stock");
    }
  fresh(); g_campaign.coins = 10000;
  auto old = trade(true, GOOD_TEXTILES, 2); g_ports[0].stock[GOOD_TEXTILES] = 0;
  uint64_t before = state_hash();
  check(sim_merchant_trade_apply(0, old).code == SIM_STALE_PREVIEW && state_hash() == before,
        "stock change invalidates reviewed purchase without partial execution");
  auto low = trade(true, GOOD_TEXTILES, 1); g_ports[0].stock[GOOD_TEXTILES] = 100;
  auto high = trade(true, GOOD_TEXTILES, 1);
  check(low.total != high.total, "stock supply changes actual reviewed prices");
  fresh(); auto buy = trade(true, GOOD_TEXTILES, 2); check(sim_merchant_trade_apply(0, buy), "saturated destination fixture buys goods");
  auto route = sim_merchant_sail_offer(0, 1); check(sim_merchant_sail(0, route), "saturated destination fixture sails");
  sim_advance_minutes(route.minutes); g_ports[1].stock[GOOD_TEXTILES] = 200;
  auto saturated = trade(false, GOOD_TEXTILES, 2);
  check(saturated.code == SIM_OK && sim_merchant_trade_apply(0, saturated), "saturated port still accepts an explicit sale");
  int32_t cash = g_campaign.coins;
  check(g_ports[1].stock[GOOD_TEXTILES] <= 200 && save_read() && !goods() && g_campaign.coins == cash,
        "saturated sale stays within serialized stock bound and survives reload with its cash");
  fresh(); g_campaign.coins = 0;
  check(sim_recovery_quote().code != SIM_OK && sim_merchant_sail_offer(0, 1).code == SIM_OK,
        "healthy zero-cash company uses free sailing instead of unnecessary recovery debt");
  g_ships[0].state = SHIP_DISABLED; g_ships[0].condition = 0;
  check(sim_recovery_quote().code == SIM_OK, "physically disabled zero-cash ship retains recovery access");
}

static void migration_and_replay() {
  for (uint8_t version : {8, 9, 10}) {
    fresh(); g_campaign.economyVersion = 1;
    auto old = sim_voyage_offer(0, 1, VOYAGE_TRADE, GOOD_TEXTILES, 4);
    check(old.code == SIM_OK && sim_voyage_dispatch(0, old), "legacy signed voyage fixture departs");
    int32_t cash = g_campaign.coins; uint32_t id = g_ships[0].booking.id;
    save_reset(); emitVersion = version; check(write_snapshot(), "legacy whole-company wire is written");
    emitVersion = CH_VERSION;
    check(save_read() && g_campaign.economyVersion == 2 && g_ships[0].booking.id == id &&
          g_ships[0].booking.arrivalCoins == old.arrivalCoins && g_ships[0].merchant.state == MERCHANT_NONE,
          "old signed terms migrate without repricing or merchant conversion");
    g_ui.returnQuiet = 1; sim_advance_minutes(old.minutes);
    check(g_campaign.coins == cash + old.arrivalCoins && !goods() && !g_ships[0].cargoCount,
          "legacy signed arrival pays once and creates no resellable windfall");
    cash = g_campaign.coins; check(save_read(), "settled legacy receipt reloads");
    sim_advance_minutes(old.minutes); check(g_campaign.coins == cash, "legacy settled receipt cannot replay payment");
  }
  fresh(); g_campaign.economyVersion = 1; g_campaign.deliveredUnits = 48; g_ports[3].unlocked = 1;
  uint8_t mixed[GOOD_COUNT]{}; mixed[GOOD_TEXTILES] = mixed[GOOD_IRONWARE] = 2;
  auto oldMixed = sim_voyage_manifest_offer(0, 3, VOYAGE_TRADE, mixed);
  check(oldMixed.code == SIM_OK && sim_voyage_dispatch(0, oldMixed), "v10 mixed signed fixture departs");
  int32_t oldCash = g_campaign.coins;
  save_reset(); emitVersion = 10; check(write_snapshot(), "v10 mixed manifest writes its historical wire");
  emitVersion = CH_VERSION;
  check(save_read() && g_ships[0].booking.quantities[GOOD_TEXTILES] == 2 &&
        g_ships[0].booking.projectQuantities[GOOD_IRONWARE] == 2 &&
        g_ships[0].booking.arrivalCoins == oldMixed.arrivalCoins, "v10 mixed promise keeps exact earmarks and payment");
  sim_advance_minutes(oldMixed.minutes);
  check(g_campaign.coins == oldCash + oldMixed.arrivalCoins && !goods() &&
        g_campaign.projectCargo[1][1] == 2, "old mixed payment and project goods settle without new sellable stock");
  fresh(); CargoItem lot{}; lot.kind = CARGO_MARKET; lot.good = GOOD_GRAIN; lot.quantity = 3;
  lot.paidUnitPrice = 17; lot.contractId = 2; lot.destination = lot.projectRegion = NO_PORT;
  lot.sourcePort = 1; lot.sailed = 1; g_ships[0].cargo[0] = lot; g_ships[0].cargoCount = 1;
  save_reset(); emitVersion = 7;
  for (uint8_t chunk = 0; chunk < CH_COUNT; ++chunk) check(write_chunk(chunk), "v7 fractional market lot wire writes");
  emitVersion = CH_VERSION;
  check(save_read() && g_campaign.economyVersion == 2 && basis() == 53 && goods() == 3 &&
        g_ships[0].cargo[0].sourcePort == NO_PORT, "v7 exact basis migrates without invented provenance");
  check(save_write() && save_read() && basis() == 53, "legacy fractional basis persists on schema11");
  fresh(); auto parcel = sim_package_offer(0, 0); check(sim_package_accept(0, parcel), "replay fixture package accepted");
  auto sail = sim_merchant_sail_offer(0, parcel.destination); check(sim_merchant_sail(0, sail), "replay fixture sails");
  g_campaign.standbySessionId = 71; g_campaign.standbyAccountedRealSeconds = 0;
  check(save_write(), "standby begins from durable unpaid package");
  save_suspend(true); g_campaign.standbyAccountedRealSeconds = sail.minutes / SIM_WORLD_MINUTES_PER_REAL_SECOND;
  sim_advance_minutes(sail.minutes); save_suspend(false);
  check(save_write() && save_read() && g_campaign.standbyAccountedRealSeconds &&
        g_ships[0].merchant.packagesDelivered == 1, "standby elapsed watermark and package credit commit together");
  int32_t cash = g_campaign.coins; sim_advance_minutes(sail.minutes);
  check(g_campaign.coins == cash, "standby reconciliation does not replay completed package");
  fresh(); parcel = sim_package_offer(0, 0); check(sim_package_accept(0, parcel), "semantic recovery fixture accepted");
  cash = g_campaign.coins;
  g_ships[0].cargo[g_ships[0].cargoCount++] = g_ships[0].cargo[0]; g_campaign.coins += 1000;
  check(save_write() && save_read() && g_campaign.coins == cash && g_ships[0].cargoCount == 1,
        "checksum-valid duplicated package rejects entire generation without mixing bonus cash");
}

static void merchant_incidents() {
  for (uint8_t kind = 0; kind < INCIDENT_COUNT; ++kind) for (uint8_t option = 0; option < encounter_choices(kind); ++option) {
    fresh(710 + kind * 3 + option);
    auto buy = trade(true, GOOD_TEXTILES, 1); check(sim_merchant_trade_apply(0, buy), "incident fixture buys one unit");
    auto parcel = sim_package_offer(0, 0); check(sim_package_accept(0, parcel), "incident fixture takes safe parcel");
    auto route = sim_merchant_sail_offer(0, parcel.destination); check(sim_merchant_sail(0, route), "incident fixture sails");
    g_campaign.completedVoyages = 1;
    g_ships[0].state = SHIP_AWAITING_DECISION; g_ships[0].incidentArmed = 0;
    g_ships[0].legElapsedMinutes = g_ships[0].legTotalMinutes / 2;
    g_incident = {1,0,kind,RISK_EVEN}; attention_add(ATTENTION_INCIDENT,0,0,true);
    check(save_write() && save_read() && g_ships[0].state == SHIP_AWAITING_DECISION && g_incident.active,
      "pending merchant decision survives save/reload");
    auto q = sim_merchant_incident_choice(option); check(q.code == SIM_OK, "displayed merchant option can execute");
    failed_commit([&]{return sim_merchant_incident_resolve(option);}, "failed decision rolls back cash, cargo, RNG and pending incident");
    int32_t cash = g_campaign.coins;
    check(sim_merchant_incident_resolve(option) && g_ships[0].state == SHIP_SAILING &&
      g_campaign.coins == cash + q.coins && g_incidentOutcome.valid && g_incidentOutcome.merchant,
      "choice matches quoted cash and resumes ship with a distinct report");
    check(!g_incident.active && g_ships[0].merchant.legMinutes[g_ships[0].routePos] == g_ships[0].legTotalMinutes,
      "resolved incident clears and Watch ETA includes its delay");
    uint64_t before = state_hash(); check(sim_merchant_incident_resolve(option).code != SIM_OK && state_hash() == before,
      "the same incident cannot pay or charge twice");
    check(save_read() && g_ships[0].state == SHIP_SAILING, "resolved voyage survives reload");
    if(kind==INCIDENT_DOUBLE && option==0)check(g_ships[0].sighting==4,"callback intent survives save/reload");
    sim_advance_minutes(30000);
    check(g_ships[0].merchant.state == MERCHANT_ARRIVED && g_ships[0].merchant.packageCoins == parcel.reward + ((kind==INCIDENT_STRANGER || kind==INCIDENT_RESCUE) && option==0?25:0) &&
      g_ships[0].merchant.packagesDelivered == ((kind==INCIDENT_STRANGER || kind==INCIDENT_RESCUE) && option==0?2:1), "all incident choices preserve promised parcel reward");
  }
  fresh(819); g_campaign.coins=5000;g_ships[0].sighting=4;g_ships[0].autoEncounter=INCIDENT_STORM+1;
  auto echo=sim_merchant_sail_offer(0,1);check(sim_merchant_sail(0,echo),"callback voyage starts");
  check(g_ships[0].sighting==3 && g_ships[0].incidentArmed==2 && !g_ships[0].autoEncounter,"callback starts as harmless sighting without stale prior-voyage news");
  sim_advance_minutes(30000);check(!g_incident.active,"callback cannot interrupt travel");
  echo=sim_merchant_sail_offer(0,0);check(sim_merchant_sail(0,echo),"voyage after callback starts");
  check(g_ships[0].sighting!=3,"callback is consumed once");
  fresh(); auto route = sim_merchant_sail_offer(0,1); check(sim_merchant_sail(0,route), "empty ship sails");
  g_ships[0].state = SHIP_AWAITING_DECISION; g_incident = {1,0,INCIDENT_STORM,RISK_EVEN}; g_campaign.coins = 0;
  uint64_t before = state_hash();
  check(sim_merchant_incident_choice(1).code == SIM_INVALID && sim_merchant_incident_resolve(1).code == SIM_INVALID && state_hash() == before,
    "empty hold cannot wager nonexistent merchandise");
  check(sim_merchant_incident_resolve(0) && g_ships[0].state == SHIP_SAILING && !g_campaign.coins,
    "a broke empty ship has a guaranteed free way onward");
  fresh(); g_campaign.coins = 5000; g_campaign.deliveredUnits = 24; check(sim_buy_ship(0), "two-ship incident fixture");
  for (uint8_t i=0;i<2;++i) { auto q=sim_merchant_sail_offer(i,1); check(sim_merchant_sail(i,q), "both ships sail"); g_ships[i].incidentArmed=1; g_ships[i].incidentType=INCIDENT_STORM; }
  g_campaign.completedVoyages=1; sim_advance_minutes(g_ships[0].legTotalMinutes/2);
  check(g_incident.active && g_incidentQueueCount==1 && g_ships[0].state==SHIP_AWAITING_DECISION && g_ships[1].state==SHIP_AWAITING_DECISION,
    "two waiting ships keep independent decisions");
  check(sim_select_incident(1) && g_incident.shipIndex==1 && g_incidentQueue[0].shipIndex==0,
    "selecting a queued ship opens its own captain");
  check(sim_merchant_incident_resolve(0) && g_incident.shipIndex==0 && g_ships[0].state==SHIP_AWAITING_DECISION && g_ships[1].state==SHIP_SAILING,
    "one resolution resumes only the chosen ship");
}

static void encounter_policy_tests() {
  for(uint8_t kind=0;kind<INCIDENT_COUNT;++kind) {
    fresh(2100+kind);g_campaign.completedVoyages=1;
    auto buy=trade(true,GOOD_TEXTILES,1);check(sim_merchant_trade_apply(0,buy),"fallback fixture cargo");
    auto sail=sim_merchant_sail_offer(0,1);check(sim_merchant_sail(0,sail),"fallback fixture sails");
    Ship& ship=g_ships[0];ship.incidentArmed=0;ship.incidentType=kind;ship.state=SHIP_AWAITING_DECISION;
    ship.legElapsedMinutes=ship.legTotalMinutes/2;ship.encounterWait=120;g_incident={1,0,kind,RISK_EVEN};
    int32_t cash=g_campaign.coins;uint8_t count=ship.cargoCount;
    uint64_t beforeAutomatic=state_hash();
    uint8_t forbidden=encounter_fallback(kind)==0?1:0;
    check(sim_merchant_incident_resolve(forbidden,true).code==SIM_INVALID && beforeAutomatic==state_hash(),"automatic handling cannot choose an unauthorized transaction");
    check(save_write() && save_read() && g_ships[0].encounterWait==120,"response countdown persists across reload");
    sim_advance_minutes(60);
    check(g_ships[0].state==SHIP_AWAITING_DECISION && g_ships[0].encounterWait==60,"countdown consumes elapsed time once");
    check(save_write() && save_read(),"partly elapsed window persists");
    sim_advance_minutes(30000);
    check(g_ships[0].state!=SHIP_AWAITING_DECISION && !g_incident.active,"overnight safe passage cannot strand ship");
    check(g_campaign.coins==cash && g_ships[0].cargoCount==count && g_ships[0].cargo[0].quantity==1,
      "automatic response never spends, accepts, sells or loses cargo");
  }
  for(uint8_t kind=INCIDENT_DOUBLE;kind<INCIDENT_COUNT;++kind){
    fresh(3200+kind);g_campaign.coins=5000;
    auto full=trade(true,GOOD_TEXTILES,4);check(sim_merchant_trade_apply(0,full),"full-hold encounter fixture");
    auto sail=sim_merchant_sail_offer(0,1);check(sim_merchant_sail(0,sail),"full-hold fixture sails");
    g_ships[0].state=SHIP_AWAITING_DECISION;g_incident={1,0,kind,RISK_EVEN};
    auto q=sim_merchant_incident_choice(0);
    check(q.code==(kind==INCIDENT_DOUBLE?SIM_OK:SIM_NO_CAPACITY),"exchange fits a full hold; additions cannot overfill it");
  }
  for(uint8_t kind=0;kind<=INCIDENT_REEF;++kind)for(int units=3;units<=4;++units) {
    fresh(3300+kind+units);g_campaign.coins=5000;
    auto load=trade(true,GOOD_TEXTILES,units);check(sim_merchant_trade_apply(0,load),"salvage capacity fixture buys cargo");
    auto route=sim_merchant_sail_offer(0,1);check(sim_merchant_sail(0,route),"salvage capacity fixture sails");
    g_ships[0].state=SHIP_AWAITING_DECISION;g_incident={1,0,kind,RISK_EVEN};
    uint64_t before=state_hash();
    check(sim_merchant_incident_choice(1).code==SIM_NO_CAPACITY && sim_merchant_incident_resolve(1).code==SIM_NO_CAPACITY && state_hash()==before,
      "salvage reserves both reward slots before risking any cargo");
    auto safe=sim_merchant_incident_choice(0);
    check(safe.code==SIM_OK && !safe.coins && !safe.delayMinutes && !safe.riskPercent,
      "safe passage has no coin, cargo or added travel price");
  }
  // Compare exact simulation state under one bulk catch-up and small ticks.
  uint64_t bulk=0;
  for(int pass=0;pass<2;++pass) {
    fresh(444);g_campaign.completedVoyages=1;auto sail=sim_merchant_sail_offer(0,1);check(sim_merchant_sail(0,sail),"clock comparison voyage");
    g_ships[0].incidentArmed=1;g_ships[0].incidentType=INCIDENT_STORM;
    if(!pass)sim_advance_minutes(1200);else for(int i=0;i<20;++i)sim_advance_minutes(60);
    if(!pass)bulk=state_hash();else check(bulk==state_hash(),"bulk catch-up equals continuous simulation through fallback");
  }
  fresh(818);g_campaign.completedVoyages=1;auto buy=trade(true,GOOD_TEXTILES,1);check(sim_merchant_trade_apply(0,buy),"risk fixture cargo");
  auto sail=sim_merchant_sail_offer(0,1);check(sim_merchant_sail(0,sail),"risk fixture sail");
  g_ships[0].state=SHIP_AWAITING_DECISION;g_incident={1,0,INCIDENT_PIRATES,RISK_EVEN};
  g_incident.risk=RISK_DANGEROUS;uint8_t dangerous=sim_merchant_incident_choice(1).riskPercent;
  g_incident.risk=RISK_FAVORABLE;uint8_t favorable=sim_merchant_incident_choice(1).riskPercent;
  check(dangerous>favorable,"risk context changes the actual quoted probability");g_incident.risk=RISK_EVEN;
  unsigned expectedLosses=sim_merchant_incident_choice(1).riskPercent*2;
  unsigned lost=0;
  for(unsigned seed=1;seed<=200;++seed) {
    // Independent deterministic RNG seeds exercise both branches; restore exact fixture.
    Ship before=g_ships[0];Campaign campaign=g_campaign;Incident incident=g_incident;
    g_campaign.rng=seed*7919u;
    check(sim_merchant_incident_resolve(1),"risk branch resolves");
    bool failed=g_ships[0].cargoCount==0;lost+=failed;
    check(failed || (g_ships[0].cargoCount==2 && g_ships[0].cargo[0].quantity==1 &&
      g_ships[0].cargo[1].good==GOOD_SPICES && g_ships[0].cargo[1].quantity==2),
      "salvage either loses exactly the stake or keeps it and gains exactly two quoted goods");
    check(g_campaign.coins==campaign.coins,"salvage has no hidden coin cost");
    g_ships[0]=before;g_campaign=campaign;g_incident=incident;
  }
  check(lost>0 && lost<200 && abs((int)lost-(int)expectedLosses)<20,"quoted risk exercises both bounded loss and cargo reward branches");
  emitVersion=11;check(save_write(),"write prior-version company");emitVersion=CH_VERSION;
  check(save_read() && g_ships[0].encounterWait==0,"version 11 company migrates without inventing elapsed wait");
}

static void encounter_economy_simulation() {
  std::printf("policy,load_units,companies,voyages,net_coins,world_minutes,encounters,stalled\n");
  for(int loadUnits : {1,4})for(int policy=0;policy<3;++policy){
    long long coins=0,elapsed=0;unsigned events=0,stalled=0;
    for(unsigned seed=1;seed<=24;++seed){
      fresh(6000+seed);g_campaign.coins=1000;g_campaign.completedVoyages=1;
      for(int trip=0;trip<12;++trip){
        uint8_t destination=g_ships[0].currentPort?0:1;
        uint8_t quantities[GOOD_COUNT]{};quantities[g_ships[0].currentPort?GOOD_GRAIN:GOOD_TEXTILES]=loadUnits;
        auto buy=sim_merchant_trade_offer(0,true,quantities);if(buy.code==SIM_OK)check(sim_merchant_trade_apply(0,buy),"simulation buys quoted cargo");
        auto sail=sim_merchant_sail_offer(0,destination);check(sim_merchant_sail(0,sail),"simulation dispatches ship");
        bool counted=false;int ticks=0;
        while((g_ships[0].state==SHIP_SAILING || g_ships[0].state==SHIP_AWAITING_DECISION) && ticks++<2000){
          sim_advance_minutes(60);
          if(g_ships[0].state==SHIP_AWAITING_DECISION){
            if(!counted){++events;counted=true;}
            if(policy){
              uint8_t kind=g_incident.kind;
              uint8_t option=encounter_opportunity(kind)?0:policy==2?1:0;
              if(sim_merchant_incident_choice(option).code!=SIM_OK)option=encounter_fallback(kind);
              check(sim_merchant_incident_resolve(option),"simulation applies only valid encounter choice");
            }
          }
        }
        stalled+=ticks>=2000;
        memset(quantities,0,sizeof(quantities));
        for(int i=0;i<g_ships[0].cargoCount;++i)if(g_ships[0].cargo[i].kind==CARGO_MARKET)quantities[g_ships[0].cargo[i].good]+=g_ships[0].cargo[i].quantity;
        auto sale=sim_merchant_trade_offer(0,false,quantities);if(sale.code==SIM_OK && sale.quantity)check(sim_merchant_trade_apply(0,sale),"simulation sells explicitly on arrival");
      }
      coins+=g_campaign.coins-1000;elapsed+=g_campaign.worldMinutes;
    }
    check(!stalled,"all simulated policies finish their voyages");
    std::printf("%s,%d,24,288,%lld,%lld,%u,%u\n",policy==0?"unattended":policy==1?"cautious-active":"opportunistic",loadUnits,coins,elapsed,events,stalled);
  }
}

int main() {
  if (!getenv("SH_FS_DIR")) { std::fprintf(stderr, "SH_FS_DIR must name an isolated test directory\n"); return 2; }
  if (!save_begin()) { std::fprintf(stderr, "test filesystem initialization failed\n"); return 2; }
  if(getenv("SH_ENCOUNTER_MONTE_CARLO")){encounter_economy_simulation();return failures?1:0;}
  starter_cargo(); ownership_and_transactions(); packages_and_capacity(); mixed_and_malformed(); intermediate_and_donation();
  stock_and_no_arbitrage(); migration_and_replay(); merchant_incidents(); encounter_policy_tests();
  save_reset(); std::printf("merchant regressions: %s (%d checks, %d failures)\n", failures ? "FAIL" : "PASS", checks, failures);
  return failures ? 1 : 0;
}
