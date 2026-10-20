#include "game.h"
#include "encounter_content.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <new>

static void compact_contracts(uint8_t port);  // keeps each port's contract board gap-free
static void gain_captain_xp(Ship& s, uint16_t amount);
static void settle_booking(Ship& ship);
static void return_record_booked(Ship& ship, int32_t banked, int32_t profit, uint32_t cargo);
static void return_record_legacy_credit(Ship& ship, int32_t banked, uint32_t cargo);
static void return_record_arrival(Ship& ship, bool booked);
static void return_reconcile_milestones();
static void return_ack_retained_capabilities();
static void merchant_refresh_packages(uint8_t port);
static uint32_t returnCompanyEpoch = 0;

Campaign g_campaign;
PortState g_ports[MAX_PORTS];
Contract g_contracts[MAX_PORTS][MAX_CONTRACTS_PER_PORT];
Ship g_ships[MAX_SHIPS];
Incident g_incident;
IncidentOutcome g_incidentOutcome;
Incident g_incidentQueue[MAX_INCIDENTS];
uint8_t g_incidentQueueCount = 0;
JournalEntry g_journal[MAX_JOURNAL];
AttentionEntry g_attention[MAX_ATTENTION];
CaptainProfile g_captains[MAX_CAPTAINS];
uint8_t g_journalHead = 0;
uint8_t g_journalCount = 0;
uint8_t g_attentionCount = 0;
RoutePreparation g_routePreparation;
UiState g_ui;

static int32_t available_unreserved_debt(int32_t debt) {
  for (int i = 0; i < g_campaign.shipCount; ++i)
    if (g_ships[i].booking.state == BOOKING_SAILING) debt -= g_ships[i].booking.withheld;
  return debt > 0 ? debt : 0;
}

// One current arrival receipt per ship. These UI records do not enter saves;
// a new voyage or a reload leaves older notices as links to the ship's fleet row.
struct ArrivalReceipt {
  uint32_t worldMinute;
  int32_t coins;
  uint8_t port, delivered, won, valid;
};
static ArrivalReceipt arrivalReceipts[MAX_SHIPS];

static void show_arrival_receipt(uint8_t shipIndex) {
  if (shipIndex < g_campaign.shipCount && g_ships[shipIndex].merchant.state == MERCHANT_ARRIVED) {
    const Ship& ship = g_ships[shipIndex];
    g_ui.arrivalSummaryVisible = 1; g_ui.arrivalSummaryShip = shipIndex;
    g_ui.arrivalSummaryPort = ship.merchant.destination;
    g_ui.arrivalSummaryCoins = ship.merchant.packageCoins;
    g_ui.arrivalSummaryDelivered = (uint8_t)min((uint16_t)255, ship.merchant.packagesDelivered);
    g_ui.arrivalSummaryWon = arrivalReceipts[shipIndex].won;
    return;
  }
  if (shipIndex < g_campaign.shipCount && g_ships[shipIndex].booking.state == BOOKING_SETTLED) {
    const Ship& ship = g_ships[shipIndex];
    g_ui.arrivalSummaryVisible = 1; g_ui.arrivalSummaryShip = shipIndex;
    g_ui.arrivalSummaryPort = ship.booking.destination;
    g_ui.arrivalSummaryCoins = ship.booking.arrivalCoins;
    g_ui.arrivalSummaryDelivered = ship.booking.settledQuantity;
    g_ui.arrivalSummaryWon = arrivalReceipts[shipIndex].won;
    return;
  }
  const ArrivalReceipt& receipt = arrivalReceipts[shipIndex];
  g_ui.arrivalSummaryVisible = receipt.valid;
  g_ui.arrivalSummaryShip = shipIndex;
  g_ui.arrivalSummaryPort = receipt.port;
  g_ui.arrivalSummaryCoins = receipt.coins;
  g_ui.arrivalSummaryDelivered = receipt.delivered;
  g_ui.arrivalSummaryWon = receipt.won;
}

static const char* const SHIP_NAMES[] = {
  "Petrel", "Wanderer", "Pilgrim", "Mercy", "Albatross",
  "Coralie", "Wayfarer", "Providence", "Swift", "Endeavour",
  "Morning Tide", "Argosy"
};
static const char* const CAPTAIN_NAMES[] = {
  "Mara Flint", "Elias Reed", "Ada Vane", "Jonah Pike", "Nadia Vale", "Silas Grey",
  "Elena Ward", "Basil Rook", "Anika Shaw", "Jules March", "Rafi North", "Maeve Bell"
};

static bool ship_at_port(const Ship& s) {
  return s.state == SHIP_DOCKED || s.state == SHIP_ARRIVED || s.state == SHIP_DISABLED;
}

static const CaptainProfile* ship_captain_profile(const Ship& s) {
  return s.captainId < MAX_CAPTAINS && g_captains[s.captainId].active ?
    &g_captains[s.captainId] : nullptr;
}

static uint8_t captain_trait(const Ship& s) {
  const CaptainProfile* c = ship_captain_profile(s);
  return c ? c->trait : TRAIT_NAVIGATOR;
}

static uint8_t captain_level(const Ship& s) {
  const CaptainProfile* c = ship_captain_profile(s);
  return c ? c->level : 0;
}

// A Merchant captain sells for 5% more, rounded to the nearest coin.
static uint16_t captain_sale_price(const Ship& s, uint16_t price) {
  if (g_campaign.economyVersion < 2 || !ship_captain_profile(s) || captain_trait(s) != TRAIT_MERCHANT) return price;
  return (uint16_t)min((uint32_t)65535u, ((uint32_t)price * 105u + 50u) / 100u);
}

static int module_slot(const Ship& s, uint8_t moduleId) {
  uint8_t encoded = moduleId + 1;
  for (int i = 0; i < 2; ++i) if (s.modules[i] == encoded) return i;
  return -1;
}

// Hull damage from an incident; bulkheads absorb a fifth of it.
static void hull_damage(Ship& s, int base) {
  int loss = module_slot(s, MODULE_BULKHEADS) >= 0 ? base * 4 / 5 : base;
  s.condition = (uint8_t)(s.condition > loss ? s.condition - loss : 0);
}

static int ship_endurance(const Ship& s) {
  return SHIP_CLASS_DEFS[s.classId].enduranceDays +
         (module_slot(s, MODULE_LONG_CASKS) >= 0 ? 6 : 0);
}

static int ship_speed(const Ship& s) {
  int speed = SHIP_CLASS_DEFS[s.classId].speed;
  if (module_slot(s, MODULE_COPPER) >= 0) speed += 10;
  if (module_slot(s, MODULE_CHARTER_PENNANT) >= 0) speed += 5;
  if (ship_captain_profile(s) && captain_trait(s) == TRAIT_NAVIGATOR)
    speed += 4 + captain_level(s) * 2;
  return speed;
}

static int ship_guns(const Ship& s) {
  int guns = SHIP_CLASS_DEFS[s.classId].guns +
             (module_slot(s, MODULE_SWIVEL_GUNS) >= 0 ? 2 : 0) +
             (module_slot(s, MODULE_CHARTER_PENNANT) >= 0 ? 1 : 0);
  if (ship_captain_profile(s) && captain_trait(s) == TRAIT_GUNNER)
    guns += 1 + captain_level(s) / 2;
  return guns;
}

static int ship_storm(const Ship& s) {
  int storm = SHIP_CLASS_DEFS[s.classId].storm +
              (module_slot(s, MODULE_STORM_RIG) >= 0 ? 2 : 0);
  if (ship_captain_profile(s) && captain_trait(s) == TRAIT_NAVIGATOR) ++storm;
  return storm;
}

int sim_lane_wind(uint8_t laneIndex, uint8_t fromPort) {
  if (laneIndex >= LANE_COUNT) return 0;
  const SeaLaneDef& lane = LANE_DEFS[laneIndex];
  int direction = fromPort == lane.a ? 1 : -1;
  uint32_t season = g_campaign.worldMinutes / (90u * 1440u);
  int seasonal = ((season + laneIndex / 7u) & 1u) ? lane.seasonAB : -lane.seasonAB;
  int wind = direction * (lane.windAB + seasonal);
  if (g_campaign.worldEventType == EVENT_TRADE_WINDS) {
    uint8_t ra = PORT_DEFS[lane.a].region, rb = PORT_DEFS[lane.b].region;
    if (ra == g_campaign.worldEventRegion || rb == g_campaign.worldEventRegion)
      wind += BALANCE.tradeWindBonus;
  }
  if (wind < -40) wind = -40;
  if (wind > 40) wind = 40;
  return wind;
}

int sim_lane_hazard(uint8_t laneIndex) {
  if (laneIndex >= LANE_COUNT) return 0;
  const SeaLaneDef& lane = LANE_DEFS[laneIndex];
  int hazard = lane.hazard;
  if (g_campaign.worldEventType == EVENT_PRIVATEERS) {
    uint8_t ra = PORT_DEFS[lane.a].region, rb = PORT_DEFS[lane.b].region;
    if (ra == g_campaign.worldEventRegion || rb == g_campaign.worldEventRegion)
      hazard += BALANCE.privateerHazardBonus;
  }
  return hazard > 7 ? 7 : hazard;
}

uint32_t rng_next() {
  uint32_t x = g_campaign.rng ? g_campaign.rng : 0x9E3779B9u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g_campaign.rng = x;
  return x;
}

uint32_t rng_range(uint32_t n) { return n ? rng_next() % n : 0; }

const char* sim_result_text(SimResultCode code) {
  static const char* const text[] = {
    "Ready", "Unavailable", "Not enough coins", "Hold is full", "Berths are full",
    "Ship is not ready", "Route is not valid", "Facility required", "Rank required",
    "Relief is not available", "Contract is unavailable", "Preview changed",
    "Captain required", "Wrong port", "No room", "Port is locked",
    "No sea lane", "Draft is too deep", "Beyond endurance",
    "Charter project incomplete", "Already owned", "Warehouse is full",
    "Cargo is unavailable", "Captain is unavailable", "No route template",
    "Not unlocked yet", "Use the rocker to change it", "Battery low: charge soon",
    "No ship of yours is docked here", "Could not save; nothing spent"
  };
  return text[code < SIM_RESULT_COUNT ? code : SIM_INVALID];
}

void sim_set_notice(SimResult result) {
  g_ui.noticeCode = result.code;
  g_ui.noticeUntilMs = result.code == SIM_OK ? 0 : millis() + 3200u;
  g_ui.dirty = 1;
}

static void refresh_tutorial_step() {
  uint8_t step = 0;
  while (step < TUTORIAL_COUNT && (g_campaign.tutorialFlags & (1u << step))) ++step;
  g_campaign.tutorialStep = step;
}

// A trade at the exchange counts for stage 2 even after the guidance was
// dismissed (sim_tutorial_complete stops recording then); the step is
// refreshed the same way so the guided hints keep advancing.
static void mark_market_traded() {
  uint8_t bit = (uint8_t)(1u << TUTORIAL_MARKET);
  if (g_campaign.tutorialFlags & bit) return;
  g_campaign.tutorialFlags |= bit;
  refresh_tutorial_step();
}

bool sim_tutorial_complete(uint8_t milestone) {
  if (g_campaign.tutorialDismissed || milestone >= TUTORIAL_COUNT) return false;
  uint8_t bit = (uint8_t)(1u << milestone);
  if (g_campaign.tutorialFlags & bit) return false;
  g_campaign.tutorialFlags |= bit;
  refresh_tutorial_step();
  g_ui.dirty = 1;
  return true;
}

bool sim_tutorial_guided() {
  return !g_campaign.tutorialDismissed &&
         g_campaign.tutorialStep < TUTORIAL_MARKET;
}

// Staged unlocks: the stage is derived from what the company has done and only
// rises (stageSeen keeps the high-water mark across saves).

static const char* const STAGE_NAMES[STAGE_COUNT] = {
  "First voyage", "Trading", "Company", "Operations", "Expansion", "Mastery"
};
#define SH_INBOX_WORD "NOTICES"
static const char* const STAGE_UNLOCKS[STAGE_COUNT] = {
  "Jobs, routes, ZOOM and NOTICES",
  "MARKET, CARGO and passenger jobs",
  "FLEET, SHIPYARD, LEDGER, captains; incidents at sea",
  "Route rules, fittings, port works, warehouses, EVENT",
  "Charts and charters, the full map, captain records",
  "The Grand Charter circumnavigation"
};
static const char* const STAGE_CONDITIONS[STAGE_COUNT] = {
  "Start a company",
  "Complete your first delivery",
  "Trade at an exchange or reach Broker",
  "Buy a second ship",
  "Upgrade a port or reach Merchant",
  "Reach Shipowner"
};
// Journal lines must fit the 63-character entry.
static const char* const STAGE_NOTICES[STAGE_COUNT] = {
  "",
  "Trading opens: MARKET and CARGO.",
  "Company opens: FLEET, SHIPYARD, LEDGER and captains.",
  "Operations open: route rules, fittings, port works.",
  "Expansion opens: charts and charters.",
  "Mastery: the Grand Charter awaits."
};

static bool any_port_upgraded() {
  for (int p = 0; p < MAX_PORTS; ++p) {
    const PortState& port = g_ports[p];
    // The home port starts with a level-1 office, warehouse and shipyard.
    uint8_t office = p == 0 ? 1 : 0, warehouse = p == 0 ? 1 : 0, yard = p == 0 ? 1 : 0;
    if (port.officeLevel > office || port.shipyardLevel > yard || port.warehouseLevel > warehouse)
      return true;
  }
  return false;
}

static uint8_t derived_stage() {
  if (g_campaign.economyVersion) {
    if (g_campaign.deliveredUnits >= 192) return 5;
    if (g_campaign.deliveredUnits >= 48) return 4;
    if (g_campaign.shipCount >= 2) return 3;
    if (g_campaign.deliveredUnits >= 24) return 2;
    return g_campaign.deliveredUnits ? 1 : 0;
  }
  if (g_campaign.contractsCompleted == 0 && g_campaign.deliveries == 0) return 0;
  // The market flag is set by the trade itself (not through the tutorial, which
  // stops recording once dismissed); marketProfit covers saves made before that.
  bool traded = (g_campaign.tutorialFlags & (1u << TUTORIAL_MARKET)) || g_campaign.marketProfit != 0;
  if (g_campaign.rank < 1 && !traded) return 1;
  if (g_campaign.shipCount < 2) return 2;
  if (g_campaign.rank < 2 && !any_port_upgraded()) return 3;
  if (g_campaign.rank < 3) return 4;
  return 5;
}

uint8_t sim_stage() {
  uint8_t derived = derived_stage();
  return derived > g_campaign.stageSeen ? derived : g_campaign.stageSeen;
}

uint8_t sim_feature_stage(uint8_t feature) {
  if (feature == FEATURE_ZOOM) return 0;
  static const uint8_t table[FEATURE_COUNT] = {
    1, 1, 1, 1,        // MARKET, CARGO, PASSENGERS, ZOOM
    2, 2, 2, 2, 2,     // FLEET, SHIPYARD, LEDGER, CAPTAINS, INCIDENTS
    3, 3, 3, 3, 3,     // ROUTE_RULES, FITTINGS, PORT_WORKS, WAREHOUSE, EVENTS
    4, 4               // CHARTERS, CAPTAIN_RECORDS
  };
  return feature < FEATURE_COUNT ? table[feature] : 0;
}

bool sim_feature_unlocked(uint8_t feature) {
  if (g_campaign.economyVersion) {
    // Migrated companies retain earned detail screens. New players see one
    // practical capability when there is a reason to use it.
    if (feature < FEATURE_COUNT && (g_campaign.legacyFeatureMask & (1u << feature)))
      return true;
    switch (feature) {
      case FEATURE_ZOOM: case FEATURE_FLEET: case FEATURE_CARGO: return true;
      // Every ship sails under a captain; the merchant game shows them from
      // the first voyage.
      case FEATURE_CAPTAINS: return g_campaign.economyVersion >= 2 || g_campaign.deliveredUnits >= 48;
      case FEATURE_MARKET: return g_campaign.economyVersion >= 2 || g_campaign.deliveredUnits >= 4;
      case FEATURE_INCIDENTS: return g_campaign.economyVersion >= 2 ? g_campaign.completedVoyages >= 1 : g_campaign.deliveredUnits >= 48;
      case FEATURE_YARD: case FEATURE_LEDGER: return g_campaign.deliveredUnits >= 24;
      case FEATURE_ROUTE_RULES: return g_campaign.shipCount >= 2 && g_campaign.completedVoyages >= 4;
      case FEATURE_WAREHOUSE: case FEATURE_FITTINGS: return g_campaign.deliveredUnits >= 36;
      default: return g_campaign.deliveredUnits >= 48;
    }
  }
  return sim_stage() >= sim_feature_stage(feature);
}

const char* sim_stage_name(uint8_t stage) { return STAGE_NAMES[stage < STAGE_COUNT ? stage : STAGE_COUNT - 1]; }
const char* sim_stage_unlocks(uint8_t stage) { return STAGE_UNLOCKS[stage < STAGE_COUNT ? stage : STAGE_COUNT - 1]; }
const char* sim_stage_condition(uint8_t stage) { return STAGE_CONDITIONS[stage < STAGE_COUNT ? stage : STAGE_COUNT - 1]; }

void sim_stage_progress(uint8_t stage, char* out, size_t cap) {
  if (!cap) return;
  out[0] = 0;
  if (g_campaign.economyVersion) {
    static const uint16_t targets[STAGE_COUNT] = {0,4,24,24,48,192};
    uint16_t target = targets[stage < STAGE_COUNT ? stage : STAGE_COUNT - 1];
    snprintf(out, cap, "%lu / %u cargo delivered", (unsigned long)min(g_campaign.deliveredUnits, (uint32_t)target), target);
    return;
  }
  auto xp_toward = [&](uint8_t rank) {
    unsigned need = rank <= 5 ? BALANCE.rankXp[rank] : 0;
    unsigned have = g_campaign.companyXp > need ? need : g_campaign.companyXp;
    snprintf(out, cap, "%u of %u XP to %s", have, need, content_rank_name(rank));
  };
  switch (stage) {
    case 1: snprintf(out, cap, "%u of 1 delivery", g_campaign.deliveries ? 1u : 0u); break;
    case 2: xp_toward(1); break;
    case 3: snprintf(out, cap, "%u of 2 ships", g_campaign.shipCount); break;
    case 4: xp_toward(2); break;
    case 5: xp_toward(3); break;
    default: break;
  }
}

void sim_stage_reconcile(bool announce) {
  uint8_t derived = derived_stage();
  while (g_campaign.stageSeen < derived && g_campaign.stageSeen + 1 < STAGE_COUNT) {
    ++g_campaign.stageSeen;
    if (announce) journal_add(2, "%s", STAGE_NOTICES[g_campaign.stageSeen]);
    g_ui.dirty = 1;
  }
}

// Notices about a ship or the treasury (arrivals, disabled ships, relief)
// are always open; the rest belong to a feature and wait for its stage.
bool attention_kind_open(uint8_t kind) {
  switch (kind) {
    case ATTENTION_INCIDENT: return sim_feature_unlocked(FEATURE_INCIDENTS);
    case ATTENTION_CHARTER: return sim_feature_unlocked(FEATURE_CHARTERS);
    case ATTENTION_EVENT: return sim_feature_unlocked(FEATURE_EVENTS);
    default: return true;
  }
}

