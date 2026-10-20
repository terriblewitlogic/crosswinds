#pragma once

// The primary game loop uses one generous text scale throughout. Less useful
// accounting and legacy controls stay in Details instead of shrinking the type.
static void voyage_duration(uint32_t minutes, char* out, size_t cap) {
  uint32_t seconds = x4_real_seconds(minutes, g_campaign.timeScale);
  if (seconds % 60u == 0) snprintf(out, cap, "%lu min", (unsigned long)(seconds / 60u));
  else x4_format_duration(out, cap, seconds);
}

// The same dock advances from pilings to construction, a finished quay, and
// the company flag when its regional ports open.
static uint8_t voyage_project_art_stage(uint8_t region) {
  if (g_campaign.regionMask & (1u << region)) return 3;
  uint16_t target = sim_project_target(region);
  if (g_campaign.projectCargo[region][0] >= target && g_campaign.projectCargo[region][1] >= target) return 2;
  return g_campaign.projectCargo[region][0] || g_campaign.projectCargo[region][1] ? 1 : 0;
}

static void draw_voyage_stamp(int x, int y, uint8_t region, int scale) {
  if (scale==1) {art_at(x,y,X4_REGION_STAMPS[region]);return;}
  const X4Art& dock=X4_DOCK_STAGES[voyage_project_art_stage(region)];
  // A real construction illustration fits both Company and the project card.
  // No separate detached quay, stretched regional emblem, or floating flag.
  native_art_at(x+(160-dock.width)/2,y+128-dock.height,dock);
}

static void voyage_goal_text(char* title, size_t titleCap, char* detail, size_t detailCap) {
  if (g_campaign.deliveredUnits < 12) {
    snprintf(title, titleCap, "Open Mariner Rest");
    snprintf(detail, detailCap, "%lu / 12 cargo delivered", (unsigned long)g_campaign.deliveredUnits);
  } else if (g_campaign.shipCount < 2) {
    snprintf(title, titleCap, "A second Sloop");
    snprintf(detail, detailCap, "Deliveries: %lu / 24",
             (unsigned long)min((uint32_t)24, g_campaign.deliveredUnits));
  } else if (voyage_needs_range()) {
    snprintf(title, titleCap, "Reach the farther ports");
    snprintf(detail, detailCap, "Choose one way to increase sailing range.");
  } else if (g_campaign.deliveredUnits < 48) {
    snprintf(title, titleCap, "Build a regional dock");
    snprintf(detail, detailCap, "%lu / 48 cargo delivered", (unsigned long)g_campaign.deliveredUnits);
  } else {
    uint8_t region = sim_project_region();
    if (region >= REGION_COUNT) {
      snprintf(title, titleCap, "%s", g_campaign.won ? "Grand Charter complete" : "Earn the Grand Charter");
      uint8_t next = 0; int visited = 0;
      for (int r = 0; r < REGION_COUNT; ++r) {
        if (g_campaign.visitedMask & (1u << r)) ++visited;
        else if (!next) next = (uint8_t)r;
      }
      if (g_campaign.won) snprintf(detail, detailCap, "Every region sailed. Your company can keep growing.");
      else if (visited < REGION_COUNT) snprintf(detail, detailCap, "%d / 6 regions visited. Next: %s", visited, REGION_NAMES[next]);
      else snprintf(detail, detailCap, "All six regions visited. Return a ship to Lantern Quay.");
    } else {
      snprintf(title, titleCap, "Open %s", REGION_NAMES[region]);
      uint16_t target = sim_project_target(region);
      snprintf(detail, detailCap, "Timber %u/%u  |  Ironware %u/%u",
        g_campaign.projectCargo[region][0], target, g_campaign.projectCargo[region][1], target);
    }
  }
}

static void draw_voyage_port() {
  const PortDef& port = PORT_DEFS[g_ui.selectedPort];
  char line[128], goods[64];
  art_at(40, 170, X4_LANDMARKS[g_ui.selectedPort], 3);
  text_fit_at(272, 114, 504, INK, port.name, 3);
  text_at(272, 188, INK, "Makes");
  voyage_goods_text(port.produced, goods, sizeof(goods));
  text_fit_at(384, 188, 392, INK, goods);
  text_at(272, 244, INK, "Wants");
  voyage_goods_text(port.demanded, goods, sizeof(goods));
  text_fit_at(384, 244, 392, INK, goods);
  snprintf(line, sizeof(line), "Duty %u%%", port.duty);
  text_fit_at(272, 308, 504, INK, line);
}

