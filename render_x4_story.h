// Included by render_x4.cpp after the legacy card helpers. These compositions
// share input's hit geometry and draw directly into the existing panel buffer.
#pragma once

static const char* story_scar_name(uint8_t scar) {
  static const char* const names[] = { "none", "stormwise", "corsair", "reefwise" };
  return names[scar <= SCAR_REEF ? scar : 0];
}

static void draw_story_incident() {
  if (!g_incident.active || g_incident.shipIndex >= g_campaign.shipCount || g_incident.kind >= INCIDENT_COUNT) {
    draw_incident_card();
    return;
  }
  const Ship& ship = g_ships[g_incident.shipIndex];
  const CaptainProfile* captain = sim_ship_captain(g_incident.shipIndex);
  card_frame(merchant_incident_label(ship));
  art_at(32, 94, X4_INCIDENTS[encounter_art(g_incident.kind)]);
  char line[128];
  // The captain reports the encounter; the ship and its state follow.
  if (captain) snprintf(line, sizeof(line), "Capt. %s, %s / %s", captain->name, ship.name, encounter_opportunity(g_incident.kind)?"Offer open":"Paused");
  else snprintf(line, sizeof(line), "%s / %s", ship.name, encounter_opportunity(g_incident.kind)?"Offer open / Sailing":"Voyage paused");
  text_fit_at(180, 90, 582, INK, line);

  text_fit_at(180, 128, 582, INK, encounter_report(g_incident.kind));

  if (ship.merchant.state == MERCHANT_SAILING) {
    for (int option = 0; option < encounter_choices(g_incident.kind); ++option) {
      const MerchantIncidentChoice q = sim_merchant_incident_choice((uint8_t)option);
      HitRegion r = incident_option_region(option);
      display.drawRect(r.x, r.y, r.w, r.h, INK);
      snprintf(line,sizeof(line),"%s%s%s",q.title?q.title:"Unavailable",q.benefit?" / ":"",q.benefit?q.benefit:"");
      text_fit_at(r.x + 12, r.y + 5, r.w - 24, INK, line);
      if (q.riskPercent && q.good<GOOD_COUNT) {
        snprintf(line,sizeof(line),"%u%%: +%u %s / %u%%: -1 %s",100-q.riskPercent,q.gainQuantity,GOOD_DEFS[q.gainGood].name,q.riskPercent,GOOD_DEFS[q.good].name);
      } else if(q.parcel) snprintf(line,sizeof(line),"Free / 1 slot / %s pays 25 coins",PORT_DEFS[ship.merchant.destination].name);
      else if(q.giveGood<GOOD_COUNT && q.gainGood<GOOD_COUNT)snprintf(line,sizeof(line),"Give 1 %s / Get 1 %s",GOOD_DEFS[q.giveGood].name,GOOD_DEFS[q.gainGood].name);
      else if(q.gainGood<GOOD_COUNT && q.coins<0) snprintf(line,sizeof(line),"%d coins / %s buys for %u now",-q.coins,PORT_DEFS[ship.merchant.destination].name,
        content_market_price(ship.merchant.destination,q.gainGood,false));
      else if(q.gainGood<GOOD_COUNT) snprintf(line,sizeof(line),"Free / Uses %u hold slot%s",q.gainQuantity,q.gainQuantity==1?"":"s");
      else snprintf(line,sizeof(line),"Keep all cargo / No charge");
      if(q.code!=SIM_OK) {
        if(q.riskPercent && q.good==NO_PORT)snprintf(line,sizeof(line),"Needs merchandise to risk; parcels are protected.");
        else if(q.riskPercent && q.code==SIM_NO_CAPACITY)snprintf(line,sizeof(line),"Needs 2 empty hold slots for recovered cargo.");
        else if(g_incident.kind==INCIDENT_DOUBLE && q.giveGood==NO_PORT && option==0)snprintf(line,sizeof(line),"No merchandise aboard to exchange.");
        else snprintf(line,sizeof(line),"Unavailable: %s",sim_result_text(q.code));
      }
      text_fit_at(r.x + 12, r.y + 35, r.w - 24, INK, line);
    }
    return;
  }

  // Each side label has its own line; the options retain their shared hit areas.
  text_at(34, 183, INK, "Hull");
  snprintf(line, sizeof(line), "%u", ship.condition); text_at(34, 216, INK, line);
  text_at(34, 276, INK, "Morale");
  snprintf(line, sizeof(line), "%u", ship.morale); text_at(34, 309, INK, line);
  text_at(658, 181, INK, "Risk");
  text_fit_at(658, 214, 108, INK,
              g_incident.risk == RISK_EVEN ? "Even" : risk_word(g_incident.risk));
  int loyalty = captain ? ((int)captain->loyalty - BALANCE.captainLoyaltyNeutral) / BALANCE.captainLoyaltyStep : 0;
  loyalty = max(-(int)BALANCE.captainLoyaltyMaxModifier, min((int)BALANCE.captainLoyaltyMaxModifier, loyalty));
  snprintf(line, sizeof(line), "Loy. %+d", loyalty);
  text_fit_at(658, 261, 108, INK, line);
  uint8_t wantedScar = g_incident.kind == INCIDENT_STORM ? SCAR_STORM :
    g_incident.kind == INCIDENT_PIRATES ? SCAR_PIRATE : g_incident.kind == INCIDENT_REEF ? SCAR_REEF : SCAR_NONE;
  bool scarred = false;
  if (captain && wantedScar != SCAR_NONE)
    for (int i = 0; i < captain->scarCount; ++i) scarred |= captain->scars[i] == wantedScar;
  snprintf(line, sizeof(line), "Scar %+d", scarred ? (int)BALANCE.captainScarModifier : 0);
  text_fit_at(658, 309, 108, INK, line);
  for (int option = 0; option < 3; ++option) {
    int coins = 0;
    if (sim_incident_option_cost((uint8_t)option, &coins))
      snprintf(line, sizeof(line), "%s / %+d coins", incident_option(g_incident.kind, (uint8_t)option), coins);
    else if (g_incident.kind == INCIDENT_PIRATES && option == 2)
      snprintf(line, sizeof(line), "Cannot pay: cargo will be lost");
    else if (g_incident.kind == INCIDENT_ILLNESS && option == 0 && !ship_good_quantity(ship, GOOD_MEDICINES))
      snprintf(line, sizeof(line), "No medicines: crew will be lost");
    else snprintf(line, sizeof(line), "%s", incident_option(g_incident.kind, (uint8_t)option));
    draw_button(INCIDENT_OPTIONS[option], line);
  }
}