// A company saved before its stage was derived can carry state the stage
// now hides. Clear it once on load so the player sees only what has opened;
// nothing here can arise in play at that stage, so nothing of value is lost.
void sim_scrub_locked_state() {
  // A presentation unlock must never rewrite the terms of a saved order or
  // silently resolve a legacy incident. These stay accessible in Details.
  if (g_campaign.economyVersion) return;
  if (!sim_feature_unlocked(FEATURE_EVENTS) && g_campaign.worldEventType != EVENT_NONE) {
    g_campaign.worldEventType = EVENT_NONE;
    g_campaign.worldEventDays = 0;
    g_campaign.worldEventGood = g_campaign.worldEventRegion = 0xFF;
    g_ui.dirty = 1;
  }
  if (!sim_feature_unlocked(FEATURE_INCIDENTS)) {
    for (uint8_t i = 0; i < g_campaign.shipCount; ++i) {
      Ship& ship = g_ships[i];
      ship.incidentArmed = 0;
      if (ship.state == SHIP_AWAITING_DECISION) ship.state = SHIP_SAILING;
    }
    memset(&g_incident, 0, sizeof(g_incident));
    g_incidentQueueCount = 0;
  }
  if (!sim_feature_unlocked(FEATURE_CHARTERS)) {
    for (int p = 0; p < MAX_PORTS; ++p) {
      bool changed = false;
      for (int i = 0; i < MAX_CONTRACTS_PER_PORT; ++i) {
        Contract& c = g_contracts[p][i];
        if (c.kind != JOB_CHARTER) continue;
        if (c.active == 1) { memset(&c, 0, sizeof(c)); changed = true; }
        else if (c.active == 2) { c.kind = JOB_FREIGHT; c.projectRegion = 0xFF; }  // sails on as plain freight
      }
      if (changed) compact_contracts((uint8_t)p);
    }
    for (uint8_t i = 0; i < g_campaign.shipCount; ++i)
      for (int c = 0; c < g_ships[i].cargoCount; ++c) {
        CargoItem& item = g_ships[i].cargo[c];
        if (item.jobKind == JOB_CHARTER) { item.jobKind = JOB_FREIGHT; item.projectRegion = 0xFF; }
      }
  }
  int kept = 0;
  for (int i = 0; i < g_attentionCount; ++i)
    if (attention_kind_open(g_attention[i].kind)) g_attention[kept++] = g_attention[i];
  if (kept != g_attentionCount) { g_attentionCount = (uint8_t)kept; g_ui.dirty = 1; }
}

void sim_tutorial_reconcile() {
  if (g_campaign.tutorialDismissed) return;
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    const Ship& s = g_ships[i];
    for (int c = 0; c < s.cargoCount; ++c) {
      if (s.cargo[c].kind == CARGO_CONTRACT) sim_tutorial_complete(TUTORIAL_CONTRACT);
      if (s.cargo[c].kind == CARGO_MARKET) sim_tutorial_complete(TUTORIAL_MARKET);
    }
    if (s.state == SHIP_SAILING || s.state == SHIP_AWAITING_DECISION ||
        s.state == SHIP_ARRIVED) {
      sim_tutorial_complete(TUTORIAL_ROUTE);
      sim_tutorial_complete(TUTORIAL_DISPATCH);
    }
    if (s.state == SHIP_ARRIVED || s.currentPort != 0) sim_tutorial_complete(TUTORIAL_ARRIVAL);
  }
  if (g_campaign.visitedMask & ~1u) sim_tutorial_complete(TUTORIAL_ARRIVAL);
  if (g_campaign.marketProfit) sim_tutorial_complete(TUTORIAL_MARKET);
  for (int i = 0; i < g_attentionCount; ++i)
    if (g_attention[i].kind == ATTENTION_ARRIVAL) sim_tutorial_complete(TUTORIAL_ARRIVAL);
}

uint8_t attention_unread_count() {
  uint8_t n = 0;
  for (int i = 0; i < g_attentionCount; ++i) if (g_attention[i].unread) ++n;
  return n;
}

uint8_t attention_urgent_unread_count() {
  uint8_t n = 0;
  for (int i = 0; i < g_attentionCount; ++i)
    if (g_attention[i].unread && g_attention[i].urgent && !g_attention[i].resolved) ++n;
  return n;
}

int attention_visible_count() {
  int n = 0;
  for (int i = 0; i < g_attentionCount; ++i)
    if (!g_ui.attentionUnreadOnly || g_attention[i].unread) ++n;
  return n;
}

void attention_add(uint8_t kind, uint8_t shipIndex, uint8_t port, bool urgent, uint8_t region) {
  for (int i = 0; i < g_attentionCount; ++i) {
    AttentionEntry& a = g_attention[i];
    bool sameIncident = kind == ATTENTION_INCIDENT && a.kind == kind &&
                        a.shipIndex == shipIndex && !a.resolved;
    if (sameIncident || (!urgent && !a.urgent && !a.resolved && a.kind == kind &&
        a.shipIndex == shipIndex && a.port == port && a.region == region)) {
      a.worldMinute = g_campaign.worldMinutes;
      a.unread = 1;
      g_ui.dirty = 1;
      return;
    }
  }
  if (g_attentionCount >= MAX_ATTENTION) {
    int remove = -1;
    for (int i = 0; i < g_attentionCount; ++i)
      if (!g_attention[i].urgent && (!g_attention[i].unread || g_attention[i].resolved)) {
        remove = i; break;
      }
    if (remove < 0 && !urgent) return;
    if (remove < 0) {
      for (int i = 0; i < g_attentionCount; ++i)
        if (!g_attention[i].urgent) { remove = i; break; }
      // Only unread, unsettled rows are left: say so rather than drop one silently.
      if (remove >= 0) journal_add(1, "Notices full: an unread notice was dropped.");
    }
    if (remove < 0) return;
    for (int i = remove; i + 1 < g_attentionCount; ++i) g_attention[i] = g_attention[i + 1];
    --g_attentionCount;
  }
  AttentionEntry& a = g_attention[g_attentionCount++];
  a.id = ++g_campaign.nextAttentionId;
  a.worldMinute = g_campaign.worldMinutes;
  a.kind = kind;
  a.shipIndex = shipIndex;
  a.port = port;
  a.urgent = urgent;
  a.unread = 1;
  a.resolved = 0;
  a.region = region;
  g_ui.dirty = 1;
}

bool attention_has_arrival_report(uint8_t index) {
  if (index >= g_attentionCount) return false;
  const AttentionEntry& a = g_attention[index];
  if (a.kind == ATTENTION_ARRIVAL && a.shipIndex < g_campaign.shipCount &&
      g_ships[a.shipIndex].booking.state == BOOKING_SETTLED &&
      g_ships[a.shipIndex].booking.destination == a.port) return true;
  return a.kind == ATTENTION_ARRIVAL && a.shipIndex < g_campaign.shipCount &&
      arrivalReceipts[a.shipIndex].valid && arrivalReceipts[a.shipIndex].port == a.port &&
      arrivalReceipts[a.shipIndex].worldMinute == a.worldMinute;
}

void attention_select(uint8_t index) {
  if (index >= g_attentionCount) return;
  AttentionEntry& a = g_attention[index];
  a.unread = 0;
  g_ui.selectedAttention = index;
  if (a.shipIndex < g_campaign.shipCount) g_ui.selectedShip = a.shipIndex;
  if (a.port < MAX_PORTS) g_ui.selectedPort = a.port;
  if (a.kind == ATTENTION_INCIDENT && g_incident.active &&
      g_incident.shipIndex == a.shipIndex && attention_kind_open(a.kind)) g_ui.card = CARD_INCIDENT;
  if (a.kind == ATTENTION_ARRIVAL) {
    if (attention_has_arrival_report(index)) { show_arrival_receipt(a.shipIndex); g_ui.card = CARD_ARRIVAL; }
    else { g_ui.card = CARD_FLEET; g_ui.page = 0; }
  }
  // Every other kind opens the screen where the player can act on it.
  if (a.kind == ATTENTION_DISABLED) { g_ui.card = CARD_FLEET; g_ui.page = 0; }
  else if (a.kind == ATTENTION_EVENT && attention_kind_open(a.kind)) { g_ui.card = CARD_EVENT; g_ui.page = 0; }
  else if (a.kind == ATTENTION_RECOVERY) { g_ui.card = CARD_SHIPYARD; g_ui.page = 2; }
  else if (a.kind == ATTENTION_CHARTER && attention_kind_open(a.kind)) {
    g_ui.card = CARD_CHARTER; g_ui.page = 0;
  }
  g_ui.dirty = 1;
}

void attention_resolve(uint8_t kind, uint8_t shipIndex) {
  for (int i = 0; i < g_attentionCount; ++i) {
    AttentionEntry& a = g_attention[i];
    if (a.kind == kind && a.shipIndex == shipIndex && !a.resolved) {
      a.resolved = 1;
      a.urgent = 0;  // settled notices may be cleared or evicted
      g_ui.dirty = 1;
    }
  }
}

void attention_clear_read() {
  for (int i = g_attentionCount - 1; i >= 0; --i) {
    if (g_attention[i].urgent || g_attention[i].unread) continue;
    for (int j = i; j + 1 < g_attentionCount; ++j) g_attention[j] = g_attention[j + 1];
    --g_attentionCount;
  }
  g_ui.dirty = 1;
}

CaptainProfile* sim_ship_captain(uint8_t shipIndex) {
  if (shipIndex >= g_campaign.shipCount) return nullptr;
  uint8_t id = g_ships[shipIndex].captainId;
  if (id >= MAX_CAPTAINS || !g_captains[id].active) return nullptr;
  return &g_captains[id];
}

void journal_add(uint8_t severity, const char* fmt, ...) {
  JournalEntry& e = g_journal[g_journalHead];
  e.worldMinute = g_campaign.worldMinutes;
  e.severity = severity;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e.text, sizeof(e.text), fmt, ap);
  va_end(ap);
  e.text[sizeof(e.text) - 1] = 0;
  g_journalHead = (uint8_t)((g_journalHead + 1) % MAX_JOURNAL);
  if (g_journalCount < MAX_JOURNAL) ++g_journalCount;
  g_ui.dirty = 1;
  if (severity >= 2) audio_alert();
}

int sim_ship_used_capacity(const Ship& ship) {
  int n = 0;
  for (int i = 0; i < ship.cargoCount; ++i)
    if (ship.cargo[i].jobKind != JOB_PASSENGER) n += ship.cargo[i].quantity;
  return n;
}

int sim_ship_passenger_load(const Ship& ship) {
  int n = 0;
  for (int i = 0; i < ship.cargoCount; ++i)
    if (ship.cargo[i].jobKind == JOB_PASSENGER) n += ship.cargo[i].quantity;
  return n;
}

int sim_ship_capacity(const Ship& ship) {
  int n = SHIP_CLASS_DEFS[ship.classId].hold;
  if (module_slot(ship, MODULE_LONG_CASKS) >= 0) --n;
  if (module_slot(ship, MODULE_BULKHEADS) >= 0) --n;
  if (module_slot(ship, MODULE_CHARTER_PENNANT) >= 0) ++n;
  return n < 1 ? 1 : n;
}

int sim_ship_berths(const Ship& ship) {
  return SHIP_CLASS_DEFS[ship.classId].berths +
         (module_slot(ship, MODULE_BERTHS) >= 0 ? 2 : 0);
}

static bool cargo_fits(const Ship& s, const CargoItem& item) {
  if (s.cargoCount >= MAX_CARGO_ITEMS) return false;
  if (item.jobKind == JOB_PASSENGER)
    return sim_ship_passenger_load(s) + item.quantity <= sim_ship_berths(s);
  return sim_ship_used_capacity(s) + item.quantity <= sim_ship_capacity(s);
}

static void cargo_remove(Ship& ship, int index) {
  if (index < 0 || index >= ship.cargoCount) return;
  for (int i = index; i + 1 < ship.cargoCount; ++i) ship.cargo[i] = ship.cargo[i + 1];
  --ship.cargoCount;
}

static void warehouse_remove(PortState& port, int index) {
  if (index < 0 || index >= port.warehouseCount) return;
  for (int i = index; i + 1 < port.warehouseCount; ++i)
    port.warehouse[i] = port.warehouse[i + 1];
  --port.warehouseCount;
}

static void route_tables(uint8_t from, uint8_t* hops, int* days) {
  uint8_t queue[MAX_PORTS];
  int head = 0, tail = 0;
  for (int i = 0; i < MAX_PORTS; ++i) { hops[i] = 0xFF; days[i] = 30000; }
  hops[from] = 0; queue[tail++] = from;
  while (head < tail) {
    uint8_t p = queue[head++];
    for (int i = 0; i < LANE_COUNT; ++i) {
      int n = LANE_DEFS[i].a == p ? LANE_DEFS[i].b :
              (LANE_DEFS[i].b == p ? LANE_DEFS[i].a : -1);
      if (n < 0 || !g_ports[n].unlocked || hops[n] != 0xFF) continue;
      hops[n] = hops[p] + 1;
      queue[tail++] = (uint8_t)n;
    }
  }
  bool used[MAX_PORTS]{};
  days[from] = 0;
  for (int pass = 0; pass < MAX_PORTS; ++pass) {
    int p = -1;
    for (int i = 0; i < MAX_PORTS; ++i)
      if (!used[i] && days[i] < 30000 && (p < 0 || days[i] < days[p])) p = i;
    if (p < 0) break;
    used[p] = true;
    for (int i = 0; i < LANE_COUNT; ++i) {
      int n = -1;
      if (LANE_DEFS[i].a == p) n = LANE_DEFS[i].b;
      else if (LANE_DEFS[i].b == p) n = LANE_DEFS[i].a;
      if (n < 0 || !g_ports[n].unlocked) continue;
      int nd = days[p] + LANE_DEFS[i].days;
      if (nd < days[n]) days[n] = nd;
    }
  }
}

static uint8_t next_locked_region() {
  for (uint8_t r = 1; r < REGION_COUNT; ++r)
    if (!(g_campaign.regionMask & (1u << r))) return r;
  return 0xFF;
}

static void update_rank() {
  uint8_t old = g_campaign.rank;
  static const uint16_t deliveredTargets[6] = {0,12,24,48,96,192};
  while (g_campaign.rank < 5 && (g_campaign.economyVersion ? g_campaign.deliveredUnits : g_campaign.companyXp) >=
      (g_campaign.economyVersion ? deliveredTargets[g_campaign.rank + 1] : BALANCE.rankXp[g_campaign.rank + 1]))
    ++g_campaign.rank;
  if (g_campaign.rank != old) {
    journal_add(2, "Company rank: %s.", content_rank_name(g_campaign.rank));
    if (g_campaign.rank >= 1 && !g_ports[3].unlocked) {
      g_ports[3].unlocked = 1;
      sim_generate_jobs(3, true);
      journal_add(1, "%s opens for trade.", PORT_DEFS[3].name);
    }
    audio_notice();
  }
  return_reconcile_milestones();
}

static void create_novice_captain(uint8_t shipIndex, uint8_t port, int ordinal) {
  if (shipIndex >= MAX_SHIPS) return;
  int captainSlot = -1;
  for (int i = 0; i < MAX_CAPTAINS; ++i) if (!g_captains[i].active) { captainSlot = i; break; }
  if (captainSlot < 0) return;
  CaptainProfile& c = g_captains[captainSlot];
  memset(&c, 0, sizeof(c));
  snprintf(c.name, sizeof(c.name), "%s", CAPTAIN_NAMES[ordinal % 12]);
  c.trait = (uint8_t)(ordinal % 5);
  c.portrait = content_captain_portrait(c.name, c.trait);
  c.level = 1;
  c.loyalty = BALANCE.captainStartingLoyalty;
  c.assignedShip = shipIndex;
  c.currentPort = port;
  c.active = 1;
  g_ships[shipIndex].captainId = (uint8_t)captainSlot;
  if (g_campaign.captainCount <= captainSlot)
    g_campaign.captainCount = (uint8_t)(captainSlot + 1);
}

static void init_ship(Ship& s, uint8_t classId, uint8_t port, int ordinal,
                      bool assignNovice = false) {
  memset(&s, 0, sizeof(s));
  s.id = g_campaign.nextShipId++;
  s.classId = classId;
  s.state = SHIP_DOCKED;
  s.currentPort = port;
  s.fromPort = s.toPort = port;
  s.condition = 100;
  s.morale = 80;
  s.crew = (uint8_t)(8 + classId * 5);
  snprintf(s.name, sizeof(s.name), "%s", SHIP_NAMES[ordinal % 12]);
  s.routeTemplate.origin = NO_PORT;
  s.routeTemplate.buyGood = NO_PORT;
  s.captainId = NO_SHIP;
  if (assignNovice) create_novice_captain((uint8_t)ordinal, port, ordinal);
}

static void story_reset();  // sim_story.h
static void atlas_reset();  // sim_atlas.h

void sim_new_campaign(uint32_t seed) {
  if (!++returnCompanyEpoch) ++returnCompanyEpoch;
  memset(&g_campaign, 0, sizeof(g_campaign));
  g_campaign.stageSeen = 0;
  story_reset();
  atlas_reset();
  memset(g_ports, 0, sizeof(g_ports));
  memset(g_contracts, 0, sizeof(g_contracts));
  memset(g_ships, 0, sizeof(g_ships));
  memset(&g_incident, 0, sizeof(g_incident));
  memset(&g_incidentOutcome, 0, sizeof(g_incidentOutcome));
  memset(arrivalReceipts, 0, sizeof(arrivalReceipts));
  memset(g_incidentQueue, 0, sizeof(g_incidentQueue));
  memset(g_journal, 0, sizeof(g_journal));
  memset(g_attention, 0, sizeof(g_attention));
  memset(g_captains, 0, sizeof(g_captains));
  memset(&g_routePreparation, 0, sizeof(g_routePreparation));
  memset(&g_ui, 0, sizeof(g_ui));
  g_journalHead = g_journalCount = 0;
  g_incidentQueueCount = 0;
  g_attentionCount = 0;

  g_campaign.seed = seed ? seed : 0x51A17EEDu;
  g_campaign.rng = g_campaign.seed;
  g_campaign.nextContractId = 1;
  g_campaign.nextShipId = 1;
  g_campaign.nextVoyageId = 1;
  g_campaign.economyVersion = 2;
  g_campaign.nextAttentionId = 0;
  g_campaign.coins = 500;
  g_campaign.regionMask = 1;
  g_campaign.chartMask = 1;
  g_campaign.visitedMask = 1;
  g_campaign.shipCount = 1;
  g_campaign.soundOn = 1;
  g_campaign.timeScale = 1;
  g_campaign.worldEventGood = 0xFF;
  g_campaign.worldEventRegion = 0xFF;
  g_campaign.lastMarketDay = 0;
  g_campaign.tutorialStep = 0;

  for (int p = 0; p < MAX_PORTS; ++p) {
    g_ports[p].packageEpoch = UINT32_MAX;
    g_ports[p].unlocked = p < 3;
    g_ports[p].officeLevel = p == 0 ? 1 : 0;
    g_ports[p].warehouseLevel = p == 0 ? 1 : 0;
    g_ports[p].shipyardLevel = p == 0 ? 1 : 0;
    for (int good = 0; good < GOOD_COUNT; ++good) {
      int target = 12;
      if (PORT_DEFS[p].produced[0] == good || PORT_DEFS[p].produced[1] == good) target = 18;
      if (PORT_DEFS[p].specialty == good)
        target = target * BALANCE.specialtyStockPercent / 100;
      if (PORT_DEFS[p].demanded[0] == good || PORT_DEFS[p].demanded[1] == good) target = 6;
      g_ports[p].stock[good] = (uint16_t)(target + (int)rng_range(5) - 2);
      uint16_t price = content_market_price((uint8_t)p, (uint8_t)good, true);
      uint16_t normalized = price * 100 / GOOD_DEFS[good].basePrice;
      if (normalized > 255) normalized = 255;
      for (int d = 0; d < 7; ++d) g_ports[p].priceHistory[good][d] = (uint8_t)normalized;
    }
  }

  init_ship(g_ships[0], 0, 0, 0, true);
  // A gift for the first sale: ordinary owned cargo, with no invented voyage
  // or purchase cost. Only new companies receive it; loading never adds cargo.
  CargoItem starter{};
  starter.good = GOOD_GRAIN;
  starter.quantity = 2;
  starter.kind = CARGO_MARKET;
  starter.jobKind = JOB_FREIGHT;
  starter.destination = starter.projectRegion = starter.sourcePort = NO_PORT;
  g_ships[0].cargo[g_ships[0].cargoCount++] = starter;
  g_ui.scene = SCENE_FLEET;
  g_campaign.onboardingStep = ONBOARD_CAPTAIN;
  g_ui.selectedPort = 0;
  g_ui.selectedShip = 0;
  g_ui.selectedGood = 0;
  g_ui.title = 1;
  // Start close to the home port; the rocker re-centres as it browses ports.
  g_ui.mapZoom = 2;
  g_ui.mapFocusX = PORT_DEFS[0].x;
  g_ui.mapFocusY = PORT_DEFS[0].y;
  g_ui.selectedCaptain = NO_SHIP;
  g_ui.dirty = 1;

  for (int p = 0; p < MAX_PORTS; ++p) if (g_ports[p].unlocked) sim_generate_jobs((uint8_t)p, true);
  journal_add(1, "A gift aboard the Petrel: 2 Grain to sell.");
}

static bool incident_references_ship(const Incident& incident, uint8_t shipIndex) {
  return incident.active && incident.shipIndex == shipIndex;
}

