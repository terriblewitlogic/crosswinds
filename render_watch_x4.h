#pragma once

// A port name centred under its route dot, kept inside the safe area.
static void watch_endpoint_label(int dotX, const char* name) {
  char fitted[64]; fit_text(name, fitted, sizeof(fitted), 300);
  const int width = text_width(fitted);
  text_at(max(24, min(776 - width, dotX - width / 2)), 374, INK, fitted);
}

static void draw_watch_journey() {
  if (g_ui.selectedShip >= g_campaign.shipCount) return;
  const Ship& ship = g_ships[g_ui.selectedShip];
  WatchJourney journey = watch_journey(ship);
  // Brief, harmless visual sightings; no modal or extra trading text.
  if(ship.sighting && ship.sighting<=3 && ship.state==SHIP_SAILING && ship.legElapsedMinutes>=ship.legTotalMinutes/2 &&
     ship.legElapsedMinutes<ship.legTotalMinutes/2+100) {
    if(ship.sighting==3) {display.drawLine(492,272,540,272,INK);display.drawLine(496,276,536,276,INK);display.drawLine(512,232,512,272,INK);display.drawTriangle(514,236,514,267,537,265,INK);}
    else if(ship.sighting==2) {display.drawLine(642,146,662,153,INK);display.drawLine(662,153,682,142,INK);display.drawLine(648,146,662,150,INK);display.drawLine(662,150,677,142,INK);}
    else for(int i=0;i<5;++i)display.drawCircle(390+i*20,340+(i%2)*6,3,INK);
  }
  char line[96];
  snprintf(line, sizeof(line), "%s to %s", ship.name, PORT_DEFS[journey.destination].name);
  if (const CaptainProfile* captain = sim_ship_captain(g_ui.selectedShip))
    captain_badge(768, 152, captain->name, captain->trait);
  text_fit_at(32, 102, 736, INK, line, 3);
  if (ship.state == SHIP_DOCKED || ship.state == SHIP_ARRIVED) snprintf(line, sizeof(line), "Arrived");
  else if (ship.state == SHIP_AWAITING_DECISION) snprintf(line, sizeof(line), "%s", merchant_incident_label(ship));
  else if (g_campaign.paused) eta_short(ship, line, sizeof(line));
  else {
    uint32_t seconds = x4_real_seconds(journey.remaining, g_campaign.timeScale);
    if (seconds >= 3600) snprintf(line, sizeof(line), "%lu hr %lu min", (unsigned long)(seconds / 3600), (unsigned long)(seconds / 60 % 60));
    else snprintf(line, sizeof(line), "%lu min %02lu sec", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
  }
  text_fit_at(32, 151, ship.state == SHIP_AWAITING_DECISION ? 736 : 380, INK, line, ship.state == SHIP_AWAITING_DECISION ? 3 : 5);
  draw_progress(34, 211, 282, journey.fraction / 100);
  if (ship.state == SHIP_AWAITING_DECISION) {
    char remaining[24];voyage_duration(ship.encounterWait?ship.encounterWait:300,remaining,sizeof(remaining));
    snprintf(line,sizeof(line),encounter_opportunity(ship.incidentType)?"Offer expires in %s":"Safe passage in %s",remaining);
  }
  else snprintf(line, sizeof(line), "%u%% complete", journey.fraction / 100);
  text_at(34, 231, INK, line);

  // Port art stands beside its route dot so the boat never covers it.
  art_at(24, 263, X4_LANDMARKS[journey.origin], 2);
  art_at(648, 223, X4_LANDMARKS[journey.destination], 2);
  // Two coastal endpoints and a curved sea trail. Completed travel is solid;
  // the remaining dots lead toward the destination without crossing land.
  int px, py; watch_ship_point(0, &px, &py);
  int dotX = px, dotY = py;
  for (int step = 1; step <= 256; ++step) {
    int x, y; uint16_t fraction = (uint16_t)(step * 10000 / 256);
    watch_ship_point(fraction, &x, &y);
    if (fraction <= journey.fraction) {
      display.drawLine(px, py, x, y, INK);
      display.drawLine(px, py + 1, x, y + 1, INK);
    }
    // Dot spacing is measured on the curve, independent of its slope.
    if ((x-dotX)*(x-dotX) + (y-dotY)*(y-dotY) >= 81) {
      if (fraction > journey.fraction) display.fillCircle(x, y, 2, INK);
      dotX = x; dotY = y;
    }
    px = x; py = y;
  }
  display.fillCircle(160, 358, 6, PAPER); display.drawCircle(160, 358, 6, INK);
  display.fillCircle(680, 344, 6, PAPER); display.drawCircle(680, 344, 6, INK);
  int shipX, shipY; watch_ship_point(journey.fraction, &shipX, &shipY);
  const X4Art& vessel=X4_SHIPS[ship.classId<5?ship.classId:0];
  native_art_at(shipX-vessel.width/2,shipY-vessel.height,vessel);
  watch_endpoint_label(160, PORT_DEFS[journey.origin].name);
  watch_endpoint_label(680, PORT_DEFS[journey.destination].name);
}