static void story_outcome_cargo(const IncidentOutcome& outcome, char* line, size_t cap) {
  if (!outcome.cargoCount) { snprintf(line, cap, "Cargo unchanged."); return; }
  if (outcome.disabled) {
    int cargo = 0, bookings = 0;
    bool contracts = false;
    for (int i = 0; i < outcome.cargoCount; ++i) {
      const IncidentCargoChange& change = outcome.cargo[i];
      if (change.jobKind == JOB_PASSENGER) bookings -= change.quantityDelta;
      else cargo -= change.quantityDelta;
      contracts |= change.kind == CARGO_CONTRACT;
    }
    if (bookings) snprintf(line, cap, "Cargo lost: %d units. Passenger bookings forfeited: %d.", cargo, bookings);
    else snprintf(line, cap, "Cargo lost: %d units.%s", cargo, contracts ? " Contract cargo was forfeited." : "");
    return;
  }
  line[0] = 0;
  size_t used = 0;
  for (int i = 0; i < outcome.cargoCount && used + 1 < cap; ++i) {
    const IncidentCargoChange& change = outcome.cargo[i];
    const char* name = content_cargo_name(change.kind, change.jobKind, change.good, false);
    int written = snprintf(line + used, cap - used, "%s%s %+d%s", i ? "; " : "", name,
                           change.quantityDelta, change.kind == CARGO_CONTRACT && change.jobKind != JOB_PACKAGE ? " (contract)" : "");
    if (written < 0 || (size_t)written >= cap - used) break;
    used += (size_t)written;
  }
}

static const char* story_outcome_note(const IncidentOutcome& outcome) {
  if (outcome.disabled) return "Disabled; repairs required.";
  bool gainedCargo = false, usedMedicine = false;
  for (int i = 0; i < outcome.cargoCount; ++i) {
    const IncidentCargoChange& change = outcome.cargo[i];
    gainedCargo |= change.quantityDelta > 0;
    usedMedicine |= change.kind == CARGO_MARKET && change.good == GOOD_MEDICINES && change.quantityDelta < 0;
  }
  if (outcome.kind == INCIDENT_ILLNESS && outcome.option == 0)
    return usedMedicine ? "Medicines used; voyage resumed." : "No medicines aboard; voyage resumed.";
  if (outcome.kind == INCIDENT_TRADE && outcome.option < 2)
    return gainedCargo ? "Goods aboard; voyage resumed." : "Trade not completed; voyage resumed.";
  if (outcome.kind == INCIDENT_PIRATES && outcome.option == 2 && outcome.treasuryDelta == 0)
    return "Passage unpaid; voyage resumed.";
  if (outcome.kind == INCIDENT_RESCUE && outcome.option == 2 && !gainedCargo)
    return "No room for salvage; voyage resumed.";
  return "Orders carried out; voyage resumed.";
}

