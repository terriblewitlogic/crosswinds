#pragma once

// Fleet Dispatch uses the approved paper-and-ink art at its readable size.
// Main places use the top tabs; a transaction gets the whole sheet of paper.
static bool dispatch_native_card() {
  return g_ui.card == CARD_SHIP || merchant_card(g_ui.card) || g_ui.card == CARD_VOYAGE_LOAD || g_ui.card == CARD_VOYAGE_DEPARTURE ||
    (g_ui.card == CARD_VOYAGE && (g_ui.page == 0 || g_ui.page == 2));
}

static void dispatch_frame(const char* title) {
  display.fillRect(16, 8, 768, 440, PAPER);
  if (title && *title) text_fit_at(144, 16, 632, INK, title, 3);
  draw_button(voyage_footer_region(g_ui.card, 2), "BACK");
}

// One icon means one unit. Preserve one of every type before repeated units;
// the final +N is exactly the number of units that do not fit, never a total.
static void dispatch_cargo_icons(int x, int y, int width, const uint8_t* quantities,
                                 uint8_t capacity = 0, bool large = false) {
  uint8_t order[255]; int total = 0, types = 0;
  for (int good = 0; good < GOOD_COUNT; ++good) if (quantities[good]) {
    order[total++] = (uint8_t)good; ++types;
  }
  for (int good = 0; good < GOOD_COUNT; ++good)
    for (int unit = 1; unit < quantities[good] && total < 255; ++unit) order[total++] = (uint8_t)good;
  const int step = large ? 64 : 42, frame = large ? 56 : 36;
  int fit = max(1, width / step);
  int shown = total <= fit ? total : max(types, fit - 1);
  shown = min(shown, total);
  if (shown < total) {
    char hidden[16];
    snprintf(hidden, sizeof(hidden), "+%d", total - shown);
    while (shown > types && shown * step + text_width(hidden) > width) {
      --shown; snprintf(hidden, sizeof(hidden), "+%d", total - shown);
    }
    if (shown >= fit || shown * step + text_width(hidden) > width) {
      char summary[32]; snprintf(summary, sizeof(summary), "%d types / %d", types, total);
      text_fit_at(x, y + 2, width, INK, summary);
      return;  // legacy manifests remain available in Cargo; never draw into the next row
    }
  }
  for (int unit = 0; unit < shown; ++unit) {
    int cx = x + unit * step, cy = y;
    display.drawRect(cx, cy, frame, frame, INK);
    art_in_box(cx + (large ? 4 : 2), cy + (large ? 4 : 2), X4_GOODS[order[unit]], large ? 48 : 32, large ? 48 : 32);
  }
  if (shown < total) {
    char hidden[16]; snprintf(hidden, sizeof(hidden), "+%d", total - shown);
    int cx = x + shown * step, cy = y;
    text_at(cx, cy + 2, INK, hidden);
  } else for (int unit = shown; unit < min((int)capacity, fit); ++unit)
    display.drawRect(x + unit * step, y, frame, frame, INK);
}

// A ship's name centred directly under its 2x art slot at (x, y).
static void dispatch_ship_caption(int x, int y, const Ship& ship) {
  const X4Art& art = X4_SHIPS[ship.classId < 5 ? ship.classId : 0];
  char fitted[64]; fit_text(ship.name, fitted, sizeof(fitted), 272, 3);
  const int centre = x + art.width, width = text_width(fitted, 3);
  text_at(max(32, centre - width / 2), y + art.height * 2 + 6, INK, fitted, 3);
}