static void draw_voyage_company() {
  char line[128];
  uint8_t project = sim_project_region();
  if (g_campaign.deliveredUnits < 12) {
    art_at(40, 164, X4_LANDMARKS[3], 3);
    text_fit_at(288, 130, 488, INK, "Open Mariner Rest", 3);
    snprintf(line, sizeof(line), "%lu / 12 cargo delivered", (unsigned long)g_campaign.deliveredUnits);
    text_fit_at(288, 222, 488, INK, line, 3);
    draw_progress(288, 294, 440, min(100, (int)g_campaign.deliveredUnits * 100 / 12));
  } else if (g_campaign.shipCount < 2) {
    art_at(40, 160, X4_SHIPS[0], 2);
    text_fit_at(288, 130, 488, INK, "A second Sloop", 3);
    text_at(288, 210, INK, "1,100 coins needed", 3);
    if (g_campaign.deliveredUnits < 24) {
      snprintf(line, sizeof(line), "%lu / 24 cargo delivered", (unsigned long)g_campaign.deliveredUnits);
      text_fit_at(288, 278, 488, INK, line);
    }
  } else if (voyage_needs_range()) {
    art_at(40, 160, X4_SHIPS[1], 2);
    text_fit_at(288, 130, 488, INK, "A longer voyage", 3);
    text_fit_at(288, 210, 488, INK, "Schooner", 3);
    snprintf(line, sizeof(line), "%u coins", sim_ship_package_cost(1));
    text_fit_at(288, 274, 488, INK, line);
  } else if (g_campaign.deliveredUnits < 48) {
    draw_voyage_stamp(72, 166, project < REGION_COUNT ? project : 0, 4);
    text_fit_at(288, 130, 488, INK, "A regional dock", 3);
    snprintf(line, sizeof(line), "%lu / 48 cargo delivered", (unsigned long)g_campaign.deliveredUnits);
    text_fit_at(288, 222, 488, INK, line, 3);
    draw_progress(288, 294, 440, min(100, (int)g_campaign.deliveredUnits * 100 / 48));
  } else if (project < REGION_COUNT) {
    draw_voyage_stamp(72, 166, project, 4);
    snprintf(line, sizeof(line), "Open %s", REGION_NAMES[project]);
    text_fit_at(288, 112, 488, INK, line, 3);
    snprintf(line, sizeof(line), "Import to %s", PORT_DEFS[sim_project_port(project)].name);
    text_fit_at(288, 170, 488, INK, line);
    for (int material = 0; material < 2; ++material) {
      int x = 288 + material * 248;
      art_at(x, 235, X4_GOODS[material ? GOOD_IRONWARE : GOOD_TIMBER], 2);
      snprintf(line, sizeof(line), "%u / %u", g_campaign.projectCargo[project][material], sim_project_target(project));
      text_fit_at(x + 64, 242, 180, INK, line, 3);
    }
  } else {
    draw_voyage_stamp(72, 166, 0, 4);
    text_fit_at(288, 130, 488, INK, "Grand Charter", 3);
    if (g_campaign.won) snprintf(line, sizeof(line), "Complete");
    else {
      int visited = 0; uint8_t next = 0;
      for (int r = 0; r < REGION_COUNT; ++r) {
        if (g_campaign.visitedMask & (1u << r)) ++visited;
        else if (!next) next = (uint8_t)r;
      }
      if (visited < REGION_COUNT) snprintf(line, sizeof(line), "Visit %s", REGION_NAMES[next]);
      else snprintf(line, sizeof(line), "Return to Lantern Quay");
    }
    text_fit_at(288, 222, 488, INK, line);
  }
}

static void draw_voyage_details() {
  card_frame(g_ui.page ? "Company Details / 2" : "Company Details / 1");
  for (int row = 0; row < CARD_VISIBLE_ROWS; ++row) {
    int item = voyage_detail_index(g_ui.page * CARD_VISIBLE_ROWS + row);
    if (item >= 0) row_text(row, VOYAGE_DETAIL_LABELS[item]);
  }
  if (voyage_detail_count() > CARD_VISIBLE_ROWS) {
    draw_button(CARD_FOOTER[0], "PREV");
    draw_button(CARD_FOOTER[1], "NEXT");
  }
  draw_button(CARD_FOOTER[2], "CLOSE");
}