static void draw_story_outcome() {
  const IncidentOutcome& outcome = g_incidentOutcome;
  card_frame("Captain's Report");
  if (outcome.valid && outcome.merchant) {
    char line[192];
    art_at(40, 98, X4_INCIDENTS[encounter_art(outcome.kind)]);
    // Signed by the captain who made the call.
    const int nameWidth = outcome.captainId != NO_SHIP ?
      captain_badge(768, 96, outcome.captainName, outcome.captainTrait) - 196 - 16 : 556;
    text_fit_at(196, 96, nameWidth, INK, outcome.shipName, 3);
    text_at(196, 140, INK, "Sailing again", 3);
    text_fit_at(40,190,712,INK,sim_merchant_outcome_action());
    story_outcome_cargo(outcome,line,sizeof(line));
    if(!line[0])snprintf(line,sizeof(line),"Cargo unchanged");
    text_fit_at(40,226,712,INK,line,3);
    if(outcome.treasuryDelta)snprintf(line,sizeof(line),"Coins %+ld",(long)outcome.treasuryDelta);
    else if((outcome.kind==INCIDENT_RESCUE || outcome.kind==INCIDENT_STRANGER) && outcome.option==0)
      snprintf(line,sizeof(line),"Parcel pays 25 coins at destination.");
    else snprintf(line,sizeof(line),"No coins spent");
    text_fit_at(40,270,712,INK,line);
    if(outcome.kind>=INCIDENT_DOUBLE && outcome.option==0) {
      static const char* const after[]={"Our thanks return, a moment late.","The granary keeper is nowhere to be seen.","The boat sinks from view. The cargo remains.","At dawn, only the parcel is aboard."};
      text_fit_at(40,304,712,INK,after[outcome.kind-INCIDENT_DOUBLE]);
    }
    draw_button(CARD_FOOTER[0], "WATCH SHIP");
    draw_button(CARD_FOOTER[1], "NEXT INCIDENT", g_incident.active);
    draw_button(CARD_FOOTER[2], "BACK");
    return;
  }
  if (!outcome.valid || outcome.kind >= 8) {
    wrapped_text(40, 102, 712, "This detailed report is no longer available. Recorded voyage news remains in the company journal.", 4, 33);
  } else {
    art_at(32, 92, X4_INCIDENTS[encounter_art(outcome.kind)]);
    text_fit_at(180, 89, 516, INK, outcome.shipName);
    char line[240];
    if (outcome.captainId != NO_SHIP) {
      art_at(708, 93, X4_CAPTAINS[x4_captain_portrait(outcome.captainName, outcome.captainTrait)]);
      snprintf(line, sizeof(line), "%s / %s", outcome.captainName, content_trait_name(outcome.captainTrait));
      text_fit_at(180, 122, 516, INK, line);
    }
    snprintf(line, sizeof(line), "%s / %+ld coins", incident_option(outcome.kind, outcome.option), (long)outcome.treasuryDelta);
    text_fit_at(32, 155, 732, INK, line);
    snprintf(line, sizeof(line), "%s", story_outcome_note(outcome));
    for (uint8_t scar = SCAR_STORM; scar <= SCAR_REEF; ++scar) {
      if (!(outcome.captainNewScarMask & (1u << scar))) continue;
      size_t used = strlen(line);
      snprintf(line + used, sizeof(line) - used, " New scar: %s.", story_scar_name(scar));
      break;
    }
    text_fit_at(32, 188, 732, INK, line);
    if (outcome.disabled) snprintf(line, sizeof(line), "Recovery hull %u", outcome.hullAfter);
    else snprintf(line, sizeof(line), "Hull %u (%+d)", outcome.hullAfter, outcome.hullDelta);
    text_fit_at(32, 221, 246, INK, line);
    snprintf(line, sizeof(line), "Crew %u (%+d)", outcome.crewAfter, outcome.crewDelta);
    text_fit_at(294, 221, 214, INK, line);
    snprintf(line, sizeof(line), "Morale %u (%+d)", outcome.moraleAfter, outcome.moraleDelta);
    text_fit_at(522, 221, 242, INK, line);
    if (outcome.disabled) snprintf(line, sizeof(line), "Voyage ended");
    else {
      char duration[32];
      x4_format_time_delta(duration, sizeof(duration), outcome.delayMinutes, g_campaign.timeScale);
      snprintf(line, sizeof(line), "Time %s", duration);
    }
    size_t used = strlen(line);
    if (outcome.captainId != NO_SHIP) {
      snprintf(line + used, sizeof(line) - used, " / XP +%u / Lv %u / Loyalty %+d",
               outcome.captainXpDelta, outcome.captainLevelAfter, outcome.captainLoyaltyDelta);
    }
    text_fit_at(32, 254, 732, INK, line);
    story_outcome_cargo(outcome, line, sizeof(line));
    wrapped_text(32, 287, 732, line, 2, 33);
  }
  draw_button(CARD_FOOTER[0], "CONTINUE");
  draw_button(CARD_FOOTER[1], "NEXT INCIDENT", g_incident.active);
  draw_button(CARD_FOOTER[2], "BACK");
}

