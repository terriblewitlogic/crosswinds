#pragma once

#include <unistd.h>

// This fixture runs the shipping input_update() buffered branch and renderer.
// Only the sampler clock, last-interaction timestamp and simulation time are
// controlled. No real-minute sleeps or direct calls to UI action handlers.
//
// Every X4 load migrates an older company to the merchant economy, and its
// signed voyages still at sea settle under their original terms. Each fixture
// is therefore an existing signed-voyage company: it signs real voyages, then
// a real save and load migrates it before the return flow is exercised through
// the merchant screens a player now sees. Package-only merchant returns are
// covered by x4_merchant_return_test.h.
struct X4ReturnUiFixture {
  uint32_t sampler = 0;
  int checks = 0;

  bool check(bool ok, const char* label) {
    ++checks;
    ::printf("return UI: %s %s (check %d; card %u page %u scene %u cash %ld)\n",
      ok ? "PASS" : "FAIL", label, checks, g_ui.card, g_ui.page, g_ui.scene, (long)g_campaign.coins);
    ::fflush(stdout);
    return ok;
  }

  void enqueue(uint8_t action) {
    int button = x4_button_for(action);
    for (int i = 0; i < 7; ++i, sampler += 10)
      board_x4_test_sample_buttons(sampler, (uint8_t)(1u << button));
    for (int i = 0; i < 6; ++i, sampler += 10) board_x4_test_sample_buttons(sampler, 0);
  }

  void frame() { input_update(); render_frame(); input_update(); }
  void key(uint8_t action) {
    input_update(); // settle the previous screen's context before a fresh tap
    enqueue(action); frame();
  }

  bool command(uint8_t command) {
    if (!input_test_focus_command(command)) return false;
    key(X4_ACT_OK); return true;
  }

  bool row(int n) {
    if (!input_test_focus_row(n)) return false;
    key(X4_ACT_OK); return true;
  }

  static bool same_region(const HitRegion& a, const HitRegion& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.id == b.id;
  }

  static int cargo_units(int ship, int good) {
    int units = 0;
    for (int i = 0; i < g_ships[ship].cargoCount; ++i)
      if (g_ships[ship].cargo[i].good == good) units += g_ships[ship].cargo[i].quantity;
    return units;
  }

  bool focus_footer(int id) {
    for (int i = 0; i < X4_CARD_ROW_MAX + 6; ++i) {
      HitRegion focus{};
      if (x4_focus_region(&focus) && same_region(focus, voyage_footer_region(g_ui.card, id))) return true;
      key(X4_ACT_DOWN);
    }
    return false;
  }

  void capture(const char* name) {
    const char* dir = getenv("SH_X4_RETURN_DIR");
    if (!dir || !dir[0]) return;
    ::mkdir(dir, 0755);
    render_request_full_refresh(); render_frame(); input_update();
    char path[1024]; snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    dump_ppm_native(path);
    ::printf("return UI: capture %s\n", path);
  }

  // An existing signed-voyage company, before the load that migrates it.
  bool fresh(int ships = 1) {
    if (x4_return_report()) key(X4_ACT_BACK);
    if (board_x4_asleep()) key(X4_ACT_POWER);
    save_reset(); historical_campaign(42, 1);
    g_campaign.tutorialDismissed = 1;
    g_campaign.legacyFeatureMask = 0xffff;
    g_campaign.deliveredUnits = 24;
    g_campaign.coins = 20000;
    // This fixture represents an existing company that has already seen its
    // first three growth unlocks, not a new player receiving those popups now.
    g_campaign.returnMilestones = RETURN_MILESTONE_TRADE | RETURN_MILESTONE_NEW_ALBION |
      RETURN_MILESTONE_SECOND_SHIP;
    g_campaign.returnMilestonesSeen = g_campaign.returnMilestones;
    g_ui.title = 0; g_ui.selectedPort = 0;
    for (int i = 1; i < ships; ++i)
      if (!check(sim_buy_ship(0), "fixture purchases a genuinely staffed second ship")) return false;
    if (!check(sim_ack_return_report(sim_return_report()), "fixture acknowledges only initial company milestones")) return false;
    x4_prepare_return(false);
    return true;
  }