static void activate_next_incident() {
  if (g_incident.active || !g_incidentQueueCount) return;
  g_incident = g_incidentQueue[0];
  for (int i = 0; i + 1 < g_incidentQueueCount; ++i) g_incidentQueue[i] = g_incidentQueue[i + 1];
  --g_incidentQueueCount;
  g_incident.active = 1;
  g_ui.dirty = 1;
}

void sim_after_load() {
  if (g_atlas.migrate) sim_atlas_migrate();
  if (g_campaign.economyVersion < 2) {
    g_campaign.economyVersion = 2;
    for (int p = 0; p < MAX_PORTS; ++p) g_ports[p].packageEpoch = UINT32_MAX;
  }
  return_ack_retained_capabilities();
  memset(&g_incidentOutcome, 0, sizeof(g_incidentOutcome));
  memset(arrivalReceipts, 0, sizeof(arrivalReceipts));
  for (uint8_t i = 0; i < g_campaign.shipCount; ++i) {
    const Ship& ship = g_ships[i];
    if (ship.booking.state != BOOKING_SETTLED && ship.merchant.state != MERCHANT_ARRIVED) continue;
    uint32_t minute = g_campaign.worldMinutes;
    for (uint8_t a = 0; a < g_attentionCount; ++a)
      if (g_attention[a].kind == ATTENTION_ARRIVAL && g_attention[a].shipIndex == i)
        minute = g_attention[a].worldMinute;
    arrivalReceipts[i] = ArrivalReceipt{minute,
      ship.merchant.state == MERCHANT_ARRIVED ? ship.merchant.packageCoins : ship.booking.arrivalCoins,
      ship.currentPort, (uint8_t)(ship.merchant.state == MERCHANT_ARRIVED ?
        min((uint16_t)255, ship.merchant.packagesDelivered) : ship.booking.settledQuantity), 0, 1};
  }
  sim_stage_reconcile(false);  // older saves derive their stage silently
  sim_scrub_locked_state();    // and drop what that stage has not opened
  for (int i = 0; i < g_attentionCount; ++i)  // settled notices never stay urgent
    if (g_attention[i].resolved) g_attention[i].urgent = 0;
  if (g_ui.selectedPort >= MAX_PORTS) g_ui.selectedPort = 0;
  if (!sim_feature_unlocked(FEATURE_ZOOM)) {
    g_ui.mapZoom = 2;
    g_ui.mapFocusX = PORT_DEFS[g_ui.selectedPort].x;
    g_ui.mapFocusY = PORT_DEFS[g_ui.selectedPort].y;
  }
  if (g_ui.selectedShip >= g_campaign.shipCount) g_ui.selectedShip = 0;
  // A return begins with the company of ships, including migrated saves.
  // Sailing progress and its selected ship remain intact.
  g_ui.scene = SCENE_FLEET;
  g_ui.page = 0;
  g_ui.title = 1;
  g_ui.card = CARD_NONE;
  g_ui.arrivalSummaryVisible = 0;
  g_ui.routeCount = 0;
  g_ui.dirty = 1;
  if (g_incident.active && g_incident.shipIndex < g_campaign.shipCount)
    g_ships[g_incident.shipIndex].state = SHIP_AWAITING_DECISION;
  else
    memset(&g_incident, 0, sizeof(g_incident));
  if (g_incidentQueueCount > MAX_INCIDENTS) g_incidentQueueCount = 0;
  for (uint8_t i = 0; i < g_campaign.shipCount; ++i) {
    if (g_ships[i].state != SHIP_AWAITING_DECISION) continue;
    bool pending = incident_references_ship(g_incident, i);
    for (int q = 0; q < g_incidentQueueCount && !pending; ++q)
      pending = incident_references_ship(g_incidentQueue[q], i);
    if (!pending) g_ships[i].state = SHIP_SAILING;
  }
  // A decision notice with no incident behind it (older saves posted one even
  // when the crew coped on their own) would never open or clear: drop it.
  int kept = 0;
  for (int i = 0; i < g_attentionCount; ++i) {
    const AttentionEntry& a = g_attention[i];
    bool orphan = a.kind == ATTENTION_INCIDENT && !a.resolved &&
                  !incident_references_ship(g_incident, a.shipIndex);
    for (int q = 0; q < g_incidentQueueCount && orphan; ++q)
      if (incident_references_ship(g_incidentQueue[q], a.shipIndex)) orphan = false;
    if (!orphan) g_attention[kept++] = g_attention[i];
  }
  if (kept != g_attentionCount) { g_attentionCount = (uint8_t)kept; g_ui.dirty = 1; }
  activate_next_incident();
  if (g_incident.active) {
    bool hasNotice = false;
    for (uint8_t i = 0; i < g_attentionCount; ++i)
      hasNotice |= g_attention[i].kind == ATTENTION_INCIDENT && !g_attention[i].resolved &&
                   g_attention[i].shipIndex == g_incident.shipIndex;
    // Reconstruct a missing notice, but preserve the saved review status and
    // event timestamp of an existing one. A reload is not a new incident.
    if (!hasNotice) attention_add(ATTENTION_INCIDENT, g_incident.shipIndex,
        g_ships[g_incident.shipIndex].currentPort, true);
  }
  sim_tutorial_reconcile();
  for (uint8_t p = 0; p < MAX_PORTS; ++p)
    if (g_ports[p].unlocked) sim_generate_jobs(p, false);
}

void sim_generate_jobs(uint8_t port, bool force) {
  if (port >= MAX_PORTS || !g_ports[port].unlocked) return;
  if (g_campaign.economyVersion >= 2) { merchant_refresh_packages(port); return; }
  Contract* board = g_contracts[port];
  if (force)
    for (int i = 0; i < MAX_CONTRACTS_PER_PORT; ++i)
      if (board[i].active != 2) memset(&board[i], 0, sizeof(board[i]));
  int desired = 8 + (g_ports[port].officeLevel >= 2 ? 1 : 0) +
                (g_ports[port].officeLevel >= 3 || g_campaign.won ? 1 : 0);
  uint8_t frontier = next_locked_region();
  int projectSlots = frontier == 0xFF ? 0 : (frontier == 1 ? 1 : 2);
  int projectStart = desired ? (int)((g_campaign.worldMinutes / 1440u + port * 3u) % desired) : 0;
  uint8_t hopsFromPort[MAX_PORTS];
  int daysFromPort[MAX_PORTS];
  route_tables(port, hopsFromPort, daysFromPort);

  for (int slot = 0; slot < desired; ++slot) {
    if (board[slot].active) continue;
    uint8_t candidates[MAX_PORTS];
    int count = 0;
    int band = (int)rng_range(100);
    if (g_campaign.won && band < 35) band += 60;
    for (int p = 0; p < MAX_PORTS; ++p) {
      if (p == port || !g_ports[p].unlocked) continue;
      int hops = hopsFromPort[p] == 0xFF ? -1 : hopsFromPort[p];
      if (hops < 0) continue;
      bool fit = band < 60 ? hops <= 2 : (band < 90 ? hops >= 2 && hops <= 4 : hops >= 3);
      if (fit) candidates[count++] = (uint8_t)p;
    }
    if (!count) {
      for (int p = 0; p < MAX_PORTS; ++p)
        if (p != port && g_ports[p].unlocked) candidates[count++] = (uint8_t)p;
    }
    if (!count) continue;

    Contract c{};
    c.id = g_campaign.nextContractId++;
    c.origin = port;
    c.destination = candidates[rng_range((uint32_t)count)];
    if (g_campaign.regionMask == 0x3F && rng_range(100) < 60) {
      uint8_t priority[MAX_PORTS];
      int priorityCount = 0;
      for (int p = 0; p < MAX_PORTS; ++p) {
        if (p == port || !g_ports[p].unlocked || hopsFromPort[p] > MAX_ROUTE_LEGS)
          continue;
        bool wanted = g_campaign.visitedMask != 0x3F ?
          !(g_campaign.visitedMask & (1u << PORT_DEFS[p].region)) : (p == 0 && !g_campaign.won);
        if (wanted) priority[priorityCount++] = (uint8_t)p;
      }
      if (priorityCount) c.destination = priority[rng_range((uint32_t)priorityCount)];
    }
    c.good = (uint8_t)rng_range(GOOD_COUNT);
    c.quantity = (uint8_t)(1 + rng_range(g_campaign.won ? 6 : 4));
    c.kind = JOB_FREIGHT;
    c.projectRegion = 0xFF;
    uint32_t kindRoll = rng_range(100);
    int projectOffset = (slot - projectStart + desired) % desired;
    bool projectSlot = projectSlots && projectOffset < projectSlots &&
                       sim_feature_unlocked(FEATURE_CHARTERS);
    if (projectSlot) {
      c.kind = JOB_CHARTER;
      c.projectRegion = frontier;
      c.good = PORT_DEFS[frontier * 4].demanded[0];  // what the frontier's first port wants
      c.quantity = (uint8_t)(2 + rng_range(3));
    } else if (kindRoll < 18) { c.kind = JOB_MAIL; c.quantity = 1; c.good = GOOD_GRAIN; }
    else if (kindRoll < 36 && sim_feature_unlocked(FEATURE_PASSENGERS)) {
      c.kind = JOB_PASSENGER; c.quantity = (uint8_t)(1 + rng_range(2)); c.good = GOOD_GRAIN;
    }
    int days = daysFromPort[c.destination] < 30000 ? daysFromPort[c.destination] : -1;
    if (days < 1) days = 4;
    int base = GOOD_DEFS[c.good].basePrice * c.quantity;
    // A shortage or glut in the destination's region moves matching freight
    // the way it moves the exchange, so an event is worth chasing with jobs too.
    if (c.kind == JOB_FREIGHT && g_campaign.worldEventDays && c.good == g_campaign.worldEventGood &&
        PORT_DEFS[c.destination].region == g_campaign.worldEventRegion) {
      if (g_campaign.worldEventType == EVENT_SHORTAGE) base = base * (100 + BALANCE.worldEventMarketPoints) / 100;
      else if (g_campaign.worldEventType == EVENT_GLUT) base = base * (100 - BALANCE.worldEventMarketPoints) / 100;
    }
    if (c.kind == JOB_MAIL) base = days * 18 + 40;
    if (c.kind == JOB_PASSENGER) base = days * 22 * c.quantity + 50;
    if (c.kind == JOB_CHARTER) base += days * 12 * c.quantity + 80;
    c.reward = (uint16_t)(base + days * 7 * c.quantity);
    c.deadlineMinute = g_campaign.worldMinutes + (uint32_t)(days * 2 + 3) * 1440u;
    c.active = 1;
    board[slot] = c;
  }
  compact_contracts(port);
}

void sim_refresh_day() {
  uint16_t day = (uint16_t)(g_campaign.worldMinutes / 1440u);
  if (day == g_campaign.lastMarketDay) return;
  g_campaign.lastMarketDay = day;
  if (g_campaign.worldEventDays) {
    --g_campaign.worldEventDays;
    if (!g_campaign.worldEventDays) {
      if (sim_feature_unlocked(FEATURE_EVENTS)) {
        journal_add(0, "The latest trade notice has expired.");
        attention_add(ATTENTION_EVENT, NO_SHIP, NO_PORT, false);
      }
      g_campaign.worldEventType = EVENT_NONE;
      g_campaign.worldEventGood = g_campaign.worldEventRegion = 0xFF;
    }
  }
  int eventChance = g_campaign.won ? 36 : 22;
  if (!g_campaign.worldEventDays && rng_range(100) < (uint32_t)eventChance &&
      sim_feature_unlocked(FEATURE_EVENTS)) {
    uint8_t regions[REGION_COUNT];
    int regionCount = 0;
    for (int r = 0; r < REGION_COUNT; ++r)
      if (g_campaign.regionMask & (1u << r)) regions[regionCount++] = (uint8_t)r;
    g_campaign.worldEventType = (uint8_t)(EVENT_SHORTAGE + rng_range(4));
    g_campaign.worldEventGood = (uint8_t)rng_range(GOOD_COUNT);
    g_campaign.worldEventRegion = regions[rng_range((uint32_t)regionCount)];
    g_campaign.worldEventDays = (uint8_t)(3 + rng_range(5));
    const char* kind = g_campaign.worldEventType == EVENT_SHORTAGE ? "shortage" :
                       (g_campaign.worldEventType == EVENT_GLUT ? "surplus" :
                       (g_campaign.worldEventType == EVENT_PRIVATEERS ? "privateer alert" : "trade winds"));
    journal_add(1, "%s: %s. See EVENT.", REGION_NAMES[g_campaign.worldEventRegion], kind);
    attention_add(ATTENTION_EVENT, NO_SHIP, NO_PORT, false);
  }
  for (int p = 0; p < MAX_PORTS; ++p) {
    for (int good = 0; good < GOOD_COUNT; ++good) {
      int target = 12;
      if (PORT_DEFS[p].produced[0] == good || PORT_DEFS[p].produced[1] == good) target = 18;
      if (PORT_DEFS[p].specialty == good)
        target = target * BALANCE.specialtyStockPercent / 100;
      if (PORT_DEFS[p].demanded[0] == good || PORT_DEFS[p].demanded[1] == good) target = 6;
      int stock = g_ports[p].stock[good];
      if (stock < target) ++stock;
      else if (stock > target) --stock;
      g_ports[p].stock[good] = (uint16_t)stock;
      uint16_t price = content_market_price((uint8_t)p, (uint8_t)good, true);
      uint16_t normalized = price * 100 / GOOD_DEFS[good].basePrice;
      if (normalized > 255) normalized = 255;
      g_ports[p].priceHistory[good][g_ports[p].historyHead] = (uint8_t)normalized;
    }
    g_ports[p].historyHead = (uint8_t)((g_ports[p].historyHead + 1) % 7);
    if (g_ports[p].unlocked) sim_generate_jobs((uint8_t)p, true);
  }
  // Docked ships wait safely. Routine service is included in a voyage quote,
  // so putting the reader down never drains the company's operating reserve.
  g_campaign.recruitmentWeek = day / 7u;
}

static Contract* find_contract(uint32_t id) {
  for (int p = 0; p < MAX_PORTS; ++p)
    for (int i = 0; i < MAX_CONTRACTS_PER_PORT; ++i)
      if (g_contracts[p][i].id == id) return &g_contracts[p][i];
  return nullptr;
}

// Keep a port's board ordered: open contracts first, then taken ones, then
// empty slots, so the contracts card never shows a gap above live offers.
// Contracts are referenced by id, never by slot, so reordering is safe.
static void compact_contracts(uint8_t port) {
  if (port >= MAX_PORTS) return;
  Contract* board = g_contracts[port];
  Contract sorted[MAX_CONTRACTS_PER_PORT];
  int n = 0;
  for (uint8_t state = 1; state <= 2; ++state)
    for (int i = 0; i < MAX_CONTRACTS_PER_PORT; ++i)
      if (board[i].active == state) sorted[n++] = board[i];
  for (int i = 0; i < MAX_CONTRACTS_PER_PORT; ++i) {
    if (i < n) board[i] = sorted[i];
    else board[i] = Contract{};
  }
}

SimResult sim_load_contract(uint8_t shipIndex, uint8_t port, uint8_t slot) {
  if (shipIndex >= g_campaign.shipCount || port >= MAX_PORTS || slot >= MAX_CONTRACTS_PER_PORT) return SIM_INVALID;
  Ship& s = g_ships[shipIndex];
  Contract& c = g_contracts[port][slot];
  if (!ship_at_port(s) || s.state == SHIP_DISABLED) return SIM_BAD_SHIP_STATE;
  if (s.currentPort != port) return SIM_WRONG_PORT;
  if (c.active != 1) return SIM_CONTRACT_UNAVAILABLE;
  if (c.kind == JOB_PACKAGE) {
    for (uint8_t row = 0; row < sim_package_count(shipIndex); ++row) {
      PackageOffer offer = sim_package_offer(shipIndex, row);
      if (offer.id == c.id) return sim_package_accept(shipIndex, offer);
    }
    return SIM_CONTRACT_UNAVAILABLE;
  }
  CargoItem item{};
  item.contractId = c.id;
  item.good = c.good;
  item.quantity = c.quantity;
  item.destination = c.destination;
  item.kind = CARGO_CONTRACT;
  item.jobKind = c.kind;
  item.projectRegion = c.projectRegion;
  if (!cargo_fits(s, item)) return c.kind == JOB_PASSENGER ? SIM_NO_BERTHS : SIM_NO_CAPACITY;
  s.cargo[s.cargoCount++] = item;
  c.active = 2;
  journal_add(0, "%s loaded for %s.", content_cargo_name(CARGO_CONTRACT, c.kind, c.good, true),
              PORT_DEFS[c.destination].name);
  compact_contracts(port);
  return SIM_OK;
}

struct MarketUnit {
  uint16_t price;
  int32_t profit;
  int32_t withheld;
  uint8_t delivered;
};

// Market cargo has no contract, so its contractId holds the fractional remainder
// of the stack's purchase cost (keeps save v7's layout; legacy items hold zero),
// so repeated purchases do not round away the basis.
static uint32_t market_cost_basis(const CargoItem& cargo) {
  return (uint32_t)cargo.paidUnitPrice * cargo.quantity + cargo.contractId;
}

static void market_set_cost_basis(CargoItem& cargo, uint32_t total) {
  cargo.paidUnitPrice = cargo.quantity ? (uint16_t)(total / cargo.quantity) : 0;
  cargo.contractId = cargo.quantity ? total % cargo.quantity : 0;
}

static void market_remove_unit(CargoItem& cargo) {
  uint32_t remaining = market_cost_basis(cargo) - cargo.paidUnitPrice;
  --cargo.quantity;
  market_set_cost_basis(cargo, remaining);
}

// Execution sells the first lot of a good. A cheaper later warehouse lot
// must not cause auto-sell to dispose of that first lot at a loss.
static bool market_first_lot_profitable(const Ship& ship, uint8_t good) {
  for (int i = 0; i < ship.cargoCount; ++i) {
    const CargoItem& cargo = ship.cargo[i];
    if (cargo.kind == CARGO_MARKET && cargo.good == good)
      return content_market_price(ship.currentPort, good, false) > cargo.paidUnitPrice;
  }
  return false;
}

// The quote runs this same unit transaction on local copies. Keep validation,
// stack order, integer rounding and debt repayment identical to execution.
static SimResult market_trade_unit(Ship& s, uint8_t good, bool buying, uint16_t& stock,
                                   int32_t& coins, int32_t& debt, int32_t& profitTotal,
                                   MarketUnit& unit) {
  if (!ship_at_port(s) || s.state == SHIP_DISABLED) return SIM_BAD_SHIP_STATE;
  unit = {};
  if (buying) {
    uint16_t price = content_market_price_at_stock(s.currentPort, good, true, stock);
    CargoItem item{};
    item.good = good; item.quantity = 1; item.destination = NO_PORT;
    item.kind = CARGO_MARKET; item.jobKind = JOB_FREIGHT; item.projectRegion = 0xFF;
    item.paidUnitPrice = price;
    item.sourcePort = s.currentPort;
    if (!stock) return SIM_CARGO_UNAVAILABLE;
    if (coins < price) return SIM_NO_FUNDS;
    if (!cargo_fits(s, item)) return SIM_NO_CAPACITY;
    for (int i = 0; i < s.cargoCount; ++i) {
      CargoItem& c = s.cargo[i];
      if (c.kind == CARGO_MARKET && c.good == good && c.quantity < 255 &&
          c.sourcePort == item.sourcePort && !c.sailed) {
        uint32_t total = market_cost_basis(c) + price;
        ++c.quantity;
        market_set_cost_basis(c, total);
        coins -= price;
        --stock;
        unit.price = price;
        return SIM_OK;
      }
    }
    s.cargo[s.cargoCount++] = item;
    coins -= price;
    --stock;
    unit.price = price;
    return SIM_OK;
  }
  for (int i = 0; i < s.cargoCount; ++i) {
    CargoItem& c = s.cargo[i];
    if (c.kind != CARGO_MARKET || c.good != good) continue;
    uint16_t price = captain_sale_price(s, content_market_price_at_stock(s.currentPort, good, false, stock));
    uint16_t paid = c.paidUnitPrice;
    int profit = (int)price - paid;
    unit.delivered = profit > 0 && c.sailed && c.sourcePort < MAX_PORTS && c.sourcePort != s.currentPort;
    int proposedWithheld = profit * BALANCE.debtWithholdPercent / 100;
    int32_t freeDebt = available_unreserved_debt(debt);
    int withheld = profit > 0 && freeDebt > 0 ?
      ((int32_t)proposedWithheld < freeDebt ? proposedWithheld : (int)freeDebt) : 0;
    debt -= withheld;
    coins += price - withheld;
    profitTotal += (int32_t)price - paid;
    // The exchange clears surplus beyond its saved local inventory limit.
    // A valid sale must never create a snapshot the loader will reject.
    if (stock < 200u) ++stock;
    market_remove_unit(c);
    if (!c.quantity) cargo_remove(s, i);
    unit.price = price;
    unit.profit = profit;
    unit.withheld = withheld;
    return SIM_OK;
  }
  return SIM_CARGO_UNAVAILABLE;
}

