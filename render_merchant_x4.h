#pragma once

static uint16_t merchant_goods_aboard(const Ship& ship) {
  uint16_t count = 0;
  for (int i = 0; i < ship.cargoCount; ++i)
    if (ship.cargo[i].kind == CARGO_MARKET) count += ship.cargo[i].quantity;
  return count;
}

static uint16_t merchant_packages_aboard(const Ship& ship) {
  uint16_t count = 0;
  for (int i = 0; i < ship.cargoCount; ++i)
    if (ship.cargo[i].kind == CARGO_CONTRACT &&
        (ship.cargo[i].jobKind == JOB_PACKAGE || ship.cargo[i].jobKind == JOB_STORY))
      count += ship.cargo[i].quantity;
  return count;
}

// A wrapped parcel is deliberately distinct from all eight market goods.
static void merchant_package_icon(int x, int y, int size) {
  art_in_box(x, y, X4_PARCELS[0], size, size);
}

// Each visible icon is one owned unit. Two rows leave room for every cargo
// type, including parcels, before repeated units and an exact +N remainder.
static void merchant_hold_icons(int x, int y, int width, const Ship& ship) {
  uint16_t quantities[GOOD_COUNT + 2] = {};
  for (int i = 0; i < ship.cargoCount; ++i) {
    const CargoItem& cargo = ship.cargo[i];
    int kind = cargo.kind == CARGO_MARKET && cargo.good < GOOD_COUNT ? cargo.good :
      cargo.kind == CARGO_CONTRACT && (cargo.jobKind == JOB_PACKAGE || cargo.jobKind == JOB_STORY) ?
      GOOD_COUNT : GOOD_COUNT + 1;
    quantities[kind] += cargo.quantity;
  }
  uint8_t order[255]; int total = 0, types = 0;
  for (int kind = 0; kind < GOOD_COUNT + 2; ++kind) if (quantities[kind]) {
    order[total++] = (uint8_t)kind; ++types;
  }
  for (int kind = 0; kind < GOOD_COUNT + 2; ++kind)
    for (int unit = 1; unit < quantities[kind] && total < 255; ++unit) order[total++] = (uint8_t)kind;
  const int step = 64, frame = 56, columns = max(1, width / step);
  const int fit = columns * 2;
  int shown = min(total, total > fit ? fit - 1 : fit);
  // A valid native hold has at most ten types and fourteen illustrated cells.
  if (shown < types) shown = min(types, fit);
  for (int unit = 0; unit < shown; ++unit) {
    int cx = x + (unit % columns) * step, cy = y + (unit / columns) * 60;
    display.drawRect(cx, cy, frame, frame, INK);
    if (order[unit] < GOOD_COUNT) art_in_box(cx + 4, cy + 4, X4_GOODS[order[unit]], 48, 48);
    else if (order[unit] == GOOD_COUNT) merchant_package_icon(cx + 4, cy + 4, 48);
    else text_at(cx + 18, cy + 10, INK, "C"); // retained contract cargo, not a market good
  }
  if (shown < total) {
    char remainder[16]; snprintf(remainder, sizeof(remainder), "+%d", total - shown);
    text_at(x + (shown % columns) * step, y + (shown / columns) * 60 + 10, INK, remainder);
  }
}

static void merchant_tabs() {
  const char* const labels[] = {"BUY", "SELL", "PARCELS"};
  // Parcels open when the guided first voyage ends.
  const int tabs = sim_onboarding_step() >= ONBOARD_BUY ? 2 : 3;
  for (int mode = 0; mode < tabs; ++mode) {
    const HitRegion r = merchant_tab_region(mode);
    text_at(r.x + (r.w - text_width(labels[mode])) / 2, r.y, INK, labels[mode]);
    if (x4_merchant_mode() == mode) display.fillRect(r.x + 24, r.y + r.h - 2, r.w - 48, 3, INK);
  }
}

