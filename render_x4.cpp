// Event-driven black-and-white renderer for the Xteink X4 (800x480 SSD1677
// e-paper, landscape). It draws straight into the panel buffer; the C3 has no PSRAM.
// render_frame() redraws only when the visible state changes and pushes a fast
// refresh for it. A full refresh clears ghosting after PARTIALS_BEFORE_FULL
// pushes, waiting for the next screen change (forced at PARTIALS_HARD_LIMIT).
// Simulation-driven changes are rate limited; button presses refresh at once.

#include "game.h"
#include "display.h"
#include "story_content.h"
#include "atlas_content.h"
#include "ui_fonts.h"
#include "img_generated_x4.h"
#include "img_ship_classes.h"
#include "x4.h"
#include "x4_presentation.h"
#include "x4_time.h"
#include "x4_routes.h"
#include "x4_standby.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voyage_ui.h"

LGFX display;
static bool ready = false;
static constexpr uint16_t INK = 0x0000;
static constexpr uint16_t PAPER = 0xFFFF;

static constexpr uint32_t PERIODIC_REFRESH_MS = 12000;
static constexpr uint32_t LIVE_MAP_REFRESH_MS = 3000;
static constexpr uint16_t PARTIALS_BEFORE_FULL = 30;
static constexpr uint16_t PARTIALS_HARD_LIMIT = 150;  // ~7.5 min of a live map

static uint32_t lastFingerprint = 0;
static uint32_t lastStructure = 0;
static uint32_t lastScreen = 0;
static uint32_t lastPushMs = 0;
static uint32_t deferredUntil = 0;
static uint16_t partialCount = 0;
static bool fullRefreshPending = true;
// The first title and first playable screen are always refreshed in full.
static bool bootTitleStampPending = true;
static bool bootMenuStampPending = true;
static bool forceRedraw = true;
static bool frameNoticeActive = false;
static uint8_t followedShip = NO_SHIP;
static uint32_t followSeed = 0;
static bool followCenterPending = false;
struct MapVisualProgress {
  uint32_t id;
  uint16_t observedElapsed, fraction;
  uint8_t from, to, valid;
};
static MapVisualProgress mapVisual[MAX_SHIPS] = {};
static uint32_t mapVisualSeed = 0;
#ifdef SH_HOST_TESTS
static uint32_t hostNoticeMask = 0;
static uint32_t hostPushCount = 0;
#endif

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

static const lgfx::IFont* text_font(int scale) {
  if (scale >= 5) return &fonts::FreeSansBold24pt7b;
  if (scale >= 4) return &fonts::FreeSansBold24pt7b;
  if (scale >= 3) return &fonts::FreeSansBold18pt7b;
  // Body text is FreeSans 12pt at its native size: a 1.5x-scaled 9pt was the
  // same size but nearest-neighbour scaling left uneven strokes and spacing.
  return &fonts::FreeSans12pt7b;
}

static float text_size(int) { return 1.0f; }

static int text_height(int scale = 2) {
  display.setFont(text_font(scale));
  display.setTextSize(text_size(scale));
  return display.fontHeight();
}

static int text_width(const char* text, int scale = 2) {
  display.setFont(text_font(scale));
  display.setTextSize(text_size(scale));
  return text ? display.textWidth(text) : 0;
}

static void text_at(int x, int y, uint16_t color, const char* text, int scale = 2) {
  display.setFont(text_font(scale));
  display.setTextSize(text_size(scale));
  display.setTextColor(color);
  display.setTextDatum(lgfx::textdatum_t::top_left);
  display.drawString(text ? text : "", x, y);
}

static void fit_text(const char* src, char* out, size_t cap, int pixels, int scale = 2) {
  if (!cap) return;
  if (!src) { out[0] = 0; return; }
  if (text_width(src, scale) <= pixels) { snprintf(out, cap, "%s", src); return; }
  int available = pixels - text_width("...", scale);
  int used = 0, keep = 0;
  while (src[keep] && keep + 4 < (int)cap) {
    char glyph[2] = { src[keep], 0 };
    int next = text_width(glyph, scale);
    if (used + next > available) break;
    used += next;
    ++keep;
  }
  memcpy(out, src, (size_t)keep);
  out[keep] = 0;
  if (keep + 3 < (int)cap) strcat(out, "...");
}

static void text_fit_at(int x, int y, int width, uint16_t color,
                        const char* text, int scale = 2) {
  char fitted[96];
  fit_text(text, fitted, sizeof(fitted), width, scale);
  text_at(x, y, color, fitted, scale);
}

// Top for text optically centred in a box of height h at y. The font height
// includes room for descenders, so plain centring sits the letters high.
static int text_middle(int y, int h, int scale = 2) {
  return y + (h - text_height(scale)) / 2 + 2;
}

static int center_text(int x, int width, const char* text, int scale = 2) {
  return x + (width - text_width(text, scale)) / 2;
}

// ---------------------------------------------------------------------------
// Shapes and patterns (two colours only, so texture stands in for shade)
// ---------------------------------------------------------------------------

static void frame_rect(int x, int y, int w, int h, int thickness) {
  for (int t = 0; t < thickness; ++t)
    display.drawRect(x + t, y + t, w - 2 * t, h - 2 * t, INK);
}

static void thick_line(int x1, int y1, int x2, int y2, int thickness, uint16_t color) {
  display.drawLine(x1, y1, x2, y2, color);
  if (thickness >= 2) {
    display.drawLine(x1 + 1, y1, x2 + 1, y2, color);
    display.drawLine(x1, y1 + 1, x2, y2 + 1, color);
  }
  if (thickness >= 3) {
    display.drawLine(x1 - 1, y1, x2 - 1, y2, color);
    display.drawLine(x1, y1 - 1, x2, y2 - 1, color);
  }
}

static void dashed_line(int x1, int y1, int x2, int y2, int dash, int gap) {
  int dx = x2 - x1, dy = y2 - y1;
  int steps = max(abs(dx), abs(dy));
  if (!steps) { display.drawPixel(x1, y1, INK); return; }
  for (int i = 0; i <= steps; ++i)
    if (i % (dash + gap) < dash)
      display.drawPixel(x1 + dx * i / steps, y1 + dy * i / steps, INK);
}

// Palette blits write each packed image as a single operation. drawBitmap's
// per-run primitives stall the SDL host and needlessly fragment device writes.
static void bitmap_at(int x, int y, const uint8_t* data, int w, int h, uint16_t ink, uint16_t paper) {
  const lgfx::rgb565_t palette[] = { lgfx::rgb565_t(paper), lgfx::rgb565_t(ink) };
  display.pushImage(x, y, w, h, data, lgfx::color_depth_t::grayscale_1bit, palette);
}
static void bitmap_at(int x, int y, const uint8_t* data, int w, int h, uint16_t ink) {
  const lgfx::rgb565_t palette[] = { lgfx::rgb565_t(PAPER), lgfx::rgb565_t(ink) };
  display.pushImage(x, y, w, h, data, 0u, lgfx::color_depth_t::grayscale_1bit, palette);
}

static void draw_button(const HitRegion& r, const char* label, bool enabled = true,
                        bool selected = false) {
  if (!label || !*label) return;
  int x = r.x + 2, y = r.y + 2, w = r.w - 4, h = r.h - 4;
  display.fillRect(x, y, w, h, PAPER);
  if (enabled) display.drawRoundRect(x, y, w, h, 3, INK);
  else {
    dashed_line(x, y, x + w - 1, y, 3, 3);
    dashed_line(x, y + h - 1, x + w - 1, y + h - 1, 3, 3);
  }
  if (selected) display.fillCircle(x + 10, y + h / 2, 3, INK);
  int scale = 2;
  char fitted[72];
  fit_text(label, fitted, sizeof(fitted), w - (selected ? 22 : 12), scale);
  text_at(x + (w - text_width(fitted, scale)) / 2,
          text_middle(y, h, scale), INK, fitted, scale);
}

[[maybe_unused]] static void draw_badge(int x, int y, const char* label, bool inverted = false, int scale = 2) {
  int width = text_width(label, scale) + 12;
  int height = text_height(scale) + 8;
  display.fillRect(x, y, width, height, inverted ? INK : PAPER);
  display.drawRect(x, y, width, height, INK);
  text_at(x + 6, y + 4, inverted ? PAPER : INK, label, scale);
}

[[maybe_unused]] static void draw_progress(int x, int y, int width, int percent) {
  display.fillRect(x, y, width, 12, PAPER);
  display.drawRect(x, y, width, 12, INK);
  int fill = max(0, min(width - 4, percent * (width - 4) / 100));
  display.fillRect(x + 2, y + 2, fill, 8, INK);
}

// Select a prepared native variant for a layout slot. Never resample pixels.
static bool art_fits(const X4Art& art, int w, int h) { return art.width <= w && art.height <= h; }
template<size_t N> static const X4Art* family_variant(const X4Art& art, const X4Art (&base)[N],
                                                     const X4Art (&detail)[N], int w, int h) {
  for (size_t i=0;i<N;++i) if (art.data==base[i].data && art_fits(detail[i],w,h)) return &detail[i];
  return nullptr;
}
static const X4Art* art_for_slot(const X4Art& art, int w, int h) {
  const X4Art* chosen=nullptr;
  if ((chosen=family_variant(art,X4_GOODS,X4_GOOD_DETAILS,w,h))) return chosen;
  if ((chosen=family_variant(art,X4_LANDMARKS,X4_LANDMARK_DETAILS,w,h))) return chosen;
  if ((chosen=family_variant(art,X4_CAPTAINS,X4_CAPTAIN_DETAILS,w,h))) return chosen;
  for (int kind=0;kind<3;++kind) for (int level=0;level<3;++level)
    if (art.data==X4_FACILITIES[kind][level].data && art_fits(X4_FACILITY_DETAILS[kind],w,h)) return &X4_FACILITY_DETAILS[kind];
  if (art_fits(art,w,h)) return &art;
  if ((chosen=family_variant(art,X4_SHIPS,X4_MAP_SHIPS,w,h))) return chosen;
  return nullptr;
}
static void native_art_at(int x, int y, const X4Art& art) {
  const uint8_t* mask=nullptr;bool spans=false;
  for (const auto& entry:X4_NATIVE_MASKS) if (entry.ink==art.data) { mask=entry.rows;spans=entry.spans;break; }
  if(!mask) for(int i=0;i<24;++i) if(art.data==X4_LANDMARKS[i].data) {mask=X4_LANDMARK_MASKS[i].data;break;}
  if(!mask) for(int kind=0;kind<3;++kind) for(int level=0;level<3;++level)
    if(art.data==X4_FACILITIES[kind][level].data) mask=X4_FACILITY_MASKS[kind][level].data;
  if (!mask) {bitmap_at(x,y,art.data,art.width,art.height,INK);return;}
  // Fixed reusable buffer, not an 8KB stack allocation. Opaque interior whites
  // conceal underlying routes; exterior paper stays transparent in one blit.
  static uint8_t pixels[192*168/4];
  if (art.width>192 || art.height>168) return;
  int stride=(art.width+3)/4, sourceStride=(art.width+7)/8;
  memset(pixels,0,stride*art.height);
  for (int row=0;row<art.height;++row) {
    if (!spans) {
      for(int col=0;col<art.width;++col) {
        int offset=row*sourceStride+col/8;uint8_t bit=0x80>>(col%8);
        if(mask[offset]&bit) pixels[row*stride+col/4]|=(art.data[offset]&bit?1:2)<<(6-2*(col%4));
      }
      continue;
    }
    int runs=*mask++;
    for (int run=0;run<runs;++run) {
      int start=*mask++, length=*mask++;
      for (int col=start;col<start+length;++col) {
        uint8_t colour=art.data[row*sourceStride+col/8]&(0x80>>(col%8))?1:2;
        pixels[row*stride+col/4]|=colour<<(6-2*(col%4));
      }
    }
  }
  const lgfx::rgb565_t palette[]={lgfx::rgb565_t(PAPER),lgfx::rgb565_t(INK),lgfx::rgb565_t(PAPER),lgfx::rgb565_t(PAPER)};
  display.pushImage(x,y,art.width,art.height,pixels,0u,lgfx::color_depth_t::grayscale_2bit,palette);
}
static void art_in_box(int x, int y, const X4Art& art, int w, int h) {
  const X4Art* chosen=art_for_slot(art,w,h);
  if (chosen) native_art_at(x+(w-chosen->width)/2,y+(h-chosen->height)/2,*chosen);
}
// Existing callers express their layout footprint as a multiple of the small
// asset. That footprint selects/centers native art; it never scales the bitmap.
static void art_at(int x, int y, const X4Art& art, int slotScale = 1) {
  if (slotScale==1) native_art_at(x,y,art);
  else art_in_box(x,y,art,art.width*slotScale,art.height*slotScale);
}

// A captain at a glance: portrait with name and trait to its left, the whole
// badge right-aligned to `right`. Used wherever a ship is the subject, so the
// captain the player relies on is always in view.
static int captain_badge(int right, int y, const char* name, uint8_t trait) {
  const X4Art& portrait = X4_CAPTAINS[x4_captain_portrait(name, trait)];
  const int portraitX = right - portrait.width;
  art_at(portraitX, y, portrait);
  char fitted[32];
  fit_text(name, fitted, sizeof(fitted), 200);
  text_at(portraitX - 10 - text_width(fitted), y + 2, INK, fitted);
  const char* role = content_trait_name(trait);
  text_at(portraitX - 10 - text_width(role), y + 30, INK, role);
  return portraitX - 10 - max(text_width(fitted), text_width(role));  // badge's left edge
}

static void art_masked_at(int x, int y, const X4Art& art, const X4Art& mask) {
  uint8_t pixels[64 * 56 / 4] = {};
  if (art.width > 64 || art.height > 56 ||
      mask.width != art.width || mask.height != art.height) return;
  const int sourceStride = (art.width + 7) / 8;
  const int targetStride = (art.width + 3) / 4;
  for (int yy = 0; yy < art.height; ++yy) for (int xx = 0; xx < art.width; ++xx) {
    const int source = yy * sourceStride + (xx >> 3);
    const uint8_t bit = 0x80 >> (xx & 7);
    if (!(mask.data[source] & bit)) continue;
    // Index zero leaves the harbor intact; enclosed white keeps the building solid.
    const uint8_t colour = (art.data[source] & bit) ? 1 : 2;
    pixels[yy * targetStride + (xx >> 2)] |= colour << (6 - 2 * (xx & 3));
  }
  const lgfx::rgb565_t palette[] = {
    lgfx::rgb565_t(PAPER), lgfx::rgb565_t(INK),
    lgfx::rgb565_t(PAPER), lgfx::rgb565_t(PAPER)
  };
  display.pushImage(x, y, art.width, art.height, pixels, 0u,
                    lgfx::color_depth_t::grayscale_2bit, palette);
}

static int wrapped_text(int x, int y, int width, const char* text, int maxLines = 3,
                        int lineHeight = 33, int scale = 2) {
  if (!text) return y;
  lineHeight = max(lineHeight, text_height(scale));
  const char* cursor = text;
  for (int line = 0; line < maxLines && *cursor; ++line) {
    char buffer[160];
    int count = 0, lastSpace = -1;
    while (cursor[count] && cursor[count] != '\n' && count < 158) {
      buffer[count] = cursor[count]; buffer[count + 1] = 0;
      if (text_width(buffer, scale) > width) break;
      if (cursor[count] == ' ') lastSpace = count;
      ++count;
    }
    if (cursor[count] && cursor[count] != '\n' && lastSpace > 0) count = lastSpace;
    if (!count) count = 1;
    memcpy(buffer, cursor, count); buffer[count] = 0;
    if (line == maxLines - 1 && cursor[count]) {
      char fitted[160];
      snprintf(fitted, sizeof(fitted), "%.*s...", (int)sizeof(fitted) - 4, buffer);
      text_fit_at(x, y, width, INK, fitted, scale);
    } else text_at(x, y, INK, buffer, scale);
    cursor += count;
    while (*cursor == ' ' || *cursor == '\n') ++cursor;
    y += lineHeight;
  }
  return y;
}

// ---------------------------------------------------------------------------
// Shared state helpers
// ---------------------------------------------------------------------------

[[maybe_unused]] static const char* ship_state_name(const Ship& s) {
  switch (s.state) {
    case SHIP_DOCKED: return "DOCKED";
    case SHIP_SAILING: return "AT SEA";
    case SHIP_AWAITING_DECISION: return "INCIDENT";
    case SHIP_ARRIVED: return "ARRIVED";
    default: return "DISABLED";
  }
}

static bool ship_at_port(const Ship& s) {
  return s.state == SHIP_DOCKED || s.state == SHIP_ARRIVED || s.state == SHIP_DISABLED;
}

static bool ship_needs_attention(uint8_t shipIndex) {
  for (int i = 0; i < g_attentionCount; ++i)
    if (g_attention[i].unread && g_attention[i].shipIndex == shipIndex) return true;
  return false;
}

[[maybe_unused]] static const char* port_class_name(uint8_t portClass) {
  return portClass == PORT_DEEPWATER ? "Deepwater" :
         (portClass == PORT_HARBOR ? "Harbour" : "Anchorage");
}

static bool notice_active() {
  return frameNoticeActive;
}

// ---------------------------------------------------------------------------
// Chrome: top bar, ticker, button column, legend
// ---------------------------------------------------------------------------

static void draw_battery_icon(int x, int y, int percent, bool usb) {
  display.drawRect(x, y, 26, 12, INK);
  display.fillRect(x + 26, y + 3, 3, 6, INK);
  int fill = max(0, min(22, percent * 22 / 100));
  display.fillRect(x + 2, y + 2, fill, 8, INK);
  if (usb) {
    display.fillRect(x + 10, y - 3, 2, 3, PAPER);
    display.drawLine(x + 13, y - 4, x + 9, y + 3, INK);
    display.drawLine(x + 9, y + 3, x + 14, y + 3, INK);
    display.drawLine(x + 14, y + 3, x + 11, y + 10, INK);
  }
}

static void draw_topbar() {
  char coins[24];
  display.drawCircle(34, 23, 8, INK);
  display.drawFastVLine(34, 18, 10, INK);
  snprintf(coins, sizeof(coins), "%ld", (long)g_campaign.coins);
  text_at(52, 8, INK, coins, 2);
  if (g_campaign.paused) text_at(350, 8, INK, "Paused", 2);
  const int percent = board_x4_battery_percent();
  const bool usb = board_x4_usb_connected();
  draw_battery_icon(742, 18, percent, usb);
  if (!usb && percent <= X4_BATTERY_WARN_PERCENT)
    text_at(712, 8, INK, "!", 2);
}

// Only actionable feedback occupies the bottom edge. Routine instructions and
// the latest journal entry stay off the play screens.
static void draw_ticker() {
  if (!notice_active()) return;
#ifdef SH_HOST_TESTS
  if (g_ui.noticeCode < 32) hostNoticeMask |= 1u << g_ui.noticeCode;
#endif
  display.fillRect(0, TICKER_Y, IW, TICKER_H, INK);
  text_fit_at(X4_INSET_LEFT + 4, TICKER_Y + 2,
              IW - X4_INSET_LEFT - X4_INSET_RIGHT - 8, PAPER,
              sim_result_text(g_ui.noticeCode), 2);
}

