#pragma once

static const ReturnRow* return_ship_row(const ReturnReport& report, uint8_t ship) {
  for (int i = 0; i < report.rowCount; ++i)
    if (report.rows[i].shipIndex == ship) return &report.rows[i];
  return nullptr;
}

static bool return_earlier_arrival(const ReturnReport& report, uint8_t ship) {
  for (int i = 0; i < report.noticeCount; ++i)
    if (report.notices[i].shipIndex == ship && report.notices[i].kind == ATTENTION_ARRIVAL) return true;
  return false;
}

static void return_status_counts(const ReturnReport& report, char* out, size_t cap, bool includeReady) {
  out[0] = 0;
  const uint8_t counts[] = {includeReady ? report.readyCount : (uint8_t)0,
    report.sailingCount, report.decisionCount, report.repairCount};
  const char* const words[] = {"ready", "sailing", "incident", "repair"};
  for (int i = 0; i < 4; ++i) {
    if (!counts[i]) continue;
    size_t used = strlen(out);
    snprintf(out + used, cap - used, "%s%u %s%s", used ? " / " : "", counts[i], words[i],
      i >= 2 && counts[i] != 1 ? "s" : "");
  }
}

static void return_exact_money(int64_t value, char* out, size_t cap, bool signedValue = false) {
  snprintf(out, cap, signedValue ? "%+lld" : "%lld", (long long)value);
}

// The large summary may abbreviate; Updates always prints the exact totals.
// A tilde explicitly marks the cases where the displayed tenth is rounded down.
static void return_summary_money(int64_t value, char* out, size_t cap, bool signedValue) {
  return_exact_money(value, out, cap, signedValue);
  if (text_width(out, 4) <= 260) return;
  uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;
  static const uint64_t units[] = {1000ull, 1000000ull, 1000000000ull, 1000000000000ull,
                                 1000000000000000ull, 1000000000000000000ull};
  static const char* const names[] = {"thousand", "million", "billion", "trillion", "quadrillion", "quintillion"};
  static const char* const shortNames[] = {"K", "M", "B", "T", "Q", "E18"};
  int unit = 0;
  while (unit < 5 && magnitude >= units[unit + 1]) ++unit;
  uint64_t whole = magnitude / units[unit], remainder = magnitude % units[unit];
  uint64_t tenth = remainder / (units[unit] / 10u);
  const char* sign = value < 0 ? "-" : signedValue ? "+" : "";
  const char* approximate = remainder % (units[unit] / 10u) ? "~" : "";
  char number[40];
  if (tenth) snprintf(number, sizeof(number), "%s%s%llu.%llu", approximate, sign,
                       (unsigned long long)whole, (unsigned long long)tenth);
  else snprintf(number, sizeof(number), "%s%s%llu", approximate, sign, (unsigned long long)whole);
  snprintf(out, cap, "%s %s", number, names[unit]);
  if (text_width(out, 4) > 260) snprintf(out, cap, "%s%s", number, shortNames[unit]);
}

static void return_ship_status(const ReturnRow& row, char* out, size_t cap) {
  if (row.state == SHIP_AWAITING_DECISION) snprintf(out, cap, "Incident needs a response");
  else if (row.state == SHIP_DISABLED) snprintf(out, cap, "Repairs needed");
  else if (row.state == SHIP_SAILING) {
    // Totals stay frozen, but a later state change must agree with the live
    // primary action. Explicitly identify it as a subsequent update.
    const Ship& ship = g_ships[row.shipIndex];
    if (ship.id == row.shipId && (ship.state == SHIP_ARRIVED || ship.state == SHIP_DOCKED))
      snprintf(out, cap, "Arrived / new update");
    else if (ship.id == row.shipId && ship.state == SHIP_AWAITING_DECISION) snprintf(out, cap, "New incident needs a response");
    else if (ship.id == row.shipId && ship.state == SHIP_DISABLED) snprintf(out, cap, "New repairs needed");
    else if (ship.id != row.shipId) snprintf(out, cap, "Sailing at last check");
    else { char eta[32]; eta_short(ship, eta, sizeof(eta)); snprintf(out, cap, "Arrives %s", eta); }
  } else if (g_campaign.economyVersion >= 2 && row.shipIndex < g_campaign.shipCount &&
             g_ships[row.shipIndex].id == row.shipId && merchant_goods_aboard(g_ships[row.shipIndex])) {
    snprintf(out, cap, "%s / Cargo aboard", row.port < MAX_PORTS ? PORT_DEFS[row.port].name : "Port");
  } else snprintf(out, cap, "%s / %s", row.bookedArrivals || row.legacyArrivals ? "Arrived" : "At port",
                  row.port < MAX_PORTS ? PORT_DEFS[row.port].name : "Port");
}