static void draw_story_arrival() {
  if (g_ui.arrivalSummaryShip < g_campaign.shipCount) {
    const Ship& vessel = g_ships[g_ui.arrivalSummaryShip];
    const MerchantVoyage& merchant = vessel.merchant;
    if (merchant.state == MERCHANT_ARRIVED && merchant.destination < MAX_PORTS &&
        merchant.destination == g_ui.arrivalSummaryPort) {
      char line[160];
      card_frame(g_ui.arrivalSummaryWon ? "Grand Charter complete" : "Arrived");
      snprintf(line, sizeof(line), "%s / %s", vessel.name, PORT_DEFS[merchant.destination].name);
      text_fit_at(40, 99, 720, INK, line, 3);
      art_at(40, 174, X4_SHIPS[vessel.classId], 2);
      const uint16_t goods = merchant_goods_aboard(vessel);
      const uint16_t parcels = merchant_packages_aboard(vessel);
      if (goods || parcels) {
        text_fit_at(304, 151, 456, INK, goods ? "Cargo aboard" : "Parcels aboard", 3);
        merchant_hold_icons(304, 198, 456, vessel);
      }
      if (vessel.voyageEarned > merchant.packageCoins || merchant.packagesDelivered) {
        const bool otherDeliveries = vessel.voyageEarned > merchant.packageCoins;
        snprintf(line, sizeof(line), "%s: %ld coins banked", otherDeliveries ? "Deliveries" : "Parcels",
          (long)(otherDeliveries ? vessel.voyageEarned : merchant.packageCoins));
        text_fit_at(304, 319, 456, INK, line);
      }
      draw_button(CARD_FOOTER[0], "OPEN SHIP");
      draw_button(CARD_FOOTER[1], "FLEET");
      draw_button(CARD_FOOTER[2], "BACK");
      return;
    }
    const BookedVoyage& booking = vessel.booking;
    if (booking.state == BOOKING_SETTLED && booking.destination < MAX_PORTS &&
        booking.destination == g_ui.arrivalSummaryPort) {
      char line[160], money[80];
      card_frame(g_ui.arrivalSummaryWon ? "Grand Charter complete" : "Arrived");
      snprintf(line, sizeof(line), "%s / %s", vessel.name, PORT_DEFS[booking.destination].name);
      text_fit_at(40, 99, 720, INK, line, 3);
      art_at(60, 180, X4_SHIPS[min(SHIP_CLASS_COUNT - 1, (int)vessel.classId)]);

      return_summary_money(booking.profit < 0 ? -(int64_t)booking.profit : booking.profit,
                           money, sizeof(money), booking.profit >= 0);
      snprintf(line, sizeof(line), "%s %s", money, booking.profit < 0 ? "loss" : "profit");
      text_fit_at(250, 170, 510, INK, line, 4);
      snprintf(line, sizeof(line), "%ld coins banked", (long)booking.arrivalCoins);
      text_fit_at(250, 234, 510, INK, line);

      // Only the completed manifest proves which earmarked materials arrived.
      // Do not infer this receipt's contribution from today's project totals.
      uint16_t manifestUnits = 0;
      for (int good = 0; good < GOOD_COUNT; ++good) manifestUnits += booking.quantities[good];
      line[0] = 0;
      if (manifestUnits == booking.quantity && booking.settledQuantity == booking.quantity &&
          booking.projectRegion < REGION_COUNT) {
        for (int good = 0; good < GOOD_COUNT; ++good) {
          if (!booking.projectQuantities[good]) continue;
          size_t used = strlen(line);
          snprintf(line + used, sizeof(line) - used, "%s+%u %s",
                   used ? " / " : "Dock cargo: ", booking.projectQuantities[good], GOOD_DEFS[good].name);
        }
      }
      if (line[0]) text_fit_at(250, 295, 510, INK, line);
      draw_button(CARD_FOOTER[0], "RETURN LOAD");
      draw_button(CARD_FOOTER[1], "FLEET");
      draw_button(CARD_FOOTER[2], "BACK");
      return;
    }
  }

  if (!g_ui.arrivalSummaryVisible || g_ui.arrivalSummaryShip >= g_campaign.shipCount || g_ui.arrivalSummaryPort >= MAX_PORTS) {
    draw_arrival_card();
    draw_button(CARD_FOOTER[1], "FLEET");
    draw_button(CARD_FOOTER[2], "BACK");
    return;
  }
  uint8_t port = g_ui.arrivalSummaryPort;
  const Ship& ship = g_ships[g_ui.arrivalSummaryShip];
  card_frame(g_ui.arrivalSummaryWon ? "Grand Charter complete" : "Arrived");
  char line[160], money[80];
  snprintf(line, sizeof(line), "%s / %s", ship.name, PORT_DEFS[port].name);
  text_fit_at(40, 99, 720, INK, line, 3);
  art_at(60, 180, X4_SHIPS[min(SHIP_CLASS_COUNT - 1, (int)ship.classId)]);
  return_summary_money(g_ui.arrivalSummaryCoins, money, sizeof(money), false);
  text_at(250, 170, INK, money, 4);
  text_at(250, 234, INK, "Coins banked");
  if (g_ui.arrivalSummaryDelivered) {
    snprintf(line, sizeof(line), "%u cargo delivered", g_ui.arrivalSummaryDelivered);
    text_fit_at(250, 295, 510, INK, line);
  }
  draw_button(CARD_FOOTER[0], "OPEN PORT");
  draw_button(CARD_FOOTER[1], "FLEET");
  draw_button(CARD_FOOTER[2], "BACK");
}