static void draw_column_buttons() {
  if (g_ui.card != CARD_NONE) return;
  uint8_t column[COLUMN_MAX];
  const int n = x4_column(column);
  char buf[24];
  for (int i = 0; i < n; ++i) {
    const uint8_t command = column[i];
    if (command >= X4_CMD_SHIP_0 && command <= X4_CMD_SHIP_2) continue;
    if (g_ui.scene == SCENE_FLEET && (command == X4_CMD_PAGE_PREV || command == X4_CMD_PAGE_NEXT)) continue;
    const HitRegion r = voyage_scene_region(command, i);
    const char* label = x4_command_label(command, buf, sizeof(buf));
    const bool tab = command == X4_CMD_FLEET || command == X4_CMD_MAP || command == X4_CMD_COMPANY;
    const bool current = (command == X4_CMD_FLEET && (g_ui.scene == SCENE_FLEET || voyage_watch_screen())) ||
      (command == X4_CMD_MAP && g_ui.scene == SCENE_WORLD && !voyage_watch_screen()) ||
      (command == X4_CMD_COMPANY && g_ui.scene == SCENE_COMPANY);
    if (tab) {
      text_at(center_text(r.x, r.w, label, 2), r.y + 3, INK, label, 2);
      if (current) display.fillRect(r.x + 24, r.y + r.h - 4, r.w - 48, 4, INK);
    } else {
      draw_button(r, label, x4_command_enabled(command), x4_command_lit(command));
    }
  }
}

// Legend for the four front buttons along the right edge, shown on the title
// screen only. The cell of the button pressed last is inverted, so the mapping
// teaches itself before play starts.
static void draw_legend() {
  const int legendX = LEGEND_X - 48, legendW = LEGEND_W + 48;
  display.fillRect(legendX, 0, legendW + 8, IH, PAPER);
  display.drawFastVLine(legendX, 0, IH, INK);
  int last = x4_last_pressed();
  int cellH = IH / 4;
  for (int i = 0; i < 4; ++i) {
    uint8_t button = X4_LEGEND_ORDER[i];
    uint8_t action = X4_ACTION_OF_BUTTON[button];
    int y0 = i * cellH + 20, h = cellH - 40;
    int x0 = legendX + 5, w = legendW - 10;
    int cx = x0 + w / 2, cy = y0 + h / 2;
    bool lit = last == button;
    display.drawRoundRect(x0, y0, w, h, 8, INK);
    if (lit) frame_rect(x0 - 2, y0 - 2, w + 4, h + 4, 2);
    uint16_t fg = INK;
    if (action == X4_ACT_UP) {
      display.fillTriangle(cx, cy - 28, cx - 13, cy - 8, cx + 13, cy - 8, fg);
      text_at(cx - text_width("UP", 2) / 2, cy + 4, fg, "UP", 2);
    } else if (action == X4_ACT_DOWN) {
      display.fillTriangle(cx, cy + 28, cx - 13, cy + 8, cx + 13, cy + 8, fg);
      text_at(cx - text_width("DOWN", 2) / 2, cy - 32, fg, "DOWN", 2);
    } else if (action == X4_ACT_OK) {
      text_at(cx - text_width("OK", 3) / 2, cy - 21, fg, "OK", 3);
    } else if (action == X4_ACT_BACK) {
      display.fillTriangle(cx - 12, cy - 18, cx + 4, cy - 28, cx + 4, cy - 8, fg);
      text_at(cx - text_width("BACK", 2) / 2, cy + 4, fg, "BACK", 2);
    } else {
      text_at(cx - text_width("PAGE", 2) / 2, cy - 16, fg, "PAGE", 2);
    }
  }
}

// ---------------------------------------------------------------------------
// Harbour and the ship / port panel
// ---------------------------------------------------------------------------

[[maybe_unused]] static void draw_harbor() {
  const PortDef& port = PORT_DEFS[g_ui.selectedPort];
  const X4Art& background = X4_HARBORS[port.region];
  bitmap_at(MAP_X, MAP_Y, background.data, background.width, background.height, INK, PAPER);
  display.drawFastHLine(MAP_X, MAP_Y + MAP_DRAW_H, MAP_DRAW_W, INK);
  art_masked_at(MAP_X + 28, MAP_Y + 145, X4_LANDMARKS[g_ui.selectedPort],
                X4_LANDMARK_MASKS[g_ui.selectedPort]);
  const PortState& state = g_ports[g_ui.selectedPort];
  const uint8_t levels[] = {state.officeLevel, state.warehouseLevel, state.shipyardLevel};
  for (int i = 0; i < 3; ++i) if (levels[i]) {
    const int level = min(2, (int)levels[i] - 1);
    art_masked_at(MAP_X + 365 + i * 58, MAP_Y + 178,
                  X4_FACILITIES[i][level], X4_FACILITY_MASKS[i][level]);
  }
  // Foreground wharves connect the additions to the shoreline.
  display.drawFastHLine(MAP_X, MAP_Y + 203, 176, INK);
  display.drawFastHLine(MAP_X, MAP_Y + 207, 176, INK);
  for (int i = 0; i < 4; ++i) display.fillRect(MAP_X + 12 + i * 48, MAP_Y + 207, 3, 13, INK);
  display.drawFastHLine(MAP_X + 358, MAP_Y + 219, MAP_DRAW_W - 358, INK);
  display.drawFastHLine(MAP_X + 358, MAP_Y + 223, MAP_DRAW_W - 358, INK);
  for (int i = 0; i < 4; ++i) display.fillRect(MAP_X + 369 + i * 50, MAP_Y + 223, 3, 13, INK);
  for (int i = 0; i < 2; ++i)
    if (port.produced[i] < GOOD_COUNT && (!i || port.produced[i] != port.produced[0]))
      art_at(MAP_X + 102 + i * 28, MAP_Y + 177, X4_GOODS[port.produced[i]]);
  int si = sim_selected_docked_ship(true);  // damaged vessels still occupy a berth
  if (si >= 0) {
    const Ship& ship = g_ships[si];
    const X4Art& art = X4_SHIPS[ship.classId];
    int x = MAP_X + (MAP_DRAW_W - art.width) / 2;
    int y = MAP_Y + MAP_DRAW_H - art.height - 12;
    // This berth is clear white water in all six regional backgrounds.
    bitmap_at(x, y, art.data, art.width, art.height, INK, PAPER);
    display.drawFastHLine(x - 12, y + art.height + 2, art.width + 24, INK);
  } else {
    text_fit_at(MAP_X + 180, MAP_Y + 242, 365, INK, "No company ship docked");
  }
  int labelWidth = max(text_width(port.name, 3), text_width(REGION_NAMES[port.region])) + 20;
  display.fillRect(MAP_X + 8, MAP_Y + 8, min(labelWidth, MAP_DRAW_W - 16), 80, PAPER);
  text_at(MAP_X + 16, MAP_Y + 10, INK, port.name, 3);
  text_at(MAP_X + 16, MAP_Y + 52, INK, REGION_NAMES[port.region]);
}

static void eta_short(const Ship& ship, char* out, size_t cap);

static void draw_panel() {
  const int x = 600, w = 184;
  char line[64];
  if (x4_watch_active() && g_ui.selectedShip < g_campaign.shipCount) {
    const Ship& ship = g_ships[g_ui.selectedShip];
    art_in_box(x + 36, 108, X4_SHIPS[ship.classId], 112, 88);
    text_fit_at(x, 206, w, INK, ship.name, 2);
    const uint8_t port = ship_at_port(ship) ? ship.currentPort :
      ship.merchant.state == MERCHANT_SAILING ? ship.merchant.destination : ship.booking.state == BOOKING_SAILING ? ship.booking.destination : ship.toPort;
    snprintf(line, sizeof(line), "%s %s", ship_at_port(ship) ? "At" : "To", PORT_DEFS[port].name);
    wrapped_text(x, 247, w, line, 2, 33, 2);
    if (ship_at_port(ship)) snprintf(line, sizeof(line), "%s", ship.state == SHIP_DISABLED ? "Repair" : "Ready");
    else eta_short(ship, line, sizeof(line));
    text_fit_at(x, 330, w, INK, line, 4);
    return;
  }
  const PortDef& port = PORT_DEFS[g_ui.selectedPort];
  // The blocks follow the port name, one line or two, at an even rhythm.
  const int dy = wrapped_text(x, 104, w, port.name, 2, 33, 2) - 170;
  text_at(x, 183 + dy, INK, "Makes", 2);
  text_at(x, 266 + dy, INK, "Wants", 2);
  for (int i = 0, visible = 0; i < 2; ++i) {
    if (port.produced[i] >= GOOD_COUNT || (i && port.produced[i] == port.produced[0])) continue;
    art_in_box(x + visible++ * 56, 222 + dy, X4_GOODS[port.produced[i]], 36, 32);
  }
  for (int i = 0, visible = 0; i < 2; ++i) {
    if (port.demanded[i] >= GOOD_COUNT || (i && port.demanded[i] == port.demanded[0])) continue;
    art_in_box(x + visible++ * 56, 305 + dy, X4_GOODS[port.demanded[i]], 36, 32);
  }
  snprintf(line, sizeof(line), "%u%% duty", port.duty);
  text_at(x, 351 + dy, INK, line, 2);
}

// ---------------------------------------------------------------------------
// World map
// ---------------------------------------------------------------------------

static int map_zoom() {
  switch (g_ui.mapZoom) {
    case 2: case 4: case 6: case 8: case 12: return g_ui.mapZoom;
    default: return 1;
  }
}

// Coordinates in the generated routes are sixteenths of a native chart pixel.
// Use the same source crop as the coastline at every zoom. Content coordinates
// remain the saved camera format; they never quantize the drawn route itself.
static constexpr int ROUTE_WORLD_W = X4_WORLD_MAP_W * X4_ROUTE_SCALE;
static constexpr int ROUTE_WORLD_H = X4_WORLD_MAP_H * X4_ROUTE_SCALE;
static_assert(X4_ROUTE_WIDTH == X4_WORLD_MAP_W && X4_ROUTE_HEIGHT == X4_WORLD_MAP_H,
              "Route coordinates must match the native coastline");
static_assert(X4_ROUTE_COUNT <= MAX_LANES, "Route table exceeds lane capacity");
struct MapView { int left, top, width, height; };

static MapView map_view() {
  int zoom = map_zoom();
  int width = X4_WORLD_MAP_W / zoom, height = X4_WORLD_MAP_H / zoom;
  if (zoom == 1) return {0, 0, width, height};
  int cx = (int)g_ui.mapFocusX * X4_WORLD_MAP_W / MAP_W;
  int cy = (int)g_ui.mapFocusY * X4_WORLD_MAP_H / MAP_H;
  return {cx - width / 2, max(0, min((int)X4_WORLD_MAP_H - height, cy - height / 2)), width, height};
}

