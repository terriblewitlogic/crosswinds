#pragma once
#include "encounter_content.h"

static inline bool merchant_card(uint8_t card) { return card == CARD_TRADE || card == CARD_PACKAGES; }
static inline HitRegion merchant_row_region(int row) { return {24, (int16_t)(120 + row * 96), 752, 88, (int8_t)row}; }
// Semantic modes remain Buy=0, Sell=1, Parcels=2 for restored drafts.
// Display and rocker order: Buy, Sell, Parcels.
static inline int merchant_tab_position(int mode) { return mode; }
static inline int merchant_next_tab(int mode, int dir) {
  return merchant_tab_position((merchant_tab_position(mode) + dir + 3) % 3);
}
static inline HitRegion merchant_tab_region(int mode) { return {(int16_t)(24 + merchant_tab_position(mode) * 256), 68, 240, 36, (int8_t)(2 + mode)}; }
static inline HitRegion merchant_pager_region(int next) { return {(int16_t)(24 + next * 120), 324, 104, 40, (int8_t)(5 + next)}; }
static inline HitRegion merchant_control_region(int row) {
  return row < 2 ? merchant_row_region(row) : row < 5 ? merchant_tab_region(row - 2) : merchant_pager_region(row - 5);
}
static inline HitRegion merchant_adjust_region(int row, int dir) {
  const HitRegion r = merchant_row_region(row);
  return {(int16_t)(dir < 0 ? 560 : 720), (int16_t)(r.y + 4), (int16_t)(dir < 0 ? 64 : 56), 52, (int8_t)(dir < 0 ? 0 : 1)};
}
static inline HitRegion merchant_footer_region(int id) {
  if (id == 2) return {24,16,96,40,2};
  return {(int16_t)(id == 0 ? 32 : 288),380,(int16_t)(id == 0 ? 240 : 480),48,(int8_t)id};
}
const MerchantTradeQuote& x4_merchant_trade_quote();
const MerchantSailQuote& x4_merchant_sail_quote();
uint8_t x4_merchant_mode();
int x4_merchant_good(int row);
uint8_t x4_merchant_good_count();
const uint8_t* x4_merchant_quantities();
PackageOffer x4_merchant_package(int row);

PackageOffer x4_merchant_selected_package();
int x4_merchant_donation_good();

bool x4_merchant_editing();
bool x4_ship_upgrades();
static inline HitRegion merchant_ship_region(int row) {
  return {(int16_t)(24 + row * 256), 352, 240, 64, (int8_t)row};
}
static inline const char* merchant_incident_label(const Ship& ship) {
  uint8_t kind = ship.incidentType;
  if (g_incident.active && g_incident.shipIndex < g_campaign.shipCount &&
      g_ships[g_incident.shipIndex].id == ship.id) kind = g_incident.kind;
  for (int i = 0; i < g_incidentQueueCount; ++i)
    if (g_incidentQueue[i].shipIndex < g_campaign.shipCount && g_ships[g_incidentQueue[i].shipIndex].id == ship.id)
      kind = g_incidentQueue[i].kind;
  return encounter_title(kind);
}
