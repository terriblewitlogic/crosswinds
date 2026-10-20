#pragma once
#include "merchant_ui.h"
#include "watch_ui.h"

bool x4_watch_active();
static inline bool voyage_watch_screen() {
  return x4_watch_active() && g_ui.scene == SCENE_WORLD && g_ui.card == CARD_NONE;
}

// Main places use top tabs. Transaction cards have only their own actions.
static inline bool voyage_dispatch_card(uint8_t card, uint8_t page) {
  return card == CARD_SHIP || merchant_card(card) || card == CARD_VOYAGE_LOAD || card == CARD_VOYAGE_DEPARTURE ||
    (card == CARD_VOYAGE && page <= 2);
}
static inline HitRegion voyage_fleet_row_region(int row) {
  return {24, (int16_t)(104 + row * 96), 752, 88, (int8_t)row};
}
static inline HitRegion voyage_footer_region(uint8_t card, int id) {
  if (merchant_card(card)) return merchant_footer_region(id);
  if (voyage_dispatch_card(card, g_ui.page)) {
    if (id == 2) return {24, 16, 96, 40, 2};
    if (card == CARD_VOYAGE && g_ui.page == 0)
      return {(int16_t)(32 + id * 128), 380, 112, 48, (int8_t)id};
    return {(int16_t)(id == 0 ? 32 : 288), 380, (int16_t)(id == 0 ? 240 : 480), 48, (int8_t)id};
  }
  if (card == CARD_NONE && g_ui.scene == SCENE_FLEET)
    return {(int16_t)(24 + id * 128), 408, 112, 40, (int8_t)id};
  return CARD_FOOTER[id];
}
static inline HitRegion voyage_scene_region(uint8_t command, int slot) {
  if(command==X4_CMD_BACK)return {24,48,112,40,(int8_t)slot};
  if (command == X4_CMD_WATCH_READY || command == X4_CMD_WATCH_MAP || command == X4_CMD_WATCH_NEXT) {
    int i = command == X4_CMD_WATCH_READY ? 0 : command == X4_CMD_WATCH_NEXT ? 1 : 2;
    return {(int16_t)(24 + i * 256), 408, 240, 40, (int8_t)slot};
  }
  if (command >= X4_CMD_SHIP_0 && command <= X4_CMD_SHIP_2)
    return g_ui.scene == SCENE_COMPANY ? HitRegion{24, (int16_t)(142 + (command - X4_CMD_SHIP_0) * 80), 752, 70, (int8_t)slot} : voyage_fleet_row_region(command - X4_CMD_SHIP_0);
  if ((command == X4_CMD_PAGE_PREV || command == X4_CMD_PAGE_NEXT) && g_ui.scene == SCENE_COMPANY)
    return {(int16_t)(command == X4_CMD_PAGE_PREV ? 600 : 688), 50, 80, 38, (int8_t)slot};
  if (g_ui.scene == SCENE_WORLD && !x4_watch_active() && command != X4_CMD_FLEET && command != X4_CMD_MAP && command != X4_CMD_COMPANY)
    return {(int16_t)(MAP_X + slot * 142), 408, 134, 40, (int8_t)slot};
  if (command == X4_CMD_PAGE_PREV || command == X4_CMD_PAGE_NEXT)
    return voyage_footer_region(CARD_NONE, command == X4_CMD_PAGE_PREV ? 0 : 1);
  if (command == X4_CMD_FLEET || command == X4_CMD_MAP || command == X4_CMD_COMPANY) {
    int tab = command == X4_CMD_FLEET ? 0 : command == X4_CMD_MAP ? 1 : 2;
    return {(int16_t)(24 + tab * 256), 48, 240, 40, (int8_t)slot};
  }
  if (g_ui.scene == SCENE_COMPANY)
    return {(int16_t)(24 + (command == X4_CMD_LEDGER ? 384 : 0)), 408, 368, 40, (int8_t)slot};
  return {(int16_t)(24 + slot * 256), 408, 240, 40, (int8_t)slot};
}