static int floor_div(int value, int divisor) {
  return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

static int route_wrapped_dx(int a, int b) {
  int dx = b - a;
  if (dx > ROUTE_WORLD_W / 2) dx -= ROUTE_WORLD_W;
  if (dx < -ROUTE_WORLD_W / 2) dx += ROUTE_WORLD_W;
  return dx;
}

static int map_x(int chartX) {
  MapView view = map_view();
  int dx = chartX - (view.left + view.width / 2) * X4_ROUTE_SCALE;
  while (dx < -ROUTE_WORLD_W / 2) dx += ROUTE_WORLD_W;
  while (dx >= ROUTE_WORLD_W / 2) dx -= ROUTE_WORLD_W;
  dx += view.width * X4_ROUTE_SCALE / 2;
  return MAP_X + floor_div(dx * MAP_DRAW_W, view.width * X4_ROUTE_SCALE);
}

static int map_y(int chartY) {
  MapView view = map_view();
  return MAP_Y + floor_div((chartY - view.top * X4_ROUTE_SCALE) * MAP_DRAW_H,
                           view.height * X4_ROUTE_SCALE);
}

static void map_focus_point(const X4RoutePoint& point) {
  g_ui.mapFocusX = (uint8_t)(((uint32_t)point.x * MAP_W + ROUTE_WORLD_W / 2) / ROUTE_WORLD_W % MAP_W);
  g_ui.mapFocusY = (uint8_t)min((uint32_t)MAP_H - 1,
    ((uint32_t)point.y * MAP_H + ROUTE_WORLD_H / 2) / ROUTE_WORLD_H);
}

void render_map_focus_port(uint8_t port) {
  if (port < MAX_PORTS) map_focus_point(X4_PORT_SHORES[port]);
  g_ui.dirty = 1;
}

static bool map_point_visible(int x, int y) {
  return x >= MAP_X && x < MAP_X + MAP_DRAW_W &&
         y >= MAP_Y && y < MAP_Y + MAP_DRAW_H;
}

static bool lane_valid(uint8_t lane, uint8_t from) {
  return lane < LANE_COUNT && X4_ROUTE_PATHS[lane].count >= 2 &&
    (from == LANE_DEFS[lane].a || from == LANE_DEFS[lane].b);
}

static const X4RoutePoint& lane_vertex(uint8_t lane, uint8_t from, int index) {
  const X4RouteSpan& path = X4_ROUTE_PATHS[lane];
  int i = from == LANE_DEFS[lane].b ? path.count - 1 - index : index;
  return X4_ROUTE_POINTS[path.offset + i];
}

static uint16_t lane_length(uint8_t lane) {
  const X4RouteSpan& path = X4_ROUTE_PATHS[lane];
  return X4_ROUTE_DISTANCES[path.offset + path.count - 1];
}

static uint16_t lane_vertex_distance(uint8_t lane, uint8_t from, int index) {
  const X4RouteSpan& path = X4_ROUTE_PATHS[lane];
  if (from == LANE_DEFS[lane].a) return X4_ROUTE_DISTANCES[path.offset + index];
  return lane_length(lane) - X4_ROUTE_DISTANCES[path.offset + path.count - 1 - index];
}

static X4RoutePoint lane_world_point_at(uint8_t lane, uint8_t from, int num, int den) {
  if (!lane_valid(lane, from)) return {0, 0};
  const X4RouteSpan& path = X4_ROUTE_PATHS[lane];
  den = max(1, den); num = max(0, min(num, den));
  // Always interpolate the canonical forward polyline, making the same place
  // on the voyage identical in either direction and at every camera/zoom.
  if (from == LANE_DEFS[lane].b) num = den - num;
  uint32_t target = (uint32_t)((uint64_t)lane_length(lane) * num / den);
  for (int i = 1; i < path.count; ++i) {
    int end = path.offset + i;
    uint16_t stop = X4_ROUTE_DISTANCES[end];
    if (target > stop) continue;
    uint16_t start = X4_ROUTE_DISTANCES[end - 1];
    const X4RoutePoint& a = X4_ROUTE_POINTS[end - 1];
    const X4RoutePoint& b = X4_ROUTE_POINTS[end];
    int length = max(1, (int)stop - start), along = (int)target - start;
    int x = a.x + route_wrapped_dx(a.x, b.x) * along / length;
    x = (x % ROUTE_WORLD_W + ROUTE_WORLD_W) % ROUTE_WORLD_W;
    int y = a.y + ((int)b.y - a.y) * along / length;
    return {(uint16_t)x, (uint16_t)y};
  }
  return X4_ROUTE_POINTS[path.offset + path.count - 1];
}

// Dashes continue across sampled curves instead of restarting at every tiny
// waypoint. Only visible copies of a wrapped segment reach the display driver.
static void route_stroke_segment(int x1, int y1, int x2, int y2, int thickness,
                                 bool dashed, uint32_t& phase, int repeatWidth,
                                 const HitRegion& clip) {
  if (repeatWidth) {
    if (x2 - x1 > repeatWidth / 2) x2 -= repeatWidth;
    if (x2 - x1 < -repeatWidth / 2) x2 += repeatWidth;
  }
  auto stroke = [&](int ax, int ay, int bx, int by) {
    for (int copy = repeatWidth ? -1 : 0; copy <= (repeatWidth ? 1 : 0); ++copy) {
      int shift = copy * repeatWidth;
      if (max(ax, bx) + shift + thickness < clip.x ||
          min(ax, bx) + shift - thickness >= clip.x + clip.w ||
          max(ay, by) + thickness < clip.y || min(ay, by) - thickness >= clip.y + clip.h) continue;
      thick_line(ax + shift, ay, bx + shift, by, thickness, INK);
    }
  };
  if (!dashed) { stroke(x1, y1, x2, y2); return; }
  int dx = x2 - x1, dy = y2 - y1;
  uint32_t length = max((uint32_t)1, (uint32_t)(sqrtf((float)dx * dx + (float)dy * dy) * 16.0f + 0.5f));
  const uint32_t on = 3 * 16, period = 7 * 16;
  for (uint32_t p = 0; p < length;) {
    uint32_t at = (phase + p) % period;
    uint32_t step = min(length - p, (at < on ? on : period) - at);
    if (at < on) {
      int ax = x1 + (int)((int64_t)dx * p / length), ay = y1 + (int)((int64_t)dy * p / length);
      int bx = x1 + (int)((int64_t)dx * (p + step) / length), by = y1 + (int)((int64_t)dy * (p + step) / length);
      stroke(ax, ay, bx, by);
    }
    p += step;
  }
  phase = (phase + length) % period;
}

static void draw_map_route_segment(const X4RoutePoint& a, const X4RoutePoint& b,
                                   int thickness, bool dashed, uint32_t& phase) {
  const HitRegion clip = {MAP_X, MAP_Y, MAP_DRAW_W, MAP_DRAW_H, -1};
  route_stroke_segment(map_x(a.x), map_y(a.y), map_x(b.x), map_y(b.y),
                       thickness, dashed, phase, MAP_DRAW_W * X4_WORLD_MAP_W / map_view().width, clip);
}

static void draw_lane_path(uint8_t lane, uint8_t from, int thickness, bool dashed) {
  if (!lane_valid(lane, from)) return;
  uint32_t phase = 0;
  for (int i = 1; i < X4_ROUTE_PATHS[lane].count; ++i)
    draw_map_route_segment(lane_vertex(lane, from, i - 1), lane_vertex(lane, from, i), thickness, dashed, phase);
}

static void lane_point_at(uint8_t lane, uint8_t from, int num, int den, int* outX, int* outY) {
  X4RoutePoint point = lane_world_point_at(lane, from, num, den);
  *outX = map_x(point.x); *outY = map_y(point.y);
}

static void draw_lane_progress(uint8_t lane, uint8_t from, int num, int den) {
  if (!lane_valid(lane, from)) return;
  den = max(1, den); num = max(0, min(num, den));
  bool reverse = from == LANE_DEFS[lane].b;
  uint32_t canonical = (uint32_t)((uint64_t)lane_length(lane) * (reverse ? den - num : num) / den);
  uint32_t target = reverse ? lane_length(lane) - canonical : canonical;
  X4RoutePoint endpoint = lane_world_point_at(lane, from, num, den);
  uint32_t phase = 0;
  for (int i = 1; i < X4_ROUTE_PATHS[lane].count; ++i) {
    const X4RoutePoint& start = lane_vertex(lane, from, i - 1);
    if (target >= lane_vertex_distance(lane, from, i))
      draw_map_route_segment(start, lane_vertex(lane, from, i), 3, false, phase);
    else { draw_map_route_segment(start, endpoint, 3, false, phase); return; }
  }
}

static bool chart_bit(int sx, int sy) {
  int rowBytes = (X4_WORLD_MAP_W + 7) / 8;
  return X4_WORLD_MAP_DATA[sy * rowBytes + (sx >> 3)] & (0x80 >> (sx & 7));
}

static void draw_world_art() {
  int zoom = map_zoom();
  if (zoom == 1) {
    bitmap_at(MAP_X, MAP_Y, X4_WORLD_MAP_DATA, X4_WORLD_MAP_W, X4_WORLD_MAP_H, INK, PAPER);
    return;
  }
  display.fillRect(MAP_X, MAP_Y, MAP_DRAW_W, MAP_DRAW_H, PAPER);
  MapView view = map_view();
  int sourceW = view.width, sourceH = view.height;
  int left = view.left, top = view.top;
  for (int y = 0; y < MAP_DRAW_H; ++y) {
    int sy = top + y * sourceH / MAP_DRAW_H;
    int runStart = -1;
    for (int x = 0; x <= MAP_DRAW_W; ++x) {
      bool ink = false;
      if (x < MAP_DRAW_W) {
        int sx = left + x * sourceW / MAP_DRAW_W;
        while (sx < 0) sx += X4_WORLD_MAP_W;
        while (sx >= X4_WORLD_MAP_W) sx -= X4_WORLD_MAP_W;
        ink = chart_bit(sx, sy);
      }
      if (ink && runStart < 0) runStart = x;
      if (!ink && runStart >= 0) {
        display.drawFastHLine(MAP_X + runStart, MAP_Y + y, x - runStart, INK);
        runStart = -1;
      }
    }
  }
}

static bool route_has_leg(uint8_t a, uint8_t b) {
  if (g_ui.selectedShip >= g_campaign.shipCount) return false;
  uint8_t from = g_ships[g_ui.selectedShip].currentPort;
  for (int i = 0; i < g_ui.routeCount; ++i) {
    uint8_t to = g_ui.routeDraft[i];
    if ((from == a && to == b) || (from == b && to == a)) return true;
    from = to;
  }
  return false;
}

static void draw_map_lanes() {
  for (int i = 0; i < LANE_COUNT; ++i) {
    const SeaLaneDef& lane = LANE_DEFS[i];
    if (!g_ports[lane.a].unlocked || !g_ports[lane.b].unlocked) continue;
    int hazard = sim_lane_hazard((uint8_t)i);
    // A lane touching the region is affected (sim_lane_hazard / sim_lane_wind use the same test).
    bool eventLane = g_campaign.worldEventDays && sim_feature_unlocked(FEATURE_EVENTS) &&
                     (PORT_DEFS[lane.a].region == g_campaign.worldEventRegion ||
                      PORT_DEFS[lane.b].region == g_campaign.worldEventRegion);
    if (route_has_leg(lane.a, lane.b)) draw_lane_path((uint8_t)i, lane.a, 3, false);
    else if (eventLane) draw_lane_path((uint8_t)i, lane.a, 2, true);  // the EVENT card says these are marked
    else draw_lane_path((uint8_t)i, lane.a, 1, hazard >= 4);
  }
}

static void sync_map_visual_state() {
  if (mapVisualSeed != g_campaign.seed) {
    memset(mapVisual, 0, sizeof(mapVisual));
    mapVisualSeed = g_campaign.seed;
  }
  for (int i = 0; i < MAX_SHIPS; ++i) {
    MapVisualProgress& v = mapVisual[i];
    if (i >= g_campaign.shipCount) { v.valid = 0; continue; }
    const Ship& s = g_ships[i];
    if (v.id != s.id || v.from != s.fromPort || v.to != s.toPort ||
        s.legElapsedMinutes < v.observedElapsed ||
        (s.state != SHIP_SAILING && s.state != SHIP_AWAITING_DECISION)) v.valid = 0;
    v.id = s.id; v.from = s.fromPort; v.to = s.toPort;
    v.observedElapsed = s.legElapsedMinutes;
  }
}

static uint16_t map_visual_fraction(uint8_t shipIndex) {
  const Ship& s = g_ships[shipIndex];
  uint16_t actual = s.legTotalMinutes ? (uint16_t)min((uint32_t)10000,
    (uint32_t)s.legElapsedMinutes * 10000u / s.legTotalMinutes) : 0;
  // A delay increases total travel time; it does not sail the ship backward.
  // Keep its last displayed position until the revised schedule catches up.
  return mapVisual[shipIndex].valid ? max(actual, mapVisual[shipIndex].fraction) : actual;
}

static bool map_ship_position(uint8_t shipIndex, int* outX, int* outY) {
  if (shipIndex >= g_campaign.shipCount) return false;
  const Ship& s = g_ships[shipIndex];
  if (s.state != SHIP_SAILING && s.state != SHIP_AWAITING_DECISION) return false;
  int lane = content_lane_between(s.fromPort, s.toPort);
  if (lane < 0) return false;
  lane_point_at((uint8_t)lane, s.fromPort, map_visual_fraction(shipIndex), 10000, outX, outY);
  return map_point_visible(*outX, *outY);
}

bool render_map_following_ship() {
  if (followSeed != g_campaign.seed || followedShip >= g_campaign.shipCount ||
      followedShip != g_ui.selectedShip) return false;
  return g_ships[followedShip].state == SHIP_SAILING ||
         g_ships[followedShip].state == SHIP_AWAITING_DECISION;
}

void render_map_follow_selected_ship() {
  followedShip = g_ui.selectedShip;
  followSeed = g_campaign.seed;
  followCenterPending = render_map_following_ship();
  g_ui.dirty = 1;
}

void render_map_stop_follow() {
  followedShip = NO_SHIP;
  followCenterPending = false;
  g_ui.dirty = 1;
}

static bool follow_map_camera() {
  if (voyage_watch_screen() || !render_map_following_ship() || g_ui.title || g_ui.card != CARD_NONE ||
      g_ui.scene != SCENE_WORLD) return false;
  int zoom = map_zoom();
  if (zoom == 1) { followCenterPending = false; return false; }
  if (!followCenterPending && (g_campaign.paused ||
      g_ships[followedShip].state != SHIP_SAILING)) return false;
  int x = 0, y = 0;
  map_ship_position(followedShip, &x, &y);  // coordinates also exist outside the viewport
  if (!followCenterPending && x >= MAP_X + MAP_DRAW_W / 5 &&
      x < MAP_X + MAP_DRAW_W * 4 / 5 && y >= MAP_Y + MAP_DRAW_H / 5 &&
      y < MAP_Y + MAP_DRAW_H * 4 / 5) return false;
  followCenterPending = false;
  int oldX = g_ui.mapFocusX, oldY = g_ui.mapFocusY;
  const Ship& ship = g_ships[followedShip];
  int lane = content_lane_between(ship.fromPort, ship.toPort);
  if (lane < 0) return false;
  map_focus_point(lane_world_point_at((uint8_t)lane, ship.fromPort,
                                     map_visual_fraction(followedShip), 10000));
  bool changed = oldX != g_ui.mapFocusX || oldY != g_ui.mapFocusY;
  return changed;
}

static bool live_voyage_map() {
  return !g_ui.title && g_ui.card == CARD_NONE && g_ui.scene == SCENE_WORLD &&
    !g_campaign.paused && g_ui.selectedShip < g_campaign.shipCount &&
    g_ships[g_ui.selectedShip].state == SHIP_SAILING;
}

static void eta_short(const Ship& s, char* out, size_t cap) {
  if (g_campaign.paused) { snprintf(out, cap, "Paused"); return; }
  if (s.state == SHIP_AWAITING_DECISION) { snprintf(out, cap, "Paused"); return; }
  uint32_t left = (uint32_t)max(0, (int)s.legTotalMinutes - (int)s.legElapsedMinutes);
  if (s.booking.state == BOOKING_SAILING)
    for (int leg = s.routePos + 1; leg < s.routeCount; ++leg) left += s.booking.legMinutes[leg];
  if (s.merchant.state == MERCHANT_SAILING)
    for (int leg = s.routePos + 1; leg < s.routeCount; ++leg) left += s.merchant.legMinutes[leg];
  x4_format_duration(out, cap, x4_real_seconds(left, g_campaign.timeScale));
}

// One layout owns port and ship labels so they avoid each other; the selected
// port and ship reserve space first. Ship labels are black, port labels white,
// and the selected port gets a compass star.
struct MapLabelPlacement {
  int16_t x, y, width, anchorX, anchorY;
  uint8_t index, ship;
  bool starred;
};
static constexpr int MAP_LABEL_STAR_W = 22;
static MapLabelPlacement mapLabels[MAX_PORTS + MAX_SHIPS];
static uint8_t mapLabelCount = 0;

static bool map_port_visible(uint8_t port) {
  return (g_ports[port].unlocked || sim_feature_unlocked(FEATURE_CHARTERS)) &&
    map_point_visible(map_x(X4_PORT_SHORES[port].x), map_y(X4_PORT_SHORES[port].y));
}

static bool map_rects_overlap(int x, int y, int w, int h, int bx, int by, int bw, int bh) {
  return x < bx + bw && x + w > bx && y < by + bh && y + h > by;
}

static bool map_label_space(int x, int y, int width) {
  if (x < MAP_X + 6 || y < MAP_Y + 6 ||
      x + width > MAP_X + MAP_DRAW_W - 6 || y + 38 > MAP_Y + MAP_DRAW_H - 6) return false;
  for (int i = 0; i < mapLabelCount; ++i) {
    const MapLabelPlacement& p = mapLabels[i];
    if (map_rects_overlap(x - 3, y - 3, width + 6, 44, p.x, p.y, p.width, 38)) return false;
  }
  // Keep marker centres and their selection rings visible beneath nearby text.
  for (int p = 0; p < MAX_PORTS; ++p) if (map_port_visible((uint8_t)p)) {
    int px = map_x(X4_PORT_SHORES[p].x), py = map_y(X4_PORT_SHORES[p].y);
    int ex = map_x(X4_ROUTE_PORTS[p].x), ey = map_y(X4_ROUTE_PORTS[p].y);
    int period = MAP_DRAW_W * X4_WORLD_MAP_W / map_view().width;
    if (ex - px > period / 2) ex -= period;
    if (ex - px < -period / 2) ex += period;
    // Keep both the shoreline contact and its short harbor approach visible.
    int left = min(px - 13, ex - 3), top = min(py - 13, ey - 3);
    int right = max(px + 14, ex + 4), bottom = max(py + 14, ey + 4);
    if (map_rects_overlap(x, y, width, 38, left, top, right - left, bottom - top)) return false;
  }
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    int px = 0, py = 0;
    int radius = i == g_ui.selectedShip ? 26 : 18;
    if (map_ship_position((uint8_t)i, &px, &py) &&
        map_rects_overlap(x, y, width, 38, px - radius, py - radius,
                          radius * 2 + 1, radius * 2 + 1)) return false;
  }
  return true;
}

static void map_label_text(const MapLabelPlacement& p, char* out, size_t cap) {
  snprintf(out, cap, "%s", p.ship ? g_ships[p.index].name : PORT_DEFS[p.index].name);
}

// A four-point compass star with short diagonal points, 2r px across.
static void draw_compass_star(int cx, int cy, uint16_t color, int r = 9) {
  const int w = max(3, r / 3), d = r * 5 / 9;
  display.fillTriangle(cx, cy - r, cx - w, cy, cx + w, cy, color);
  display.fillTriangle(cx, cy + r, cx - w, cy, cx + w, cy, color);
  display.fillTriangle(cx - r, cy, cx, cy - w, cx, cy + w, color);
  display.fillTriangle(cx + r, cy, cx, cy - w, cx, cy + w, color);
  display.drawLine(cx - d, cy - d, cx + d, cy + d, color);
  display.drawLine(cx - d, cy + d, cx + d, cy - d, color);
}

static void reserve_map_label(uint8_t index, bool ship, bool priority) {
  if (mapLabelCount >= MAX_PORTS + MAX_SHIPS) return;
  int ax = 0, ay = 0;
  bool starred = false;
  if (ship) {
    if (!map_ship_position(index, &ax, &ay)) return;
    // Unselected ships are named only when they need the reader.
    bool attention = ship_needs_attention(index) || g_ships[index].state == SHIP_AWAITING_DECISION;
    if (!priority && !attention) return;
  } else {
    if (!map_port_visible(index) || (!priority && (!g_ports[index].unlocked || map_zoom() == 1))) return;
    ax = map_x(X4_PORT_SHORES[index].x); ay = map_y(X4_PORT_SHORES[index].y);
    starred = index == g_ui.selectedPort;
  }
  MapLabelPlacement next = {0, 0, 0, (int16_t)ax, (int16_t)ay, index, (uint8_t)ship, starred};
  const int star = starred ? MAP_LABEL_STAR_W : 0;
  char label[80], fitted[80]; map_label_text(next, label, sizeof(label));
  fit_text(label, fitted, sizeof(fitted), MAP_DRAW_W - 28 - star, 2);
  int width = text_width(fitted, 2) + 16 + star;
  const int offsets[][2] = {{14,-52},{14,14},{-width-14,-52},{-width-14,14},
    {-width/2,-54},{-width/2,16},{16,-19},{-width-16,-19},{14,-96},{14,58}};
  bool found = false;
  for (const auto& offset : offsets) {
    int x = max(MAP_X + 6, min(MAP_X + MAP_DRAW_W - width - 6, ax + offset[0]));
    int y = max(MAP_Y + 6, min(MAP_Y + MAP_DRAW_H - 44, ay + offset[1]));
    if (!map_label_space(x, y, width)) continue;
    next.x = (int16_t)x; next.y = (int16_t)y; found = true; break;
  }
  // At an edge, priority labels can use the nearest free slot in the chart.
  // This search is bounded by the fixed 560x280 viewport and fixed label pool.
  if (!found && priority) {
    int best = 0x7FFFFFFF;
    for (int y = MAP_Y + 6; y <= MAP_Y + MAP_DRAW_H - 44; y += 10)
      for (int x = MAP_X + 6; x <= MAP_X + MAP_DRAW_W - width - 6; x += 14) {
        if (!map_label_space(x, y, width)) continue;
        int score = abs(x + width / 2 - ax) + abs(y + 19 - ay);
        if (score >= best) continue;
        best = score; next.x = (int16_t)x; next.y = (int16_t)y; found = true;
      }
  }
  if (!found) return;
  next.width = (int16_t)width;
  mapLabels[mapLabelCount++] = next;
}

static void prepare_map_labels() {
  mapLabelCount = 0;
  if (g_ui.selectedPort < MAX_PORTS) reserve_map_label(g_ui.selectedPort, false, true);
  if (!x4_watch_active() && g_ui.selectedShip < g_campaign.shipCount) reserve_map_label(g_ui.selectedShip, true, true);
  for (int i = 0; i < g_campaign.shipCount; ++i)
    if (i != g_ui.selectedShip) reserve_map_label((uint8_t)i, true, false);
  for (int p = 0; p < MAX_PORTS; ++p)
    if (p != g_ui.selectedPort) reserve_map_label((uint8_t)p, false, false);
}

static void draw_map_labels(bool leaders) {
  for (int i = 0; i < mapLabelCount; ++i) {
    const MapLabelPlacement& p = mapLabels[i];
    if (leaders) {
      int ex = max((int)p.x, min((int)p.x + p.width - 1, (int)p.anchorX));
      int ey = max((int)p.y, min((int)p.y + 37, (int)p.anchorY));
      display.drawLine(p.anchorX, p.anchorY, ex, ey, INK);
      continue;
    }
    const int star = p.starred ? MAP_LABEL_STAR_W : 0;
    char label[80], fitted[80]; map_label_text(p, label, sizeof(label));
    fit_text(label, fitted, sizeof(fitted), MAP_DRAW_W - 28 - star, 2);
    display.fillRect(p.x, p.y, p.width, 38, p.ship ? INK : PAPER);
    display.drawRect(p.x, p.y, p.width, 38, INK);
    if (p.starred) draw_compass_star(p.x + 4 + MAP_LABEL_STAR_W / 2, p.y + 19, INK);
    text_at(p.x + 8 + star, text_middle(p.y, 38), p.ship ? PAPER : INK, fitted, 2);
  }
}

static void draw_map_ships(bool progressOnly) {
  for (int order = 0; order <= g_campaign.shipCount; ++order) {
    int i=order==g_campaign.shipCount?g_ui.selectedShip:order;
    if (i>=g_campaign.shipCount || (order<g_campaign.shipCount && i==g_ui.selectedShip)) continue;
    const Ship& s = g_ships[i];
    int x = 0, y = 0;
    if (!map_ship_position((uint8_t)i, &x, &y)) continue;
    if (progressOnly) {
      int lane = content_lane_between(s.fromPort, s.toPort);
      draw_lane_progress((uint8_t)lane, s.fromPort, map_visual_fraction((uint8_t)i), 10000);
      continue;
    }
    const X4Art& marker=X4_MAP_SHIPS[s.classId<5?s.classId:0];
    if (i == g_ui.selectedShip) {
      display.fillCircle(x,y,23,PAPER);
      display.drawCircle(x,y,24,INK);display.drawCircle(x,y,25,INK);
    }
    native_art_at(x-marker.width/2,y-marker.height/2,marker);
  }
}

static bool company_ship_docked_at(uint8_t port) {
  for (int i = 0; i < g_campaign.shipCount; ++i)
    if (ship_at_port(g_ships[i]) && g_ships[i].currentPort == port) return true;
  return false;
}

static void draw_map_port_approaches() {
  for (int p = 0; p < MAX_PORTS; ++p) {
    if (!g_ports[p].unlocked && !sim_feature_unlocked(FEATURE_CHARTERS)) continue;
    uint32_t phase = 0;
    // The town sits on the actual shoreline. This short jetty joins it to
    // the separate safe-water endpoint used by ships and sailing routes.
    draw_map_route_segment(X4_PORT_SHORES[p], X4_ROUTE_PORTS[p], 2, false, phase);
  }
}

static void draw_map_ports() {
  for (int p = 0; p < MAX_PORTS; ++p) {
    if (!map_port_visible((uint8_t)p)) continue;
    int x = map_x(X4_PORT_SHORES[p].x), y = map_y(X4_PORT_SHORES[p].y);
    bool unlocked = g_ports[p].unlocked, selected = p == g_ui.selectedPort;
    int radius = selected ? 7 : 5;
    display.fillCircle(x, y, radius, PAPER);
    if (unlocked) {
      if (PORT_DEFS[p].portClass == PORT_DEEPWATER) display.drawRect(x - radius, y - radius, radius * 2 + 1, radius * 2 + 1, INK);
      else if (PORT_DEFS[p].portClass == PORT_ANCHORAGE) display.drawTriangle(x, y - radius, x - radius, y + radius, x + radius, y + radius, INK);
      else display.drawCircle(x, y, radius, INK);
      display.fillCircle(x, y, 1, INK);
    } else display.drawCircle(x, y, 4, INK);
    if (company_ship_docked_at((uint8_t)p)) {
      display.fillRect(x + radius + 2, y - radius - 10, 2, radius + 10, INK);
      display.fillTriangle(x + radius + 4, y - radius - 10, x + radius + 14, y - radius - 6,
                           x + radius + 4, y - radius - 2, INK);
    }
    if (selected) { display.drawCircle(x, y, radius + 3, INK); display.drawCircle(x, y, radius + 4, INK); }
    if (unlocked && g_campaign.worldEventDays && sim_feature_unlocked(FEATURE_EVENTS) &&
        PORT_DEFS[p].region == g_campaign.worldEventRegion)
      display.drawCircle(x, y, radius + 7, INK);
  }
}

static void draw_world_map() {
  draw_world_art();
  display.setClipRect(MAP_X, MAP_Y, MAP_DRAW_W, MAP_DRAW_H);
  draw_map_lanes();
  draw_map_ships(true);
  draw_map_port_approaches();
  prepare_map_labels();
  draw_map_labels(true);  // leaders stay under markers and every label
  draw_map_ports();
  draw_map_ships(false);
  draw_map_labels(false);
  display.clearClipRect();
  display.drawRect(MAP_X - 1, MAP_Y - 1, MAP_DRAW_W + 2, MAP_DRAW_H + 2, INK);
}

// ---------------------------------------------------------------------------
// Cards
// ---------------------------------------------------------------------------