static void merchant_frame() {
  display.fillRect(16, 8, 768, 440, PAPER);
  uint8_t port = g_ui.selectedShip < g_campaign.shipCount ? g_ships[g_ui.selectedShip].currentPort : g_ui.selectedPort;
  const char* portName = port < MAX_PORTS ? PORT_DEFS[port].name : "Market";
  char title[112], shipName[48];
  if (g_ui.selectedShip < g_campaign.shipCount) {
    fit_text(g_ships[g_ui.selectedShip].name, shipName, sizeof(shipName),
      max(80, 632 - text_width(portName, 3) - text_width(" / ", 3)), 3);
    snprintf(title, sizeof(title), "%s / %s", shipName, portName);
  } else snprintf(title, sizeof(title), "%s", portName);
  text_fit_at(144, 16, 632, INK, title, 3);
  draw_button(merchant_footer_region(2), "BACK");
  merchant_tabs();
}

static void draw_merchant_ship() {
  if (g_ui.selectedShip >= g_campaign.shipCount) return;
  const Ship& ship = g_ships[g_ui.selectedShip];
  dispatch_frame(ship.name);
  art_at(40, 94, X4_SHIPS[ship.classId]);
  char line[128];
  snprintf(line, sizeof(line), "%s %s", ship.state == SHIP_ARRIVED ? "Arrived at" : "At", PORT_DEFS[ship.currentPort].name);
  text_fit_at(180, 98, 580, INK, line, 3);
  snprintf(line, sizeof(line), "%ld coins / %u of %u spaces free", (long)g_campaign.coins,
    sim_ship_capacity(ship) - sim_ship_used_capacity(ship), sim_ship_capacity(ship));
  text_fit_at(180, 146, 580, INK, line);
  uint8_t charted[MAX_ROUTE_LEGS + 1];
  const uint8_t pages = sim_atlas_voyage_pages(g_ui.selectedShip, charted, sizeof(charted));
  char atlas[64] = "";
  if (pages == 1) snprintf(atlas, sizeof(atlas), "New Atlas page: %s", PORT_DEFS[charted[0]].name);
  else if (pages > 1) snprintf(atlas, sizeof(atlas), "%u new Atlas pages", pages);
  if (ship.state == SHIP_ARRIVED && ship.merchant.packagesDelivered) {
    snprintf(line, sizeof(line), "%u %s delivered / +%ld coins%s%s", ship.merchant.packagesDelivered, ship.merchant.packagesDelivered == 1 ? "parcel" : "parcels",
             (long)ship.merchant.packageCoins, pages ? " / " : "", pages ? (pages == 1 ? "New Atlas page" : atlas) : "");
    text_fit_at(40, 194, 720, INK, line);
  } else if (pages) text_fit_at(40, 194, 720, INK, atlas);
  else text_at(40, 194, INK, ship.cargoCount ? "Cargo aboard" : "Cargo hold is empty");
  const CaptainProfile* captain = sim_ship_captain(g_ui.selectedShip);
  merchant_hold_icons(40, 228, captain ? 520 : 720, ship);
  if (captain) captain_badge(768, 232, captain->name, captain->trait);
  const char* const labels[] = {"TRADE", "SAIL", "UPGRADES"};
  for (int i = 0; i < 3; ++i)
    draw_button(merchant_ship_region(i), labels[i], i < 2 || sim_onboarding_step() < ONBOARD_BUY);
}

// A row's name (and its optional second line) sit centred in the row; its
// price or reward is right-aligned to the card edge on every tab.
static int merchant_row_text_top(const HitRegion& r, bool secondLine) {
  const int block = text_height(3) + (secondLine ? 4 + text_height() : 0);
  return r.y + (r.h - block) / 2;
}
static void merchant_row_value(const HitRegion& r, int top, const char* value) {
  text_at(r.x + r.w - 8 - text_width(value, 3), top, INK, value, 3);
}

