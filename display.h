#pragma once
#include "game.h"
#include "lgfx_config.h"

extern LGFX display;

struct HitRegion {
  int16_t x, y, w, h;
  int8_t id;
  uint8_t slopR = 0, slopB = 0;
};

inline int hit_test(const HitRegion* rows, int count, int x, int y) {
  for (int i = 0; i < count; ++i) {
    const HitRegion& r = rows[i];
    if (x >= r.x && x < r.x + r.w + r.slopR &&
        y >= r.y && y < r.y + r.h + r.slopB) return r.id;
  }
  return -1;
}

// Landscape 800x480 inside a safe area: the bezel hides about 9px on the left
// and 3px on the other edges (CrossPoint's measured inset). Navigation is above
// the content; the world chart has a context panel to its right and actions
// below. The button legend shows on the title only.
static constexpr int X4_INSET_LEFT = 16;
static constexpr int X4_INSET_RIGHT = 8;
static constexpr int X4_INSET_TOP = 6;
static constexpr int X4_INSET_BOTTOM = 6;
static constexpr int LEGEND_X = 736;
static constexpr int LEGEND_W = 56;
static constexpr int COLUMN_X = 600;
static constexpr int COLUMN_W = 184;
static constexpr int COLUMN_Y = 46;
static constexpr int COLUMN_STEP = 36;
static constexpr int COLUMN_BUTTON_H = 34;
static constexpr int COLUMN_MAX = 11;
static constexpr int MAP_X = X4_INSET_LEFT;
static constexpr int MAP_Y = 104;
static constexpr int MAP_DRAW_W = 560;
static constexpr int MAP_DRAW_H = 280;
static constexpr int PANEL_Y = MAP_Y + MAP_DRAW_H + 4;

static inline HitRegion x4_slot_region(int slot) {
  HitRegion r;
  r.x = (int16_t)COLUMN_X;
  r.y = (int16_t)(COLUMN_Y + slot * COLUMN_STEP);
  r.w = (int16_t)COLUMN_W;
  r.h = (int16_t)COLUMN_BUTTON_H;
  r.id = (int8_t)slot;
  return r;
}

static constexpr int CARD_X = X4_INSET_LEFT;
static constexpr int CARD_Y = 46;
static constexpr int CARD_W = 768;
static constexpr int CARD_H = 392;
static constexpr int CARD_ROW_Y = 88;
static constexpr int CARD_ROW_H = 44;
static constexpr int CARD_VISIBLE_ROWS = 6;
static constexpr int ROUTE_VISIBLE_ROWS = 5;
static constexpr HitRegion CARD_ROWS[CARD_VISIBLE_ROWS] = {
  { 32, CARD_ROW_Y + CARD_ROW_H * 0, 736, CARD_ROW_H, 0 },
  { 32, CARD_ROW_Y + CARD_ROW_H * 1, 736, CARD_ROW_H, 1 },
  { 32, CARD_ROW_Y + CARD_ROW_H * 2, 736, CARD_ROW_H, 2 },
  { 32, CARD_ROW_Y + CARD_ROW_H * 3, 736, CARD_ROW_H, 3 },
  { 32, CARD_ROW_Y + CARD_ROW_H * 4, 736, CARD_ROW_H, 4 },
  { 32, CARD_ROW_Y + CARD_ROW_H * 5, 736, CARD_ROW_H, 5 },
};

