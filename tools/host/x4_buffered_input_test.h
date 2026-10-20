// Integration coverage for the device's buffered x4_input_update branch.
// Only the physical sampler clock is synthetic: actions, selection, trades,
// save calls and rendered frames all run through the normal game code.
#pragma once

static bool run_x4_buffered_input_test() {
  historical_campaign(42, 1);
  g_campaign.stageSeen = 1; g_campaign.legacyFeatureMask = 0xffff;
  g_campaign.tutorialFlags = (1u << TUTORIAL_MARKET) - 1;
  g_campaign.tutorialStep = TUTORIAL_MARKET;
  g_campaign.tutorialDismissed = 0;
  g_campaign.coins = 10000;
  g_ui.title = 0;
  board_x4_test_buffered_input(true);
  uint32_t clock = 0;
  auto sample = [&](uint8_t mask, unsigned duration) {
    for (unsigned elapsed = 0; elapsed < duration; elapsed += 10) {
      board_x4_test_sample_buttons(clock, mask);
      clock += 10;
    }
  };
  auto tap = [&](uint8_t action) {
    int button = x4_button_for(action);
    sample((uint8_t)(1u << button), 70);
    sample(0, 60);
  };
  int checks = 0;
  auto check = [&](bool ok, const char* label) {
    ++checks;
    if (!ok) ::printf("buffered input integration: FAIL %s (check %d)\n", label, checks);
    return ok;
  };
  auto frame = [&]() { input_update(); render_frame(); };
  auto openMarket = [&]() {
    if (g_ui.scene == SCENE_FLEET && g_ui.card == CARD_NONE) {
      if (!input_test_focus_command(X4_CMD_COMPANY)) return false;
      tap(X4_ACT_OK); frame(); input_update();
    }
    if (!input_test_focus_command(X4_CMD_LEDGER)) return false;
    tap(X4_ACT_OK); frame(); input_update();
    if (g_ui.card != CARD_MORE) return false;
    tap(X4_ACT_DOWN); frame(); tap(X4_ACT_DOWN); frame();
    tap(X4_ACT_OK); frame(); input_update();
    return g_ui.card == CARD_MARKET;
  };

  // Startup can accumulate a key while the first frame is being prepared.
  // Ready must discard it without dispatch, and establish the context before
  // the very first fresh tap. Do not prime input_update before that tap: doing
  // so would hide the original first-poll discard regression.
  HitRegion startupFocus{}, readyFocus{}, downFocus{}, restoredFocus{};
  if (!check(x4_focus_region(&startupFocus), "startup has an initial focus")) return false;
  uint32_t startupPresses = x4_press_count();
  int32_t startupCoins = g_campaign.coins;
  sample((uint8_t)(1u << x4_button_for(X4_ACT_DOWN)), 70);
  render_frame();
  x4_input_ready();
  if (!check(x4_press_count() == startupPresses && x4_focus_region(&readyFocus) &&
      readyFocus.x == startupFocus.x && readyFocus.y == startupFocus.y &&
      g_ui.card == CARD_NONE && g_campaign.coins == startupCoins && g_ships[0].cargoCount == 0,
      "ready discards startup input without running an action")) return false;
  sample(0, 60);
  tap(X4_ACT_DOWN); frame();
  if (!check(x4_press_count() == startupPresses + 1 && x4_focus_region(&downFocus) &&
      (downFocus.x != startupFocus.x || downFocus.y != startupFocus.y) && g_ui.card == CARD_NONE,
      "first fresh tap after ready navigates exactly once")) return false;
  input_update(); input_update();
  if (!check(x4_press_count() == startupPresses + 1,
      "first fresh tap is not replayed after release")) return false;
  tap(X4_ACT_UP); frame();
  if (!check(x4_press_count() == startupPresses + 2 && x4_focus_region(&restoredFocus) &&
      restoredFocus.x == startupFocus.x && restoredFocus.y == startupFocus.y &&
      g_campaign.coins == startupCoins && g_ships[0].cargoCount == 0,
      "UP restores startup focus without spending or loading")) return false;

  // Open MARKET with a real queued OK after normal startup navigation.
  if (!check(openMarket(), "market available through Details")) return false;
  input_update(); // opening a new card invalidates old-screen queue contents
  if (!check(g_ui.card == CARD_MARKET && g_ui.selectedGood == GOOD_GRAIN, "open market")) return false;
  uint32_t before = x4_press_count();
  int32_t initialCoins = g_campaign.coins;

  // Ten complete short taps occur during a two-second display blackout.
  // Reach Ironware, open its BUY control, make the company's first trade,
  // return to Ironware, bulk buy/sell, and navigate away and back.
  uint32_t blackoutStart = clock;
  for (int i = 0; i < 3; ++i) tap(X4_ACT_DOWN);
  tap(X4_ACT_OK); tap(X4_ACT_OK); tap(X4_ACT_UP);
  tap(X4_ACT_PAGE_NEXT); tap(X4_ACT_PAGE_PREV); tap(X4_ACT_DOWN); tap(X4_ACT_UP);
  sample(0, 2000 - (clock - blackoutStart));
  if (!check(x4_press_count() == before && g_ships[0].cargoCount == 0, "blackout cannot run game actions")) return false;
  int32_t boughtCoins = 0;
  for (int i = 0; i < 10; ++i) {
    frame();
    if (!check(x4_press_count() == before + (uint32_t)i + 1, "one queued action per frame")) return false;
    if (i < 3 && !check(g_ui.selectedGood == i + 1, "goods navigable after blackout")) return false;
    if (i == 3 && !check(g_ui.selectedGood == 3 && g_campaign.coins == initialCoins,
        "selecting good focuses trade without buying")) return false;
    if (i == 4 && !check(g_ui.selectedGood == 3 && g_ships[0].cargoCount == 1 &&
        g_ships[0].cargo[0].good == 3 && g_ships[0].cargo[0].quantity == 1 &&
        g_campaign.tutorialStep >= TUTORIAL_MARKET,
        "first trade preserves selected good through guidance changes")) return false;
    if (i == 5) {
      HitRegion row = x4_card_row_region(CARD_MARKET, 0, 3);
      HitRegion actual{};
      if (!check(g_ui.selectedGood == 3 && x4_focus_region(&actual) &&
          actual.x == row.x && actual.y == row.y, "UP from BUY returns to same good")) return false;
    }
    if (i == 6) {
      boughtCoins = g_campaign.coins;
      if (!check(g_ships[0].cargoCount == 1 && g_ships[0].cargo[0].good == 3 &&
          g_ships[0].cargo[0].quantity == sim_ship_capacity(g_ships[0]) && boughtCoins < initialCoins,
          "buffered rocker buys selected good")) return false;
    }
    if (i == 7 && !check(g_ships[0].cargoCount == 0 && g_campaign.coins > boughtCoins,
        "buffered rocker sells selected good")) return false;
  }
  if (!check(g_ui.selectedGood == 3, "ten queued taps return to Ironware")) return false;
  tap(X4_ACT_OK); frame();
  HitRegion focus{};
  if (!check(x4_focus_region(&focus) && focus.x == CARD_FOOTER[0].x && focus.y == CARD_FOOTER[0].y,
      "OK on good focuses BUY ONE")) return false;
  tap(X4_ACT_OK); frame();
  if (!check(g_ships[0].cargoCount == 1 && g_ships[0].cargo[0].good == 3 &&
      g_ships[0].cargo[0].quantity == 1, "buffered OK buys exactly one")) return false;
  before = x4_press_count();
  for (int i = 0; i < 5; ++i) input_update();
  if (!check(x4_press_count() == before && g_ships[0].cargo[0].quantity == 1, "release causes no repeats")) return false;

  // A held OK commits once even as sampling continues through more redraws.
  sample((uint8_t)(1u << x4_button_for(X4_ACT_OK)), 1000); frame();
  sample((uint8_t)(1u << x4_button_for(X4_ACT_OK)), 1000); input_update();
  if (!check(x4_press_count() == before + 1 && g_ships[0].cargo[0].quantity == 2, "long hold buys once")) return false;
  sample(0, 60);

  // Closing a card invalidates subsequent old-screen keys; no hidden trade
  // or accidental new-screen activation may escape the transition barrier.
  before = x4_press_count();
  tap(X4_ACT_BACK); tap(X4_ACT_DOWN); tap(X4_ACT_OK);
  frame(); input_update();
  if (!check(g_ui.card == CARD_NONE && x4_press_count() == before + 1 &&
      g_ships[0].cargo[0].quantity == 2, "card transition discards queued actions")) return false;
  if (!check(openMarket(), "restore market through Details")) return false;
  if (!check(g_ui.card == CARD_MARKET, "fresh press works after discard")) return false;

  // An uncommitted key started just before the boundary must not become a
  // delayed action after discard. POWER debounce is covered by the pure test.
  before = x4_press_count();
  sample((uint8_t)(1u << x4_button_for(X4_ACT_DOWN)), 10);
  g_ui.page = 1; // asynchronous page change fixture, detected by input_update
  input_update();
  sample((uint8_t)(1u << x4_button_for(X4_ACT_DOWN)), 100);
  input_update();
  if (!check(x4_press_count() == before, "page transition suppresses incipient old key")) return false;
  sample(0, 60);

  // A disabled selected ship must not prevent another healthy ship in the
  // same harbour from trading, nor may the purchase target the broken ship.
  historical_campaign(43, 1);
  g_campaign.stageSeen = 3; g_campaign.legacyFeatureMask = 0xffff; g_campaign.tutorialDismissed = 1;
  g_campaign.coins = 10000; g_campaign.shipCount = 2;
  g_ships[1] = g_ships[0]; g_ships[1].id = g_ships[0].id + 1;
  g_ships[0].state = SHIP_DISABLED;
  g_ui.title = 0; g_ui.selectedShip = 0;
  input_update();
  if (!check(openMarket(), "market available with disabled selection through Details")) return false;
  if (!check(g_ui.card == CARD_MARKET && g_ui.selectedShip == 1,
      "opening market chooses healthy docked ship")) return false;
  tap(X4_ACT_OK); frame(); tap(X4_ACT_OK); frame();
  if (!check(g_ships[0].cargoCount == 0 && g_ships[1].cargoCount == 1 &&
      g_ships[1].cargo[0].quantity == 1, "purchase reaches healthy ship only")) return false;
  g_ships[1].state = SHIP_DISABLED;
  int32_t blockedCoins = g_campaign.coins;
  tap(X4_ACT_OK); frame();
  if (!check(sim_selected_docked_ship() < 0 && g_ui.noticeCode == SIM_NO_SHIP_HERE &&
      g_campaign.coins == blockedCoins && g_ships[1].cargo[0].quantity == 1,
      "no healthy docked ship gives visible refusal without trade")) return false;

  // EVENT's advertised MARKET action must land on its actual commodity and
  // affected port; its MAP action must land on that region instead.
  g_ships[1].state = SHIP_DOCKED;
  g_campaign.worldEventType = EVENT_SHORTAGE;
  g_campaign.worldEventRegion = PORT_DEFS[0].region;
  g_campaign.worldEventGood = GOOD_MEDICINES;
  g_ui.selectedPort = 1; g_ui.selectedShip = 0;
  g_ui.card = CARD_EVENT;
  input_test_touch(CARD_FOOTER[0].x + 20, CARD_FOOTER[0].y + 10);
  render_frame();
  if (!check(g_ui.card == CARD_MARKET && g_ui.scene == SCENE_PORT &&
      g_ui.selectedPort == 0 && g_ui.selectedGood == GOOD_MEDICINES && g_ui.selectedShip == 1,
      "event MARKET opens correct port, good and healthy ship")) return false;
  g_campaign.worldEventType = EVENT_PRIVATEERS;
  g_ui.card = CARD_EVENT; g_ui.selectedPort = 1;
  input_test_touch(CARD_FOOTER[0].x + 20, CARD_FOOTER[0].y + 10);
  if (!check(g_ui.card == CARD_NONE && g_ui.scene == SCENE_WORLD && g_ui.selectedPort == 0,
      "nontrade event MAP opens affected region")) return false;
  g_campaign.worldEventType = EVENT_GLUT;
  g_campaign.worldEventRegion = 1;
  for (int p = 0; p < MAX_PORTS; ++p)
    if (PORT_DEFS[p].region == 1) g_ports[p].unlocked = 0;
  g_ui.card = CARD_EVENT;
  input_test_touch(CARD_FOOTER[0].x + 20, CARD_FOOTER[0].y + 10);
  if (!check(g_ui.card == CARD_EVENT && g_ui.noticeCode == SIM_PORT_LOCKED && g_ui.selectedPort == 0,
      "unavailable event region gives refusal without wrong-port market")) return false;
  board_x4_test_buffered_input(false);
  ::printf("buffered input integration: PASS (%d checks; ten 70ms taps over 2000ms blackout; real navigation/buy/sell/hold/context)\n", checks);
  return true;
}