static void draw_voyage_goal_card() {
  char goal[96], detail[160], line[160];
  voyage_goal_text(goal, sizeof(goal), detail, sizeof(detail));
  card_frame("Your next goal");
  text_fit_at(40, 96, 712, INK, goal, 3);
  text_fit_at(40, 146, 712, INK, detail);
  if (g_campaign.deliveredUnits < 12) {
    art_at(52, 205, X4_LANDMARKS[3], 2);
    wrapped_text(226, 205, 526, g_campaign.economyVersion >= 2 ?
      "Sell imports or deliver parcels. Each unit counts toward the 12 deliveries." :
      "Every paid delivery counts. Freight and trade both bring Mariner Rest closer.", 3, 33);
  } else if (g_campaign.shipCount < 2) {
    art_at(54, 213, X4_SHIPS[0]);
    text_at(226, 202, INK, "Sell imports or deliver parcels to count.");
    text_at(226, 242, INK, g_campaign.economyVersion >= 2 ?
      "Need 1,100 coins: pay 900, keep 200." : "Price 900 coins. Keep 200 to sail.");
    snprintf(line, sizeof(line), "Your coins: %ld / 1,100", (long)g_campaign.coins);
    text_at(226, 282, INK, line);
    draw_button(CARD_FOOTER[0], "BUY 900", sim_ship_purchase_block(0) == SIM_OK);
  } else if (voyage_needs_range()) {
    art_at(46, 215, X4_SHIPS[1]);
    snprintf(line, sizeof(line), "Schooner: need %u coins (%u kept)", sim_ship_package_cost(1) + 200, 200);
    text_fit_at(194, 199, 560, INK, line);
    text_at(194, 237, INK, "OR Long casks: 420 coins, -1 cargo space.");
    text_at(194, 275, INK, "Both options reach the next dock project.");
    SimResultCode blocked = sim_ship_purchase_block(1);
    if (blocked != SIM_OK && blocked != SIM_NO_FUNDS) {
      snprintf(line, sizeof(line), "Ship purchase: %s", sim_result_text(blocked));
      text_fit_at(40, 318, 712, INK, line);
    }
    snprintf(line, sizeof(line), "BUY %u", sim_ship_package_cost(1));
    draw_button(CARD_FOOTER[0], line, sim_ship_purchase_block(1) == SIM_OK);
    draw_button(CARD_FOOTER[1], "REFIT SLOOP");
  } else if (g_campaign.deliveredUnits < 48) {
    wrapped_text(40, 207, 712, "Keep both ships earning. Your next dock project opens another region and its trading goods.", 3, 33);
  } else {
    uint8_t region = sim_project_region();
    if (region < REGION_COUNT) {
      draw_voyage_stamp(32, 208, region, 2);
      snprintf(line, sizeof(line), "Deliver to %s", PORT_DEFS[sim_project_port(region)].name);
      text_fit_at(208, 201, 544, INK, line, 3);
      text_at(208, 245, INK, "Buy elsewhere. Sail here. Give materials.");
      snprintf(line, sizeof(line), "Then build: %u coins needed (%u kept)", sim_project_cost(region) + 200, 200);
      text_fit_at(208, 284, 544, INK, line);
      snprintf(line, sizeof(line), "BUILD %u", sim_project_cost(region));
      bool materials = g_campaign.projectCargo[region][0] >= sim_project_target(region) &&
        g_campaign.projectCargo[region][1] >= sim_project_target(region);
      draw_button(CARD_FOOTER[0], line, materials && g_campaign.coins >= sim_project_cost(region) + 200);
      if (g_campaign.economyVersion >= 2) {
        int good = x4_merchant_donation_good();
        if (good >= 0 && good < GOOD_COUNT) {
          snprintf(line, sizeof(line), "Give 1 %s", GOOD_DEFS[good].name);
          draw_button(CARD_FOOTER[1], line);
        }
      }
    } else {
      for (uint8_t r = 0; r < REGION_COUNT; ++r)
        if (g_campaign.visitedMask & (1u << r)) draw_voyage_stamp(92 + r * 116, 208, r, 1);
      wrapped_text(40, 280, 712, g_campaign.won ?
        "Your company has circled the world. Keep building your fleet and choosing the voyages you enjoy." :
        "Visit a port in every region, then bring a company ship home to Lantern Quay to earn the Grand Charter.", 2, 33);
    }
  }
  draw_button(CARD_FOOTER[2], "CLOSE");
}