static SimResult market_execute(uint8_t shipIndex, uint8_t good, bool buying) {
  if (shipIndex >= g_campaign.shipCount || good >= GOOD_COUNT) return SIM_INVALID;
  Ship& ship = g_ships[shipIndex];
  if (ship.currentPort >= MAX_PORTS) return SIM_INVALID;
  MarketUnit unit{};
  SimResult result = market_trade_unit(ship, good, buying, g_ports[ship.currentPort].stock[good],
                                      g_campaign.coins, g_campaign.recoveryDebt,
                                      g_campaign.marketProfit, unit);
  if (!result) return result;
  if (unit.delivered) {
    ++g_campaign.deliveredUnits;
    if (g_campaign.companyXp < 65535u) ++g_campaign.companyXp;
    if (g_campaign.deliveries < 65535u) ++g_campaign.deliveries;
    update_rank();
  }
  if (buying) g_ui.dirty = 1;
  else journal_add(unit.profit >= 0 ? 1 : 0, "Sold %s for %u.",
                   GOOD_DEFS[good].shortName, unit.price);
  mark_market_traded();
  return SIM_OK;
}

SimResult sim_market_buy(uint8_t shipIndex, uint8_t good) {
  return market_execute(shipIndex, good, true);
}

SimResult sim_market_sell(uint8_t shipIndex, uint8_t good) {
  return market_execute(shipIndex, good, false);
}

MarketQuote sim_market_quote(uint8_t shipIndex, uint8_t good, bool buying, uint8_t requested) {
  MarketQuote quote{};
  if (shipIndex >= g_campaign.shipCount || good >= GOOD_COUNT ||
      g_ships[shipIndex].currentPort >= MAX_PORTS) {
    quote.stopReason = SIM_INVALID;
    return quote;
  }
  Ship ship = g_ships[shipIndex];
  uint16_t stock = g_ports[ship.currentPort].stock[good];
  int32_t coins = g_campaign.coins, debt = g_campaign.recoveryDebt;
  int32_t profit = g_campaign.marketProfit;
  for (int i = 0; i < requested; ++i) {
    MarketUnit unit{};
    SimResult result = market_trade_unit(ship, good, buying, stock, coins, debt, profit, unit);
    if (!result) { quote.stopReason = result.code; break; }
    ++quote.quantity;
    quote.total += unit.price;
    quote.withholding += unit.withheld;
  }
  quote.treasuryDelta = coins - g_campaign.coins;
  return quote;
}

SimResult sim_transfer_to_warehouse(uint8_t shipIndex, uint8_t cargoIndex) {
  if (shipIndex >= g_campaign.shipCount) return SIM_INVALID;
  Ship& s = g_ships[shipIndex];
  if (!ship_at_port(s) || s.state == SHIP_DISABLED) return SIM_BAD_SHIP_STATE;
  if (cargoIndex >= s.cargoCount) return SIM_CARGO_UNAVAILABLE;
  if (s.cargo[cargoIndex].kind == CARGO_BOOKED || s.cargo[cargoIndex].jobKind == JOB_PACKAGE ||
      s.cargo[cargoIndex].jobKind == JOB_STORY)
    return SIM_CONTRACT_UNAVAILABLE;
  PortState& p = g_ports[s.currentPort];
  int cap = 4 + p.warehouseLevel * 4;
  if (cap > MAX_WAREHOUSE_ITEMS) cap = MAX_WAREHOUSE_ITEMS;
  if (p.warehouseCount >= cap) return SIM_WAREHOUSE_FULL;
  p.warehouse[p.warehouseCount++] = s.cargo[cargoIndex];
  cargo_remove(s, cargoIndex);
  journal_add(0, "Cargo laid over at %s.", PORT_DEFS[s.currentPort].name);
  return SIM_OK;
}

SimResult sim_transfer_from_warehouse(uint8_t shipIndex, uint8_t warehouseIndex) {
  if (shipIndex >= g_campaign.shipCount) return SIM_INVALID;
  Ship& s = g_ships[shipIndex];
  if (!ship_at_port(s) || s.state == SHIP_DISABLED) return SIM_BAD_SHIP_STATE;
  PortState& p = g_ports[s.currentPort];
  if (warehouseIndex >= p.warehouseCount) return SIM_CARGO_UNAVAILABLE;
  if (p.warehouse[warehouseIndex].kind == CARGO_BOOKED || p.warehouse[warehouseIndex].jobKind == JOB_PACKAGE ||
      p.warehouse[warehouseIndex].jobKind == JOB_STORY)
    return SIM_CONTRACT_UNAVAILABLE;
  if (!cargo_fits(s, p.warehouse[warehouseIndex]))
    return p.warehouse[warehouseIndex].jobKind == JOB_PASSENGER ? SIM_NO_BERTHS : SIM_NO_CAPACITY;
  s.cargo[s.cargoCount++] = p.warehouse[warehouseIndex];
  warehouse_remove(p, warehouseIndex);
  journal_add(0, "Layover cargo loaded aboard %s.", s.name);
  return SIM_OK;
}

void sim_route_clear() {
  g_ui.routeScroll = 0;
  g_ui.routeCount = 0;
  memset(g_ui.routeDraft, NO_PORT, sizeof(g_ui.routeDraft));
  g_ui.dirty = 1;
}

void sim_route_undo() {
  if (!g_ui.routeCount) return;
  g_ui.routeDraft[--g_ui.routeCount] = NO_PORT;
  g_ui.routeScroll = 0;
  memset(&g_routePreparation, 0, sizeof(g_routePreparation));
  g_ui.routePrepArmed = 0;
  g_ui.dirty = 1;
}

SimResult sim_route_recall() {
  if (g_ui.selectedShip >= g_campaign.shipCount) return SIM_INVALID;
  Ship& s = g_ships[g_ui.selectedShip];
  const RouteTemplate& t = s.routeTemplate;
  if (!t.stopCount || t.stopCount > MAX_ROUTE_LEGS) return SIM_TEMPLATE_REQUIRED;
  sim_route_clear();
  if (s.currentPort == t.origin) {
    for (int i = 0; i < t.stopCount; ++i) {
      SimResult added = sim_route_add(t.stops[i]);
      if (added) continue;
      sim_route_clear();
      return added;
    }
  } else if (s.currentPort == t.stops[t.stopCount - 1]) {
    for (int i = t.stopCount - 2; i >= 0; --i) {
      SimResult added = sim_route_add(t.stops[i]);
      if (added) continue;
      sim_route_clear();
      return added;
    }
    SimResult added = sim_route_add(t.origin);
    if (!added) { sim_route_clear(); return added; }
  } else {
    return SIM_WRONG_PORT;
  }
  return SIM_OK;
}

SimResult sim_route_template_save(uint8_t shipIndex) {
  if (shipIndex >= g_campaign.shipCount || shipIndex != g_ui.selectedShip) return SIM_INVALID;
  if (!g_ui.routeCount) return SIM_BAD_ROUTE;
  Ship& s = g_ships[shipIndex];
  RouteTemplate& t = s.routeTemplate;
  t.origin = s.currentPort;
  t.stopCount = g_ui.routeCount;
  memcpy(t.stops, g_ui.routeDraft, g_ui.routeCount);
  if (t.buyGood >= GOOD_COUNT) t.buyGood = NO_PORT;
  g_routePreparation.computed = 0;
  g_routePreparation.valid = 0;
  journal_add(0, "%s route template saved.", s.name);
  return SIM_OK;
}

void sim_route_template_clear(uint8_t shipIndex) {
  if (shipIndex >= g_campaign.shipCount) return;
  memset(&g_ships[shipIndex].routeTemplate, 0, sizeof(RouteTemplate));
  g_ships[shipIndex].routeTemplate.origin = NO_PORT;
  g_ships[shipIndex].routeTemplate.buyGood = NO_PORT;
  memset(&g_routePreparation, 0, sizeof(g_routePreparation));
  g_ui.dirty = 1;
}

static uint32_t route_prep_fingerprint(uint8_t shipIndex) {
  if (shipIndex >= g_campaign.shipCount) return 0;
  const Ship& s = g_ships[shipIndex];
  const RouteTemplate& t = s.routeTemplate;
  uint32_t h = 2166136261u;
  auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix((uint32_t)g_campaign.coins); mix((uint32_t)g_campaign.recoveryDebt);
  mix(g_campaign.worldEventType); mix(g_campaign.worldEventGood);
  mix(g_campaign.worldEventRegion); mix(g_campaign.worldEventDays);
  mix(s.currentPort); mix(s.state); mix(s.classId); mix(s.condition); mix(s.morale);
  mix(s.crew); mix(s.modules[0]); mix(s.modules[1]); mix(s.captainId); mix(s.cargoCount);
  if (const CaptainProfile* captain = ship_captain_profile(s)) {
    mix(captain->trait); mix(captain->level); mix(captain->loyalty);
  }
  mix(t.origin); mix(t.stopCount);
  mix(t.loadMode); mix(t.sellMode); mix(t.buyGood); mix(t.maxBuyPrice);
  for (int i = 0; i < t.stopCount; ++i) mix(t.stops[i]);
  for (int i = 0; i < s.cargoCount; ++i) {
    const CargoItem& c = s.cargo[i];
    mix(c.contractId); mix(c.paidUnitPrice); mix(c.good); mix(c.quantity);
    mix(c.destination); mix(c.kind); mix(c.jobKind); mix(c.projectRegion);
  }
  if (s.currentPort < MAX_PORTS) {
    for (int g = 0; g < GOOD_COUNT; ++g) mix(g_ports[s.currentPort].stock[g]);
    for (int i = 0; i < MAX_CONTRACTS_PER_PORT; ++i) {
      const Contract& c = g_contracts[s.currentPort][i];
      mix(c.id); mix(c.deadlineMinute); mix(c.reward); mix(c.destination);
      mix(c.good); mix(c.quantity); mix(c.kind); mix(c.projectRegion); mix(c.active);
    }
  }
  return h;
}

static uint8_t template_endpoint(const Ship& s) {
  const RouteTemplate& t = s.routeTemplate;
  if (!t.stopCount) return NO_PORT;
  if (s.currentPort == t.origin) return t.stops[t.stopCount - 1];
  if (s.currentPort == t.stops[t.stopCount - 1]) return t.origin;
  return NO_PORT;
}

static RouteEstimate estimate_route_for_ship(const Ship& s, const uint8_t* route,
                                             uint8_t count, int32_t availableCoins);

SimResult sim_route_prepare_preview(uint8_t shipIndex) {
  memset(&g_routePreparation, 0, sizeof(g_routePreparation));
  if (shipIndex >= g_campaign.shipCount) return SIM_INVALID;
  Ship& original = g_ships[shipIndex];
  if (!ship_at_port(original) || original.state == SHIP_DISABLED) return SIM_BAD_SHIP_STATE;
  uint8_t endpoint = template_endpoint(original);
  if (endpoint == NO_PORT) return SIM_WRONG_PORT;
  Ship temp = original;
  int32_t coins = g_campaign.coins;
  int32_t debt = g_campaign.recoveryDebt;
  const RouteTemplate& t = original.routeTemplate;

  // Sales and purchases are priced unit by unit against a scratch copy of the
  // port's stock, exactly as sim_market_sell / sim_market_buy will do on APPLY.
  uint16_t savedStock[GOOD_COUNT];
  memcpy(savedStock, g_ports[temp.currentPort].stock, sizeof(savedStock));
  int32_t scratchProfit = g_campaign.marketProfit;
  if (t.sellMode == ROUTE_SELL_PROFIT) {
    for (int good = 0; good < GOOD_COUNT; ++good) {
      while (market_first_lot_profitable(temp, (uint8_t)good)) {
        MarketUnit unit{};
        if (!market_trade_unit(temp, (uint8_t)good, false,
                               g_ports[temp.currentPort].stock[good], coins, debt,
                               scratchProfit, unit)) break;
        g_routePreparation.sales += unit.price;
        ++g_routePreparation.soldUnits;
      }
    }
  }

  bool chosen[MAX_CONTRACTS_PER_PORT]{};
  if (t.loadMode == ROUTE_LOAD_ENDPOINT) {
    while (true) {
      int best = -1;
      for (int i = 0; i < MAX_CONTRACTS_PER_PORT; ++i) {
        const Contract& c = g_contracts[temp.currentPort][i];
        if (chosen[i] || c.active != 1 || c.destination != endpoint) continue;
        CargoItem item{};
        item.quantity = c.quantity; item.jobKind = c.kind;
        if (!cargo_fits(temp, item)) continue;
        if (best < 0 || c.reward > g_contracts[temp.currentPort][best].reward) best = i;
      }
      if (best < 0) break;
      chosen[best] = true;
      const Contract& c = g_contracts[temp.currentPort][best];
      CargoItem item{};
      item.contractId = c.id; item.good = c.good; item.quantity = c.quantity;
      item.destination = c.destination; item.kind = CARGO_CONTRACT;
      item.jobKind = c.kind; item.projectRegion = c.projectRegion;
      temp.cargo[temp.cargoCount++] = item;
      ++g_routePreparation.loadedContracts;
    }
  }

  if (t.buyGood < GOOD_COUNT) {
    while (true) {
      uint16_t price = content_market_price(temp.currentPort, t.buyGood, true);
      if (price > t.maxBuyPrice) break;
      MarketUnit unit{};
      if (!market_trade_unit(temp, t.buyGood, true,
                             g_ports[temp.currentPort].stock[t.buyGood], coins, debt,
                             scratchProfit, unit)) break;
      ++g_routePreparation.boughtUnits;
      g_routePreparation.purchases += unit.price;
    }
  }
  memcpy(g_ports[temp.currentPort].stock, savedStock, sizeof(savedStock));

  uint8_t oldSelected = g_ui.selectedShip;
  g_ui.selectedShip = shipIndex;
  SimResult route = sim_route_recall();
  g_ui.selectedShip = oldSelected;
  if (!route) return route;
  RouteEstimate estimate = estimate_route_for_ship(temp, g_ui.routeDraft, g_ui.routeCount, coins);
  if (!estimate.valid && estimate.code != SIM_NO_FUNDS) return estimate.code;
  g_routePreparation.operatingCost = estimate.cost;
  g_routePreparation.projectedCoins = coins - estimate.cost;
  g_routePreparation.etaDays = estimate.days;
  g_routePreparation.risk = estimate.risk;
  g_routePreparation.holdUsed = (uint8_t)sim_ship_used_capacity(temp);
  g_routePreparation.holdCapacity = (uint8_t)sim_ship_capacity(temp);
  g_routePreparation.berthsUsed = (uint8_t)sim_ship_passenger_load(temp);
  g_routePreparation.berthCapacity = (uint8_t)sim_ship_berths(temp);
  g_routePreparation.reversed = original.currentPort != t.origin;
  g_routePreparation.fingerprint = route_prep_fingerprint(shipIndex);
  g_routePreparation.computed = 1;
  g_routePreparation.valid = coins >= estimate.cost;
  g_ui.routePrepArmed = g_routePreparation.valid;
  g_ui.dirty = 1;
  return g_routePreparation.valid ? SimResult(SIM_OK) : SimResult(SIM_NO_FUNDS);
}

SimResult sim_route_prepare_apply(uint8_t shipIndex) {
  if (shipIndex >= g_campaign.shipCount) return SIM_INVALID;
  if (!g_routePreparation.computed) return SIM_TEMPLATE_REQUIRED;
  if (!g_routePreparation.valid) return SIM_NO_FUNDS;
  if (g_routePreparation.fingerprint != route_prep_fingerprint(shipIndex)) {
    sim_route_prepare_preview(shipIndex);
    return SIM_STALE_PREVIEW;
  }
  Ship& s = g_ships[shipIndex];
  uint8_t endpoint = template_endpoint(s);
  const RouteTemplate t = s.routeTemplate;
  g_ui.selectedShip = shipIndex;
  SimResult route = sim_route_recall();
  if (!route) return route;
  if (t.sellMode == ROUTE_SELL_PROFIT) {
    for (int good = 0; good < GOOD_COUNT; ++good) {
      while (true) {
        if (!market_first_lot_profitable(s, (uint8_t)good) ||
            !sim_market_sell(shipIndex, (uint8_t)good)) break;
      }
    }
  }
  if (t.loadMode == ROUTE_LOAD_ENDPOINT) {
    while (true) {
      int best = -1;
      for (int i = 0; i < MAX_CONTRACTS_PER_PORT; ++i) {
        Contract& c = g_contracts[s.currentPort][i];
        if (c.active != 1 || c.destination != endpoint) continue;
        CargoItem item{}; item.quantity = c.quantity; item.jobKind = c.kind;
        if (cargo_fits(s, item) && (best < 0 || c.reward > g_contracts[s.currentPort][best].reward)) best = i;
      }
      if (best < 0 || !sim_load_contract(shipIndex, s.currentPort, (uint8_t)best)) break;
    }
  }
  if (t.buyGood < GOOD_COUNT) {
    while (content_market_price(s.currentPort, t.buyGood, true) <= t.maxBuyPrice &&
           sim_market_buy(shipIndex, t.buyGood)) {}
  }
  bool reversed = g_routePreparation.reversed;
  g_routePreparation.valid = 0;
  g_routePreparation.computed = 0;
  g_ui.routePrepArmed = 0;
  journal_add(0, "%s route prepared %s; departure awaits confirmation.", s.name,
              reversed ? "homeward" : "outward");
  return route;
}

SimResult sim_route_can_add(uint8_t port) {
  if (port >= MAX_PORTS || g_ui.selectedShip >= g_campaign.shipCount) return SIM_INVALID;
  if (!g_ports[port].unlocked) return SIM_PORT_LOCKED;
  if (g_ui.routeCount >= MAX_ROUTE_LEGS) return SIM_FULL;
  const Ship& s = g_ships[g_ui.selectedShip];
  if (!ship_at_port(s) || s.state == SHIP_DISABLED) return SIM_BAD_SHIP_STATE;
  uint8_t from = g_ui.routeCount ? g_ui.routeDraft[g_ui.routeCount - 1] : s.currentPort;
  int lane = content_lane_between(from, port);
  if (lane < 0 || port == from) return SIM_LANE_UNAVAILABLE;
  const ShipClassDef& cls = SHIP_CLASS_DEFS[s.classId];
  if (cls.draft > PORT_DEFS[port].portClass) return SIM_DRAFT_LIMIT;
  if (LANE_DEFS[lane].days > ship_endurance(s)) return SIM_ENDURANCE_LIMIT;
  return SIM_OK;
}

SimResult sim_route_add(uint8_t port) {
  SimResult allowed = sim_route_can_add(port);
  if (!allowed) return allowed;
  g_ui.routeDraft[g_ui.routeCount++] = port;
  g_ui.routeScroll = 0;
  g_routePreparation.computed = 0;
  g_routePreparation.valid = 0;
  g_ui.dirty = 1;
  return SIM_OK;
}

// Wind-adjusted days for one lane. Capped so the leg clock (16-bit minutes)
// cannot wrap on a slow ship in a long headwind, with room left for the
// delays incidents add; the estimate, the voyage and the relief scan all use it.
static constexpr int MAX_LEG_DAYS = 45;
static int leg_days_for_ship(const Ship& s, int lane, uint8_t from) {
  int wind = sim_lane_wind((uint8_t)lane, from);
  int effective = ship_speed(s) * (100 + wind) / 100;
  if (effective < 50) effective = 50;
  int days = (LANE_DEFS[lane].days * 100 + effective - 1) / effective;
  if (days < 1) days = 1;
  if (days > MAX_LEG_DAYS) days = MAX_LEG_DAYS;
  return days;
}