  bool dispatch(uint8_t ship, uint8_t destination, VoyageQuote* result = nullptr) {
    VoyageQuote quote = sim_voyage_offer(ship, destination, VOYAGE_FREIGHT, NO_PORT, 4);
    if (!check(quote.code == SIM_OK && quote.arrivalCoins != quote.profit,
               "fixture gets a real freight quote with distinct banked income and profit")) return false;
    if (!check(sim_voyage_dispatch(ship, quote), "fixture dispatch commits real cargo and sailing cost")) return false;
    if (result) *result = quote;
    return true;
  }

  // The real load path migrates the company, keeping signed voyages at sea.
  bool migrate() {
    if (!check(g_campaign.economyVersion == 1 && save_write() && save_read() && g_campaign.economyVersion == 2,
        "loading the signed-voyage company migrates it to the merchant economy")) return false;
    x4_prepare_return(false);
    g_ui.title = 0; g_ui.card = CARD_NONE; g_ui.scene = SCENE_FLEET; g_ui.page = 0;
    g_ui.selectedShip = 0; g_ui.selectedPort = 0;
    input_test_set_last_interaction_ms(millis());
    board_x4_discard_button_events(); frame();
    return check(!x4_return_report() && !sim_return_has_updates(), "migration itself is not news");
  }

  void idle() { input_test_set_last_interaction_ms(millis() - (X4_RETURN_IDLE_MS + 1000u)); }
};