// One line per guided step, in the captain's voice, along the bottom edge.
static bool onboarding_line(char* out, size_t cap) {
  const uint8_t step = sim_onboarding_step();
  const CaptainProfile* captain = sim_ship_captain(0);
  if (step < ONBOARD_BUY || !captain || g_ui.title || g_ui.card == CARD_INCIDENT) return false;
  const char* say =
    step == ONBOARD_BUY ? (g_ui.card == CARD_TRADE && x4_merchant_editing() ?
      "A full hold of Textiles. Press BUY." : "Textiles are cheap here. TRADE, then buy a hold full.") :
    step == ONBOARD_SAIL ? "Now SAIL to Gullhaven. They pay well for Textiles." :
    step == ONBOARD_VOYAGE ? "Two minutes to Gullhaven. Watch us sail." :
    g_ui.card == CARD_TRADE && x4_merchant_editing() ? "Sell them all. Press SELL." :
    "Gullhaven! Open TRADE and sell the Textiles.";
  snprintf(out, cap, "%s: %s", captain->name, say);
  return true;
}

static void draw_onboarding_strip() {
  char line[128];
  if (!onboarding_line(line, sizeof(line))) return;
  // Hub buttons end at 448; the line sits below them, inside the bottom inset.
  display.fillRect(X4_INSET_LEFT, 450, IW - X4_INSET_LEFT - X4_INSET_RIGHT, 30 - X4_INSET_BOTTOM, PAPER);
  display.drawFastHLine(X4_INSET_LEFT + 16, 450, IW - X4_INSET_LEFT - X4_INSET_RIGHT - 32, INK);
  text_fit_at(32, 451, 736, INK, line);
}

static const char* onboarding_trait_effect(uint8_t trait) {
  return trait == TRAIT_MERCHANT ? "Sells every cargo for 5% more." :
         trait == TRAIT_GUNNER ? "Holds off pirates as if armed." :
         "Faster sailing. Reads reefs.";
}

static void onboarding_heading(const char* heading) {
  display.setFont(&fonts::FreeSerifBold24pt7b);
  display.setTextSize(1);
  display.setTextColor(INK);
  display.setTextDatum(lgfx::textdatum_t::top_left);
  const int width = display.textWidth(heading), x = (IW - width) / 2;
  display.drawString(heading, x, 22);
  draw_compass_star(x - 30, 44, INK, 14);
  draw_compass_star(x + width + 30, 44, INK, 14);
  display.drawFastHLine(60, 80, IW - 120, INK);
}

// Choose your captain: three candidates side by side.
static void draw_onboarding_choice() {
  onboarding_heading("Choose your captain");
  for (uint8_t i = 0; i < 3; ++i) {
    CaptainProfile c{};
    if (!sim_onboarding_candidate(i, &c)) continue;
    const int x = 32 + i * 248, y = 96, w = 232, h = 284;
    if (i == x4_onboarding_pick()) frame_rect(x, y, w, h, 3);
    else display.drawRect(x, y, w, h, INK);
    art_in_box(x + (w - 104) / 2, y + 12, X4_CAPTAINS[c.portrait], 104, 108);
    text_at(center_text(x, w, c.name), y + 126, INK, c.name);
    const char* trait = content_trait_name(c.trait);
    text_at(center_text(x, w, trait, 3), y + 160, INK, trait, 3);
    wrapped_text(x + 14, y + 210, w - 28, onboarding_trait_effect(c.trait), 2, 31);
  }
  const char* hint = "UP / DOWN choose  /  OK hire  /  BACK skip the guided voyage";
  text_at(center_text(0, IW, hint), 400, INK, hint);
}

// Name your captain: cycle a list; the face stays with the captain.
static void draw_onboarding_name() {
  onboarding_heading("Name your captain");
  const CaptainProfile* c = sim_ship_captain(0);
  if (!c) return;
  frame_rect(96, 112, 128, 140, 3);
  art_in_box(104, 120, X4_CAPTAINS[c->portrait], 112, 124);
  text_at(272, 112, INK, "Captain");
  text_fit_at(272, 146, 496, INK, c->name, 4);
  char line[96];
  snprintf(line, sizeof(line), "%s: %s", content_trait_name(c->trait), onboarding_trait_effect(c->trait));
  wrapped_text(272, 214, 496, line, 2, 31);
  text_at(272, 300, INK, "UP / DOWN  another name");
  snprintf(line, sizeof(line), "OK  sail with Captain %s", c->name);
  text_fit_at(272, 334, 496, INK, line);
  text_at(272, 368, INK, "BACK  choose again");
}