static void draw_story_captains() {
  if (g_ui.captainView == 1 && g_ui.selectedCaptain < MAX_CAPTAINS && g_captains[g_ui.selectedCaptain].active) {
    const CaptainProfile& captain = g_captains[g_ui.selectedCaptain];
    card_frame("Captain's Record");
    art_at(40, 90, X4_CAPTAINS[x4_captain_portrait(captain.name, captain.trait)], 2);
    text_fit_at(156, 89, 608, INK, captain.name, 3);
    bool atSea = captain.assignedShip < g_campaign.shipCount &&
      (g_ships[captain.assignedShip].state == SHIP_SAILING || g_ships[captain.assignedShip].state == SHIP_AWAITING_DECISION);
    char line[160];
    snprintf(line, sizeof(line), "%s / Lv %u / %s", content_trait_name(captain.trait), captain.level,
             atSea || captain.currentPort >= MAX_PORTS ? "At sea" : PORT_DEFS[captain.currentPort].name);
    text_fit_at(156, 134, 608, INK, line);
    text_fit_at(156, 168, 608, INK, trait_effect(captain.trait, captain.level));
    if (captain.level < BALANCE.maxCaptainLevel)
      snprintf(line, sizeof(line), "XP %u / next %u", captain.xp, captain.level * BALANCE.captainXpPerLevel);
    else snprintf(line, sizeof(line), "XP %u / maximum level", captain.xp);
    text_fit_at(32, 207, 370, INK, line);
    snprintf(line, sizeof(line), "Loyalty %u/100", captain.loyalty);
    text_fit_at(418, 207, 346, INK, line);
    snprintf(line, sizeof(line), "%u voyages", captain.voyages);
    text_fit_at(32, 241, 370, INK, line);
    snprintf(line, sizeof(line), "%u units delivered", captain.deliveries);
    text_fit_at(418, 241, 346, INK, line);
    snprintf(line, sizeof(line), "%u rescues", captain.rescues);
    text_fit_at(32, 275, 370, INK, line);
    snprintf(line, sizeof(line), "%u incidents survived", captain.incidentsSurvived);
    text_fit_at(418, 275, 346, INK, line);
    snprintf(line, sizeof(line), "Ship: %s", captain.assignedShip < g_campaign.shipCount ?
             g_ships[captain.assignedShip].name : "Reserve");
    text_fit_at(32, 309, 370, INK, line);
    snprintf(line, sizeof(line), "Scars: %s%s%s", captain.scarCount ? story_scar_name(captain.scars[0]) : "none",
             captain.scarCount > 1 ? ", " : "", captain.scarCount > 1 ? story_scar_name(captain.scars[1]) : "");
    text_fit_at(418, 309, 346, INK, line);
    draw_button(CARD_FOOTER[0], "CHOOSE SHIP");
    draw_button(CARD_FOOTER[1], "BACK");
    draw_button(CARD_FOOTER[2], "BACK");
    return;
  }
  if (g_ui.captainView != 2 || g_ui.selectedPort >= MAX_PORTS) { draw_captains_card(); return; }
  char recruitmentTitle[80];snprintf(recruitmentTitle,sizeof(recruitmentTitle),"Recruit / %s",PORT_DEFS[g_ui.selectedPort].name);
  card_frame(recruitmentTitle);
  display.drawFastVLine(396, 94, 250, INK);
  bool hireAffordable = false;
  for (int candidate = 0; candidate < 2; ++candidate) {
    CaptainProfile captain{};
    bool available = sim_recruit_candidate(g_ui.selectedPort, (uint8_t)candidate, &captain);
    const HitRegion& r = X4_RECRUIT_ROWS[candidate];
    int x = r.x + 14;
    if (!available) {
      text_fit_at(x, 94, r.w - 28, INK, "Office expansion", 3);
      art_at(x + 105, 148, X4_FACILITIES[0][0], 2);
      wrapped_text(x, 244, r.w - 28, "Upgrade this office to level 2 to meet a second candidate.", 3, 33);
      continue;
    }
    bool selected = candidate == g_ui.selectedCaptain;
    text_fit_at(x, 89, r.w - 44, INK, captain.name, 3);
    if (selected) display.fillCircle(r.x + r.w - 16, 107, 4, INK);
    art_at(x, 132, X4_CAPTAINS[x4_captain_portrait(captain.name, captain.trait)], 2);
    text_fit_at(x + 112, 134, r.w - 140, INK, content_trait_name(captain.trait));
    char line[128];
    snprintf(line, sizeof(line), "Level %u", captain.level);
    text_at(x + 112, 168, INK, line);
    snprintf(line, sizeof(line), "%u coins", captain.hireCost);
    text_fit_at(x + 112, 202, r.w - 140, INK, line);
    wrapped_text(x, 247, r.w - 28, trait_effect(captain.trait, captain.level), 2, 33);
    snprintf(line, sizeof(line), "XP %u / Loyalty %u", captain.xp, captain.loyalty);
    text_fit_at(x, 315, r.w - 28, INK, line);
    if (selected) hireAffordable = g_campaign.coins >= captain.hireCost;
  }
  draw_button(CARD_FOOTER[0], "HIRE", g_ui.selectedCaptain < 2 && hireAffordable);
  draw_button(CARD_FOOTER[1], "BACK");
  draw_button(CARD_FOOTER[2], "BACK");
}