static void draw_merchant_trade() {
  merchant_frame();
  const MerchantTradeQuote& quote = x4_merchant_trade_quote();
  const bool buying = x4_merchant_mode() == 0;
  const Ship* ship = g_ui.selectedShip < g_campaign.shipCount ? &g_ships[g_ui.selectedShip] : nullptr;
  char line[160];
  if (x4_merchant_editing() && g_ui.selectedGood < GOOD_COUNT) {
    int good = g_ui.selectedGood;
    art_at(48, 132, X4_GOODS[good], 3);
    text_fit_at(152, 128, 600, INK, GOOD_DEFS[good].name, 3);
    snprintf(line, sizeof(line), "%u coins each", quote.unitPrices[good]);
    text_at(152, 175, INK, line);
    snprintf(line, sizeof(line), "%u", x4_merchant_quantities()[good]);
    text_at(center_text(0, IW, line, 5), 222, INK, line, 5);
    unsigned aboard = 0;
    if (!buying && ship) for (int c = 0; c < ship->cargoCount; ++c)
      if (ship->cargo[c].kind == CARGO_MARKET && ship->cargo[c].good == good) aboard += ship->cargo[c].quantity;
    const char* hint = !buying && aboard && x4_merchant_quantities()[good] >= aboard ? "All aboard / DOWN -" : "UP + / DOWN -";
    text_at(center_text(0, IW, hint), 280, INK, hint);
    if (quote.code != SIM_OK) snprintf(line, sizeof(line), "%s", sim_result_text(quote.code));
    else if (!buying) {
      const CaptainProfile* captain = sim_ship_captain(g_ui.selectedShip);
      snprintf(line, sizeof(line), "%+ld profit / %ld coins received%s", (long)quote.profit, (long)quote.cashDelta,
               captain && captain->trait == TRAIT_MERCHANT ? " / Merchant +5%" : "");
    }
    else snprintf(line, sizeof(line), "%ld coins remaining", (long)g_campaign.coins + quote.cashDelta);
    text_fit_at(40, 332, 712, INK, line);
    snprintf(line, sizeof(line), "%s %u / %ld coins", buying ? "BUY" : "SELL", quote.quantity,
      (long)(buying ? quote.total : quote.cashDelta));
    draw_button(merchant_footer_region(1), line, quote.quantity && quote.code == SIM_OK);
    return;
  }
  for (int row = 0; row < 2; ++row) {
    int good = x4_merchant_good(row); if (good < 0 || good >= GOOD_COUNT) continue;
    const HitRegion r = merchant_row_region(row);
    art_at(r.x + 20, r.y + 8, X4_GOODS[good], 3);
    const int top = merchant_row_text_top(r, !buying);
    text_fit_at(r.x + 124, top, 460, INK, GOOD_DEFS[good].name, 3);
    snprintf(line, sizeof(line), "%uc", quote.unitPrices[good]);
    merchant_row_value(r, top, line);
    if (!buying) {
      unsigned aboard = 0;
      if (ship) for (int c = 0; c < ship->cargoCount; ++c)
        if (ship->cargo[c].kind == CARGO_MARKET && ship->cargo[c].good == good) aboard += ship->cargo[c].quantity;
      snprintf(line, sizeof(line), "%u aboard", aboard);
      text_fit_at(r.x + 124, top + text_height(3) + 4, 608, INK, line);
    }
    display.drawFastHLine(r.x, r.y + r.h, r.w, INK);
  }
  if (!x4_merchant_good_count()) text_fit_at(40, 165, 712, INK, buying ? "No goods available" : "No goods aboard", 3);
  if (x4_merchant_good_count() > 2) {
    draw_button(merchant_pager_region(0), "PREV"); draw_button(merchant_pager_region(1), "NEXT");
  }
  snprintf(line, sizeof(line), "%ld coins / %d of %d spaces used", (long)g_campaign.coins,
    ship ? sim_ship_used_capacity(*ship) : 0, ship ? sim_ship_capacity(*ship) : 0);
  text_fit_at(40, 378, 712, INK, line);
  text_at(40, 416, INK, "OK select / Rocker: tabs");
}

