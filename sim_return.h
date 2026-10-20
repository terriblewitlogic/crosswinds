// Included by sim.cpp: durable return accounting is independent of UI notices.
static uint32_t return_current_milestones() {
  uint32_t bits = 0, units = g_campaign.deliveredUnits;
  if (units >= 4) bits |= RETURN_MILESTONE_TRADE;
  if (units >= 12) bits |= RETURN_MILESTONE_NEW_ALBION;
  if (units >= 24) bits |= RETURN_MILESTONE_SECOND_SHIP;
  if (units >= 36) bits |= RETURN_MILESTONE_FITTINGS;
  if (units >= 48) bits |= RETURN_MILESTONE_PROJECTS;
  for (uint8_t region = 1; region < REGION_COUNT; ++region) {
    if (g_campaign.regionMask & (1u << region)) bits |= RETURN_MILESTONE_REGION_BASE << region;
    uint16_t target = sim_project_target(region);
    if (target && g_campaign.projectCargo[region][0] >= target &&
        g_campaign.projectCargo[region][1] >= target)
      bits |= RETURN_MILESTONE_PROJECT_READY_BASE << region;
  }
  if (g_campaign.won) bits |= RETURN_MILESTONE_GRAND_CHARTER;
  return bits;
}
static void return_reconcile_milestones() {
  g_campaign.returnMilestones |= return_current_milestones();
}
static void return_ack_retained_capabilities() {
  if (!g_campaign.economyVersion || !g_campaign.legacyFeatureMask) return;
  uint16_t retained = g_campaign.legacyFeatureMask;
  uint32_t bits = 0;
  if (retained & (1u << FEATURE_MARKET)) bits |= RETURN_MILESTONE_TRADE;
  // The old yard opens this purchase only with the actual saved-stage bypass
  // in sim_ship_purchase_block. A visible fleet or captain tool alone does not.
  if ((retained & (1u << FEATURE_YARD)) && g_campaign.stageSeen >= 2)
    bits |= RETURN_MILESTONE_SECOND_SHIP;
  if ((retained & (1u << FEATURE_YARD)) && (retained & (1u << FEATURE_FITTINGS)))
    bits |= RETURN_MILESTONE_FITTINGS;
  // Below the new threshold, an already-open Mariner Rest proves prior access.
  // Above it, a pending opening may be genuinely new and must remain unread.
  if (g_campaign.deliveredUnits < 12 && g_ports[3].unlocked)
    bits |= RETURN_MILESTONE_NEW_ALBION;
  // Old charter tools are not the new 48-delivery project opportunity, nor
  // proof that any construction materials or new region have been earned.
  g_campaign.returnMilestones |= bits;
  g_campaign.returnMilestonesSeen |= bits;
}
static void return_record_booked(Ship& ship, int32_t banked, int32_t profit, uint32_t cargo) {
  ReturnTally& t = ship.returnLedger.total;
  ++t.revision; t.bookedCoins += banked; t.bookedProfit += profit; t.bookedCargo += cargo;
  return_reconcile_milestones();
}
static void return_record_legacy_credit(Ship& ship, int32_t banked, uint32_t cargo) {
  ReturnTally& t = ship.returnLedger.total;
  ++t.revision; t.legacyCoins += banked; t.legacyCargo += cargo;
  return_reconcile_milestones();
}
static void return_record_arrival(Ship& ship, bool booked) {
  ReturnTally& t = ship.returnLedger.total;
  ++t.revision;
  if (booked) ++t.bookedArrivals; else ++t.legacyArrivals;
  return_reconcile_milestones();
}
void sim_return_migrate_legacy() {
  // Called only after a coherent pre-v9 company is fully loaded. Old notice
  // text cannot recover cash; only extant unread signed receipts are seeded.
  for (uint8_t i = 0; i < g_campaign.shipCount; ++i) {
    Ship& ship = g_ships[i]; ship.returnLedger = {};
    const BookedVoyage& b = ship.booking;
    if (b.state != BOOKING_SETTLED || !b.receiptUnread) continue;
    ReturnTally& t = ship.returnLedger.total;
    t.revision = 1; t.bookedArrivals = 1; t.bookedCargo = b.settledQuantity;
    t.bookedCoins = b.arrivalCoins; t.bookedProfit = b.profit;
  }
  // Migration does not celebrate capabilities the player already earned.
  g_campaign.returnMilestones = g_campaign.returnMilestonesSeen = return_current_milestones();
}
static void return_milestone_text(ReturnReport& r) {
  uint32_t bits = r.milestoneMask;
  if (bits & RETURN_MILESTONE_GRAND_CHARTER) {
    snprintf(r.milestone, sizeof(r.milestone), "Grand Charter complete"); return;
  }
  for (int region = REGION_COUNT - 1; region >= 1; --region)
    if (bits & (RETURN_MILESTONE_REGION_BASE << region)) {
      snprintf(r.milestone, sizeof(r.milestone), "%s ports are open", REGION_NAMES[region]); return;
    }
  for (int region = REGION_COUNT - 1; region >= 1; --region)
    if (bits & (RETURN_MILESTONE_PROJECT_READY_BASE << region)) {
      snprintf(r.milestone, sizeof(r.milestone), "%s: construction cargo delivered", REGION_NAMES[region]); return;
    }
  const char* text = bits & RETURN_MILESTONE_PROJECTS ? "Company projects are available" :
    bits & RETURN_MILESTONE_FITTINGS ? "Ship fittings are available" :
    bits & RETURN_MILESTONE_SECOND_SHIP ? "A second crewed ship is available" :
    bits & RETURN_MILESTONE_NEW_ALBION ? "Mariner Rest is open for trade" :
    bits & RETURN_MILESTONE_TRADE ? (g_campaign.economyVersion >= 2 ? "First deliveries complete" : "Booked trade is available") : "";
  snprintf(r.milestone, sizeof(r.milestone), "%s", text);
}
static bool return_tally_equal(const ReturnTally& a, const ReturnTally& b) {
  return a.revision == b.revision && a.bookedArrivals == b.bookedArrivals &&
    a.legacyArrivals == b.legacyArrivals && a.bookedCargo == b.bookedCargo &&
    a.legacyCargo == b.legacyCargo && a.bookedCoins == b.bookedCoins &&
    a.bookedProfit == b.bookedProfit && a.legacyCoins == b.legacyCoins;
}
static uint32_t return_report_fingerprint(const ReturnReport& r) {
  uint32_t h = 2166136261u;
  auto mix = [&](uint64_t n) { for (int i = 0; i < 8; ++i) { h ^= (uint8_t)n; h *= 16777619u; n >>= 8; } };
  mix(r.companySeed); mix(r.companyEpoch); mix(r.rowCount); mix(r.noticeCount); mix(r.milestoneMask);
  for (uint8_t i = 0; i < r.rowCount && i < MAX_SHIPS; ++i) {
    const ReturnRow& row = r.rows[i]; const ReturnTally& t = row.mark;
    mix(row.shipIndex); mix(row.shipId); mix(row.bookingId); mix(t.revision);
    mix(t.bookedArrivals); mix(t.legacyArrivals); mix(t.bookedCargo); mix(t.legacyCargo);
    mix((uint64_t)t.bookedCoins); mix((uint64_t)t.bookedProfit); mix((uint64_t)t.legacyCoins);
  }
  for (uint8_t i = 0; i < r.noticeCount && i < MAX_ATTENTION; ++i) {
    const auto& n = r.notices[i]; mix(n.id); mix(n.worldMinute); mix(n.kind);
    mix(n.shipIndex); mix(n.port); mix(n.region);
  }
  return h ? h : 1;
}
uint32_t sim_return_epoch() { return returnCompanyEpoch; }
ReturnReport sim_return_report() {
  ReturnReport r{}; r.companySeed = g_campaign.seed; r.companyEpoch = returnCompanyEpoch;
  r.preferredShip = NO_SHIP; r.rowCount = g_campaign.shipCount;
  r.milestoneMask = g_campaign.returnMilestones & ~g_campaign.returnMilestonesSeen;
  for (uint8_t i = 0; i < g_campaign.shipCount; ++i) {
    const Ship& ship = g_ships[i]; ReturnRow& row = r.rows[i];
    const ReturnTally& t = ship.returnLedger.total; const ReturnTally& seen = ship.returnLedger.seen;
    row.mark = t; row.shipIndex = i; row.shipId = ship.id; row.state = ship.state;
    row.port = ship.currentPort; row.destination = ship.state == SHIP_SAILING ?
      (ship.merchant.state == MERCHANT_SAILING ? ship.merchant.destination : ship.booking.state == BOOKING_SAILING ? ship.booking.destination : ship.toPort) : ship.currentPort;
    row.bookingId = ship.booking.state == BOOKING_SETTLED ? ship.booking.id : 0;
    row.bookedArrivals = t.bookedArrivals - seen.bookedArrivals;
    row.legacyArrivals = t.legacyArrivals - seen.legacyArrivals;
    row.deliveredUnits = t.bookedCargo - seen.bookedCargo + t.legacyCargo - seen.legacyCargo;
    row.bookedCoins = t.bookedCoins - seen.bookedCoins; row.bookedProfit = t.bookedProfit - seen.bookedProfit;
    row.legacyCoins = t.legacyCoins - seen.legacyCoins;
    r.bookedArrivals += row.bookedArrivals; r.legacyArrivals += row.legacyArrivals;
    r.deliveredUnits += row.deliveredUnits; r.bookedCoins += row.bookedCoins;
    r.bookedProfit += row.bookedProfit; r.legacyCoins += row.legacyCoins;
    if (t.revision != seen.revision) r.eventShipMask |= (uint16_t)(1u << i);
    bool decision = ship.state == SHIP_AWAITING_DECISION || incident_references_ship(g_incident, i);
    for (uint8_t q = 0; q < g_incidentQueueCount; ++q) decision |= incident_references_ship(g_incidentQueue[q], i);
    if (decision) { r.decisionShipMask |= (uint16_t)(1u << i); ++r.decisionCount; }
    if (ship.state == SHIP_DISABLED) { r.repairShipMask |= (uint16_t)(1u << i); ++r.repairCount; }
    if (ship.state == SHIP_SAILING) ++r.sailingCount;
    if (!decision && ship_at_port(ship) && ship.state != SHIP_DISABLED && ship.condition &&
        (g_campaign.economyVersion >= 2 || !ship.cargoCount) && sim_ship_captain(i)) {
      uint8_t destinations[MAX_PORTS];
      uint8_t count = sim_voyage_destinations(i, destinations, MAX_PORTS);
      bool ready = g_campaign.economyVersion >= 2;
      for (uint8_t p = 0; p < count && !ready; ++p)
        for (uint8_t g = 0; g < 2 && !ready; ++g) {
          uint8_t good = PORT_DEFS[ship.currentPort].produced[g];
          if (g && good == PORT_DEFS[ship.currentPort].produced[0]) continue;
          for (uint8_t quantity = (uint8_t)sim_ship_capacity(ship); quantity && !ready; --quantity)
            ready = sim_voyage_offer(i, destinations[p], VOYAGE_FREIGHT, good, quantity).code == SIM_OK;
        }
      if (ready) {
        r.readyShipMask |= (uint16_t)(1u << i); ++r.readyCount;
      }
    }
  }
  // Old arrival notices retain their identity, without inventing lost receipt
  // amounts. Current problems remain actionable even after their notice is read.
  for (uint8_t i = 0; i < g_attentionCount; ++i) {
    const AttentionEntry& a = g_attention[i];
    if (!a.unread || a.resolved) continue;
    bool relevant = a.kind == ATTENTION_INCIDENT || a.kind == ATTENTION_DISABLED;
    bool legacy = a.kind == ATTENTION_ARRIVAL && a.shipIndex < g_campaign.shipCount &&
      g_ships[a.shipIndex].booking.state == BOOKING_NONE &&
      g_ships[a.shipIndex].returnLedger.total.legacyArrivals == 0;
    if (!relevant && !legacy) continue;
    if (legacy) { ++r.legacyNoticeCount; r.eventShipMask |= (uint16_t)(1u << a.shipIndex); }
    r.notices[r.noticeCount++] = {a.id, a.worldMinute, a.shipIndex, a.kind, a.port, a.region};
  }
  uint16_t preferred = r.decisionShipMask ? r.decisionShipMask : r.repairShipMask ? r.repairShipMask :
    (uint16_t)(r.readyShipMask & r.eventShipMask) ? (uint16_t)(r.readyShipMask & r.eventShipMask) : r.readyShipMask;
  for (uint8_t i = 0; i < g_campaign.shipCount; ++i) if (preferred & (1u << i)) { r.preferredShip = i; break; }
  if (r.preferredShip == NO_SHIP)
    for (uint8_t i = 0; i < g_campaign.shipCount; ++i)
      if ((r.eventShipMask & (1u << i)) && ship_at_port(g_ships[i])) { r.preferredShip = i; break; }
  if (r.preferredShip == NO_SHIP)
    for (uint8_t i = 0; i < g_campaign.shipCount; ++i) if (g_ships[i].state == SHIP_SAILING) { r.preferredShip = i; break; }
  r.meaningful = r.eventShipMask || r.milestoneMask || r.noticeCount;
  return_milestone_text(r); r.fingerprint = return_report_fingerprint(r); return r;
}
bool sim_return_has_updates() {
  // Avoid route quotation when merely checking the banner/idle trigger.
  if (g_campaign.returnMilestones & ~g_campaign.returnMilestonesSeen) return true;
  for (uint8_t i = 0; i < g_campaign.shipCount; ++i)
    if (g_ships[i].returnLedger.total.revision != g_ships[i].returnLedger.seen.revision) return true;
  for (uint8_t i = 0; i < g_attentionCount; ++i) {
    const auto& a = g_attention[i];
    if (a.unread && !a.resolved && (a.kind == ATTENTION_INCIDENT || a.kind == ATTENTION_DISABLED ||
        (a.kind == ATTENTION_ARRIVAL && a.shipIndex < g_campaign.shipCount &&
         g_ships[a.shipIndex].booking.state == BOOKING_NONE && !g_ships[a.shipIndex].returnLedger.total.legacyArrivals))) return true;
  }
  return false;
}
SimResult sim_ack_return_report(const ReturnReport& r) {
  if (r.companySeed != g_campaign.seed || r.companyEpoch != returnCompanyEpoch ||
      r.rowCount > g_campaign.shipCount || r.noticeCount > MAX_ATTENTION ||
      !r.fingerprint || r.fingerprint != return_report_fingerprint(r) ||
      (r.milestoneMask & ~g_campaign.returnMilestones)) return SIM_STALE_PREVIEW;
  for (uint8_t i = 0; i < r.rowCount; ++i) {
    const auto& row = r.rows[i];
    if (row.shipIndex != i || row.shipId != g_ships[i].id || row.mark.revision > g_ships[i].returnLedger.total.revision)
      return SIM_STALE_PREVIEW;
  }
  ReturnTally before[MAX_SHIPS];
  for (uint8_t i = 0; i < g_campaign.shipCount; ++i) before[i] = g_ships[i].returnLedger.seen;
  uint8_t unread[MAX_ATTENTION]; for (uint8_t i = 0; i < g_attentionCount; ++i) unread[i] = g_attention[i].unread;
  uint32_t milestones = g_campaign.returnMilestonesSeen;
  bool changed = false;
  for (uint8_t i = 0; i < r.rowCount; ++i) {
    ReturnTally& seen = g_ships[i].returnLedger.seen;
    if (r.rows[i].mark.revision > seen.revision) { seen = r.rows[i].mark; changed = true; }
  }
  g_campaign.returnMilestonesSeen |= r.milestoneMask;
  changed |= milestones != g_campaign.returnMilestonesSeen;
  for (uint8_t i = 0; i < r.noticeCount; ++i) for (uint8_t j = 0; j < g_attentionCount; ++j) {
    const auto& m = r.notices[i]; auto& a = g_attention[j];
    if (a.id == m.id && a.worldMinute == m.worldMinute && a.kind == m.kind &&
        a.shipIndex == m.shipIndex && a.port == m.port && a.region == m.region && a.unread) {
      a.unread = 0; changed = true;
    }
  }
  if (!changed) return SIM_OK;
  if (save_write()) return SIM_OK;
  for (uint8_t i = 0; i < g_campaign.shipCount; ++i) g_ships[i].returnLedger.seen = before[i];
  for (uint8_t i = 0; i < g_attentionCount; ++i) g_attention[i].unread = unread[i];
  g_campaign.returnMilestonesSeen = milestones;
  return SIM_SAVE_FAILED;
}
SimResult sim_ack_arrival_receipt(uint8_t i, uint32_t bookingId) {
  if (i >= g_campaign.shipCount) return SIM_INVALID;
  Ship& ship = g_ships[i]; BookedVoyage& b = ship.booking;
  MerchantVoyage& merchant = ship.merchant;
  if (bookingId && merchant.id == bookingId && merchant.state == MERCHANT_ARRIVED) {
    ReturnTally before = ship.returnLedger.seen;
    const ReturnTally& total = ship.returnLedger.total;
    uint8_t unread = merchant.receiptUnread;
    // This receipt may acknowledge only this journey's exact package credits,
    // not an earlier unread voyage or retained contract delivery.
    if (total.legacyArrivals - before.legacyArrivals == 1 &&
        total.bookedArrivals == before.bookedArrivals && total.bookedCargo == before.bookedCargo &&
        total.bookedCoins == before.bookedCoins && total.bookedProfit == before.bookedProfit &&
        total.legacyCargo - before.legacyCargo == merchant.packagesDelivered &&
        total.legacyCoins - before.legacyCoins == merchant.packageCoins)
      ship.returnLedger.seen = total;
    merchant.receiptUnread = 0;
    if (!unread && return_tally_equal(before, ship.returnLedger.seen)) return SIM_OK;
    if (save_write()) return SIM_OK;
    merchant.receiptUnread = unread; ship.returnLedger.seen = before;
    return SIM_SAVE_FAILED;
  }
  if (!bookingId || b.id != bookingId || b.state != BOOKING_SETTLED) return SIM_STALE_PREVIEW;
  ReturnTally before = ship.returnLedger.seen;
  uint8_t unread = b.receiptUnread;
  const ReturnTally& t = ship.returnLedger.total;
  // Only the exact sole pending booking can acknowledge this ship's ledger.
  // A receipt cannot consume older grouped results or later legacy credits.
  if (t.bookedArrivals - before.bookedArrivals == 1 && t.legacyArrivals == before.legacyArrivals &&
      t.legacyCargo == before.legacyCargo && t.legacyCoins == before.legacyCoins &&
      t.bookedCargo - before.bookedCargo == b.settledQuantity &&
      t.bookedCoins - before.bookedCoins == b.arrivalCoins && t.bookedProfit - before.bookedProfit == b.profit)
    ship.returnLedger.seen = t;
  b.receiptUnread = 0;
  if (unread == b.receiptUnread && return_tally_equal(before, ship.returnLedger.seen)) return SIM_OK;
  if (save_write()) return SIM_OK;
  b.receiptUnread = unread; ship.returnLedger.seen = before;
  return SIM_SAVE_FAILED;
}