static void draw_story_charters() {
  if (g_ui.selectedPort >= MAX_PORTS) { draw_charter_card(); return; }
  card_frame("Regional Charters");
  int selected = PORT_DEFS[g_ui.selectedPort].region;
  for (int region = 0; region < REGION_COUNT; ++region) {
    if (!(g_campaign.regionMask & (1u << region))) { selected = region; break; }
  }
  HitRegion focus;
  bool focused = x4_focus_region(&focus);
  for (int region = 0; region < REGION_COUNT; ++region) {
    const HitRegion& r = X4_CHARTER_ROWS[region];
    if (focused && focus.id == region && focus.x == r.x && focus.y == r.y) selected = region;
    bool charted = g_campaign.chartMask & (1u << region);
    bool open = g_campaign.regionMask & (1u << region);
    bool visited = g_campaign.visitedMask & (1u << region);
    art_at(r.x + 10, r.y + 4, X4_REGION_STAMPS[region]);
    if (visited) {
      display.drawLine(r.x + 17, r.y + 53, r.x + 22, r.y + 58, INK);
      display.drawLine(r.x + 22, r.y + 58, r.x + 33, r.y + 47, INK);
    }
    text_fit_at(r.x + 62, r.y, r.w - 74, INK, REGION_NAMES[region]);
    char line[128];
    if (open) snprintf(line, sizeof(line), "Open / %s", visited ? "visited" : "unvisited");
    else if (!charted) snprintf(line, sizeof(line), "Chart: %u coins", BALANCE.chartCost[region]);
    else snprintf(line, sizeof(line), "%u/%u / %u coins", g_campaign.charterProgress[region],
                  BALANCE.charterTarget[region], BALANCE.charterCost[region]);
    text_fit_at(r.x + 62, r.y + 31, r.w - 74, INK, line);
  }
  static const char* const goods[REGION_COUNT] = { "", "ironware", "ironware", "tea", "timber", "ironware" };
  char purpose[192];
  if (!selected) snprintf(purpose, sizeof(purpose), "Home Waters: the company's first trading harbors.");
  else snprintf(purpose, sizeof(purpose), "%s: %s between open ports.",
                x4_region((uint8_t)selected).projectName, goods[selected]);
  text_fit_at(42, 309, 712, INK, purpose);
  draw_button(CARD_FOOTER[2], "BACK");
}

