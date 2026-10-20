#pragma once

static bool run_x4_startup_input_test() {
  int checks = 0; uint32_t sampler = 0;
  auto check = [&](bool ok, const char* label) {
    ++checks; ::printf("startup input: %s %s\n", ok ? "PASS" : "FAIL", label);
    ::fflush(stdout); return ok;
  };
  auto sample = [&](uint8_t mask, int duration = 100) {
    for (int i = 0; i < duration; i += 10, sampler += 10)
      board_x4_test_sample_buttons(sampler, mask);
  };
  // A new company starts with a gift aboard; nothing bought means the hold
  // is still exactly that.
  int opening = 0;
  auto fresh = [&]() {
    save_reset(); sim_new_campaign(42); g_bootResumed = false;
    // The First Voyage tour would open after the title; the merchant UI test
    // covers it, and this one is about the buffered presses around startup.
    g_campaign.tutorialDismissed = 1;
    opening = sim_ship_used_capacity(g_ships[0]);
    board_x4_test_buffered_input(false); board_x4_test_buffered_input(true);
    input_update(); render_frame();
  };
  auto untouched = [&]() { return sim_ship_used_capacity(g_ships[0]) == opening; };
  constexpr uint8_t ok = 1u << X4_BTN_CONFIRM;
  fresh();
  uint32_t before = x4_press_count(); int32_t coins = g_campaign.coins;
  sample(ok, 120); // title is visible; its slow first frame is still finishing
  x4_input_ready();
  if (!check(!g_ui.title && g_ui.scene == SCENE_FLEET && g_ui.card == CARD_NONE &&
        x4_press_count() == before + 1 && g_campaign.coins == coins && untouched(),
        "OK during the title refresh begins once without opening a purchase")) return false;
  sample(ok, 1500); input_update(); render_frame(); input_update();
  if (!check(g_ui.card == CARD_NONE && x4_press_count() == before + 1 &&
        g_campaign.coins == coins && untouched(),
        "holding that OK through the Fleet refresh cannot leak into its first action")) return false;
  sample(0); input_update(); sample(ok); input_update(); render_frame();
  if (!check(g_ui.card == CARD_SHIP && x4_press_count() == before + 2 &&
        g_campaign.coins == coins && untouched(),
        "a released and freshly pressed OK opens the ready boat normally")) return false;

  fresh(); before = x4_press_count();
  for (int i = 0; i < 3; ++i) { sample(ok); sample(0); }
  x4_input_ready(); input_update(); render_frame(); input_update();
  if (!check(!g_ui.title && g_ui.card == CARD_NONE && x4_press_count() == before + 1 &&
        g_campaign.coins == 500 && untouched(),
        "several impatient startup taps coalesce into a single start")) return false;

  fresh(); before = x4_press_count();
  sample(1u << X4_BTN_LEFT); sample(0); x4_input_ready(); input_update();
  if (!check(g_ui.title && x4_press_count() == before,
        "a navigation press during startup does not dismiss the title")) return false;
  sample(ok); input_update(); render_frame(); input_update();
  if (!check(!g_ui.title && g_ui.card == CARD_NONE && x4_press_count() == before + 1,
        "first fresh OK after ready still starts normally")) return false;

  fresh(); g_ui.title = 0; input_update(); render_frame(); before = x4_press_count();
  sample(0); sample(ok); x4_input_ready(); input_update();
  if (!check(g_ui.card == CARD_NONE && x4_press_count() == before && g_campaign.coins == 500,
        "startup on a resumed non-title screen still absorbs OK without buying or acknowledging")) return false;
  board_x4_test_buffered_input(false);
  ::printf("startup input: PASS (%d checks; early OK, held/repeated OK, fresh navigation, non-title safety)\n", checks);
  return true;
}