static RouteEstimate estimate_route_for_ship(const Ship& s, const uint8_t* route,
                                             uint8_t count, int32_t availableCoins) {
  RouteEstimate e{};
  e.code = SIM_BAD_ROUTE;
  e.reason = "No route";
  if (!count || count > MAX_ROUTE_LEGS) return e;
  if (!ship_at_port(s) || s.state == SHIP_DISABLED) {
    e.code = SIM_BAD_SHIP_STATE; e.reason = sim_result_text(e.code); return e;
  }
  uint8_t from = s.currentPort;
  int riskTotal = 0;
  uint32_t minutes = 0;
  uint32_t arrivalMinutes[MAX_ROUTE_LEGS]{};
  for (int i = 0; i < count; ++i) {
    uint8_t to = route[i];
    int lane = content_lane_between(from, to);
    if (to >= MAX_PORTS) { e.code = SIM_INVALID; e.reason = sim_result_text(e.code); return e; }
    if (lane < 0) { e.code = SIM_LANE_UNAVAILABLE; e.reason = sim_result_text(e.code); return e; }
    if (!g_ports[to].unlocked) { e.code = SIM_PORT_LOCKED; e.reason = sim_result_text(e.code); return e; }
    if (SHIP_CLASS_DEFS[s.classId].draft > PORT_DEFS[to].portClass) {
      e.code = SIM_DRAFT_LIMIT; e.reason = sim_result_text(e.code); return e;
    }
    const SeaLaneDef& l = LANE_DEFS[lane];
    if (l.days > ship_endurance(s)) {
      e.code = SIM_ENDURANCE_LIMIT; e.reason = sim_result_text(e.code); return e;
    }
    int days = leg_days_for_ship(s, lane, from);
    minutes += (uint32_t)days * 1440u;
    arrivalMinutes[i] = minutes;
    riskTotal += sim_lane_hazard((uint8_t)lane);
    from = to;
  }

  int freightUnits = 0, passengerUnits = 0;
  uint8_t commonDestination = NO_PORT;
  bool sameDestination = true;
  for (int i = 0; i < s.cargoCount; ++i) {
    const CargoItem& c = s.cargo[i];
    if (c.kind != CARGO_CONTRACT) continue;
    if (c.jobKind == JOB_PASSENGER) passengerUnits += c.quantity;
    else freightUnits += c.quantity;
    if (commonDestination == NO_PORT) commonDestination = c.destination;
    else if (commonDestination != c.destination) sameDestination = false;
  }
  bool freightFull = !freightUnits || freightUnits * 100 >=
    sim_ship_capacity(s) * BALANCE.consolidationPercent;
  bool passengerFull = !passengerUnits || passengerUnits * 100 >=
    sim_ship_berths(s) * BALANCE.consolidationPercent;
  bool consolidation = commonDestination != NO_PORT && sameDestination && freightFull && passengerFull;
  // Contracts pay in delivery order, and recovery debt withholds from each in
  // that order, so walk the legs and the cargo the way deliver_at_port does.
  int32_t debt = available_unreserved_debt(g_campaign.recoveryDebt);
  for (int leg = 0; leg < count; ++leg) {
    for (int i = s.cargoCount - 1; i >= 0; --i) {
      const CargoItem& c = s.cargo[i];
      if (c.kind != CARGO_CONTRACT) continue;
      int deliveryLeg = -1;
      for (int r = 0; r < count; ++r) if (route[r] == c.destination) { deliveryLeg = r; break; }
      if (deliveryLeg != leg) continue;
      Contract* contract = find_contract(c.contractId);
      if (!contract) continue;
      uint32_t reward = contract->reward;
      if (contract->kind == JOB_MAIL &&
          g_campaign.worldMinutes + arrivalMinutes[deliveryLeg] > contract->deadlineMinute)
        reward = reward * BALANCE.lateRewardPercent / 100;
      if (consolidation) reward = reward * (100 + BALANCE.consolidationBonusPercent) / 100;
      if (ship_captain_profile(s) && captain_trait(s) == TRAIT_MERCHANT)
        reward = reward * (100 + captain_level(s) * 5) / 100;
      int withheld = 0;
      if (debt > 0) {
        int proposed = (int)(reward * BALANCE.debtWithholdPercent / 100);
        withheld = (int32_t)proposed < debt ? proposed : (int)debt;
        debt -= withheld;
      }
      e.revenue += reward;
      e.netRevenue += reward - withheld;
    }
  }
  int daily = 7 + s.crew / 4 + s.classId * 2;
  if (ship_captain_profile(s) && captain_trait(s) == TRAIT_QUARTERMASTER)
    daily = daily * (100 - min(20, captain_level(s) * 4)) / 100;
  if (daily < 1) daily = 1;
  e.days = (uint16_t)((minutes + 1439u) / 1440u);
  e.realSeconds = (uint16_t)(minutes / SIM_WORLD_MINUTES_PER_REAL_SECOND > 65535u ? 65535u : minutes / SIM_WORLD_MINUTES_PER_REAL_SECOND);
  e.cost = (uint16_t)(e.days * daily);
  e.risk = (uint8_t)(riskTotal / count);
  if (s.condition < 60) e.risk = (uint8_t)(e.risk + 1);
  e.profit = (int32_t)e.revenue - e.cost;
  e.netProfit = (int32_t)e.netRevenue - e.cost;
  if (availableCoins < e.cost) {
    e.code = SIM_NO_FUNDS; e.reason = sim_result_text(e.code); return e;
  }
  e.valid = 1;
  e.code = SIM_OK;
  e.reason = "Ready";
  return e;
}

int sim_leg_days(uint8_t shipIndex, int lane, uint8_t from) {
  if (shipIndex >= g_campaign.shipCount || lane < 0 || lane >= LANE_COUNT) return 0;
  return leg_days_for_ship(g_ships[shipIndex], lane, from);
}

RouteEstimate sim_estimate_route(uint8_t shipIndex, const uint8_t* route, uint8_t count) {
  RouteEstimate e{};
  e.code = SIM_INVALID;
  e.reason = sim_result_text(e.code);
  if (shipIndex >= g_campaign.shipCount) return e;
  return estimate_route_for_ship(g_ships[shipIndex], route, count, g_campaign.coins);
}

static void arm_leg(Ship& s) {
  if (s.routePos == 0) {
    s.voyageEarned = 0; s.voyageDelivered = 0;
    arrivalReceipts[&s - g_ships].valid = 0;
  }  // a new voyage
  uint8_t from = s.currentPort;
  uint8_t to = s.route[s.routePos];
  int lane = content_lane_between(from, to);
  s.fromPort = from;
  s.toPort = to;
  s.legElapsedMinutes = 0;
  if (s.booking.state == BOOKING_SAILING) {
    s.legTotalMinutes = s.booking.legMinutes[s.routePos];
    s.incidentArmed = 0;
    s.state = SHIP_SAILING;
    return;
  }
  if (s.merchant.state == MERCHANT_SAILING) {
    s.legTotalMinutes = s.merchant.legMinutes[s.routePos];
    // One possible encounter per journey, after a safe first voyage.
    s.incidentArmed=0; s.encounterWait=0;
    if(s.routePos==0) {
      s.autoEncounter=0;s.autoDelay=0;
      bool echo=s.sighting==4;
      s.sighting=0;
      if(echo) {s.sighting=3;s.incidentArmed=2;}
      else if(sim_feature_unlocked(FEATURE_INCIDENTS)) {
        // Half hazards, half opportunities; weird encounters are a minority.
        static const uint8_t pool[]={INCIDENT_PIRATES,INCIDENT_STORM,INCIDENT_REEF,
          INCIDENT_RESCUE,INCIDENT_TRADE,INCIDENT_DOUBLE,INCIDENT_ISLAND,INCIDENT_NIGHT_MARKET,INCIDENT_STRANGER};
        uint32_t roll=rng_range(100);
        if(roll<16) {
          uint8_t pick=(uint8_t)(roll<8?rng_range(3):3+rng_range(6));
          if(s.lastEncounter==pool[pick]+1)pick=(uint8_t)(pick<3?(pick+1)%3:3+(pick-3+1)%6);
          static const uint8_t regional[REGION_COUNT][6]={
            {INCIDENT_RESCUE,INCIDENT_TRADE,INCIDENT_TRADE,INCIDENT_DOUBLE,INCIDENT_NIGHT_MARKET,INCIDENT_STRANGER},
            {INCIDENT_RESCUE,INCIDENT_TRADE,INCIDENT_ISLAND,INCIDENT_ISLAND,INCIDENT_DOUBLE,INCIDENT_STRANGER},
            {INCIDENT_RESCUE,INCIDENT_TRADE,INCIDENT_ISLAND,INCIDENT_NIGHT_MARKET,INCIDENT_NIGHT_MARKET,INCIDENT_STRANGER},
            {INCIDENT_RESCUE,INCIDENT_TRADE,INCIDENT_DOUBLE,INCIDENT_DOUBLE,INCIDENT_NIGHT_MARKET,INCIDENT_STRANGER},
            {INCIDENT_RESCUE,INCIDENT_TRADE,INCIDENT_ISLAND,INCIDENT_ISLAND,INCIDENT_NIGHT_MARKET,INCIDENT_DOUBLE},
            {INCIDENT_RESCUE,INCIDENT_TRADE,INCIDENT_STRANGER,INCIDENT_STRANGER,INCIDENT_DOUBLE,INCIDENT_NIGHT_MARKET}};
          uint8_t kind=pick<3?pool[pick]:regional[PORT_DEFS[s.merchant.destination].region][pick-3];
          if(s.lastEncounter==kind+1)kind=kind==INCIDENT_TRADE?INCIDENT_RESCUE:INCIDENT_TRADE;
          s.incidentType=kind;s.incidentArmed=1;
        } else if(roll<24) {s.incidentArmed=2;s.sighting=(uint8_t)(1+rng_range(2));}
      }
    }
    s.state = SHIP_SAILING;
    return;
  }
  int wind = sim_lane_wind((uint8_t)lane, from);
  int days = leg_days_for_ship(s, lane, from);
  s.legTotalMinutes = (uint16_t)(days * 1440);
  int chance = 8 + sim_lane_hazard((uint8_t)lane) * 5;
  if (s.condition < 60) chance += 10;
  if (s.morale < 40) chance += 6;
  if (wind < -10) chance += 5;
  for (int i = 0; i < s.cargoCount; ++i)
    if (s.cargo[i].jobKind == JOB_FREIGHT && s.cargo[i].good >= GOOD_TEA) { chance += 2; break; }
  if (chance > 70) chance = 70;
  s.incidentArmed = rng_range(100) < (uint32_t)chance && sim_feature_unlocked(FEATURE_INCIDENTS);
  uint32_t roll = rng_range(100);
  if (sim_lane_hazard((uint8_t)lane) >= 4 && roll < 28) s.incidentType = INCIDENT_PIRATES;
  else if (roll < 48) s.incidentType = INCIDENT_STORM;
  else if (roll < 59) s.incidentType = INCIDENT_REEF;
  else if (roll < 69) s.incidentType = INCIDENT_ILLNESS;
  else if (roll < 78) s.incidentType = INCIDENT_INSPECTION;
  else if (roll < 86) s.incidentType = INCIDENT_MUTINY;
  else if (roll < 94) s.incidentType = INCIDENT_RESCUE;
  else s.incidentType = INCIDENT_TRADE;
  s.state = SHIP_SAILING;
}

static void story_on_departure(uint8_t index);
#include "sim_voyages.h"
#include "sim_merchant.h"
#include "sim_story.h"
#include "sim_atlas.h"

SimResult sim_dispatch(uint8_t shipIndex) {
  if (shipIndex >= g_campaign.shipCount || shipIndex != g_ui.selectedShip) return SIM_INVALID;
  if (!sim_ship_captain(shipIndex)) return SIM_CAPTAIN_REQUIRED;
  Ship& s = g_ships[shipIndex];
  for (int i = 0; i < s.cargoCount; ++i)
    if (s.cargo[i].jobKind == JOB_PACKAGE || s.cargo[i].jobKind == JOB_STORY) return SIM_CONTRACT_UNAVAILABLE;
  RouteEstimate e = sim_estimate_route(shipIndex, g_ui.routeDraft, g_ui.routeCount);
  if (!e.valid) return e.code;
  s.booking = {}; // an explicitly chosen legacy voyage owns no signed order
  s.merchant = {};
  g_campaign.coins -= e.cost;
  g_campaign.operatingCosts += e.cost;
  s.prepaidCost = e.cost;
  s.routeCount = g_ui.routeCount;
  s.routePos = 0;
  memcpy(s.route, g_ui.routeDraft, g_ui.routeCount);
  sim_route_template_save(shipIndex);

  int freightUnits = 0, passengerUnits = 0;
  uint8_t destination = NO_PORT;
  bool same = true;
  for (int i = 0; i < s.cargoCount; ++i) {
    const CargoItem& c = s.cargo[i];
    if (c.kind != CARGO_CONTRACT) continue;
    if (c.jobKind == JOB_PASSENGER) passengerUnits += c.quantity;
    else freightUnits += c.quantity;
    if (destination == NO_PORT) destination = c.destination;
    else if (destination != c.destination) same = false;
  }
  bool freightFull = !freightUnits || freightUnits * 100 >=
    sim_ship_capacity(s) * BALANCE.consolidationPercent;
  bool passengerFull = !passengerUnits || passengerUnits * 100 >=
    sim_ship_berths(s) * BALANCE.consolidationPercent;
  s.manifestBonus = destination != NO_PORT && same && freightFull && passengerFull;
  arm_leg(s);
  journal_add(1, "%s sailed for %s.", s.name, PORT_DEFS[s.toPort].name);
  sim_route_clear();
  g_ui.card = CARD_NONE;
  g_ui.scene = SCENE_WORLD;
  return SIM_OK;
}

static void fail_ship(Ship& s, const char* cause) {
  for (int i = 0; i < s.cargoCount; ++i) {
    story_on_cargo_lost(s.cargo[i]);
    if (s.cargo[i].kind == CARGO_CONTRACT) {
      Contract* c = find_contract(s.cargo[i].contractId);
      if (c) c->active = 0;
    }
  }
  s.cargoCount = 0;
  s.condition = 25;
  s.currentPort = s.fromPort;
  s.toPort = s.fromPort;
  s.routeCount = s.routePos = 0;
  s.state = SHIP_DISABLED;
  journal_add(2, "%s was disabled by %s; cargo lost.", s.name, cause);
  attention_add(ATTENTION_DISABLED, (uint8_t)(&s - g_ships), s.currentPort, true);
}

static void gain_captain_xp(Ship& s, uint16_t amount) {
  uint8_t shipIndex = (uint8_t)(&s - g_ships);
  CaptainProfile* c = sim_ship_captain(shipIndex);
  if (!c) return;
  uint32_t total = (uint32_t)c->xp + amount;
  c->xp = (uint16_t)(total > 65535u ? 65535u : total);
  while (c->level < BALANCE.maxCaptainLevel &&
         c->xp >= (uint16_t)(c->level * BALANCE.captainXpPerLevel)) {
    ++c->level;
    journal_add(1, "%s reached captain level %u.", c->name, c->level);
  }
}

static int32_t deliver_at_port(Ship& s) {
  int delivered = 0;
  int32_t earned = 0;
  for (int i = s.cargoCount - 1; i >= 0; --i) {
    CargoItem& cargo = s.cargo[i];
    if (cargo.kind != CARGO_CONTRACT || cargo.destination != s.currentPort) continue;
    if (cargo.jobKind == JOB_STORY) {
      int32_t reward = story_deliver(s, i);
      if (reward >= 0) { earned += reward; ++delivered; }
      continue;
    }
    Contract* c = find_contract(cargo.contractId);
    if (cargo.jobKind == JOB_PACKAGE) {
      if (!c || c->active != 2 || c->kind != JOB_PACKAGE || cargo.quantity != 1) continue;
      // The package is customer-owned: its accepted reward is the exact
      // credit, unaffected by duty, lateness, captain bonuses or debt.
      g_campaign.coins += c->reward; earned += c->reward;
      g_campaign.contractRevenue += c->reward;
      ++g_campaign.deliveredUnits;
      if (g_campaign.companyXp < 65535u) ++g_campaign.companyXp;
      if (g_campaign.deliveries < 65535u) ++g_campaign.deliveries;
      if (g_campaign.contractsCompleted < 65535u) ++g_campaign.contractsCompleted;
      s.merchant.packageCoins += c->reward; ++s.merchant.packagesDelivered;
      ++delivered;
      CaptainProfile* captain = sim_ship_captain((uint8_t)(&s - g_ships));
      if (captain && captain->deliveries < 65535u) ++captain->deliveries;
      c->active = 0; cargo_remove(s, i);
      continue;
    }
    if (c) {
      uint32_t reward = c->reward;
      if (c->kind == JOB_MAIL && g_campaign.worldMinutes > c->deadlineMinute)
        reward = reward * BALANCE.lateRewardPercent / 100;
      if (s.manifestBonus) reward = reward * (100 + BALANCE.consolidationBonusPercent) / 100;
      if (ship_captain_profile(s) && captain_trait(s) == TRAIT_MERCHANT)
        reward = reward * (100 + captain_level(s) * 5) / 100;
      int proposedWithheld = (int)(reward * BALANCE.debtWithholdPercent / 100);
      int32_t freeDebt = available_unreserved_debt(g_campaign.recoveryDebt);
      int withheld = freeDebt > 0 ?
        ((int32_t)proposedWithheld < freeDebt ? proposedWithheld : (int)freeDebt) : 0;
      g_campaign.recoveryDebt -= withheld;
      g_campaign.coins += (int32_t)reward - withheld;
      earned += (int32_t)reward - withheld;
      g_campaign.contractRevenue += reward;
      g_campaign.companyXp += cargo.quantity;
      g_campaign.deliveredUnits += cargo.quantity;
      g_campaign.deliveries += cargo.quantity;
      ++g_campaign.contractsCompleted;
      CaptainProfile* captain = sim_ship_captain((uint8_t)(&s - g_ships));
      if (captain) {
        captain->deliveries = (uint16_t)min(65535, (int)captain->deliveries + cargo.quantity);
        if (captain->loyalty < 100) ++captain->loyalty;
      }
      if (cargo.projectRegion < REGION_COUNT && sim_feature_unlocked(FEATURE_CHARTERS)) {
        uint16_t before = g_campaign.charterProgress[cargo.projectRegion];
        uint16_t credit = cargo.quantity * (cargo.projectRegion > 1 ? BALANCE.laterProjectCredit : 1);
        g_campaign.charterProgress[cargo.projectRegion] += credit;
        if (g_campaign.economyVersion) {
          // Original charter cargo keeps the progress promised when it was
          // accepted. Add only this delivery's converted increment so new
          // signed materials delivered in the meantime are preserved.
          uint8_t region = cargo.projectRegion;
          uint32_t oldTarget = BALANCE.charterTarget[region];
          if (oldTarget) {
            uint32_t target = sim_project_target(region);
            uint32_t beforeUnits = min((uint32_t)before, oldTarget) * 2u * target / oldTarget;
            uint32_t afterUnits = min((uint32_t)g_campaign.charterProgress[region], oldTarget) * 2u * target / oldTarget;
            uint32_t timberDelta = afterUnits / 2u - beforeUnits / 2u;
            uint32_t ironwareDelta = (afterUnits - afterUnits / 2u) - (beforeUnits - beforeUnits / 2u);
            g_campaign.projectCargo[region][0] = (uint16_t)min(target, (uint32_t)g_campaign.projectCargo[region][0] + timberDelta);
            g_campaign.projectCargo[region][1] = (uint16_t)min(target, (uint32_t)g_campaign.projectCargo[region][1] + ironwareDelta);
          }
        }
        if (before < BALANCE.charterTarget[cargo.projectRegion] &&
            g_campaign.charterProgress[cargo.projectRegion] >= BALANCE.charterTarget[cargo.projectRegion])
          attention_add(ATTENTION_CHARTER, NO_SHIP, s.currentPort, false, cargo.projectRegion);
      }
      c->active = 0;
      delivered += cargo.quantity;
      gain_captain_xp(s, cargo.quantity);
    }
    cargo_remove(s, i);
  }
  if (delivered) {
    journal_add(1, "%s delivered %d unit%s at %s.", s.name, delivered,
                delivered == 1 ? "" : "s", PORT_DEFS[s.currentPort].name);
    audio_notice();
    update_rank();
  }
  // The arrival report covers the whole voyage, not only its last leg.
  s.voyageEarned += earned;
  s.voyageDelivered = (uint16_t)min(65535, (int)s.voyageDelivered + delivered);
  if (delivered || earned) return_record_legacy_credit(s, earned, (uint32_t)delivered);
  return earned;
}