static void draw_return_company(const ReturnReport& report) {
  char line[160];
  // The report replaces the tabs, so its title takes their band.
  text_at(32, 50, INK, x4_return_away() ? "Welcome back" : "Company updates", 3);
  if (report.rowCount > 3) {
    snprintf(line, sizeof(line), "%u/%u", g_ui.page + 1, (report.rowCount + 2) / 3);
    text_at(588 - text_width(line), 57, INK, line);
  }
  const unsigned arrivals = report.bookedArrivals + report.legacyArrivals;
  const long long credits = report.bookedCoins + report.legacyCoins;
  if (credits) snprintf(line, sizeof(line), "%u arrived / +%lld coins received", arrivals, credits);
  else if (arrivals) snprintf(line, sizeof(line), "%u arrived / Cargo remains aboard", arrivals);
  else if (report.milestone[0]) snprintf(line, sizeof(line), "%s", report.milestone);
  else return_status_counts(report, line, sizeof(line), true);
  text_fit_at(32, 96, 736, INK, line);
  for (int row = 0; row < 3; ++row) {
    int index = x4_return_row_ship(row);
    if (index < 0 || index >= g_campaign.shipCount) continue;
    const Ship& ship = g_ships[index];
    HitRegion r = voyage_scene_region(X4_CMD_SHIP_0 + row, row);
    text_fit_at(40, r.y + 4, 380, INK, ship.name, 3);
    const bool sailing = ship.state == SHIP_SAILING;
    const bool decision = ship.state == SHIP_AWAITING_DECISION;
    const char* action = decision ? "RESPOND >" : ship.state == SHIP_DISABLED ? "REPAIR >" :
      sailing ? "WATCH >" : "OPEN SHIP >";
    text_at(r.x + r.w - 16 - text_width(action), r.y + 10, INK, action);
    uint8_t port = sailing || decision ? ship.toPort : ship.currentPort;
    snprintf(line, sizeof(line), "%s%s", sailing || decision ? "To " : "At ", port < MAX_PORTS ? PORT_DEFS[port].name : "sea");
    if (decision) snprintf(line, sizeof(line), "%s / %s", merchant_incident_label(ship),encounter_opportunity(ship.incidentType)?"Offer open":"Paused");
    if(ship.autoEncounter && !decision) {
      char duration[24];voyage_duration(ship.autoDelay,duration,sizeof(duration));
      snprintf(line,sizeof(line),"%s / +%s",encounter_auto_note(ship.autoEncounter-1),duration);
    }
    text_fit_at(40, r.y + 40, 720, INK, line);
    display.drawFastHLine(40, r.y + r.h, 720, INK);
  }
}

