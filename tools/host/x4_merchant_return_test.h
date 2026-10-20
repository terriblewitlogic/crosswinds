#pragma once

// Actual queued front buttons and renderer. Only elapsed simulation time and
// the last-interaction timestamp are advanced by the fixture.
struct X4MerchantReturnFixture {
  uint32_t sampler = 0;
  int checks = 0;
  bool check(bool ok, const char* label) {
    ++checks;
    ::printf("merchant return: %s %s (check %d; ship %u port %u card %u cash %ld)\n",
      ok ? "PASS" : "FAIL", label, checks, g_ui.selectedShip, g_ui.selectedPort,
      g_ui.card, (long)g_campaign.coins);
    ::fflush(stdout); return ok;
  }
  void frame() { input_update(); render_frame(); input_update(); }
  void key(uint8_t action) {
    input_update();
    int button = x4_button_for(action);
    for (int i = 0; i < 7; ++i, sampler += 10)
      board_x4_test_sample_buttons(sampler, (uint8_t)(1u << button));
    for (int i = 0; i < 6; ++i, sampler += 10) board_x4_test_sample_buttons(sampler, 0);
    frame();
  }
  bool command(uint8_t command) {
    if (!input_test_focus_command(command)) return false;
    key(X4_ACT_OK); return true;
  }
  static bool same_region(const HitRegion& a, const HitRegion& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.id == b.id;
  }
  bool focus_footer(int id) {
    HitRegion wanted = voyage_footer_region(g_ui.card, id);
    for (int attempts = 0; attempts < 20; ++attempts) {
      HitRegion actual{};
      if (x4_focus_region(&actual) && same_region(actual, wanted)) return true;
      key(X4_ACT_DOWN);
    }
    return false;
  }
  static int owned(uint8_t ship, uint8_t good = GOOD_TEXTILES) {
    int count = 0;
    for (uint8_t slot = 0; slot < g_ships[ship].cargoCount; ++slot) {
      const CargoItem& cargo = g_ships[ship].cargo[slot];
      if (cargo.kind == CARGO_MARKET && cargo.good == good) count += cargo.quantity;
    }
    return count;
  }
  void capture(const char* name) {
    const char* directory = getenv("SH_X4_MERCHANT_RETURN_DIR");
    if (!directory || !directory[0]) return;
    ::mkdir(directory, 0755);
    render_request_full_refresh(); render_frame(); input_update();
    char path[1024]; snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    dump_ppm_native(path);
  }
};