static void arrive_leg(Ship& s) {
  s.currentPort = s.toPort;
  CaptainProfile* captain = sim_ship_captain((uint8_t)(&s - g_ships));
  if (captain) captain->currentPort = s.currentPort;
  bool booked = s.booking.state == BOOKING_SAILING;
  bool merchant = s.merchant.state == MERCHANT_SAILING;
  if (!booked && !merchant) s.condition = (uint8_t)(s.condition > 1 ? s.condition - 1 : 1);
  for (int i = 0; i < s.cargoCount; ++i)
    if (s.cargo[i].kind == CARGO_MARKET && s.cargo[i].sourcePort < MAX_PORTS &&
        s.cargo[i].sourcePort != s.currentPort) s.cargo[i].sailed = 1;
  uint8_t visitedBefore = g_campaign.visitedMask;
  g_campaign.visitedMask |= (uint8_t)(1u << PORT_DEFS[s.currentPort].region);
  atlas_chart(s);
  if (visitedBefore != 0x3F && g_campaign.visitedMask == 0x3F)
    for (int p = 0; p < MAX_PORTS; ++p)
      if (g_ports[p].unlocked) sim_generate_jobs((uint8_t)p, true);
  settle_booking(s);
  deliver_at_port(s);
  ++s.routePos;
  if (s.routePos < s.routeCount) {
    arm_leg(s);
  } else {
    s.state = SHIP_ARRIVED;
    s.routeCount = s.routePos = 0;
    if (merchant) {
      s.merchant.state = MERCHANT_ARRIVED; s.merchant.receiptUnread = 1;
      ++g_campaign.completedVoyages;
      story_on_arrival(s);
    }
    uint8_t shipIndex = (uint8_t)(&s - g_ships);
    bool homecoming = s.currentPort == 0 && g_campaign.visitedMask == 0x3F && !g_campaign.won;
    arrivalReceipts[shipIndex] = ArrivalReceipt{g_campaign.worldMinutes, s.voyageEarned,
      s.currentPort, (uint8_t)min(255, (int)s.voyageDelivered), (uint8_t)homecoming, 1};
    bool quietArrival = false;
    quietArrival = g_ui.title || g_ui.returnQuiet;
    // An ordinary arrival must not change the ship or footer target of the
    // report being read. Its own notice can reopen its own receipt afterward.
    if (!quietArrival && (g_ui.card != CARD_ARRIVAL || !g_ui.arrivalSummaryVisible || homecoming))
      show_arrival_receipt(shipIndex);
    bool guidedArrival = sim_tutorial_guided();
    sim_tutorial_complete(TUTORIAL_ARRIVAL);
    if (guidedArrival && !quietArrival) g_ui.card = CARD_ARRIVAL;
    journal_add(1, "%s arrived at %s.", s.name, PORT_DEFS[s.currentPort].name);
    attention_add(ATTENTION_ARRIVAL, (uint8_t)(&s - g_ships), s.currentPort, false);
    if (captain && captain->voyages < 65535) ++captain->voyages;
    if (homecoming) {
      g_campaign.won = 1;
      g_campaign.rank = 5;
      if (!quietArrival) { g_ui.arrivalSummaryWon = 1; g_ui.card = CARD_ARRIVAL; }
      journal_add(2, "The Grand Charter: the company has circled the world.");
      journal_add(2, "Company rank: %s.", content_rank_name(g_campaign.rank));
    }
    return_record_arrival(s, booked);
  }
  save_write();
}

static uint8_t incident_risk(const Ship& s, uint8_t kind) {
  int strength = s.condition / 20 + captain_level(s);
  if (kind == INCIDENT_PIRATES) strength += ship_guns(s) + ship_speed(s) / 50;
  else if (kind == INCIDENT_STORM || kind == INCIDENT_REEF) strength += ship_storm(s) * 2;
  else strength += s.morale / 25;
  if (kind == INCIDENT_ILLNESS && ship_captain_profile(s) &&
      captain_trait(s) == TRAIT_SURGEON) strength += 4;
  if (kind == INCIDENT_RESCUE && ship_captain_profile(s) &&
      captain_trait(s) == TRAIT_NAVIGATOR) strength += 3;  // a navigator finds the boats
  if ((kind == INCIDENT_INSPECTION || kind == INCIDENT_MUTINY) &&
      ship_captain_profile(s) && captain_trait(s) == TRAIT_QUARTERMASTER) strength += 2;
  if (kind == INCIDENT_TRADE && ship_captain_profile(s) &&
      captain_trait(s) == TRAIT_MERCHANT) strength += 3;
  int lane = content_lane_between(s.fromPort, s.toPort);
  int threat = 6 + (lane >= 0 ? sim_lane_hazard((uint8_t)lane) : 2);
  if (strength >= threat + 3) return RISK_FAVORABLE;
  if (strength + 1 >= threat) return RISK_EVEN;
  return RISK_DANGEROUS;
}

static void update_ship(Ship& s, uint8_t index, uint32_t minutes) {
  if (s.state == SHIP_ARRIVED) { s.state = SHIP_DOCKED; return; }
  if(s.state==SHIP_AWAITING_DECISION && s.merchant.state==MERCHANT_SAILING) {
    if(!s.encounterWait)s.encounterWait=30*SIM_WORLD_MINUTES_PER_REAL_SECOND;
    bool opportunity=encounter_opportunity(s.incidentType);
    uint32_t used=min(minutes,(uint32_t)s.encounterWait);
    if(opportunity) used=min(used,(uint32_t)(s.legTotalMinutes-s.legElapsedMinutes));
    s.encounterWait-=used;
    if(opportunity)s.legElapsedMinutes+=(uint16_t)used;
    if(!s.encounterWait || (opportunity && s.legElapsedMinutes>=s.legTotalMinutes)) {
      uint8_t previous=g_incident.active?g_incident.shipIndex:NO_SHIP;
      if(sim_select_incident(index)) {
        SimResult result=sim_merchant_incident_resolve(encounter_fallback(s.incidentType),true);
        if(previous!=index && previous<g_campaign.shipCount)sim_select_incident(previous);
        if(result.code==SIM_OK && (minutes>used || s.legElapsedMinutes>=s.legTotalMinutes)) update_ship(s,index,minutes-used);
      }
    }
    return;
  }
  if (s.state != SHIP_SAILING) return;
  uint32_t next = (uint32_t)s.legElapsedMinutes + minutes;
  uint16_t half = s.legTotalMinutes / 2;
  if (s.incidentArmed && s.legElapsedMinutes < half && next >= half) {
    if(s.incidentArmed==2) {
      s.incidentArmed=0;
      journal_add(1,s.sighting==1?"%s: lights wheel beneath the keel.":s.sighting==3?"%s: our other ship returns, then fades. No trade this time.":"%s: an albatross follows us beyond the last light.",s.name);
      update_ship(s,index,minutes);return;
    }
    s.legElapsedMinutes = half;
    s.encounterWait=30*SIM_WORLD_MINUTES_PER_REAL_SECOND;
    s.lastEncounter=s.incidentType+1;
    s.state = SHIP_AWAITING_DECISION;
    s.incidentArmed = 0;
    Incident pending{1, index, s.incidentType, incident_risk(s, s.incidentType)};
    if (!g_incident.active) {
      g_incident = pending;
      g_ui.dirty = 1;
      audio_alert();
      attention_add(ATTENTION_INCIDENT, index, s.currentPort, true);
    } else if (g_incidentQueueCount < MAX_INCIDENTS) {
      g_incidentQueue[g_incidentQueueCount++] = pending;
      journal_add(2, "%s awaits orders at sea.", s.name);
      attention_add(ATTENTION_INCIDENT, index, s.currentPort, true);
    } else {
      // No room to wait for orders: the crew coped, and there is nothing to decide.
      s.state = SHIP_SAILING;
      journal_add(1, "%s handled a minor incident without orders.", s.name);
    }
    if(next>half && s.merchant.state==MERCHANT_SAILING)update_ship(s,index,next-half);
    return;
  }
  if (next >= s.legTotalMinutes) {
    uint32_t remaining = next - s.legTotalMinutes;
    arrive_leg(s);
    // A standby catch-up step can straddle a harbor. Carry its remainder
    // into the next signed leg so the sum of quoted minutes stays exact.
    if (remaining && s.state == SHIP_SAILING &&
        (s.booking.state == BOOKING_SAILING || s.merchant.state == MERCHANT_SAILING))
      update_ship(s, index, remaining);
  } else s.legElapsedMinutes = (uint16_t)next;
}

void sim_advance_minutes(uint32_t minutes) {
  sim_stage_reconcile(true);
  for (int p = 0; p < MAX_PORTS; ++p) compact_contracts((uint8_t)p);  // after deliveries and expiries
  if (g_campaign.paused || !minutes) return;
  while (minutes) {
    uint32_t step = minutes > 60 ? 60 : minutes;
    g_campaign.worldMinutes += step;
    for (uint8_t i = 0; i < g_campaign.shipCount; ++i) update_ship(g_ships[i], i, step);
    sim_refresh_day();
    if (g_campaign.economyVersion >= 2) for (uint8_t p = 0; p < MAX_PORTS; ++p)
      if (g_ports[p].unlocked) merchant_refresh_packages(p);
    minutes -= step;
  }
  g_ui.dirty = 1;
}

static void jettison_one(Ship& s) {
  for (int i = 0; i < s.cargoCount; ++i) {
    if (s.cargo[i].kind == CARGO_MARKET) {
      journal_add(1, "%s jettisoned %s.", s.name,
                  content_cargo_name(s.cargo[i].kind, s.cargo[i].jobKind, s.cargo[i].good, false));
      market_remove_unit(s.cargo[i]);
      if (!s.cargo[i].quantity) cargo_remove(s, i);
      return;
    }
  }
  int victim = -1;
  for (int i = 0; i < s.cargoCount && victim < 0; ++i) if (!story_cargo(s.cargo[i])) victim = i;
  if (victim >= 0) {
    CargoItem lost = s.cargo[victim];
    if (lost.kind == CARGO_CONTRACT) {
      Contract* contract = find_contract(lost.contractId);
      if (contract) contract->active = 0;
      journal_add(2, "%s forfeited its %s contract.", s.name,
                  content_cargo_name(CARGO_CONTRACT, lost.jobKind, lost.good, false));
    }
    cargo_remove(s, victim);
  }
}

static void outcome_cargo_change(const CargoItem& cargo, int sign) {
  for (int i = 0; i < g_incidentOutcome.cargoCount; ++i) {
    IncidentCargoChange& change = g_incidentOutcome.cargo[i];
    if (change.kind == cargo.kind && change.jobKind == cargo.jobKind && change.good == cargo.good) {
      change.quantityDelta += (int16_t)(sign * cargo.quantity);
      return;
    }
  }
  if (g_incidentOutcome.cargoCount >= MAX_CARGO_ITEMS) return;
  IncidentCargoChange& change = g_incidentOutcome.cargo[g_incidentOutcome.cargoCount++];
  change.kind = cargo.kind;
  change.jobKind = cargo.jobKind;
  change.good = cargo.good;
  change.quantityDelta = (int16_t)(sign * cargo.quantity);
}

static void capture_incident_outcome(const Incident& incident, uint8_t option,
                                    const Ship& before, int32_t coinsBefore,
                                    const CaptainProfile* captainBefore) {
  uint32_t serial = g_incidentOutcome.serial + 1;
  memset(&g_incidentOutcome, 0, sizeof(g_incidentOutcome));
  IncidentOutcome& outcome = g_incidentOutcome;
  const Ship& after = g_ships[incident.shipIndex];
  outcome.serial = serial ? serial : 1;
  outcome.worldMinute = g_campaign.worldMinutes;
  outcome.valid = 1;
  outcome.shipIndex = incident.shipIndex;
  outcome.classId = before.classId;
  outcome.kind = incident.kind;
  outcome.option = option;
  outcome.risk = incident.risk;
  outcome.disabled = after.state == SHIP_DISABLED;
  snprintf(outcome.shipName, sizeof(outcome.shipName), "%s", before.name);
  outcome.treasuryDelta = g_campaign.coins - coinsBefore;
  outcome.hullDelta = (int16_t)((int)after.condition - before.condition);
  outcome.moraleDelta = (int16_t)((int)after.morale - before.morale);
  outcome.crewDelta = (int16_t)((int)after.crew - before.crew);
  outcome.hullAfter = after.condition;
  outcome.moraleAfter = after.morale;
  outcome.crewAfter = after.crew;
  int beforeLeft = max(0, (int)before.legTotalMinutes - before.legElapsedMinutes);
  int afterLeft = max(0, (int)after.legTotalMinutes - after.legElapsedMinutes);
  outcome.delayMinutes = afterLeft - beforeLeft;
  for (int i = 0; i < before.cargoCount; ++i) outcome_cargo_change(before.cargo[i], -1);
  for (int i = 0; i < after.cargoCount; ++i) outcome_cargo_change(after.cargo[i], 1);
  int kept = 0;
  for (int i = 0; i < outcome.cargoCount; ++i)
    if (outcome.cargo[i].quantityDelta) outcome.cargo[kept++] = outcome.cargo[i];
  outcome.cargoCount = (uint8_t)kept;
  // Zero unused entries so the receipt's byte fingerprint is deterministic.
  memset(outcome.cargo + kept, 0, sizeof(outcome.cargo[0]) * (MAX_CARGO_ITEMS - kept));
  outcome.captainId = NO_SHIP;
  const CaptainProfile* captainAfter = sim_ship_captain(incident.shipIndex);
  if (captainBefore && captainAfter) {
    outcome.captainId = before.captainId;
    outcome.captainTrait = captainBefore->trait;
    snprintf(outcome.captainName, sizeof(outcome.captainName), "%s", captainBefore->name);
    outcome.captainXpDelta = (uint16_t)(captainAfter->xp - captainBefore->xp);
    outcome.captainLoyaltyDelta = (int16_t)((int)captainAfter->loyalty - captainBefore->loyalty);
    outcome.captainLoyaltyAfter = captainAfter->loyalty;
    outcome.captainLevelBefore = captainBefore->level;
    outcome.captainLevelAfter = captainAfter->level;
    uint8_t previousScars = 0;
    for (int i = 0; i < captainBefore->scarCount; ++i)
      previousScars |= (uint8_t)(1u << captainBefore->scars[i]);
    for (int i = 0; i < captainAfter->scarCount; ++i) {
      uint8_t scar = captainAfter->scars[i];
      if (scar != SCAR_NONE && !(previousScars & (1u << scar)))
        outcome.captainNewScarMask |= (uint8_t)(1u << scar);
    }
  }
}

#include "sim_merchant_incidents.h"

void sim_resolve_incident(uint8_t option) {
  if (g_incident.active && g_incident.shipIndex < g_campaign.shipCount && g_ships[g_incident.shipIndex].merchant.state == MERCHANT_SAILING) { sim_merchant_incident_resolve(option); return; }
  if (!g_incident.active || g_incident.shipIndex >= g_campaign.shipCount || option > 2) return;
  Ship& s = g_ships[g_incident.shipIndex];
  const Incident resolvedIncident = g_incident;
  const Ship before = s;
  const int32_t coinsBefore = g_campaign.coins;
  const CaptainProfile* initialCaptain = sim_ship_captain(g_incident.shipIndex);
  const CaptainProfile captainBefore = initialCaptain ? *initialCaptain : CaptainProfile{};
  uint8_t kind = g_incident.kind;
  int advantage = g_incident.risk == RISK_FAVORABLE ? BALANCE.incidentFavorableBonus :
                  (g_incident.risk == RISK_DANGEROUS ?
                   -(int)BALANCE.incidentDangerousPenalty : 0);
  int success = (int)rng_range(100) + advantage;
  CaptainProfile* captain = sim_ship_captain(g_incident.shipIndex);
  if (captain) {
    int loyalty = ((int)captain->loyalty - BALANCE.captainLoyaltyNeutral) /
                  BALANCE.captainLoyaltyStep;
    if (loyalty < -(int)BALANCE.captainLoyaltyMaxModifier)
      loyalty = -(int)BALANCE.captainLoyaltyMaxModifier;
    if (loyalty > BALANCE.captainLoyaltyMaxModifier)
      loyalty = BALANCE.captainLoyaltyMaxModifier;
    success += loyalty;
    uint8_t wantedScar = kind == INCIDENT_STORM ? SCAR_STORM :
                         (kind == INCIDENT_PIRATES ? SCAR_PIRATE :
                         (kind == INCIDENT_REEF ? SCAR_REEF : SCAR_NONE));
    for (int i = 0; i < captain->scarCount; ++i)
      if (captain->scars[i] == wantedScar) success += BALANCE.captainScarModifier;
  }
  const char* cause = "the incident";

  if (kind == INCIDENT_PIRATES) {
    cause = "pirates";
    if (option == 0) {
      success += ship_speed(s) / 5;
      if (success < 65) { hull_damage(s, 18); jettison_one(s); }
      else journal_add(1, "%s outran the privateer.", s.name);
    } else if (option == 1) {
      success += ship_guns(s) * 8;
      hull_damage(s, 10);
      if (success >= 70) {
        g_campaign.coins += 90; g_campaign.incidentNet += 90;
        journal_add(1, "%s beat off the privateer.", s.name);
      }
      else { hull_damage(s, 20); jettison_one(s); }
    } else {
      int bribe = 70 + s.classId * 25;
      if (ship_captain_profile(s) && captain_trait(s) == TRAIT_MERCHANT)
        bribe = bribe * 85 / 100;
      if (g_campaign.coins >= bribe) {
        g_campaign.coins -= bribe; g_campaign.incidentNet -= bribe;
        journal_add(0, "%s bought safe passage for %d.", s.name, bribe);
      }
      else jettison_one(s);
    }
  } else if (kind == INCIDENT_STORM) {
    cause = "the storm";
    if (option == 0) { s.legElapsedMinutes += 360; s.morale = (uint8_t)min(100, s.morale + 4); }
    else if (option == 1) { s.legTotalMinutes = (uint16_t)min(65000, s.legTotalMinutes + 720); }
    else {
      success += ship_storm(s) * 10;
      if (success < 75) hull_damage(s, 24);
      else journal_add(1, "%s carried sail through the storm.", s.name);
    }
  } else if (kind == INCIDENT_REEF) {
    cause = "a reef";
    if (option == 0) s.legTotalMinutes = (uint16_t)min(65000, s.legTotalMinutes + 480);
    else if (option == 1) {
      { int fee = min((int)g_campaign.coins, 35); g_campaign.coins -= fee; g_campaign.incidentNet -= fee; }
      s.legTotalMinutes = (uint16_t)min(65000, s.legTotalMinutes + 180);
    }
    else if (success < 80) hull_damage(s, 30);
  } else if (kind == INCIDENT_ILLNESS) {
    cause = "illness";
    int crowded = sim_ship_passenger_load(s) > 0 ? 1 : 0;  // sickness spreads below deck
    if (option == 0) {
      bool medicine = false;
      for (int i = 0; i < s.cargoCount; ++i)
        if (s.cargo[i].kind == CARGO_MARKET && s.cargo[i].good == GOOD_MEDICINES) {
          market_remove_unit(s.cargo[i]);
          if (!s.cargo[i].quantity) cargo_remove(s, i);
          medicine = true; break;
        }
      if (medicine) s.morale = (uint8_t)min(100, s.morale + 8);
      else {
        int loss = (ship_captain_profile(s) && captain_trait(s) == TRAIT_SURGEON ? 1 : 2) + crowded;
        s.crew = (uint8_t)(s.crew > loss ? s.crew - loss : 1);
      }
    } else if (option == 1) { s.legTotalMinutes += 720; s.morale = (uint8_t)min(100, s.morale + 3); }
    else {
      int loss = 3 + crowded, blow = 12 + crowded * 4;
      s.crew = (uint8_t)(s.crew > loss ? s.crew - loss : 1);
      s.morale = (uint8_t)(s.morale > blow ? s.morale - blow : 0);
    }
  } else {
    if (kind == INCIDENT_INSPECTION) {
      cause = "an inspection";
      if (option == 0) { int fee = min((int)g_campaign.coins, 25); g_campaign.coins -= fee; g_campaign.incidentNet -= fee; }
      else if (option == 1 && success < 55) {
        { int fine = min((int)g_campaign.coins, 90); g_campaign.coins -= fine; g_campaign.incidentNet -= fine; }
        s.morale = (uint8_t)(s.morale > 8 ? s.morale - 8 : 0);
      } else if (option == 2) s.legTotalMinutes += 360;
    } else if (kind == INCIDENT_MUTINY) {
      cause = "mutiny";
      if (option == 0) {
        s.legTotalMinutes = (uint16_t)min(65000, s.legTotalMinutes + 480);
        s.morale = (uint8_t)min(100, s.morale + 15);
      } else if (option == 1) {
        int bonus = min((int)g_campaign.coins, 60 + s.crew * 2);
        g_campaign.coins -= bonus; g_campaign.incidentNet -= bonus;
        s.morale = (uint8_t)min(100, s.morale + 20);
      } else if (success < 65) {
        hull_damage(s, 16);
        s.morale = 25;
      } else {
        s.morale = (uint8_t)min(100, s.morale + 6);
      }
    } else if (kind == INCIDENT_RESCUE) {
      cause = "a rescue";
      if (option == 0) {
        s.legTotalMinutes = (uint16_t)min(65000, s.legTotalMinutes + 360);
        g_campaign.coins += 45; g_campaign.incidentNet += 45;
        s.morale = (uint8_t)min(100, s.morale + 10);
        gain_captain_xp(s, 2);
        if (captain && captain->rescues < 65535) ++captain->rescues;
        journal_add(1, "%s rescued survivors and earned a reward.", s.name);
      } else if (option == 1) {
        s.morale = (uint8_t)(s.morale > 4 ? s.morale - 4 : 0);
      } else {
        CargoItem salvage{};
        salvage.good = (uint8_t)(GOOD_TEA + rng_range(GOOD_COUNT - GOOD_TEA));
        salvage.quantity = 1;
        salvage.destination = NO_PORT;
        salvage.kind = CARGO_MARKET;
        salvage.jobKind = JOB_FREIGHT;
        salvage.projectRegion = 0xFF;
        if (cargo_fits(s, salvage)) s.cargo[s.cargoCount++] = salvage;
        else s.legTotalMinutes = (uint16_t)min(65000, s.legTotalMinutes + 180);
      }
    } else {
      cause = "a trade offer";
      CargoItem offer{};
      offer.good = (uint8_t)(GOOD_TEA + rng_range(GOOD_COUNT - GOOD_TEA));
      offer.quantity = 1;
      offer.destination = NO_PORT;
      offer.kind = CARGO_MARKET;
      offer.jobKind = JOB_FREIGHT;
      offer.projectRegion = 0xFF;
      int price = GOOD_DEFS[offer.good].basePrice * (option == 1 ? 55 : 70) / 100;
      if (option < 2 && g_campaign.coins >= price && cargo_fits(s, offer)) {
        offer.paidUnitPrice = (uint16_t)price;
        g_campaign.coins -= price; g_campaign.incidentNet -= price;
        s.cargo[s.cargoCount++] = offer;
        journal_add(1, "%s bought %s at sea.", s.name, GOOD_DEFS[offer.good].name);
      } else if (option < 2) {
        journal_add(0, g_campaign.coins < price ? "%s could not afford the trader's goods." :
                    "%s had no room for the trader's goods.", s.name);
      }
    }
  }

  ++g_campaign.incidentsResolved;
  gain_captain_xp(s, 1);
  if (captain) {
    if (s.condition > 0 && captain->incidentsSurvived < 65535) ++captain->incidentsSurvived;
    int loyaltyChange = kind == INCIDENT_RESCUE && option == 0 ? 3 :
                        (kind == INCIDENT_MUTINY ? (option < 2 ? 2 : -5) :
                        (kind == INCIDENT_PIRATES && option == 2 ? -2 : 1));
    int loyalty = captain->loyalty + loyaltyChange;
    captain->loyalty = (uint8_t)(loyalty < 0 ? 0 : (loyalty > 100 ? 100 : loyalty));
    uint8_t scar = kind == INCIDENT_STORM ? SCAR_STORM :
                   (kind == INCIDENT_PIRATES ? SCAR_PIRATE :
                   (kind == INCIDENT_REEF ? SCAR_REEF : SCAR_NONE));
    if (scar != SCAR_NONE && g_incident.risk == RISK_DANGEROUS &&
        s.condition < BALANCE.captainScarConditionThreshold &&
        s.condition > 0 && captain->scarCount < BALANCE.maxCaptainScars) {
      bool duplicate = false;
      for (int i = 0; i < captain->scarCount; ++i) if (captain->scars[i] == scar) duplicate = true;
      if (!duplicate) captain->scars[captain->scarCount++] = scar;
    }
  }
  if (!s.condition) fail_ship(s, cause);
  else s.state = SHIP_SAILING;
  capture_incident_outcome(resolvedIncident, option, before, coinsBefore,
                           initialCaptain ? &captainBefore : nullptr);
  memset(&g_incident, 0, sizeof(g_incident));
  attention_resolve(ATTENTION_INCIDENT, (uint8_t)(&s - g_ships));
  g_ui.card = CARD_NONE;
  activate_next_incident();
  if (g_incident.active)  // the next ship's turn: its notice comes back unread
    attention_add(ATTENTION_INCIDENT, g_incident.shipIndex,
                  g_ships[g_incident.shipIndex].currentPort, true);
  g_ui.dirty = 1;
}

