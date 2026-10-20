#pragma once

// Capture-only fixtures: render the production UI without changing a player's
// save. Core trading/Watch/return sequences are captured by their input tests.
static bool run_x4_screen_atlas(const char* dir) {
  ::mkdir(dir, 0755);
  char path[1024]; snprintf(path, sizeof(path), "%s/frames.jsonl", dir);
  FILE* manifest = fopen(path, "w"); if (!manifest) return false;
  int count = 0;
  auto capture = [&](const char* name) {
    input_test_set_last_interaction_ms(millis());
    g_ui.dirty = 1; render_request_full_refresh(); render_frame();
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name); dump_ppm_native(path);
    fprintf(manifest, "{\"id\":\"%s\",\"scene\":%u,\"card\":%u,\"page\":%u}\n",
      name, g_ui.scene, g_ui.card, g_ui.page); fflush(manifest);
    printf("atlas: %s\n", name); fflush(stdout); ++count;
  };
  auto seed = [&]() {
    sim_new_campaign(42); g_ui.title = 0; g_ui.card = CARD_NONE;
    // Prepared empty/full-hold states; real new-company cargo is captured by the merchant UI test.
    g_ships[0].cargoCount = 0; memset(g_ships[0].cargo, 0, sizeof(g_ships[0].cargo));
    g_campaign.tutorialDismissed = 1; g_campaign.tutorialFlags = 255;
    g_campaign.tutorialStep = TUTORIAL_COUNT;
    render_host_reset_map_presentation();
  };
  auto card = [&](uint8_t kind, uint8_t page, const char* name) {
    g_ui.card = kind; g_ui.page = page; capture(name);
  };
  auto touch = [&](HitRegion r) { input_test_touch(r.x + r.w / 2, r.y + r.h / 2); };
  if(getenv("SH_X4_ATLAS_ART_ONLY")) {
    for(int stage=0;stage<4;++stage) {
      gallery_seed();g_ui.scene=SCENE_COMPANY;g_ui.card=CARD_NONE;
      g_campaign.deliveredUnits=80;
      g_campaign.regionMask=stage==3?63:1;
      memset(g_campaign.projectCargo,0,sizeof(g_campaign.projectCargo));
      uint16_t target=sim_project_target(1);
      if(stage>0)g_campaign.projectCargo[1][0]=stage==1?1:target;
      if(stage>1)g_campaign.projectCargo[1][1]=target;
      char name[40];snprintf(name,sizeof(name),"dock-stage-%d",stage);capture(name);
      if(stage<3){snprintf(name,sizeof(name),"dock-goal-%d",stage);card(CARD_PROGRESS,0,name);}
    }
    for(int port=0;port<MAX_PORTS;++port) {
      gallery_seed();g_ui.scene=SCENE_PORT;g_ui.card=CARD_NONE;g_ui.selectedPort=port;
      char name[40];snprintf(name,sizeof(name),"landmark-%02d",port);capture(name);
    }
    fclose(manifest);return true;
  }
  if (!getenv("SH_X4_ATLAS_INCIDENTS_ONLY")) {
  seed(); g_ui.title = 1; g_bootResumed = false; capture("title-new");
  g_bootResumed = true; capture("title-resume"); g_bootResumed = false;
  render_sleep_screen("Saved. Hold Power to return");
  snprintf(path,sizeof(path),"%s/saved.ppm",dir);dump_ppm_native(path);
  if(getenv("SH_X4_ATLAS_TITLE_ONLY")){fclose(manifest);return true;}
  seed(); g_ui.scene = SCENE_WORLD;
  g_ui.mapZoom = 1; capture("world-full");
  g_ui.mapZoom = 2; capture("world-regional");
  g_ui.mapZoom = 4; capture("world-close");
  seed(); g_ui.scene = SCENE_COMPANY; capture("company-new");
  card(CARD_MORE, 0, "details-new");
  // The Grey Passenger's Keepsakes after three deliveries.
  g_campaign.storyDeliveries = 3; g_campaign.storyDelivered = 0x0B;
  g_campaign.storyOrder[0] = 0; g_campaign.storyOrder[1] = 3; g_campaign.storyOrder[2] = 1;
  card(CARD_KEEPSAKES, 0, "keepsakes");
  seed(); g_ui.scene = SCENE_COMPANY; card(CARD_ATLAS, 0, "sea-atlas");
  seed(); g_ui.scene = SCENE_COMPANY;
  card(CARD_PROGRESS, 0, "goal-first-sales");
  card(CARD_MENU, 0, "settings");
  g_campaign.paused = 1; card(CARD_MENU, 0, "settings-paused");
  g_ui.confirmReset = 1; card(CARD_MENU, 0, "settings-reset");
  seed(); card(CARD_MANIFEST, 0, "cargo-empty");
  g_ports[0].warehouseLevel = 1; card(CARD_MANIFEST, 1, "warehouse-empty");
  card(CARD_INBOX, 0, "notices-empty");
  g_campaign.worldEventType = EVENT_NONE; card(CARD_EVENT, 0, "event-none");
  g_journalCount = 0; card(CARD_JOURNAL, 0, "journal-empty");

  seed(); g_ui.scene = SCENE_FLEET; card(CARD_SHIP, 0, "ship-empty"); g_ui.card = CARD_NONE;
  touch(voyage_scene_region(X4_CMD_SHIP_0, 0));
  if (g_ui.card == CARD_SHIP) touch(merchant_ship_region(0));
  touch(merchant_tab_region(1)); capture("sell-empty");
  touch(merchant_tab_region(0)); g_campaign.coins = 0;
  touch(merchant_adjust_region(0, 1)); capture("buy-no-funds");
  seed(); uint8_t fullHold[GOOD_COUNT]{}; fullHold[GOOD_TEXTILES] = 4;
  auto fullPurchase = sim_merchant_trade_offer(0, true, fullHold);
  if (!sim_merchant_trade_apply(0, fullPurchase)) { fclose(manifest); return false; }
  g_ui.scene = SCENE_FLEET; touch(voyage_scene_region(X4_CMD_SHIP_0, 0));
  if (g_ui.card == CARD_SHIP) touch(merchant_ship_region(0));
  touch(merchant_tab_region(0)); capture("buy-full-hold");
  touch(merchant_tab_region(2)); capture("packages-full-hold");

  gallery_seed(); g_ui.scene = SCENE_COMPANY;
  g_campaign.deliveredUnits = 80; capture("company-expanded");
  card(CARD_MORE, 0, "details-expanded-1"); card(CARD_MORE, 1, "details-expanded-2");
  g_ships[0].cargoCount = 0;
  uint8_t quantities[GOOD_COUNT]{}; quantities[GOOD_TEXTILES] = 2;
  auto purchase = sim_merchant_trade_offer(0, true, quantities);
  if (!sim_merchant_trade_apply(0, purchase)) { fclose(manifest); return false; }
  card(CARD_MANIFEST, 0, "cargo-loaded");
  if (g_ships[0].cargoCount) {
    g_ports[0].warehouse[0] = g_ships[0].cargo[0]; g_ports[0].warehouseCount = 1;
  }
  g_ports[0].warehouseLevel = 1; card(CARD_MANIFEST, 1, "warehouse-loaded");
  card(CARD_LEDGER, 0, "ledger"); card(CARD_JOURNAL, 0, "journal");
  for (int p = 0; p < 3; ++p) { char name[32]; snprintf(name, sizeof(name), "yard-%d", p); card(CARD_SHIPYARD, p, name); }

  gallery_full_company(); g_ui.scene = SCENE_FLEET; g_ui.card = CARD_NONE;
  for (int p = 0; p < 4; ++p) {
    char name[32]; snprintf(name, sizeof(name), "fleet-page-%d", p + 1); capture(name);
    if (p < 3) touch(voyage_scene_region(X4_CMD_PAGE_NEXT, 0));
  }
  for (int p = 0; p < 2; ++p) { char name[32]; snprintf(name, sizeof(name), "ship-list-%d", p + 1); card(CARD_FLEET, p, name); }

  seed(); g_ui.scene = SCENE_COMPANY; g_campaign.deliveredUnits = 12;
  card(CARD_PROGRESS, 0, "goal-second-ship-locked");
  g_campaign.deliveredUnits = 24; g_campaign.coins = 1200;
  card(CARD_PROGRESS, 0, "goal-second-ship-ready");
  g_campaign.coins = 10000; sim_buy_ship(0); g_campaign.deliveredUnits = 36;
  card(CARD_PROGRESS, 0, "goal-next-region");
  g_campaign.deliveredUnits = 48; g_campaign.regionMask = g_campaign.chartMask = 3;
  for (int p = 0; p < 8; ++p) g_ports[p].unlocked = 1;
  g_ui.selectedShip = 0; g_ui.selectedPort = 0;
  g_ships[0].modules[0] = MODULE_COPPER + 1; g_ships[0].modules[1] = MODULE_STORM_RIG + 1;
  card(CARD_PROGRESS, 0, "goal-range");
  touch(CARD_FOOTER[1]);
  if(g_ui.card==CARD_PORT_SHIPS)touch(voyage_card_row_region(CARD_PORT_SHIPS,0,0));
  capture("fitting-slots");
  touch(voyage_card_row_region(CARD_VOYAGE, 5, 0)); capture("fitting-review");
  if (g_ui.card != CARD_VOYAGE || g_ui.page != 4) { fclose(manifest); return false; }
  g_ships[0].modules[0] = MODULE_LONG_CASKS + 1;
  uint8_t region = sim_project_region();
  if (region >= REGION_COUNT) { fclose(manifest); return false; }
  card(CARD_PROGRESS, 0, "project-empty");
  g_campaign.projectCargo[region][0] = 3;
  card(CARD_PROGRESS, 0, "project-partial");
  g_campaign.projectCargo[region][0] = g_campaign.projectCargo[region][1] = sim_project_target(region);
  card(CARD_PROGRESS, 0, "project-ready");
  g_campaign.regionMask = g_campaign.chartMask = 63; g_campaign.visitedMask = 3;
  card(CARD_PROGRESS, 0, "charter-visits");
  g_campaign.visitedMask = 63; card(CARD_PROGRESS, 0, "charter-homecoming");
  g_campaign.won = 1; card(CARD_PROGRESS, 0, "charter-complete");

  // Retained booked-voyage cards are explicitly classified as legacy in HTML.
  seed(); g_campaign.economyVersion = 1; g_ui.scene = SCENE_FLEET;
  touch(voyage_scene_region(X4_CMD_SHIP_0, 0));
  if (g_ui.card == CARD_SHIP) touch(merchant_ship_region(0)); capture("legacy-destinations");
  touch(voyage_card_row_region(CARD_VOYAGE, 0, 0)); capture("legacy-load");
  if (g_ui.card != CARD_VOYAGE_LOAD) { fclose(manifest); return false; }
  touch(voyage_footer_region(CARD_VOYAGE_LOAD, 1)); capture("legacy-review");
  g_ui.page = 1; capture("legacy-cargo-choice");
  seed(); g_campaign.economyVersion = 1; g_campaign.coins = 0;
  g_ships[0].state = SHIP_DISABLED; g_ships[0].condition = 20; g_ships[0].crew = 1;
  g_ui.scene = SCENE_FLEET; touch(voyage_scene_region(X4_CMD_SHIP_0, 0));
  if (g_ui.card == CARD_SHIP) touch(merchant_ship_region(0));
  capture("legacy-harbor-work");
  }
  for (uint8_t kind = 0; kind < INCIDENT_COUNT; ++kind) for(int branch=0;branch<(kind<=INCIDENT_REEF?2:1);++branch) {
    seed(); g_campaign.coins = 1000;
    uint8_t load[GOOD_COUNT]{}; load[GOOD_TEXTILES] = 2;
    auto buy = sim_merchant_trade_offer(0, true, load); sim_merchant_trade_apply(0, buy);
    auto route = sim_merchant_sail_offer(0, 1); sim_merchant_sail(0, route);
    g_campaign.completedVoyages = 1; g_ships[0].state = SHIP_AWAITING_DECISION; g_ships[0].incidentArmed = 0;
    g_ships[0].legElapsedMinutes = g_ships[0].legTotalMinutes / 2;
    g_ships[0].incidentType=kind;g_ships[0].encounterWait=300;
    g_incident = {1, 0, kind, RISK_EVEN};
    if(kind<=INCIDENT_REEF) {
      uint8_t risk=sim_merchant_incident_choice(1).riskPercent;
      for(uint32_t trial=1;trial<10000;++trial) {
        g_campaign.rng=trial;bool failed=rng_range(100)<risk;
        if(failed==(branch==1)){g_campaign.rng=trial;break;}
      }
    }
    char name[48]; snprintf(name, sizeof(name), "merchant-incident-%u", kind); if(!branch)card(CARD_INCIDENT,0,name);
    if (!sim_merchant_incident_resolve(kind <= INCIDENT_REEF ? 1 : 0)) { fclose(manifest); return false; }
    snprintf(name, sizeof(name), branch?"merchant-outcome-loss-%u":"merchant-outcome-%u", kind); card(CARD_OUTCOME, 0, name);
  }
  fclose(manifest); printf("atlas: PASS (%d additional frames)\n", count);
  return true;
}