[[maybe_unused]] static void draw_story_progress() {
  uint8_t stage = sim_stage();
  if (stage >= STAGE_COUNT) { draw_progress_card(); return; }
  card_frame("Company Progress");
  for (int i = 0; i < STAGE_COUNT; ++i) {
    int y = 106 + i * 43;
    bool done = i < stage || (i == STAGE_COUNT - 1 && g_campaign.won);
    if (i + 1 < STAGE_COUNT) dashed_line(50, y + 16, 50, y + 27, 4, 3);
    if (done) display.fillCircle(50, y, 16, INK);
    else {
      display.drawCircle(50, y, 16, INK);
      if (i == stage) display.drawCircle(50, y, 19, INK);
    }
    char number[4]; snprintf(number, sizeof(number), "%d", i + 1);
    text_at(center_text(34, 32, number), y - 12, done ? PAPER : INK, number);
    text_fit_at(80, y - 17, 156, INK, sim_stage_name((uint8_t)i));
  }
  display.drawFastVLine(242, 90, 253, INK);
  static const char* const guidance[STAGE_COUNT] = {
    "Load a job and plot its destination.",
    "Trade goods at an exchange.",
    "Add a ship and appoint its captain.",
    "Prepare routes and improve the fleet.",
    "Buy charts and complete regional projects.",
    "Visit six regions, then return home.",
  };
  text_fit_at(258, 89, 506, INK, g_campaign.won ? "Charter complete" : sim_stage_name(stage), 3);
  text_fit_at(258, 135, 506, INK, g_campaign.won ? "Your company may continue trading." : guidance[stage]);
  char line[192];
  snprintf(line, sizeof(line), "Open: %s", sim_stage_unlocks(stage));
  wrapped_text(258, 176, 506, line, 2, 33);
  if (stage + 1 < STAGE_COUNT) {
    char progress[64]; sim_stage_progress((uint8_t)(stage + 1), progress, sizeof(progress));
    snprintf(line, sizeof(line), "Next: %s", sim_stage_condition((uint8_t)(stage + 1)));
    wrapped_text(258, 247, 506, line, 2, 33);
    if (*progress) text_fit_at(258, 315, 506, INK, progress);
  } else {
    int visited = 0;
    for (int i = 0; i < REGION_COUNT; ++i) if (g_campaign.visitedMask & (1u << i)) ++visited;
    snprintf(line, sizeof(line), "Regions visited: %d/6", visited);
    text_at(258, 252, INK, line);
    wrapped_text(258, 288, 506, g_campaign.won ? "Returned to Lantern Quay." : "Complete the voyage at Lantern Quay.", 2, 33);
  }
  draw_button(CARD_FOOTER[2], "BACK");
}

// ---------------------------------------------------------------------------
// The Grey Passenger.
// ---------------------------------------------------------------------------

static int story_delivery_position(uint8_t chapter) {
  for (uint8_t i = 0; i < g_campaign.storyDeliveries; ++i)
    if (g_campaign.storyOrder[i] == chapter) return i;
  return -1;
}

// One whole-screen page for taking a keepsake aboard, delivering it and
// rereading it: framed like a charter, the keepsake on the left.
static void draw_atlas_page(uint8_t port);
static void draw_story_moment() {
  const uint8_t mode = x4_story_moment_mode(), chapter = x4_story_moment_chapter();
  if (mode == X4_ATLAS_PAGE) { if (chapter < MAX_PORTS) draw_atlas_page(chapter); return; }
  if (chapter >= STORY_CHAPTERS) return;
  const StoryChapter& story = STORY_CHAPTER_DEFS[chapter];
  const char* port = PORT_DEFS[story.port].name;
  char line[160];
  display.fillScreen(PAPER);
  frame_rect(10, 10, IW - 20, IH - 20, 4);
  display.drawRect(20, 20, IW - 40, IH - 40, INK);
  display.setFont(&fonts::FreeSerifBold24pt7b);
  display.setTextSize(1);
  display.setTextColor(INK);
  display.setTextDatum(lgfx::textdatum_t::top_left);
  display.drawString(story.keepsake, 44, 34);
  display.drawFastHLine(44, 86, IW - 88, INK);
  display.drawFastHLine(44, 89, IW - 88, INK);
  art_at(40, 104, X4_KEEPSAKES[chapter]);
  const char* prompt = "OK to continue";
  text_at(IW - 44 - text_width(prompt), 50, INK, prompt);

  const int x = 256, width = IW - 44 - x;
  if (mode == X4_STORY_REVEAL) {
    snprintf(line, sizeof(line), "For %s", story.recipient);
    text_fit_at(x, 112, width, INK, line, 3);
    snprintf(line, sizeof(line), "at %s", port);
    text_fit_at(x, 158, width, INK, line, 3);
    snprintf(line, sizeof(line), "+%u coins on delivery", sim_story_reward());
    text_fit_at(x, 216, width, INK, line);
    wrapped_text(x, 252, width, "Delivered automatically when this ship reaches port. It can't be sold or dropped.", 2, 30);
    snprintf(line, sizeof(line), "%s nods, and is gone before you look up.",
             story_courier_name(g_campaign.storyDeliveries));
    wrapped_text(x, 330, width, line, 2, 30);
  } else {
    // Under the keepsake: where it went and, on delivery, what it paid.
    const int position = story_delivery_position(chapter);
    text_at(40 + (192 - text_width(port)) / 2, 306, INK, port);
    if (mode == X4_STORY_DELIVERED) snprintf(line, sizeof(line), "+%u coins", STORY_REWARDS[max(0, position)]);
    else snprintf(line, sizeof(line), "Delivered");
    text_at(40 + (192 - text_width(line)) / 2, 336, INK, line);
    wrapped_text(x, 104, width, story.passage, 9, 29);
    if (position >= 0 && position < STORY_CHAPTERS - 1) {
      display.drawFastHLine(44, 378, 120, INK);
      wrapped_text(44, 390, IW - 88, STORY_THREAD_LINES[position], 2, 29);
    }
  }
}