static void draw_return_details() {
  const ReturnReport* report = x4_return_report();
  if (!report) { card_frame("Company updates"); draw_button(CARD_FOOTER[2], "BACK"); return; }
  char line[192], money[80], title[80];
  int pages = max(1, (x4_return_row_count() + 2) / 3);
  snprintf(title, sizeof(title), "Company updates / %u of %d", g_ui.page + 1, pages);
  card_frame(title);
  if (report->bookedArrivals) {
    return_exact_money(report->bookedProfit, money, sizeof(money), true);
    snprintf(line, sizeof(line), "%s profit: %s", g_campaign.economyVersion >= 2 ? "Earlier voyage" : "Booked", money);
    text_at(40, 92, INK, line);
    return_exact_money(report->bookedCoins + (g_campaign.economyVersion >= 2 ? report->legacyCoins : 0), money, sizeof(money));
    snprintf(line, sizeof(line), "%scoins banked: %s", g_campaign.economyVersion >= 2 ? "" : "Booked ", money);
    text_at(40, 124, INK, line);
  } else {
    if (g_campaign.economyVersion >= 2) {
      if (report->legacyArrivals) snprintf(line, sizeof(line), "%lu arrival%s", (unsigned long)report->legacyArrivals, report->legacyArrivals == 1 ? "" : "s");
      else snprintf(line, sizeof(line), "%s", report->legacyNoticeCount ? "Earlier arrival" : "Fleet status");
      text_at(40, 92, INK, line);
    } else text_at(40, 92, INK, "No booked arrivals in this update");
    if (report->legacyArrivals || report->legacyCoins || report->deliveredUnits) {
      return_exact_money(report->legacyCoins, money, sizeof(money));
      snprintf(line, sizeof(line), "%s: %s", g_campaign.economyVersion >= 2 ? "Delivery payments" : "Legacy coins banked", money);
      if (report->legacyCoins || g_campaign.economyVersion < 2) text_at(40, 124, INK, line);
    } else if (report->legacyNoticeCount) text_at(40, 124, INK, "Earlier arrival amounts are unavailable");
  }
  for (int row = 0; row < 3; ++row) {
    int index = x4_return_row_ship(row);
    if (index < 0 || index >= g_campaign.shipCount) continue;
    const ReturnRow* captured = return_ship_row(*report, (uint8_t)index);
    if (!captured) continue;
    HitRegion r = voyage_card_row_region(CARD_RETURN_DETAILS, g_ui.page, row);
    text_fit_at(r.x + 8, r.y, 265, INK, g_ships[index].name);
    return_ship_status(*captured, line, sizeof(line));
    text_fit_at(r.x + 296, r.y, 432, INK, line);
    if (return_earlier_arrival(*report, (uint8_t)index) && !captured->legacyArrivals && !captured->bookedArrivals) {
      snprintf(line, sizeof(line), "Earlier arrival / receipt amounts unavailable");
    } else if (captured->bookedArrivals && (captured->legacyArrivals || captured->legacyCoins)) {
      return_exact_money(captured->legacyCoins, money, sizeof(money));
      snprintf(line, sizeof(line), "%s: %s", g_campaign.economyVersion >= 2 ? "Other delivery payments" : "Booked + legacy / legacy coins banked", money);
    } else if (captured->legacyArrivals || captured->legacyCoins ||
               (!captured->bookedArrivals && captured->deliveredUnits)) {
      return_exact_money(captured->legacyCoins, money, sizeof(money));
      if (g_campaign.economyVersion >= 2 && !captured->legacyCoins) line[0] = 0;
      else snprintf(line, sizeof(line), "%lu %sarrival%s / %s coins banked", (unsigned long)captured->legacyArrivals,
        g_campaign.economyVersion >= 2 ? "" : "legacy ", captured->legacyArrivals == 1 ? "" : "s", money);
    } else if (captured->bookedArrivals) {
      snprintf(line, sizeof(line), "%lu booked arrival%s / %lu cargo / net %+lld",
        (unsigned long)captured->bookedArrivals, captured->bookedArrivals == 1 ? "" : "s",
        (unsigned long)captured->deliveredUnits, (long long)captured->bookedProfit);
    } else if (captured->state == SHIP_SAILING)
      snprintf(line, sizeof(line), "Bound for %s", captured->destination < MAX_PORTS ? PORT_DEFS[captured->destination].name : "port");
    else snprintf(line, sizeof(line), "%s", captured->state == SHIP_AWAITING_DECISION ?
      "Read the incident and choose a response." : captured->state == SHIP_DISABLED ? "Open repairs for this ship." : "Open this ship's port.");
    text_fit_at(r.x + 8, r.y + 26, r.w - 16, INK, line);
    display.drawFastHLine(r.x + 8, r.y + r.h, r.w - 16, INK);
  }
  if (pages > 1) { draw_button(CARD_FOOTER[0], "PREV"); draw_button(CARD_FOOTER[1], "NEXT"); }
  draw_button(CARD_FOOTER[2], "BACK");
}
