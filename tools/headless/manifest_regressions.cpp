// Actual engine transactions and LittleFS bundles; no UI or mocked saves.
#include "../../save.cpp"
#include "economy_fixtures.h"
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
  emitVersion = CH_VERSION; save_reset(); historical_campaign(seed, 1);
  // This suite preserves the historical signed-manifest contract. New v2
  // owned-goods semantics are tested separately in merchant_regressions.
  g_campaign.coins = 6000; g_campaign.deliveredUnits = 48; g_ports[3].unlocked = 1;
  for (int p = 0; p < MAX_PORTS; ++p) for (int g = 0; g < GOOD_COUNT; ++g) g_ports[p].stock[g] = 100;
  check(save_write(), "starting company commits");
}
static uint64_t state_hash() {
  uint64_t value = 1469598103934665603ull;
  auto bytes = [&](const void* data, size_t size) { const auto* p = (const uint8_t*)data;
    while (size--) { value ^= *p++; value *= 1099511628211ull; } };
  bytes(&g_campaign, sizeof(g_campaign)); bytes(g_ships, sizeof(g_ships)); bytes(g_ports, sizeof(g_ports));
  bytes(g_captains, sizeof(g_captains)); bytes(g_contracts, sizeof(g_contracts));
  bytes(&g_ui, sizeof(g_ui)); bytes(g_attention, sizeof(g_attention)); bytes(g_journal, sizeof(g_journal));
  return value;
}
static VoyageQuote mixed(uint8_t ship = 0, uint8_t textiles = 2, uint8_t iron = 2) {
  uint8_t amounts[GOOD_COUNT]{}; amounts[GOOD_TEXTILES] = textiles; amounts[GOOD_IRONWARE] = iron;
  return sim_voyage_manifest_offer(ship, 3, VOYAGE_TRADE, amounts);
}
static void block_save(bool blocked) {
  std::string path = std::string(getenv("SH_FS_DIR")) + "/sh_company.tmp";
  check((blocked ? mkdir(path.c_str(), 0755) : rmdir(path.c_str())) == 0, "toggle save failure");
}
static bool corrupt_snapshot() {
  File f = LittleFS.open(SNAP_PATH, "r+"); uint8_t byte = 0;
  bool ok = f && f.seek(60) && f.read(&byte, 1) == 1; byte ^= 0x51;
  ok &= f.seek(60) && f.write(&byte, 1) == 1; f.flush(); f.close(); return ok;
}

static void quotes_and_readonly() {
  fresh(); uint64_t before = state_hash(); uint8_t empty[GOOD_COUNT]{};
  auto metadata = sim_voyage_manifest_offer(0, 3, VOYAGE_TRADE, empty);
  check(metadata.code != SIM_OK && !metadata.quantity && metadata.capacity == 4 && metadata.minutes == 4800,
        "empty manifest exposes route and capacity without silently filling");
  check(metadata.offeredGoods == ((1u << GOOD_TEXTILES) | (1u << GOOD_IRONWARE)) &&
        metadata.projectGoods == (1u << GOOD_IRONWARE) && metadata.availableQuantities[GOOD_IRONWARE] == 4,
        "all compatible cargo and its project meaning are exposed");
  check(metadata.unitCargoCosts[GOOD_TEXTILES] == 35 && metadata.unitGrossPayments[GOOD_IRONWARE] == 100,
        "empty load metadata includes readable per-good prices");
  for (int pass = 0; pass < 30; ++pass) {
    auto q = mixed();
    check(q.code == SIM_OK && q.quantity == 4 && q.cargoCost == 160 && q.sailingCost == 96 &&
          q.grossPayment == 400 && q.duty == 40 && q.buyerPayment == 360 && q.bonus == 40 &&
          q.payNow == 256 && q.arrivalCoins == 400 && q.profit == 144 && q.projectQuantities[GOOD_IRONWARE] == 2,
          "mixed quote sums duty and full-hold margin bonus once");
  }
  auto cash = mixed(0, 4, 0), project = mixed(0, 0, 4), partial = mixed(0, 2, 1);
  check(cash.code == SIM_OK && cash.payNow == 236 && cash.arrivalCoins == 404 && cash.profit == 168 &&
        cash.projectRegion == NO_PORT, "New Albion cash choice is distinct");
  check(project.code == SIM_OK && project.payNow == 276 && project.arrivalCoins == 396 && project.profit == 120,
        "New Albion material choice sacrifices cash for four physical project units");
  check(partial.quantity == 3 && !partial.bonus && partial.payNow == 211 && partial.arrivalCoins == 270 &&
        partial.profit == 59, "partial mixed load has exact cargo cost and no full bonus");
  auto deliberateLoss = mixed(0, 0, 2);
  check(deliberateLoss.code == SIM_OK && deliberateLoss.payNow == 186 && deliberateLoss.arrivalCoins == 180 &&
        deliberateLoss.profit == -6 && !deliberateLoss.bonus && deliberateLoss.projectQuantities[GOOD_IRONWARE] == 2,
        "deliberate two-Iron trade review exposes its actual six-coin loss without a subsidy");
  check(mixed(0, 4, 1).code == SIM_NO_CAPACITY, "five items cannot fit four slots");
  uint8_t bad[GOOD_COUNT]{}; bad[GOOD_GRAIN] = 1;
  auto rejected = sim_voyage_manifest_offer(0, 3, VOYAGE_TRADE, bad);
  check(rejected.code != SIM_OK && rejected.quantities[GOOD_GRAIN] == 1 && !rejected.quantities[GOOD_TEXTILES],
        "unsupported cargo is rejected without replacing the player's selection");
  check(sim_voyage_manifest_offer(0, 3, VOYAGE_TRADE, nullptr).code == SIM_INVALID, "null manifest rejected");
  check(state_hash() == before, "browsing every quote leaves the entire company unchanged");
  auto belmar = sim_voyage_offer(0, 1, VOYAGE_TRADE, GOOD_TEXTILES, 4);
  auto north = sim_voyage_offer(0, 2, VOYAGE_TRADE, GOOD_IRONWARE, 4);
  check(belmar.minutes == 1200 && belmar.payNow == 180 && belmar.profit == 80 && belmar.arrivalCoins == 260,
        "fast Textiles fixture is exact");
  check(north.minutes == 2400 && north.payNow == 236 && north.profit == 115 && north.arrivalCoins == 351 &&
        north.grossPayment == 340 && north.duty == 17, "Northwatch pays more per trip and less per minute");
}