static inline void voyage_goods_text(const uint8_t goods[2], char* out, size_t cap) {
  out[0] = 0;
  for (int i = 0; i < 2; ++i) {
    if (goods[i] >= GOOD_COUNT || (i && goods[i] == goods[0])) continue;
    size_t used = strlen(out);
    snprintf(out + used, cap - used, "%s%s", used ? ", " : "", GOOD_DEFS[goods[i]].name);
  }
  if (!out[0]) snprintf(out, cap, "None");
}

// Details retain the tools needed by old saves without making the first
// delivery a lesson in eleven different management screens.
static constexpr uint8_t VOYAGE_DETAIL_CARDS[] = {
  CARD_FLEET, CARD_MANIFEST, CARD_MARKET, CARD_JOBS, CARD_ROUTE, CARD_SHIPYARD,
  CARD_CAPTAINS, CARD_ATLAS, CARD_KEEPSAKES, CARD_LEDGER, CARD_INBOX, CARD_EVENT, CARD_HELP, CARD_MENU,
};
static constexpr const char* VOYAGE_DETAIL_LABELS[] = {
  "Ships", "Cargo & warehouse", "Open market", "Legacy contracts", "Custom routes", "Ships & port works",
  "Captains", "Sea Atlas", "Keepsakes", "Ledger & journal", "Notices", "Market event", "Controls & help", "Settings",
};
static constexpr int VOYAGE_DETAIL_COUNT = sizeof(VOYAGE_DETAIL_CARDS) / sizeof(VOYAGE_DETAIL_CARDS[0]);

static inline bool voyage_detail_available(int item) {
  switch (VOYAGE_DETAIL_CARDS[item]) {
    case CARD_FLEET: return g_campaign.economyVersion < 2;
    case CARD_MARKET: return g_campaign.economyVersion < 2 && sim_feature_unlocked(FEATURE_MARKET);
    case CARD_JOBS: return g_campaign.economyVersion < 2 && (g_campaign.legacyFeatureMask || g_campaign.deliveredUnits >= 48);
    case CARD_ROUTE: return g_campaign.economyVersion < 2 && (g_campaign.legacyFeatureMask || g_campaign.deliveredUnits >= 24);
    case CARD_SHIPYARD: return sim_feature_unlocked(FEATURE_YARD) || sim_recovery_available();
    case CARD_CAPTAINS: return sim_feature_unlocked(FEATURE_CAPTAINS);
    case CARD_LEDGER: return sim_feature_unlocked(FEATURE_LEDGER);
    case CARD_EVENT: return sim_feature_unlocked(FEATURE_EVENTS);
    case CARD_KEEPSAKES: return g_campaign.storyDeliveries > 0;
    default: return true;
  }
}

static inline int voyage_detail_index(int visible) {
  for (int item = 0; item < VOYAGE_DETAIL_COUNT; ++item) {
    if (!voyage_detail_available(item)) continue;
    if (visible-- == 0) return item;
  }
  return -1;
}

static inline int voyage_detail_count() {
  int count = 0;
  for (int item = 0; item < VOYAGE_DETAIL_COUNT; ++item) count += voyage_detail_available(item) ? 1 : 0;
  return count;
}

static inline bool voyage_needs_range() {
  if (g_campaign.shipCount < 2 || !(g_campaign.regionMask & 2u) || sim_project_region() >= REGION_COUNT) return false;
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    const Ship& ship = g_ships[i];
    if (ship.classId == 1 || (ship.classId == 0 &&
        (ship.modules[0] == MODULE_LONG_CASKS + 1 || ship.modules[1] == MODULE_LONG_CASKS + 1))) return false;
  }
  return true;
}