bool sim_incident_option_cost(uint8_t option, int* coins) {
  *coins = 0;
  if (!g_incident.active || g_incident.shipIndex >= g_campaign.shipCount || option > 2) return false;
  const Ship& s = g_ships[g_incident.shipIndex];
  switch (g_incident.kind) {
    case INCIDENT_PIRATES: {
      if (option != 2) return false;
      int bribe = 70 + s.classId * 25;
      if (ship_captain_profile(s) && captain_trait(s) == TRAIT_MERCHANT) bribe = bribe * 85 / 100;
      if ((int)g_campaign.coins < bribe) return false;  // unaffordable: cargo goes overboard instead
      *coins = -bribe;
      return true;
    }
    case INCIDENT_REEF: if (option != 1) return false; *coins = -min((int)g_campaign.coins, 35); return true;
    case INCIDENT_INSPECTION: if (option != 0) return false; *coins = -min((int)g_campaign.coins, 25); return true;
    case INCIDENT_MUTINY:
      if (option != 1) return false;
      *coins = -min((int)g_campaign.coins, 60 + s.crew * 2);
      return true;
    case INCIDENT_RESCUE: if (option != 0) return false; *coins = 45; return true;
    default: return false;
  }
}

int sim_selected_docked_ship(bool includeDisabled) {
  auto available = [&](const Ship& s) {
    return ship_at_port(s) && (includeDisabled || s.state != SHIP_DISABLED) &&
           s.currentPort == g_ui.selectedPort;
  };
  if (g_ui.selectedShip < g_campaign.shipCount) {
    const Ship& s = g_ships[g_ui.selectedShip];
    if (available(s)) return g_ui.selectedShip;
  }
  for (int i = 0; i < g_campaign.shipCount; ++i)
    if (available(g_ships[i])) return i;
  return -1;
}

uint16_t sim_ship_package_cost(uint8_t classId) {
  if (classId >= SHIP_CLASS_COUNT) return 0;
  return g_campaign.economyVersion && classId == 0 ? 900 : SHIP_CLASS_DEFS[classId].cost;
}

SimResultCode sim_ship_purchase_block(uint8_t classId) {
  if (classId >= SHIP_CLASS_COUNT) return SIM_INVALID;
  if (g_campaign.shipCount >= MAX_SHIPS) return SIM_FULL;
  uint8_t p = g_ui.selectedPort;
  const ShipClassDef& cls = SHIP_CLASS_DEFS[classId];
  if (p >= MAX_PORTS) return SIM_WRONG_PORT;
  if (g_campaign.economyVersion && g_campaign.deliveredUnits < 24 && g_campaign.stageSeen < 2)
    return SIM_RANK_REQUIRED;
  uint8_t requiredYard = classId == 0 ? 0 : (classId == 1 ? 1 : (classId <= 3 ? 2 : 3));
  if (!g_ports[p].unlocked || cls.draft > PORT_DEFS[p].portClass) return SIM_WRONG_PORT;
  if (cls.rank > g_campaign.rank) return SIM_RANK_REQUIRED;
  if (g_ports[p].shipyardLevel < requiredYard) return SIM_FACILITY_REQUIRED;
  if (g_campaign.economyVersion) {
    Ship package{}; package.classId = classId; package.currentPort = p; package.captainId = NO_SHIP;
    bool useful = false;
    for (uint8_t destination = 0; destination < MAX_PORTS && !useful; ++destination) {
      bool matching = false;
      for (int good = 0; good < GOOD_COUNT; ++good)
        if (port_makes(p, (uint8_t)good) &&
            (port_wants(destination, (uint8_t)good) || project_for_order(destination, (uint8_t)good) < REGION_COUNT))
          matching = true;
      if (!matching) continue;
      VoyageQuote route{};
      useful = booked_route(package, destination, route);
    }
    if (!useful) return SIM_ENDURANCE_LIMIT;
  }
  int price = sim_ship_package_cost(classId);
  if (g_campaign.coins < price + (g_campaign.economyVersion ? 200 : 0)) return SIM_NO_FUNDS;
  if (g_campaign.economyVersion) {
    bool freeCaptain = false;
    for (int i = 0; i < MAX_CAPTAINS; ++i) if (!g_captains[i].active) freeCaptain = true;
    if (!freeCaptain) return SIM_CAPTAIN_UNAVAILABLE;
  }
  return SIM_OK;
}

SimResult sim_buy_ship(uint8_t classId) {
  SimResultCode block = sim_ship_purchase_block(classId);
  if (block != SIM_OK) return block;
  uint8_t p = g_ui.selectedPort;
  Campaign campaignBefore = g_campaign;
  Ship shipBefore = g_ships[g_campaign.shipCount];
  CaptainProfile captainsBefore[MAX_CAPTAINS];
  memcpy(captainsBefore, g_captains, sizeof(captainsBefore));
  UiState uiBefore = g_ui;
  uint8_t headBefore = g_journalHead, countBefore = g_journalCount;
  JournalEntry journalBefore = g_journal[headBefore];
  int price = sim_ship_package_cost(classId);
  g_campaign.coins -= price;
  g_campaign.capitalSpending += price;
  int i = g_campaign.shipCount++;
  init_ship(g_ships[i], classId, p, i, g_campaign.economyVersion != 0);
  g_ui.selectedShip = (uint8_t)i;
  if (g_campaign.economyVersion) journal_add(2, "%s and captain ready at %s.", g_ships[i].name, PORT_DEFS[p].name);
  else journal_add(2, "%s joined at %s; assign a captain.", g_ships[i].name, PORT_DEFS[p].name);
  if (g_campaign.economyVersion && !save_write()) {
    g_campaign = campaignBefore; g_ships[i] = shipBefore; g_ui = uiBefore;
    memcpy(g_captains, captainsBefore, sizeof(captainsBefore));
    g_journalHead = headBefore; g_journalCount = countBefore; g_journal[headBefore] = journalBefore;
    return SIM_SAVE_FAILED;
  }
  return SimResult(SIM_OK, i);
}

// The warehouse reaches its 12-stack limit at level 2. Keep saved level-3
// warehouses usable, but never sell another upgrade that adds no capacity.
// Offices and shipyards reach their final benefit at level 3.
uint8_t sim_port_upgrade_max_level(uint8_t kind) { return kind == 1 ? 2 : 3; }

SimResult sim_buy_port_upgrade(uint8_t kind) {
  uint8_t p = g_ui.selectedPort;
  if (p >= MAX_PORTS || !g_ports[p].unlocked || kind > 2) return SIM_INVALID;
  uint8_t* level = kind == 0 ? &g_ports[p].officeLevel :
                   (kind == 1 ? &g_ports[p].warehouseLevel : &g_ports[p].shipyardLevel);
  if (*level >= sim_port_upgrade_max_level(kind)) return SIM_FACILITY_REQUIRED;
  int base = kind == 0 ? 320 : (kind == 1 ? 280 : 520);
  int cost = base * (*level + 1);
  if (g_campaign.coins < cost) return SIM_NO_FUNDS;
  g_campaign.coins -= cost;
  g_campaign.capitalSpending += cost;
  ++*level;
  journal_add(1, "%s facilities improved.", PORT_DEFS[p].name);
  if (kind == 0) sim_generate_jobs(p, true);
  return SIM_OK;
}

SimResult sim_buy_module(uint8_t moduleId) {
  if (moduleId >= MODULE_COUNT || g_ui.selectedShip >= g_campaign.shipCount) return SIM_INVALID;
  Ship& s = g_ships[g_ui.selectedShip];
  if (!ship_at_port(s) || s.state == SHIP_DISABLED) return SIM_BAD_SHIP_STATE;
  if (module_slot(s, moduleId) >= 0) return SIM_ALREADY_OWNED;
  if (moduleId == MODULE_CHARTER_PENNANT && !g_campaign.won) return SIM_LOCKED;  // earned, not sold
  int slot = s.modules[0] ? (s.modules[1] ? -1 : 1) : 0;
  if (slot < 0) return SIM_FULL;
  if (g_campaign.coins < MODULE_DEFS[moduleId].cost) return SIM_NO_FUNDS;
  s.modules[slot] = moduleId + 1;
  if (sim_ship_used_capacity(s) > sim_ship_capacity(s)) {
    s.modules[slot] = 0;
    return SIM_NO_CAPACITY;
  }
  if (sim_ship_passenger_load(s) > sim_ship_berths(s)) {
    s.modules[slot] = 0;
    return SIM_NO_BERTHS;
  }
  g_campaign.coins -= MODULE_DEFS[moduleId].cost;
  g_campaign.capitalSpending += MODULE_DEFS[moduleId].cost;
  journal_add(1, "%s fitted with %s.", s.name, MODULE_DEFS[moduleId].name);
  return SIM_OK;
}

SimResult sim_replace_module(uint8_t shipIndex, uint8_t slot, uint8_t moduleId) {
  if (shipIndex >= g_campaign.shipCount || slot >= 2 || moduleId >= MODULE_COUNT) return SIM_INVALID;
  Ship& ship = g_ships[shipIndex];
  if (!ship_at_port(ship) || ship.state == SHIP_DISABLED || ship.booking.state == BOOKING_SAILING)
    return SIM_BAD_SHIP_STATE;
  if (ship.currentPort >= MAX_PORTS || !g_ports[ship.currentPort].unlocked) return SIM_WRONG_PORT;
  if (module_slot(ship, moduleId) >= 0) return SIM_ALREADY_OWNED;
  if (moduleId == MODULE_CHARTER_PENNANT && !g_campaign.won) return SIM_LOCKED;
  int price = MODULE_DEFS[moduleId].cost;
  if (g_campaign.coins < price) return SIM_NO_FUNDS;
  Ship replacement = ship;
  replacement.modules[slot] = moduleId + 1;
  if (sim_ship_used_capacity(replacement) > sim_ship_capacity(replacement)) return SIM_NO_CAPACITY;
  if (sim_ship_passenger_load(replacement) > sim_ship_berths(replacement)) return SIM_NO_BERTHS;
  if (ship_endurance(replacement) < ship_endurance(ship)) {
    // Removing long casks cannot strand a ship at an ocean port, or make
    // cargo already entrusted to its captain impossible to deliver.
    bool exit = false;
    for (uint8_t p = 0; p < MAX_PORTS; ++p) {
      bool useful = false;
      for (int good = 0; good < GOOD_COUNT; ++good)
        if (port_makes(ship.currentPort, (uint8_t)good) &&
            (port_wants(p, (uint8_t)good) || project_for_order(p, (uint8_t)good) < REGION_COUNT)) useful = true;
      if (!useful) continue;
      VoyageQuote route{};
      if (booked_route(replacement, p, route)) { exit = true; break; }
    }
    if (!exit) return SIM_ENDURANCE_LIMIT;
    for (int i = 0; i < ship.cargoCount; ++i) {
      const CargoItem& item = ship.cargo[i];
      if (item.kind != CARGO_CONTRACT || item.destination == ship.currentPort) continue;
      VoyageQuote route{};
      if (!booked_route(replacement, item.destination, route)) return SIM_ENDURANCE_LIMIT;
    }
  }
  Campaign campaignBefore = g_campaign; Ship shipBefore = ship; UiState uiBefore = g_ui;
  uint8_t headBefore = g_journalHead, countBefore = g_journalCount;
  JournalEntry journalBefore = g_journal[headBefore];
  ship = replacement;
  g_campaign.coins -= price; g_campaign.capitalSpending += price;
  journal_add(1, "%s fitted with %s.", ship.name, MODULE_DEFS[moduleId].name);
  if (!save_write()) {
    g_campaign = campaignBefore; ship = shipBefore; g_ui = uiBefore;
    g_journalHead = headBefore; g_journalCount = countBefore; g_journal[headBefore] = journalBefore;
    return SIM_SAVE_FAILED;
  }
  return SIM_OK;
}

SimResult sim_repair_ship(uint8_t shipIndex) {
  if (shipIndex >= g_campaign.shipCount) return SIM_INVALID;
  Ship& s = g_ships[shipIndex];
  if (!ship_at_port(s)) return SIM_BAD_SHIP_STATE;
  if (s.condition >= 100) return SIM_ALREADY_OWNED;
  int cost = (100 - s.condition) * BALANCE.repairCostPerPoint;
  if (g_campaign.coins < cost) return SIM_NO_FUNDS;
  g_campaign.coins -= cost;
  g_campaign.capitalSpending += cost;
  s.condition = 100;
  s.state = SHIP_DOCKED;
  attention_resolve(ATTENTION_DISABLED, shipIndex);
  journal_add(1, "%s repaired for %d.", s.name, cost);
  return SIM_OK;
}

// Cheapest operable departure and whether anything is still at sea; shared
// by the relief claim and by the card that offers it.
static int recovery_captain(uint8_t shipIndex) {
  if (sim_ship_captain(shipIndex)) return g_ships[shipIndex].captainId;
  const Ship& ship = g_ships[shipIndex];
  for (int i = 0; i < MAX_CAPTAINS; ++i) {
    const CaptainProfile& captain = g_captains[i];
    if (!captain.active || captain.currentPort != ship.currentPort) continue;
    if (captain.assignedShip >= g_campaign.shipCount) return i;
    const Ship& previous = g_ships[captain.assignedShip];
    if (ship_at_port(previous) && previous.currentPort == ship.currentPort) return i;
  }
  return -1;
}

static int recovery_departure_cost(const Ship& ship, int captainId) {
  Ship crewed = ship;
  if (captainId >= 0) crewed.captainId = (uint8_t)captainId;
  const Ship& s = crewed;
  int cheapest = 30000;
  for (int lane = 0; lane < LANE_COUNT; ++lane) {
    int to = LANE_DEFS[lane].a == s.currentPort ? LANE_DEFS[lane].b :
             (LANE_DEFS[lane].b == s.currentPort ? LANE_DEFS[lane].a : -1);
    if (to < 0 || !g_ports[to].unlocked || LANE_DEFS[lane].days > ship_endurance(s) ||
        SHIP_CLASS_DEFS[s.classId].draft > PORT_DEFS[to].portClass) continue;
    int days = leg_days_for_ship(s, lane, s.currentPort);
    int daily = 7 + s.crew / 4 + s.classId * 2;
    if (ship_captain_profile(s) && captain_trait(s) == TRAIT_QUARTERMASTER)
      daily = daily * (100 - min(20, captain_level(s) * 4)) / 100;
    int cost = days * max(1, daily);
    if (cost < cheapest) cheapest = cost;
  }
  return cheapest;
}

static void recovery_scan(int* bestShip, int* bestCost, bool* anyMoving) {
  *bestShip = -1; *bestCost = 30000; *anyMoving = false;
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    Ship& s = g_ships[i];
    if (s.state == SHIP_SAILING || s.state == SHIP_AWAITING_DECISION) *anyMoving = true;
    int captain = recovery_captain((uint8_t)i);
    if (!ship_at_port(s) || s.state == SHIP_DISABLED || captain < 0) continue;
    int cost = recovery_departure_cost(s, captain);
    if (cost < *bestCost) { *bestCost = cost; *bestShip = i; }
  }
}

static int recovery_disabled_ship() {
  for (int i = 0; i < g_campaign.shipCount; ++i)
    if (g_ships[i].state == SHIP_DISABLED && recovery_captain((uint8_t)i) >= 0 &&
        recovery_departure_cost(g_ships[i], recovery_captain((uint8_t)i)) < 30000) return i;
  return -1;
}

// Coins plus what the docked fleet's trade cargo would fetch right now.
static int32_t liquid_assets() {
  int32_t liquid = g_campaign.coins;
  for (int ship = 0; ship < g_campaign.shipCount; ++ship) {
    Ship& s = g_ships[ship];
    if (!ship_at_port(s) || s.state == SHIP_DISABLED) continue;
    for (int i = 0; i < s.cargoCount; ++i) if (s.cargo[i].kind == CARGO_MARKET)
      liquid += content_market_price(s.currentPort, s.cargo[i].good, false) * s.cargo[i].quantity;
  }
  return liquid;
}