static void atomic_reservations() {
  fresh(); check(sim_buy_ship(0), "second crewed ship available");
  g_campaign.projectCargo[1][1] = sim_project_target(1) - 2;
  auto a = mixed(), b = mixed(1); check(a.code == SIM_OK && b.code == SIM_OK, "two drafts can inspect same remaining orders");
  uint64_t before = state_hash(); block_save(true);
  check(sim_voyage_dispatch(0, a).code == SIM_SAVE_FAILED && state_hash() == before,
        "save failure rolls back funds, both stocks, reservations, ship and UI");
  block_save(false); check(sim_voyage_dispatch(0, a), "mixed departure commits");
  check(g_ports[0].stock[GOOD_TEXTILES] == 98 && g_ports[0].stock[GOOD_IRONWARE] == 98 &&
        g_ports[3].bookingUsed[GOOD_TEXTILES] == 2 && !g_ports[3].bookingUsed[GOOD_IRONWARE],
        "two owned goods debited together; only ordinary order consumes recurring allowance");
  before = state_hash(); check(sim_voyage_dispatch(1, b).code == SIM_STALE_PREVIEW && state_hash() == before,
        "another boat cannot reuse a stale mixed project reservation");
  auto newer = mixed(1); check(newer.code != SIM_OK && newer.quantities[GOOD_IRONWARE] == 2,
        "refreshed draft preserves the unavailable requested good");
  uint32_t refresh = sim_voyage_next_refresh_minute(); sim_advance_minutes(refresh - g_campaign.worldMinutes);
  check(g_ships[0].booking.state == BOOKING_SAILING && mixed(1).code != SIM_OK,
        "ordinary demand refresh cannot reopen project units committed on a longer voyage");
  sim_advance_minutes(1800);
  check(g_campaign.projectCargo[1][1] == sim_project_target(1) && !g_ships[0].cargoCount,
        "only two earmarked Ironware reach the dock and cannot be resold");
  check(mixed(1).code != SIM_OK, "completed project materials never become renewable orders");

  fresh(); check(sim_buy_ship(0), "second ship for ordinary contention");
  g_ports[3].bookingEpoch = g_campaign.worldMinutes / 3600;
  g_ports[3].bookingUsed[GOOD_TEXTILES] = 10; // two of twelve ordinary units remain
  a = mixed(); b = mixed(1); check(sim_voyage_dispatch(0, a), "last ordinary units can share a mixed order");
  before = state_hash(); check(sim_voyage_dispatch(1, b).code == SIM_STALE_PREVIEW && state_hash() == before,
        "exhausting one good atomically rejects the whole second manifest");
}