// The Grey Passenger's sealed offer: who and what it pays, never what or where.
static void draw_merchant_story_detail() {
  StoryOffer offer{};
  if (!sim_story_offer(g_ui.selectedShip, offer)) return;
  art_at(40, 124, X4_COURIERS[0]);
  text_fit_at(224, 124, 530, INK, story_courier_name(g_campaign.storyDeliveries), 3);
  wrapped_text(224, 172, 530, offer.line < 3 ? STORY_OFFER_LINES[offer.line] : STORY_FINALE_OFFER, 2, 30);
  char line[96];
  snprintf(line, sizeof(line), "Sealed parcel / +%u coins on delivery", offer.reward);
  text_fit_at(224, 246, 530, INK, line);
  text_fit_at(224, 280, 530, INK, offer.code == SIM_OK ? "Uses one cargo space" : "Needs one free cargo space");
  draw_button(merchant_footer_region(1), "TAKE PARCEL", offer.code == SIM_OK);
}

static void draw_merchant_story_row(const HitRegion& r) {
  StoryOffer offer{};
  if (!sim_story_offer(g_ui.selectedShip, offer)) return;
  art_at(r.x + 20, r.y + 4, X4_COURIERS[1]);
  const int top = merchant_row_text_top(r, true);
  text_fit_at(r.x + 124, top, 392, INK, story_courier_name(g_campaign.storyDeliveries), 3);
  char line[32]; snprintf(line, sizeof(line), "+%u coins", offer.reward);
  merchant_row_value(r, top, line);
  text_fit_at(r.x + 124, top + text_height(3) + 4, 620, INK, "Sealed parcel / 1 space");
  display.drawFastHLine(r.x, r.y + r.h, r.w, INK);
}

static void draw_merchant_packages() {
  merchant_frame();
  if (x4_merchant_editing() && x4_merchant_story_selected()) { draw_merchant_story_detail(); return; }
  if (x4_merchant_editing()) {
    PackageOffer offer = x4_merchant_selected_package();
    char line[128];
    merchant_package_icon(48, 140, 72);
    snprintf(line, sizeof(line), "Parcel to %s", offer.destination < MAX_PORTS ? PORT_DEFS[offer.destination].name : "port");
    text_fit_at(152, 128, 600, INK, line, 3);
    snprintf(line, sizeof(line), "+%u coins on delivery", offer.reward);
    text_fit_at(152, 179, 600, INK, line);
    text_at(40, 252, INK, "Free to take / Uses one cargo space");
    text_at(40, 297, INK, "Delivered automatically at its port.");
    if (offer.code != SIM_OK) text_fit_at(40, 340, 712, INK, sim_result_text(offer.code));
    draw_button(merchant_footer_region(1), "TAKE PARCEL", offer.id && offer.code == SIM_OK);
    return;
  }
  char line[128];
  int count = 0;
  for (int row = 0; row < 2; ++row) {
    if (x4_merchant_story_row(row)) { ++count; draw_merchant_story_row(merchant_row_region(row)); continue; }
    PackageOffer offer = x4_merchant_package(row);
    if (offer.destination >= MAX_PORTS || !offer.id) continue;
    ++count;
    HitRegion r = merchant_row_region(row);
    merchant_package_icon(r.x + 16, r.y + 8, 72);
    const int top = merchant_row_text_top(r, true);
    text_fit_at(r.x + 124, top, 392, INK, PORT_DEFS[offer.destination].name, 3);
    snprintf(line, sizeof(line), "+%u coins", offer.reward);
    merchant_row_value(r, top, line);
    snprintf(line, sizeof(line), "1 space / Free to take");
    text_fit_at(r.x + 124, top + text_height(3) + 4, 620, INK, line);
    display.drawFastHLine(r.x, r.y + r.h, r.w, INK);
  }
  if (!count) text_fit_at(40, 165, 712, INK, "No parcels available", 3);
  if (x4_merchant_good_count() > 2) {
    draw_button(merchant_pager_region(0), "PREV");
    draw_button(merchant_pager_region(1), "NEXT");
  }
  text_at(40, 416, INK, "OK select / Rocker: tabs");
}