static void card_frame(const char* title) {
  display.fillRect(CARD_X, CARD_Y, CARD_W, CARD_H, PAPER);
  display.drawFastHLine(CARD_X, CARD_Y + CARD_H - 1, CARD_W, INK);
  char fitted[96];
  fit_text(title, fitted, sizeof(fitted), CARD_W - 40, 3);
  text_at(CARD_X + 16, CARD_Y - 2, INK, fitted, 3);
  display.drawFastHLine(CARD_X + 16, CARD_Y + 36, CARD_W - 32, INK);
}

static void row_background(int row, bool selected, bool enabled = true) {
  const HitRegion& r = CARD_ROWS[row];
  display.fillRect(r.x, r.y, r.w, r.h - 2, PAPER);
  display.drawFastHLine(r.x + 16, r.y + r.h - 3, r.w - 16, INK);
  const int middle = r.y + r.h / 2;
  if (selected) display.fillCircle(r.x + 10, middle, 3, INK);
  else if (!enabled) display.drawFastHLine(r.x + 6, middle, 7, INK);
}

static void row_text(int row, const char* primary, const char* secondary = nullptr,
                     uint16_t = INK, bool selected = false, bool enabled = true,
                     const X4Art* icon = nullptr) {
  if (!primary || !*primary) return;
  row_background(row, selected, enabled);
  const HitRegion& r = CARD_ROWS[row];
  int x = r.x + 18, width = r.w - 26;
  if (icon) {
    art_at(x, r.y + (r.h - icon->height) / 2, *icon);
    x += 42; width -= 42;
  }
  int y = text_middle(r.y, r.h);
  if (!secondary || !*secondary) {
    text_fit_at(x, y, width, INK, primary);
    return;
  }
  int primaryWidth = text_width(primary), secondaryWidth = text_width(secondary);
  int available = width - 24;
  if (primaryWidth + secondaryWidth <= available) {
    text_at(x, y, INK, primary);
    text_at(x + width - secondaryWidth, y, INK, secondary);
    return;
  }
  int left = available * primaryWidth / max(1, primaryWidth + secondaryWidth);
  text_fit_at(x, y, left, INK, primary);
  text_fit_at(x + left + 24, y, available - left, INK, secondary);
}

static int selected_ship_or_none() {
  return g_ui.selectedShip < g_campaign.shipCount ? g_ui.selectedShip : -1;
}

static int route_candidate(int row) { return x4_route_candidate(row); }

static const char* risk_word(uint8_t risk) {
  return risk == RISK_FAVORABLE ? "Low" : (risk == RISK_EVEN ? "Moderate" : "High");
}

static void draw_jobs_card() {
  char title[64];
  snprintf(title, sizeof(title), "Jobs at %s", PORT_DEFS[g_ui.selectedPort].name);
  card_frame(title);
  int ship = sim_selected_docked_ship();
  int start = g_ui.page * CARD_VISIBLE_ROWS;
  bool emptyShown = false;
  for (int row = 0; row < CARD_VISIBLE_ROWS; ++row) {
    int slot = start + row;
    bool active = slot < MAX_CONTRACTS_PER_PORT &&
                  g_contracts[g_ui.selectedPort][slot].active == 1;
    if (!active) {
      row_text(row, emptyShown ? "" : "No more contracts", nullptr, INK, false, false);
      emptyShown = true;
      continue;
    }
    const Contract& c = g_contracts[g_ui.selectedPort][slot];
    const char* kind = c.kind == JOB_PACKAGE ? "Parcel" : c.kind == JOB_MAIL ? "Mail" :
      (c.kind == JOB_PASSENGER ? "Passengers" :
       (c.kind == JOB_CHARTER ? "Project" : GOOD_DEFS[c.good].name));
    char primary[80], secondary[96];
    snprintf(primary, sizeof(primary), "%u %s > %s", c.quantity, kind, PORT_DEFS[c.destination].name);
    // Only mail is paid less when late; other jobs have no clock.
    if (c.kind == JOB_MAIL) {
      if (c.deadlineMinute < g_campaign.worldMinutes)
        snprintf(secondary, sizeof(secondary), "%uc / Late", c.reward);
      else {
        char due[24];
        x4_format_duration(due, sizeof(due),
          x4_real_seconds(c.deadlineMinute - g_campaign.worldMinutes, g_campaign.timeScale), true);
        snprintf(secondary, sizeof(secondary), "%uc / due %s", c.reward, due);
      }
    } else {
      snprintf(secondary, sizeof(secondary), "%u coins", c.reward);
    }
    row_text(row, primary, secondary, INK, false, ship >= 0);
  }
  draw_button(CARD_FOOTER[0], "PREV", g_ui.page > 0);
  draw_button(CARD_FOOTER[1], "NEXT",
              (g_ui.page + 1) * CARD_VISIBLE_ROWS < MAX_CONTRACTS_PER_PORT);
  draw_button(CARD_FOOTER[2], "BACK");
}

static void draw_manifest_card() {
  int ship = selected_ship_or_none();
  char title[64];
  // The warehouse page lists the selected ship's own port.
  if (g_ui.page) snprintf(title, sizeof(title), "Warehouse / %s",
                          PORT_DEFS[ship >= 0 ? g_ships[ship].currentPort : g_ui.selectedPort].name);
  else snprintf(title, sizeof(title), "Cargo / %s",
                ship >= 0 ? g_ships[ship].name : "the ship");
  if (ship >= 0) {
    const Ship& s = g_ships[ship];
    int count = g_ui.page ? g_ports[s.currentPort].warehouseCount : s.cargoCount;
    if (count > CARD_VISIBLE_ROWS) {
      size_t used = strlen(title);
      snprintf(title + used, sizeof(title) - used, " / %u-%u of %u",
               g_ui.routeScroll + 1u, min((int)g_ui.routeScroll + CARD_VISIBLE_ROWS, count), count);
    }
  }
  card_frame(title);
  if (ship < 0) {
    row_text(0, "Choose a ship from FLEET", nullptr, INK, false, false);
  } else {
    const Ship& s = g_ships[ship];
    const PortState& port = g_ports[s.currentPort];
    int count = g_ui.page ? port.warehouseCount : s.cargoCount;
    for (int row = 0; row < CARD_VISIBLE_ROWS; ++row) {
      int index = g_ui.routeScroll + row;
      if (index >= count) { row_text(row, row ? "" : "Cargo space available", nullptr, INK, false, false); continue; }
      const CargoItem& cargo = g_ui.page ? port.warehouse[index] : s.cargo[index];
      char primary[80], secondary[96];
      snprintf(primary, sizeof(primary), "%u %s", cargo.quantity,
               content_cargo_name(cargo.kind, cargo.jobKind, cargo.good, false));
      if (cargo.kind == CARGO_CONTRACT)
        snprintf(secondary, sizeof(secondary), "> %s", PORT_DEFS[cargo.destination].name);
      else snprintf(secondary, sizeof(secondary), "Paid %uc each", cargo.paidUnitPrice);
      row_text(row, primary, secondary);
    }
  }
  draw_button(CARD_FOOTER[0], "SHIP", true, !g_ui.page);
  if (ship >= 0 && (g_ships[ship].state == SHIP_DOCKED || g_ships[ship].state == SHIP_ARRIVED) &&
      sim_feature_unlocked(FEATURE_WAREHOUSE))
    draw_button(CARD_FOOTER[1], "WAREHOUSE", true, g_ui.page);
  draw_button(CARD_FOOTER[2], "BACK");
}

// Miniature bounds share the same fixed chart coordinates as the live map.
struct MiniatureView { int x, y, w, h; };

static MiniatureView route_chart_view(const Ship& ship, int prospective) {
  const X4RoutePoint& origin = X4_PORT_SHORES[ship.currentPort];
  int left = origin.x, right = left, top = origin.y, bottom = top;
  uint8_t from = ship.currentPort;
  for (int i = 0; i < g_ui.routeCount + (prospective >= 0); ++i) {
    uint8_t to = i < g_ui.routeCount ? g_ui.routeDraft[i] : (uint8_t)prospective;
    int lane = content_lane_between(from, to);
    if (lane >= 0) for (int p = 0; p < X4_ROUTE_PATHS[lane].count; ++p) {
      const X4RoutePoint& point = lane_vertex((uint8_t)lane, from, p);
      left = min(left, (int)point.x); right = max(right, (int)point.x);
      top = min(top, (int)point.y); bottom = max(bottom, (int)point.y);
    }
    const X4RoutePoint& shore = X4_PORT_SHORES[to];
    left = min(left, (int)shore.x); right = max(right, (int)shore.x);
    top = min(top, (int)shore.y); bottom = max(bottom, (int)shore.y);
    from = to;
  }
  int w = max(ROUTE_WORLD_W * 90 / MAP_W, right - left + ROUTE_WORLD_W * 20 / MAP_W);
  int h = max(ROUTE_WORLD_H * 36 / MAP_H, bottom - top + ROUTE_WORLD_H * 12 / MAP_H);
  w = max(w, h * 5 / 2); h = max(h, w * 2 / 5);
  if (w >= ROUTE_WORLD_W || h >= ROUTE_WORLD_H || right - left > ROUTE_WORLD_W / 2)
    return {0, 0, ROUTE_WORLD_W, ROUTE_WORLD_H};
  return {max(0, min(ROUTE_WORLD_W - w, (left + right - w) / 2)),
          max(0, min(ROUTE_WORLD_H - h, (top + bottom - h) / 2)), w, h};
}

static void miniature_leg(int lane, uint8_t from, const HitRegion& r,
                          const MiniatureView& view, bool prospective) {
  if (lane < 0 || !lane_valid((uint8_t)lane, from)) return;
  uint32_t phase = 0;
  for (int i = 1; i < X4_ROUTE_PATHS[lane].count; ++i) {
    const X4RoutePoint& a = lane_vertex((uint8_t)lane, from, i - 1);
    const X4RoutePoint& b = lane_vertex((uint8_t)lane, from, i);
    int x1 = r.x + floor_div(((int)a.x - view.x) * r.w, view.w);
    int y1 = r.y + floor_div(((int)a.y - view.y) * r.h, view.h);
    int x2 = r.x + floor_div(((int)b.x - view.x) * r.w, view.w);
    int y2 = r.y + floor_div(((int)b.y - view.y) * r.h, view.h);
    route_stroke_segment(x1, y1, x2, y2, prospective ? 1 : 2, prospective,
                         phase, view.w == ROUTE_WORLD_W ? r.w : 0, r);
  }
}

static void miniature_harbor(uint8_t port, const HitRegion& r, const MiniatureView& view) {
  const X4RoutePoint& shore = X4_PORT_SHORES[port];
  const X4RoutePoint& entry = X4_ROUTE_PORTS[port];
  int x1 = r.x + floor_div(((int)shore.x - view.x) * r.w, view.w);
  int y1 = r.y + floor_div(((int)shore.y - view.y) * r.h, view.h);
  int x2 = r.x + floor_div(((int)entry.x - view.x) * r.w, view.w);
  int y2 = r.y + floor_div(((int)entry.y - view.y) * r.h, view.h);
  uint32_t phase = 0;
  route_stroke_segment(x1, y1, x2, y2, 2, false, phase,
                       view.w == ROUTE_WORLD_W ? r.w : 0, r);
}

static void draw_route_miniature(const Ship& ship, int prospective) {
  HitRegion r = { 40, 94, 416, 74, -1 };
  MiniatureView view = route_chart_view(ship, prospective);
  display.setClipRect(r.x, r.y, r.w, r.h);
  // The renderer is single-threaded. Keep this fixed scratch space out of the
  // ESP32-C3 loop task's stack, leaving room for font and panel-driver calls.
  static uint8_t bits[52 * 74];
  memset(bits, 0, 52 * 74);
  for (int y = 0; y < r.h; ++y) {
    for (int x = 0; x < r.w; ++x) {
      bool ink = false;
      int sy0 = (view.y * r.h + y * view.h) / (r.h * X4_ROUTE_SCALE);
      int sy1 = max(sy0 + 1, (view.y * r.h + (y + 1) * view.h) / (r.h * X4_ROUTE_SCALE));
      int sx0 = (view.x * r.w + x * view.w) / (r.w * X4_ROUTE_SCALE);
      int sx1 = max(sx0 + 1, (view.x * r.w + (x + 1) * view.w) / (r.w * X4_ROUTE_SCALE));
      for (int sy = sy0; sy < sy1 && !ink; ++sy)
        for (int sx = sx0; sx < sx1; ++sx)
          ink |= chart_bit(sx, sy);
      if (ink) bits[y * 52 + x / 8] |= 0x80 >> (x & 7);
    }
  }
  bitmap_at(r.x, r.y, bits, r.w, r.h, INK, PAPER);
  uint8_t from = ship.currentPort;
  for (int i = 0; i < g_ui.routeCount; ++i) {
    uint8_t to = g_ui.routeDraft[i];
    miniature_leg(content_lane_between(from, to), from, r, view, false);
    from = to;
  }
  if (prospective >= 0) miniature_leg(content_lane_between(from, (uint8_t)prospective), from, r, view, true);
  miniature_harbor(ship.currentPort, r, view);
  for (int i = 0; i < g_ui.routeCount; ++i) miniature_harbor(g_ui.routeDraft[i], r, view);
  if (prospective >= 0) miniature_harbor((uint8_t)prospective, r, view);
  for (int i = -1; i < g_ui.routeCount; ++i) {
    uint8_t port = i < 0 ? ship.currentPort : g_ui.routeDraft[i];
    int x = r.x + ((int)X4_PORT_SHORES[port].x - view.x) * r.w / view.w;
    int y = r.y + ((int)X4_PORT_SHORES[port].y - view.y) * r.h / view.h;
    display.fillCircle(x, y, 4, PAPER); display.drawCircle(x, y, 4, INK);
    display.fillCircle(x, y, 1, INK);
  }
  display.clearClipRect();
  display.drawFastHLine(r.x, r.y + r.h + 2, r.w, INK);
}

static void draw_route_card() {
  int shipIndex = selected_ship_or_none();
  if (shipIndex < 0) {
    card_frame("Plot a Voyage");
    row_text(0, "No ship selected", "Choose a docked ship from FLEET.", INK, false, false);
    draw_button(CARD_FOOTER[2], "BACK");
    return;
  }
  Ship& ship = g_ships[shipIndex];
  if (g_ui.page == 2) {
    card_frame("Preparation Preview");
    char line[96];
    const RouteTemplate& route = ship.routeTemplate;
    uint8_t endpoint = route.stopCount ? route.stops[route.stopCount - 1] : ship.currentPort;
    snprintf(line, sizeof(line), "%s TO %s", PORT_DEFS[route.origin].name, PORT_DEFS[endpoint].name);
    row_text(0, line, g_routePreparation.reversed ? "Homeward" : "Outward");
    snprintf(line, sizeof(line), "Cargo %u/%u | Passengers %u/%u",
             g_routePreparation.holdUsed, g_routePreparation.holdCapacity,
             g_routePreparation.berthsUsed, g_routePreparation.berthCapacity);
    row_text(1, line);
    snprintf(line, sizeof(line), "Expected sales: %u coins", g_routePreparation.sales);
    row_text(2, line);
    snprintf(line, sizeof(line), "Purchases: %u coins", g_routePreparation.purchases);
    row_text(3, line);
    snprintf(line, sizeof(line), "Operating cost: %u coins", g_routePreparation.operatingCost);
    row_text(4, line);
    snprintf(line, sizeof(line), "Treasury after preparation: %ld coins", (long)g_routePreparation.projectedCoins);
    row_text(5, line, g_routePreparation.valid ? "Ready to apply" : "Review unavailable items",
             INK, g_routePreparation.valid, g_routePreparation.valid);
    draw_button(CARD_FOOTER[0], "APPLY", g_routePreparation.valid);
    draw_button(CARD_FOOTER[1], "REFRESH", route.stopCount > 0);
    draw_button(CARD_FOOTER[2], "BACK");
    return;
  }
  if (g_ui.page == 1) {
    card_frame("Route Preparation");
    RouteTemplate& route = ship.routeTemplate;
    char line[96];
    snprintf(line, sizeof(line), "Save route | %u leg%s", g_ui.routeCount,
             g_ui.routeCount == 1 ? "" : "s");
    row_text(0, line, "Reusable template", INK, false, g_ui.routeCount > 0);
    row_text(1, route.loadMode ? "Auto-load destination jobs: ON" : "Auto-load destination jobs: OFF");
    row_text(2, route.sellMode ? "Auto-sell profitable cargo: ON" : "Auto-sell profitable cargo: OFF");
    snprintf(line, sizeof(line), "Purchase rule: %s",
             route.buyGood < GOOD_COUNT ? GOOD_DEFS[route.buyGood].name : "None");
    row_text(3, line, "OK changes; None disables");
    snprintf(line, sizeof(line), "Maximum purchase price: %u coins", route.maxBuyPrice);
    row_text(4, line, "Rocker adjusts by 10", INK, false, route.buyGood < GOOD_COUNT);
    row_text(5, route.stopCount ? "Saved route is ready" : "No saved route",
             route.stopCount ? "Preview, then apply" : "Save a route first",
             INK, false, route.stopCount > 0);
    draw_button(CARD_FOOTER[0], "PREVIEW", route.stopCount > 0);
    draw_button(CARD_FOOTER[1], "CLEAR", route.stopCount > 0);
    draw_button(CARD_FOOTER[2], "BACK");
    return;
  }
  char title[96];
  snprintf(title, sizeof(title), "Plot a voyage / %s", ship.name);
  card_frame(title);
  uint8_t from = g_ui.routeCount ? g_ui.routeDraft[g_ui.routeCount - 1] : ship.currentPort;
  HitRegion focus;
  int prospective = -1;
  if (x4_focus_region(&focus) && focus.x == X4_ROUTE_ROWS[0].x &&
      focus.y < X4_ROUTE_ROWS[5].y && focus.id < ROUTE_VISIBLE_ROWS)
    prospective = route_candidate(focus.id);
  if (prospective >= 0 && !sim_route_can_add((uint8_t)prospective)) prospective = -1;
  draw_route_miniature(ship, prospective);
  char line[120];
  if (!g_ui.routeCount) {
    snprintf(line, sizeof(line), "From %s", PORT_DEFS[ship.currentPort].name);
    text_fit_at(40, 180, 396, INK, line);
    wrapped_text(40, 217, 396, "Choose the next port on the right. Build up to eight stops, then SAIL.", 3);
  } else for (int i = 0; i < g_ui.routeCount; ++i) {
    int units = 0;
    for (int c = 0; c < ship.cargoCount; ++c)
      if (ship.cargo[c].kind == CARGO_CONTRACT && ship.cargo[c].destination == g_ui.routeDraft[i]) {
        bool earlier = false;
        for (int leg = 0; leg < i; ++leg) earlier |= g_ui.routeDraft[leg] == g_ui.routeDraft[i];
        if (!earlier) units += ship.cargo[c].quantity;
      }
    int x = 40 + (i / 4) * 216, y = 176 + (i % 4) * 28;
    snprintf(line, sizeof(line), "%d", i + 1);
    text_at(x, y, INK, line);
    text_fit_at(x + 20, y, 188, INK, PORT_DEFS[g_ui.routeDraft[i]].name);
    if (units) display.fillCircle(x - 5, y + 14, 2, INK);
  }
  RouteEstimate estimate = sim_estimate_route((uint8_t)shipIndex, g_ui.routeDraft, g_ui.routeCount);
  if (g_ui.routeCount) {
    char duration[24];
    x4_format_duration(duration, sizeof(duration),
      x4_real_seconds((uint32_t)estimate.days * 1440u, g_campaign.timeScale), true);
    snprintf(line, sizeof(line), "~%s / Cost %u / Risk %s", duration, estimate.cost,
             estimate.risk >= 5 ? "High" : estimate.risk >= 3 ? "Med" : "Low");
    text_fit_at(40, 292, 416, INK, line);
    snprintf(line, sizeof(line), "Income %lu / Net %+ld", (unsigned long)estimate.netRevenue, (long)estimate.netProfit);
    text_fit_at(40, 323, 416, INK, estimate.valid ? line : estimate.reason);
  }
  for (int row = 0; row < ROUTE_VISIBLE_ROWS; ++row) {
    const HitRegion& r = X4_ROUTE_ROWS[row];
    int port = route_candidate(row);
    if (port < 0) continue;
    int lane = content_lane_between(from, (uint8_t)port);
    SimResult canAdd = sim_route_can_add((uint8_t)port);
    snprintf(line, sizeof(line), "%s%s", canAdd ? "" : "! ", PORT_DEFS[port].name);
    char duration[24];
    x4_format_duration(duration, sizeof(duration),
      x4_real_seconds((uint32_t)sim_leg_days((uint8_t)shipIndex, lane, from) * 1440u,
                      g_campaign.timeScale), true);
    int durationWidth = text_width(duration);
    text_fit_at(r.x + 12, r.y + 4, r.w - durationWidth - 40, INK, line);
    text_at(r.x + r.w - durationWidth - 12, r.y + 4, INK, duration);
    display.drawFastHLine(r.x + 10, r.y + r.h - 3, r.w - 20, INK);
  }
  if (sim_feature_unlocked(FEATURE_ROUTE_RULES)) {
    const HitRegion& r = X4_ROUTE_ROWS[5];
    text_fit_at(r.x + 12, r.y + 4, r.w - 24, INK, "RULES / preparation");
  }
  bool prepares = ship.routeTemplate.loadMode || ship.routeTemplate.sellMode || ship.routeTemplate.buyGood < GOOD_COUNT;
  draw_button(CARD_FOOTER[0], g_ui.routeCount ? "UNDO" : prepares ? "RECALL+PREP" : "RECALL",
              g_ui.routeCount || ship.routeTemplate.stopCount);
  draw_button(CARD_FOOTER[1], "SAIL", estimate.valid);
  draw_button(CARD_FOOTER[2], "BACK");
}