static bool run_x4_return_ui_test() {
  X4ReturnUiFixture t;
  const char* fs = getenv("SH_FS_DIR");
  if (!t.check(fs && fs[0], "destructive test reset uses an explicit isolated host filesystem")) return false;
  board_x4_test_buffered_input(true);

  // Several actual signed voyages settle while the reader is unattended. A
  // coherent reload exercises the boot entry point with persisted data.
  if (!t.fresh(2)) return false;
  VoyageQuote first{}, second{};
  if (!t.dispatch(0, 1, &first) || !t.dispatch(1, 1, &second) || !t.migrate()) return false;
  int32_t afterSail = g_campaign.coins;
  t.idle();
  sim_advance_minutes(max(first.minutes, second.minutes));
  if (!t.check(g_campaign.coins == afterSail + first.arrivalCoins + second.arrivalCoins &&
      g_ships[0].booking.state == BOOKING_SETTLED && g_ships[1].booking.state == BOOKING_SETTLED,
      "two migrated settlements bank exactly the two signed payments")) return false;
  if (!t.check(save_write() && save_read(), "the whole settled company survives a coherent reload")) return false;
  x4_prepare_return(true); t.frame();
  const ReturnReport* report = x4_return_report();
  if (!t.check(report && report->meaningful && report->bookedArrivals == 2 && report->rowCount == 2 &&
      report->bookedCoins == (int64_t)first.arrivalCoins + second.arrivalCoins &&
      report->bookedProfit == (int64_t)first.profit + second.profit &&
      report->deliveredUnits == first.quantity + second.quantity &&
      !g_ui.title && g_ui.scene == SCENE_COMPANY && g_ui.card == CARD_NONE,
      "boot presents one Company update with exact banked income, net profit and cargo")) return false;
  t.capture("01-boot-company-update");

  if (!t.check(t.command(X4_CMD_LEDGER) && g_ui.card == CARD_RETURN_DETAILS &&
      x4_return_report() && x4_return_row_count() == 2,
      "Updates opens optional per-ship details without acknowledging the recap")) return false;
  t.capture("02-optional-update-details");
  int detailShip = x4_return_row_ship(1);
  if (!t.check(input_test_focus_row(1), "second ship detail is a physical focus target")) return false;
  t.key(X4_ACT_BACK);
  if (!t.check(g_ui.card == CARD_NONE && g_ui.scene == SCENE_COMPANY && x4_return_report() && sim_return_has_updates(),
      "Back from optional details returns to the same frozen Company update")) return false;

  char blockedPath[1024]; snprintf(blockedPath, sizeof(blockedPath), "%s/sh_company.tmp", fs);
  if (!t.check(::mkdir(blockedPath, 0755) == 0, "fixture can block only the atomic save temporary path")) return false;
  t.key(X4_ACT_BACK);
  bool refusedAck = x4_return_report() && sim_return_has_updates() && g_ui.noticeCode == SIM_SAVE_FAILED;
  t.capture("04-save-failure-retains-update");
  int unblockResult = ::rmdir(blockedPath);
  if (!t.check(refusedAck && unblockResult == 0,
      "failed acknowledgement keeps unseen reports and the active recap available for retry")) return false;
  // On the merchant economy a detail row is the way into that ship: it
  // acknowledges the update and opens the neutral ship overview.
  if (!t.check(t.command(X4_CMD_LEDGER) && g_ui.card == CARD_RETURN_DETAILS && t.row(1),
      "the retry opens the second ship from its detail row")) return false;
  if (!t.check(!x4_return_report() && !sim_return_has_updates() && g_ui.card == CARD_SHIP &&
      g_ui.selectedShip == detailShip && g_ships[detailShip].booking.state == BOOKING_SETTLED &&
      g_ships[detailShip].currentPort == g_ships[detailShip].booking.destination,
      "one detail row acknowledges the grouped update once and opens its arrived ship")) return false;
  t.capture("03-detail-ship-overview");
  if (!t.check(save_read() && !sim_return_has_updates(), "acknowledged watermark survives reload")) return false;
  x4_prepare_return(true); t.frame();
  if (!t.check(!x4_return_report(), "reload without new events cannot create an empty recap")) return false;

  // The first ordinary button after idle must not perform the interrupted
  // purchase. It opens a read-only recap, then BACK restores the same good and
  // explicit Buy control. The pending payment was already banked on arrival.
  if (!t.fresh(2) || !t.dispatch(1, 1, &second) || !t.migrate()) return false;
  if (!t.check(t.command(X4_CMD_SHIP_0) && g_ui.card == CARD_SHIP && t.row(0) &&
      g_ui.card == CARD_TRADE && x4_merchant_mode() == 0,
      "ship zero opens Buy at its home port")) return false;
  int textiles = -1;
  for (int r = 0; r < 2; ++r) if (x4_merchant_good(r) == GOOD_TEXTILES) textiles = r;
  if (textiles >= 0 && t.row(textiles))
    while (x4_merchant_trade_quote().quantity > 1) t.key(X4_ACT_DOWN);
  if (!t.check(textiles >= 0 && x4_merchant_editing() &&
      x4_merchant_trade_quote().quantity == 1 && t.focus_footer(1),
      "Buy has a one-unit Textiles quote with its Buy control focused")) return false;
  HitRegion marketFocus{}; x4_focus_region(&marketFocus);
  uint8_t marketGood = g_ui.selectedGood, marketCargo = g_ships[0].cargoCount;
  t.idle(); sim_advance_minutes(second.minutes);
  int32_t idleCash = g_campaign.coins;
  if (!t.check(g_ui.card == CARD_TRADE && x4_merchant_editing() && g_ships[1].booking.state == BOOKING_SETTLED,
      "unattended signed arrival preserves the open Buy quote")) return false;
  input_update();
  uint32_t pressesBeforeReturn = x4_press_count();
  t.enqueue(X4_ACT_OK); t.enqueue(X4_ACT_OK); t.frame();
  if (!t.check(x4_return_report() && g_campaign.coins == idleCash && g_ships[0].cargoCount == marketCargo,
      "idle OK is consumed by the recap and cannot buy goods")) return false;
  if (!t.check(x4_press_count() == pressesBeforeReturn + 1,
      "a second old-screen OK queued during refresh cannot act on the new recap")) return false;
  t.capture("05-idle-trade-return");
  t.key(X4_ACT_BACK);
  HitRegion restoredFocus{}; x4_focus_region(&restoredFocus);
  if (!t.check(!x4_return_report() && g_ui.card == CARD_TRADE && x4_merchant_editing() &&
      g_ui.selectedShip == 0 && g_ui.selectedPort == 0 && g_ui.selectedGood == marketGood &&
      x4_merchant_trade_quote().quantity == 1 &&
      X4ReturnUiFixture::same_region(marketFocus, restoredFocus) && g_campaign.coins == idleCash,
      "Back restores the Buy quote and its focus without spending")) return false;
  t.key(X4_ACT_OK);
  if (!t.check(g_campaign.coins < idleCash && X4ReturnUiFixture::cargo_units(0, marketGood) == 1,
      "a subsequent explicit OK performs one purchase on the restored control")) return false;
  idleCash = g_campaign.coins;
  if (!t.check(!x4_merchant_editing() && input_test_focus_row(textiles), "purchase returns to the item list")) return false;
  input_test_set_last_interaction_ms(millis() - X4_RETURN_IDLE_MS);
  t.key(X4_ACT_OK);
  if (!t.check(!x4_return_report() && g_ui.card == CARD_TRADE && x4_merchant_editing() &&
      g_campaign.coins == idleCash,
      "without meaningful news, idle input acts normally on the visible Trade screen instead of an empty recap")) return false;

  // A reviewed Sail is an uncommitted draft. Returning from the recap must
  // preserve its destination, quoted terms and Sail focus byte-for-byte.
  if (!t.fresh(2) || !t.dispatch(1, 1, &second) || !t.migrate()) return false;
  if (!t.check(t.command(X4_CMD_SHIP_0) && g_ui.card == CARD_SHIP && t.row(1) &&
      g_ui.card == CARD_VOYAGE && g_ui.page == 0,
      "idle Sail fixture enters real voyage preparation")) return false;
  if (!t.check(t.row(0) && g_ui.page == 2 && x4_merchant_sail_quote().code == SIM_OK && t.focus_footer(1),
      "reviewed free voyage has an explicit Sail focus")) return false;
  MerchantSailQuote interrupted = x4_merchant_sail_quote();
  UiState interruptedUi = g_ui;
  HitRegion quoteFocus{}; x4_focus_region(&quoteFocus);
  t.idle(); sim_advance_minutes(second.minutes); idleCash = g_campaign.coins;
  if (!t.check(g_ui.card == CARD_VOYAGE && g_ui.page == 2,
      "arrival while idle cannot replace a reviewed voyage")) return false;
  t.key(X4_ACT_OK);
  if (!t.check(x4_return_report() && g_ships[0].state == SHIP_DOCKED &&
      g_ships[0].merchant.state != MERCHANT_SAILING && g_campaign.coins == idleCash,
      "idle Sail press is consumed without dispatching")) return false;
  t.key(X4_ACT_BACK); x4_focus_region(&restoredFocus);
  if (!t.check(!x4_return_report() && g_ui.card == interruptedUi.card && g_ui.page == interruptedUi.page &&
      g_ui.routeScroll == interruptedUi.routeScroll && g_ui.selectedShip == interruptedUi.selectedShip &&
      g_ui.selectedPort == interruptedUi.selectedPort &&
      !memcmp(&interrupted, &x4_merchant_sail_quote(), sizeof(interrupted)) &&
      X4ReturnUiFixture::same_region(quoteFocus, restoredFocus),
      "Back restores the complete voyage review, page and exact Sail focus")) return false;
  t.capture("06-restored-sail-review");

  // Returning must restore the original refit module and slot. The reduced
  // recap no longer exposes unrelated growth work before acknowledgement.
  if (!t.fresh(2) || !t.dispatch(1, 1, &second) || !t.migrate()) return false;
  g_campaign.deliveredUnits = 48; g_campaign.regionMask = 3; g_campaign.rank = 3;
  for (int p = 0; p < MAX_PORTS; ++p) if (PORT_DEFS[p].region == 1) g_ports[p].unlocked = 1;
  g_ships[0].modules[0] = MODULE_BULKHEADS + 1; g_ships[0].modules[1] = MODULE_STORM_RIG + 1;
  t.frame();
  if (!t.check(t.command(X4_CMD_SHIP_0) && g_ui.card == CARD_SHIP && t.row(2) &&
      g_ui.card == CARD_SHIPYARD && g_ui.page == 1 && input_test_focus_row(MODULE_SWIVEL_GUNS),
      "ship Upgrades offers the original module")) return false;
  t.key(X4_ACT_OK);
  if (!t.check(g_ui.card == CARD_VOYAGE && g_ui.page == 5 && input_test_focus_row(1),
      "original refit selects the second occupied slot")) return false;
  t.key(X4_ACT_OK);
  interruptedUi = g_ui;
  if (!t.check(g_ui.page == 4 && x4_refit_module() == MODULE_SWIVEL_GUNS && x4_refit_slot() == 1,
      "original replacement review has distinct module and slot")) return false;
  t.idle(); sim_advance_minutes(second.minutes); idleCash = g_campaign.coins;
  t.key(X4_ACT_OK);
  if (!t.check(x4_return_report() && g_campaign.coins == idleCash &&
      !input_test_focus_command(X4_CMD_YARD) && input_test_focus_command(X4_CMD_LEDGER),
      "returning press cannot refit; recap exposes only its next action and Updates")) return false;
  t.key(X4_ACT_BACK);
  if (!t.check(!x4_return_report() && g_ui.card == interruptedUi.card && g_ui.page == interruptedUi.page &&
      g_ui.selectedShip == interruptedUi.selectedShip && x4_refit_module() == MODULE_SWIVEL_GUNS &&
      x4_refit_slot() == 1 && g_campaign.coins == idleCash &&
      g_ships[0].modules[1] == MODULE_STORM_RIG + 1,
      "recap Back restores the original refit review without buying either module")) return false;
  t.capture("06c-restored-refit-review");

  // Freeze only the first settlement, then allow a second real voyage to
  // arrive while the report is visible. Acknowledgement must use its snapshot.
  if (!t.fresh(2) || !t.dispatch(0, 1, &first)) return false;
  uint32_t advance = first.minutes / 2;
  sim_advance_minutes(advance);
  if (!t.dispatch(1, 1, &second) || !t.migrate()) return false;
  t.idle(); sim_advance_minutes(first.minutes - advance);
  x4_prepare_return(true); t.frame();
  report = x4_return_report();
  if (!t.check(report && report->bookedArrivals == 1 && g_ships[1].booking.state == BOOKING_SAILING,
      "snapshot starts with one settled voyage and a second still sailing")) return false;
  int64_t frozenCoins = report->bookedCoins;
  uint32_t frozenFingerprint = report->fingerprint;
  sim_advance_minutes(second.minutes); t.frame();
  report = x4_return_report();
  if (!t.check(report && report->bookedArrivals == 1 && report->bookedCoins == frozenCoins &&
      report->fingerprint == frozenFingerprint && g_ships[1].booking.state == BOOKING_SETTLED,
      "later arrival cannot mutate the report currently being read")) return false;
  lastSimulation = millis();
  t.key(X4_ACT_POWER); t.key(X4_ACT_POWER);
  report = x4_return_report();
  if (!t.check(!board_x4_asleep() && report && report->fingerprint == frozenFingerprint &&
      report->bookedArrivals == 1 && report->bookedCoins == frozenCoins,
      "Power off and user wake keep an already-open snapshot frozen")) return false;
  t.key(X4_ACT_BACK);
  ReturnReport newer = sim_return_report();
  if (!t.check(!x4_return_report() && newer.bookedArrivals == 1 &&
      newer.bookedCoins == second.arrivalCoins && newer.bookedProfit == second.profit &&
      save_write() && save_read() && sim_return_report().bookedArrivals == 1,
      "snapshot acknowledgement leaves the later settlement unseen across reload")) return false;
  x4_prepare_return(true); t.frame(); t.capture("07-next-unseen-settlement");
  t.key(X4_ACT_BACK);

  // POWER keeps its configured sleep meaning instead of opening a recap. Losing
  // the retained standby clock cannot fabricate any time or arrival on wake.
  if (!t.fresh() || !t.dispatch(0, 1, &first) || !t.migrate()) return false;
  t.idle();
  // The live clock is normally reset by loop(); this synchronous test does not
  // run loop, so align it before the genuine POWER handler settles awake time.
  lastSimulation = millis();
  uint32_t beforeOffWorld = g_campaign.worldMinutes;
  int32_t beforeOffCash = g_campaign.coins;
  t.key(X4_ACT_POWER);
  if (!t.check(board_x4_asleep() && !x4_return_report() &&
      (SH_X4_VOYAGE_STANDBY ? g_campaign.standbySessionId != 0 : g_campaign.standbySessionId == 0),
      "idle POWER preserves configured standby/shutdown rather than opening a recap")) return false;
  board_x4_test_standby_lose_clock();
  t.key(X4_ACT_POWER);
  if (!t.check(!board_x4_asleep() && !x4_return_report() && g_campaign.worldMinutes == beforeOffWorld &&
      g_campaign.coins == beforeOffCash && g_ships[0].booking.state == BOOKING_SAILING,
      "true-off wake preserves saved progress without fake travel or an empty report")) return false;

  t.idle(); sim_advance_minutes(first.minutes);
  beforeOffCash = g_campaign.coins; beforeOffWorld = g_campaign.worldMinutes;
  lastSimulation = millis();
  t.key(X4_ACT_POWER);
  if (!t.check(board_x4_asleep() && !x4_return_report() && sim_return_has_updates(),
      "POWER shuts down even when unseen arrivals would intercept an ordinary button")) return false;
  t.key(X4_ACT_POWER);
  if (!t.check(!board_x4_asleep() && x4_return_report() && x4_return_report()->bookedArrivals == 1 &&
      g_campaign.coins == beforeOffCash && g_campaign.worldMinutes == beforeOffWorld,
      "user wake displays already-banked unseen news without replaying its payment")) return false;
  t.key(X4_ACT_BACK);

  // Presentation-only stress state: the finite ledger and long vessel names
  // are fabricated deliberately here, never used as economic correctness proof.
  if (!t.fresh(MAX_SHIPS) || !t.migrate()) return false;
  for (int i = 0; i < MAX_SHIPS; ++i) {
    Ship& ship = g_ships[i];
    snprintf(ship.name, sizeof(ship.name), "Indomitable %03d", i + 1);
    ship.returnLedger.total = {};
    ship.returnLedger.seen = {};
    ship.returnLedger.total.revision = 1000;
    ship.returnLedger.total.bookedArrivals = 1000;
    ship.returnLedger.total.bookedCargo = 4000;
    ship.returnLedger.total.bookedCoins = 2000000000ll;
    ship.returnLedger.total.bookedProfit = 1000000000ll;
  }
  x4_prepare_return(true); t.frame();
  report = x4_return_report();
  if (!t.check(report && report->rowCount == MAX_SHIPS && report->bookedArrivals == 12000 &&
      report->bookedCoins == 24000000000ll && report->bookedProfit == 12000000000ll,
      "presentation stress report preserves 64-bit aggregate totals")) return false;
  t.capture("08-large-company-update-presentation-only");
  t.key(X4_ACT_PAGE_NEXT);
  if (!t.check(x4_return_report() && g_ui.scene == SCENE_COMPANY && g_ui.page == 0,
      "rocker cannot abandon or page the summary")) return false;
  if (!t.check(t.command(X4_CMD_PAGE_NEXT) && g_ui.page == 1 && x4_return_row_ship(0) == 3 &&
      input_test_focus_command(X4_CMD_SHIP_0),
      "summary NEXT shows the following three ships of the frozen report")) return false;
  t.capture("08b-company-second-page-presentation-only");
  if (!t.check(t.command(X4_CMD_PAGE_PREV) && g_ui.page == 0 && x4_return_row_ship(2) == 2,
      "summary PREVIOUS returns across the page boundary in the same priority order")) return false;
  if (!t.check(t.command(X4_CMD_LEDGER) && g_ui.card == CARD_RETURN_DETAILS,
      "large company exposes optional detail pagination")) return false;
  uint16_t seenShips = 0;
  for (int page = 0; page < 4; ++page) {
    if (!t.check(g_ui.page == page && x4_page_keys_active(), "top rocker reaches each three-ship update page")) return false;
    for (int row = 0; row < 3; ++row) {
      int ship = x4_return_row_ship(row);
      if (!t.check(ship >= 0 && ship < MAX_SHIPS && !(seenShips & (1u << ship)),
          "detail pages include each vessel once without overlap or omission")) return false;
      seenShips |= (uint16_t)(1u << ship);
    }
    char name[96]; snprintf(name, sizeof(name), "09-large-details-page-%d-presentation-only", page + 1);
    t.capture(name);
    t.key(X4_ACT_PAGE_NEXT);
  }
  if (!t.check(g_ui.page == 0 && seenShips == (1u << MAX_SHIPS) - 1u,
      "final page wraps predictably after all twelve long-named ships")) return false;
  t.key(X4_ACT_BACK);
  if (!t.check(x4_return_report() && save_write() && save_read() && !x4_return_report(),
      "same-seed coherent reload invalidates the previous controller snapshot identity")) return false;
  x4_prepare_return(true); t.frame();
  if (!t.check(x4_return_report() && x4_return_report()->companyEpoch == sim_return_epoch() &&
      x4_return_report()->bookedCoins == 24000000000ll,
      "reload can create a valid fresh snapshot from the persisted unseen ledger")) return false;
  sim_new_campaign(42);
  if (!t.check(!x4_return_report(), "same-seed new company cannot expose the old return report")) return false;
  x4_prepare_return(false); t.frame();
  if (!t.check(!x4_return_report() && !sim_return_has_updates() && g_campaign.coins == 500,
      "new company starts with its own funds and no stale or empty recap")) return false;

  // Explicit presentation fixtures cover recovery/legacy states that the
  // economy intentionally does not manufacture through RNG.
  if (!t.fresh(3) || !t.migrate()) return false;
  g_ui.returnQuiet = 1;
  g_ships[0].state = SHIP_AWAITING_DECISION;
  g_incident = {1, 0, INCIDENT_STORM, RISK_EVEN};
  g_ships[1].state = SHIP_DISABLED; g_ships[1].condition = 20;
  g_ships[2].returnLedger.total.revision = 1;
  g_ships[2].returnLedger.total.legacyArrivals = 1;
  g_ships[2].returnLedger.total.legacyCargo = 2;
  g_ships[2].returnLedger.total.legacyCoins = 376;
  attention_add(ATTENTION_INCIDENT, 0, 0, true);
  attention_add(ATTENTION_DISABLED, 1, 0, true);
  x4_prepare_return(true); t.frame(); report = x4_return_report();
  if (!t.check(report && report->decisionCount == 1 && report->repairCount == 1 &&
      report->legacyArrivals == 1 && report->legacyCoins == 376 && report->bookedCoins == 0 &&
      g_ui.selectedShip == 0,
      "mixed-state presentation distinguishes legacy banked cash and prioritizes a waiting decision")) return false;
  t.capture("11-mixed-decision-repair-legacy-presentation-only");
  if (!t.check(t.command(X4_CMD_LEDGER) && g_ui.card == CARD_RETURN_DETAILS,
      "mixed-state optional rows remain reachable")) return false;
  t.capture("12-mixed-details-presentation-only");
  t.key(X4_ACT_BACK); t.key(X4_ACT_BACK);

  if (!t.fresh() || !t.migrate()) return false;
  attention_add(ATTENTION_ARRIVAL, 0, 0, false);
  x4_prepare_return(true); t.frame(); report = x4_return_report();
  if (!t.check(report && report->legacyNoticeCount == 1 && !report->bookedArrivals &&
      !report->legacyArrivals && !report->bookedCoins && !report->legacyCoins,
      "old arrival notice carries no fabricated financial amount")) return false;
  t.capture("13-unknown-legacy-arrival-presentation-only");
  t.key(X4_ACT_BACK);

  // Leave a genuine, unseen company for the separate process invoked by the
  // Makefile target. That process must enter the report through normal setup(),
  // not through a test's direct call to x4_prepare_return(). The sidecar is test
  // evidence only; the game save reader does not consult it.
  if (!t.fresh(2) || !t.dispatch(0, 1, &first) || !t.dispatch(1, 1, &second) || !t.migrate()) return false;
  t.idle(); sim_advance_minutes(max(first.minutes, second.minutes));
  if (!t.check(save_write(), "persist genuine unseen voyages for a separate process boot")) return false;
  char expectedPath[1024]; snprintf(expectedPath, sizeof(expectedPath), "%s/return-ui-boot-expected.txt", fs);
  FILE* expected = ::fopen(expectedPath, "w");
  if (!t.check(expected != nullptr, "isolated boot evidence sidecar can be created")) return false;
  int written = ::fprintf(expected, "%u %u %ld %lld %lld %u\n", g_campaign.seed, g_campaign.worldMinutes,
    (long)g_campaign.coins, (long long)first.arrivalCoins + second.arrivalCoins,
    (long long)first.profit + second.profit, (unsigned)(first.quantity + second.quantity));
  int closed = ::fclose(expected);
  if (!t.check(written > 0 && closed == 0, "boot expected totals are preserved independently of the report model")) return false;

  board_x4_test_buffered_input(false);
  ::printf("return UI: PASS (%d checks; migrated signed voyages, queued controls, boot and idle recap, exact money, draft restoration, frozen ack, save failure, true off)\n", t.checks);
  return true;
}