static bool run_x4_merchant_return_test() {
  X4MerchantReturnFixture t;
  const char* fs = getenv("SH_FS_DIR");
  if (!t.check(fs && fs[0], "reset is confined to an explicit isolated host filesystem")) return false;
  board_x4_test_buffered_input(true);
  if (board_x4_asleep()) t.key(X4_ACT_POWER);
  save_reset(); sim_new_campaign(42);
  // This fixture needs an empty first ship for an uncommitted purchase draft.
  g_ships[0].cargoCount = 0; memset(g_ships[0].cargo, 0, sizeof(g_ships[0].cargo));
  g_campaign.coins = 5000; g_campaign.deliveredUnits = 24;
  g_campaign.tutorialDismissed = 1; g_campaign.legacyFeatureMask = 0xffff;
  if (!t.check(g_campaign.economyVersion == 2 && sim_buy_ship(0) &&
        g_campaign.shipCount == 2 && sim_ship_captain(0) && sim_ship_captain(1),
        "fixture purchases a real second staffed merchant ship")) return false;
  if (!t.check(sim_ack_return_report(sim_return_report()), "initial company milestones are already read")) return false;
  x4_prepare_return(false); // clears an obsolete controller session after the new company
  g_ui.title = 0; g_ui.card = CARD_NONE; g_ui.scene = SCENE_FLEET; g_ui.page = 0;
  g_ui.selectedShip = 0; g_ui.selectedPort = 0;
  input_test_set_last_interaction_ms(millis()); board_x4_discard_button_events(); t.frame();

  uint8_t goods[GOOD_COUNT]{}; goods[GOOD_TEXTILES] = 3;
  auto purchase = sim_merchant_trade_offer(1, true, goods);
  if (!t.check(purchase.code == SIM_OK && sim_merchant_trade_apply(1, purchase),
        "boat one purchases three real owned goods")) return false;
  PackageOffer package{};
  for (uint8_t row = 0; row < sim_package_count(1); ++row) {
    auto candidate = sim_package_offer(1, row);
    if (candidate.destination == 1) { package = candidate; break; }
  }
  if (!t.check(package.id && sim_package_accept(1, package) && sim_ship_used_capacity(g_ships[1]) == 4,
        "boat one accepts a free one-slot package beside its merchandise")) return false;
  auto voyage = sim_merchant_sail_offer(1, 1);
  if (!t.check(voyage.code == SIM_OK && sim_merchant_sail(1, voyage),
        "boat one sails toward Gullhaven with goods and package")) return false;
  // The setup actions changed selection like a genuine departure. Return to
  // Fleet before starting boat zero's entirely uncommitted UI draft.
  g_ui.card = CARD_NONE; g_ui.scene = SCENE_FLEET; g_ui.page = 0;
  input_test_set_last_interaction_ms(millis()); board_x4_discard_button_events(); t.frame();
  if (!t.check(t.command(X4_CMD_SHIP_0) && g_ui.card == CARD_SHIP &&
        g_ui.selectedShip == 0 && g_ui.selectedPort == 0 && x4_merchant_mode() == 0,
        "queued Fleet selection opens boat zero Buy at Lantern Quay")) return false;
  if (!input_test_focus_row(0)) return false; t.key(X4_ACT_OK);
  int textiles = -1, ironware = -1;
  for (int row = 0; row < 2; ++row) {
    if (x4_merchant_good(row) == GOOD_TEXTILES) textiles = row;
    if (x4_merchant_good(row) == GOOD_IRONWARE) ironware = row;
  }
  if (!t.check(textiles >= 0 && ironware >= 0 && input_test_focus_row(textiles),
        "boat zero exposes both local produced goods as quantity controls")) return false;
  if (!t.check(input_test_focus_row(ironware), "focus can move to another good without purchasing")) return false;
  input_test_focus_row(textiles); t.key(X4_ACT_OK);
  while (x4_merchant_trade_quote().quantity > 3) t.key(X4_ACT_DOWN);
  if (!t.check(t.focus_footer(1), "the explicit Buy confirmation has physical focus")) return false;
  MerchantTradeQuote draft = x4_merchant_trade_quote();
  uint8_t draftAmounts[GOOD_COUNT]; memcpy(draftAmounts, x4_merchant_quantities(), GOOD_COUNT);
  UiState draftUi = g_ui; HitRegion draftFocus{}; x4_focus_region(&draftFocus);
  int32_t beforeArrival = g_campaign.coins;
  if (!t.check(draft.code == SIM_OK && draft.quantity == 3 &&
        draftAmounts[GOOD_TEXTILES] == 3 && draftAmounts[GOOD_IRONWARE] == 0 && !g_ships[0].cargoCount,
        "single-item Buy draft has exact quantities and has spent nothing")) return false;
  t.capture("12-two-ship-uncommitted-buy");

  input_test_set_last_interaction_ms(millis() - (X4_RETURN_IDLE_MS + 1000u));
  sim_advance_minutes(voyage.minutes);
  int32_t arrivalCash = beforeArrival + package.reward;
  if (!t.check(g_ships[1].merchant.state == MERCHANT_ARRIVED && g_ships[1].currentPort == 1 &&
        X4MerchantReturnFixture::owned(1) == 3 && g_campaign.coins == arrivalCash &&
        g_ui.card == CARD_TRADE && g_ui.selectedShip == 0 && !g_ships[0].cargoCount,
        "idle arrival pays only boat one's package and leaves boat zero's visible draft alone")) return false;
  t.key(X4_ACT_OK);
  const ReturnReport* report = x4_return_report();
  if (!t.check(report && g_ui.scene == SCENE_COMPANY && g_ui.card == CARD_NONE &&
        report->legacyArrivals == 1 && report->legacyCoins == package.reward &&
        !report->bookedArrivals && !report->bookedCoins && !report->bookedProfit &&
        report->deliveredUnits == 1 && g_campaign.coins == arrivalCash && !g_ships[0].cargoCount,
        "first idle OK opens exact package-only recap and cannot commit the focused Buy")) return false;
  t.capture("13-merchant-company-update");
  if (!t.check(t.command(X4_CMD_LEDGER) && g_ui.card == CARD_RETURN_DETAILS && x4_return_report(),
        "optional Updates details keep the captured report and interrupted draft")) return false;
  t.capture("14-merchant-update-details");
  t.key(X4_ACT_BACK);
  if (!t.check(g_ui.card == CARD_NONE && x4_return_report(), "Back from Updates returns to the same recap")) return false;
  if (!t.check(t.command(X4_CMD_JOBS), "Continue is an explicit return to the interrupted screen")) return false;
  HitRegion restoredFocus{}; x4_focus_region(&restoredFocus);
  if (!t.check(!x4_return_report() && g_ui.card == draftUi.card && g_ui.page == draftUi.page &&
        g_ui.selectedShip == draftUi.selectedShip && g_ui.selectedPort == draftUi.selectedPort &&
        g_ui.selectedGood == draftUi.selectedGood && x4_merchant_mode() == 0 &&
        !memcmp(draftAmounts, x4_merchant_quantities(), GOOD_COUNT) &&
        draft.total == x4_merchant_trade_quote().total && draft.cashDelta == x4_merchant_trade_quote().cashDelta &&
        x4_merchant_trade_quote().fingerprint == sim_merchant_trade_offer(0, true, draftAmounts).fingerprint &&
        X4MerchantReturnFixture::same_region(draftFocus, restoredFocus) &&
        g_campaign.coins == arrivalCash && !g_ships[0].cargoCount,
        "Continue restores quantities and Buy focus, with an executable quote reflecting parcel cash")) return false;
  t.capture("15-restored-merchant-buy");
  if (!t.check(g_ships[1].returnLedger.seen.legacyArrivals == g_ships[1].returnLedger.total.legacyArrivals &&
        g_ships[1].returnLedger.seen.legacyCoins == g_ships[1].returnLedger.total.legacyCoins &&
        !sim_return_report().legacyArrivals, "recap acknowledges only the actual frozen package arrival")) return false;
  t.key(X4_ACT_BACK); t.key(X4_ACT_BACK); t.key(X4_ACT_BACK);
  if (!t.check(g_ui.card == CARD_NONE && g_ui.scene == SCENE_FLEET &&
        g_campaign.coins == arrivalCash && !g_ships[0].cargoCount,
        "canceling restored Buy returns to Fleet without spending")) return false;
  if (!t.check(t.command(X4_CMD_SHIP_1) && g_ui.card == CARD_SHIP &&
        g_ui.selectedShip == 1 && g_ui.selectedPort == 1 && g_ships[0].currentPort == 0 &&
        g_ships[1].currentPort == 1 && X4MerchantReturnFixture::owned(1) == 3,
        "arrived boat opens its neutral overview at Gullhaven while the other boat remains at Lantern Quay")) return false;
  t.capture("16-arrived-boat-sell-at-belmar");
  if (!t.check(input_test_focus_row(0), "arrived boat's actual owned merchandise is focusable")) return false;
  t.key(X4_ACT_OK); t.key(X4_ACT_PAGE_NEXT);
  input_test_focus_row(0); t.key(X4_ACT_OK);
  auto sale = x4_merchant_trade_quote();
  if (!t.check(sale.code == SIM_OK && sale.port == 1 && sale.shipIndex == 1 && sale.quantity == 3 &&
        g_campaign.coins == arrivalCash && X4MerchantReturnFixture::owned(1) == 3,
        "editing Sell uses boat one's real destination market without crediting early")) return false;
  t.key(X4_ACT_BACK); t.key(X4_ACT_BACK); t.key(X4_ACT_BACK);
  if (!t.check(g_ui.card == CARD_NONE && g_campaign.coins == arrivalCash &&
        X4MerchantReturnFixture::owned(1) == 3 && !g_ships[0].cargoCount,
        "canceling Sell preserves both ships' cargo and company cash")) return false;
  if (!t.check(t.command(X4_CMD_SHIP_0) && g_ui.card == CARD_SHIP && x4_merchant_mode() == 0 &&
        g_ui.selectedShip == 0 && g_ui.selectedPort == 0 && !x4_merchant_trade_quote().quantity,
        "returning to boat zero opens a clean Lantern Quay Buy draft")) return false;
  g_ui.returnQuiet = 1;
  auto home = sim_merchant_sail_offer(1, 0);
  if (!t.check(sim_merchant_sail(1, home), "second return fixture sails home")) return false;
  g_ships[1].incidentArmed = 0; // this fixture isolates the return controller
  sim_advance_minutes(home.minutes); x4_prepare_return(false); t.frame();
  if (!t.check(x4_return_report() && x4_return_row_ship(0) == 1, "newly arrived ship is the first actionable return row")) return false;
  if (!t.check(x4_column_slot(X4_CMD_FLEET) < 0 && x4_column_slot(X4_CMD_MAP) < 0 &&
      x4_column_slot(X4_CMD_COMPANY) < 0 && x4_column_slot(X4_CMD_JOBS) >= 0,
      "Welcome back offers ship actions and Continue instead of another navigation hub")) return false;
  t.key(X4_ACT_PAGE_NEXT);
  if (!t.check(x4_return_report() && g_ui.scene == SCENE_COMPANY,
      "rocker does not abandon the return summary")) return false;
  t.capture("19-welcome-back-row");
  if (!t.check(t.command(X4_CMD_SHIP_0) && !x4_return_report() && g_ui.card == CARD_SHIP &&
      g_ui.selectedShip == 1 && g_ui.selectedPort == 0 && x4_merchant_mode() == 0,
      "return row opens that ship without assuming a sale")) return false;
  t.capture("20-welcome-back-direct-sell");
  board_x4_test_buffered_input(false);
  ::printf("merchant return: PASS (%d checks; queued buttons, two ships, recap and exact draft restoration)\n", t.checks);
  return true;
}