static void draw_onboarding() {
  display.fillScreen(PAPER);
  if (sim_onboarding_step() == ONBOARD_NAME) draw_onboarding_name();
  else draw_onboarding_choice();
}

// The goal moment: a whole-screen page, framed like a charter, for each rung
// of the NEXT GOAL ladder. OK or BACK returns to the screen underneath.
static void draw_goal_moment() {
  const uint8_t step = x4_goal_moment_step();
  const uint8_t region = x4_goal_moment_region() < REGION_COUNT ? x4_goal_moment_region() : 1;
  char title[96], body[192], next[96], unused[160];
  const X4Art* art = nullptr;
  const CaptainProfile* firstCaptain = g_campaign.shipCount ? sim_ship_captain(0) : nullptr;
  if (step == GOAL_MOMENT_FIRST_PROFIT) {
    snprintf(title, sizeof(title), "Your first profit");
    snprintf(body, sizeof(body), "+%ld coins on Textiles. Captain %s gained experience. Parcels and every port are now open.",
             (long)x4_goal_moment_value(), firstCaptain ? firstCaptain->name : "");
    art = firstCaptain ? &X4_CAPTAINS[firstCaptain->portrait] : nullptr;
  } else if (step == GOAL_STEP_MARINER_REST) {
    snprintf(title, sizeof(title), "Mariner Rest is open");
    snprintf(body, sizeof(body), "12 cargo delivered. A new port joins your trade, with timber and grain to carry on.");
    art = &X4_LANDMARK_DETAILS[3];
  } else if (step == GOAL_STEP_SECOND_SLOOP) {
    snprintf(title, sizeof(title), "A second Sloop sails");
    snprintf(body, sizeof(body), "Two ships now earn for the company. Keep both of them busy.");
    art = &X4_SHIPS[0];
  } else if (step == GOAL_STEP_RANGE) {
    snprintf(title, sizeof(title), "The farther ports are in reach");
    snprintf(body, sizeof(body), "Your fleet can now sail as far as the next dock project.");
    art = &X4_SHIPS[1];
  } else if (step == GOAL_STEP_DOCK_DELIVERIES) {
    snprintf(title, sizeof(title), "48 cargo delivered");
    snprintf(body, sizeof(body), "Regional docks can be built. Bring timber and ironware to open new waters.");
    art = &X4_DOCK_STAGES[0];
  } else if (step < GOAL_STEP_GRAND_CHARTER) {
    snprintf(title, sizeof(title), "%s is open", REGION_NAMES[region]);
    const uint8_t port = sim_project_port(region);
    snprintf(body, sizeof(body), "The dock at %s is built. New ports and goods are yours to trade.",
             port < MAX_PORTS ? PORT_DEFS[port].name : "the coast");
    art = &X4_DOCK_STAGES[3];
  } else {
    snprintf(title, sizeof(title), "The Grand Charter");
    snprintf(body, sizeof(body), "Your company has sailed every region and come home. The whole world is your trade.");
  }
  if (voyage_goal_step() >= GOAL_STEP_DONE) snprintf(next, sizeof(next), "Keep building your fleet");
  else voyage_goal_text(next, sizeof(next), unused, sizeof(unused));

  display.fillScreen(PAPER);
  frame_rect(10, 10, IW - 20, IH - 20, 4);
  display.drawRect(20, 20, IW - 40, IH - 40, INK);
  // Heading in the title page's serif, between two compass stars.
  const char* heading = "GOAL COMPLETE";
  display.setFont(&fonts::FreeSerifBold24pt7b);
  display.setTextSize(1);
  display.setTextColor(INK);
  display.setTextDatum(lgfx::textdatum_t::top_left);
  const int headingW = display.textWidth(heading), headingX = (IW - headingW) / 2;
  display.drawString(heading, headingX, 38);
  draw_compass_star(headingX - 34, 60, INK, 16);
  draw_compass_star(headingX + headingW + 34, 60, INK, 16);
  display.drawFastHLine(60, 100, IW - 120, INK);
  display.drawFastHLine(60, 103, IW - 120, INK);

  // Illustration in a sunburst.
  const int cx = 168, cy = 236;
  for (int ray = 0; ray < 24; ++ray) {
    float a = ray * 6.2831853f / 24;
    int inner = 92, outer = ray % 2 ? 112 : 126;
    display.drawLine(cx + (int)(cosf(a) * inner), cy + (int)(sinf(a) * inner),
                     cx + (int)(cosf(a) * outer), cy + (int)(sinf(a) * outer), INK);
  }
  display.drawCircle(cx, cy, 86, INK);
  display.drawCircle(cx, cy, 88, INK);
  if (art) art_in_box(cx - 80, cy - 70, *art, 160, 140);
  else for (uint8_t r = 0; r < REGION_COUNT; ++r)
    art_at(cx - 66 + (r % 3) * 46, cy - 44 + (r / 3) * 48, X4_REGION_STAMPS[r]);

  text_fit_at(318, 134, 440, INK, title, 3);
  wrapped_text(318, 188, 440, body, 3, 33);
  display.drawFastHLine(318, 300, 422, INK);
  text_at(318, 312, INK, "Next goal");
  text_fit_at(318, 346, 440, INK, next, 3);

  const char* prompt = "Press OK to continue";
  const int promptW = text_width(prompt) + 40, promptX = (IW - promptW) / 2;
  display.fillRoundRect(promptX, 404, promptW, 46, 5, INK);
  text_at(promptX + 20, text_middle(404, 46), PAPER, prompt);
}

