// Real LittleFS snapshots and actual engine arrivals, with pre-v9 wire fixtures.
#include "../../save.cpp"
#include "economy_fixtures.h"
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
void audio_begin() {} void audio_tick() {} void audio_notice() {} void audio_alert() {}
static int checks, failures;
static void check(bool ok, const char* text) {
  ++checks; if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", text); }
}
static void fresh(uint32_t seed = 51) {
  emitVersion = CH_VERSION; save_reset(); historical_campaign(seed, 1);
  // These ledger scenarios use real historical signed orders. The current
  // merchant arrivals and package credits have their own v2 regression suite.
  check(save_write(), "initial signed-economy fixture save");
}
static VoyageQuote dispatch(uint8_t ship, uint8_t destination, uint8_t mode, uint8_t good) {
  // A preceding real save_read may have upgraded the company to v2. Creating
  // another historical signed order is explicit test setup, never production.
  g_campaign.economyVersion = 1;
  VoyageQuote q = sim_voyage_offer(ship, destination, mode, good, 4);
  check(q.code == SIM_OK, "real voyage is available");
  check(sim_voyage_dispatch(ship, q), "real voyage commits"); return q;
}
static void block_save(bool blocked) {
  std::string path = std::string(getenv("SH_FS_DIR")) + "/sh_company.tmp";
  check((blocked ? mkdir(path.c_str(), 0755) : rmdir(path.c_str())) == 0, "toggle deliberate save failure");
}
static uint64_t hash_state() {
  uint64_t hash = 1469598103934665603ull;
  auto bytes = [&](const void* p, size_t n) { const auto* b = (const uint8_t*)p;
    while (n--) { hash ^= *b++; hash *= 1099511628211ull; } };
  bytes(&g_campaign, sizeof(g_campaign)); bytes(g_ships, sizeof(g_ships)); bytes(g_ports, sizeof(g_ports));
  bytes(g_captains, sizeof(g_captains)); bytes(g_contracts, sizeof(g_contracts)); bytes(&g_ui, sizeof(g_ui));
  bytes(g_attention, sizeof(g_attention)); bytes(g_journal, sizeof(g_journal));
  bytes(&g_incident, sizeof(g_incident)); bytes(g_incidentQueue, sizeof(g_incidentQueue)); return hash;
}
static void actual_and_frozen() {
  fresh(); g_campaign.deliveredUnits = 24; g_campaign.coins = 4000;
  check(sim_buy_ship(0), "second staffed sloop purchased");
  VoyageQuote a = dispatch(0, 1, VOYAGE_FREIGHT, GOOD_TEXTILES);
  VoyageQuote b = dispatch(1, 2, VOYAGE_FREIGHT, GOOD_IRONWARE);
  check(a.minutes < b.minutes, "two real arrivals have separate times");
  int32_t departing = g_campaign.coins;
  g_ui.title = 1; g_ui.returnQuiet = 1; g_ui.card = CARD_MARKET;
  sim_advance_minutes(a.minutes);
  check(g_ui.card == CARD_MARKET, "quiet arrival preserves an open card");
  uint64_t before = hash_state(); ReturnReport first = sim_return_report();
  check(first.bookedArrivals == 1 && first.bookedCoins == a.arrivalCoins && first.bookedProfit == a.profit &&
    first.deliveredUnits == 4 && first.sailingCount == 1, "report uses actual banked cash and separate profit");
  check(sim_return_has_updates() && hash_state() == before, "report and update checks are read only");
  sim_advance_minutes(b.minutes - a.minutes + 60);
  check(g_campaign.coins == departing + a.arrivalCoins + b.arrivalCoins, "actual payouts credited once");
  ReturnReport both = sim_return_report();
  check(both.bookedArrivals == 2 && both.deliveredUnits == 8, "two ships grouped exactly");
  block_save(true);
  check(sim_ack_return_report(both).code == SIM_SAVE_FAILED, "failed group acknowledgement reported");
  check(sim_return_report().bookedArrivals == 2, "failed group ack restores all watermarks");
  block_save(false);
  check(sim_ack_return_report(first), "old frozen report can acknowledge its captured prefix");
  ReturnReport later = sim_return_report();
  check(later.bookedArrivals == 1 && later.eventShipMask == 2 && later.bookedCoins == b.arrivalCoins,
    "arrival after capture remains unseen");
  check(g_ships[0].booking.receiptUnread && g_ships[1].booking.receiptUnread,
    "group acknowledgement does not clear individual receipts");
  check(sim_ack_return_report(both) && !sim_return_has_updates(), "newer frozen report acknowledges both");
  check(sim_ack_return_report(first) && !sim_return_has_updates(), "old report cannot regress newer acknowledgement");
  check(save_read() && !sim_return_has_updates(), "group watermarks survive actual reload");
  check(sim_ack_return_report(both).code == SIM_STALE_PREVIEW, "old runtime report rejected after reload");
  int32_t cash = g_campaign.coins; sim_advance_minutes(20000);
  check(g_campaign.coins == cash && !sim_return_has_updates(), "reading and waiting never repeats payout or recap");
}
static void receipt_prefix_and_overwrite() {
  fresh(); auto a = dispatch(0, 1, VOYAGE_FREIGHT, GOOD_TEXTILES); sim_advance_minutes(a.minutes);
  uint32_t id = g_ships[0].booking.id;
  ReturnReport first = sim_return_report();
  block_save(true);
  check(sim_ack_arrival_receipt(0, id).code == SIM_SAVE_FAILED && g_ships[0].booking.receiptUnread &&
    sim_return_report().bookedArrivals == 1, "failed individual ack restores flag and grouped prefix");
  block_save(false);
  check(sim_ack_arrival_receipt(0, id) && !sim_return_report().bookedArrivals,
    "reading sole receipt prevents the same arrival appearing as new");
  check(sim_ack_return_report(first), "independent milestone can still be acknowledged");
  check(sim_ack_arrival_receipt(0, id + 1).code == SIM_STALE_PREVIEW, "wrong booking receipt cannot clear state");
  auto b = dispatch(0, 0, VOYAGE_TRADE, GOOD_GRAIN); sim_advance_minutes(b.minutes);
  auto c = dispatch(0, 1, VOYAGE_FREIGHT, GOOD_TEXTILES); sim_advance_minutes(c.minutes);
  ReturnReport accumulated = sim_return_report();
  check(accumulated.bookedArrivals == 2 && accumulated.bookedCoins == b.arrivalCoins + c.arrivalCoins &&
    accumulated.bookedProfit == b.profit + c.profit && accumulated.deliveredUnits == 8,
    "ledger survives receipt overwrite across two paid voyages on one ship");
  check(sim_ack_arrival_receipt(0, g_ships[0].booking.id) && sim_return_report().bookedArrivals == 2,
    "latest individual receipt cannot consume older unpresented arrival");
  check(save_read() && sim_return_report().bookedArrivals == 2, "overwritten unread arrivals survive reboot");
  ReturnReport current = sim_return_report(); auto d = dispatch(0, 0, VOYAGE_TRADE, GOOD_GRAIN);
  sim_advance_minutes(d.minutes);
  check(sim_ack_return_report(current) && sim_return_report().bookedArrivals == 1,
    "same ship's newer arrival survives frozen-prefix acknowledgement");
  auto wrong = sim_return_report(); wrong.rows[0].mark.bookedCoins++;
  check(sim_ack_return_report(wrong).code == SIM_STALE_PREVIEW, "tampered captured watermark rejected");
  uint32_t seed = g_campaign.seed; auto stale = sim_return_report(); sim_new_campaign(seed); g_campaign.onboardingStep = ONBOARD_DONE;
  check(sim_ack_return_report(stale).code == SIM_STALE_PREVIEW, "same-seed new company rejects old report");
}
static void legacy_and_readiness() {
  fresh(124); g_campaign.legacyFeatureMask = 0xffff; g_campaign.recoveryDebt = 100;
  // A real old contract sails and settles through deliver_at_port, bypassing no credit code.
  Contract c{}; c.id = g_campaign.nextContractId++; c.active = 2; c.origin = 0;
  c.destination = 1; c.good = GOOD_TEXTILES; c.quantity = 4; c.reward = 120;
  c.kind = JOB_FREIGHT; c.projectRegion = NO_PORT;
  g_contracts[0][0] = c;
  CargoItem cargo{}; cargo.contractId = c.id; cargo.good = c.good; cargo.quantity = 4;
  cargo.kind = CARGO_CONTRACT; cargo.jobKind = c.kind; cargo.destination = 1; cargo.projectRegion = NO_PORT;
  cargo.sourcePort = NO_PORT; g_ships[0].cargo[0] = cargo; g_ships[0].cargoCount = 1;
  g_ui.selectedShip = 0; g_ui.selectedPort = 0;
  check(sim_route_add(1) && sim_dispatch(0), "legacy loaded voyage dispatches");
  g_ships[0].incidentArmed = 0;
  int32_t cash = g_campaign.coins;
  uint32_t duration = g_ships[0].legTotalMinutes;
  sim_advance_minutes(duration + 60);
  ReturnReport legacy = sim_return_report();
  check(legacy.legacyArrivals == 1 && legacy.legacyCoins > 0 && legacy.legacyCoins == g_campaign.coins - cash &&
    !legacy.bookedCoins && !legacy.bookedProfit && legacy.deliveredUnits == 4,
    "legacy actual net credit is separate, with no invented booked profit");
  check(save_read() && sim_return_report().legacyCoins == legacy.legacyCoins, "legacy cumulative credit persists");
  check(sim_ack_return_report(sim_return_report()) && !sim_return_has_updates(), "legacy arrival acknowledged once");
  // The assertions below describe the old paid-freight readiness contract.
  // V2 correctly permits free merchant travel even with an empty treasury.
  g_campaign.economyVersion = 1;
  g_campaign.coins = 0;
  check(sim_return_report().readyCount == 0, "unaffordable connected hull is not reported ready");
  g_campaign.coins = 1000;
  check(sim_return_report().readyCount == 1, "affordable actual freight offer makes hull ready");
}
static void migrate_and_recover() {
  fresh(); auto q = dispatch(0, 1, VOYAGE_FREIGHT, GOOD_TEXTILES); sim_advance_minutes(q.minutes);
  int32_t cash = g_campaign.coins; uint32_t voyage = g_ships[0].booking.id;
  save_reset(); emitVersion = 7; g_campaign.coins = 9999;
  for (uint8_t i = 0; i < CH_COUNT; ++i) check(write_chunk(i), "stale v7 company fixture written");
  emitVersion = 8; g_campaign.coins = cash;
  check(write_snapshot(), "actual v8 whole-company fixture written"); emitVersion = CH_VERSION;
  check(save_read() && g_campaign.coins == cash && g_ships[0].booking.id == voyage,
    "authoritative v8 bundle wins over stale v7 company and preserves assets");
  ReturnReport migrated = sim_return_report();
  check(migrated.bookedArrivals == 1 && migrated.bookedCoins == q.arrivalCoins && migrated.bookedProfit == q.profit &&
    !migrated.milestoneMask, "migration seeds only extant unread receipt, no old milestone celebration");
  check(save_write() && save_read() && sim_return_report().bookedArrivals == 1, "v9 reload does not seed receipt twice");
  check(sim_ack_return_report(sim_return_report()) && save_read() && !sim_return_has_updates() &&
    g_ships[0].booking.receiptUnread, "v9 acknowledgement stays separate from migrated receipt flag");
  // A semantically invalid but checksum-correct current generation must fall back as a whole.
  int32_t oldCash = g_campaign.coins;
  g_ships[0].returnLedger.seen.bookedCargo = g_ships[0].returnLedger.total.bookedCargo + 1;
  g_campaign.coins += 999;
  check(save_write(), "checksum-valid invalid-ledger fixture written");
  check(save_read() && g_campaign.coins == oldCash && !sim_return_has_updates(),
    "invalid ledger rejects entire generation rather than mixing cash with older progress");
  fresh(77); g_campaign.deliveries = 9; g_campaign.coins = 8123; g_campaign.stageSeen = 3;
  attention_add(ATTENTION_ARRIVAL, 0, 0, false);
  save_reset(); emitVersion = 7;
  for (uint8_t i = 0; i < CH_COUNT; ++i) check(write_chunk(i), "actual v7 component fixture written");
  emitVersion = CH_VERSION;
  check(save_read() && g_campaign.coins == 8123 && g_campaign.deliveredUnits == 9,
    "v7 migration preserves old company");
  auto old = sim_return_report();
  check(!old.bookedArrivals && !old.legacyArrivals && !old.bookedCoins && !old.legacyCoins &&
    old.legacyNoticeCount == 1 && !old.milestoneMask, "v7 old arrival is named without fabricated cash or count");
  check(sim_ack_return_report(old) && save_read() && !sim_return_has_updates(), "migrated legacy notice ack persists");
}
static void v8_inflight_and_read_receipt() {
  fresh(95); auto q = dispatch(0, 1, VOYAGE_FREIGHT, GOOD_TEXTILES);
  int32_t departing = g_campaign.coins;
  save_reset(); emitVersion = 8; check(write_snapshot(), "v8 active booking fixture written"); emitVersion = CH_VERSION;
  check(save_read() && g_ships[0].booking.state == BOOKING_SAILING && !sim_return_has_updates(),
    "v8 active voyage migration does not invent an arrival");
  sim_advance_minutes(q.minutes);
  check(save_read() && g_campaign.coins == departing + q.arrivalCoins && sim_return_report().bookedArrivals == 1,
    "migrated active order banks and records exactly one arrival");
  g_ships[0].booking.receiptUnread = 0;
  save_reset(); emitVersion = 8; check(write_snapshot(), "v8 already-read receipt fixture written"); emitVersion = CH_VERSION;
  check(save_read() && !sim_return_has_updates() && !sim_return_report().bookedCoins,
    "already-read v8 receipt is not resurrected as new earnings");
  sim_advance_minutes(20000);
  check(g_campaign.coins == departing + q.arrivalCoins && !sim_return_has_updates(),
    "migrated settled receipt cannot pay or record an arrival again");
}
static void notice_cutoff_and_rollback() {
  fresh(98);
  g_ships[0].state = SHIP_AWAITING_DECISION;
  g_incident = Incident{1, 0, INCIDENT_STORM, RISK_EVEN};
  attention_add(ATTENTION_INCIDENT, 0, 0, true);
  ReturnReport first = sim_return_report();
  check(first.decisionCount == 1 && first.preferredShip == 0 && first.noticeCount == 1,
    "unresolved incident is a single actionable ship");
  ++g_campaign.worldMinutes;
  attention_add(ATTENTION_INCIDENT, 0, 0, true);
  check(save_write(), "refreshed incident notice commits");
  check(sim_ack_return_report(first) && sim_return_has_updates(),
    "frozen notice identity does not clear a newer refresh of the same incident");
  ReturnReport current = sim_return_report();
  block_save(true);
  check(sim_ack_return_report(current).code == SIM_SAVE_FAILED && sim_return_has_updates(),
    "failed notice acknowledgement rolls back unread state");
  block_save(false);
  check(sim_ack_return_report(current) && !sim_return_has_updates() && sim_return_report().decisionCount == 1,
    "reading recap leaves unresolved decision actionable without repeating the banner");
  check(save_read() && !sim_return_has_updates() && sim_return_report().decisionCount == 1,
    "notice acknowledgement and unresolved incident both persist");
}
static void write_version_fixture(uint8_t version) {
  save_reset(); emitVersion = version;
  if (version < 8) {
    for (uint8_t i = 0; i < CH_COUNT; ++i) check(write_chunk(i), "legacy capability component written");
  } else check(write_snapshot(), "legacy capability bundle written");
  emitVersion = CH_VERSION;
}
static void retained_capabilities() {
  constexpr uint32_t retainedBits = RETURN_MILESTONE_TRADE | RETURN_MILESTONE_NEW_ALBION |
    RETURN_MILESTONE_SECOND_SHIP | RETURN_MILESTONE_FITTINGS;
  for (uint8_t version : {7, 8, 9}) {
    fresh(0x1900u + version);
    g_campaign.deliveries = 9; g_campaign.deliveredUnits = 9;
    g_campaign.stageSeen = 4; g_campaign.rank = 2; g_campaign.companyXp = 100;
    g_campaign.coins = 20000; g_ports[3].unlocked = 1;
    for (uint8_t f = 0; f < FEATURE_COUNT; ++f)
      if (sim_feature_stage(f) <= 4) g_campaign.legacyFeatureMask |= (uint16_t)(1u << f);
    // V9 represents a company migrated before the legacy-capability correction:
    // its earned tools exist but their future threshold markers were not seeded.
    g_campaign.returnMilestones = g_campaign.returnMilestonesSeen = RETURN_MILESTONE_TRADE;
    int32_t cash = g_campaign.coins; uint32_t shipId = g_ships[0].id;
    write_version_fixture(version);
    check(save_read() && g_campaign.coins == cash && g_ships[0].id == shipId &&
      g_campaign.shipCount == 1 && !g_ships[0].cargoCount && g_campaign.regionMask == 1,
      "legacy capability reconciliation leaves treasury, fleet, cargo and regions untouched");
    check((g_campaign.returnMilestonesSeen & retainedBits) == retainedBits &&
      !sim_return_report().milestoneMask && !(g_campaign.returnMilestonesSeen & RETURN_MILESTONE_PROJECTS),
      "v7 v8 and existing v9 retain old capabilities without hiding the new project opportunity");
    check(sim_ship_purchase_block(0) == SIM_OK && sim_buy_ship(0),
      "saved yard stage really permits a staffed ship purchase below24 deliveries");
    check(sim_feature_unlocked(FEATURE_YARD) && sim_feature_unlocked(FEATURE_FITTINGS) &&
      sim_buy_module(MODULE_LONG_CASKS), "retained fitting entitlement really exposes and permits fitting below36");
    check(save_write() && save_read() && (g_campaign.returnMilestonesSeen & retainedBits) == retainedBits,
      "seeded legacy capability markers persist in unchanged v9 wire format");
    g_ui.selectedShip = 0;
    for (int trip = 0; trip < 10; ++trip) {
      bool outward = g_ships[0].currentPort == 0;
      auto q = dispatch(0, outward ? 1 : 0, VOYAGE_FREIGHT, outward ? GOOD_TEXTILES : GOOD_GRAIN);
      sim_advance_minutes(q.minutes);
      ReturnReport r = sim_return_report();
      check(!(r.milestoneMask & retainedBits), "later real deliveries do not reannounce retained tools or old New Albion");
      if (g_campaign.deliveredUnits >= 48)
        check(r.milestoneMask & RETURN_MILESTONE_PROJECTS, "new48-delivery project opportunity still announces");
      check(sim_ack_return_report(r), "earned arrival and any new milestone acknowledge normally");
    }
    // Existing charter tools prove neither a completed new material order nor
    // a new regional opening. Exercise both real milestone-producing paths.
    uint8_t region = sim_project_region();
    g_campaign.projectCargo[region][0] = g_campaign.projectCargo[region][1] = sim_project_target(region);
    bool outward = g_ships[0].currentPort == 0;
    auto q = dispatch(0, outward ? 1 : 0, VOYAGE_FREIGHT, outward ? GOOD_TEXTILES : GOOD_GRAIN);
    sim_advance_minutes(q.minutes);
    check(sim_return_report().milestoneMask & (RETURN_MILESTONE_PROJECT_READY_BASE << region),
      "completed construction materials remain a new opportunity despite old charter entitlement");
    check(sim_ack_return_report(sim_return_report()) && sim_project_contribute(region),
      "new regional works still require their actual funded contribution");
    check(sim_return_report().milestoneMask & (RETURN_MILESTONE_REGION_BASE << region),
      "new paid regional opening remains unreviewed until explicitly acknowledged");
  }
}
static void genuinely_new_capabilities() {
  // A retained market is not proof of yard/fittings, a regional opening or a
  // project. Even after reload, each genuinely new earned marker stays unread.
  const uint32_t thresholds[] = {12, 24, 36, 48};
  const uint32_t milestones[] = {RETURN_MILESTONE_NEW_ALBION, RETURN_MILESTONE_SECOND_SHIP,
    RETURN_MILESTONE_FITTINGS, RETURN_MILESTONE_PROJECTS};
  for (int i = 0; i < 4; ++i) {
    fresh(0x2900u + i); g_campaign.coins = 5000;
    g_campaign.legacyFeatureMask = 1u << FEATURE_MARKET;
    g_campaign.deliveredUnits = thresholds[i] - 4;
    g_campaign.stageSeen = 1;
    check(save_write() && save_read(), "market-only legacy company loads before a new threshold");
    check(!(g_campaign.returnMilestonesSeen & milestones[i]), "unearned capability is not preacknowledged");
    auto q = dispatch(0, 1, VOYAGE_FREIGHT, GOOD_TEXTILES); sim_advance_minutes(q.minutes);
    check(sim_return_report().milestoneMask & milestones[i], "actual new capability announces on paid arrival");
    check(save_read() && (sim_return_report().milestoneMask & milestones[i]),
      "genuinely new pending capability remains unread after v9 reload");
  }
  fresh(0x2910u); g_campaign.legacyFeatureMask = (1u << FEATURE_YARD); g_campaign.stageSeen = 1;
  g_campaign.coins = 5000;
  check(save_write() && save_read() && sim_ship_purchase_block(0) == SIM_RANK_REQUIRED &&
    !(g_campaign.returnMilestonesSeen & RETURN_MILESTONE_SECOND_SHIP),
    "yard visibility alone does not falsely imply the actual saved-stage purchase bypass");
}
int main() {
  if (!getenv("SH_FS_DIR") || !save_begin()) return 2;
  actual_and_frozen(); receipt_prefix_and_overwrite(); legacy_and_readiness(); migrate_and_recover();
  v8_inflight_and_read_receipt();
  notice_cutoff_and_rollback();
  retained_capabilities(); genuinely_new_capabilities();
  save_reset();
  std::printf("Return ledger v9: %s (%d checks, %d failures)\n", failures ? "FAIL" : "PASS", checks, failures);
  return failures ? 1 : 0;
}