static bool run_x4_return_boot_ui_test() {
  X4ReturnUiFixture t;
  const char* fs = getenv("SH_FS_DIR");
  if (!t.check(fs && fs[0] && g_bootResumed, "normal setup resumes the prior process's actual save")) return false;
  char path[1024]; snprintf(path, sizeof(path), "%s/return-ui-boot-expected.txt", fs);
  FILE* expected = ::fopen(path, "r");
  if (!t.check(expected != nullptr, "separate boot has independently recorded expected settlement totals")) return false;
  unsigned seed = 0, world = 0, cargo = 0;
  long cash = 0; long long banked = 0, profit = 0;
  int parsed = ::fscanf(expected, "%u %u %ld %lld %lld %u", &seed, &world, &cash, &banked, &profit, &cargo);
  ::fclose(expected);
  const ReturnReport* report = x4_return_report();
  if (!t.check(parsed == 6 && seed == g_campaign.seed && world == g_campaign.worldMinutes &&
      cash == g_campaign.coins && report && report->meaningful && report->bookedArrivals == 2 &&
      report->bookedCoins == banked && report->bookedProfit == profit && report->deliveredUnits == cargo &&
      g_ui.title && g_ui.scene == SCENE_COMPANY && g_ui.card == CARD_NONE,
      "normal setup keeps the title in front of exactly one persisted Company update without advancing time or cash")) return false;
  t.capture("10-actual-process-boot");
  board_x4_test_buffered_input(true);
  t.key(X4_ACT_OK);
  if (!t.check(!g_ui.title && x4_return_report(), "OK on the title opens the update")) return false;
  t.key(X4_ACT_BACK);
  if (!t.check(!x4_return_report() && !sim_return_has_updates() && g_campaign.coins == cash,
      "one real Back after process boot acknowledges the group without replaying payment")) return false;
  board_x4_test_buffered_input(false);
  ::printf("return boot UI: PASS (%d checks; separate process, shipping setup and one grouped persisted recap)\n", t.checks);
  return true;
}

// A cold boot keeps the title in front
// of a waiting Company update, and OK on the title shows that update.
static bool run_x4_return_title_boot_test() {
  X4ReturnUiFixture t;
  const ReturnReport* report = x4_return_report();
  if (!t.check(g_bootResumed && g_ui.title && report && report->meaningful && g_ui.scene == SCENE_COMPANY,
      "a cold start keeps the title in front of the waiting update")) return false;
  t.capture("20-title-before-update");
  board_x4_test_buffered_input(true);
  t.key(X4_ACT_OK);
  bool shown = t.check(!g_ui.title && x4_return_report() && g_ui.scene == SCENE_COMPANY && g_ui.card == CARD_NONE,
      "OK on the title opens the waiting update");
  board_x4_test_buffered_input(false);
  if (!shown) return false;
  t.capture("21-update-after-title");
  ::printf("return title boot UI: PASS (%d checks; title first, then the Company update)\n", t.checks);
  return true;
}