static void draw_dispatch_fleet() {
  char line[128], eta[32];
  for (int row = 0; row < 3; ++row) {
    uint8_t index = x4_fleet_ship(row);
    if (index >= g_campaign.shipCount) break;
    const Ship& ship = g_ships[index];
    HitRegion r = voyage_fleet_row_region(row);
    art_at(r.x + 4, r.y, X4_SHIPS[ship.classId]);
    text_fit_at(r.x + 144, r.y + 8, 420, INK, ship.name, 3);
    const char* state = "Ready";
    if (ship.state == SHIP_SAILING) { eta_short(ship, eta, sizeof(eta)); state = eta; }
    else if (ship.state == SHIP_AWAITING_DECISION) state = "Respond";
    else if (ship.state == SHIP_DISABLED) state = "Repair";
    text_at(r.x + r.w - 16 - min(168, text_width(state, 3)), r.y + 24, INK, state, 3);
    uint8_t port = ship.state == SHIP_SAILING ?
      (ship.merchant.state == MERCHANT_SAILING ? ship.merchant.destination :
       ship.booking.state == BOOKING_SAILING ? ship.booking.destination : ship.toPort) : ship.currentPort;
    snprintf(line, sizeof(line), "%s %s", ship.state == SHIP_SAILING ? "To" : "At", port < MAX_PORTS ? PORT_DEFS[port].name : "sea");
    if (ship.state == SHIP_AWAITING_DECISION) snprintf(line, sizeof(line), "%s / %s", merchant_incident_label(ship),encounter_opportunity(ship.incidentType)?"Offer open":"Voyage paused");
    text_fit_at(r.x + 144, r.y + 52, 420, INK, line);
    display.drawFastHLine(r.x, r.y + r.h, r.w, INK);
  }
  if (g_campaign.shipCount > 3) {
    draw_button(voyage_footer_region(CARD_NONE, 0), "PREV");
    draw_button(voyage_footer_region(CARD_NONE, 1), "NEXT");
  }
}

static void dispatch_port_goods(int x, int y, const char* label, const uint8_t goods[2]) {
  text_at(x, y + 3, INK, label);
  int cursor = x + text_width(label) + 12;
  for (int item = 0; item < 2; ++item) {
    if (goods[item] >= GOOD_COUNT || (item && goods[item] == goods[0])) continue;
    art_in_box(cursor, y, X4_GOODS[goods[item]], 36, 36);
    cursor += 40;
  }
}

#include "render_merchant_x4.h"

