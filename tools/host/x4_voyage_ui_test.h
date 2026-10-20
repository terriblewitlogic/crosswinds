#pragma once

static bool run_x4_details_navigation_test() {
  historical_campaign(42, 1);
  g_campaign.stageSeen = 3; g_campaign.legacyFeatureMask = 0xffff; g_campaign.tutorialDismissed = 1;
  g_campaign.coins = 10000; g_ui.title = 0; g_ui.scene = SCENE_COMPANY;
  board_x4_test_buffered_input(true);
  uint32_t sampler = 0;
  auto key = [&](uint8_t action) {
    input_update();
    int button = x4_button_for(action);
    for (int i = 0; i < 7; ++i, sampler += 10)
      board_x4_test_sample_buttons(sampler, (uint8_t)(1u << button));
    for (int i = 0; i < 6; ++i, sampler += 10) board_x4_test_sample_buttons(sampler, 0);
    input_update(); render_frame(); input_update();
  };
  auto details = [&]() {
    if (!input_test_focus_command(X4_CMD_LEDGER)) return false;
    key(X4_ACT_OK); return g_ui.card == CARD_MORE;
  };
  auto check = [&](bool ok, const char* label) {
    if (!ok) ::printf("details navigation: FAIL %s (card %u page %u)\n", label, g_ui.card, g_ui.page);
    return ok;
  };
  if (!check(details(), "Details opens")) return false;
  key(X4_ACT_PAGE_NEXT); key(X4_ACT_OK);
  if (!check(g_ui.card == CARD_CAPTAINS, "captains reachable through Details page two")) return false;
  key(X4_ACT_OK); key(X4_ACT_BACK);
  if (!check(g_ui.card == CARD_CAPTAINS && g_ui.captainView == 0, "Back returns to captain roster")) return false;
  key(X4_ACT_BACK);
  if (!details()) return false;
  for (int i = 0; i < 4; ++i) key(X4_ACT_DOWN);
  key(X4_ACT_OK);
  if (!check(g_ui.card == CARD_ROUTE, "custom routes reachable through Details")) return false;
  g_ships[0].routeTemplate.buyGood = GOOD_GRAIN;
  g_ships[0].routeTemplate.maxBuyPrice = 30;
  if (!input_test_focus_row(5)) return false;
  key(X4_ACT_OK);
  if (!check(g_ui.page == 1, "route rules open")) return false;
  for (int i = 0; i < 4; ++i) key(X4_ACT_DOWN);
  key(X4_ACT_OK);
  if (!check(g_ships[0].routeTemplate.maxBuyPrice == 30 && g_ui.noticeCode == SIM_USE_ROCKER,
      "OK on rule price cannot change it")) return false;
  key(X4_ACT_PAGE_NEXT);
  if (!check(g_ships[0].routeTemplate.maxBuyPrice == 40, "rocker still changes rule price")) return false;
  board_x4_test_buffered_input(false);
  ::printf("details navigation: PASS (captain Back, custom route rules, price rocker)\n");
  return true;
}