static int ship_good_quantity(const Ship& ship, uint8_t good) {
  int quantity = 0;
  for (int i = 0; i < ship.cargoCount; ++i)
    if (ship.cargo[i].kind == CARGO_MARKET && ship.cargo[i].good == good)
      quantity += ship.cargo[i].quantity;
  return quantity;
}

static void draw_market_card() {
  char title[80], line[128];
  uint8_t portId = g_ui.selectedPort;
  const PortDef& port = PORT_DEFS[portId];
  const PortState& state = g_ports[portId];
  snprintf(title, sizeof(title), "Market / %s", port.name);
  int ship = sim_selected_docked_ship();
  card_frame(ship >= 0 ? "" : title);
  if (ship >= 0) {
    text_fit_at(CARD_X + 16, CARD_Y - 2, 448, INK, title, 3);
    text_fit_at(CARD_X + 480, CARD_Y + 4, CARD_W - 496, INK, g_ships[ship].name);
  }
  uint8_t selected = min((int)GOOD_COUNT - 1, (int)g_ui.selectedGood);
  for (int good = 0; good < GOOD_COUNT; ++good) {
    const HitRegion& r = X4_MARKET_ROWS[good];
    art_at(r.x + 8, r.y + 3, X4_GOODS[good]);
    text_fit_at(r.x + 45, r.y, r.w - 65, INK, GOOD_DEFS[good].name);
    if (good == selected) display.fillCircle(r.x + r.w - 12, r.y + 15, 3, INK);
    display.drawFastHLine(r.x + 6, r.y + r.h - 1, r.w - 12, INK);
  }
  int x = X4_MARKET_DETAIL.x + 12;
  display.drawFastVLine(X4_MARKET_DETAIL.x - 2, 94, 244, INK);
  art_at(x, 93, X4_GOODS[selected]);
  text_at(x + 36, 86, INK, GOOD_DEFS[selected].name, 3);
  bool made = port.produced[0] == selected || port.produced[1] == selected;
  bool wanted = port.demanded[0] == selected || port.demanded[1] == selected;
  snprintf(line, sizeof(line), "%s%s / Duty %u%%", made ? "Local" : wanted ? "In demand" : "Imported",
           port.specialty == selected ? " / Specialty" : "", port.duty);
  text_fit_at(x, 124, 424, INK, line);
  snprintf(line, sizeof(line), "Buy 1: %u", content_market_price(portId, selected, true));
  text_fit_at(x, 157, 200, INK, line);
  MarketQuote oneSale = ship >= 0 ? sim_market_quote((uint8_t)ship, selected, false, 1) : MarketQuote{};
  snprintf(line, sizeof(line), "Sell 1: %ld%s", ship >= 0 && oneSale.quantity ? (long)oneSale.treasuryDelta :
           (long)content_market_price(portId, selected, false), oneSale.withholding ? " net" : "");
  text_fit_at(x + 212, 157, 212, INK, line);
  snprintf(line, sizeof(line), "Stock %u / Aboard %d / Free %d", state.stock[selected],
           ship >= 0 ? ship_good_quantity(g_ships[ship], selected) : 0,
           ship >= 0 ? sim_ship_capacity(g_ships[ship]) - sim_ship_used_capacity(g_ships[ship]) : 0);
  text_fit_at(x, 190, 424, INK, line);
  text_at(x, 223, INK, "Price index / 7 days");
  int low = 255, high = 0;
  for (int d = 0; d < 7; ++d) {
    int v = state.priceHistory[selected][(state.historyHead + d) % 7];
    low = min(low, v); high = max(high, v);
  }
  int previousX = 0, previousY = 0;
  for (int d = 0; d < 7; ++d) {
    int v = state.priceHistory[selected][(state.historyHead + d) % 7];
    int px = x + 12 + d * 66, py = 271 - (v - low) * 16 / max(1, high - low);
    if (d) display.drawLine(previousX, previousY, px, py, INK);
    display.fillCircle(px, py, 2, INK);
    previousX = px; previousY = py;
  }
  display.drawFastHLine(x + 8, 274, 404, INK);
  if (ship >= 0) {
    MarketQuote buy = sim_market_quote((uint8_t)ship, selected, true, 5);
    MarketQuote sell = sim_market_quote((uint8_t)ship, selected, false, 5);
    snprintf(line, sizeof(line), "Buy %u: %ld", buy.quantity, (long)buy.total);
    text_fit_at(x, 277, 200, INK, line);
    snprintf(line, sizeof(line), "Sell %u: +%ld", sell.quantity, (long)sell.treasuryDelta);
    text_fit_at(x + 212, 277, 212, INK, line);
    const char* reason = sell.withholding ? "Sales shown after debt withholding." :
                                          "Coins include duty; rocker trades 5.";
    if (buy.quantity < 5) {
      if (buy.quantity) {
        const char* limit = buy.stopReason == SIM_NO_CAPACITY ? "hold space" :
                            buy.stopReason == SIM_NO_FUNDS ? "coins available" :
                            buy.stopReason == SIM_CARGO_UNAVAILABLE ? "stock available" : "trade limit";
        snprintf(line, sizeof(line), "Buy up to %u: %s", buy.quantity, limit);
        reason = line;
      } else reason = sim_result_text(buy.stopReason);
    }
    text_fit_at(x, 310, 424, INK, reason);
    draw_button(CARD_FOOTER[0], "BUY 1", buy.quantity > 0);
    draw_button(CARD_FOOTER[1], "SELL 1", sell.quantity > 0);
  } else {
    bool needsRepair = sim_selected_docked_ship(true) >= 0;
    wrapped_text(x, 281, 424, needsRepair ? "Repair your ship at SHIPYARD to trade." :
                                         "Dock a company ship to trade here.", 2);
    draw_button(CARD_FOOTER[0], "BUY 1", false);
    draw_button(CARD_FOOTER[1], "SELL 1", false);
  }
  draw_button(CARD_FOOTER[2], "BACK");
}

static void draw_fleet_card() {
  card_frame("Company Fleet");
  int start = g_ui.page * CARD_VISIBLE_ROWS;
  for (int row = 0; row < CARD_VISIBLE_ROWS; ++row) {
    int index = start + row;
    if (index >= g_campaign.shipCount) continue;
    const Ship& ship = g_ships[index];
    const HitRegion& r = CARD_ROWS[row];
    row_background(row, index == g_ui.selectedShip);
    art_in_box(r.x + 18, r.y + 2, X4_SHIPS[ship.classId], 52, 36);
    char line[128];
    if (ship.state == SHIP_AWAITING_DECISION) {
      snprintf(line, sizeof(line), "%s / Incident needs a response", ship.name);
    } else if (ship.state == SHIP_DISABLED) {
      snprintf(line, sizeof(line), "%s / Repair / Hull %u", ship.name, ship.condition);
    } else if (ship.state == SHIP_SAILING) {
      char eta[16]; eta_short(ship, eta, sizeof(eta));
      snprintf(line, sizeof(line), "%s > %s / %s", ship.name, PORT_DEFS[ship.toPort].name, eta);
    } else snprintf(line, sizeof(line), "%s / %s / Hold %d/%d", ship.name,
                    PORT_DEFS[ship.currentPort].name, sim_ship_used_capacity(ship), sim_ship_capacity(ship));
    text_fit_at(r.x + 86, text_middle(r.y, r.h), r.w - 102, INK, line);
  }
  draw_button(CARD_FOOTER[0], "PAGE", g_campaign.shipCount > CARD_VISIBLE_ROWS);
  if (sim_feature_unlocked(FEATURE_CAPTAINS)) draw_button(CARD_FOOTER[1], "CAPTAINS");
  draw_button(CARD_FOOTER[2], "BACK");
}

static void draw_ledger_card() {
  card_frame("Company Ledger");
  char primary[96], secondary[96];
  snprintf(primary, sizeof(primary), "%ld coins", (long)g_campaign.coins);
  row_text(0, "Treasury", primary);
  snprintf(secondary, sizeof(secondary), "%lu coins", (unsigned long)g_campaign.contractRevenue);
  row_text(1, "Contract revenue", secondary);
  snprintf(secondary, sizeof(secondary), "%+ld coins", (long)g_campaign.marketProfit);
  row_text(2, "Market profit", secondary);
  snprintf(secondary, sizeof(secondary), "%ld / Debt %ld", (long)g_campaign.operatingCosts,
           (long)g_campaign.recoveryDebt);
  row_text(3, "Voyage costs", secondary);
  snprintf(secondary, sizeof(secondary), "%ld / Events %+ld",
           (long)g_campaign.capitalSpending, (long)g_campaign.incidentNet);
  row_text(4, "Ships & works", secondary);
  snprintf(primary, sizeof(primary), "%s%s / %u ships / %u delivered", sim_atlas_complete() ? "Chartmakers / " : "",
           content_rank_name(g_campaign.rank), g_campaign.shipCount, g_campaign.deliveries);
  row_text(5, primary);
  if (sim_feature_unlocked(FEATURE_CHARTERS)) draw_button(CARD_FOOTER[0], "CHARTERS");
  draw_button(CARD_FOOTER[1], "JOURNAL");
  draw_button(CARD_FOOTER[2], "BACK");
}

static void draw_journal_card() {
  card_frame("Company Journal");
  int start = g_ui.page * X4_JOURNAL_VISIBLE_ROWS;
  for (int row = 0; row < X4_JOURNAL_VISIBLE_ROWS; ++row) {
    int logical = start + row;
    if (logical >= g_journalCount) {
      if (!logical) text_at(50, CARD_ROWS[0].y + 6, INK, "No journal entries");
      continue;
    }
    int index = (g_journalHead + MAX_JOURNAL - 1 - logical) % MAX_JOURNAL;
    const JournalEntry& entry = g_journal[index];
    char line[128];
    snprintf(line, sizeof(line), "Day %lu%s / %s", (unsigned long)(entry.worldMinute / 1440u + 1),
             entry.severity >= 2 ? " !" : "", entry.text);
    // One- and two-line entries both sit centred in their 83 px band, and the
    // text and rules keep within the card's header rule (x 32..768).
    int y = CARD_ROWS[0].y + row * 88;
    const int textW = CARD_X + CARD_W - 16 - 48, lineH = text_height() + 3;
    const int lines = text_width(line) <= textW ? 1 : 2;
    wrapped_text(48, y + (83 - lines * lineH) / 2, textW, line, 2, lineH);
    display.drawFastHLine(CARD_X + 16, y + 83, CARD_W - 32, INK);
  }
  draw_button(CARD_FOOTER[0], "OLDER", (g_ui.page + 1) * X4_JOURNAL_VISIBLE_ROWS < g_journalCount);
  draw_button(CARD_FOOTER[1], "NEWER", g_ui.page > 0);
  draw_button(CARD_FOOTER[2], "BACK");
}

static const char* incident_title(uint8_t kind) {
  static const char* titles[] = { "Privateer to Windward", "Storm Rising", "Reef Ahead",
    "Fever Below Deck", "Customs Inspection", "Crew Dispute", "Survivors Sighted",
    "A Merchant Hails" };
  return titles[min(7, (int)kind)];
}

static const char* incident_option(uint8_t kind, uint8_t option) {
  static const char* options[8][3] = {
    // Order matches sim_resolve_incident: option 0, 1, 2 for each kind.
    { "Run under full sail", "Turn and fight", "Buy safe passage" },
    { "Run before it", "Heave to", "Carry sail through" },
    { "Take soundings", "Hire a pilot", "Risk the channel" },
    { "Use medicines", "Rest the crew", "Press onward" },
    { "Pay the fee", "Hide the books", "Wait at anchor" },
    { "Grant shore leave", "Promise a bonus", "Face down the crew" },
    { "Take survivors aboard", "Keep to the course", "Search for cargo" },
    { "Buy the offered goods", "Drive a hard bargain", "Decline politely" },
  };
  return options[min(7, (int)kind)][min(2, (int)option)];
}

static void draw_incident_card() {
  card_frame(incident_title(g_incident.kind));
  if (g_incident.shipIndex < g_campaign.shipCount) {
    const Ship& ship = g_ships[g_incident.shipIndex];
    char primary[96], secondary[120];
    snprintf(primary, sizeof(primary), "RESPONSE NEEDED | %s", ship.name);
    const CaptainProfile* captain = sim_ship_captain(g_incident.shipIndex);
    int loyalty = captain ? ((int)captain->loyalty - BALANCE.captainLoyaltyNeutral) / BALANCE.captainLoyaltyStep : 0;
    loyalty = max(-(int)BALANCE.captainLoyaltyMaxModifier, min((int)BALANCE.captainLoyaltyMaxModifier, loyalty));
    uint8_t wantedScar = g_incident.kind == INCIDENT_STORM ? SCAR_STORM :
                         (g_incident.kind == INCIDENT_PIRATES ? SCAR_PIRATE :
                         (g_incident.kind == INCIDENT_REEF ? SCAR_REEF : SCAR_NONE));
    bool scarred = false;
    if (captain) for (int i = 0; i < captain->scarCount; ++i)
      scarred |= wantedScar != SCAR_NONE && captain->scars[i] == wantedScar;
    snprintf(secondary, sizeof(secondary), "%s | Hull %u | Morale %u | Risk %s | Loyalty %+d | Scar %+d",
             SHIP_CLASS_DEFS[ship.classId].name, ship.condition, ship.morale,
             risk_word(g_incident.risk), loyalty, scarred ? (int)BALANCE.captainScarModifier : 0);
    row_text(0, primary, secondary, INK, true);
  }
  text_at(center_text(CARD_X, CARD_W, "UP / DOWN choose, OK decides", 2),
          INCIDENT_OPTIONS[0].y - 24, INK, "UP / DOWN choose, OK decides", 2);
  for (int i = 0; i < 3; ++i) {
    // Options with a fixed price say so; the rest depend on the roll.
    char label[64];
    int coins = 0;
    if (sim_incident_option_cost((uint8_t)i, &coins))
      snprintf(label, sizeof(label), "%s | %s%d coins", incident_option(g_incident.kind, i),
               coins > 0 ? "+" : "", coins);
    else if (g_incident.kind == INCIDENT_PIRATES && i == 2)
      snprintf(label, sizeof(label), "%s | cannot afford: cargo lost", incident_option(g_incident.kind, i));
    else snprintf(label, sizeof(label), "%s", incident_option(g_incident.kind, i));
    draw_button(INCIDENT_OPTIONS[i], label);
  }
}

static void draw_shipyard_card() {
  static const char* modes[] = { "Shipyard / %s", "Fittings / %s", "Port Works / %s" };
  int mode = g_ui.page % 3;
  char title[64];
  if (x4_ship_upgrades() && g_ui.selectedShip < g_campaign.shipCount)
    snprintf(title, sizeof(title), "Upgrades / %s", g_ships[g_ui.selectedShip].name);
  else snprintf(title, sizeof(title), modes[mode], PORT_DEFS[g_ui.selectedPort].name);
  card_frame(title);
  int selected = selected_ship_or_none();
  for (int row = 0; row < CARD_VISIBLE_ROWS; ++row) {
    char primary[96] = "", secondary[120] = "";
    bool enabled = false;
    if (mode == 0 && row < SHIP_CLASS_COUNT) {
      const ShipClassDef& ship = SHIP_CLASS_DEFS[row];
      snprintf(primary, sizeof(primary), "%s / %u coins", ship.name,
               sim_ship_package_cost((uint8_t)row));
      if (g_campaign.economyVersion)
        snprintf(secondary, sizeof(secondary), "%u spaces / Crewed / 200 reserve", ship.hold);
      else snprintf(secondary, sizeof(secondary), "Hold %u / Berths %u / %ud / Guns %u",
               ship.hold, ship.berths, ship.enduranceDays, ship.guns);
      SimResultCode block = sim_ship_purchase_block((uint8_t)row);
      enabled = block == SIM_OK;
      if (!enabled) snprintf(secondary, sizeof(secondary), "%s", sim_result_text(block));
    } else if (mode == 1 && row < MODULE_SHOP_COUNT) {
      snprintf(primary, sizeof(primary), "%s / %uc", MODULE_DEFS[row].name, MODULE_DEFS[row].cost);
      snprintf(secondary, sizeof(secondary), "%s", g_campaign.economyVersion && row == MODULE_LONG_CASKS ?
        "Farther routes / -1 cargo space" : MODULE_DEFS[row].effect);
      enabled = selected >= 0 && sim_feature_unlocked(FEATURE_FITTINGS) && g_campaign.coins >= MODULE_DEFS[row].cost;
    } else if (mode == 2 && row < 6) {
      if (row < 3 && !sim_feature_unlocked(FEATURE_PORT_WORKS)) {
        static const char* names[] = { "Office", "Warehouse", "Shipyard" };
        uint8_t stage = sim_feature_stage(FEATURE_PORT_WORKS);
        snprintf(primary, sizeof(primary), "%s upgrades", names[row]);
        snprintf(secondary, sizeof(secondary), "Opens at stage %u", stage + 1);
        enabled = false;
      } else if (row < 3) {
        static const char* names[] = { "Office", "Warehouse", "Shipyard" };
        uint8_t level = row == 0 ? g_ports[g_ui.selectedPort].officeLevel :
          (row == 1 ? g_ports[g_ui.selectedPort].warehouseLevel : g_ports[g_ui.selectedPort].shipyardLevel);
        int base = row == 0 ? 320 : (row == 1 ? 280 : 520);
        int cost = base * (level + 1);
        if (level >= sim_port_upgrade_max_level((uint8_t)row)) {
          snprintf(primary, sizeof(primary), "%s / Level %u", names[row], level);
          snprintf(secondary, sizeof(secondary), "Maximum level");
          enabled = false;
        } else {
          snprintf(primary, sizeof(primary), "%s / Level %u > %u", names[row], level, level + 1);
          snprintf(secondary, sizeof(secondary), "%d coins", cost);
          enabled = g_campaign.coins >= cost;
        }
      } else if (row == 3) {
        int cost = selected >= 0 ? (100 - g_ships[selected].condition) * BALANCE.repairCostPerPoint : 0;
        snprintf(primary, sizeof(primary), "Repair ship");
        if (selected >= 0) snprintf(secondary, sizeof(secondary), "Hull %u / %dc", g_ships[selected].condition, cost);
        else snprintf(secondary, sizeof(secondary), "Select a ship");
        enabled = selected >= 0 && cost > 0 && g_campaign.coins >= cost;
      } else if (row == 4) {
        snprintf(primary, sizeof(primary), "Maritime relief");
        enabled = sim_recovery_available();
        snprintf(secondary, sizeof(secondary), enabled ?
          (g_campaign.economyVersion ? "Protected paid delivery" : "Claim an advance") : "For stranded companies");
      } else if (g_campaign.won) {  // row 5: the Grand Charter's reward, fitted here
        const ModuleDef& pennant = MODULE_DEFS[MODULE_CHARTER_PENNANT];
        snprintf(primary, sizeof(primary), "%s / %uc", pennant.name, pennant.cost);
        snprintf(secondary, sizeof(secondary), "%s", pennant.effect);
        enabled = selected >= 0 && g_campaign.coins >= pennant.cost &&
                  !(g_ships[selected].modules[0] == MODULE_CHARTER_PENNANT + 1 ||
                    g_ships[selected].modules[1] == MODULE_CHARTER_PENNANT + 1);
      }
    }
    const X4Art* fittingIcon = mode == 1 && row < MODULE_SHOP_COUNT ? &X4_FITTINGS[row] :
      mode == 2 && row == 5 && g_campaign.won ? &X4_FITTINGS[MODULE_CHARTER_PENNANT] : nullptr;
    row_text(row, primary, *secondary ? secondary : nullptr, INK, false, enabled, fittingIcon);
  }
  if (x4_ship_upgrades()) {
    if (!sim_feature_unlocked(FEATURE_FITTINGS)) text_fit_at(40, 366, 472, INK, "More deliveries unlock upgrades.");
    draw_button(CARD_FOOTER[2], "BACK");
    return;
  }
  // Ships and Port Works are always reachable (repair and relief live there);
  // the Fittings page joins the cycle once unlocked.
  draw_button(CARD_FOOTER[0], "PREV");
  draw_button(CARD_FOOTER[1], "NEXT");
  draw_button(CARD_FOOTER[2], "BACK");
}