// Rungs of the NEXT GOAL ladder (voyage_goal_text), in order. Every regional
// dock built is its own rung, so each opened region is a completion too.
enum : uint8_t {
  GOAL_STEP_MARINER_REST, GOAL_STEP_SECOND_SLOOP, GOAL_STEP_RANGE, GOAL_STEP_DOCK_DELIVERIES,
  GOAL_STEP_REGION_DOCKS,                               // + regions already open
  GOAL_STEP_GRAND_CHARTER = GOAL_STEP_REGION_DOCKS + REGION_COUNT + 1, GOAL_STEP_DONE,
  GOAL_MOMENT_FIRST_PROFIT = 0xF0  // the end of the guided first voyage, not a ladder rung
};
static inline uint8_t voyage_goal_step() {
  if (g_campaign.deliveredUnits < 12) return GOAL_STEP_MARINER_REST;
  if (g_campaign.shipCount < 2) return GOAL_STEP_SECOND_SLOOP;
  if (voyage_needs_range()) return GOAL_STEP_RANGE;
  if (g_campaign.deliveredUnits < 48) return GOAL_STEP_DOCK_DELIVERIES;
  if (sim_project_region() < REGION_COUNT) {
    uint8_t open = 0;
    for (int r = 0; r < REGION_COUNT; ++r) open += (g_campaign.regionMask >> r) & 1u;
    return (uint8_t)(GOAL_STEP_REGION_DOCKS + open);
  }
  return g_campaign.won ? GOAL_STEP_DONE : GOAL_STEP_GRAND_CHARTER;
}

static constexpr int VOYAGE_DESTINATION_ROWS = 2;
static constexpr int VOYAGE_FLEET_ROWS = 3;
static inline int voyage_card_row_count(uint8_t card, uint8_t page) {
  if (card == CARD_SHIP) return 3;
  if (merchant_card(card)) return x4_merchant_editing() ? 0 : 7;
  if (card == CARD_RETURN_DETAILS) return 3;
  if (card == CARD_VOYAGE_LOAD || card == CARD_VOYAGE_DEPARTURE) return 2;
  return card == CARD_VOYAGE ? (page == 0 ? VOYAGE_DESTINATION_ROWS : page == 1 || page == 5 ? 2 : 0) :
    x4_card_row_count(card);
}
static inline HitRegion voyage_card_row_region(uint8_t card, uint8_t page, int row,
                                               uint8_t captainView = 0) {
  if (card == CARD_SHIP) return merchant_ship_region(row);
  if (merchant_card(card)) return merchant_control_region(row);
  if (card == CARD_RETURN_DETAILS) return {32, (int16_t)(160 + row * 66), 736, 52, (int8_t)row};
  if (card == CARD_VOYAGE_LOAD) return {24, (int16_t)(92 + row * 112), 752, 100, (int8_t)row};
  if (card == CARD_VOYAGE_DEPARTURE) return {328, (int16_t)(112 + row * 128), 448, 104, (int8_t)row};
  if (card == CARD_VOYAGE) {
    if (page == 0) return {24, (int16_t)(82 + row * 144), 752, 136, (int8_t)row};
    if (page == 5) return {32, (int16_t)(166 + row * 96), 736, 84, (int8_t)row};
    return {32, (int16_t)(112 + row * 104), 736, 84, (int8_t)row};
  }
  return x4_card_row_region(card, page, row, captainView);
}

// Touch targets match the separate visible arrows; the number and good name
// remain selection targets. Physical OK still adds one to the focused row.
static inline HitRegion voyage_load_adjust_region(int row, int dir) {
  HitRegion r = voyage_card_row_region(CARD_VOYAGE_LOAD, 0, row);
  return {(int16_t)(dir < 0 ? 560 : 720), (int16_t)(r.y + 4),
    (int16_t)(dir < 0 ? 64 : 56), 52, (int8_t)(dir < 0 ? 0 : 1)};
}

const VoyageQuote& x4_voyage_quote();
int x4_voyage_destination(int row);
uint8_t x4_voyage_destination_count();
uint8_t x4_company_ship(int row);  // ready vessels first, then ships at sea
uint8_t x4_fleet_ship(int row);    // visible row on the current fleet page
uint8_t x4_next_ready_ship();
bool x4_watch_active();
uint8_t x4_load_good_count();
int x4_load_good(int row);
uint8_t x4_load_selected_good();
uint8_t x4_refit_module();
uint8_t x4_refit_slot();