static void draw_keepsakes_card() {
  card_frame("Keepsakes");
  char where[64];
  for (int row = 0; row < CARD_VISIBLE_ROWS; ++row) {
    int entry = g_ui.page * CARD_VISIBLE_ROWS + row;
    if (entry >= g_campaign.storyDeliveries) break;
    const StoryChapter& story = STORY_CHAPTER_DEFS[g_campaign.storyOrder[entry]];
    snprintf(where, sizeof(where), "%s, %s", story.recipient, PORT_DEFS[story.port].name);
    row_text(row, story.keepsake, where);
  }
  if (g_campaign.storyDeliveries > CARD_VISIBLE_ROWS) {
    draw_button(CARD_FOOTER[0], "PREV");
    draw_button(CARD_FOOTER[1], "NEXT");
  }
  draw_button(CARD_FOOTER[2], "CLOSE");
}

// The Sea Atlas: one list page per region, then a whole-screen page per
// charted port drawn by draw_story_moment.

static void atlas_goods(char* out, size_t cap, const char* label, const uint8_t* goods) {
  if (goods[0] == goods[1] || goods[1] >= GOOD_COUNT) snprintf(out, cap, "%s %s", label, GOOD_DEFS[goods[0]].name);
  else snprintf(out, cap, "%s %s and %s", label, GOOD_DEFS[goods[0]].name, GOOD_DEFS[goods[1]].name);
}

static void draw_atlas_page(uint8_t port) {
  const PortDef& def = PORT_DEFS[port];
  char line[160], wants[80];
  display.fillScreen(PAPER);
  frame_rect(10, 10, IW - 20, IH - 20, 4);
  display.drawRect(20, 20, IW - 40, IH - 40, INK);
  display.setFont(&fonts::FreeSerifBold24pt7b);
  display.setTextSize(1);
  display.setTextColor(INK);
  display.setTextDatum(lgfx::textdatum_t::top_left);
  display.drawString(def.name, 44, 34);
  const char* prompt = "OK to continue";
  text_at(IW - 44 - text_width(prompt), 50, INK, prompt);
  display.drawFastHLine(44, 86, IW - 88, INK);
  display.drawFastHLine(44, 89, IW - 88, INK);
  art_at(72, 108, X4_ATLAS_LANDMARKS[port]);
  const char* region = REGION_NAMES[def.region];
  text_at(40 + (192 - text_width(region)) / 2, 232, INK, region);
  wrapped_text(256, 104, IW - 44 - 256, ATLAS_ENTRIES[port].text, 8, 29);
  display.drawFastHLine(44, 352, IW - 88, INK);
  atlas_goods(line, sizeof(line), "Makes", def.produced);
  atlas_goods(wants, sizeof(wants), "Wants", def.demanded);
  snprintf(line + strlen(line), sizeof(line) - strlen(line), " / %s", wants);
  text_fit_at(44, 364, IW - 88, INK, line);
  const uint8_t ship = g_atlas.ship[port];
  if (ship == ATLAS_HOME) snprintf(line, sizeof(line), "The company's home port");
  else if (ship < g_campaign.shipCount && g_atlas.voyage[port])
    snprintf(line, sizeof(line), "Charted by the %s, day %u", g_ships[ship].name, g_atlas.day[port]);
  else snprintf(line, sizeof(line), "Charted on day %u", g_atlas.day[port]);
  text_fit_at(44, 400, IW - 88, INK, line);
}

static void draw_atlas_card() {
  const uint8_t region = g_ui.page < REGION_COUNT ? g_ui.page : 0;
  char title[64], value[48];
  snprintf(title, sizeof(title), "Sea Atlas / %s", REGION_NAMES[region]);
  card_frame(title);
  if (sim_atlas_region_count(region) == 4) art_at(CARD_X + CARD_W - 64, CARD_Y - 6, X4_REGION_STAMPS[region]);
  for (int row = 0; row < 4; ++row) {
    const uint8_t port = (uint8_t)(region * 4 + row);
    const bool charted = sim_atlas_charted(port);
    if (charted && g_atlas.ship[port] == ATLAS_HOME) snprintf(value, sizeof(value), "Home port");
    else if (charted) snprintf(value, sizeof(value), "Day %u", g_atlas.day[port]);
    else snprintf(value, sizeof(value), "Not yet charted");
    row_text(row, PORT_DEFS[port].name, value, INK, false, charted);
  }
  snprintf(title, sizeof(title), "Charted %u of 24%s", sim_atlas_count(), sim_atlas_complete() ? " / Chartmakers" : "");
  row_text(5, title);
  draw_button(CARD_FOOTER[0], "PREV");
  draw_button(CARD_FOOTER[1], "NEXT");
  draw_button(CARD_FOOTER[2], "CLOSE");
}