static bool protected_relief_candidate(int* shipIndex, uint8_t* destination) {
  *shipIndex = -1; *destination = NO_PORT;
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    const Ship& s = g_ships[i];
    if (s.state == SHIP_SAILING || s.state == SHIP_AWAITING_DECISION ||
        s.booking.state == BOOKING_SAILING) return false;
  }
  // Cargo with a realizable value should be sold before drawing relief. A
  // full hold of legacy orders also remains intact, with its original route.
  if (liquid_assets() > g_campaign.coins) return false;
  if (!g_campaign.shipCount) { *shipIndex = 0; *destination = 1; return true; }
  int cheapestRestoration = 0x7fffffff;
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    const Ship& s = g_ships[i];
    if (!ship_at_port(s) || s.cargoCount) continue;
    uint8_t destinations[MAX_PORTS];
    uint8_t count = g_campaign.economyVersion >= 2 ?
      sim_merchant_destinations((uint8_t)i, destinations, MAX_PORTS) :
      sim_voyage_destinations((uint8_t)i, destinations, MAX_PORTS);
    if (!count) continue;
    // Free merchant travel and finite packages remain a viable next action
    // even with no coins. Only a ship needing actual restoration draws relief.
    if (g_campaign.economyVersion >= 2 && s.state != SHIP_DISABLED && sim_ship_captain((uint8_t)i))
      return false;
    int cheapestDeparture = 0x7fffffff; uint8_t closest = destinations[0];
    for (int p = 0; p < count; ++p) {
      VoyageQuote q{}; q.origin = s.currentPort; q.destination = destinations[p];
      if (!booked_route(s, destinations[p], q)) continue;
      int cost = booked_sailing_cost(s, q);
      if (cost < cheapestDeparture) { cheapestDeparture = cost; closest = destinations[p]; }
      if (s.state != SHIP_DISABLED && sim_ship_captain((uint8_t)i)) {
        VoyageQuote ordinary = sim_voyage_offer((uint8_t)i, destinations[p], VOYAGE_FREIGHT, NO_PORT, 0);
        if (ordinary.code == SIM_OK) return false;
      }
    }
    int restoration = cheapestDeparture;
    if (s.state == SHIP_DISABLED) restoration += max(0, 50 - (int)s.condition) * BALANCE.repairCostPerPoint;
    if (recovery_captain((uint8_t)i) < 0) restoration += BALANCE.captainRecruitBaseCost + BALANCE.captainRecruitLevelCost;
    // Exhausted demand alone is not insolvency: it replenishes with time.
    if (s.state != SHIP_DISABLED && sim_ship_captain((uint8_t)i) && g_campaign.coins >= cheapestDeparture)
      return false;
    if (restoration < cheapestRestoration) {
      cheapestRestoration = restoration; *shipIndex = i; *destination = closest;
    }
  }
  return *shipIndex >= 0 && g_campaign.coins < cheapestRestoration;
}

static bool prepare_protected_ship(uint8_t index, Ship& restored, CaptainProfile& captain, int& captainSlot) {
  if (index >= MAX_SHIPS) return false;
  captainSlot = g_campaign.shipCount ? recovery_captain(index) : -1;
  if (captainSlot < 0)
    for (int i = 0; i < MAX_CAPTAINS; ++i)
      if (!g_captains[i].active) { captainSlot = i; break; }
  if (captainSlot < 0)
    for (int i = 0; i < MAX_CAPTAINS; ++i)
      if (g_captains[i].active && g_captains[i].assignedShip == NO_SHIP) { captainSlot = i; break; }
  if (captainSlot < 0) return false;
  if (!g_campaign.shipCount) {
    restored = {};
    restored.id = g_campaign.nextShipId;
    restored.condition = 100; restored.morale = 80; restored.crew = 8;
    restored.routeTemplate.origin = restored.routeTemplate.buyGood = NO_PORT;
    snprintf(restored.name, sizeof(restored.name), "%s", SHIP_NAMES[0]);
  } else restored = g_ships[index];
  restored.state = SHIP_DOCKED;
  restored.condition = (uint8_t)max((int)restored.condition, 50);
  restored.morale = (uint8_t)max((int)restored.morale, 50);
  restored.crew = (uint8_t)max((int)restored.crew, 8 + restored.classId * 5);
  restored.captainId = (uint8_t)captainSlot;
  if (g_captains[captainSlot].active) captain = g_captains[captainSlot];
  else {
    captain = {};
    snprintf(captain.name, sizeof(captain.name), "%s", CAPTAIN_NAMES[index % 12]);
    captain.trait = index % 5; captain.level = 1;
    captain.portrait = content_captain_portrait(captain.name, captain.trait);
    captain.loyalty = BALANCE.captainStartingLoyalty; captain.active = 1;
  }
  captain.assignedShip = index; captain.currentPort = restored.currentPort;
  return true;
}

VoyageQuote sim_recovery_quote() {
  VoyageQuote q{}; q.code = SIM_DEBT_UNAVAILABLE; q.mode = VOYAGE_RELIEF;
  q.origin = q.destination = q.projectRegion = NO_PORT; q.shipIndex = NO_SHIP;
  if (!g_campaign.economyVersion) return q;
  int index; uint8_t destination;
  if (!protected_relief_candidate(&index, &destination)) return q;
  Ship restored; CaptainProfile captain; int captainSlot;
  if (!prepare_protected_ship((uint8_t)index, restored, captain, captainSlot)) {
    q.code = SIM_CAPTAIN_UNAVAILABLE; return q;
  }
  q.shipIndex = (uint8_t)index; q.origin = restored.currentPort; q.destination = destination;
  q.good = PORT_DEFS[destination].demanded[0];
  q.quantity = q.capacity = q.availableQuantity = (uint8_t)sim_ship_capacity(restored);
  if (!booked_route(restored, destination, q, &captain)) { q.code = SIM_BAD_ROUTE; return q; }
  Ship atDestination = restored; atDestination.currentPort = destination;
  int nextCost = 30000;
  for (uint8_t p = 0; p < MAX_PORTS; ++p) {
    bool matching = false;
    for (int good = 0; good < GOOD_COUNT; ++good)
      if (port_makes(destination, (uint8_t)good) &&
          (port_wants(p, (uint8_t)good) || project_for_order(p, (uint8_t)good) < REGION_COUNT)) matching = true;
    if (!matching) continue;
    VoyageQuote onward{}; onward.origin = destination; onward.destination = p;
    if (booked_route(atDestination, p, onward, &captain))
      nextCost = min(nextCost, booked_sailing_cost(atDestination, onward));
  }
  if (nextCost == 30000) { q.code = SIM_BAD_ROUTE; return q; }
  q.buyerPayment = max(80, nextCost + 20);
  q.payout = q.arrivalCoins = q.profit = q.buyerPayment;
  // The fund prepays travel and minimum restoration. Earnings are bounded
  // at sixteen coins per real minute, including for a larger distant hull.
  uint32_t reliefMinutes = (uint32_t)(q.payout * 600 + 15) / 16;
  if (reliefMinutes > q.minutes) {
    uint32_t original = q.legMinutes[0];
    q.legMinutes[0] = (uint16_t)min((uint32_t)60000u, original + reliefMinutes - q.minutes);
    q.minutes += q.legMinutes[0] - original;
  }
  q.code = SIM_OK;
  q.fingerprint = voyage_fingerprint(q);
  // Captain/restoration choices are part of the reviewed transaction, even
  // if two candidates would happen to quote the same arrival time.
  auto mix = [&](uint32_t value) { q.fingerprint = (q.fingerprint ^ value) * 16777619u; };
  mix((uint32_t)captainSlot); mix(captain.level); mix(captain.trait);
  mix(restored.condition); mix(restored.crew); mix(restored.morale);
  if (!q.fingerprint) q.fingerprint = 1;
  return q;
}

static SimResult claim_protected_relief(const VoyageQuote& q) {
  uint8_t index = q.shipIndex;
  Ship restored; CaptainProfile captain; int captainSlot;
  if (!prepare_protected_ship(index, restored, captain, captainSlot)) return SIM_CAPTAIN_UNAVAILABLE;
  Campaign campaignBefore = g_campaign;
  Ship shipBefore = g_ships[index];
  CaptainProfile captainsBefore[MAX_CAPTAINS];
  memcpy(captainsBefore, g_captains, sizeof(captainsBefore));
  uint8_t captainIdsBefore[MAX_SHIPS];
  for (int i = 0; i < MAX_SHIPS; ++i) captainIdsBefore[i] = g_ships[i].captainId;
  UiState uiBefore = g_ui;
  ArrivalReceipt receiptBefore = arrivalReceipts[index];
  AttentionEntry attentionBefore[MAX_ATTENTION];
  memcpy(attentionBefore, g_attention, sizeof(attentionBefore));
  uint8_t headBefore = g_journalHead, countBefore = g_journalCount;
  JournalEntry journalBefore = g_journal[headBefore];
  auto rollback = [&]() {
    g_campaign = campaignBefore; g_ships[index] = shipBefore; g_ui = uiBefore;
    arrivalReceipts[index] = receiptBefore;
    memcpy(g_attention, attentionBefore, sizeof(attentionBefore));
    memcpy(g_captains, captainsBefore, sizeof(captainsBefore));
    for (int i = 0; i < MAX_SHIPS; ++i) g_ships[i].captainId = captainIdsBefore[i];
    g_journalHead = headBefore; g_journalCount = countBefore; g_journal[headBefore] = journalBefore;
  };
  if (!g_campaign.shipCount) { g_campaign.shipCount = 1; ++g_campaign.nextShipId; }
  uint8_t priorShip = g_captains[captainSlot].assignedShip;
  if (g_captains[captainSlot].active && priorShip < g_campaign.shipCount)
    g_ships[priorShip].captainId = NO_SHIP;
  g_ships[index] = restored;
  g_captains[captainSlot] = captain;
  if (g_campaign.captainCount <= captainSlot) g_campaign.captainCount = (uint8_t)(captainSlot + 1);
  Ship& s = g_ships[index];
  install_booking(s, q);
  g_ui.selectedShip = (uint8_t)index; g_ui.selectedPort = q.destination;
  g_ui.scene = SCENE_WORLD; g_ui.card = CARD_NONE; g_ui.dirty = 1;
  attention_resolve(ATTENTION_DISABLED, (uint8_t)index);
  journal_add(1, "Protected delivery: %ld on arrival.", (long)q.arrivalCoins);
  if (!save_write()) { rollback(); return SIM_SAVE_FAILED; }
  return SimResult(SIM_OK, q.arrivalCoins);
}

SimResult sim_claim_recovery(const VoyageQuote& quote) {
  if (quote.mode != VOYAGE_RELIEF || quote.planningOnly || !quote.fingerprint) return SIM_INVALID;
  VoyageQuote fresh = sim_recovery_quote();
  if (fresh.fingerprint != quote.fingerprint) return SIM_STALE_PREVIEW;
  if (fresh.code != SIM_OK) return fresh.code;
  return claim_protected_relief(fresh);
}

bool sim_recovery_available() {
  if (g_campaign.economyVersion) {
    int ship; uint8_t destination;
    return protected_relief_candidate(&ship, &destination);
  }
  int bestShip, bestCost; bool anyMoving;
  recovery_scan(&bestShip, &bestCost, &anyMoving);
  if (anyMoving) return false;
  if (bestShip >= 0) return liquid_assets() < bestCost;
  if (recovery_disabled_ship() >= 0) return true;
  return g_campaign.shipCount == 0;
}

SimResult sim_claim_recovery() {
  if (g_campaign.economyVersion) {
    VoyageQuote quote = sim_recovery_quote();
    if (quote.code != SIM_OK) return quote.code;
    return sim_claim_recovery(quote);
  }
  int bestShip, bestCost; bool anyMoving;
  recovery_scan(&bestShip, &bestCost, &anyMoving);
  if (anyMoving) return SIM_DEBT_UNAVAILABLE;
  if (bestShip >= 0) {
    if (liquid_assets() >= bestCost) return SIM_DEBT_UNAVAILABLE;
  } else {
    bestShip = recovery_disabled_ship();
    if (bestShip >= 0) {
      Ship& s = g_ships[bestShip];
      s.condition = BALANCE.recoveryCondition;
      s.state = SHIP_DOCKED;
      attention_resolve(ATTENTION_DISABLED, (uint8_t)bestShip);
      bestCost = max((int)BALANCE.recoveryDepartureReserve,
                     recovery_departure_cost(s, recovery_captain((uint8_t)bestShip)));
      g_campaign.recoveryDebt += BALANCE.recoveryCondition * BALANCE.repairCostPerPoint;
    } else if (!g_campaign.shipCount) {
      g_campaign.shipCount = 1;
      init_ship(g_ships[0], 0, 0, 0, true);
  // A gift for the first sale: ordinary owned cargo, with no invented voyage
  // or purchase cost. Only new companies receive it; loading never adds cargo.
  CargoItem starter{};
  starter.good = GOOD_GRAIN;
  starter.quantity = 2;
  starter.kind = CARGO_MARKET;
  starter.jobKind = JOB_FREIGHT;
  starter.destination = starter.projectRegion = starter.sourcePort = NO_PORT;
  g_ships[0].cargo[g_ships[0].cargoCount++] = starter;
      bestShip = 0;
      bestCost = BALANCE.recoveryDepartureReserve;
    } else return SIM_DEBT_UNAVAILABLE;
  }
  // A co-located spare captain (including one on a disabled hull) can crew
  // this departure. Relief must leave a usable ship, not another missing-
  // captain error below the cost of recruitment or repair.
  if (!sim_ship_captain((uint8_t)bestShip)) {
    int captain = recovery_captain((uint8_t)bestShip);
    if (captain < 0 || !sim_assign_captain((uint8_t)captain, (uint8_t)bestShip))
      return SIM_CAPTAIN_REQUIRED;
  }
  int32_t shortfall = (int32_t)bestCost - g_campaign.coins;
  int advance = shortfall > 0 ? (int)shortfall : 0;
  g_campaign.coins += advance;
  g_campaign.recoveryDebt += advance;
  g_ui.selectedShip = (uint8_t)bestShip;
  g_ui.selectedPort = g_ships[bestShip].currentPort;
  attention_add(ATTENTION_RECOVERY, (uint8_t)bestShip, g_ui.selectedPort, false);
  journal_add(2, "Maritime fund advance: %d; debt now %ld.", advance,
              (long)g_campaign.recoveryDebt);
  return SimResult(SIM_OK, advance);
}

SimResult sim_purchase_chart(uint8_t region) {
  if (region == 0 || region >= REGION_COUNT) return SIM_INVALID;
  if (g_campaign.chartMask & (1u << region)) return SIM_ALREADY_OWNED;
  if (g_campaign.economyVersion) return SIM_LOCKED;
  for (int r = 1; r < region; ++r)
    if (!(g_campaign.regionMask & (1u << r))) return SIM_PORT_LOCKED;
  if (g_campaign.coins < BALANCE.chartCost[region]) return SIM_NO_FUNDS;
  g_campaign.coins -= BALANCE.chartCost[region];
  g_campaign.capitalSpending += BALANCE.chartCost[region];
  g_campaign.chartMask |= (uint8_t)(1u << region);
  journal_add(1, "%s charts acquired.", REGION_NAMES[region]);
  return SIM_OK;
}

SimResult sim_purchase_charter(uint8_t region) {
  if (region == 0 || region >= REGION_COUNT) return SIM_INVALID;
  if (g_campaign.regionMask & (1u << region)) return SIM_ALREADY_OWNED;
  if (g_campaign.economyVersion) return SIM_LOCKED;
  for (int r = 1; r < region; ++r)
    if (!(g_campaign.regionMask & (1u << r))) return SIM_PORT_LOCKED;
  if (!(g_campaign.chartMask & (1u << region))) return SIM_PORT_LOCKED;
  if (g_campaign.charterProgress[region] < BALANCE.charterTarget[region])
    return SIM_PROJECT_INCOMPLETE;
  if (g_campaign.coins < BALANCE.charterCost[region]) return SIM_NO_FUNDS;
  g_campaign.coins -= BALANCE.charterCost[region];
  g_campaign.capitalSpending += BALANCE.charterCost[region];
  g_campaign.regionMask |= (uint8_t)(1u << region);
  for (int p = 0; p < MAX_PORTS; ++p) {
    if (PORT_DEFS[p].region == region) {
      g_ports[p].unlocked = 1;
      sim_generate_jobs((uint8_t)p, true);
    }
  }
  if (g_campaign.regionMask == 0x3F)
    for (int p = 0; p < MAX_PORTS; ++p)
      if (g_ports[p].unlocked) sim_generate_jobs((uint8_t)p, true);
  journal_add(2, "%s charter granted.", REGION_NAMES[region]);
  audio_notice();
  return SIM_OK;
}

bool sim_recruit_candidate(uint8_t port, uint8_t candidate, CaptainProfile* out) {
  if (!out || port >= MAX_PORTS || candidate > 1 || !g_ports[port].unlocked ||
      (candidate == 1 && g_ports[port].officeLevel < 2))
    return false;
  memset(out, 0, sizeof(*out));
  out->level = g_ports[port].officeLevel < 2 ? 1 :
    (uint8_t)(1 + ((g_campaign.recruitmentWeek + port + candidate) % 2));
  out->hireCost = (uint16_t)(BALANCE.captainRecruitBaseCost +
                             out->level * BALANCE.captainRecruitLevelCost);
  uint32_t nameIndex = (g_campaign.recruitmentWeek * 3u + port + candidate * 5u) % 12u;
  snprintf(out->name, sizeof(out->name), "%s", CAPTAIN_NAMES[nameIndex]);
  out->trait = (uint8_t)((port + candidate + g_campaign.recruitmentWeek) % 5u);
  out->portrait = content_captain_portrait(out->name, out->trait);
  out->xp = (uint16_t)((out->level - 1) * BALANCE.captainXpPerLevel);
  out->loyalty = BALANCE.captainRecruitLoyalty;
  out->assignedShip = NO_SHIP;
  out->currentPort = port;
  return true;
}

SimResult sim_hire_captain(uint8_t candidate) {
  uint8_t port = g_ui.selectedPort;
  if (candidate > 1 || port >= MAX_PORTS) return SIM_INVALID;
  if (candidate == 1 && g_ports[port].officeLevel < 2) return SIM_FACILITY_REQUIRED;
  int slot = -1;
  for (int i = 0; i < MAX_CAPTAINS; ++i) if (!g_captains[i].active) { slot = i; break; }
  if (slot < 0) return SIM_FULL;
  CaptainProfile recruit{};
  if (!sim_recruit_candidate(port, candidate, &recruit)) return SIM_CAPTAIN_UNAVAILABLE;
  if (g_campaign.coins < recruit.hireCost) return SIM_NO_FUNDS;
  CaptainProfile& c = g_captains[slot];
  c = recruit;
  c.active = 1;
  if (g_campaign.captainCount <= slot) g_campaign.captainCount = (uint8_t)(slot + 1);
  g_campaign.coins -= c.hireCost;
  g_campaign.capitalSpending += c.hireCost;
  journal_add(1, "Captain %s hired at %s.", c.name, PORT_DEFS[port].name);
  return SimResult(SIM_OK, slot);
}

SimResult sim_assign_captain(uint8_t captainId, uint8_t shipIndex) {
  if (captainId >= MAX_CAPTAINS || shipIndex >= g_campaign.shipCount ||
      !g_captains[captainId].active) return SIM_INVALID;
  Ship& ship = g_ships[shipIndex];
  CaptainProfile& captain = g_captains[captainId];
  if (!ship_at_port(ship) || ship.state == SHIP_DISABLED) return SIM_BAD_SHIP_STATE;
  if (captain.currentPort != ship.currentPort) return SIM_WRONG_PORT;
  if (captain.assignedShip < g_campaign.shipCount) {
    Ship& old = g_ships[captain.assignedShip];
    if (!ship_at_port(old) || old.currentPort != ship.currentPort) return SIM_WRONG_PORT;
    old.captainId = NO_SHIP;
  }
  CaptainProfile* previous = sim_ship_captain(shipIndex);
  if (previous) {
    previous->assignedShip = NO_SHIP;
    previous->currentPort = ship.currentPort;
  }
  ship.captainId = captainId;
  captain.assignedShip = shipIndex;
  captain.currentPort = ship.currentPort;
  journal_add(1, "%s assigned to %s.", captain.name, ship.name);
  return SIM_OK;
}

#include "sim_return.h"
#include "sim_onboarding.h"