static void draw_merchant_routes() {
  dispatch_frame("Where to?");
  char line[160], time[32];
  int count = 0;
  for (int row = 0; row < VOYAGE_DESTINATION_ROWS; ++row) {
    int destination = x4_voyage_destination(row);
    if (destination < 0 || destination >= MAX_PORTS) continue;
    ++count;
    HitRegion r = voyage_card_row_region(CARD_VOYAGE, 0, row);
    MerchantSailQuote offer = sim_merchant_sail_offer(g_ui.selectedShip, (uint8_t)destination);
    const PortDef& port = PORT_DEFS[destination];
    art_at(r.x + 4, r.y + 12, X4_LANDMARKS[destination], 2);
    text_fit_at(r.x + 152, r.y + 6, 590, INK, port.name, 3);
    voyage_duration(offer.minutes, time, sizeof(time));
    unsigned parcels = 0;
    if (g_ui.selectedShip < g_campaign.shipCount)
      for (int c = 0; c < g_ships[g_ui.selectedShip].cargoCount; ++c) {
        const CargoItem& item = g_ships[g_ui.selectedShip].cargo[c];
        if (item.kind == CARGO_CONTRACT && (item.jobKind == JOB_PACKAGE || item.jobKind == JOB_STORY))
          for (int leg = 0; leg < offer.routeCount; ++leg)
            if (item.destination == offer.route[leg]) { parcels += item.quantity; break; }
      }
    snprintf(line, sizeof(line), "%s / Parcels: %u", time, parcels);
    text_fit_at(r.x + 152, r.y + 50, 590, INK, line, 3);
    dispatch_port_goods(r.x + 152, r.y + 97, "Makes", port.produced);
    dispatch_port_goods(r.x + 370, r.y + 97, "Wants", port.demanded);
    snprintf(line, sizeof(line), "Duty %u%%", port.duty);
    text_fit_at(r.x + 606, r.y + 100, 138, INK, line);
    display.drawFastHLine(r.x, r.y + r.h, r.w, INK);
  }
  if (!count) text_fit_at(40, 166, 712, INK, "No available route", 3);
  if (x4_voyage_destination_count() > VOYAGE_DESTINATION_ROWS) {
    draw_button(voyage_footer_region(CARD_VOYAGE, 0), "PREV");
    draw_button(voyage_footer_region(CARD_VOYAGE, 1), "NEXT");
  }
}

static void draw_merchant_review() {
  const MerchantSailQuote& quote = x4_merchant_sail_quote();
  const Ship& ship = g_ships[g_ui.selectedShip];
  char line[128], time[32];
  snprintf(line, sizeof(line), "To %s", quote.destination < MAX_PORTS ? PORT_DEFS[quote.destination].name : "port");
  dispatch_frame(line);
  art_at(40, 100, X4_SHIPS[ship.classId], 2);
  dispatch_ship_caption(40, 100, ship);
  voyage_duration(quote.minutes, time, sizeof(time));
  text_fit_at(328, 80, 448, INK, time, 4);
  if (merchant_goods_aboard(ship)) {
    snprintf(line, sizeof(line), "Est. sale %ld", (long)quote.saleEstimate);
    text_fit_at(328, 142, 448, INK, line, 3);
    text_at(328, 187, INK, "At today's prices");
  }
  merchant_hold_icons(328, 226, 448, ship);
  if (quote.code != SIM_OK) text_fit_at(328, 300, 448, INK, sim_result_text(quote.code));
  else if (quote.packagePayout) {
    snprintf(line, sizeof(line), "Parcels +%ld fixed", (long)quote.packagePayout);
    text_fit_at(328, 300, 448, INK, line);
  }
  draw_button(voyage_footer_region(CARD_VOYAGE, 0), "MARKET");
  draw_button(voyage_footer_region(CARD_VOYAGE, 1), "SAIL", quote.code == SIM_OK);
}
