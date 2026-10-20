// Xteink X4: shared declarations for the board layer (board_x4.cpp), the button
// navigation layer (input.cpp) and the e-paper renderer (render_x4.cpp).
#pragma once
#include "game.h"
#include "display.h"


// Buttons as the resistor ladder reports them. Held upright, the front buttons
// run BACK, CONFIRM, LEFT, RIGHT; UP and DOWN are the side rocker; POWER is on top.
enum X4Button : uint8_t {
  X4_BTN_BACK, X4_BTN_CONFIRM, X4_BTN_LEFT, X4_BTN_RIGHT,
  X4_BTN_UP, X4_BTN_DOWN, X4_BTN_POWER, X4_BTN_COUNT
};

// Game actions. The game is played in landscape with the front buttons in a
// column down the right edge (RIGHT, LEFT, CONFIRM, BACK from the top), which
// become UP / DOWN / OK / BACK. The rocker flips ports and pages.
enum X4Action : uint8_t {
  X4_ACT_UP, X4_ACT_DOWN, X4_ACT_OK, X4_ACT_BACK,
  X4_ACT_PAGE_PREV, X4_ACT_PAGE_NEXT, X4_ACT_POWER, X4_ACT_COUNT
};

// Physical button -> action. Edit this table if a unit's buttons read in a
// different order; the on-screen legend highlights whichever physical button
// was pressed last, which makes a wrong entry obvious.
static constexpr uint8_t X4_ACTION_OF_BUTTON[X4_BTN_COUNT] = {
  X4_ACT_BACK,       // X4_BTN_BACK
  X4_ACT_OK,         // X4_BTN_CONFIRM
  X4_ACT_DOWN,       // X4_BTN_LEFT
  X4_ACT_UP,         // X4_BTN_RIGHT
  X4_ACT_PAGE_PREV,  // X4_BTN_UP
  X4_ACT_PAGE_NEXT,  // X4_BTN_DOWN
  X4_ACT_POWER,      // X4_BTN_POWER
};

// The four front buttons as they appear along the right screen edge, top to
// bottom, so the legend can be drawn beside each one.
static constexpr uint8_t X4_LEGEND_ORDER[4] = { X4_BTN_RIGHT, X4_BTN_LEFT, X4_BTN_CONFIRM, X4_BTN_BACK };

#include "x4_pins.h"
#include "x4_standby.h"

// Board layer.
void board_x4_begin_early();   // power latch and pin modes; call first in setup()
void board_x4_begin();         // ADC and wake handling; after the display is up
void board_x4_input_ready();   // first visible frame complete; arm fresh Power presses
bool board_x4_background_wake();  // valid timer standby boot, before display init
void board_x4_resume_standby(bool loaded);  // after coherent save load; may sleep again
void board_x4_sync_active_clock();  // settle awake time before a sleep checkpoint
uint8_t board_x4_read_buttons();  // bitmask (1 << X4Button) of buttons held now
bool board_x4_buffered_input();  // device sampler is running through redraws
int board_x4_take_button_event();  // oldest press, POWER first, or -1
bool board_x4_button_pending(uint8_t button);  // inspect without consuming a startup gesture
void board_x4_discard_button_events();  // screen changed; preserve POWER/held state
#if defined(SH_HOST_TESTS)
void board_x4_test_buffered_input(bool enabled);  // synchronous host sampler fixture
void board_x4_test_sample_buttons(uint32_t now, uint8_t mask);
bool board_x4_test_standby_begin(uint64_t nowUs, uint32_t session);
bool board_x4_test_standby_resume(uint64_t nowUs, bool retainedClock, bool userWake);
void board_x4_test_standby_lose_clock();
#endif
int board_x4_battery_percent();   // 0..100, cached
// On battery: a warning at the first threshold, a save and power-off at the second.
static constexpr int X4_BATTERY_WARN_PERCENT = 10;
static constexpr int X4_BATTERY_OFF_PERCENT = 3;
bool board_x4_usb_connected();
void board_x4_power_off();     // POWER: experimental sailing standby; long hold shuts down
void board_x4_shutdown();      // explicit true shutdown, also used for depleted battery
void board_x4_tick();          // idle timeout
bool board_x4_sleep_soon();    // within a minute of the idle power-off
bool board_x4_asleep();        // desktop preview only: sleep screen showing