static void draw_charter_card() {
  card_frame("Charts and Charters");
  for (int row = 0; row < REGION_COUNT; ++row) {
    bool open = g_campaign.regionMask & (1u << row);
    char primary[80], secondary[120];
    snprintf(primary, sizeof(primary), "%s", REGION_NAMES[row]);
    if (open) snprintf(secondary, sizeof(secondary), "Open for trade");
    else if (!(g_campaign.chartMask & (1u << row)))
      snprintf(secondary, sizeof(secondary), "Purchase chart | %u coins", BALANCE.chartCost[row]);
    else snprintf(secondary, sizeof(secondary), "Progress %u/%u | Charter %u coins",
                  g_campaign.charterProgress[row], BALANCE.charterTarget[row], BALANCE.charterCost[row]);
    row_text(row, primary, secondary, INK, open);
  }
  draw_button(CARD_FOOTER[2], "BACK");
}

static void draw_menu_card() {
  card_frame("Company Menu");
  char speed[24];
  snprintf(speed, sizeof(speed), "%ux", g_campaign.timeScale);
  static const char* labels[] = { "Resume sailing", "Pause all clocks", nullptr,
                                  "Company progress", "Help", "Reset campaign" };
  for (int row = 0; row < CARD_VISIBLE_ROWS; ++row) {
    const char* label = row == 2 ? "Game speed" : labels[row];
    if (row == 0 && !g_campaign.paused) label = "Back to the game";  // nothing to resume
    bool selected = (row == 1 && g_campaign.paused) ||
                    (row == 2 && g_campaign.timeScale > 1) ||
                    (row == 5 && g_ui.confirmReset);
    char progress[80];
    snprintf(progress, sizeof(progress), "Stage %u of %d", sim_stage() + 1, STAGE_COUNT);
    const char* detail = row == 2 ? speed :
      (row == 3 ? progress :
       (row == 5 ? (g_ui.confirmReset ? "OK again erases company" : "Erase and start again") : row==0 && g_campaign.economyVersion>=2 ? "Safe passage: automatic / No spending" : nullptr));
    row_text(row, label, detail, INK, selected);
  }
  draw_button(CARD_FOOTER[0], "SAVE");
  draw_button(CARD_FOOTER[1], "JOURNAL");
  draw_button(CARD_FOOTER[2], "BACK");
}

static void draw_progress_card() {
  card_frame("Company Progress");
  uint8_t stage = sim_stage();
  for (int row = 0; row < STAGE_COUNT && row < CARD_VISIBLE_ROWS; ++row) {
    char primary[96], secondary[128];
    bool circled = row == STAGE_COUNT - 1 && g_campaign.won;  // the last stage is done, not awaited
    const char* mark = row < stage || circled ? "DONE" : (row == stage ? "NOW" : (row == stage + 1 ? "NEXT" : "LATER"));
    if (row <= stage) snprintf(primary, sizeof(primary), "%s | Stage %d: %s", mark, row + 1, sim_stage_name((uint8_t)row));
    else snprintf(primary, sizeof(primary), "%s | Stage %d: %s | %s", mark, row + 1,
                  sim_stage_name((uint8_t)row), sim_stage_condition((uint8_t)row));
    char progress[48] = "";
    if (row == stage + 1) sim_stage_progress((uint8_t)row, progress, sizeof(progress));
    snprintf(secondary, sizeof(secondary), "%s%s%s", circled ? "The Grand Charter: the company has circled the world" :
             sim_stage_unlocks((uint8_t)row), *progress ? " | " : "", progress);
    row_text(row, primary, secondary, INK, row == stage && !circled, row <= stage + 1);
  }
  draw_button(CARD_FOOTER[2], "BACK");
}

static void draw_help_card() {
  card_frame("Captain's Notes");
  if (g_campaign.economyVersion >= 2) {
    static const char* lines[] = {
      "UP/DOWN choose; OK opens; BACK returns.",
      "Trade: rocker switches the top tabs.",
      "Select a good: UP + / DOWN - / OK confirm.",
      "Parcels are free; one space each.",
      "Hazards use the shown safe fallback.",
#if   SH_X4_VOYAGE_STANDBY
      "POWER: voyages continue in standby."
#else
      "POWER saves; voyages pause while off."
#endif
    };
    for (int row = 0; row < CARD_VISIBLE_ROWS; ++row) row_text(row, lines[row]);
    draw_button(CARD_FOOTER[0], "TUTORIAL");
    draw_button(CARD_FOOTER[2], "BACK");
    return;
  }
  char jobs[80];
  snprintf(jobs, sizeof(jobs), "Full hold to one port earns +%u%%.",
           BALANCE.consolidationBonusPercent);
  static const char* lines[] = {
    "UP/DOWN choose; OK opens; BACK returns.",
    "Rocker: ports, pages, or trade up to 5.",
    nullptr,
    "Travel times: real minutes at your speed.",
    "NOTICES: arrivals, incidents and repairs.",
    "POWER saves; voyages pause while off."
  };
  for (int row = 0; row < CARD_VISIBLE_ROWS; ++row)
    row_text(row, row == 2 ? jobs : lines[row]);
  draw_button(CARD_FOOTER[0], "TUTORIAL");
  draw_button(CARD_FOOTER[2], "BACK");
}

static const char* attention_name(uint8_t kind) {
  static const char* names[] = { "RESPONSE NEEDED", "ARRIVAL READY", "SHIP DISABLED",
    "CHARTER READY", "WORLD EVENT", "RECOVERY AVAILABLE" };
  return names[min((int)ATTENTION_RECOVERY, (int)kind)];
}

static int visible_attention_index(int visibleRow) {
  int wanted = g_ui.page * CARD_VISIBLE_ROWS + visibleRow;
  int found = 0;
  for (int i = g_attentionCount - 1; i >= 0; --i) {
    if (g_ui.attentionUnreadOnly && !g_attention[i].unread) continue;
    if (found++ == wanted) return i;
  }
  return -1;
}

static void draw_inbox_card() {
  int pages = (attention_visible_count() + CARD_VISIBLE_ROWS - 1) / CARD_VISIBLE_ROWS;
  char title[48];
  if (pages > 1) snprintf(title, sizeof(title), "Notices | page %d of %d", g_ui.page + 1, pages);
  else snprintf(title, sizeof(title), "Notices");
  card_frame(title);
  for (int row = 0; row < CARD_VISIBLE_ROWS; ++row) {
    int index = visible_attention_index(row);
    if (index < 0) { row_text(row, row ? "" : "No messages", nullptr, INK, false, false); continue; }
    const AttentionEntry& attention = g_attention[index];
    char primary[96], secondary[120];
    const char* subject = attention.shipIndex < g_campaign.shipCount ?
      g_ships[attention.shipIndex].name :
      (attention.port < MAX_PORTS ? PORT_DEFS[attention.port].name : "Company notice");
    snprintf(primary, sizeof(primary), "%s%s", attention.urgent && !attention.resolved ? "! " : "", subject);
    static const char* opens[] = { "Respond", "Arrival report", "Repair / Fleet",
      "Charters", "World event", "Relief / Port works" };
    const char* action = attention.kind <= ATTENTION_RECOVERY ? opens[attention.kind] : attention_name(attention.kind);
    if (attention.kind == ATTENTION_ARRIVAL && !attention_has_arrival_report((uint8_t)index))
      action = "Arrival / Fleet";
    if (attention.kind == ATTENTION_INCIDENT && !attention.resolved &&
        !(g_incident.active && g_incident.shipIndex == attention.shipIndex))
      action = "Incident queued";
    if (attention.resolved) action = "Settled / Mark read";
    if (!attention_kind_open(attention.kind)) action = "Mark read";
    snprintf(secondary, sizeof(secondary), "%s / %s", attention.unread ? "New" : "Read", action);
    row_text(row, primary, secondary, INK, attention.unread);
  }
  draw_button(CARD_FOOTER[0], g_ui.attentionUnreadOnly ? "ALL" : "UNREAD");
  draw_button(CARD_FOOTER[1], "CLEAR READ");
  draw_button(CARD_FOOTER[2], "BACK");
}

static void draw_event_card() {
  card_frame("Trade Conditions");
  if (g_campaign.worldEventType == EVENT_NONE) {
    row_text(0, "No active trade notice", nullptr, INK, false, false);
    row_text(1, "New trade conditions will appear here.");
  } else {
    const char* kind = g_campaign.worldEventType == EVENT_SHORTAGE ? "SHORTAGE" :
      (g_campaign.worldEventType == EVENT_GLUT ? "SURPLUS" :
       (g_campaign.worldEventType == EVENT_PRIVATEERS ? "PRIVATEERS" : "TRADE WINDS"));
    char secondary[120];
    row_text(0, kind, REGION_NAMES[g_campaign.worldEventRegion], INK, true);
    uint32_t remaining = (uint32_t)g_campaign.worldEventDays * 1440u;
    uint32_t today = g_campaign.worldMinutes % 1440u;
    remaining = remaining > today ? remaining - today : 0;
    char duration[24];
    x4_format_duration(duration, sizeof(duration),
      x4_real_seconds(remaining, g_campaign.timeScale));
    snprintf(secondary, sizeof(secondary), "%s remaining%s", duration,
             g_campaign.paused ? " / Paused" : "");
    row_text(1, secondary);
    if (g_campaign.worldEventGood < GOOD_COUNT) {
      snprintf(secondary, sizeof(secondary), "Affected good: %s", GOOD_DEFS[g_campaign.worldEventGood].name);
      row_text(2, secondary);
    }
    if (g_campaign.worldEventType == EVENT_SHORTAGE) {
      row_text(3, "Sell into the affected region.");
      row_text(4, "Demand and selling prices are higher.");
    } else if (g_campaign.worldEventType == EVENT_GLUT) {
      row_text(3, "Take these goods to another region.");
      row_text(4, "Local selling prices are lower.");
    } else if (g_campaign.worldEventType == EVENT_PRIVATEERS) {
      row_text(3, "Consider avoiding marked sea lanes.");
      row_text(4, "Voyage risk is higher in this region.");
    } else {
      row_text(3, "Consider the marked sea lanes.");
      row_text(4, "Favorable winds shorten the voyage.");
    }
    row_text(5, "The map marks affected ports and lanes.");
  }
  bool market = g_campaign.worldEventType == EVENT_SHORTAGE || g_campaign.worldEventType == EVENT_GLUT;
  draw_button(CARD_FOOTER[0], market ? "MARKET" : "REGION MAP");
  draw_button(CARD_FOOTER[1], "MAP");
  draw_button(CARD_FOOTER[2], "BACK");
}

static void draw_arrival_card() {
  card_frame(g_ui.arrivalSummaryWon ? "The Grand Charter" : "Arrival Report");
  if (g_ui.arrivalSummaryShip < g_campaign.shipCount && g_ui.arrivalSummaryPort < MAX_PORTS) {
    char primary[96], secondary[120];
    snprintf(primary, sizeof(primary), "%s reached %s",
             g_ships[g_ui.arrivalSummaryShip].name, PORT_DEFS[g_ui.arrivalSummaryPort].name);
    row_text(0, primary, g_ui.arrivalSummaryWon ?
             "The company has circled the world. Rank: Grand Charter." :
             "The ship is ready for orders.", INK, true);
    snprintf(primary, sizeof(primary), "%u unit%s delivered",
             g_ui.arrivalSummaryDelivered, g_ui.arrivalSummaryDelivered == 1 ? "" : "s");
    row_text(1, primary);
    snprintf(primary, sizeof(primary), "%+ld coins earned", (long)g_ui.arrivalSummaryCoins);
    row_text(2, primary);
    snprintf(secondary, sizeof(secondary), "Inspect %s, unload cargo, or choose another job.",
             PORT_DEFS[g_ui.arrivalSummaryPort].name);
    row_text(3, "NEXT", secondary);
  }
  draw_button(CARD_FOOTER[0], "OPEN PORT");
  draw_button(CARD_FOOTER[1], "MAP");
  draw_button(CARD_FOOTER[2], "BACK");
}

static const char* trait_effect(uint8_t trait, uint8_t level) {
  static char effect[80];
  if (trait == TRAIT_NAVIGATOR) snprintf(effect, sizeof(effect), "Speed +%u | Storm defense +1", 4 + level * 2);
  else if (trait == TRAIT_GUNNER) snprintf(effect, sizeof(effect), "Guns +%u", 1 + level / 2);
  else if (trait == TRAIT_MERCHANT) snprintf(effect, sizeof(effect), "Rewards +%u%% | Bribes cost less", level * 5);
  else if (trait == TRAIT_SURGEON) snprintf(effect, sizeof(effect), "Illness defense +4 | Reduced crew loss");
  else snprintf(effect, sizeof(effect), "Operating costs -%u%%", min(20, level * 4));
  return effect;
}

static void draw_captains_card() {
  if (g_ui.captainView == 1 && g_ui.selectedCaptain < MAX_CAPTAINS &&
      g_captains[g_ui.selectedCaptain].active) {
    const CaptainProfile& captain = g_captains[g_ui.selectedCaptain];
    card_frame(captain.name);
    char primary[96], secondary[120];
    snprintf(primary, sizeof(primary), "%s | Level %u", content_trait_name(captain.trait), captain.level);
    snprintf(secondary, sizeof(secondary), "XP %u | Loyalty %u", captain.xp, captain.loyalty);
    row_text(0, primary, secondary);
    row_text(1, "Specialty", trait_effect(captain.trait, captain.level));
    snprintf(primary, sizeof(primary), "%u voyages | %u units delivered", captain.voyages, captain.deliveries);
    row_text(2, "Service record", primary);
    snprintf(primary, sizeof(primary), "%u rescues | %u incidents survived", captain.rescues, captain.incidentsSurvived);
    row_text(3, "At sea", primary);
    row_text(4, captain.assignedShip < g_campaign.shipCount ? "Assigned" : "Reserve",
             captain.assignedShip < g_campaign.shipCount ? g_ships[captain.assignedShip].name : "No ship assignment");
    static const char* scarNames[] = { "none", "stormwise", "corsair", "reefwise" };
    snprintf(primary, sizeof(primary), "%s%s%s",
             captain.scarCount ? scarNames[captain.scars[0] <= SCAR_REEF ? captain.scars[0] : 0] : "none",
             captain.scarCount > 1 ? ", " : "",
             captain.scarCount > 1 ? scarNames[captain.scars[1] <= SCAR_REEF ? captain.scars[1] : 0] : "");
    row_text(5, "Scars", primary, INK, false, captain.scarCount > 0);
    draw_button(CARD_FOOTER[0], "ASSIGN");
    draw_button(CARD_FOOTER[1], "BACK");
    draw_button(CARD_FOOTER[2], "BACK");
    return;
  }
  if (g_ui.captainView == 2) {
    card_frame("Recruitment Office");
    bool hireAffordable = false;
    for (int candidate = 0; candidate < 2; ++candidate) {
      CaptainProfile preview{};
      bool available = sim_recruit_candidate(g_ui.selectedPort, (uint8_t)candidate, &preview);
      bool affordable = available && g_campaign.coins >= preview.hireCost;
      if (candidate == g_ui.selectedCaptain) hireAffordable = affordable;
      for (int part = 0; part < 3; ++part) {
        int row = candidate * 3 + part;
        char primary[96] = "", secondary[120] = "";
        if (available && part == 0) {
          snprintf(primary, sizeof(primary), "%s | %s Level %u", preview.name,
                   content_trait_name(preview.trait), preview.level);
          snprintf(secondary, sizeof(secondary), "Hire for %u coins", preview.hireCost);
        } else if (available && part == 1) snprintf(primary, sizeof(primary), "%s", trait_effect(preview.trait, preview.level));
        else if (available) snprintf(primary, sizeof(primary), "XP %u | Loyalty %u", preview.xp, preview.loyalty);
        row_text(row, primary, *secondary ? secondary : nullptr, INK,
                 available && candidate == g_ui.selectedCaptain, affordable);
      }
    }
    draw_button(CARD_FOOTER[0], "HIRE", g_ui.selectedCaptain < 2 && hireAffordable);
    draw_button(CARD_FOOTER[1], "BACK");
    draw_button(CARD_FOOTER[2], "BACK");
    return;
  }
  card_frame("Captain Roster");
  int start = g_ui.page * CARD_VISIBLE_ROWS;
  for (int row = 0; row < CARD_VISIBLE_ROWS; ++row) {
    int index = start + row;
    bool active = index < MAX_CAPTAINS && g_captains[index].active;
    if (!active) { row_text(row, "", nullptr, INK, false, false); continue; }
    const CaptainProfile& captain = g_captains[index];
    char primary[96], secondary[120];
    snprintf(primary, sizeof(primary), "%s | %s Level %u", captain.name,
             content_trait_name(captain.trait), captain.level);
    snprintf(secondary, sizeof(secondary), "XP %u | Loyalty %u | %s", captain.xp,
             captain.loyalty, captain.assignedShip < g_campaign.shipCount ?
             g_ships[captain.assignedShip].name : "Reserve");
    row_text(row, primary, secondary, INK, index == g_ui.selectedCaptain);
  }
  draw_button(CARD_FOOTER[0], "PAGE", g_campaign.captainCount > CARD_VISIBLE_ROWS);
  draw_button(CARD_FOOTER[1], "RECRUIT");
  draw_button(CARD_FOOTER[2], "BACK");
}

