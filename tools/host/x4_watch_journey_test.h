#pragma once

static bool run_x4_watch_journey_test() {
  int checks = 0;
  auto check = [&](bool ok, const char* label) {
    ++checks; ::printf("watch journey: %s %s\n", ok ? "PASS" : "FAIL", label); ::printf(" [ship %u card %u state %u]\n",g_ui.selectedShip,g_ui.card,g_ships[g_ui.selectedShip].state); ::fflush(stdout);
    return ok;
  };
  uint32_t sampler = 0;
  auto key = [&](uint8_t action) {
    input_update(); int button = x4_button_for(action);
    for (int i = 0; i < 7; ++i, sampler += 10) board_x4_test_sample_buttons(sampler, (uint8_t)(1u << button));
    for (int i = 0; i < 6; ++i, sampler += 10) board_x4_test_sample_buttons(sampler, 0);
    input_update(); render_frame(); input_update();
  };
  auto command = [&](uint8_t cmd) {
    if (!input_test_focus_command(cmd)) return false;
    key(X4_ACT_OK); return true;
  };
  auto capture = [&](const char* name) {
    const char* dir = getenv("SH_X4_WATCH_JOURNEY_DIR"); if (!dir) return;
    ::mkdir(dir, 0755); char path[1024]; snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    render_request_full_refresh(); render_frame(); dump_ppm_native(path);
  };
  save_reset(); sim_new_campaign(42); g_ui.title = 0; g_campaign.onboardingStep = ONBOARD_DONE;
  board_x4_test_buffered_input(true); input_update();
  Ship readyShip = g_ships[0];
  MerchantSailQuote quote = sim_merchant_sail_offer(0, 1);
  if (!check(quote.code == SIM_OK && sim_merchant_sail(0, quote), "start an actual merchant voyage")) return false;
  if (!check(command(X4_CMD_SHIP_0) && voyage_watch_screen(), "sailing ship opens dedicated Watch")) return false;
  if (!check(x4_column_slot(X4_CMD_WATCH_MAP) >= 0 && x4_column_slot(X4_CMD_WATCH_READY) >= 0 &&
        x4_column_slot(X4_CMD_ZOOM) < 0, "journey has its own actions, separate from chart controls")) return false;
  capture("01-departure");
  WatchJourney before = watch_journey(g_ships[0]);
  sim_advance_minutes(quote.minutes / 3); input_update(); render_frame();
  WatchJourney after = watch_journey(g_ships[0]);
  int x0, y0, x1, y1; watch_ship_point(before.fraction, &x0, &y0); watch_ship_point(after.fraction, &x1, &y1);
  if (!check(after.remaining < before.remaining && after.fraction > 3000 && after.fraction < 3500 &&
        x1 > x0 && y1 != y0, "elapsed simulation moves the ship and advances the whole-journey bar")) return false;
  capture("02-one-third");
  uint32_t pushes = render_host_push_count();
  sim_advance_minutes(SIM_WORLD_MINUTES_PER_REAL_SECOND); render_frame();
  if (!check(render_host_push_count() == pushes, "automatic journey updates respect e-paper refresh interval")) return false;
  delay(3100); render_tick(); render_frame();
  if (!check(render_host_push_count() == pushes + 1, "deferred countdown and movement produce a new frame")) return false;
  after = watch_journey(g_ships[0]);
  g_campaign.paused = 1; sim_advance_minutes(100); input_update();
  if (!check(watch_journey(g_ships[0]).remaining == after.remaining, "pause freezes countdown and progress")) return false;
  capture("03-paused"); g_campaign.paused = 0;
  if (!check(command(X4_CMD_WATCH_MAP) && !x4_watch_active() && g_ui.scene == SCENE_WORLD &&
        g_ui.mapZoom == 1, "World map opens the full geographical chart")) return false;
  capture("04-world");
  if (!check(command(X4_CMD_ZOOM) && g_ui.mapZoom == 2, "world-map zoom remains available")) return false;
  capture("05-zoom");
  key(X4_ACT_BACK);
  if (!check(voyage_watch_screen(), "Back from the reference map returns to Watch")) return false;
  key(X4_ACT_BACK);
  if (!check(!x4_watch_active() && g_ui.scene == SCENE_FLEET,
        "Watch Back returns to its Fleet parent")) return false;
  command(X4_CMD_FLEET); command(X4_CMD_SHIP_0);
  if (!check(command(X4_CMD_WATCH_READY) && g_ui.scene == SCENE_FLEET && !x4_watch_active(),
        "with no ready ship the action returns to Fleet")) return false;
  g_campaign.shipCount = 3;
  g_ships[1] = readyShip; g_ships[1].id = 2; snprintf(g_ships[1].name, sizeof(g_ships[1].name), "Swift");
  g_ships[2] = g_ships[0]; g_ships[2].id = 3; g_ships[2].merchant.id += 10;
  snprintf(g_ships[2].name, sizeof(g_ships[2].name), "North Star");
  g_ships[2].legElapsedMinutes = g_ships[2].legTotalMinutes * 2 / 3;
  command(X4_CMD_SHIP_0);
  if (!input_test_focus_command(X4_CMD_WATCH_NEXT)) return false;
  key(X4_ACT_OK);
  if (!check(voyage_watch_screen() && g_ui.selectedShip == 2,
        "Next ship switches to another sailing boat and skips docked ships")) return false;
  capture("06-second-ship");
  if (!command(X4_CMD_WATCH_NEXT)) return false;
  if (!check(g_ui.selectedShip == 0 && voyage_watch_screen(), "Next ship wraps back to the first journey")) return false;
  if (!check(command(X4_CMD_WATCH_READY) && g_ui.selectedShip == 1 && g_ui.card == CARD_SHIP && !x4_watch_active(),
        "Next ready ship opens that ship's overview")) return false;
  capture("07-next-ready");
  key(X4_ACT_BACK); command(X4_CMD_SHIP_0);
  sim_advance_minutes(watch_journey(g_ships[0]).remaining); input_update(); render_frame();
  if (!check(g_ships[0].state == SHIP_ARRIVED && voyage_watch_screen(),
        "watched arrival remains on Watch")) return false;
  capture("08a-arrived-watch");
  if (!check(command(X4_CMD_WATCH_READY) && g_ui.card == CARD_SHIP && g_ui.selectedShip == 0,
        "Open ship selects the watched arrival, not another ready boat")) return false;
  capture("08-arrival");
  Ship multi = readyShip; multi.state = SHIP_SAILING;
  multi.merchant.state = MERCHANT_SAILING; multi.merchant.origin = 4; multi.merchant.destination = 9;
  multi.routeCount = 3; multi.routePos = 1; multi.legTotalMinutes = 200; multi.legElapsedMinutes = 50;
  multi.merchant.legMinutes[0] = 100; multi.merchant.legMinutes[1] = 200; multi.merchant.legMinutes[2] = 300;
  WatchJourney leg = watch_journey(multi);
  if (!check(leg.total == 600 && leg.remaining == 450 && leg.fraction == 2500 && leg.origin == 4 && leg.destination == 9,
        "multi-leg journey retains terminal ports, total ETA and cumulative progress")) return false;
  multi.routePos = 2; multi.legTotalMinutes = 300; multi.legElapsedMinutes = 0;
  if (!check(watch_journey(multi).fraction == 5000, "intermediate port does not reset progress")) return false;
  // Resolve an actual merchant encounter through the Watch action and front buttons.
  save_reset(); sim_new_campaign(84); g_ui.title = 0; g_campaign.onboardingStep = ONBOARD_DONE;
  // Exercise unavailable salvage with no cargo to stake. New companies now
  // receive starter Grain, which intentionally makes this choice available.
  g_ships[0].cargoCount = 0; memset(g_ships[0].cargo, 0, sizeof(g_ships[0].cargo));
  auto incidentRoute = sim_merchant_sail_offer(0, 1);
  if (!check(sim_merchant_sail(0, incidentRoute), "incident UI fixture sails")) return false;
  command(X4_CMD_SHIP_0);
  g_campaign.completedVoyages = 1; g_ships[0].incidentArmed = 1; g_ships[0].incidentType = INCIDENT_STORM;
  sim_advance_minutes(incidentRoute.minutes/2); input_update(); render_frame();
  if (!check(g_ships[0].state == SHIP_AWAITING_DECISION && voyage_watch_screen(), "Watch exposes a waiting captain")) return false;
  capture("09-watch-decision");
  if (!check(command(X4_CMD_WATCH_READY) && g_ui.card == CARD_INCIDENT, "Watch opens that ship's captain decision")) return false;
  capture("10-merchant-incident");
  g_campaign.coins = 0; key(X4_ACT_DOWN); key(X4_ACT_OK);
  if (!check(g_ui.card == CARD_INCIDENT && g_ships[0].state == SHIP_AWAITING_DECISION && !g_campaign.coins,
      "unavailable salvage stays on the decision without charging")) return false;
  key(X4_ACT_UP); key(X4_ACT_OK);
  if (!check(g_ui.card == CARD_OUTCOME && g_ships[0].state == SHIP_SAILING && !g_incident.active,
      "safe free choice resumes sailing and opens its report")) return false;
  capture("11-merchant-outcome");
  HitRegion continueButton = CARD_FOOTER[0]; input_test_touch(continueButton.x + 20, continueButton.y + 20);
  if (!check(voyage_watch_screen(), "Watch ship returns to the resolved journey")) return false;
  // New opportunities have two focusable choices; expiry never clicks either.
  g_ships[0].state=SHIP_AWAITING_DECISION;g_ships[0].incidentType=INCIDENT_NIGHT_MARKET;
  g_ships[0].encounterWait=300;g_incident={1,0,INCIDENT_NIGHT_MARKET,RISK_EVEN};
  g_campaign.coins=500;int32_t offerCash=g_campaign.coins;
  if(!check(command(X4_CMD_WATCH_READY) && g_ui.card==CARD_INCIDENT,"two-choice opportunity opens from Watch"))return false;
  HitRegion first{};x4_focus_region(&first);key(X4_ACT_DOWN);key(X4_ACT_DOWN);HitRegion wrapped{};x4_focus_region(&wrapped);
  if(!check(first.y==wrapped.y,"two-choice focus wraps without a hidden third action"))return false;
  capture("12-night-market");key(X4_ACT_BACK);
  if(!check(voyage_watch_screen() && g_incident.active,"Back leaves the offer open without a purchase"))return false;
  command(X4_CMD_WATCH_READY);sim_advance_minutes(300);input_update();render_frame();
  if(!check(g_ui.card!=CARD_INCIDENT && g_campaign.coins==offerCash && g_ships[0].state!=SHIP_AWAITING_DECISION,
      "expiry exits the stale offer without spending coins"))return false;
  board_x4_test_buffered_input(false);
  ::printf("watch journey: PASS (%d checks)\n", checks); return true;
}