static VoyageQuote voyage_visible_offer(uint8_t destination, uint8_t mode) {
  bool planning = g_ui.selectedShip < g_campaign.shipCount &&
    g_ships[g_ui.selectedShip].booking.state == BOOKING_SAILING;
  return planning ? sim_return_voyage_quote(g_ui.selectedShip, destination, mode, NO_PORT, 0) :
    sim_voyage_offer(g_ui.selectedShip, destination, mode, NO_PORT, 0);
}

static void draw_voyage_refit() {
  uint8_t module = x4_refit_module();
  if (module >= MODULE_COUNT || g_ui.selectedShip >= g_campaign.shipCount) return;
  const Ship& ship = g_ships[g_ui.selectedShip];
  char line[160];
  if (g_ui.page == 5) {
    snprintf(line, sizeof(line), "%s / ship upgrades", ship.name);
    card_frame(line);
    text_at(40, 98, INK, "Two upgrade spaces per ship.");
    text_at(40, 132, INK, "Choose an empty space or replace an upgrade.");
    for (int slot = 0; slot < 2; ++slot) {
      HitRegion row = voyage_card_row_region(CARD_VOYAGE, 5, slot);
      uint8_t prior = ship.modules[slot];
      snprintf(line, sizeof(line), "Upgrade %d / %s", slot + 1, prior ? MODULE_DEFS[prior - 1].name : "Empty");
      if (prior) art_at(row.x + 8, row.y + 8, X4_FITTINGS[prior - 1]);
      text_fit_at(row.x + (prior ? 52 : 8), row.y + 4, prior ? 668 : 712, INK, line, 3);
      text_fit_at(row.x + 8, row.y + 47, 712, INK, prior ? "Review a replacement. No refund for the old fitting." : "Add Long casks: more range, -1 cargo space.");
      display.drawFastHLine(row.x + 8, row.y + row.h, row.w - 16, INK);
    }
    draw_button(CARD_FOOTER[2], "BACK");
    return;
  }
  uint8_t prior = ship.modules[x4_refit_slot()];
  card_frame("Review ship upgrade");
  text_fit_at(40, 96, 712, INK, ship.name, 3);
  snprintf(line, sizeof(line), "%s%s", prior ? "Replace: " : "Use empty upgrade space", prior ? MODULE_DEFS[prior - 1].name : "");
  text_fit_at(40, 151, 712, INK, line);
  snprintf(line, sizeof(line), "Fit: %s", MODULE_DEFS[module].name);
  art_at(40, 202, X4_FITTINGS[module]);
  text_fit_at(88, 196, 664, INK, line, 3);
  text_fit_at(40, 244, 712, INK, module == MODULE_LONG_CASKS ?
    "Reaches farther ports. Uses one cargo space." : MODULE_DEFS[module].effect);
  snprintf(line, sizeof(line), "Pay %u coins%s", MODULE_DEFS[module].cost, prior ? " / Old fitting has no refund" : "");
  text_fit_at(40, 291, 712, INK, line);
  snprintf(line, sizeof(line), "FIT %u", MODULE_DEFS[module].cost);
  draw_button(CARD_FOOTER[1], line, g_campaign.coins >= MODULE_DEFS[module].cost);
  draw_button(CARD_FOOTER[2], "BACK");
}