static void persistence_and_arrival() {
  fresh(); auto q = mixed(); int32_t starting = g_campaign.coins;
  check(sim_voyage_dispatch(0, q), "mixed journey departs"); uint32_t id = g_ships[0].booking.id;
  check(save_read() && g_ships[0].booking.id == id && g_ships[0].cargoCount == 2 &&
        g_ships[0].booking.quantities[GOOD_TEXTILES] == 2 && g_ships[0].booking.projectQuantities[GOOD_IRONWARE] == 2 &&
        g_ships[0].booking.duty == 40 && g_campaign.coins == starting - 256,
        "v10 actual bundle restores locked prices, both cargo rows and their ownership");
  // Real reload upgrades to v2; this next assertion specifically exercises
  // the old signed-return planner, not the current merchant UI.
  g_campaign.economyVersion = 1;
  uint8_t returnLoad[GOOD_COUNT]{}; returnLoad[GOOD_GRAIN] = 2; returnLoad[GOOD_TIMBER] = 2;
  uint64_t before = state_hash(); auto plan = sim_return_voyage_manifest_offer(0, 0, VOYAGE_TRADE, returnLoad);
  check(plan.code == SIM_OK && plan.planningOnly && plan.quantity == 4 && plan.origin == 3,
        "mixed return can be planned from future port");
  check(sim_voyage_dispatch(0, plan).code == SIM_INVALID && state_hash() == before,
        "return planning cannot reserve cargo or dispatch a sailing ship");
  g_ui.title = 1; g_ui.returnQuiet = 1;
  g_campaign.standbySessionId = 100; g_campaign.standbyAccountedRealSeconds = 0;
  check(save_write(), "departure and standby start are one durable baseline");
  save_suspend(true); sim_advance_minutes(q.minutes);
  g_campaign.standbyAccountedRealSeconds = q.minutes / SIM_WORLD_MINUTES_PER_REAL_SECOND;
  save_suspend(false); check(save_write(), "timer catch-up commits mixed arrival with its elapsed watermark");
  check(g_campaign.coins == starting + 144 && g_campaign.projectCargo[1][1] == 2 &&
        g_campaign.deliveredUnits == 52 && g_ships[0].booking.settledQuantity == 4 && !g_ships[0].cargoCount,
        "one arrival credits four paid units but only the two earmarked project units");
  ReturnReport report = sim_return_report();
  check(report.bookedArrivals == 1 && report.bookedCoins == 400 && report.bookedProfit == 144 &&
        report.deliveredUnits == 4, "return ledger groups one mixed voyage with actual cash and profit");
  check(corrupt_snapshot() && save_read() && g_campaign.coins == starting - 256 &&
        g_ships[0].booking.state == BOOKING_SAILING && !g_campaign.projectCargo[1][1] &&
        !g_campaign.standbyAccountedRealSeconds, "corrupt settled bundle restores whole uncredited departure");
  save_suspend(true); sim_advance_minutes(q.minutes);
  g_campaign.standbyAccountedRealSeconds = q.minutes / SIM_WORLD_MINUTES_PER_REAL_SECOND;
  save_suspend(false); check(save_write(), "replayed mixed arrival commits once");
  for (int repeat = 0; repeat < 8; ++repeat) {
    check(save_read(), "settled mixed receipt reloads"); sim_advance_minutes(600);
    check(g_campaign.coins == starting + 144 && g_campaign.projectCargo[1][1] == 2 &&
          sim_return_report().bookedArrivals == 1, "reopening cannot pay or build the dock twice");
  }
  check(sim_ack_arrival_receipt(0, id) && save_read() && !g_ships[0].booking.receiptUnread &&
        g_campaign.coins == starting + 144, "reading mixed receipt only acknowledges it");
}

static void two_project_materials() {
  fresh(); g_campaign.regionMask = g_campaign.chartMask = 31;
  for (int p = 0; p < 20; ++p) g_ports[p].unlocked = 1;
  Ship& ship = g_ships[0]; ship.currentPort = ship.fromPort = ship.toPort = 12;
  g_captains[0].currentPort = 12;
  uint8_t quantities[GOOD_COUNT]{}; quantities[GOOD_TIMBER] = 1; quantities[GOOD_IRONWARE] = 3;
  g_campaign.projectCargo[5][0] = sim_project_target(5) - 1;
  auto q = sim_voyage_manifest_offer(0, 16, VOYAGE_TRADE, quantities);
  check(q.code == SIM_OK && q.projectRegion == 5 && q.projectQuantities[GOOD_TIMBER] == 1 &&
        q.projectQuantities[GOOD_IRONWARE] == 3, "one later voyage can book both real dock materials");
  check(sim_voyage_dispatch(0, q) && save_read(), "both project rows survive actual departure reload");
  sim_advance_minutes(q.minutes);
  check(g_campaign.projectCargo[5][0] == sim_project_target(5) && g_campaign.projectCargo[5][1] == 3 &&
        g_campaign.deliveredUnits == 52 && !g_ships[0].cargoCount,
        "each material credits its own actual quantity, not the manifest's first good or total");
}

