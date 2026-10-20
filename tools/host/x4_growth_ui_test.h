#pragma once

// Company growth on the merchant economy every X4 company runs: protected
// harbor work for a stranded company, an occupied-slot refit from the range
// goal, an explicit project donation and a crewed ship package. Setup voyages
// use production transactions; every screen action is a queued button press.
static bool run_x4_growth_ui_test() {
  save_reset(); sim_new_campaign(52); g_ui.title = 0; g_campaign.tutorialDismissed = 1;
  // Stranded: the only ship is wrecked, its hold is empty and no coins remain.
  g_ships[0].cargoCount = 0; memset(g_ships[0].cargo, 0, sizeof(g_ships[0].cargo));
  g_campaign.coins = 0; g_ships[0].state = SHIP_DISABLED;
  g_ships[0].condition = 20; g_ships[0].crew = 1; g_ui.scene = SCENE_FLEET; g_ui.card = CARD_NONE;
  board_x4_test_buffered_input(true);
  uint32_t sampler = 0; int checks = 0;
  auto key = [&](uint8_t action) {
    input_update();
    int button = x4_button_for(action);
    for (int i = 0; i < 7; ++i, sampler += 10)
      board_x4_test_sample_buttons(sampler, (uint8_t)(1u << button));
    for (int i = 0; i < 6; ++i, sampler += 10) board_x4_test_sample_buttons(sampler, 0);
    input_update(); render_frame(); input_update();
  };
  auto check = [&](bool ok, const char* label) {
    ++checks;
    if (!ok) ::printf("growth UI: FAIL %s (check %d; card %u page %u view %u coins %ld)\n", label,
      checks, g_ui.card, g_ui.page, g_ui.captainView, (long)g_campaign.coins);
    return ok;
  };
  auto command = [&](uint8_t cmd) {
    if (!input_test_focus_command(cmd)) return false;
    key(X4_ACT_OK); return true;
  };
  auto row = [&](int n) { if (!input_test_focus_row(n)) return false; key(X4_ACT_OK); return true; };
  auto footer = [&](int id) {
    for (int i = 0; i < X4_CARD_ROW_MAX + 6; ++i) {
      HitRegion have{}, want = voyage_footer_region(g_ui.card, id);
      if (x4_focus_region(&have) && have.x == want.x && have.y == want.y && have.w == want.w) {
        key(X4_ACT_OK); return true;
      }
      key(X4_ACT_DOWN);
    }
    return false;
  };
  auto tool = [&](uint8_t card) {
    for (int i = 0; i < voyage_detail_count(); ++i)
      if (VOYAGE_DETAIL_CARDS[voyage_detail_index(i)] == card) {
        g_ui.page = (uint8_t)(i / CARD_VISIBLE_ROWS); input_update(); render_frame();
        return row(i % CARD_VISIBLE_ROWS);
      }
    return false;
  };
  auto capture = [&](const char* name) {
    const char* dir = getenv("SH_X4_GROWTH_DIR"); if (!dir) return;
    render_request_full_refresh(); render_frame(); input_update();
    ::mkdir(dir, 0755); char path[1024]; snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    dump_ppm_native(path);
  };
  input_update(); render_frame();
  Ship broken = g_ships[0];
  if (!check(sim_recovery_available() && command(X4_CMD_SHIP_0) && g_ui.card == CARD_SHIPYARD && g_ui.page == 2,
      "a wrecked ship opens repairs and harbor work")) return false;
  capture("11-stranded-yard");
  if (!check(row(4) && g_ui.card == CARD_VOYAGE && g_ui.page == 3,
      "Harbor Work is offered to the stranded company")) return false;
  VoyageQuote relief = x4_voyage_quote();
  if (!check(relief.mode == VOYAGE_RELIEF && relief.payNow == 0 && relief.arrivalCoins >= 80 &&
      !memcmp(&broken, &g_ships[0], sizeof(Ship)) && g_campaign.coins == 0,
      "relief review gives actual time and pay without repairs or advance")) return false;
  capture("12-harbor-work");
  key(X4_ACT_BACK);
  if (!check(!memcmp(&broken, &g_ships[0], sizeof(Ship)) && g_campaign.coins == 0,
      "Back cancels protected work cleanly")) return false;
  if (g_ui.card != CARD_SHIPYARD && !command(X4_CMD_SHIP_0)) return false;
  if (!row(4)) return false;
  key(X4_ACT_OK);
  if (!check(g_ships[0].booking.state == BOOKING_SAILING && g_ships[0].booking.mode == VOYAGE_RELIEF &&
      g_ships[0].condition >= 50 && g_campaign.coins == 0 && g_campaign.recoveryDebt == 0,
      "confirm relief restores only its working ship and dispatches without loan")) return false;
  sim_advance_minutes(relief.minutes); render_frame(); input_update();
  if (!check(g_campaign.coins == relief.arrivalCoins && !sim_recovery_available(),
      "paid work returns the company to free merchant sailing")) return false;

  save_reset(); sim_new_campaign(53); g_ui.title = 0; g_campaign.tutorialDismissed = 1;
  g_campaign.deliveredUnits = 48; g_campaign.companyXp = 48; g_campaign.rank = 3;
  g_campaign.stageSeen = 3; g_campaign.legacyFeatureMask = 0xffff; g_campaign.coins = 10000;
  if (!sim_buy_ship(0)) return false;
  g_campaign.regionMask = g_campaign.chartMask = 3;
  for (int p = 0; p < 8; ++p) g_ports[p].unlocked = 1;
  g_ui.selectedShip = 0; g_ui.selectedPort = 0; g_ui.scene = SCENE_COMPANY; g_ui.card = CARD_NONE;
  g_ships[0].modules[0] = MODULE_COPPER + 1;
  g_ships[0].modules[1] = MODULE_STORM_RIG + 1;
  input_update(); render_frame();
  int32_t before = g_campaign.coins;
  if (!check(voyage_needs_range() && command(X4_CMD_YARD) && g_ui.card == CARD_PROGRESS,
      "next goal explains a ship able to reach farther ports")) return false;
  capture("13-range-goal");
  if (!check(footer(1) && g_ui.card == CARD_PORT_SHIPS && g_ui.captainView == 3,
      "range goal asks which sloop to refit")) return false;
  if (!check(row(0) && g_ui.selectedShip == 0 && g_ui.card == CARD_VOYAGE && g_ui.page == 5,
      "Refit Sloop asks which occupied fitting to replace")) return false;
  capture("14-fitting-slots");
  key(X4_ACT_OK);
  if (!check(g_ui.page == 4 && x4_refit_slot() == 0 && g_ships[0].modules[0] == MODULE_COPPER + 1 &&
      g_campaign.coins == before, "replacement review names old/new fitting without spending")) return false;
  key(X4_ACT_BACK); key(X4_ACT_DOWN); key(X4_ACT_OK);
  if (!check(x4_refit_slot() == 1, "player can choose the other occupied slot")) return false;
  capture("15-refit-review");
  key(X4_ACT_OK);
  if (!check(g_ships[0].modules[0] == MODULE_COPPER + 1 && g_ships[0].modules[1] == MODULE_LONG_CASKS + 1 &&
      g_campaign.coins == before - 420 && sim_ship_capacity(g_ships[0]) == 3 && !voyage_needs_range(),
      "confirmed refit charges 420, preserves chosen fitting and trades one cargo space for range")) return false;
  capture("15b-after-refit");

  // A dock project takes imported materials only when the player donates them.
  uint8_t region = sim_project_region();
  uint8_t dock = region < REGION_COUNT ? sim_project_port(region) : NO_PORT;
  g_ships[0].cargoCount = 0; memset(g_ships[0].cargo, 0, sizeof(g_ships[0].cargo));
  uint8_t ironware[GOOD_COUNT]{}; ironware[GOOD_IRONWARE] = 2;
  MerchantTradeQuote materials = sim_merchant_trade_offer(0, true, ironware);
  if (!check(dock < MAX_PORTS && dock != g_ships[0].currentPort && materials.code == SIM_OK &&
      sim_merchant_trade_apply(0, materials), "fixture buys two Ironware away from the dock")) return false;
  MerchantSailQuote toDock = sim_merchant_sail_offer(0, dock);
  if (!check(toDock.code == SIM_OK && sim_merchant_sail(0, toDock), "fixture sails the materials to the dock")) return false;
  g_ships[0].incidentArmed = 0; g_ui.returnQuiet = 1;
  sim_advance_minutes(toDock.minutes);
  uint16_t ironBefore = g_campaign.projectCargo[region][1];
  g_ui.card = CARD_NONE; g_ui.scene = SCENE_COMPANY; g_ui.selectedShip = 0; g_ui.returnQuiet = 0;
  input_update(); render_frame();
  if (!check(g_ships[0].currentPort == dock && g_campaign.projectCargo[region][1] == ironBefore &&
      command(X4_CMD_YARD) && g_ui.card == CARD_PROGRESS && x4_merchant_donation_good() == GOOD_IRONWARE,
      "arrival never donates; the goal offers the imported Ironware")) return false;
  capture("16-project-donation");
  int32_t donationCash = g_campaign.coins;
  if (!check(footer(1) && g_campaign.projectCargo[region][1] == ironBefore + 1 &&
      g_campaign.coins == donationCash && g_ui.card == CARD_PROGRESS,
      "explicit donation delivers one unit without sale proceeds")) return false;
  key(X4_ACT_BACK);
  if (g_ui.card != CARD_NONE) key(X4_ACT_BACK);

  if (!check(command(X4_CMD_LEDGER) && g_ui.card == CARD_MORE && tool(CARD_SHIPYARD) &&
      g_ui.card == CARD_PORT_PICKER, "Company shipyard first asks which port")) return false;
  if (!check(row(0) && g_ui.card == CARD_SHIPYARD && g_ui.selectedPort == 0,
      "shipyard remains reachable in Details")) return false;
  capture("19-crewed-ship-packages");
  before = g_campaign.coins; uint8_t ships = g_campaign.shipCount;
  key(X4_ACT_DOWN); key(X4_ACT_OK);
  if (!check(g_campaign.shipCount == ships + 1 && g_ships[ships].classId == 1 &&
      g_ships[ships].captainId != NO_SHIP && g_campaign.coins == before - sim_ship_package_cost(1) &&
      g_ui.scene == SCENE_COMPANY && g_ui.card == CARD_NONE,
      "ship package includes its captain without opening recruitment")) return false;
  if (!command(X4_CMD_YARD)) return false;
  g_campaign.projectCargo[2][0] = 8; render_frame();
  capture("20-project-delivering");
  g_campaign.projectCargo[2][0] = g_campaign.projectCargo[2][1] = sim_project_target(2);
  render_frame(); capture("21-project-ready");
  g_campaign.regionMask = g_campaign.chartMask = 63;
  for (int p = 0; p < MAX_PORTS; ++p) g_ports[p].unlocked = 1;
  render_frame(); capture("24-grand-charter-regions");
  g_campaign.visitedMask = 63; render_frame(); capture("25-grand-charter-homecoming");
  g_campaign.won = 1; render_frame(); capture("26-grand-charter-complete");
  if (!check(g_ui.card == CARD_PROGRESS && g_campaign.coins == before - sim_ship_package_cost(1),
      "regional progression and final Charter guidance remain read-only")) return false;
  save_reset(); sim_new_campaign(55); g_ui.title = 0; g_campaign.tutorialDismissed = 1;
  g_campaign.deliveredUnits = 24; g_campaign.coins = 1200;
  g_ui.scene = SCENE_COMPANY; g_ui.card = CARD_NONE; input_update(); render_frame();
  if (!check(command(X4_CMD_YARD) && g_ui.card == CARD_PROGRESS, "second-ship goal opens")) return false;
  capture("27-second-sloop");
  board_x4_test_buffered_input(false);
  ::printf("growth UI: PASS (%d checks; harbor work, occupied-slot refit, project donation, crewed packages)\n", checks);
  return true;
}
