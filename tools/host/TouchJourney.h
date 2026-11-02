#pragma once

// Included by the game repository's SDL host after its shared test helpers.
void crosswinds_touch_tap(int x, int y);
void crosswinds_touch_swipe(int startX, int startY, int endX, int endY);

static bool run_crosswinds_touch_test() {
  int checks = 0;
  auto check = [&](bool ok, const char* text) {
    ++checks;
    printf("touch: %s %s (step %u card %u scene %u)\n", ok ? "PASS" : "FAIL", text, sim_onboarding_step(), g_ui.card,
           g_ui.scene);
    return ok;
  };
  auto frame = [] {
    input_update();
    render_frame();
  };
  auto tap = [&](HitRegion r) {
    crosswinds_touch_tap(r.x + r.w / 2, r.y + r.h / 2);
    frame();
  };
  auto focusTap = [&] {
    HitRegion r{};
    if (!x4_focus_region(&r)) return false;
    tap(r);
    return true;
  };
  auto owned = [](uint8_t good) {
    unsigned n = 0;
    for (int c = 0; c < g_ships[0].cargoCount; ++c)
      if (g_ships[0].cargo[c].kind == CARGO_MARKET && g_ships[0].cargo[c].good == good)
        n += g_ships[0].cargo[c].quantity;
    return n;
  };
  input_test_goal_moments(true);
  save_reset();
  sim_new_campaign(91);
  frame();
  crosswinds_touch_tap(-1, 200);
  if (!check(g_ui.title, "out-of-bounds tap ignored")) return false;
  crosswinds_touch_tap(200, 440);
  frame();
  if (!check(x4_onboarding_screen(), "title tap opens captain selection")) return false;
  tap(x4_onboarding_card_region(1));
  if (!check(sim_onboarding_step() == ONBOARD_NAME && sim_ship_captain(0)->trait == TRAIT_MERCHANT,
             "tap hires the selected captain"))
    return false;
  const auto oldName = x4_onboarding_name_index();
  tap(x4_onboarding_button_region(0));
  if (!check(x4_onboarding_name_index() != oldName, "Another name changes the name")) return false;
  tap(x4_onboarding_button_region(2));
  if (!check(sim_onboarding_step() == ONBOARD_CAPTAIN, "Choose again returns to captains")) return false;
  tap(x4_onboarding_card_region(1));
  tap(x4_onboarding_button_region(1));
  if (!check(sim_onboarding_step() == ONBOARD_BUY && g_ui.card == CARD_SHIP, "Sail with Captain opens the ship"))
    return false;
  tap(merchant_ship_region(0));
  tap(merchant_row_region(0));
  if (!check(x4_merchant_editing(), "tap goods opens quantity editor")) return false;
  auto quantity = x4_merchant_trade_quote().quantity;
  tap(merchant_quantity_region(-1));
  if (!check(x4_merchant_trade_quote().quantity == quantity - 1, "minus reduces quantity")) return false;
  tap(merchant_quantity_region(1));
  if (!check(x4_merchant_trade_quote().quantity == quantity, "plus restores quantity")) return false;
  const auto coins = g_campaign.coins;
  crosswinds_touch_swipe(500, 410, 500, 210);
  frame();
  if (!check(g_campaign.coins == coins && !owned(GOOD_TEXTILES), "swipe cannot confirm a purchase")) return false;
  if (const char* path = getenv("SH_TOUCH_SCREENSHOT")) dump_ppm_native(path);
  tap(merchant_footer_region(1));
  if (!check(owned(GOOD_TEXTILES) == quantity && sim_onboarding_step() == ONBOARD_SAIL,
             "BUY commits the reviewed quantity"))
    return false;
  tap(merchant_footer_region(2));
  tap(merchant_ship_region(1));
  if (!check(g_ui.card == CARD_VOYAGE, "SAIL opens destinations")) return false;
  if (!focusTap() || !focusTap()) return false;
  if (!check(g_ships[0].merchant.state == MERCHANT_SAILING, "destination and departure accept taps")) return false;
  g_ships[0].incidentArmed = 0;
  sim_advance_minutes(g_ships[0].merchant.legMinutes[0] + 1);
  frame();
  if (g_ui.card != CARD_SHIP && !focusTap()) return false;
  tap(merchant_ship_region(0));
  tap(merchant_row_region(0));
  if (!check(x4_merchant_editing() && x4_merchant_mode() == 1, "arrival opens touch selling")) return false;
  tap(merchant_footer_region(1));
  if (!check(!owned(GOOD_TEXTILES) && sim_onboarding_step() == ONBOARD_DONE && x4_goal_moment_active(),
             "SELL completes the guided voyage"))
    return false;
  crosswinds_touch_tap(400, 430);
  frame();
  if (!check(!x4_goal_moment_active(), "tap dismisses profit page")) return false;
  const auto savedCoins = g_campaign.coins;
  if (!check(save_write() && save_read() && g_campaign.coins == savedCoins, "touch journey survives a save reload"))
    return false;
  save_reset();
  sim_new_campaign(92);
  frame();
  crosswinds_touch_tap(200, 440);
  frame();
  tap(x4_onboarding_button_region(3));
  if (!check(sim_onboarding_step() == ONBOARD_DONE, "Skip starts a free company")) return false;
  g_campaign.legacyFeatureMask = 0xffff;
  g_campaign.coins = 10000;
  g_ui.title = false;
  g_ui.scene = SCENE_PORT;
  g_ui.card = CARD_SHIPYARD;
  g_ui.page = 2;
  g_ui.selectedPort = g_ships[0].currentPort;
  g_ui.noticeCode = SIM_OK;
  g_ports[g_ui.selectedPort].warehouseLevel = 0;
  frame();
  const auto pushes = render_host_push_count();
  tap(CARD_ROWS[1]);
  if (!check(g_ports[g_ui.selectedPort].warehouseLevel == 1 && render_host_push_count() > pushes,
             "warehouse purchase immediately repaints the new level and price"))
    return false;
  printf("touch: PASS (%d checks)\n", checks);
  return true;
}