// These two workspaces use a list beside a live detail panel. Keep their
// geometry shared with focus and touch routing; other cards retain six rows.
static constexpr int X4_MARKET_ROW_COUNT = GOOD_COUNT;
static constexpr int X4_CARD_ROW_MAX = GOOD_COUNT;
static constexpr int X4_JOURNAL_VISIBLE_ROWS = 3;
static constexpr HitRegion X4_MARKET_ROWS[X4_MARKET_ROW_COUNT] = {
  { 32, 88, 272, 32, 0 }, { 32, 120, 272, 32, 1 },
  { 32, 152, 272, 32, 2 }, { 32, 184, 272, 32, 3 },
  { 32, 216, 272, 32, 4 }, { 32, 248, 272, 32, 5 },
  { 32, 280, 272, 32, 6 }, { 32, 312, 272, 32, 7 },
};
static constexpr HitRegion X4_MARKET_DETAIL = { 320, 88, 448, 256, -1 };
static constexpr HitRegion X4_ROUTE_ROWS[CARD_VISIBLE_ROWS] = {
  { 472, 88, 296, 44, 0 }, { 472, 132, 296, 44, 1 },
  { 472, 176, 296, 44, 2 }, { 472, 220, 296, 44, 3 },
  { 472, 264, 296, 44, 4 }, { 472, 308, 296, 44, 5 },
};
static constexpr HitRegion X4_ROUTE_DETAIL = { 32, 88, 432, 264, -1 };
static constexpr HitRegion X4_CHARTER_ROWS[CARD_VISIBLE_ROWS] = {
  { 32, 88, 360, 64, 0 }, { 408, 88, 360, 64, 1 },
  { 32, 158, 360, 64, 2 }, { 408, 158, 360, 64, 3 },
  { 32, 228, 360, 64, 4 }, { 408, 228, 360, 64, 5 },
};
static constexpr HitRegion X4_RECRUIT_ROWS[2] = {
  { 32, 88, 352, 260, 0 }, { 408, 88, 360, 260, 3 },
};

static inline int x4_card_row_count(uint8_t card) {
  return card == CARD_MARKET ? X4_MARKET_ROW_COUNT : CARD_VISIBLE_ROWS;
}

static inline HitRegion x4_card_row_region(uint8_t card, uint8_t page, int row,
                                         uint8_t captainView = 0) {
  if (row < 0 || row >= x4_card_row_count(card)) return { 0, 0, 0, 0, -1 };
  if (card == CARD_MARKET) return X4_MARKET_ROWS[row];
  if (card == CARD_ROUTE && page == 0) return X4_ROUTE_ROWS[row];
  if (card == CARD_CHARTER) return X4_CHARTER_ROWS[row];
  if (card == CARD_CAPTAINS && captainView == 2) {
    if (row == 0 || row == 3) return X4_RECRUIT_ROWS[row / 3];
    return { 0, 0, 0, 0, -1 };
  }
  return CARD_ROWS[row];
}

// Input owns the candidate list so chart labels and activation cannot differ.
int x4_route_candidate(int row);
void render_map_follow_selected_ship();
void render_map_focus_port(uint8_t port);
void render_map_stop_follow();
bool render_map_following_ship();
#if defined(SH_HOST_TESTS)
bool input_test_focus_row(int row);  // gallery selection only; never activates a control
bool input_test_focus_command(uint8_t command);
void render_host_reset_map_presentation();
bool render_host_map_position(uint8_t ship, int* x, int* y);
bool render_host_route_point(uint8_t lane, uint8_t from, uint16_t fraction,
                             int32_t* worldX, int32_t* worldY, int* screenX, int* screenY);
bool render_host_route_progress_ink(uint8_t lane, uint8_t from, uint16_t fraction);
uint32_t render_host_push_count();
uint32_t render_host_last_push_ms();
#endif

static constexpr HitRegion CARD_FOOTER[3] = {
  { 32, 358, 232, 40, 0, 8, 0 },
  { 284, 358, 232, 40, 1, 8, 0 },
  { 536, 358, 232, 40, 2, 0, 0 },
};

static constexpr HitRegion INCIDENT_OPTIONS[3] = {
  { 150, 176, 500, 48, 0 },
  { 150, 236, 500, 48, 1 },
  { 150, 296, 500, 48, 2 },
};

static inline HitRegion incident_option_region(int option) {
  if (g_incident.active && g_incident.shipIndex < g_campaign.shipCount && g_ships[g_incident.shipIndex].merchant.state == MERCHANT_SAILING)
    return {32, (int16_t)(162 + option * 78), 736, 68, (int8_t)option};
  return INCIDENT_OPTIONS[option];
}
