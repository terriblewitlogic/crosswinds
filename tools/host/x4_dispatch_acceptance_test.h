#pragma once

// Independent acceptance of fleet scale: twelve merchant ships, route pages
// and several journeys under Watch. Ship purchase and the setup voyages use
// production transactions; navigation passes through the physical button queue.
static bool run_x4_dispatch_acceptance_test() {
  int checks = 0; uint32_t sampler = 0;
  auto check = [&](bool ok, const char* label) {
    ++checks;
    if (!ok) ::printf("dispatch acceptance: FAIL %s (card %u, page %u, ship %u)\n",
      label, g_ui.card, g_ui.page, g_ui.selectedShip);
    return ok;
  };
  auto key = [&](uint8_t action) {
    input_update();
    int button = x4_button_for(action);
    for (int i = 0; i < 7; ++i, sampler += 10)
      board_x4_test_sample_buttons(sampler, (uint8_t)(1u << button));
    for (int i = 0; i < 6; ++i, sampler += 10) board_x4_test_sample_buttons(sampler, 0);
    input_update(); render_frame(); input_update();
  };
  auto capture = [&](const char* name) {
    const char* dir = getenv("SH_X4_DISPATCH_DIR"); if (!dir) return;
    ::mkdir(dir, 0755);
    char path[1024]; snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    render_frame(); dump_ppm_native(path);
  };
  auto command = [&](uint8_t cmd) {
    // Transactions no longer contain hidden main-navigation targets.
    for (int back = 0; voyage_dispatch_card(g_ui.card, g_ui.page) && back < 4; ++back) key(X4_ACT_BACK);
    if (!input_test_focus_command(cmd)) return false;
    key(X4_ACT_OK); return true;
  };
  auto footer = [&](int id) {
    HitRegion target = voyage_footer_region(g_ui.card, id);
    for (int attempt = 0; attempt < 24; ++attempt) {
      HitRegion focus{};
      if (x4_focus_region(&focus) && focus.x == target.x && focus.y == target.y && focus.w == target.w) {
        key(X4_ACT_OK); return true;
      }
      key(X4_ACT_DOWN);
    }
    return false;
  };
  auto row = [&](int n) { if (!input_test_focus_row(n)) return false; key(X4_ACT_OK); return true; };
  // A merchant company (the economy every X4 company runs) grown to a full
  // fleet. Ship purchase uses the production transaction.
  save_reset(); sim_new_campaign(42); g_ui.title = 0; g_campaign.tutorialDismissed = 1;
  g_campaign.coins = 100000; g_campaign.deliveredUnits = 48;
  g_campaign.rank = 5; g_ports[0].shipyardLevel = 3;
  g_ports[3].unlocked = 1;
  while (g_campaign.shipCount < MAX_SHIPS)
    if (!check(sim_buy_ship(0), "purchase another usable ship")) return false;
  g_ui.selectedShip = 0; g_ui.scene = SCENE_FLEET; g_ui.card = CARD_NONE; g_ui.page = 0;
  board_x4_test_buffered_input(true); input_update(); render_frame();
  // The rocker changes the main tab; Fleet pages with its own NEXT control.
  for (int page = 0; page < 4; ++page) {
    if (!check(g_ui.scene == SCENE_FLEET && g_ui.page == page &&
               x4_fleet_ship(0) == page * 3 && x4_fleet_ship(2) == page * 3 + 2,
               "all twelve ships reachable at three per page")) return false;
    if (page == 0) capture("12-ships-first-page");
    if (page == 3) capture("12-ships-last-page");
    if (!command(X4_CMD_PAGE_NEXT)) return false;
  }
  if (!check(g_ui.page == 0, "fleet pager wraps without dropping a ship")) return false;
  if (!check(command(X4_CMD_PAGE_PREV) && g_ui.page == 3 && x4_fleet_ship(2) == MAX_SHIPS - 1,
      "fleet pager steps back to the last page")) return false;
  if (!command(X4_CMD_PAGE_NEXT)) return false;
  if (!check(voyage_fleet_row_region(0).w == 752 &&
      voyage_scene_region(X4_CMD_FLEET, 0).y == 48 &&
      voyage_scene_region(X4_CMD_MAP, 0).y == 48 &&
      voyage_scene_region(X4_CMD_COMPANY, 0).y == 48,
      "main places use top tabs and full-width fleet rows")) return false;
  if (!check(command(X4_CMD_SHIP_0) && g_ui.card == CARD_SHIP && g_ui.selectedShip == 0,
      "ready ship opens its overview")) return false;
  if (!check(row(1) && g_ui.card == CARD_VOYAGE && g_ui.page == 0, "Sail opens route choices")) return false;
  uint8_t hidden[COLUMN_MAX];
  if (!check(x4_column(hidden) == 0 && !input_test_focus_command(X4_CMD_COMPANY),
      "transaction exposes no hidden main tabs to physical focus")) return false;
  int routeControls = min((int)x4_voyage_destination_count(), VOYAGE_DESTINATION_ROWS) +
    (x4_voyage_destination_count() > VOYAGE_DESTINATION_ROWS ? 2 : 0) + 1;
  for (int press = 0; press < routeControls; ++press) {
    HitRegion focus{}; bool visible = false;
    if (!x4_focus_region(&focus)) return false;
    auto matches = [&](const HitRegion& r) { return focus.x == r.x && focus.y == r.y && focus.w == r.w && focus.h == r.h; };
    for (int row = 0; row < min((int)x4_voyage_destination_count(), VOYAGE_DESTINATION_ROWS); ++row)
      visible |= matches(voyage_card_row_region(CARD_VOYAGE, 0, row));
    for (int id = 0; id < 3; ++id)
      if (id == 2 || x4_voyage_destination_count() > VOYAGE_DESTINATION_ROWS)
        visible |= matches(voyage_footer_region(CARD_VOYAGE, id));
    if (!check(visible, "each route focus lies on a visible row, pager, or Back")) return false;
    key(X4_ACT_DOWN);
  }
  int32_t unchangedCash = g_campaign.coins;
  HitRegion topBack = voyage_footer_region(CARD_VOYAGE, 2);
  input_test_touch(topBack.x + topBack.w / 2, topBack.y + topBack.h / 2);
  input_update(); render_frame(); input_update();
  if (!check(g_ui.card == CARD_SHIP && g_ui.selectedShip == 0 && g_campaign.coins == unchangedCash,
      "visible top Back returns to the ship and spends nothing")) return false;
  key(X4_ACT_BACK);
  if (!check(g_ui.card == CARD_NONE && g_ui.scene == SCENE_FLEET && g_campaign.coins == unchangedCash,
      "Back from the ship returns to Fleet")) return false;
  input_test_touch(700, 8); input_test_touch(400, TICKER_Y + 2);
  if (!check(g_ui.card == CARD_NONE && g_campaign.coins == unchangedCash,
      "removed topbar and ticker contain no invisible click actions")) return false;
  if (!command(X4_CMD_SHIP_0) || !row(1)) return false;
  bool found = false;
  for (int page = 0; page < MAX_PORTS && !found; ++page) {
    for (int row = 0; row < VOYAGE_DESTINATION_ROWS; ++row) if (x4_voyage_destination(row) == 3) {
      if (!input_test_focus_row(row)) return false;
      key(X4_ACT_OK); found = true; break;
    }
    if (!found) key(X4_ACT_PAGE_NEXT);
  }
  if (!check(found && g_ui.card == CARD_VOYAGE && g_ui.page == 2 &&
      x4_merchant_sail_quote().code == SIM_OK && x4_merchant_sail_quote().destination == 3,
      "Mariner Rest reachable beyond first route page")) return false;
  capture("far-route-review");
  int32_t before = g_campaign.coins;
  if (!check(footer(1) && voyage_watch_screen() && g_ships[0].state == SHIP_SAILING &&
      g_campaign.coins == before, "free Sail departs on Watch without spending")) return false;
  capture("far-route-watch");
  if (!check(command(X4_CMD_WATCH_READY) && g_ui.selectedShip == 1 && g_ui.card == CARD_SHIP,
      "next ready chooses another actual ship")) return false;
  if (!check(command(X4_CMD_FLEET) && command(X4_CMD_SHIP_0) &&
      voyage_watch_screen() && g_ui.selectedShip == 0,
      "fleet opens the sailing ship directly in Watch")) return false;
  key(X4_ACT_BACK);
  if (!check(g_ui.scene == SCENE_FLEET && g_ui.card == CARD_NONE && g_ui.selectedShip == 0,
      "Watch Back restores Fleet and ship")) return false;
  uint8_t ports[MAX_PORTS];
  uint8_t routes = sim_merchant_destinations(2, ports, MAX_PORTS);
  MerchantSailQuote coastal = sim_merchant_sail_offer(1, 1);
  if (!check(coastal.code == SIM_OK && sim_merchant_sail(1, coastal), "first other ship sails")) return false;
  MerchantSailQuote northern = routes > 1 ? sim_merchant_sail_offer(2, ports[1]) : MerchantSailQuote{};
  if (!check(northern.code == SIM_OK && sim_merchant_sail(2, northern),
      "two other ships sail independent routes")) return false;
  g_ui.card = CARD_NONE; g_ui.scene = SCENE_FLEET; g_ui.page = 0; input_update(); render_frame();
  if (!check(command(X4_CMD_SHIP_1) && g_ui.selectedShip == 1 && voyage_watch_screen(),
      "Watch opens the chosen sailing ship")) return false;
  if (!check(command(X4_CMD_WATCH_NEXT) && g_ui.selectedShip == 2 && voyage_watch_screen(),
      "Next ship reaches the other journey")) return false;
  capture("two-journeys-watch");
  if (!check(command(X4_CMD_WATCH_MAP) && !x4_watch_active() && g_ui.scene == SCENE_WORLD &&
      g_ui.mapZoom == 1 && g_ui.selectedShip == 2,
      "World overview keeps chosen boat identity")) return false;
  capture("two-journeys-world");
  key(X4_ACT_BACK);
  if (!check(voyage_watch_screen() && g_ui.selectedShip == 2,
      "Back from the World overview returns to that boat's Watch")) return false;
  board_x4_test_buffered_input(false);
  ::printf("dispatch acceptance: PASS (%d checks; twelve ships, route pages, free departure, Watch, several journeys)\n", checks);
  return true;
}