#include "render_dispatch_x4.h"

static void draw_voyage_card() {
  if (g_ui.page == 0) { draw_dispatch_routes(); return; }
  if (g_ui.page == 2) { draw_dispatch_review(); return; }
  if (g_ui.page == 4 || g_ui.page == 5) { draw_voyage_refit(); return; }
  const VoyageQuote& quote = x4_voyage_quote();
  char line[160], time[32];
  if (g_ui.page == 0) {
    card_frame("Prepare voyage / destination");
    int count = 0;
    for (int row = 0; row < VOYAGE_DESTINATION_ROWS; ++row) {
      int dest = x4_voyage_destination(row);
      if (dest < 0) break;
      ++count;
      HitRegion region = voyage_card_row_region(CARD_VOYAGE, 0, row);
      VoyageQuote offer = voyage_visible_offer((uint8_t)dest,
        g_campaign.completedVoyages ? VOYAGE_TRADE : VOYAGE_FREIGHT);
      if (offer.code != SIM_OK) offer = voyage_visible_offer((uint8_t)dest, VOYAGE_FREIGHT);
      voyage_duration(offer.minutes, time, sizeof(time));
      text_fit_at(region.x + 12, region.y, 416, INK, PORT_DEFS[dest].name, 3);
      text_fit_at(region.x + 550, region.y + 6, 174, INK, time);
      char wants[64]; voyage_goods_text(PORT_DEFS[dest].demanded, wants, sizeof(wants));
      if (offer.projectRegion < REGION_COUNT)
        snprintf(line, sizeof(line), "Dock project: %s", GOOD_DEFS[offer.good].name);
      else snprintf(line, sizeof(line), "Wants %s", wants);
      text_fit_at(region.x + 12, region.y + 40, 490, INK, line);
      if (offer.code == SIM_OK) {
        snprintf(line, sizeof(line), "+%ld profit", (long)offer.profit);
        text_fit_at(region.x + 550, region.y + 40, 174, INK, line);
      }
      display.drawFastHLine(region.x + 12, region.y + region.h, region.w - 24, INK);
    }
    if (!count) wrapped_text(44, 122, 696, "No booked destination is available for this ship. Company Details keeps cargo, custom routes and harbor relief available.", 4, 36);
    if (x4_voyage_destination_count() > VOYAGE_DESTINATION_ROWS) {
      draw_button(CARD_FOOTER[0], "PREV");
      draw_button(CARD_FOOTER[1], "NEXT");
    }
    draw_button(CARD_FOOTER[2], "BACK");
    return;
  }
  if (g_ui.page == 1) {
    snprintf(line, sizeof(line), "To %s / choose cargo", PORT_DEFS[quote.destination].name);
    card_frame(line);
    for (int row = 0; row < 2; ++row) {
      HitRegion region = voyage_card_row_region(CARD_VOYAGE, 1, row);
      VoyageQuote offer = voyage_visible_offer(quote.destination, row == 0 ? VOYAGE_FREIGHT : VOYAGE_TRADE);
      bool recommended = row == (g_campaign.completedVoyages || quote.planningOnly ? 1 : 0);
      text_at(region.x + 12, region.y, INK, row == 0 ? "Carry freight" : "Buy & trade", 3);
      if (recommended) text_fit_at(region.x + 540, region.y + 6, 184, INK, "Suggested");
      if (offer.quantity && offer.routeCount)
        snprintf(line, sizeof(line), "Pay %ld now   /   Earn %+ld", (long)offer.payNow, (long)offer.profit);
      else snprintf(line, sizeof(line), "%s", sim_result_text(offer.code));
      text_fit_at(region.x + 12, region.y + 45, 712, INK, line);
      display.drawFastHLine(region.x + 12, region.y + region.h, region.w - 24, INK);
    }
    draw_button(CARD_FOOTER[2], "BACK");
    return;
  }
  snprintf(line, sizeof(line), "%s to %s", quote.mode == VOYAGE_RELIEF ? "Harbor work" :
    quote.mode == VOYAGE_TRADE ? "Trade" : "Freight", PORT_DEFS[quote.destination].name);
  card_frame(line);
  voyage_duration(quote.minutes, time, sizeof(time));
  snprintf(line, sizeof(line), "%u %s / %s", quote.quantity,
    quote.good < GOOD_COUNT ? GOOD_DEFS[quote.good].name : "cargo", time);
  text_fit_at(40, 92, 506, INK, line, 3);
  if (quote.capacity > 4) {
    if (quote.good < GOOD_COUNT) art_in_box(554, 98, X4_GOODS[quote.good], 32, 32);
    snprintf(line, sizeof(line), "%u / %u", quote.quantity, quote.capacity);
    text_fit_at(600, 94, 160, INK, line, 3);
  } else for (int slot = 0; slot < quote.capacity; ++slot) {
    bool roomy = quote.capacity <= 4;
    int x = 554 + slot * (roomy ? 44 : 26);
    display.drawRect(x, 96, roomy ? 38 : 24, roomy ? 38 : 27, INK);
    if (slot < quote.quantity && quote.good < GOOD_COUNT)
      art_in_box(x + 3, 99, X4_GOODS[quote.good], roomy ? 32 : 18, roomy ? 32 : 20);
  }
  display.drawFastHLine(40, 142, 720, INK);
  text_at(40, 156, INK, quote.planningOnly ? "Future cost" : "Pay now");
  snprintf(line, sizeof(line), "%ld", (long)quote.payNow);
  text_at(40, 192, INK, line, 4);
  text_at(288, 156, INK, quote.planningOnly ? "Est. payment" : "Paid on arrival");
  snprintf(line, sizeof(line), "%ld", (long)quote.arrivalCoins);
  text_at(288, 192, INK, line, 4);
  text_at(556, 156, INK, quote.planningOnly ? "Est. profit" : "You earn");
  snprintf(line, sizeof(line), "%+ld", (long)quote.profit);
  text_at(556, 192, INK, line, 4);
  if (quote.mode == VOYAGE_TRADE)
    snprintf(line, sizeof(line), "Cargo %ld  |  Sailing %ld", (long)quote.cargoCost, (long)quote.sailingCost);
  else snprintf(line, sizeof(line), "Cargo supplied  |  Sailing %ld", (long)quote.sailingCost);
  text_fit_at(40, 263, 720, INK, line);
  if (quote.planningOnly) {
    text_fit_at(40, 303, 720, INK, "Plan after arrival / offers rechecked in port");
  } else if (quote.code != SIM_OK) {
    text_fit_at(40, 303, 720, INK, sim_result_text(quote.code));
  } else {
    if (quote.mode == VOYAGE_RELIEF) snprintf(line, sizeof(line), "Minimum repairs and crew supplied. No loan.");
    else if (quote.projectRegion < REGION_COUNT) {
      if (quote.bonus > 0) snprintf(line, sizeof(line), "Dock project: %s / Full load +%ld", REGION_NAMES[quote.projectRegion], (long)quote.bonus);
      else snprintf(line, sizeof(line), "Dock project: %s", REGION_NAMES[quote.projectRegion]);
    }
    else if (quote.bonus > 0) snprintf(line, sizeof(line), "Full load +%ld (20%%)  |  Payment fixed", (long)quote.bonus);
    else snprintf(line, sizeof(line), "Fill all %u spaces for a 20%% bonus", quote.capacity);
    text_fit_at(40, 303, 720, INK, line);
  }
  if (g_ui.page != 3) draw_button(CARD_FOOTER[0], "FILL LOAD");
  if (quote.planningOnly) {
    bool arrived = g_ui.selectedShip < g_campaign.shipCount &&
      (g_ships[g_ui.selectedShip].state == SHIP_DOCKED || g_ships[g_ui.selectedShip].state == SHIP_ARRIVED);
    snprintf(line, sizeof(line), "%s", arrived ? "RECHECK" : "WATCH SHIP");
  } else if (quote.mode == VOYAGE_RELIEF) snprintf(line, sizeof(line), "TAKE WORK");
  else snprintf(line, sizeof(line), "%s %ld", quote.mode == VOYAGE_TRADE ? "BUY & SAIL" : "SAIL", (long)quote.payNow);
  draw_button(CARD_FOOTER[1], line, quote.planningOnly || quote.code == SIM_OK);
  draw_button(CARD_FOOTER[2], "BACK");
}