#include "render_voyage_x4.h"
#include "render_watch_x4.h"
#include "render_return_x4.h"
#include "render_x4_story.h"

static void draw_tutorial_card() {
  card_frame("First Voyage");
  if (g_campaign.economyVersion >= 2) {
    static const char* const titles[]={"Your first sale","Buy for the next port","Choose where to sail","Watch your voyage","Sell on your terms","Parcels are optional"};
    static const char* const lines[][3]={
      {"Your ship starts with 2 Grain, a gift.","Open your ship, then TRADE > SELL.","OK selects. Up/down sets quantity. OK sells."},
      {"Rocker: Buy / Sell / Parcels.","Buy goods this port MAKES.","Sell at a port that WANTS them for more."},
      {"BACK to your ship, then choose SAIL.","Compare what each port wants and pays.","Choose a port, review, then confirm."},
      {"WATCH follows your boat across the sea.","Travel continues while the device sleeps.","Unanswered incidents take the free safe option."},
      {"Arrival leaves your goods aboard.","Open TRADE > SELL when the price suits you.","Sell one unit, several, or keep them all."},
      {"A parcel is free to accept and uses one slot.","It pays automatically at its named port.","Start with a small trade. Grow your fleet."}
    };
    uint8_t page=min((int)g_ui.page,(int)MERCHANT_TUTORIAL_PAGES-1);
    char label[32];snprintf(label,sizeof(label),"%u / %u",page+1,MERCHANT_TUTORIAL_PAGES);
    // Art in a fixed 120x96 slot; title beside it; page count on the right.
    text_at(CARD_X+CARD_W-16-text_width(label),130,INK,label);
    const X4Art& art=page==2 || page==3 ? X4_SHIPS[0] : page==5 ? X4_PARCELS[0] : X4_GOODS[page==4?GOOD_SPICES:GOOD_GRAIN];
    art_in_box(40,92,art,120,96);
    text_fit_at(176,122,480,INK,titles[page],3);
    for(int line=0;line<3;++line)text_fit_at(40,206+line*44,712,INK,lines[page][line]);
    draw_button(CARD_FOOTER[0],page+1==MERCHANT_TUTORIAL_PAGES?"START TRADING":"NEXT");
    draw_button(CARD_FOOTER[1],"SKIP TOUR");
    draw_button(CARD_FOOTER[2],"BACK",page>0);
    return;
  }
  static const char* lines[] = {
    "1. JOBS: choose a contract that fits.",
    "2. Note the job's destination.",
    "3. ROUTE: add neighboring ports.",
    "4. Check minutes, cost, income and risk.",
    "5. SAIL: voyages run as you manage.",
    "6. NOTICES: arrivals and incidents."
  };
  for (int row = 0; row < CARD_VISIBLE_ROWS; ++row)
    row_text(row, lines[row]);
  draw_button(CARD_FOOTER[0], "RESTART");
  draw_button(CARD_FOOTER[1], "DISMISS");
  draw_button(CARD_FOOTER[2], "BACK");
}

static void draw_port_reference_card() {
  if(g_ui.card==CARD_PORT_PICKER) {
    card_frame(g_ui.captainView==1?"Choose port / Recruit":"Choose port / Shipyard");
    for(int row=0;row<CARD_VISIBLE_ROWS;++row){uint8_t port=x4_yard_port(row);if(port<MAX_PORTS)row_text(row,PORT_DEFS[port].name);}
    draw_button(CARD_FOOTER[0],"PREVIOUS");draw_button(CARD_FOOTER[1],"NEXT");draw_button(CARD_FOOTER[2],"BACK");return;
  }
  char line[128];snprintf(line,sizeof(line),"%s / %s",PORT_DEFS[g_ui.selectedPort].name,
    g_ui.card==CARD_PORT_PRICES?"Prices":g_ui.captainView>=2?"Choose ship":g_ui.captainView?"Incoming ships":"Ships here");
  if(g_ui.card==CARD_PORT_SHIPS && g_ui.captainView>=2)snprintf(line,sizeof(line),"%s",g_ui.captainView==2?"Choose ship / Cargo":g_ui.captainView==3?"Choose ship / Range fitting":"Assign captain / Choose ship");
  card_frame(line);
  if(g_ui.card==CARD_PORT_PRICES) {
    text_at(40,100,INK,"Current price per unit");
    for(int row=0;row<4;++row) {
      int good=g_ui.page*4+row;if(good>=GOOD_COUNT)break;
      art_at(40,148+row*48,X4_GOODS[good]);
      text_fit_at(92,148+row*48,255,INK,GOOD_DEFS[good].name);
      snprintf(line,sizeof(line),"Buy %u / Sell %u",content_market_price(g_ui.selectedPort,good,true),content_market_price(g_ui.selectedPort,good,false));
      text_fit_at(354,148+row*48,408,INK,line);
    }
    draw_button(CARD_FOOTER[0],"PREVIOUS");draw_button(CARD_FOOTER[1],"NEXT");
  } else {
    bool any=false;
    for(int row=0;row<CARD_VISIBLE_ROWS;++row) {
      uint8_t id=x4_port_ship(row);if(id>=g_campaign.shipCount)break;any=true;
      const Ship& ship=g_ships[id];
      snprintf(line,sizeof(line),"%s / %s",ship.name,g_ui.captainView==4?"Assign captain":g_ui.captainView>=2?PORT_DEFS[ship.currentPort].name:g_ui.captainView?(ship.state==SHIP_AWAITING_DECISION?"Encounter":"Sailing"):ship.state==SHIP_DISABLED?"Needs repair":"Docked");
      row_text(row,line);
    }
    if(!any)wrapped_text(40,128,712,g_ui.captainView?"No ships are sailing to this port.":"None of your ships are docked here.",3,44);
    if(g_campaign.shipCount>CARD_VISIBLE_ROWS){draw_button(CARD_FOOTER[0],"PREVIOUS");draw_button(CARD_FOOTER[1],"NEXT");}
  }
  draw_button(CARD_FOOTER[2],"BACK");
}

static void draw_card() {
  switch (g_ui.card) {
    case CARD_PORT_PRICES: case CARD_PORT_SHIPS: case CARD_PORT_PICKER: draw_port_reference_card();break;
    case CARD_SHIP: draw_merchant_ship(); break;
    case CARD_TRADE: draw_merchant_trade(); break;
    case CARD_PACKAGES: draw_merchant_packages(); break;
    case CARD_VOYAGE_LOAD: draw_dispatch_load(); break;
    case CARD_VOYAGE_DEPARTURE: draw_dispatch_departure(); break;
    case CARD_RETURN_DETAILS: draw_return_details(); break;
    case CARD_VOYAGE: draw_voyage_card(); break;
    case CARD_MORE: draw_voyage_details(); break;
    case CARD_JOBS: draw_jobs_card(); break;
    case CARD_MANIFEST: draw_manifest_card(); break;
    case CARD_ROUTE: draw_route_card(); break;
    case CARD_MARKET: draw_market_card(); break;
    case CARD_FLEET: draw_fleet_card(); break;
    case CARD_LEDGER: draw_ledger_card(); break;
    case CARD_INCIDENT: draw_story_incident(); break;
    case CARD_SHIPYARD: draw_shipyard_card(); break;
    case CARD_CHARTER: draw_story_charters(); break;
    case CARD_MENU: draw_menu_card(); break;
    case CARD_HELP: draw_help_card(); break;
    case CARD_INBOX: draw_inbox_card(); break;
    case CARD_EVENT: draw_event_card(); break;
    case CARD_CAPTAINS: draw_story_captains(); break;
    case CARD_TUTORIAL: draw_tutorial_card(); break;
    case CARD_JOURNAL: draw_journal_card(); break;
    case CARD_ARRIVAL: draw_story_arrival(); break;
    case CARD_PROGRESS: draw_voyage_goal_card(); break;
    case CARD_OUTCOME: draw_story_outcome(); break;
    case CARD_KEEPSAKES: draw_keepsakes_card(); break;
    case CARD_ATLAS: draw_atlas_card(); break;
    default: break;
  }
}

// Card footer hint so the button scheme is discoverable without the help card.
static void draw_card_hint() {
  if (dispatch_native_card() || g_ui.card == CARD_ARRIVAL || g_ui.card == CARD_RETURN_DETAILS) return;
  char where[120], controls[128];
  const char* rocker = "";
  if (x4_page_keys_active()) rocker = g_ui.card == CARD_MARKET ? " / rocker: trade 5" :
    g_ui.card == CARD_VOYAGE && g_ui.page == 2 ? " / rocker: quantity" :
    g_ui.card == CARD_ROUTE && g_ui.page == 1 ? " / rocker: price" : " / rocker: page";
  snprintf(controls, sizeof(controls), "UP/DOWN / OK select / BACK %s%s",
           "back", rocker);
  if (g_ui.card == CARD_MARKET) {
    HitRegion marketFocus{};
    bool tradeFocused = x4_focus_region(&marketFocus) && marketFocus.y == CARD_FOOTER[0].y;
    snprintf(controls, sizeof(controls), "%s",
             tradeFocused ? "UP/DOWN action / OK confirm / BACK close" :
                            "UP/DOWN goods / OK trade / BACK close");
  }
  if(g_ui.card==CARD_TUTORIAL && g_campaign.economyVersion>=2)
    snprintf(controls,sizeof(controls),"OK selects / BACK previous / Rocker pages");
  const char* hint=controls;
  if(g_ui.card==CARD_INCIDENT && g_incident.active && g_incident.shipIndex<g_campaign.shipCount) {
    const Ship& ship=g_ships[g_incident.shipIndex];
    if(ship.merchant.state==MERCHANT_SAILING) {
      char remaining[24];voyage_duration(ship.encounterWait?ship.encounterWait:300,remaining,sizeof(remaining));
      const auto fallback=sim_merchant_incident_choice(encounter_fallback(g_incident.kind));
      if(encounter_opportunity(g_incident.kind))snprintf(controls,sizeof(controls),"Offer closes in %s. Nothing accepted.",remaining);
      else snprintf(controls,sizeof(controls),"In %s: %s",remaining,fallback.title?fallback.title:"safe passage");
      hint=controls;
    } else hint="Decide before this ship sails on";
  }
  bool warning = false;
  HitRegion focus;
  if (g_ui.card == CARD_ROUTE && g_ui.page == 0 && g_ui.selectedShip < g_campaign.shipCount &&
      x4_focus_region(&focus) && focus.x == X4_ROUTE_ROWS[0].x && focus.id >= 0 && focus.id < ROUTE_VISIBLE_ROWS) {
    int destination = x4_route_candidate(focus.id);
    if (destination >= 0) {
      SimResult canAdd = sim_route_can_add((uint8_t)destination);
      const Ship& ship = g_ships[g_ui.selectedShip];
      uint8_t from = g_ui.routeCount ? g_ui.routeDraft[g_ui.routeCount - 1] : ship.currentPort;
      int lane = content_lane_between(from, (uint8_t)destination);
      if (!canAdd) {
        snprintf(where, sizeof(where), "%s", sim_result_text(canAdd.code));
        warning = true;
      } else if (lane >= 0) {
        int hazard = sim_lane_hazard((uint8_t)lane);
        snprintf(where, sizeof(where), "Wind %+d / Risk %s / net after debt",
                 sim_lane_wind((uint8_t)lane, from), hazard >= 5 ? "High" : hazard >= 3 ? "Moderate" : "Low");
      } else where[0] = 0;
      if (*where) hint = where;
    }
  }
  if (g_ui.card == CARD_ROUTE && g_ui.page == 0 && !g_ui.routeCount &&
      g_ui.selectedShip < g_campaign.shipCount && x4_focus_region(&focus) &&
      focus.x == CARD_FOOTER[0].x && focus.y == CARD_FOOTER[0].y) {
    const RouteTemplate& route = g_ships[g_ui.selectedShip].routeTemplate;
    if (route.stopCount && (route.loadMode || route.sellMode || route.buyGood < GOOD_COUNT))
      hint = "RECALL+PREP loads and trades now; SAIL departs";
  }
  bool wantsDockedShip = g_ui.card == CARD_JOBS || g_ui.card == CARD_MARKET ||
                         g_ui.card == CARD_SHIPYARD;
  if (g_ui.card == CARD_MANIFEST && g_ui.selectedShip < g_campaign.shipCount &&
      g_ships[g_ui.selectedShip].state != SHIP_DOCKED && g_ships[g_ui.selectedShip].state != SHIP_ARRIVED)
    hint = "Cargo aboard / Transfers available after docking";
  if (wantsDockedShip && sim_selected_docked_ship() < 0) {
    if (g_ui.selectedShip < g_campaign.shipCount) {
      const Ship& s = g_ships[g_ui.selectedShip];
      snprintf(where, sizeof(where), "%s is %s%s",
               s.name, ship_at_port(s) ? "at " : "sailing to ",
               PORT_DEFS[ship_at_port(s) ? s.currentPort : s.toPort].name);
    } else {
      snprintf(where, sizeof(where), "No ship docked at %s", PORT_DEFS[g_ui.selectedPort].name);
    }
    hint = where;
    warning = true;
  }
  char fitted[128];
  fit_text(hint, fitted, sizeof(fitted), CARD_W - 24, 2);
  if (warning) display.fillRect(CARD_X + 8, CARD_Y + CARD_H - 38, CARD_W - 16, 36, INK);
  text_at(center_text(CARD_X, CARD_W, fitted, 2), CARD_Y + CARD_H - 36, warning ? PAPER : INK, fitted, 2);
}

static void draw_guidance_hint() {
  if (g_campaign.economyVersion && (g_ui.card == CARD_NONE || g_ui.card == CARD_VOYAGE)) return;
  if (!sim_tutorial_guided()) return;
  const HitRegion* target = nullptr;
  if (g_ui.card == CARD_JOBS && g_campaign.tutorialStep == TUTORIAL_CONTRACT)
    target = &CARD_ROWS[0];
  else if (g_ui.card == CARD_ROUTE && g_campaign.tutorialStep == TUTORIAL_ROUTE)
    target = &X4_ROUTE_ROWS[0];
  else if (g_ui.card == CARD_ROUTE && g_campaign.tutorialStep == TUTORIAL_DISPATCH)
    target = &CARD_FOOTER[1];
  HitRegion slot;
  if (g_ui.card == CARD_NONE && g_ui.scene == SCENE_PORT &&
      (g_campaign.tutorialStep == TUTORIAL_CONTRACT || g_campaign.tutorialStep == TUTORIAL_ROUTE)) {
    int index = x4_column_slot(g_campaign.tutorialStep == TUTORIAL_CONTRACT ? X4_CMD_JOBS : X4_CMD_ROUTE);
    if (index >= 0) { slot = x4_slot_region(index); target = &slot; }
  }
  if (!target) return;
  int gap = target->h < 40 ? 3 : 5;
  int x0 = target->x - gap, y0 = target->y - gap, x1 = target->x + target->w + gap, y1 = target->y + target->h + gap;
  dashed_line(x0, y0, x1, y0, 4, 3);
  dashed_line(x1, y0, x1, y1, 4, 3);
  dashed_line(x1, y1, x0, y1, 4, 3);
  dashed_line(x0, y1, x0, y0, 4, 3);
}

// Thick frame around the focused control. Larger targets get a white gap so
// the frame reads even against a black (selected) button.
static void draw_focus() {
  HitRegion r;
  if (!x4_focus_region(&r)) return;
  if (r.h < 48) {
    // Thicken the control's own outline, inside its bounds, so the frame
    // never touches a rule or hint just outside it.
    frame_rect(r.x, r.y, r.w, r.h - 1, 3);
    return;
  }
  int x = r.x - 6, y = r.y - 6, w = r.w + 12, h = r.h + 12;
  // A paper ring separates the frame from the content and swallows the row's
  // own bottom rule, which would otherwise show as a second line.
  for (int ring = 3; ring <= 5; ++ring) display.drawRect(x + ring, y + ring, w - 2 * ring, h - 2 * ring, PAPER);
  frame_rect(x, y, w, h, 3);
}

static void draw_title(const char* overridePrompt = nullptr) {
  bitmap_at(0, 0, X4_TITLE_DATA, X4_TITLE_W, X4_TITLE_H, INK, PAPER);
  // Book-frontispiece lettering; task screens retain their familiar UI font.
  display.setFont(&fonts::FreeSerifBold24pt7b);
  display.setTextSize(1);
  display.setTextColor(INK);
  display.setTextDatum(lgfx::textdatum_t::top_left);
  display.drawString("CROSSWINDS", 32, 28);
  const char* prompt = overridePrompt ? overridePrompt : g_bootResumed ? "Press OK to sail again" : "Press OK to begin";
  int width = text_width(prompt) + 36;
  display.fillRoundRect(28, 414, width, 52, 5, PAPER);
  display.drawRoundRect(28, 414, width, 52, 5, INK);
  text_at(46, text_middle(414, 52), INK, prompt);
}

static void draw_all() {
  if (g_ui.title) { draw_title(); draw_legend(); return; }
  if (x4_goal_moment_active()) { draw_goal_moment(); return; }
  if (x4_story_moment_active()) { draw_story_moment(); return; }
  if (x4_onboarding_screen()) { draw_onboarding(); return; }
  display.fillScreen(PAPER);
  if (!dispatch_native_card()) draw_topbar();
  if (g_ui.card == CARD_NONE) {
    if (voyage_watch_screen()) draw_watch_journey();
    else if (g_ui.scene == SCENE_WORLD) { draw_world_map(); draw_panel(); }
    else if (g_ui.scene == SCENE_FLEET) draw_dispatch_fleet();
    else if (g_ui.scene == SCENE_COMPANY) {
      if (const ReturnReport* report = x4_return_report()) draw_return_company(*report);
      else draw_voyage_company();
    }
    else draw_voyage_port();
    draw_column_buttons();
  } else {
    draw_card();
    draw_card_hint();
  }
  draw_onboarding_strip();
  draw_ticker();
  draw_guidance_hint();
  draw_focus();
}

// ---------------------------------------------------------------------------
// Refresh policy
// ---------------------------------------------------------------------------

static uint32_t hash_bytes(uint32_t h, const void* data, size_t n) {
  const uint8_t* p = (const uint8_t*)data;
  while (n--) { h ^= *p++; h *= 16777619u; }
  return h;
}

template <typename T>
static uint32_t mix(uint32_t h, const T& value) { return hash_bytes(h, &value, sizeof(value)); }