static void intentional_loss_receipt() {
  fresh(); auto q = mixed(0, 0, 2); int32_t coins = g_campaign.coins;
  check(sim_voyage_dispatch(0, q) && save_read() && g_ships[0].booking.profit == -6,
        "explicitly approved losing trade persists its honest terms");
  sim_advance_minutes(q.minutes); ReturnReport report = sim_return_report();
  check(g_campaign.coins == coins - 6 && g_campaign.projectCargo[1][1] == 2 &&
        report.bookedCoins == 180 && report.bookedProfit == -6 && report.deliveredUnits == 2,
        "negative net journey banks the real receipt and delivers only purchased project units");
  check(sim_ack_return_report(report) && save_read() && !sim_return_report().bookedArrivals &&
        g_campaign.coins == coins - 6, "negative-profit recap acknowledgement survives reload without altering cash");
}

static void migration_and_validation() {
  for (uint8_t version : {8, 9}) {
    fresh(80 + version); auto q = sim_voyage_offer(0, 2, VOYAGE_TRADE, GOOD_IRONWARE, 4);
    check(sim_voyage_dispatch(0, q), "prepare actual older single-good booking");
    // The saved v8/v9 contract pre-dates today's 115-profit quote.
    BookedVoyage& b = g_ships[0].booking; b.buyerPayment = 320; b.bonus = 28;
    b.payout = b.arrivalCoins = 348; b.profit = 112;
    save_reset(); emitVersion = version; check(write_snapshot(), "old authoritative full bundle writes");
    emitVersion = CH_VERSION;
    check(save_read() && g_ships[0].booking.quantities[GOOD_IRONWARE] == 4 &&
          g_ships[0].booking.arrivalCoins == 348 && !g_ships[0].booking.priceVersion,
          "v8/v9 migration expands manifest without changing promised historical prices");
    int32_t coins = g_campaign.coins; sim_advance_minutes(q.minutes);
    check(save_read() && g_campaign.coins == coins + 348 && g_ships[0].booking.profit == 112,
          "migrated old voyage credits its locked terms exactly once");
    save_reset(); emitVersion = version; check(write_snapshot(), "older unread settled receipt writes");
    emitVersion = CH_VERSION;
    check(save_read() && g_ships[0].booking.state == BOOKING_SETTLED &&
          g_ships[0].booking.quantities[GOOD_IRONWARE] == 4 && g_ships[0].booking.receiptUnread &&
          g_campaign.coins == coins + 348, "older settled receipts migrate without a second payment");
  }
  fresh(); auto q = mixed(); check(sim_voyage_dispatch(0, q), "valid mixed bundle for semantic corruption tests");
  Ship before = g_ships[0];
  auto rejects = [&](const char* message) {
    size_t length = build_chunk(CH_FLEET);
    check(!apply_chunk(CH_FLEET, buf, length, CH_VERSION), message);
    g_ships[0] = before;
  };
  g_ships[0].booking.quantities[GOOD_TEXTILES] = 3;
  rejects("checksum-valid manifest total mismatch rejected");
  g_ships[0].booking.projectQuantities[GOOD_TEXTILES] = 1;
  rejects("non-material good cannot be earmarked as project cargo");
  g_ships[0].cargo[1].projectRegion = NO_PORT;
  rejects("cargo rows must retain the signed project ownership");
  g_ships[0].booking.duty += 1;
  rejects("checksum-valid inconsistent aggregate duty rejected");
  g_ships[0].cargo[0].good = GOOD_TIMBER;
  rejects("different cargo cannot masquerade as a locked manifest");
  CargoItem extra{}; extra.kind = CARGO_MARKET; extra.good = GOOD_GRAIN; extra.quantity = 1;
  extra.sourcePort = 0; extra.destination = extra.projectRegion = NO_PORT;
  g_ships[0].cargo[g_ships[0].cargoCount++] = extra;
  rejects("a signed empty-hold departure cannot contain unrelated extra cargo");
}

int main() {
  if (!getenv("SH_FS_DIR") || !save_begin()) return 2;
  quotes_and_readonly(); atomic_reservations(); persistence_and_arrival(); two_project_materials();
  intentional_loss_receipt(); migration_and_validation();
  std::printf("Mixed manifest v10: %s (%d checks, %d failures)\n", failures ? "FAIL" : "PASS", checks, failures);
  return failures ? 1 : 0;
}