static void draw_dispatch_routes() {
  if (g_campaign.economyVersion >= 2) { draw_merchant_routes(); return; }
  char title[96], line[160], time[32];
  snprintf(title, sizeof(title), "Where to?");
  dispatch_frame(title);
  int count = 0;
  for (int row = 0; row < VOYAGE_DESTINATION_ROWS; ++row) {
    int destination = x4_voyage_destination(row);
    if (destination < 0) break;
    ++count;
    HitRegion r = voyage_card_row_region(CARD_VOYAGE, 0, row);
    const PortDef& port = PORT_DEFS[destination];
    VoyageQuote offer = voyage_visible_offer((uint8_t)destination, VOYAGE_TRADE);
    voyage_duration(offer.minutes, time, sizeof(time));
    art_at(r.x + 4, r.y + 12, X4_LANDMARKS[destination], 2);
    text_fit_at(r.x + 152, r.y + 6, 590, INK, port.name, 3);
    snprintf(line, sizeof(line), "%s  /  %+ld profit", time, (long)offer.profit);
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

static void draw_dispatch_load() {
  const VoyageQuote& quote = x4_voyage_quote();
  char line[160], title[96];
  snprintf(title, sizeof(title), "Load for %s", quote.destination < MAX_PORTS ? PORT_DEFS[quote.destination].name : "destination");
  dispatch_frame(title);
  for (int row = 0; row < 2; ++row) {
    uint8_t good = x4_load_good(row);
    if (good >= GOOD_COUNT) break;
    HitRegion r = voyage_card_row_region(CARD_VOYAGE_LOAD, g_ui.page, row);
    bool projectGood = quote.projectGoods & (1u << good);
    bool limited = quote.availableQuantities[good] < quote.capacity;
    bool selected = good == x4_load_selected_good();
    art_at(r.x + 20, r.y + 14, X4_GOODS[good], 3);
    text_fit_at(r.x + 124, r.y + (projectGood || limited ? 8 : 30), 408, INK, GOOD_DEFS[good].name, 3);
    line[0] = 0;
    if (projectGood) snprintf(line, sizeof(line), "Dock project");
    if (limited) {
      size_t used = strlen(line);
      if (!quote.availableQuantities[good]) snprintf(line + used, sizeof(line) - used, "%sSold out", used ? " / " : "");
      else snprintf(line + used, sizeof(line) - used, "%sMax %u", used ? " / " : "", quote.availableQuantities[good]);
    }
    if (line[0]) text_fit_at(r.x + 124, r.y + 53, 408, INK, line);
    const HitRegion less = voyage_load_adjust_region(row, -1);
    const HitRegion more = voyage_load_adjust_region(row, 1);
    const int arrowY = text_middle(less.y, less.h, 3);
    text_at(less.x + (less.w - text_width("<", 3)) / 2, arrowY, INK, "<", 3);
    text_at(more.x + (more.w - text_width(">", 3)) / 2, arrowY, INK, ">", 3);
    snprintf(line, sizeof(line), "%u", quote.quantities[good]);
    text_at(672 - text_width(line, 3) / 2, arrowY, INK, line, 3);
    if (selected) {
      display.fillRect(r.x, r.y + 20, 4, 60, INK);
      text_fit_at(r.x + 572, r.y + 59, 172, INK, "Rocker");
    }
    display.drawFastHLine(r.x, r.y + r.h, r.w, INK);
  }
  snprintf(line, sizeof(line), "%u / %u hold", quote.quantity, quote.capacity);
  text_fit_at(40, 331, 336, INK, line, 3);
  snprintf(line, sizeof(line), "%+ld profit", (long)quote.profit);
  text_fit_at(432, 331, 336, INK, line, 3);
  uint8_t selectedGood = x4_load_selected_good();
  bool canFill = selectedGood < GOOD_COUNT && quote.quantity < quote.capacity &&
    quote.quantities[selectedGood] < quote.availableQuantities[selectedGood];
  draw_button(voyage_footer_region(CARD_VOYAGE_LOAD, 0), "FILL HOLD", canFill);
  draw_button(voyage_footer_region(CARD_VOYAGE_LOAD, 1), "REVIEW", quote.quantity > 0);
}

static void draw_dispatch_review() {
  if (g_campaign.economyVersion >= 2) { draw_merchant_review(); return; }
  const VoyageQuote& quote = x4_voyage_quote();
  char line[160], time[32];
  snprintf(line, sizeof(line), "To %s", quote.destination < MAX_PORTS ? PORT_DEFS[quote.destination].name : "destination");
  dispatch_frame(line);
  voyage_duration(quote.minutes, time, sizeof(time));
  art_at(40, 100, X4_SHIPS[g_ships[g_ui.selectedShip].classId], 2);
  dispatch_ship_caption(40, 100, g_ships[g_ui.selectedShip]);
  text_fit_at(328, 92, 448, INK, time, 4);
  snprintf(line, sizeof(line), "%+ld profit", (long)quote.profit);
  text_fit_at(328, 151, 448, INK, line, text_width(line, 4) <= 448 ? 4 : 3);
  dispatch_cargo_icons(328, 225, 448, quote.quantities, quote.capacity, true);
  snprintf(line, sizeof(line), "Receive %ld", (long)quote.arrivalCoins);
  text_fit_at(328, 303, 448, INK, line);
  line[0] = 0;
  if (quote.planningOnly) snprintf(line, sizeof(line), "Plan after arrival");
  else if (quote.code != SIM_OK) snprintf(line, sizeof(line), "%s", sim_result_text(quote.code));
  else {
    int materials = 0; for (int g = 0; g < GOOD_COUNT; ++g) materials += quote.projectQuantities[g];
    if (materials) snprintf(line, sizeof(line), "+%d dock cargo", materials);
  }
  if (line[0]) text_fit_at(328, 343, 448, INK, line);
  draw_button(voyage_footer_region(CARD_VOYAGE, 0), "EDIT");
  bool stillSailing = g_ui.selectedShip < g_campaign.shipCount && g_ships[g_ui.selectedShip].booking.state == BOOKING_SAILING;
  if (quote.planningOnly) snprintf(line, sizeof(line), "%s", stillSailing ? "WATCH" : "RECHECK");
  else snprintf(line, sizeof(line), "Pay %ld & sail", (long)quote.payNow);
  draw_button(voyage_footer_region(CARD_VOYAGE, 1), line, quote.planningOnly || quote.code == SIM_OK);
}

static void draw_dispatch_departure() {
  const Ship& ship = g_ships[g_ui.selectedShip];
  dispatch_frame("");
  art_at(48, 110, X4_SHIPS[ship.classId], 2);
  dispatch_ship_caption(48, 110, ship);
  uint8_t ready = x4_next_ready_ship();
  for (int row = 0; row < 2; ++row) {
    HitRegion r = voyage_card_row_region(CARD_VOYAGE_DEPARTURE, 0, row);
    display.drawRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 3, INK);
    text_fit_at(r.x + 24, text_middle(r.y, r.h, 3), r.w - 48, INK, row ? "Watch" : ready < MAX_SHIPS ? "Next ready" : "Fleet", 3);
  }
}