// Fleet, World and Company keep fixed top tab positions. Context actions and
// paged ship rows precede them in focus order. BACK at the top level opens
// settings (or restores Watch's origin). Input owns this renderer contract.
enum X4Command : uint8_t {
  X4_CMD_PORT, X4_CMD_MAP, X4_CMD_JOBS, X4_CMD_MARKET, X4_CMD_YARD, X4_CMD_ROUTE,
  X4_CMD_CARGO, X4_CMD_FLEET, X4_CMD_LEDGER, X4_CMD_MAIL, X4_CMD_EVENT, X4_CMD_ZOOM,
  X4_CMD_COMPANY, X4_CMD_SHIP_0, X4_CMD_SHIP_1, X4_CMD_SHIP_2,
  X4_CMD_PAGE_PREV, X4_CMD_PAGE_NEXT,
  X4_CMD_WATCH_READY, X4_CMD_WATCH_MAP, X4_CMD_WATCH_NEXT,
  X4_CMD_BACK, X4_CMD_COUNT
};
int x4_column(uint8_t* out);                 // commands top to bottom, at most COLUMN_MAX
int x4_column_slot(uint8_t command);         // slot of a command, or -1
const char* x4_command_label(uint8_t command, char* buf, size_t cap);
bool x4_command_enabled(uint8_t command);
bool x4_command_lit(uint8_t command);        // always false: the owner wants no inverted button;
                                             // the unread count in the NOTICES label is the signal

// Button navigation layer (input.cpp).
bool x4_focus_region(HitRegion* out);  // rectangle of the focused control
uint32_t x4_focus_fingerprint();       // changes whenever the focus moves
bool x4_take_input_pending();          // true once after any handled button
uint32_t x4_last_input_ms();
void x4_input_ready();        // establish the first screen's input context after rendering
int x4_last_pressed();                 // physical button of the latest press, or -1
uint32_t x4_press_count();             // increments on every press
bool x4_page_keys_active();            // rocker does something on this screen

// One grouped, read-only company update after returning to the reader.
// A press after X4_RETURN_IDLE_MS without input counts as coming back; shorter
// pauses, such as watching a voyage, stay in the same session.
static constexpr uint32_t X4_RETURN_IDLE_MS = 15u * 60u * 1000u;
struct ReturnReport;
// After load/standby reconciliation or a long idle (away), or when COMPANY
// opens pending updates mid-session (not away).
void x4_prepare_return(bool resumed, bool away = true, bool keepTitle = false);
bool x4_return_away();  // the report greets a returning reader

// A completed NEXT GOAL rung takes the whole screen over whatever is open
// until OK or BACK; the screen underneath is left exactly as it was.
bool x4_goal_moment_active();
uint8_t x4_goal_moment_step();    // the GOAL_STEP_* rung just completed
uint8_t x4_goal_moment_region();  // the region a completed dock opened
int32_t x4_goal_moment_value();   // the first-profit moment's coins
// The Grey Passenger's whole-screen pages.
enum X4StoryMoment : uint8_t { X4_STORY_REVEAL = 1, X4_STORY_DELIVERED, X4_STORY_REREAD,
                               X4_ATLAS_PAGE };  // Sea Atlas page: chapter is the port
bool x4_story_moment_active();
uint8_t x4_story_moment_mode();     // X4StoryMoment
uint8_t x4_story_moment_chapter();
bool x4_merchant_story_row(int row);  // the visible Parcels row holds her offer
bool x4_merchant_story_selected();
// The guided first voyage's captain and name screens (full-screen steps).
bool x4_onboarding_screen();
uint8_t x4_onboarding_pick();        // highlighted candidate, 0..2
uint8_t x4_onboarding_name_index();  // highlighted name while naming
const ReturnReport* x4_return_report();
int x4_return_row_count();
int x4_return_row_ship(int visibleRow);  // current three-row update page
#if defined(SH_HOST_TESTS)
void input_test_set_last_interaction_ms(uint32_t when);
void input_test_goal_moments(bool enabled);  // scripted suites that fabricate progress turn them off
#endif

// Renderer (render_x4.cpp).
void render_sleep_screen(const char* note = "Hold Power");
void render_request_full_refresh();


uint8_t x4_port_ship(int row);
uint8_t x4_yard_port(int row);