// Anything whose change must be shown immediately.
static uint32_t structure_hash() {
  uint32_t h = 2166136261u;
  h = mix(h, g_ui.title); h = mix(h, g_ui.scene); h = mix(h, g_ui.card);
  h = mix(h, x4_watch_active());
  h = mix(h, g_ui.page); h = mix(h, g_ui.captainView); h = mix(h, g_ui.selectedShip);
  const ReturnReport* report = x4_return_report();
  h = mix(h, report != nullptr);
  if (report) h = mix(h, report->fingerprint);
  h = mix(h, x4_goal_moment_active());
  h = mix(h, x4_story_moment_mode()); h = mix(h, x4_story_moment_chapter());
  h = mix(h, g_atlas.charted);
  h = mix(h, sim_onboarding_step()); h = mix(h, x4_onboarding_pick()); h = mix(h, x4_onboarding_name_index());
  h = mix(h, g_ui.selectedPort); h = mix(h, g_ui.selectedGood); h = mix(h, g_ui.selectedCaptain);
  h = mix(h, g_ui.mapZoom); h = mix(h, g_ui.mapFocusX); h = mix(h, g_ui.mapFocusY);
  bool following = render_map_following_ship();
  h = mix(h, following);
  if (g_ui.selectedShip < g_campaign.shipCount) {
    const Ship& selected = g_ships[g_ui.selectedShip];
    h = mix(h, selected.state); h = mix(h, selected.fromPort); h = mix(h, selected.toPort);
  }
  h = mix(h, g_ui.confirmReset); h = mix(h, g_ui.attentionUnreadOnly);
  h = mix(h, g_ui.routeCount); h = mix(h, g_ui.routeScroll);
  if (g_ui.card == CARD_TRADE) h = mix(h, x4_merchant_trade_quote());
  if (g_ui.card == CARD_VOYAGE && g_campaign.economyVersion >= 2) h = mix(h, x4_merchant_sail_quote());
  if (g_ui.card == CARD_VOYAGE || g_ui.card == CARD_VOYAGE_LOAD) h = mix(h, x4_voyage_quote());
  if (g_ui.card == CARD_VOYAGE_LOAD) h = mix(h, x4_load_selected_good());
  h = hash_bytes(h, g_ui.routeDraft, sizeof(g_ui.routeDraft));
  h = mix(h, g_ui.arrivalSummaryVisible); h = mix(h, g_incident);
  h = mix(h, g_ui.arrivalSummaryShip); h = mix(h, g_ui.arrivalSummaryPort);
  h = mix(h, g_ui.arrivalSummaryDelivered); h = mix(h, g_ui.arrivalSummaryCoins);
  h = mix(h, g_ui.arrivalSummaryWon); h = mix(h, g_incidentOutcome.serial);
  if (g_ui.card == CARD_MARKET) {
    h = mix(h, g_campaign.coins); h = mix(h, g_campaign.recoveryDebt);
    h = mix(h, g_campaign.worldEventType); h = mix(h, g_campaign.worldEventGood);
    h = mix(h, g_campaign.worldEventRegion); h = mix(h, g_campaign.worldEventDays);
    const PortState& market = g_ports[g_ui.selectedPort];
    h = hash_bytes(h, market.stock, sizeof(market.stock));
    h = hash_bytes(h, market.priceHistory, sizeof(market.priceHistory));
    int ship = sim_selected_docked_ship();
    if (ship >= 0) h = hash_bytes(h, g_ships[ship].cargo, sizeof(g_ships[ship].cargo));
  }
  h = mix(h, g_campaign.tutorialStep); h = mix(h, g_campaign.tutorialDismissed);
  h = mix(h, g_campaign.paused); h = mix(h, g_campaign.timeScale); h = mix(h, g_campaign.soundOn);
  uint8_t notice = notice_active() ? g_ui.noticeCode : 0;
  h = mix(h, notice);
  uint32_t focus = x4_focus_fingerprint();
  h = mix(h, focus);
  uint32_t presses = x4_press_count();
  h = mix(h, presses);
  return h;
}

// Everything the screen shows; slow-moving values are bucketed so that a
// voyage in progress does not force a refresh every second.
static uint32_t state_hash() {
  uint32_t h = structure_hash();
  h = mix(h, g_campaign.coins); h = mix(h, g_campaign.rank); h = mix(h, g_campaign.shipCount);
  uint32_t day = g_campaign.worldMinutes / 1440u;
  h = mix(h, day);
  h = mix(h, g_campaign.worldEventType); h = mix(h, g_campaign.worldEventGood);
  h = mix(h, g_campaign.worldEventRegion); h = mix(h, g_campaign.worldEventDays);
  if (g_ui.card == CARD_EVENT && g_campaign.worldEventDays) {
    uint32_t remaining = (uint32_t)g_campaign.worldEventDays * 1440u -
      g_campaign.worldMinutes % 1440u;
    h = mix(h, x4_real_seconds(remaining, g_campaign.timeScale));
  }
  h = mix(h, g_campaign.recoveryDebt); h = mix(h, g_campaign.captainCount);
  h = mix(h, g_campaign.regionMask); h = mix(h, g_campaign.chartMask);
  h = mix(h, g_campaign.visitedMask); h = mix(h, g_campaign.won);
  h = mix(h, g_campaign.capitalSpending); h = mix(h, g_campaign.incidentNet);
  h = hash_bytes(h, g_campaign.charterProgress, sizeof(g_campaign.charterProgress));
  h = mix(h, g_campaign.contractRevenue); h = mix(h, g_campaign.marketProfit);
  h = mix(h, g_campaign.operatingCosts); h = mix(h, g_campaign.deliveries);
  h = mix(h, g_campaign.deliveredUnits); h = mix(h, g_campaign.completedVoyages);
  h = hash_bytes(h, g_campaign.projectCargo, sizeof(g_campaign.projectCargo));
  h = mix(h, g_campaign.incidentsResolved); h = mix(h, g_campaign.lastMarketDay);
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    const Ship& s = g_ships[i];
    h = mix(h, s.state); h = mix(h, s.currentPort); h = mix(h, s.toPort);
    h = mix(h, s.condition); h = mix(h, s.morale); h = mix(h, s.cargoCount);
    h = mix(h, s.routeCount); h = mix(h, s.captainId); h = hash_bytes(h, s.modules, sizeof(s.modules));
    int left = max(0, (int)s.legTotalMinutes - (int)s.legElapsedMinutes);
    uint32_t seconds = !g_campaign.paused && s.state == SHIP_SAILING ?
      x4_real_seconds((uint32_t)left, g_campaign.timeScale) : 0;
    h = mix(h, seconds);
    if (!g_ui.title && g_ui.card == CARD_NONE && g_ui.scene == SCENE_WORLD) {
      int x = 0, y = 0;
      bool visible = map_ship_position((uint8_t)i, &x, &y);
      h = mix(h, visible);
      if (visible) { h = mix(h, x); h = mix(h, y); }
    }
    for (int c = 0; c < s.cargoCount && c < MAX_CARGO_ITEMS; ++c) h = mix(h, s.cargo[c]);
    h = mix(h, s.routeTemplate);
    h = mix(h, s.booking); h = mix(h, s.merchant);
    h = mix(h, s.encounterWait / SIM_WORLD_MINUTES_PER_REAL_SECOND);
    h = mix(h, s.sighting); h = mix(h, s.autoEncounter);
  }
  h = mix(h, g_journalHead); h = mix(h, g_journalCount);
  h = mix(h, g_attentionCount);
  for (int i = 0; i < g_attentionCount; ++i) {
    h = mix(h, g_attention[i].unread); h = mix(h, g_attention[i].resolved);
  }
  const PortState& port = g_ports[g_ui.selectedPort];
  h = hash_bytes(h, port.stock, sizeof(port.stock));
  h = mix(h, port.warehouseCount); h = mix(h, port.unlocked);
  h = mix(h, port.officeLevel); h = mix(h, port.warehouseLevel); h = mix(h, port.shipyardLevel);
  for (int i = 0; i < MAX_CONTRACTS_PER_PORT; ++i) {
    h = mix(h, g_contracts[g_ui.selectedPort][i].id);
    h = mix(h, g_contracts[g_ui.selectedPort][i].active);
    const Contract& contract = g_contracts[g_ui.selectedPort][i];
    if (g_ui.card == CARD_JOBS && contract.active == 1 && contract.kind == JOB_MAIL) {
      bool late = contract.deadlineMinute < g_campaign.worldMinutes;
      uint32_t seconds = x4_real_seconds(late ? 0 :
        contract.deadlineMinute - g_campaign.worldMinutes, g_campaign.timeScale);
      bool minutes = seconds >= 60;
      uint32_t shown = minutes ? seconds / 60u + (seconds % 60u != 0) : seconds;
      h = mix(h, late); h = mix(h, minutes); h = mix(h, shown);
    }
  }
  for (int i = 0; i < MAX_CAPTAINS; ++i) {
    const CaptainProfile& c = g_captains[i];
    h = mix(h, c.active); h = mix(h, c.assignedShip); h = mix(h, c.level);
    h = mix(h, c.xp); h = mix(h, c.loyalty); h = mix(h, c.trait);
    h = hash_bytes(h, c.name, sizeof(c.name)); h = mix(h, c.scarCount);
    h = hash_bytes(h, c.scars, sizeof(c.scars)); h = mix(h, c.voyages);
    h = mix(h, c.deliveries); h = mix(h, c.rescues); h = mix(h, c.incidentsSurvived);
  }
  h = mix(h, g_routePreparation.computed); h = mix(h, g_routePreparation.valid);
  h = mix(h, g_routePreparation.projectedCoins);
  int battery = board_x4_battery_percent();  // exact: the label prints the exact value
  bool usb = board_x4_usb_connected();
  h = mix(h, battery); h = mix(h, usb); h = mix(h, board_x4_sleep_soon());
  for (int i = 0; i < g_campaign.shipCount; ++i) h = mix(h, g_ships[i].state);  // the IDLE badge
  return h;
}

static void push_frame(bool startupStamp = false, bool screenChange = true) {
  bool full = startupStamp || fullRefreshPending || partialCount >= PARTIALS_HARD_LIMIT ||
    (screenChange && partialCount >= PARTIALS_BEFORE_FULL);
  // epd_text is the driver's half refresh: one pass with the stronger waveform.
  display.setEpdMode(full ? lgfx::epd_mode_t::epd_text : lgfx::epd_mode_t::epd_fast);
  if (full) display.display(0, 0, IW, IH);
  else display.display();
  display.waitDisplay();
  if (startupStamp) Serial.println("X4 display: startup refresh complete");
  if (full) { fullRefreshPending = false; partialCount = 0; }
  else ++partialCount;
#ifdef SH_HOST_TESTS
  ++hostPushCount;
#endif
}

// ---------------------------------------------------------------------------
// Public renderer interface
// ---------------------------------------------------------------------------

bool display_begin() {
  display.setAutoDisplay(false);
  display.init();
  display.setAutoDisplay(false);
  display.setRotation(DISPLAY_ROTATION);
  display.setEpdMode(lgfx::epd_mode_t::epd_quality);
  display.setTextWrap(false);
  display.setFont(text_font(2));
  display.setTextSize(1);
  display.fillScreen(PAPER);
  ready = true;
  bootTitleStampPending = true;
  bootMenuStampPending = true;
  fullRefreshPending = true;
  forceRedraw = true;
  return true;
}

void render_mark_dirty(int, int, int, int) { g_ui.dirty = 1; }
void render_note_touch(int, int) {}
bool render_topbar_inbox_hit(int, int) { return false; }
bool render_topbar_event_hit(int, int) { return false; }

int render_map_port_at(int x, int y) {
  int best = -1, bestDistance = 999999;
  for (int p = 0; p < MAX_PORTS; ++p) {
    int px = map_x(X4_PORT_SHORES[p].x), py = map_y(X4_PORT_SHORES[p].y);
    int dx = x - px, dy = y - py, distance = dx * dx + dy * dy;
    if (g_ports[p].unlocked && map_point_visible(px, py) &&
        distance <= 400 && distance < bestDistance) {
      best = p; bestDistance = distance;
    }
  }
  return best;
}

int render_map_ship_at(int x, int y) {
  int best = -1, bestDistance = 999999;
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    int sx = 0, sy = 0;
    if (!map_ship_position((uint8_t)i, &sx, &sy)) continue;
    int dx = x - sx, dy = y - sy, distance = dx * dx + dy * dy;
    if (distance <= 625 && distance < bestDistance) {
      best = i; bestDistance = distance;
    }
  }
  return best;
}

bool render_map_zoom_button_hit(int x, int y) {
  int slot = x4_column_slot(X4_CMD_ZOOM);
  if (slot < 0) return false;
  HitRegion r = voyage_scene_region(X4_CMD_ZOOM, slot);
  return hit_test(&r, 1, x, y) >= 0;
}

void render_map_toggle_zoom(int, int) {
  int zoom = map_zoom();
  if (zoom == 12) g_ui.mapZoom = 1;
  else if (zoom == 8) g_ui.mapZoom = 12;
  else if (zoom == 6) g_ui.mapZoom = 8;
  else if (zoom == 4) g_ui.mapZoom = 6;
  else if (zoom == 2) g_ui.mapZoom = 4;
  else {
    uint8_t port = g_ui.selectedPort < MAX_PORTS ? g_ui.selectedPort : 0;
    render_map_focus_port(port);
    g_ui.mapZoom = 2;
  }
  if (render_map_following_ship()) followCenterPending = true;
  g_ui.dirty = 1;
}

void render_map_pan_by(int startX, int startY, int currentX, int currentY,
                       uint8_t startFocusX, uint8_t startFocusY) {
  if (!sim_feature_unlocked(FEATURE_ZOOM)) return;
  int zoom = map_zoom();
  if (zoom <= 1) return;
  if (startX == currentX && startY == currentY) return;
  render_map_stop_follow();
  int visibleW = MAP_W / zoom;
  int visibleH = MAP_H / zoom;
  int focusX = (int)startFocusX - (currentX - startX) * visibleW / MAP_DRAW_W;
  int focusY = (int)startFocusY - (currentY - startY) * visibleH / MAP_DRAW_H;
  while (focusX < 0) focusX += MAP_W;
  while (focusX >= MAP_W) focusX -= MAP_W;
  focusY = max(visibleH / 2, min(MAP_H - visibleH / 2, focusY));
  if (g_ui.mapFocusX == focusX && g_ui.mapFocusY == focusY) return;
  g_ui.mapFocusX = (uint8_t)focusX;
  g_ui.mapFocusY = (uint8_t)focusY;
  g_ui.dirty = 1;
}

void render_tick() {
  uint32_t now = millis();
  if (deferredUntil && (int32_t)(now - deferredUntil) >= 0) {
    deferredUntil = 0;
    g_ui.dirty = 1;
  }
  if (g_ui.noticeCode != SIM_OK && (int32_t)(g_ui.noticeUntilMs - now) <= 0) {
    g_ui.noticeCode = SIM_OK;
    g_ui.dirty = 1;
  }
}

void render_request_full_refresh() {
  fullRefreshPending = true;
  forceRedraw = true;
  g_ui.dirty = 1;
}

void render_sleep_screen(const char* note) {
  if (!ready) return;
  bool failed = note && strcmp(note, "SAVE FAILED") == 0;
  // Both normal sleep paths share the same quiet cover and wake prompt.
  draw_title(failed ? "Save failed. Hold Power to return." : "Hold Power");
  if (failed) {
    const char* status = "Voyage suspended";
    // Clear only the changing caption, preserving the surrounding rigging.
    display.fillRect(30, 90, text_width(status) + 8, text_height() + 8, PAPER);
    text_at(34, 94, INK, status);
  }
  fullRefreshPending = true;
  push_frame();
  forceRedraw = true;
}

void render_frame() {
  if (!ready) return;
  if (board_x4_asleep()) { g_ui.dirty = 0; return; }
  uint32_t now = millis();
  sync_map_visual_state();
  // A notice visible when composition starts remains visible in that frame.
  // Slow host drawing must not expire it between fingerprinting and the ticker.
  frameNoticeActive = g_ui.noticeCode != SIM_OK && (int32_t)(g_ui.noticeUntilMs - now) > 0;
  bool input = x4_take_input_pending();
  uint32_t structure = structure_hash();
  uint32_t fingerprint = state_hash();
  if (!forceRedraw && fingerprint == lastFingerprint) { g_ui.dirty = 0; return; }
  bool immediate = forceRedraw || input || structure != lastStructure;
  uint32_t interval = live_voyage_map() ? LIVE_MAP_REFRESH_MS : PERIODIC_REFRESH_MS;
  if (!immediate && (uint32_t)(now - lastPushMs) < interval) {
    deferredUntil = lastPushMs + interval;
    if (!deferredUntil) deferredUntil = 1;
    g_ui.dirty = 0;
    return;
  }
  // Automatic camera motion is accepted only after the update is due. Its
  // structural focus change must not turn every simulation tick into a push.
  if (follow_map_camera()) {
    structure = structure_hash();
    fingerprint = state_hash();
  }
  display.startWrite();
  draw_all();
  display.endWrite();
  bool startupStamp = g_ui.title ? bootTitleStampPending : bootMenuStampPending;
  uint32_t screen = mix(mix(mix(mix(2166136261u, g_ui.title), g_ui.scene), g_ui.card), x4_watch_active());
  screen = mix(screen, x4_goal_moment_active());
  screen = mix(screen, x4_story_moment_mode());
  screen = mix(screen, x4_onboarding_screen() ? sim_onboarding_step() : 0);
  push_frame(startupStamp, screen != lastScreen);
  lastScreen = screen;
  if (g_ui.title) bootTitleStampPending = false;
  else bootMenuStampPending = false;
  lastFingerprint = fingerprint;
  lastStructure = structure;
  if (!g_ui.title && g_ui.card == CARD_NONE && g_ui.scene == SCENE_WORLD)
    for (int i = 0; i < g_campaign.shipCount; ++i) {
      int x = 0, y = 0;
      if (map_ship_position((uint8_t)i, &x, &y)) {
        mapVisual[i].fraction = map_visual_fraction((uint8_t)i);
        mapVisual[i].valid = 1;
      }
    }
  lastPushMs = millis();
  deferredUntil = 0;
  forceRedraw = false;
  g_ui.dirty = 0;
}

#ifdef SH_HOST_TESTS
void render_host_reset_map_presentation() {
  render_map_stop_follow();
  memset(mapVisual, 0, sizeof(mapVisual));
  mapVisualSeed = g_campaign.seed;
}
bool render_host_map_position(uint8_t ship, int* x, int* y) {
  return map_ship_position(ship, x, y);
}
bool render_host_route_point(uint8_t lane, uint8_t from, uint16_t fraction,
                             int32_t* worldX, int32_t* worldY, int* screenX, int* screenY) {
  if (!lane_valid(lane, from) || fraction > 10000) return false;
  X4RoutePoint point = lane_world_point_at(lane, from, fraction, 10000);
  *worldX = point.x; *worldY = point.y;
  *screenX = map_x(point.x); *screenY = map_y(point.y);
  return true;
}
bool render_host_route_progress_ink(uint8_t lane, uint8_t from, uint16_t fraction) {
  if (!lane_valid(lane, from) || fraction > 10000) return false;
  int x = 0, y = 0; lane_point_at(lane, from, fraction, 10000, &x, &y);
  if (!map_point_visible(x, y)) return false;
  display.clearClipRect(); display.fillScreen(PAPER);
  display.setClipRect(MAP_X, MAP_Y, MAP_DRAW_W, MAP_DRAW_H);
  draw_lane_progress(lane, from, fraction, 10000);
  display.clearClipRect();
  return display.readPixel(x, y) == 0;
}
uint32_t render_host_push_count() { return hostPushCount; }
uint32_t render_host_last_push_ms() { return lastPushMs; }
uint32_t render_host_notice_mask() { return hostNoticeMask; }
void render_host_reset_notice_mask() { hostNoticeMask = 0; }
bool render_host_ui_skin_selftest() {
  return X4_TITLE_W == IW && X4_TITLE_H == IH && X4_WORLD_MAP_W == MAP_DRAW_W &&
         X4_WORLD_MAP_H == MAP_DRAW_H && X4_HARBOR_W == MAP_DRAW_W && X4_HARBOR_H == MAP_DRAW_H;
}
#endif

