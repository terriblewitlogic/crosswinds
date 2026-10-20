#include "game.h"
#include "display.h"
#include "encounter_content.h"
#include "story_content.h"
#include <string.h>
#include "x4.h"
#include "voyage_ui.h"

#ifdef SH_HOST_TESTS
extern bool sh_test_touch(int32_t* x, int32_t* y);
#endif
static int x4_slot_at(int x, int y);
static void x4_run_slot(int slot);
static void x4_run_command(uint8_t command);
static void x4_open_ship(uint8_t ship);
static void x4_watch_ship();
static void x4_load_footer(int id);
static void x4_departure_row(int row);
static void x4_open_voyage(bool planning = false);
static void x4_open_relief();
static void x4_begin_refit(uint8_t module);
static void x4_voyage_footer(int id);
static void x4_voyage_row(int row);
static void x4_voyage_quantity(int dir);
static void x4_sync();
static bool x4_finish_return(bool restore);
static void x4_return_row(int row);
static void x4_return_footer(int id);
static bool x4_touch_return_gate();
static void x4_goal_moment_close();
static void x4_story_moment_open(uint8_t mode, uint8_t chapter);
static void x4_story_moment_close();
static void x4_onboarding_focus();
static uint8_t x4_footer_mask();
static void x4_focus_load_row(int row);
static void x4_merchant_open(int mode = -1);
static void x4_merchant_ship();
static void x4_cycle_port(int dir);
static void x4_merchant_ship_row(int row);
static int x4_rows(uint8_t* rows);
static void x4_remember_focus();
static bool navOpeningSubject=false;
static void x4_nav_enter(uint8_t card);
static void x4_nav_push();
static bool x4_nav_back();
static void x4_nav_reset();
static bool x4_nav_parent_is(uint8_t card);
static void x4_nav_drop_parent(uint8_t card);
static void x4_nav_departed();
static bool merchantUpgradeReturn = false;
static bool returnPreviousMerchantUpgradeReturn = false;
static void x4_merchant_row(int row);
static void x4_merchant_footer(int id);
static void x4_merchant_quantity(int dir);
static void x4_merchant_routes();
static bool x4_merchant_touch(int x, int y);
static void x4_merchant_voyage_row(int row);
static void x4_merchant_voyage_footer(int id);

static bool lastDown = false;
static bool mapTouchActive = false;
static bool mapTouchDragged = false;
static int mapTouchStartX = 0;
static int mapTouchStartY = 0;
static int mapTouchLastX = 0;
static int mapTouchLastY = 0;
static uint8_t mapTouchStartFocusX = 0;
static uint8_t mapTouchStartFocusY = 0;

static void open_card(uint8_t card) {
  if (g_campaign.economyVersion >= 2) {
    if (card == CARD_MARKET || card == CARD_JOBS) { x4_merchant_open(card == CARD_JOBS ? 2 : -1); return; }
    if (card == CARD_ROUTE) { x4_merchant_routes(); return; }
  }
  x4_nav_enter(card);
  if (card != CARD_MENU) g_ui.confirmReset = 0;
  if (card == CARD_MARKET || card == CARD_JOBS) {
    int ship = sim_selected_docked_ship();
    if (ship >= 0) g_ui.selectedShip = (uint8_t)ship;
  }
  g_ui.card = card;
  g_ui.page = 0;
  g_ui.routeScroll = 0;
  if (card == CARD_CAPTAINS) {
    g_ui.captainView = 0;
    g_ui.selectedCaptain = g_ui.selectedShip < g_campaign.shipCount ?
      g_ships[g_ui.selectedShip].captainId : NO_SHIP;
  }
  g_ui.dirty = 1;
}

static int input_route_candidate(int row) {
  if (g_ui.selectedShip >= g_campaign.shipCount) return -1;
  const Ship& s = g_ships[g_ui.selectedShip];
  if ((s.state != SHIP_DOCKED && s.state != SHIP_ARRIVED) || s.state == SHIP_DISABLED)
    return -1;
  uint8_t from = g_ui.routeCount ? g_ui.routeDraft[g_ui.routeCount - 1] : s.currentPort;
  int wanted = row + g_ui.routeScroll;
  int found = 0;
  for (int p = 0; p < MAX_PORTS; ++p) {
    if (!g_ports[p].unlocked || p == from || content_lane_between(from, (uint8_t)p) < 0) continue;
    if (found++ == wanted) return p;
  }
  return -1;
}

int x4_route_candidate(int row) { return input_route_candidate(row); }

static bool selected_ship_can_route() {
  if (g_ui.selectedShip >= g_campaign.shipCount) return false;
  const Ship& s = g_ships[g_ui.selectedShip];
  return s.state == SHIP_DOCKED || s.state == SHIP_ARRIVED;
}

[[maybe_unused]] static bool selected_ship_at_port() {
  if (g_ui.selectedShip >= g_campaign.shipCount) return false;
  const Ship& s = g_ships[g_ui.selectedShip];
  return s.state == SHIP_DOCKED || s.state == SHIP_ARRIVED || s.state == SHIP_DISABLED;
}

static bool guided() { return sim_tutorial_guided(); }

static uint8_t next_time_scale(uint8_t current) {
  if (current <= 1) return 2;
  if (current <= 2) return 4;
  if (current <= 4) return 8;
  if (current <= 8) return 16;
  return 1;
}

static bool apply_result(SimResult result, bool persist = false) {
  sim_set_notice(result);
  if (result && persist) save_write();
  return result;
}

// A locked feature explains itself instead of doing nothing.
static bool unlocked(uint8_t feature) {
  if (sim_feature_unlocked(feature)) return true;
  apply_result(SIM_LOCKED);
  return false;
}

static int input_attention_index(int visibleRow) {
  int wanted = g_ui.page * CARD_VISIBLE_ROWS + visibleRow;
  int found = 0;
  for (int i = g_attentionCount - 1; i >= 0; --i) {
    if (g_ui.attentionUnreadOnly && !g_attention[i].unread) continue;
    if (found++ == wanted) return i;
  }
  return -1;
}

static void select_ship_at_port(uint8_t port) {
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    const Ship& s = g_ships[i];
    if ((s.state == SHIP_DOCKED || s.state == SHIP_ARRIVED || s.state == SHIP_DISABLED) &&
        s.currentPort == port) {
      g_ui.selectedShip = (uint8_t)i;
      return;
    }
  }
}

static int x4_manifest_count() {
  if (g_ui.selectedShip >= g_campaign.shipCount) return 0;
  const Ship& ship = g_ships[g_ui.selectedShip];
  return g_ui.page ? g_ports[ship.currentPort].warehouseCount : ship.cargoCount;
}

static void x4_manifest_clamp() {
  int count = x4_manifest_count();
  int last = count ? ((count - 1) / CARD_VISIBLE_ROWS) * CARD_VISIBLE_ROWS : 0;
  if (g_ui.routeScroll > last) g_ui.routeScroll = (uint8_t)last;
}

static void handle_footer(int id) {
  if(g_campaign.economyVersion>=2 && g_ui.card==CARD_CAPTAINS && g_ui.captainView && (id==1 || id==2)) {
    if(x4_nav_parent_is(CARD_PORT_PICKER) || x4_nav_parent_is(CARD_CAPTAINS)){x4_nav_back();return;}
    id=1;
  }
  if(g_campaign.economyVersion>=2 && id==2 && !x4_return_report()) {
    bool local=(merchant_card(g_ui.card) && x4_merchant_editing()) ||
      (g_ui.card==CARD_VOYAGE && g_ui.page!=0 && g_ui.page!=5) ||
      (g_ui.card==CARD_CAPTAINS && g_ui.captainView!=0) || g_ui.card==CARD_TUTORIAL ||
      g_ui.card==CARD_OUTCOME || g_ui.card==CARD_ARRIVAL;
    if(!local && x4_nav_back())return;
  }
  switch (g_ui.card) {
    case CARD_PORT_PICKER: {
      int ports=0;for(int p=0;p<MAX_PORTS;++p)ports+=g_ports[p].unlocked?1:0;
      int pages=max(1,(ports+CARD_VISIBLE_ROWS-1)/CARD_VISIBLE_ROWS);
      if(id<2)g_ui.page=(g_ui.page+(id?1:pages-1))%pages;else g_ui.card=CARD_NONE;
      break;
    }
    case CARD_PORT_PRICES: if(id<2)g_ui.page=(g_ui.page+1)%2;else g_ui.card=CARD_NONE;break;
    case CARD_PORT_SHIPS: if(id<2)g_ui.page=(g_ui.page+1)%max(1,(g_campaign.shipCount+CARD_VISIBLE_ROWS-1)/CARD_VISIBLE_ROWS);else g_ui.card=CARD_NONE;break;
    case CARD_SHIP: if (id == 2) x4_run_command(X4_CMD_FLEET); break;
    case CARD_TRADE: case CARD_PACKAGES: x4_merchant_footer(id); break;
    case CARD_VOYAGE: x4_voyage_footer(id); break;
    case CARD_VOYAGE_LOAD: x4_load_footer(id); break;
    case CARD_VOYAGE_DEPARTURE: if (id == 2) x4_run_command(X4_CMD_FLEET); break;
    case CARD_JOBS:
      if (id == 0 && g_ui.page) --g_ui.page;
      else if (id == 1 && (g_ui.page + 1) * CARD_VISIBLE_ROWS < MAX_CONTRACTS_PER_PORT) ++g_ui.page;
      else if (id == 2) g_ui.card = CARD_NONE;
      break;
    case CARD_MANIFEST:
      if (id == 1 && !selected_ship_can_route()) { apply_result(SIM_BAD_SHIP_STATE); break; }
      if (id < 2 && g_ui.page != id) g_ui.routeScroll = 0;
      if (id == 0) g_ui.page = 0;
      else if (id == 1) { if (unlocked(FEATURE_WAREHOUSE)) g_ui.page = 1; }
      else if (id == 2) g_ui.card = CARD_NONE;
      break;
    case CARD_ROUTE:
      if (g_ui.page == 2) {
        if (id == 0) {
          SimResult applied = sim_route_prepare_apply(g_ui.selectedShip);
          apply_result(applied, applied.code == SIM_OK);
          if (applied) g_ui.page = 0;
        } else if (id == 1) {
          apply_result(sim_route_prepare_preview(g_ui.selectedShip));
        } else if (id == 2) g_ui.page = 1;
      } else if (g_ui.page == 1) {
        if (id == 0) {
          SimResult preview = sim_route_prepare_preview(g_ui.selectedShip);
          apply_result(preview);
          if (g_routePreparation.computed) g_ui.page = 2;
        } else if (id == 1) {
          sim_route_template_clear(g_ui.selectedShip);
          save_write();
        } else if (id == 2) {
          g_ui.page = 0;
          memset(&g_routePreparation, 0, sizeof(g_routePreparation));
        }
      } else if (id == 0) {
        if (g_ui.routeCount) {
          sim_route_undo();
        }
        else {
          // RECALL brings the route back and, when the template carries rules,
          // runs its preparation (sell, load, buy) so one press readies the ship.
          SimResult recalled = sim_route_recall();
          const RouteTemplate& t = g_ships[g_ui.selectedShip].routeTemplate;
          bool rules = t.loadMode || t.sellMode || t.buyGood < GOOD_COUNT;
          if (recalled && rules && sim_feature_unlocked(FEATURE_ROUTE_RULES)) {
            SimResult preview = sim_route_prepare_preview(g_ui.selectedShip);
            if (preview && g_routePreparation.computed && g_routePreparation.valid) {
              SimResult applied = sim_route_prepare_apply(g_ui.selectedShip);
              apply_result(applied, applied.code == SIM_OK);
            } else apply_result(preview);
          } else apply_result(recalled);
        }
      }
      else if (id == 1) {
        if (apply_result(sim_dispatch(g_ui.selectedShip))) {
          render_map_follow_selected_ship();
          sim_tutorial_complete(TUTORIAL_DISPATCH);
          save_write();
        }
      }
      else if (id == 2) g_ui.card = CARD_NONE;
      break;
    case CARD_MARKET: {
      int si = sim_selected_docked_ship();
      if (si < 0 && id < 2) { apply_result(SIM_NO_SHIP_HERE); break; }
      if (si >= 0 && id == 0) {
        if (apply_result(sim_market_buy((uint8_t)si, g_ui.selectedGood))) {
          sim_tutorial_complete(TUTORIAL_MARKET);
          save_write();
        }
      } else if (si >= 0 && id == 1) {
        if (apply_result(sim_market_sell((uint8_t)si, g_ui.selectedGood))) {
          sim_tutorial_complete(TUTORIAL_MARKET);
          save_write();
        }
      } else if (id == 2) g_ui.card = CARD_NONE;
      break;
    }
    case CARD_FLEET:
      if (id == 0) g_ui.page = (uint8_t)((g_ui.page + 1) % ((g_campaign.shipCount + CARD_VISIBLE_ROWS - 1) / CARD_VISIBLE_ROWS));
      else if (id == 1) { if (unlocked(FEATURE_CAPTAINS)) open_card(CARD_CAPTAINS); }
      else if (id == 2) g_ui.card = CARD_NONE;
      break;
    case CARD_LEDGER:
      if (id == 0) { if (unlocked(FEATURE_CHARTERS)) open_card(CARD_CHARTER); }
      else if (id == 1) open_card(CARD_JOURNAL);
      else if (id == 2) g_ui.card = CARD_NONE;
      break;
    case CARD_SHIPYARD: {
      // Pages: 0 ships, 1 fittings, 2 port works (repair and relief live on 2).
      // The fittings page joins the cycle only once its feature is unlocked.
      if (id == 2) {
        if (merchantUpgradeReturn) { x4_merchant_ship(); break; }
        g_ui.card = CARD_NONE; break;
      }
      if (id > 1) break;
      if (merchantUpgradeReturn) break;
      int mode = g_ui.page % 3;
      for (int i = 0; i < 3; ++i) {
        mode = (mode + (id == 0 ? 2 : 1)) % 3;
        if (g_campaign.economyVersion >= 2 && mode == 1) continue;
        if (mode != 1 || sim_feature_unlocked(FEATURE_FITTINGS)) break;
      }
      g_ui.page = (uint8_t)mode;
      break;
    }
    case CARD_PROGRESS:
      if (id == 0) {
        if (g_campaign.deliveredUnits >= 24 && g_campaign.shipCount < 2)
          apply_result(sim_buy_ship(0), true);
        else if (voyage_needs_range()) apply_result(sim_buy_ship(1), true);
        else if (g_campaign.deliveredUnits >= 48 && sim_project_region() < REGION_COUNT)
          apply_result(sim_project_contribute(sim_project_region()), true);
      } else if (id == 1 && x4_merchant_donation_good() >= 0) {
        apply_result(sim_merchant_donate(g_ui.selectedShip, (uint8_t)x4_merchant_donation_good(), 1));
      } else if (id == 1 && voyage_needs_range()) {
        open_card(CARD_PORT_SHIPS);g_ui.captainView=3;
      }
      if (id == 2) g_ui.card = CARD_NONE;
      break;
    case CARD_CHARTER:
    case CARD_HELP:
      if (id == 0) open_card(CARD_TUTORIAL);
      else if (id == 2) g_ui.card = CARD_NONE;
      break;
    case CARD_MORE:
      if (id < 2) {
        int pages = (voyage_detail_count() + CARD_VISIBLE_ROWS - 1) / CARD_VISIBLE_ROWS;
        g_ui.page = (uint8_t)((g_ui.page + 1) % max(1, pages));
      }
      else g_ui.card = CARD_NONE;
      break;
    case CARD_ARRIVAL:
      if (g_ui.arrivalSummaryShip < g_campaign.shipCount &&
          g_ships[g_ui.arrivalSummaryShip].merchant.state == MERCHANT_ARRIVED) {
        const Ship& ship = g_ships[g_ui.arrivalSummaryShip];
        if (!apply_result(sim_ack_arrival_receipt(g_ui.arrivalSummaryShip, ship.merchant.id))) break;
      }
      if (g_ui.arrivalSummaryShip < g_campaign.shipCount &&
          g_ships[g_ui.arrivalSummaryShip].booking.state == BOOKING_SETTLED) {
        const Ship& ship = g_ships[g_ui.arrivalSummaryShip];
        if (!apply_result(sim_ack_arrival_receipt(g_ui.arrivalSummaryShip, ship.booking.id))) break;
      }
      if (x4_return_report()) {
        if (id == 2) { open_card(CARD_RETURN_DETAILS); break; }
        if (!x4_finish_return(false)) break;
      }
      // The report is about one port: OPEN PORT and MAP go there, not to
      // whatever port the player was browsing.
      // ... and to the ship that arrived, so JOBS and ROUTE act on it.
      if (id == 0 || id == 1) {
        g_ui.selectedPort = g_ui.arrivalSummaryPort;
        if (g_ui.arrivalSummaryShip < g_campaign.shipCount) g_ui.selectedShip = g_ui.arrivalSummaryShip;
        g_ui.scene = id == 0 ? SCENE_PORT : SCENE_WORLD;
        g_ui.card = CARD_NONE;
        if (id == 1) {
          x4_run_command(X4_CMD_FLEET);
          if (g_ui.selectedShip < g_campaign.shipCount) g_ui.page = g_ui.selectedShip / VOYAGE_FLEET_ROWS;
        }
        if (id == 0 && g_ui.selectedShip < g_campaign.shipCount &&
            (g_ships[g_ui.selectedShip].booking.state == BOOKING_SETTLED || g_campaign.economyVersion >= 2)) x4_open_voyage();
      }
      else if (id == 2) g_ui.card = CARD_NONE;
      break;
    case CARD_RETURN_DETAILS: x4_return_footer(id); break;
    case CARD_OUTCOME:
      if (g_incidentOutcome.merchant && (id == 0 || id==2)) {
        x4_nav_back();g_ui.selectedShip=g_incidentOutcome.shipIndex;
        g_ui.card=CARD_NONE;x4_watch_ship();break;
      }
      if (id == 1 && g_incident.active) open_card(CARD_INCIDENT);
      else if (id == 0 || id == 2) g_ui.card = CARD_NONE;
      break;
    case CARD_EVENT:
      if (id == 0) {
        bool market = g_campaign.worldEventType == EVENT_SHORTAGE ||
                      g_campaign.worldEventType == EVENT_GLUT;
        if (market && !unlocked(FEATURE_MARKET)) break;
        int target = -1, lockedTarget = -1;
        for (int p = 0; p < MAX_PORTS; ++p) {
          if (PORT_DEFS[p].region != g_campaign.worldEventRegion) continue;
          if (lockedTarget < 0) lockedTarget = p;
          if (g_ports[p].unlocked) { target = p; break; }
        }
        if (market && target < 0) { apply_result(SIM_PORT_LOCKED); break; }
        x4_nav_push();
        if (g_campaign.worldEventType != EVENT_NONE) {
          if (target < 0) target = lockedTarget;
          if (target >= 0) g_ui.selectedPort = (uint8_t)target;
        }
        if (market) {
          if (g_campaign.worldEventGood < GOOD_COUNT)
            g_ui.selectedGood = g_campaign.worldEventGood;
          g_ui.scene = SCENE_PORT;
          if(g_campaign.economyVersion>=2)open_card(CARD_PORT_PRICES);else
          open_card(CARD_MARKET);
        } else {
          g_ui.scene = SCENE_WORLD; g_ui.card = CARD_NONE;
          render_map_stop_follow();
          render_map_focus_port(g_ui.selectedPort);
        }
      } else if (id == 1) {
        x4_nav_push();
        render_map_stop_follow();
        g_ui.scene = SCENE_WORLD; g_ui.card = CARD_NONE;
      } else if (id == 2) g_ui.card = CARD_NONE;
      break;
    case CARD_MENU:
      if (id == 0) { save_write(); g_ui.confirmReset = 0; }
      else if (id == 1) { g_ui.confirmReset = 0; open_card(CARD_JOURNAL); }
      else if (id == 2) { g_ui.confirmReset = 0; g_ui.card = CARD_NONE; }
      break;
    case CARD_INBOX:
      if (id == 0) { g_ui.attentionUnreadOnly = !g_ui.attentionUnreadOnly; g_ui.page = 0; }
      else if (id == 1) { attention_clear_read(); g_ui.page = 0; }
      else if (id == 2) g_ui.card = CARD_NONE;
      break;
    case CARD_ATLAS:
      if (id < 2) g_ui.page = (uint8_t)((g_ui.page + (id == 0 ? REGION_COUNT - 1 : 1)) % REGION_COUNT);
      else g_ui.card = CARD_NONE;
      break;
    case CARD_KEEPSAKES:
      if (id < 2) {
        int pages = (g_campaign.storyDeliveries + CARD_VISIBLE_ROWS - 1) / CARD_VISIBLE_ROWS;
        g_ui.page = (uint8_t)((g_ui.page + 1) % max(1, pages));
      }
      else g_ui.card = CARD_NONE;
      break;
    case CARD_CAPTAINS:
      if (g_ui.captainView == 0) {
        if (id == 0) {
          int pages = (g_campaign.captainCount + CARD_VISIBLE_ROWS - 1) / CARD_VISIBLE_ROWS;
          g_ui.page = (uint8_t)(pages > 1 ? (g_ui.page + 1) % pages : 0);
        }
        else if (id == 1) {
          if(g_campaign.economyVersion>=2){open_card(CARD_PORT_PICKER);g_ui.captainView=1;}
          else
          {g_ui.captainView=2;g_ui.selectedCaptain=0;}
        }
        else if (id == 2) g_ui.card = CARD_NONE;
      } else if (g_ui.captainView == 1) {
        if (id == 0) {
          if(g_campaign.economyVersion>=2){open_card(CARD_PORT_SHIPS);g_ui.captainView=4;}
          else
          apply_result(sim_assign_captain(g_ui.selectedCaptain,g_ui.selectedShip),true);
        }
        else if (id == 1) { g_ui.captainView = 0; g_ui.page = 0; }
        else if (id == 2) g_ui.card = CARD_NONE;
      } else {
        if (id == 0) {
          SimResult hired = sim_hire_captain(g_ui.selectedCaptain);
          apply_result(hired, hired.code == SIM_OK);
          if (hired) {
            x4_nav_drop_parent(CARD_PORT_PICKER);
            g_ui.selectedCaptain = (uint8_t)hired.value; g_ui.captainView = 1;
          }
        } else if (id == 1) { g_ui.captainView = 0; g_ui.page = 0; }
        else if (id == 2) g_ui.card = CARD_NONE;
      }
      break;
    case CARD_JOURNAL:
      if (id == 0 && (g_ui.page + 1) * X4_JOURNAL_VISIBLE_ROWS < g_journalCount) ++g_ui.page;
      else if (id == 1 && g_ui.page) --g_ui.page;
      else if (id == 2) g_ui.card = CARD_NONE;
      break;
    case CARD_TUTORIAL:
      if(g_campaign.economyVersion>=2) {
        if(id==2) {if(g_ui.page)--g_ui.page;}
        else if(id==0 && g_ui.page+1<MERCHANT_TUTORIAL_PAGES)++g_ui.page;
        else if(id==0 || id==1) {
          uint8_t before=g_campaign.tutorialDismissed;
          g_campaign.tutorialDismissed=1;
          if(!save_write()) {g_campaign.tutorialDismissed=before;apply_result(SIM_SAVE_FAILED);break;}
          if(!x4_nav_back()){g_ui.card=CARD_NONE;g_ui.page=0;g_ui.scene=SCENE_FLEET;}
        }
        break;
      }
      if (id == 0) {
        g_campaign.tutorialStep = 0; g_campaign.tutorialFlags = 0;
        g_campaign.tutorialDismissed = 0; save_write();
      }
      else if (id == 1) {
        g_campaign.tutorialDismissed = 1; g_ui.card = CARD_NONE; save_write();
      }
      else if (id == 2) g_ui.card = CARD_NONE;
      break;
    default: break;
  }
  g_ui.dirty = 1;
}

static void handle_card_row(int row, int x) {
  if(g_ui.card==CARD_PORT_PICKER){
    uint8_t port=x4_yard_port(row);if(port>=MAX_PORTS)return;
    bool recruit=g_ui.captainView==1;
    x4_nav_push();navOpeningSubject=true;g_ui.selectedPort=port;
    open_card(recruit?CARD_CAPTAINS:CARD_SHIPYARD);
    if(recruit){g_ui.captainView=2;g_ui.selectedCaptain=0;}
    navOpeningSubject=false;return;
  }
  if(g_ui.card==CARD_PORT_SHIPS){
    uint8_t ship=x4_port_ship(row);if(ship>=g_campaign.shipCount)return;
    uint8_t purpose=g_ui.captainView;
    if(purpose>=2) {
      g_ui.selectedShip=ship;g_ui.selectedPort=g_ships[ship].currentPort;
      if(purpose==2)open_card(CARD_MANIFEST);
      else if(purpose==3)x4_begin_refit(MODULE_LONG_CASKS);
      else if(apply_result(sim_assign_captain(g_ui.selectedCaptain,ship)))x4_nav_back();
    } else x4_open_ship(ship);
    return;
  }
  if(g_ui.card==CARD_PORT_PRICES)return;
  if (row < 0 || row >= voyage_card_row_count(g_ui.card, g_ui.page)) return;
  switch (g_ui.card) {
    case CARD_SHIP: x4_merchant_ship_row(row); break;
    case CARD_TRADE: case CARD_PACKAGES: x4_merchant_row(row); break;
    case CARD_VOYAGE: x4_voyage_row(row); break;
    case CARD_VOYAGE_LOAD: {
      int good = x4_load_good(row);
      if (good >= 0) { g_ui.selectedGood = (uint8_t)good; x4_voyage_quantity(1); }
      break;
    }
    case CARD_VOYAGE_DEPARTURE: x4_departure_row(row); break;
    case CARD_JOBS: {
      int si = sim_selected_docked_ship();
      int slot = g_ui.page * CARD_VISIBLE_ROWS + row;
      if (si >= 0 && slot < MAX_CONTRACTS_PER_PORT &&
          apply_result(sim_load_contract((uint8_t)si, g_ui.selectedPort, (uint8_t)slot))) {
        g_ui.selectedShip = (uint8_t)si;
        sim_tutorial_complete(TUTORIAL_CONTRACT);
        sim_tutorial_complete(TUTORIAL_CARGO);
        save_write();
      }
      break;
    }
    case CARD_MANIFEST:
      if (!unlocked(FEATURE_WAREHOUSE)) break;
      if (g_ui.selectedShip < g_campaign.shipCount) {
        x4_manifest_clamp();
        row += g_ui.routeScroll;
        SimResult changed = g_ui.page ?
          sim_transfer_from_warehouse(g_ui.selectedShip, (uint8_t)row) :
          sim_transfer_to_warehouse(g_ui.selectedShip, (uint8_t)row);
        apply_result(changed, true);
        x4_manifest_clamp();
      }
      break;
    case CARD_ROUTE: {
      if (g_ui.page == 2) {
        break;
      } else if (g_ui.page == 1) {
        Ship& s = g_ships[g_ui.selectedShip];
        RouteTemplate& t = s.routeTemplate;
        if (row == 0) apply_result(sim_route_template_save(g_ui.selectedShip), true);
        else if (row == 1) t.loadMode = t.loadMode ? ROUTE_LOAD_NONE : ROUTE_LOAD_ENDPOINT;
        else if (row == 2) t.sellMode = t.sellMode ? ROUTE_SELL_NONE : ROUTE_SELL_PROFIT;
        else if (row == 3) {
          t.buyGood = t.buyGood >= GOOD_COUNT ? 0 :
            (t.buyGood + 1 >= GOOD_COUNT ? NO_PORT : (uint8_t)(t.buyGood + 1));
          if (t.buyGood < GOOD_COUNT)
            t.maxBuyPrice = content_market_price(s.currentPort, t.buyGood, true);
        }
        else if (row == 4 && t.buyGood < GOOD_COUNT) {
          if (x < CARD_X + CARD_W / 2)
            t.maxBuyPrice = t.maxBuyPrice >= 10 ? (uint16_t)(t.maxBuyPrice - 10) : 0;
          else if (t.maxBuyPrice <= 65525) t.maxBuyPrice = (uint16_t)(t.maxBuyPrice + 10);
        }
        g_routePreparation.valid = 0;
        g_routePreparation.computed = 0;
      } else if (row == 5 && !guided()
      ) {
        if (unlocked(FEATURE_ROUTE_RULES)) g_ui.page = 1;
      } else if (row < ROUTE_VISIBLE_ROWS) {
        int port = input_route_candidate(row);
        if (port >= 0 && apply_result(sim_route_add((uint8_t)port))) {
          sim_tutorial_complete(TUTORIAL_ROUTE);
          save_write();
        }
      }
      break;
    }
    case CARD_ATLAS: {
      const int port = g_ui.page * 4 + row;
      if (row < 4 && sim_atlas_charted((uint8_t)port)) x4_story_moment_open(X4_ATLAS_PAGE, (uint8_t)port);
      break;
    }
    case CARD_KEEPSAKES: {
      int entry = g_ui.page * CARD_VISIBLE_ROWS + row;
      if (entry < g_campaign.storyDeliveries) x4_story_moment_open(X4_STORY_REREAD, g_campaign.storyOrder[entry]);
      break;
    }
    case CARD_MORE:
      {
        int item = voyage_detail_index(g_ui.page * CARD_VISIBLE_ROWS + row);
        if(item>=0) {
          if(g_campaign.economyVersion>=2 && VOYAGE_DETAIL_CARDS[item]==CARD_MANIFEST){open_card(CARD_PORT_SHIPS);g_ui.captainView=2;}
          else if(g_campaign.economyVersion>=2 && VOYAGE_DETAIL_CARDS[item]==CARD_SHIPYARD){open_card(CARD_PORT_PICKER);g_ui.captainView=0;}
          else open_card(VOYAGE_DETAIL_CARDS[item]);
        }
      }
      break;
    case CARD_MARKET: {
      int good = row;
      if (good < GOOD_COUNT) g_ui.selectedGood = (uint8_t)good;
      break;
    }
    case CARD_FLEET: {
      int ship = g_ui.page * CARD_VISIBLE_ROWS + row;
      if (ship < g_campaign.shipCount) {
        g_ui.selectedShip = (uint8_t)ship;
        const Ship& s = g_ships[ship];
        g_ui.selectedPort = s.state == SHIP_SAILING || s.state == SHIP_AWAITING_DECISION ? s.toPort : s.currentPort;
        if (s.state == SHIP_DISABLED) {
          open_card(CARD_SHIPYARD);
          g_ui.page = 2;
        } else if (s.state == SHIP_AWAITING_DECISION) {
          open_card(g_incident.active && g_incident.shipIndex == ship ? CARD_INCIDENT : CARD_INBOX);
        } else {
          g_ui.scene = s.state == SHIP_SAILING ? SCENE_WORLD : SCENE_PORT;
          g_ui.card = CARD_NONE;
          if (s.state == SHIP_SAILING) render_map_follow_selected_ship();
        }
      }
      break;
    }
    case CARD_SHIPYARD: {
      SimResult changed = SIM_INVALID;
      int mode = g_ui.page % 3;
      if (mode == 0 && row < SHIP_CLASS_COUNT) changed = sim_buy_ship((uint8_t)row);
      else if (mode == 1 && row < MODULE_SHOP_COUNT) {
        if (!unlocked(FEATURE_FITTINGS)) break;
        if (g_campaign.economyVersion) { x4_begin_refit((uint8_t)row); break; }
        changed = sim_buy_module((uint8_t)row);
      }
      else if (mode == 2 && row < 3) { if (!unlocked(FEATURE_PORT_WORKS)) break; changed = sim_buy_port_upgrade((uint8_t)row); }
      else if (mode == 2 && row == 3) changed = sim_repair_ship(g_ui.selectedShip);
      else if (mode == 2 && row == 4) {
        if (g_campaign.economyVersion) { x4_open_relief(); break; }
        changed = sim_claim_recovery();
      }
      else if (mode == 2 && row == 5 && g_campaign.won) changed = sim_buy_module(MODULE_CHARTER_PENNANT);
      bool purchasedShip = mode == 0 && changed.code == SIM_OK;
      apply_result(changed, true);
      if (purchasedShip) {
        if (g_campaign.economyVersion) { g_ui.card = CARD_NONE; g_ui.scene = SCENE_COMPANY; }
        else
          open_card(CARD_CAPTAINS);
      }
      break;
    }
    case CARD_CHARTER:
      if (row < REGION_COUNT) {
        SimResult changed = (g_campaign.chartMask & (1u << row)) ?
          sim_purchase_charter((uint8_t)row) : sim_purchase_chart((uint8_t)row);
        apply_result(changed, true);
      }
      break;
    case CARD_MENU:
      if (row == 0) { g_campaign.paused = 0; g_ui.confirmReset = 0; g_ui.card = CARD_NONE; }
      else if (row == 1) { g_campaign.paused = !g_campaign.paused; g_ui.confirmReset = 0; }
      else if (row == 2) { g_campaign.timeScale = next_time_scale(g_campaign.timeScale); save_write(); g_ui.confirmReset = 0; }
      else if (row == 3) open_card(CARD_PROGRESS);  // no speaker: the row shows company progress
      else if (row == 4) open_card(CARD_HELP);
      else if (row == 5) {
        if (g_ui.confirmReset) {
          save_reset();
          sim_new_campaign(rng_next());
        } else g_ui.confirmReset = 1;
      }
      break;
    case CARD_INBOX: {
      int index = input_attention_index(row);
      if (index >= 0) {
        UiState noticeParent=g_ui;
        attention_select((uint8_t)index);
        if(g_campaign.economyVersion>=2) {
          AttentionEntry& notice=g_attention[index];
          bool shipNotice=notice.kind==ATTENTION_ARRIVAL || notice.kind==ATTENTION_DISABLED;
          if(g_ui.card!=CARD_INBOX || (shipNotice && notice.shipIndex<g_campaign.shipCount)) {
            // attention_select marks the notice read and resolves its subject;
            // capture its source before adopting that destination.
            uint8_t target=g_ui.card,port=g_ui.selectedPort,ship=g_ui.selectedShip,page=g_ui.page;
            g_ui.card=noticeParent.card;g_ui.scene=noticeParent.scene;g_ui.selectedPort=noticeParent.selectedPort;
            g_ui.selectedShip=noticeParent.selectedShip;g_ui.page=noticeParent.page;x4_nav_push();
            g_ui.selectedPort=port;g_ui.selectedShip=ship;g_ui.card=target;g_ui.page=page;
            if(shipNotice && ship<g_campaign.shipCount) {
              navOpeningSubject=true;x4_open_ship(ship);navOpeningSubject=false;
            }
          }
        }
        // Reading a row under the unread filter can empty the current page.
        int pages = (attention_visible_count() + CARD_VISIBLE_ROWS - 1) / CARD_VISIBLE_ROWS;
        if (g_ui.card == CARD_INBOX && g_ui.page >= pages) g_ui.page = (uint8_t)(pages > 0 ? pages - 1 : 0);
        // A disabled ship opens the fleet page that lists it.
        if (g_ui.card == CARD_FLEET && g_ui.selectedShip < g_campaign.shipCount)
          g_ui.page = (uint8_t)(g_ui.selectedShip / CARD_VISIBLE_ROWS);
        save_write();
      }
      break;
    }
    case CARD_RETURN_DETAILS: x4_return_row(row); break;
    case CARD_CAPTAINS: {
      if (g_ui.captainView == 0) {
        int captain = g_ui.page * CARD_VISIBLE_ROWS + row;
        if (captain < MAX_CAPTAINS && g_captains[captain].active) {
          g_ui.selectedCaptain = (uint8_t)captain;
          g_ui.captainView = 1;
        }
      } else if (g_ui.captainView == 2) {
        uint8_t candidate = (uint8_t)(row / 3);
        CaptainProfile preview{};
        if (sim_recruit_candidate(g_ui.selectedPort, candidate, &preview))
          g_ui.selectedCaptain = candidate;
      }
      break;
    }
    default: break;
  }
  g_ui.dirty = 1;
}

static void handle_incident_choice(uint8_t option) {
  if (g_incident.active && g_incident.shipIndex < g_campaign.shipCount &&
      g_ships[g_incident.shipIndex].merchant.state == MERCHANT_SAILING) {
    if (!apply_result(sim_merchant_incident_resolve(option))) return;
  } else { sim_resolve_incident(option); save_write(); }
  open_card(CARD_OUTCOME);
}

static void handle_card_touch(int x, int y) {
  if (g_ui.card == CARD_INCIDENT) {
    for (int option = 0; option < (g_campaign.economyVersion>=2?encounter_choices(g_incident.kind):3); ++option) {
      HitRegion r = incident_option_region(option);
      if (hit_test(&r, 1, x, y) >= 0) { handle_incident_choice((uint8_t)option); break; }
    }
    return;
  }
  for (int id = 0; id < 3; ++id) {
    if (!(x4_footer_mask() & (1u << id))) continue;
    HitRegion r = voyage_footer_region(g_ui.card, id);
    if (hit_test(&r, 1, x, y) >= 0) { handle_footer(id); return; }
  }
  if (x4_merchant_touch(x, y)) return;
  if (g_ui.card == CARD_VOYAGE_LOAD) {
    for (int row = 0; row < 2; ++row) {
      int good = x4_load_good(row);
      HitRegion region = voyage_card_row_region(CARD_VOYAGE_LOAD, g_ui.page, row);
      if (good < 0 || hit_test(&region, 1, x, y) < 0) continue;
      g_ui.selectedGood = (uint8_t)good;
      x4_focus_load_row(row);
      for (int dir : {-1, 1}) {
        HitRegion arrow = voyage_load_adjust_region(row, dir);
        if (hit_test(&arrow, 1, x, y) >= 0) { x4_voyage_quantity(dir); break; }
      }
      g_ui.dirty = 1;
      return;
    }
  }
  int row = -1;
  for (int i = 0; i < voyage_card_row_count(g_ui.card, g_ui.page); ++i) {
    HitRegion r = voyage_card_row_region(g_ui.card, g_ui.page, i, g_ui.captainView);
    if (hit_test(&r, 1, x, y) >= 0) { row = i; break; }
  }
  if (row >= 0) handle_card_row(row, x);
}


static void handle_touch(int x, int y);
#if defined(SH_HOST_TESTS)
void input_test_touch(int x, int y) { handle_touch(x, y); }
#endif

static void handle_touch(int x, int y) {
  render_note_touch(x, y);
  mapTouchActive = false;
  if (x4_goal_moment_active()) { x4_goal_moment_close(); return; }
  if (x4_story_moment_active()) { x4_story_moment_close(); return; }
  if (x4_onboarding_screen()) return;  // the captain screens are keys only
  if (x4_touch_return_gate()) return;
  if (g_ui.title) {
    g_ui.title = 0;
    if (g_ui.scene == SCENE_WORLD) render_map_follow_selected_ship();
    if(g_campaign.economyVersion>=2 && !g_campaign.tutorialDismissed && !g_campaign.completedVoyages && !g_campaign.deliveredUnits &&
       !sim_onboarding_active()) {
      open_card(CARD_TUTORIAL);g_ui.page=0;
    }
    if (sim_tutorial_complete(TUTORIAL_PORT)) save_write();
    g_ui.dirty = 1;
    return;
  }
  // X4 transaction Back is above the main tabs. Hidden legacy chrome has no
  // hit targets: only card actions or the visible main controls may respond.
  if (g_ui.card != CARD_NONE) { handle_card_touch(x, y); return; }
  if (y < TOPBAR_H) return;
  int slot = x4_slot_at(x, y);
  if (slot >= 0) { x4_run_slot(slot); return; }
  bool onChart = !x4_watch_active() && x >= MAP_X && x < MAP_X + MAP_DRAW_W && y >= MAP_Y && y < MAP_Y + MAP_DRAW_H;
  if (g_ui.scene == SCENE_WORLD && onChart) {
    if (render_map_zoom_button_hit(x, y)) {
      if (unlocked(FEATURE_ZOOM)) render_map_toggle_zoom(x, y);
      return;
    }
    mapTouchActive = true;
    mapTouchDragged = false;
    mapTouchStartX = mapTouchLastX = x;
    mapTouchStartY = mapTouchLastY = y;
    mapTouchStartFocusX = g_ui.mapFocusX;
    mapTouchStartFocusY = g_ui.mapFocusY;
  }
}

static void handle_map_touch_move(int x, int y) {
  mapTouchLastX = x;
  mapTouchLastY = y;
  int dx = x - mapTouchStartX;
  int dy = y - mapTouchStartY;
  if (dx * dx + dy * dy < 144) return;
  mapTouchDragged = true;
  render_map_pan_by(mapTouchStartX, mapTouchStartY, x, y,
                    mapTouchStartFocusX, mapTouchStartFocusY);
}

static void handle_map_touch_release() {
  if (!mapTouchActive) return;
  if (!mapTouchDragged) {
    int ship = render_map_ship_at(mapTouchLastX, mapTouchLastY);
    if (ship >= 0) {
      g_ui.selectedShip = (uint8_t)ship;
      const Ship& selected = g_ships[ship];
      g_ui.selectedPort = selected.state == SHIP_SAILING ||
                          selected.state == SHIP_AWAITING_DECISION ?
                          selected.toPort : selected.currentPort;
      x4_watch_ship();
      g_ui.dirty = 1;
    } else {
      int port = render_map_port_at(mapTouchLastX, mapTouchLastY);
      if (port >= 0) {
        render_map_stop_follow();
        g_ui.selectedPort = (uint8_t)port;
        if(g_campaign.economyVersion<2)select_ship_at_port((uint8_t)port);
        g_ui.dirty = 1;
      }
    }
  }
  mapTouchActive = false;
}

// Xteink X4 button navigation. The touch handlers above hold the game's
// behaviour; this layer keeps a focus cursor over the same hit geometry and
// feeds them synthetic taps. X4_ACTION_OF_BUTTON (x4.h) maps buttons to actions:
// UP/DOWN move the cursor, OK activates, BACK closes a card (at the top level it
// opens the ship's bell menu), the rocker flips ports and pages, POWER saves and
// powers off. Presses never auto-repeat: the sampler captures edges while
// e-paper rendering blocks this loop, and actions run one per visible frame.
#include "x4.h"
#if defined(LGFX_SDL)
void board_x4_host_wake();
#endif

// A preparation is deliberately transient. Merely changing the destination,
// quantity or cargo type cannot reserve stock, debit coins or load the ship.
static VoyageQuote voyageDraft{};
static uint8_t voyageDestination = NO_PORT;
static uint8_t voyageQuantities[GOOD_COUNT]{};
static UiState voyageOriginUi{}, watchOriginUi{};
static bool watchActive = false;
static uint8_t fleetPage = 0;
static uint32_t voyageCompanyEpoch = 0;
bool x4_watch_active() { return watchActive; }

static void x4_fleet_page(int dir) {
  int pages = max(1, (g_campaign.shipCount + VOYAGE_FLEET_ROWS - 1) / VOYAGE_FLEET_ROWS);
  fleetPage = (uint8_t)((g_ui.page + dir + pages) % pages);
  g_ui.page = fleetPage;
  g_ui.dirty = 1;
}
uint8_t x4_fleet_ship(int row) {
  int index = (int)g_ui.page * VOYAGE_FLEET_ROWS + row;
  return row >= 0 && row < VOYAGE_FLEET_ROWS && index < g_campaign.shipCount ? (uint8_t)index : NO_SHIP;
}
uint8_t x4_load_good_count() {
  uint8_t count = 0;
  for (uint8_t good = 0; good < GOOD_COUNT; ++good)
    if ((voyageDraft.offeredGoods & (1u << good)) || voyageQuantities[good]) ++count;
  return count;
}
int x4_load_good(int row) {
  int index = row + (int)g_ui.page * 2;
  for (uint8_t good = 0; good < GOOD_COUNT; ++good)
    if ((voyageDraft.offeredGoods & (1u << good)) || voyageQuantities[good])
      if (index-- == 0) return good;
  return -1;
}
uint8_t x4_load_selected_good() { return g_ui.selectedGood; }

static void x4_watch_ship() {
  if (g_ui.selectedShip >= g_campaign.shipCount) return;
  if (watchActive && g_ui.scene == SCENE_WORLD && g_ui.card == CARD_NONE) {
    render_map_follow_selected_ship(); return;
  }
  x4_nav_push();
  watchOriginUi = g_ui;
  if (g_ui.card == CARD_VOYAGE_DEPARTURE) { watchOriginUi.card = CARD_NONE; watchOriginUi.scene = SCENE_FLEET; watchOriginUi.page = fleetPage; }
  watchActive = true;
  g_ui.card = CARD_NONE; g_ui.scene = SCENE_WORLD;
  render_map_follow_selected_ship(); g_ui.dirty = 1;
}
static void x4_watch_back() {
  if(g_campaign.economyVersion>=2){if(!x4_nav_back())x4_run_command(X4_CMD_FLEET);return;}
  if (!watchActive) { open_card(CARD_MENU); return; }
  render_map_stop_follow();
  g_ui = watchOriginUi; g_ui.dirty = 1;
  watchActive = false;
}
static uint8_t refitModule = MODULE_LONG_CASKS, refitSlot = 0;
static bool refitFromGoal = false;
uint8_t x4_refit_module() { return refitModule; }
uint8_t x4_refit_slot() { return refitSlot; }

static void x4_begin_refit(uint8_t module) {
  if (!selected_ship_can_route()) { apply_result(SIM_BAD_SHIP_STATE); return; }
  refitFromGoal = g_ui.card == CARD_PROGRESS || (g_ui.card==CARD_PORT_SHIPS && g_ui.captainView==3);
  refitModule = module; refitSlot = 0;
  open_card(CARD_VOYAGE); g_ui.page = 5;
}

static void x4_open_relief() {
  VoyageQuote quote = sim_recovery_quote();
  if (quote.code != SIM_OK) { apply_result(quote.code); return; }
  open_card(CARD_VOYAGE); voyageDraft = quote; g_ui.page = 3;
}

const VoyageQuote& x4_voyage_quote() { return voyageDraft; }
uint8_t x4_voyage_destination_count() {
  uint8_t ports[MAX_PORTS];
  return g_campaign.economyVersion >= 2 ? sim_merchant_destinations(g_ui.selectedShip, ports, MAX_PORTS) : sim_voyage_destinations(g_ui.selectedShip, ports, MAX_PORTS);
}
int x4_voyage_destination(int row) {
  uint8_t ports[MAX_PORTS];
  uint8_t count = g_campaign.economyVersion >= 2 ? sim_merchant_destinations(g_ui.selectedShip, ports, MAX_PORTS) : sim_voyage_destinations(g_ui.selectedShip, ports, MAX_PORTS);
  int index = row + g_ui.routeScroll;
  return index >= 0 && index < count ? ports[index] : -1;
}

uint8_t x4_company_ship(int row) {
  int found = 0;
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < g_campaign.shipCount; ++i) {
      bool ready = g_ships[i].state == SHIP_DOCKED || g_ships[i].state == SHIP_ARRIVED;
      if ((pass == 0) != ready) continue;
      if (found++ == row) return (uint8_t)i;
    }
  }
  return NO_SHIP;
}

static void x4_open_voyage(bool planning) {
  if (g_ui.selectedShip >= g_campaign.shipCount) { apply_result(SIM_BAD_SHIP_STATE); return; }
  Ship& ship = g_ships[g_ui.selectedShip];
  if (ship.state == SHIP_SAILING && (g_campaign.economyVersion >= 2 || !planning)) { x4_watch_ship(); return; }
  g_ui.selectedPort = ship.state == SHIP_SAILING ? ship.toPort : ship.currentPort;
  if (g_campaign.economyVersion < 2 && sim_recovery_available()) { x4_open_relief(); return; }
  if (g_campaign.economyVersion >= 2 && (ship.state == SHIP_DOCKED || ship.state == SHIP_ARRIVED)) { x4_merchant_ship(); return; }
  if (ship.state == SHIP_AWAITING_DECISION) {
    open_card(sim_select_incident(g_ui.selectedShip) ? CARD_INCIDENT : CARD_INBOX); return;
  }
  if (ship.state == SHIP_DISABLED) { open_card(CARD_SHIPYARD); g_ui.page = 2; return; }
  voyageOriginUi = g_ui;
  if (g_ui.scene == SCENE_FLEET) fleetPage = g_ui.page;
  if (ship.cargoCount && ship.state != SHIP_SAILING) { open_card(CARD_ROUTE); return; }
  open_card(CARD_VOYAGE);
  voyageDraft = {}; voyageDestination = NO_PORT;
  memset(voyageQuantities, 0, sizeof(voyageQuantities));
  voyageCompanyEpoch = sim_return_epoch();
}
static void x4_open_ship(uint8_t index) {
  if (index >= g_campaign.shipCount) return;
  x4_nav_push();navOpeningSubject=true;
  g_ui.selectedShip = index;
  const Ship& ship = g_ships[index];
  g_ui.selectedPort = ship.state == SHIP_SAILING || ship.state == SHIP_AWAITING_DECISION ? ship.toPort : ship.currentPort;
  x4_open_voyage();navOpeningSubject=false;
}

static VoyageQuote x4_quote(uint8_t destination, uint8_t mode, uint8_t quantity = 0) {
  bool planning = g_ui.selectedShip < g_campaign.shipCount &&
    g_ships[g_ui.selectedShip].booking.state == BOOKING_SAILING;
  return planning ? sim_return_voyage_quote(g_ui.selectedShip, destination, mode, NO_PORT, quantity) :
    sim_voyage_offer(g_ui.selectedShip, destination, mode, NO_PORT, quantity);
}

static void x4_refresh_manifest() {
  bool planning = g_ui.selectedShip < g_campaign.shipCount &&
    g_ships[g_ui.selectedShip].booking.state == BOOKING_SAILING;
  voyageDraft = planning ? sim_return_voyage_manifest_offer(g_ui.selectedShip, voyageDestination, VOYAGE_TRADE, voyageQuantities) :
    sim_voyage_manifest_offer(g_ui.selectedShip, voyageDestination, VOYAGE_TRADE, voyageQuantities);
}
static void x4_open_load(bool prefill) {
  if (prefill) {
    VoyageQuote suggested = x4_quote(voyageDestination, VOYAGE_TRADE);
    memcpy(voyageQuantities, suggested.quantities, sizeof(voyageQuantities));
    g_ui.selectedGood = suggested.good;
  }
  x4_refresh_manifest();
  g_ui.card = CARD_VOYAGE_LOAD; g_ui.page = 0;
  int good = x4_load_good(0);
  if (good >= 0 && g_ui.selectedGood >= GOOD_COUNT) g_ui.selectedGood = (uint8_t)good;
  g_ui.dirty = 1;
}
static void x4_voyage_review(uint8_t mode, uint8_t quantity = 0) {
  // Repeat and legacy callers keep their exact chosen manifest when available.
  if (mode == VOYAGE_TRADE) x4_refresh_manifest();
  else voyageDraft = x4_quote(voyageDestination, mode, quantity);
  g_ui.card = CARD_VOYAGE; g_ui.page = 2; g_ui.dirty = 1;
}
static void x4_voyage_row(int row) {
  if (g_campaign.economyVersion >= 2 && g_ui.page == 0) { x4_merchant_voyage_row(row); return; }
  if (g_ui.page == 5 && row < 2) { refitSlot = (uint8_t)row; g_ui.page = 4; g_ui.dirty = 1; return; }
  if (g_ui.page == 0) {
    int destination = x4_voyage_destination(row);
    if (destination < 0) return;
    voyageDestination = (uint8_t)destination;
    x4_open_load(true);
  } else if (g_ui.page == 1 && row < 2) x4_voyage_review(row == 0 ? VOYAGE_FREIGHT : VOYAGE_TRADE);
}
static void x4_voyage_quantity(int dir) {
  if (g_ui.card != CARD_VOYAGE_LOAD || g_ui.selectedGood >= GOOD_COUNT) return;
  uint8_t good = g_ui.selectedGood;
  x4_refresh_manifest();
  int quantity = voyageQuantities[good];
  if (dir < 0 && quantity) --voyageQuantities[good];
  else if (dir > 0 && voyageDraft.quantity < voyageDraft.capacity && quantity < voyageDraft.availableQuantities[good])
    ++voyageQuantities[good];
  x4_refresh_manifest(); g_ui.dirty = 1;
}
static void x4_load_footer(int id) {
  if (id == 2) { g_ui.card = CARD_VOYAGE; g_ui.page = 0; g_ui.dirty = 1; return; }
  x4_refresh_manifest();
  if (id == 0 && g_ui.selectedGood < GOOD_COUNT) {
    uint8_t good = g_ui.selectedGood;
    int room = max(0, (int)voyageDraft.capacity - (int)voyageDraft.quantity);
    int supply = max(0, (int)voyageDraft.availableQuantities[good] - (int)voyageQuantities[good]);
    voyageQuantities[good] += (uint8_t)min(room, supply);
    x4_refresh_manifest();
  } else if (id == 1) {
    if (voyageDraft.code != SIM_OK || !voyageDraft.quantity) apply_result(voyageDraft.code);
    else { g_ui.card = CARD_VOYAGE; g_ui.page = 2; }
  }
  g_ui.dirty = 1;
}
uint8_t x4_next_ready_ship() {
  uint8_t selected = g_ui.selectedShip;
  for (int i = 1; i <= g_campaign.shipCount; ++i) {
    uint8_t index = (uint8_t)((selected + i) % g_campaign.shipCount);
    if (index == selected) continue;
    const Ship& ship = g_ships[index];
    if (ship.state == SHIP_DOCKED || ship.state == SHIP_ARRIVED) return index;
  }
  return NO_SHIP;
}
static void x4_departure_row(int row) {
  if (row == 1) { x4_watch_ship(); return; }
  if (row != 0) return;
  uint8_t index = x4_next_ready_ship();
  if (index != NO_SHIP) {
    g_ui.card = CARD_NONE; g_ui.scene = SCENE_FLEET;
    g_ui.page = fleetPage = index / VOYAGE_FLEET_ROWS;
    x4_open_ship(index); return;
  }
  x4_run_command(X4_CMD_FLEET);
}

static void x4_voyage_footer(int id) {
  if (g_campaign.economyVersion >= 2 && g_ui.page <= 2) { x4_merchant_voyage_footer(id); return; }
  if (g_ui.page == 5) {
    if (id == 2) {
      open_card(refitFromGoal ? CARD_PROGRESS : CARD_SHIPYARD);
      if (!refitFromGoal) g_ui.page = 1;
    }
    return;
  }
  if (g_ui.page == 4) {
    if (id == 2) g_ui.page = 5;
    else if (id == 1 && apply_result(sim_replace_module(g_ui.selectedShip, refitSlot, refitModule))) {
      if (g_campaign.economyVersion >= 2) {
        open_card(refitFromGoal ? CARD_PROGRESS : CARD_SHIPYARD);
        if (!refitFromGoal) g_ui.page = 1;
      } else { g_ui.card = CARD_NONE; g_ui.scene = SCENE_COMPANY; }
    }
    g_ui.dirty = 1; return;
  }
  if (g_ui.page == 3) {
    if (id == 2) g_ui.card = CARD_NONE;
    else if (id == 1) {
      if (apply_result(sim_claim_recovery(voyageDraft))) {
        g_ui.card = CARD_NONE; g_ui.scene = SCENE_WORLD; render_map_follow_selected_ship();
      } else { voyageDraft = sim_recovery_quote(); board_x4_discard_button_events(); }
    }
    g_ui.dirty = 1; return;
  }
  if (id == 2) {
    if (g_ui.page == 2) x4_open_load(false);
    else if (g_ui.page) g_ui.page = 0;
    else {
      g_ui = voyageOriginUi; g_ui.dirty = 1;
      if (g_ui.scene == SCENE_WORLD) render_map_stop_follow();
    }
  } else if (g_ui.page == 0) {
    int pages = max(1, (x4_voyage_destination_count() + VOYAGE_DESTINATION_ROWS - 1) / VOYAGE_DESTINATION_ROWS);
    int page = (g_ui.routeScroll / VOYAGE_DESTINATION_ROWS + (id == 0 ? pages - 1 : 1)) % pages;
    g_ui.routeScroll = (uint8_t)(page * VOYAGE_DESTINATION_ROWS);
    voyageDestination = NO_PORT;
  } else if (g_ui.page == 2 && id == 0) x4_open_load(false);
  else if (g_ui.page == 2 && id == 1) {
    if (voyageCompanyEpoch != sim_return_epoch()) { apply_result(SIM_BAD_SHIP_STATE); x4_run_command(X4_CMD_FLEET); return; }
    if (voyageDraft.planningOnly) {
      if (g_ui.selectedShip < g_campaign.shipCount && g_ships[g_ui.selectedShip].booking.state == BOOKING_SAILING) {
        g_ui = voyageOriginUi; g_ui.card = CARD_NONE; g_ui.scene = SCENE_WORLD;
        render_map_follow_selected_ship(); g_ui.dirty = 1;
      } else {
        // Arrival never makes a prospective quote actionable on the same OK.
        // Recheck the unchanged manifest and require a second explicit Sail.
        x4_refresh_manifest(); g_ui.dirty = 1;
      }
      return;
    }
    SimResult result = sim_voyage_dispatch(g_ui.selectedShip, voyageDraft);
    if (apply_result(result)) {
      g_ui.card = CARD_VOYAGE_DEPARTURE; g_ui.page = 0;
    } else {
      // Preserve the chosen quantities; a refusal must never substitute cargo.
      x4_open_load(false);
      board_x4_discard_button_events();
    }
  }
  g_ui.dirty = 1;
}

enum X4Zone : uint8_t { X4_ZONE_SCENE, X4_ZONE_CARD, X4_ZONE_INCIDENT };
struct X4CardItem { uint8_t footer, id; };  // footer 0: row id, footer 1: footer button id
static constexpr int X4_CARD_MAX = X4_CARD_ROW_MAX + 6;

static uint8_t x4Zone = X4_ZONE_SCENE;
static int8_t x4Item = 0;
// Identity of the focused control, so the cursor follows the same thing when
// the list under it changes (a loaded contract vanishes, a stage unlock adds
// a column button) instead of silently naming whatever moved into its index.
static uint8_t x4KeyFooter = 0, x4KeyId = 0, x4KeyCmd = 0xFF;
static uint8_t x4SeenTitle = 0xFF, x4SeenScene = 0xFF, x4SeenCard = 0xFF;
static uint8_t x4SeenPage = 0xFF, x4SeenView = 0xFF, x4SeenStep = 0xFF;
static uint8_t x4SeenGood = 0xFF;
static uint8_t x4Stable = 0, x4Candidate = 0, x4CandidateCount = 0;
static uint32_t x4LastInputMs = 0;
static uint32_t x4PressCount = 0;
static int x4LastPressed = -1;
static bool x4InputPending = false;

// Presentation is frozen while it is read. The engine owns durable counters;
// this controller owns only navigation and the interrupted, uncommitted draft.
static ReturnReport returnReport{};
#include "input_merchant_x4.h"
#include "input_navigation_x4.h"

static MerchantUiDraft returnPreviousMerchant{};
static UiState returnPreviousUi{}, returnPreviousVoyageOrigin{}, returnPreviousWatchOrigin{};
static bool returnPreviousWatchActive = false;
static uint8_t returnPreviousFleetPage = 0;
static uint32_t returnPreviousVoyageEpoch = 0;
static VoyageQuote returnPreviousVoyage{};
static RoutePreparation returnPreviousRoute{};
static uint8_t returnPreviousDestination = NO_PORT;
static uint8_t returnPreviousRefitModule = MODULE_LONG_CASKS, returnPreviousRefitSlot = 0;
static bool returnPreviousRefitFromGoal = false;
static bool returnActive = false;
static bool returnAway = false;
static uint8_t returnOrder[MAX_SHIPS]{};
static uint8_t returnPreviousZone = 0, returnPreviousFooter = 0, returnPreviousId = 0;
static uint8_t returnPreviousCommand = 0;
static int8_t returnPreviousItem = 0;

const ReturnReport* x4_return_report() {
  return returnActive && returnReport.companySeed == g_campaign.seed &&
    returnReport.companyEpoch == sim_return_epoch() ? &returnReport : nullptr;
}

int x4_return_row_count() { return x4_return_report() ? returnReport.rowCount : 0; }

int x4_return_row_ship(int visibleRow) {
  int row = (int)g_ui.page * 3 + visibleRow;
  return row >= 0 && row < x4_return_row_count() ? returnOrder[row] : -1;
}

static int return_priority(const ReturnRow& row) {
  if (row.state == SHIP_AWAITING_DECISION) return 0;
  if (row.state == SHIP_DISABLED) return 1;
  if (row.bookedArrivals || row.legacyArrivals) return 2;
  if (row.state == SHIP_ARRIVED || row.state == SHIP_DOCKED) return 3;
  return 4;
}

bool x4_return_away() { return returnAway; }

void x4_prepare_return(bool resumed, bool away, bool keepTitle) {
  // A boot creates a new input session even when there is nothing to report.
  if (resumed) x4LastInputMs = millis();
  if (g_ui.title || (returnActive && !x4_return_report())) returnActive = false;
  // Waking while reading keeps the captured prefix. Later arrivals remain
  // pending for the next update instead of moving its totals under the reader.
  if (returnActive) { g_ui.returnQuiet = 1; return; }
  ReturnReport next = sim_return_report();
  if (!next.meaningful) { g_ui.returnQuiet = 0; return; }
  if (!returnActive) {
    x4_sync();
    returnPreviousNav=navTrail;
    returnPreviousUi = g_ui;
    returnPreviousMerchant = merchantDraft;
    returnPreviousMerchantUpgradeReturn = merchantUpgradeReturn;
    returnPreviousVoyage = voyageDraft; returnPreviousDestination = voyageDestination;
    returnPreviousVoyageOrigin = voyageOriginUi; returnPreviousWatchOrigin = watchOriginUi;
    returnPreviousWatchActive = watchActive; returnPreviousFleetPage = fleetPage;
    returnPreviousVoyageEpoch = voyageCompanyEpoch;
    returnPreviousRoute = g_routePreparation;
    returnPreviousRefitModule = refitModule; returnPreviousRefitSlot = refitSlot;
    returnPreviousRefitFromGoal = refitFromGoal;
    returnPreviousUi.title = 0;  // BACK resumes play, not another title screen
    returnPreviousUi.returnQuiet = 0;
    returnPreviousZone = x4Zone; returnPreviousItem = x4Item;
    returnPreviousFooter = x4KeyFooter; returnPreviousId = x4KeyId;
    returnPreviousCommand = x4KeyCmd;
  }
  returnReport = next;
  for (int i = 0; i < returnReport.rowCount; ++i) returnOrder[i] = returnReport.rows[i].shipIndex;
  for (int i = 1; i < returnReport.rowCount; ++i) {
    uint8_t ship = returnOrder[i];
    auto priority = [&](uint8_t index) {
      for (int r = 0; r < returnReport.rowCount; ++r)
        if (returnReport.rows[r].shipIndex == index) return return_priority(returnReport.rows[r]);
      return 5;
    };
    int at = i;
    while (at > 0 && priority(ship) < priority(returnOrder[at - 1])) {
      returnOrder[at] = returnOrder[at - 1]; --at;
    }
    returnOrder[at] = ship;
  }
  returnActive = true; returnAway = away;
  // keepTitle: the report opens behind the title, and OK on the title shows it.
  if (!keepTitle) g_ui.title = 0;
  g_ui.scene = SCENE_COMPANY; g_ui.card = CARD_NONE;
  g_ui.page = 0; g_ui.returnQuiet = 1;
  if (returnReport.preferredShip < g_campaign.shipCount) {
    g_ui.selectedShip = returnReport.preferredShip;
    const Ship& ship = g_ships[g_ui.selectedShip];
    g_ui.selectedPort = ship.state == SHIP_SAILING || ship.state == SHIP_AWAITING_DECISION ?
      ship.toPort : ship.currentPort;
  }
  for (int i = 0; i < returnReport.rowCount; ++i)
    if (returnOrder[i] == g_ui.selectedShip) g_ui.page = (uint8_t)(i / 3);
  x4Zone = X4_ZONE_SCENE; x4Item = 0; x4KeyCmd = X4_CMD_SHIP_0;
  x4SeenTitle = x4SeenScene = x4SeenCard = 0xFF;
  g_ui.noticeCode = SIM_OK; g_ui.noticeUntilMs = 0; g_ui.dirty = 1;
  board_x4_discard_button_events();
}

static bool x4_finish_return(bool restore) {
  if (!x4_return_report()) return true;
  if (!apply_result(sim_ack_return_report(returnReport))) return false;
  returnActive = false;
  if(!restore)x4_nav_reset();
  if (restore) {
    navTrail=returnPreviousNav;navTrail.epoch=sim_return_epoch();
    g_ui = returnPreviousUi;
    merchantDraft = returnPreviousMerchant;
    merchantUpgradeReturn = returnPreviousMerchantUpgradeReturn;
    voyageDraft = returnPreviousVoyage; voyageDestination = returnPreviousDestination;
    memcpy(voyageQuantities, voyageDraft.quantities, sizeof(voyageQuantities));
    voyageOriginUi = returnPreviousVoyageOrigin; watchOriginUi = returnPreviousWatchOrigin;
    watchActive = returnPreviousWatchActive; fleetPage = returnPreviousFleetPage;
    voyageCompanyEpoch = returnPreviousVoyageEpoch;
    g_routePreparation = returnPreviousRoute;
    refitModule = returnPreviousRefitModule; refitSlot = returnPreviousRefitSlot;
    refitFromGoal = returnPreviousRefitFromGoal;
    x4Zone = returnPreviousZone; x4Item = returnPreviousItem;
    x4KeyFooter = returnPreviousFooter; x4KeyId = returnPreviousId;
    x4KeyCmd = returnPreviousCommand;
    // Preserve the exact reviewed control rather than resetting to page 0.
    x4SeenTitle = g_ui.title; x4SeenScene = g_ui.scene; x4SeenCard = g_ui.card;
    x4SeenPage = g_ui.page; x4SeenView = g_ui.captainView;
    x4SeenStep = g_campaign.tutorialStep; x4SeenGood = g_ui.selectedGood;
    if (g_ui.card == CARD_TRADE) {
      const MerchantTradeQuote prior = merchantDraft.trade;
      x4_merchant_refresh(); // preserve quantities, refresh cash/stock fingerprint
      if (prior.total != merchantDraft.trade.total || prior.cashDelta != merchantDraft.trade.cashDelta)
        apply_result(SIM_STALE_PREVIEW); // changed prices are visible before any commit
    }
  }
  g_ui.returnQuiet = 0; g_ui.dirty = 1;
  board_x4_discard_button_events();
  return true;
}

// Goal moments. The ladder position is watched from the company as loaded, so
// only rungs crossed while playing are celebrated; the return report already
// summarizes milestones reached while away.
static uint8_t goalSeenStep = 0xFF, goalSeenRegion = REGION_COUNT;
static uint32_t goalSeenSeed = 0, goalSeenEpoch = 0;
static bool goalMomentActive = false;
static uint8_t goalMomentStep = 0, goalMomentRegion = 0;
bool x4_goal_moment_active() { return goalMomentActive; }
uint8_t x4_goal_moment_step() { return goalMomentStep; }
uint8_t x4_goal_moment_region() { return goalMomentRegion; }

#if defined(SH_HOST_TESTS)
static bool goalMomentsEnabled = true;
void input_test_goal_moments(bool enabled) { goalMomentsEnabled = enabled; goalMomentActive = false; }
#endif

static void x4_goal_moment_update() {
#if defined(SH_HOST_TESTS)
  if (!goalMomentsEnabled) { goalSeenStep = 0xFF; return; }
#endif
  uint8_t step = voyage_goal_step();
  if (goalSeenStep == 0xFF || goalSeenSeed != g_campaign.seed || goalSeenEpoch != sim_return_epoch()) {
    goalSeenStep = step; goalSeenSeed = g_campaign.seed; goalSeenEpoch = sim_return_epoch();
    goalSeenRegion = sim_project_region(); goalMomentActive = false;
    return;
  }
  if (step <= goalSeenStep) {
    if (step >= GOAL_STEP_REGION_DOCKS && step < GOAL_STEP_GRAND_CHARTER) goalSeenRegion = sim_project_region();
    return;
  }
  // Wait for a calm screen: never over an incident, the guided tour, a draft
  // being edited, the return report, the title or the sleep cover.
  if (goalMomentActive || x4_story_moment_active() || g_ui.title || board_x4_asleep() || x4_return_report() ||
      x4Zone == X4_ZONE_INCIDENT || g_ui.card == CARD_INCIDENT || g_ui.card == CARD_OUTCOME ||
      g_ui.card == CARD_ARRIVAL || g_ui.card == CARD_TUTORIAL || x4_merchant_editing()) return;
  goalMomentStep = goalSeenStep; goalMomentRegion = goalSeenRegion;
  goalSeenStep = step; goalSeenRegion = sim_project_region();
  goalMomentActive = true;
  board_x4_discard_button_events();
  render_request_full_refresh();  // the moment arrives with a deliberate, clean full refresh
}

static void x4_goal_moment_close() {
  goalMomentActive = false;
  board_x4_discard_button_events();
  render_request_full_refresh();  // clear the heavy art before the task screen returns
}

// The Grey Passenger: a whole-screen page when a
// keepsake is taken aboard, when it is delivered, and when it is reread from
// Keepsakes. A delivery waits for the same calm screen as a goal moment.
static uint8_t storyMomentMode = 0, storyMomentChapter = STORY_NONE;
bool x4_story_moment_active() { return storyMomentMode != 0; }
uint8_t x4_story_moment_mode() { return storyMomentMode; }
uint8_t x4_story_moment_chapter() { return storyMomentChapter; }

static void x4_story_moment_open(uint8_t mode, uint8_t chapter) {
  if (chapter >= (mode == X4_ATLAS_PAGE ? MAX_PORTS : STORY_CHAPTERS)) return;
  storyMomentMode = mode; storyMomentChapter = chapter;
  board_x4_discard_button_events();
  render_request_full_refresh();
}

static void x4_story_moment_update() {
  if (storyMomentMode || sim_story_card_pending() == STORY_NONE) return;
  if (goalMomentActive || g_ui.title || board_x4_asleep() || x4_return_report() || x4_onboarding_screen() ||
      x4Zone == X4_ZONE_INCIDENT || g_ui.card == CARD_INCIDENT || g_ui.card == CARD_OUTCOME ||
      g_ui.card == CARD_ARRIVAL || g_ui.card == CARD_TUTORIAL || x4_merchant_editing()) return;
  x4_story_moment_open(X4_STORY_DELIVERED, sim_story_card_pending());
}

static void x4_story_moment_close() {
  if (storyMomentMode == X4_STORY_DELIVERED) sim_story_ack_card();
  storyMomentMode = 0; storyMomentChapter = STORY_NONE;
  board_x4_discard_button_events();
  render_request_full_refresh();
}

// Onboarding (sim_onboarding.h): the captain and name screens are full-screen
// steps; the voyage runs on the ordinary screens with focus steered to the next
// control and a captain's line along the bottom.
static uint8_t onboardPick = 0, onboardName = 0;
static uint32_t goalMomentValue = 0;
uint8_t x4_onboarding_pick() { return onboardPick; }
uint8_t x4_onboarding_name_index() { return onboardName; }
int32_t x4_goal_moment_value() { return (int32_t)goalMomentValue; }
bool x4_onboarding_screen() {
  const uint8_t step = sim_onboarding_step();
  return !g_ui.title && (step == ONBOARD_CAPTAIN || step == ONBOARD_NAME);
}

static void x4_merchant_ship();
static void x4_onboarding_press(uint8_t action) {
  const bool naming = sim_onboarding_step() == ONBOARD_NAME;
  const int count = naming ? sim_onboarding_name_count() : 3;
  uint8_t& pick = naming ? onboardName : onboardPick;
  if (action == X4_ACT_UP || action == X4_ACT_PAGE_PREV) {
    pick = (uint8_t)((pick + count - 1) % count);
    if (naming) apply_result(sim_onboarding_rename(pick));
  } else if (action == X4_ACT_DOWN || action == X4_ACT_PAGE_NEXT) {
    pick = (uint8_t)((pick + 1) % count);
    if (naming) apply_result(sim_onboarding_rename(pick));
  } else if (action == X4_ACT_OK && !naming) {
    if (apply_result(sim_onboarding_hire(pick))) {
      // Naming starts on the chosen captain's own name.
      const CaptainProfile* captain = sim_ship_captain(0);
      onboardName = 0;
      for (uint8_t i = 0; captain && i < sim_onboarding_name_count(); ++i)
        if (!strcmp(sim_onboarding_name(i), captain->name)) { onboardName = i; break; }
    }
  } else if (action == X4_ACT_OK) {
    if (apply_result(sim_onboarding_confirm_name())) {
      // The voyage begins on the starting ship, captain beside the hold.
      x4_nav_reset();
      g_ui.card = CARD_NONE; g_ui.scene = SCENE_FLEET; g_ui.selectedShip = 0;
      x4_merchant_ship();
    }
  } else if (action == X4_ACT_BACK) {
    // Choosing: skip the guided voyage. Naming: back to the three captains.
    if (naming) { g_campaign.onboardingStep = ONBOARD_CAPTAIN; }
    else apply_result(sim_onboarding_skip(onboardPick));
  }
  g_ui.dirty = 1;
  board_x4_discard_button_events();
}

// The guided voyage ends with its own goal moment: the first profit.
static void x4_onboarding_tick() {
  static uint32_t seenEpoch = 0;
  if (seenEpoch != sim_return_epoch()) {  // a new or reloaded company starts on the first captain
    seenEpoch = sim_return_epoch(); onboardPick = 0; onboardName = 0;
  }
  if (!sim_onboarding_update()) return;
  goalMomentValue = (uint32_t)max((int32_t)0, g_campaign.marketProfit);
  goalMomentStep = GOAL_MOMENT_FIRST_PROFIT; goalMomentRegion = 0;
  goalSeenStep = voyage_goal_step();  // the ladder carries on from here
  goalMomentActive = true;
  board_x4_discard_button_events();
  render_request_full_refresh();
}

static bool x4_touch_return_gate() {
  bool returning = (uint32_t)(millis() - x4LastInputMs) >= X4_RETURN_IDLE_MS;
  x4LastInputMs = millis();
  if (!returning || g_ui.title || x4_return_report() || !sim_return_has_updates()) return false;
  x4_prepare_return(false);
  return true;
}

static void x4_return_footer(int id) {
  if (id == 2) {
    g_ui.card = CARD_NONE;
    for (int i = 0; i < returnReport.rowCount; ++i)
      if (returnOrder[i] == g_ui.selectedShip) g_ui.page = (uint8_t)(i / 3);
    return;
  }
  int pages = max(1, (x4_return_row_count() + 2) / 3);
  g_ui.page = (uint8_t)((g_ui.page + (id == 0 ? pages - 1 : 1)) % pages);
}

static void x4_return_row(int row) {
  int index = x4_return_row_ship(row);
  if (index < 0 || index >= g_campaign.shipCount) return;
  const Ship& ship = g_ships[index];
  const ReturnRow* captured = nullptr;
  for (int i = 0; i < returnReport.rowCount; ++i)
    if (returnReport.rows[i].shipIndex == index) captured = &returnReport.rows[i];
  g_ui.selectedShip = (uint8_t)index;
  g_ui.selectedPort = ship.state == SHIP_SAILING || ship.state == SHIP_AWAITING_DECISION ?
    ship.toPort : ship.currentPort;
  if (g_campaign.economyVersion < 2 && captured && captured->bookingId == ship.booking.id &&
      ship.booking.state == BOOKING_SETTLED && ship.state != SHIP_AWAITING_DECISION &&
      ship.state != SHIP_DISABLED) {
    g_ui.arrivalSummaryVisible = 1; g_ui.arrivalSummaryShip = (uint8_t)index;
    g_ui.arrivalSummaryPort = ship.booking.destination;
    g_ui.arrivalSummaryCoins = ship.booking.arrivalCoins;
    g_ui.arrivalSummaryDelivered = ship.booking.settledQuantity;
    g_ui.arrivalSummaryWon = g_campaign.won && ship.currentPort == 0;
    open_card(CARD_ARRIVAL);
  } else if (x4_finish_return(false)) {
    if (ship.state == SHIP_AWAITING_DECISION) {
      if (sim_select_incident((uint8_t)index)) open_card(CARD_INCIDENT);
      else open_card(CARD_INBOX);
    }
    else if (ship.state == SHIP_DISABLED) { open_card(CARD_SHIPYARD); g_ui.page = 2; }
    else {
      g_ui.card = CARD_NONE;
      g_ui.scene = SCENE_FLEET; g_ui.page = fleetPage = (uint8_t)index / VOYAGE_FLEET_ROWS;
      x4_open_voyage();
    }
  }
}

// Main scenes expose top tabs plus their visible body/footer actions.
// Transaction cards deliberately remove main navigation until Back.
int x4_column(uint8_t* out) {
  int n = 0;
  if (voyage_dispatch_card(g_ui.card, g_ui.page)) return 0;
  if(g_campaign.economyVersion>=2 && g_ui.scene==SCENE_PORT && g_ui.card==CARD_NONE) {
    out[n++]=X4_CMD_MARKET;out[n++]=X4_CMD_CARGO;out[n++]=X4_CMD_LEDGER;out[n++]=X4_CMD_BACK;return n;
  }
  if (g_ui.scene == SCENE_FLEET && g_ui.card != CARD_NONE) {
    out[n++] = X4_CMD_FLEET; out[n++] = X4_CMD_MAP; out[n++] = X4_CMD_COMPANY; return n;
  }
  if (g_ui.scene == SCENE_FLEET) {
    int pages = max(1, (g_campaign.shipCount + VOYAGE_FLEET_ROWS - 1) / VOYAGE_FLEET_ROWS);
    if (g_ui.page >= pages) g_ui.page = (uint8_t)(pages - 1);
    fleetPage = g_ui.page;
    for (int row = 0; row < VOYAGE_FLEET_ROWS; ++row)
      if (x4_fleet_ship(row) < g_campaign.shipCount) out[n++] = X4_CMD_SHIP_0 + row;
    if (pages > 1) { out[n++] = X4_CMD_PAGE_PREV; out[n++] = X4_CMD_PAGE_NEXT; }
  } else if (g_ui.scene == SCENE_WORLD) {
    if (watchActive) {
      out[n++] = X4_CMD_WATCH_READY;
      out[n++] = X4_CMD_WATCH_NEXT;
      out[n++] = X4_CMD_WATCH_MAP;
    } else {
    out[n++] = X4_CMD_PAGE_PREV; out[n++] = X4_CMD_PAGE_NEXT;
    out[n++] = X4_CMD_ZOOM;
    out[n++] = X4_CMD_PORT;
    }
  } else if (g_ui.scene == SCENE_COMPANY) {
    if (x4_return_report()) {
      for (int row = 0; row < 3; ++row) if (x4_return_row_ship(row) >= 0) out[n++] = X4_CMD_SHIP_0 + row;
      out[n++] = X4_CMD_JOBS;
      if (returnReport.rowCount > 3) { out[n++] = X4_CMD_PAGE_PREV; out[n++] = X4_CMD_PAGE_NEXT; }
    }
    else out[n++] = X4_CMD_YARD;
    out[n++] = X4_CMD_LEDGER;
  } else {
    out[n++] = X4_CMD_JOBS;
    bool repeat = g_campaign.economyVersion < 2 && g_campaign.shipCount >= 2 && g_campaign.completedVoyages >= 4 &&
      g_ui.selectedShip < g_campaign.shipCount && g_ships[g_ui.selectedShip].booking.state == BOOKING_SETTLED;
    out[n++] = repeat ? X4_CMD_ROUTE : X4_CMD_CARGO;
    out[n++] = X4_CMD_LEDGER;
  }
  if (!x4_return_report()) { out[n++] = X4_CMD_FLEET; out[n++] = X4_CMD_MAP; out[n++] = X4_CMD_COMPANY; }
  return n;
}

int x4_column_slot(uint8_t command) {
  uint8_t column[COLUMN_MAX];
  int n = x4_column(column);
  for (int i = 0; i < n; ++i) if (column[i] == command) return i;
  return -1;
}

const char* x4_command_label(uint8_t command, char* buf, size_t cap) {
  if (x4_return_report() && g_ui.scene == SCENE_COMPANY) {
    if (command == X4_CMD_LEDGER) return "UPDATES";
    if (command == X4_CMD_JOBS) return "CONTINUE";
    if (command >= X4_CMD_SHIP_0 && command <= X4_CMD_SHIP_2) {
      int index = x4_return_row_ship(command - X4_CMD_SHIP_0);
      return index >= 0 ? g_ships[index].name : "";
    }
  }
  if(g_campaign.economyVersion>=2 && g_ui.scene==SCENE_PORT) {
    if(command==X4_CMD_MARKET)return "Prices";
    if(command==X4_CMD_CARGO)return "Ships here";
    if(command==X4_CMD_LEDGER)return "Incoming";
  }
  switch (command) {
    case X4_CMD_WATCH_NEXT: return "Next ship";
    case X4_CMD_WATCH_READY: if (g_ui.selectedShip < g_campaign.shipCount && g_ships[g_ui.selectedShip].state == SHIP_AWAITING_DECISION) return "Respond"; if (g_ui.selectedShip < g_campaign.shipCount && (g_ships[g_ui.selectedShip].state == SHIP_ARRIVED || g_ships[g_ui.selectedShip].state == SHIP_DOCKED)) return "Open ship"; return x4_next_ready_ship() < MAX_SHIPS ? "Next ready ship" : "Fleet";
    case X4_CMD_WATCH_MAP: return "World map";
    case X4_CMD_BACK: return "BACK";
    case X4_CMD_PORT: return "PORT";
    case X4_CMD_MAP: return "WORLD";
    case X4_CMD_JOBS:
      if (g_campaign.economyVersion >= 2) return g_ui.selectedShip < g_campaign.shipCount && g_ships[g_ui.selectedShip].state == SHIP_SAILING ? "CARGO" : "MARKET";
      return sim_recovery_available() ? "HARBOR WORK" : g_ui.scene == SCENE_COMPANY ? "SEND SHIP" :
      g_ui.selectedShip < g_campaign.shipCount && g_ships[g_ui.selectedShip].booking.state == BOOKING_SAILING ?
      "PLAN LOAD" : "PREPARE";
    case X4_CMD_MARKET: return "MARKET";
    case X4_CMD_ROUTE: return "REPEAT";
    case X4_CMD_CARGO: return "WATCH";
    case X4_CMD_FLEET: return "FLEET";
    case X4_CMD_COMPANY: return "COMPANY";
    case X4_CMD_PAGE_PREV: return g_ui.scene == SCENE_WORLD ? "< PORT" : "PREVIOUS";
    case X4_CMD_PAGE_NEXT: return g_ui.scene == SCENE_WORLD ? "PORT >" : "NEXT";
    case X4_CMD_SHIP_0: case X4_CMD_SHIP_1: case X4_CMD_SHIP_2: {
      uint8_t ship = x4_fleet_ship(command - X4_CMD_SHIP_0);
      return ship < g_campaign.shipCount ? g_ships[ship].name : "";
    }
    case X4_CMD_YARD: return "NEXT GOAL";
    case X4_CMD_LEDGER: return "DETAILS";
    case X4_CMD_MAIL: {
      uint8_t unread = attention_unread_count();
      // A trailing mark means a decision is waiting, not just news.
      if (unread) snprintf(buf, cap, "NOTICES %u%s", unread, attention_urgent_unread_count() ? "!" : "");
      else snprintf(buf, cap, "NOTICES");
      return buf;
    }
    case X4_CMD_EVENT: return "EVENT";
    case X4_CMD_ZOOM:
      // Reopen the map at its chosen detail level; 1x opens at 2x here.
      if (g_ui.scene != SCENE_WORLD) {
        snprintf(buf, cap, g_ui.mapZoom >= 10 ? "ZOOM%ux" : "ZOOM %ux", g_ui.mapZoom > 1 ? (unsigned)g_ui.mapZoom : 2u);
        return buf;
      }
      snprintf(buf, cap, g_ui.mapZoom >= 10 ? "ZOOM%ux" : "ZOOM %ux", (unsigned)g_ui.mapZoom);
      return buf;
    default: return "";
  }
}

bool x4_command_enabled(uint8_t command) {
  if (command == X4_CMD_WATCH_NEXT) {
    for (int i = 0; i < g_campaign.shipCount; ++i)
      if (i != g_ui.selectedShip && (g_ships[i].state == SHIP_SAILING || g_ships[i].state == SHIP_AWAITING_DECISION)) return true;
    return false;
  }

  if (x4_return_report() && g_ui.scene == SCENE_COMPANY && command == X4_CMD_JOBS)
    return g_ui.selectedShip < g_campaign.shipCount;
  switch (command) {
    case X4_CMD_PORT: return g_ports[g_ui.selectedPort].unlocked;
    case X4_CMD_JOBS:
      if (g_campaign.economyVersion >= 2) return g_ui.selectedShip < g_campaign.shipCount && g_ships[g_ui.selectedShip].state != SHIP_DISABLED;
      return sim_recovery_available() || (g_ports[g_ui.selectedPort].unlocked && (sim_selected_docked_ship() >= 0 ||
      (g_ui.selectedShip < g_campaign.shipCount && g_ships[g_ui.selectedShip].booking.state == BOOKING_SAILING)));
    case X4_CMD_MARKET: return g_ports[g_ui.selectedPort].unlocked;
    case X4_CMD_YARD: return true;
    case X4_CMD_CARGO: return g_ui.scene==SCENE_PORT || g_ui.selectedShip < g_campaign.shipCount;
    case X4_CMD_ROUTE: return selected_ship_can_route();
    default: return true;
  }
}

bool x4_command_lit(uint8_t) { return false; }

static void x4_select_ready_ship() {
  if (g_ui.selectedShip < g_campaign.shipCount && selected_ship_can_route()) {
    g_ui.selectedPort = g_ships[g_ui.selectedShip].currentPort;
    return;
  }
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    if (g_ships[i].state != SHIP_DOCKED && g_ships[i].state != SHIP_ARRIVED) continue;
    g_ui.selectedShip = (uint8_t)i;
    g_ui.selectedPort = g_ships[i].currentPort;
    return;
  }
}

static void x4_run_command(uint8_t command) {
  if (command == X4_CMD_WATCH_NEXT) {
    for (int offset = 1; offset <= g_campaign.shipCount; ++offset) {
      int index = (g_ui.selectedShip + offset) % g_campaign.shipCount;
      if (g_ships[index].state != SHIP_SAILING && g_ships[index].state != SHIP_AWAITING_DECISION) continue;
      g_ui.selectedShip = (uint8_t)index; g_ui.selectedPort = g_ships[index].toPort;
      render_map_follow_selected_ship(); break;
    }
    g_ui.dirty = 1; return;
  }
  if ((command == X4_CMD_PAGE_PREV || command == X4_CMD_PAGE_NEXT) && g_ui.scene == SCENE_WORLD) {
    x4_cycle_port(command == X4_CMD_PAGE_PREV ? -1 : 1); g_ui.dirty = 1; return;
  }
  if ((command == X4_CMD_PAGE_PREV || command == X4_CMD_PAGE_NEXT) && x4_return_report()) {
    int pages = max(1, (returnReport.rowCount + 2) / 3);
    g_ui.page = (uint8_t)((g_ui.page + (command == X4_CMD_PAGE_PREV ? pages - 1 : 1)) % pages);
    g_ui.selectedShip = returnOrder[g_ui.page * 3]; g_ui.dirty = 1; return;
  }
  if (command == X4_CMD_WATCH_MAP) { x4_run_command(X4_CMD_MAP); return; }
  if (command == X4_CMD_WATCH_READY) {
    if (g_ui.selectedShip < g_campaign.shipCount &&
        (g_ships[g_ui.selectedShip].state == SHIP_AWAITING_DECISION ||
         g_ships[g_ui.selectedShip].state == SHIP_ARRIVED ||
         g_ships[g_ui.selectedShip].state == SHIP_DOCKED)) { x4_open_voyage(); return; }
    if(g_campaign.economyVersion>=2){uint8_t next=x4_next_ready_ship();if(next<MAX_SHIPS)x4_open_ship(next);else x4_run_command(X4_CMD_FLEET);return;}
    watchActive = false; render_map_stop_follow();
    x4_departure_row(0); return;
  }
  if (x4_return_report() && g_ui.scene == SCENE_COMPANY) {
    if (command == X4_CMD_LEDGER) { open_card(CARD_RETURN_DETAILS); return; }
    if (command == X4_CMD_JOBS) { x4_finish_return(true); return; }
    if (command >= X4_CMD_SHIP_0 && command <= X4_CMD_SHIP_2) {
      x4_return_row(command - X4_CMD_SHIP_0); g_ui.dirty = 1; return;
    }
    if ((command == X4_CMD_PORT || command == X4_CMD_MAP || command == X4_CMD_FLEET || command == X4_CMD_COMPANY) &&
        !x4_finish_return(false)) return;
  }
  switch (command) {
    case X4_CMD_BACK: if(!x4_nav_back())x4_run_command(X4_CMD_MAP);break;
    case X4_CMD_PORT:
      if (!g_ports[g_ui.selectedPort].unlocked) { apply_result(SIM_PORT_LOCKED); break; }
      x4_nav_push();
      g_ui.scene = SCENE_PORT; g_ui.card = CARD_NONE; watchActive = false;
      if(g_campaign.economyVersion<2)select_ship_at_port(g_ui.selectedPort);
      if (sim_tutorial_complete(TUTORIAL_PORT)) save_write();
      break;
    case X4_CMD_MAP:
      if(watchActive)x4_nav_push();else x4_nav_reset();
      g_ui.card = CARD_NONE; g_ui.scene = SCENE_WORLD; watchActive = false;
      render_map_stop_follow(); g_ui.mapZoom = 1;x4_nav_restore_world();
      break;
    case X4_CMD_JOBS:
      if (g_campaign.economyVersion >= 2 && g_ui.selectedShip < g_campaign.shipCount && g_ships[g_ui.selectedShip].state == SHIP_SAILING) { open_card(CARD_MANIFEST); break; }
      if (g_ui.scene == SCENE_COMPANY) x4_select_ready_ship();
      if (!g_ports[g_ui.selectedPort].unlocked) apply_result(SIM_PORT_LOCKED);
      else x4_open_voyage(g_ui.scene == SCENE_WORLD && watchActive);
      break;
    case X4_CMD_MARKET:
      if(g_campaign.economyVersion>=2 && g_ui.scene==SCENE_PORT){open_card(CARD_PORT_PRICES);break;}
      if (g_ports[g_ui.selectedPort].unlocked) { if (g_campaign.economyVersion >= 2) x4_merchant_open(); else open_card(CARD_MARKET); }
      else apply_result(SIM_PORT_LOCKED);
      break;
    case X4_CMD_YARD: open_card(CARD_PROGRESS); break;
    case X4_CMD_ROUTE:
      if (selected_ship_can_route()) {
        VoyageQuote repeated = sim_repeat_voyage_quote(g_ui.selectedShip);
        if (repeated.destination < MAX_PORTS) {
          voyageOriginUi = g_ui;
          open_card(CARD_VOYAGE);
          voyageDestination = repeated.destination;
          voyageDraft = repeated;
          memcpy(voyageQuantities, repeated.quantities, sizeof(voyageQuantities));
          voyageCompanyEpoch = sim_return_epoch();
          g_ui.page = 2;
        } else apply_result(repeated.code);
      } else apply_result(SIM_BAD_SHIP_STATE);
      break;
    case X4_CMD_CARGO:
      if(g_campaign.economyVersion>=2 && g_ui.scene==SCENE_PORT){open_card(CARD_PORT_SHIPS);g_ui.captainView=0;break;}
      if (g_ui.selectedShip < g_campaign.shipCount) x4_watch_ship();
      else apply_result(SIM_BAD_SHIP_STATE);
      break;
    case X4_CMD_FLEET:
      x4_nav_reset();
      g_ui.card = CARD_NONE; g_ui.scene = SCENE_FLEET;
      g_ui.page = fleetPage; watchActive = false;
      break;
    case X4_CMD_COMPANY:
      x4_nav_reset();
      g_ui.card = CARD_NONE; g_ui.scene = SCENE_COMPANY; g_ui.page = 0;
      watchActive = false;
      if (!x4_return_report() && sim_return_has_updates()) x4_prepare_return(false, false);
      break;
    case X4_CMD_SHIP_0: case X4_CMD_SHIP_1: case X4_CMD_SHIP_2:
      x4_open_ship(x4_fleet_ship(command - X4_CMD_SHIP_0)); break;
    case X4_CMD_PAGE_PREV: x4_fleet_page(-1); break;
    case X4_CMD_PAGE_NEXT: x4_fleet_page(1); break;
    case X4_CMD_LEDGER: if(g_campaign.economyVersion>=2 && g_ui.scene==SCENE_PORT){open_card(CARD_PORT_SHIPS);g_ui.captainView=1;}else open_card(CARD_MORE); break;
    case X4_CMD_MAIL: open_card(CARD_INBOX); break;
    case X4_CMD_EVENT: open_card(CARD_EVENT); break;
    case X4_CMD_ZOOM:
      if (g_ui.scene != SCENE_WORLD) {
        g_ui.scene = SCENE_WORLD;
        render_map_follow_selected_ship();
        if (g_ui.mapZoom <= 1) render_map_toggle_zoom(0, 0);
      } else render_map_toggle_zoom(0, 0);
      break;
    default: break;
  }
  g_ui.dirty = 1;
}

static void x4_run_slot(int slot) {
  uint8_t column[COLUMN_MAX];
  int n = x4_column(column);
  if (slot >= 0 && slot < n) x4_run_command(column[slot]);
}

// Mouse support for the desktop preview: which column slot is under (x, y).
static int x4_slot_at(int x, int y) {
  if (g_ui.card != CARD_NONE) return -1;
  uint8_t column[COLUMN_MAX];
  int n = x4_column(column);
  for (int i = 0; i < n; ++i) {
    HitRegion r = voyage_scene_region(column[i], i);
    if (hit_test(&r, 1, x, y) >= 0) return i;
  }
  return -1;
}

// Rows of the open card that do something when confirmed.
static int x4_rows(uint8_t* rows) {
  int n = 0;
  switch (g_ui.card) {
    case CARD_SHIP: rows[n++] = 0; rows[n++] = 1; rows[n++] = 2; break;
    case CARD_TRADE: case CARD_PACKAGES:
      if (x4_merchant_editing()) break;
      for (int row = 0; row < 2; ++row) if (g_ui.page * 2 + row < x4_merchant_good_count()) rows[n++] = row;
      if (x4_merchant_good_count() > 2) { rows[n++] = 5; rows[n++] = 6; }
      break;
    case CARD_VOYAGE_LOAD:
      for (int row = 0; row < 2; ++row) if (x4_load_good(row) >= 0) rows[n++] = (uint8_t)row;
      break;
    case CARD_VOYAGE_DEPARTURE: rows[n++] = 0; rows[n++] = 1; break;
    case CARD_VOYAGE:
      if (g_ui.page == 0) {
        for (int r = 0; r < VOYAGE_DESTINATION_ROWS; ++r)
          if (x4_voyage_destination(r) >= 0) rows[n++] = (uint8_t)r;
      } else if (g_ui.page == 1 || g_ui.page == 5) { rows[n++] = 0; rows[n++] = 1; }
      break;
    case CARD_JOBS:
      for (int r = 0; r < CARD_VISIBLE_ROWS; ++r) {
        int slot = g_ui.page * CARD_VISIBLE_ROWS + r;
        if (slot < MAX_CONTRACTS_PER_PORT && g_contracts[g_ui.selectedPort][slot].active == 1)
          rows[n++] = (uint8_t)r;
      }
      break;
    case CARD_MANIFEST:
      if (selected_ship_can_route() && sim_feature_unlocked(FEATURE_WAREHOUSE)) {
        x4_manifest_clamp();
        int count = x4_manifest_count();
        for (int r = 0; r < CARD_VISIBLE_ROWS && r + g_ui.routeScroll < count; ++r)
          rows[n++] = (uint8_t)r;
      }
      break;
    case CARD_ROUTE:
      if (g_ui.page == 0) {
        for (int r = 0; r < ROUTE_VISIBLE_ROWS; ++r)
          if (input_route_candidate(r) >= 0) rows[n++] = (uint8_t)r;
        if (!guided() && sim_feature_unlocked(FEATURE_ROUTE_RULES)) rows[n++] = 5;
      } else if (g_ui.page == 1) {
        for (int r = 0; r < 5; ++r) rows[n++] = (uint8_t)r;
      }
      break;
    case CARD_MARKET:
      for (int r = 0; r < X4_MARKET_ROW_COUNT; ++r) rows[n++] = (uint8_t)r;
      break;
    case CARD_FLEET:
      for (int r = 0; r < CARD_VISIBLE_ROWS; ++r)
        if (g_ui.page * CARD_VISIBLE_ROWS + r < g_campaign.shipCount) rows[n++] = (uint8_t)r;
      break;
    case CARD_SHIPYARD: {
      int mode = g_ui.page % 3;
      if (mode == 0) { for (int r = 0; r < SHIP_CLASS_COUNT; ++r) rows[n++] = (uint8_t)r; }
      else if (mode == 1) { if (sim_feature_unlocked(FEATURE_FITTINGS)) for (int r = 0; r < MODULE_SHOP_COUNT; ++r) rows[n++] = (uint8_t)r; }
      else {
        if (sim_feature_unlocked(FEATURE_PORT_WORKS)) for (int r = 0; r < 3; ++r) rows[n++] = (uint8_t)r;
        rows[n++] = 3; rows[n++] = 4;  // repair and relief are always reachable
        if (g_campaign.won) rows[n++] = 5;  // the Grand Charter's pennant
      }
      break;
    }
    case CARD_CHARTER:
      for (int r = 0; r < REGION_COUNT && r < CARD_VISIBLE_ROWS; ++r) rows[n++] = (uint8_t)r;
      break;
    case CARD_MORE:
      for (int r = 0; r < CARD_VISIBLE_ROWS; ++r)
        if (voyage_detail_index(g_ui.page * CARD_VISIBLE_ROWS + r) >= 0) rows[n++] = (uint8_t)r;
      break;
    case CARD_ATLAS:
      for (int r = 0; r < 4; ++r) if (sim_atlas_charted((uint8_t)(g_ui.page * 4 + r))) rows[n++] = (uint8_t)r;
      break;
    case CARD_KEEPSAKES:
      for (int r = 0; r < CARD_VISIBLE_ROWS; ++r)
        if (g_ui.page * CARD_VISIBLE_ROWS + r < g_campaign.storyDeliveries) rows[n++] = (uint8_t)r;
      break;
    case CARD_PORT_PICKER:
      for(int r=0;r<CARD_VISIBLE_ROWS;++r)if(x4_yard_port(r)<MAX_PORTS)rows[n++]=r;
      break;
    case CARD_PORT_SHIPS:
      for(int r=0;r<CARD_VISIBLE_ROWS;++r)if(x4_port_ship(r)<g_campaign.shipCount)rows[n++]=r;
      break;
    case CARD_MENU:
      for (int r = 0; r < CARD_VISIBLE_ROWS; ++r) rows[n++] = (uint8_t)r;
      break;
    case CARD_INBOX:
      for (int r = 0; r < CARD_VISIBLE_ROWS; ++r)
        if (input_attention_index(r) >= 0) rows[n++] = (uint8_t)r;
      break;
    case CARD_RETURN_DETAILS:
      for (int r = 0; r < 3; ++r) if (x4_return_row_ship(r) >= 0) rows[n++] = (uint8_t)r;
      break;
    case CARD_CAPTAINS:
      if (g_ui.captainView == 0) {
        for (int r = 0; r < CARD_VISIBLE_ROWS; ++r) {
          int index = g_ui.page * CARD_VISIBLE_ROWS + r;
          if (index < MAX_CAPTAINS && g_captains[index].active) rows[n++] = (uint8_t)r;
        }
      } else if (g_ui.captainView == 2) {
        for (int candidate = 0; candidate < 2; ++candidate) {
          CaptainProfile preview{};
          if (sim_recruit_candidate(g_ui.selectedPort, (uint8_t)candidate, &preview))
            rows[n++] = (uint8_t)(candidate * 3);
        }
      }
      break;
    default: break;
  }
  return n;
}

static uint8_t x4_footer_mask() {
  switch (g_ui.card) {
    case CARD_NONE: case CARD_INCIDENT: return 0;
    case CARD_SHIP: return 0b100;
    case CARD_SHIPYARD: return merchantUpgradeReturn ? 0b100 : 0b111;
    case CARD_TRADE: case CARD_PACKAGES: return x4_merchant_editing() ? 0b110 : 0b100;
    case CARD_KEEPSAKES: return g_campaign.storyDeliveries > CARD_VISIBLE_ROWS ? 0b111 : 0b100;
    case CARD_ATLAS: return 0b111;
    case CARD_VOYAGE_LOAD: return 0b111;
    case CARD_VOYAGE_DEPARTURE: return 0b100;
    case CARD_VOYAGE:
      if (g_ui.page == 3 || g_ui.page == 4) return 0b110;
      if (g_ui.page == 5) return 0b100;
      return g_ui.page == 1 ? 0b100 : g_ui.page == 2 ? 0b111 :
        x4_voyage_destination_count() > VOYAGE_DESTINATION_ROWS ? 0b111 : 0b100;
    case CARD_PROGRESS: if (voyage_needs_range() || x4_merchant_donation_good() >= 0) return 0b111;
      return (g_campaign.deliveredUnits >= 24 && g_campaign.shipCount < 2) ||
      (g_campaign.deliveredUnits >= 48 && sim_project_region() < REGION_COUNT) ? 0b101 : 0b100;
    case CARD_CHARTER: return 0b100;
    case CARD_HELP: return 0b101;
    case CARD_PORT_PICKER: return 0b111;
    case CARD_PORT_PRICES: return 0b111;
    case CARD_PORT_SHIPS: return g_campaign.shipCount>CARD_VISIBLE_ROWS?0b111:0b100;
    case CARD_TUTORIAL: return g_campaign.economyVersion>=2 && !g_ui.page ? 0b011 : 0b111;
    case CARD_MORE: return voyage_detail_count() > CARD_VISIBLE_ROWS ? 0b111 : 0b100;
    case CARD_RETURN_DETAILS: return x4_return_row_count() > 3 ? 0b111 : 0b100;
    case CARD_MENU: return 0b111;
    case CARD_FLEET: return sim_feature_unlocked(FEATURE_CAPTAINS) ? 0b111 : 0b101;
    case CARD_LEDGER: return sim_feature_unlocked(FEATURE_CHARTERS) ? 0b111 : 0b110;
    case CARD_MANIFEST: return selected_ship_can_route() && sim_feature_unlocked(FEATURE_WAREHOUSE) ? 0b111 : 0b101;
    case CARD_OUTCOME: return g_incident.active ? 0b111 : 0b101;
    default: return 0b111;
  }
}

// Everything the cursor can rest on inside a card: rows first, then footer.
static int x4_card_items(X4CardItem* out) {
  uint8_t rows[X4_CARD_ROW_MAX];
  int n = 0;
  int rowCount = x4_rows(rows);
  for (int i = 0; i < rowCount; ++i) out[n++] = { 0, rows[i] };
  uint8_t mask = x4_footer_mask();
  for (uint8_t id = 0; id < 3; ++id) if (mask & (1u << id)) out[n++] = { 1, id };
  return n;
}

// Record what the cursor rests on right now.
static void x4_remember_focus() {
  if (x4Zone == X4_ZONE_CARD) {
    X4CardItem items[X4_CARD_MAX];
    int n = x4_card_items(items);
    if (n) {
      if (x4Item >= n) x4Item = (int8_t)(n - 1);
      if (x4Item < 0) x4Item = 0;
      x4KeyFooter = items[x4Item].footer; x4KeyId = items[x4Item].id;
      // A good becomes selected as soon as focus enters its row. The detail
      // and both trading shortcuts therefore always describe the same good.
      if (g_ui.card == CARD_MARKET && !x4KeyFooter) g_ui.selectedGood = x4KeyId;
      if (merchant_card(g_ui.card) && !x4KeyFooter && x4KeyId < 2) {
        int good = g_ui.card == CARD_PACKAGES ? g_ui.page * 2 + x4KeyId : x4_merchant_good(x4KeyId);
        if (good >= 0) g_ui.selectedGood = (uint8_t)good;
      }
      if (g_ui.card == CARD_VOYAGE_LOAD && !x4KeyFooter) {
        int good = x4_load_good(x4KeyId);
        if (good >= 0) g_ui.selectedGood = (uint8_t)good;
      }
    }
  } else if (x4Zone == X4_ZONE_SCENE) {
    uint8_t column[COLUMN_MAX];
    int n = x4_column(column);
    if (n) {
      if (x4Item >= n) x4Item = (int8_t)(n - 1);
      if (x4Item < 0) x4Item = 0;
      x4KeyCmd = column[x4Item];
    }
  }
}

static void x4_focus_load_row(int row) {
  x4_sync();
  X4CardItem items[X4_CARD_MAX];
  int n = x4_card_items(items);
  for (int i = 0; i < n; ++i) if (!items[i].footer && items[i].id == row) {
    x4Zone = X4_ZONE_CARD; x4Item = (int8_t)i;
    x4_remember_focus(); return;
  }
}

// Put the cursor back on the remembered control after the list was rebuilt.
// A row that vanished hands the cursor to the next row below it (or the last
// row above); a footer button keeps its slot.
static void x4_relocate_focus() {
  if (x4Zone == X4_ZONE_CARD) {
    X4CardItem items[X4_CARD_MAX];
    int n = x4_card_items(items);
    if (!n) { x4Item = 0; return; }
    for (int i = 0; i < n; ++i)
      if (items[i].footer == x4KeyFooter && items[i].id == x4KeyId) { x4Item = (int8_t)i; return; }
    int candidate = -1;
    if (!x4KeyFooter) {
      for (int i = 0; i < n; ++i) if (!items[i].footer && items[i].id >= x4KeyId) { candidate = i; break; }
      if (candidate < 0) for (int i = n - 1; i >= 0; --i) if (!items[i].footer) { candidate = i; break; }
      if (candidate < 0) for (int i = 0; i < n; ++i) if (items[i].footer && items[i].id == 2) { candidate = i; break; }
    } else {
      for (int i = 0; i < n; ++i) if (items[i].footer && items[i].id >= x4KeyId) { candidate = i; break; }
      if (candidate < 0) candidate = n - 1;
    }
    x4Item = (int8_t)(candidate < 0 ? 0 : candidate);
    x4KeyFooter = items[x4Item].footer; x4KeyId = items[x4Item].id;
  } else if (x4Zone == X4_ZONE_SCENE) {
    uint8_t column[COLUMN_MAX];
    int n = x4_column(column);
    if (!n) { x4Item = 0; return; }
    for (int i = 0; i < n; ++i) if (column[i] == x4KeyCmd) { x4Item = (int8_t)i; return; }
    if (x4Item >= n) x4Item = (int8_t)(n - 1);
    if (x4Item < 0) x4Item = 0;
    x4KeyCmd = column[x4Item];
  }
}

static int x4_card_index_of_footer(uint8_t id) {
  X4CardItem items[X4_CARD_MAX];
  int n = x4_card_items(items);
  for (int i = 0; i < n; ++i) if (items[i].footer == 1 && items[i].id == id) return i;
  return -1;
}

// Destinations reachable from the end of the route draft (the route card
// shows ROUTE_VISIBLE_ROWS of them at a time, scrolled by the rocker).
static int x4_route_candidate_count() {
  if (g_ui.selectedShip >= g_campaign.shipCount) return 0;
  const Ship& s = g_ships[g_ui.selectedShip];
  if (s.state != SHIP_DOCKED && s.state != SHIP_ARRIVED) return 0;
  uint8_t from = g_ui.routeCount ? g_ui.routeDraft[g_ui.routeCount - 1] : s.currentPort;
  int count = 0;
  for (int p = 0; p < MAX_PORTS; ++p)
    if (g_ports[p].unlocked && p != from && content_lane_between(from, (uint8_t)p) >= 0) ++count;
  return count;
}

static bool x4_price_row_focused() {
  if (g_ui.card != CARD_ROUTE || g_ui.page != 1 || x4Zone != X4_ZONE_CARD) return false;
  X4CardItem items[X4_CARD_MAX];
  int n = x4_card_items(items);
  return x4Item < n && !items[x4Item].footer && items[x4Item].id == 4;
}

static void x4_reset_focus(bool soft) {
  if (g_ui.title) { x4Zone = X4_ZONE_SCENE; x4Item = 0; return; }
  if (g_ui.card == CARD_INCIDENT) {
    x4Zone = X4_ZONE_INCIDENT; x4Item = 0;
  } else if (g_ui.card != CARD_NONE) {
    if (!soft || x4Zone != X4_ZONE_CARD) {
      x4Zone = X4_ZONE_CARD;
      x4Item = g_ui.card == CARD_MARKET && g_ui.selectedGood < GOOD_COUNT ? (int8_t)g_ui.selectedGood : 0;
    }
    else if (x4KeyFooter || g_ui.card == CARD_MARKET) x4_relocate_focus();
      // Trading can complete guidance and unlock features. Keep the chosen
      // good/action so the next sell or buy still refers to that same cargo.
    else x4Item = 0;                             // a new page starts at its first row
  } else if (!soft || x4Zone != X4_ZONE_SCENE) {
    bool fromCard = x4Zone != X4_ZONE_SCENE;
    x4Zone = X4_ZONE_SCENE; x4Item = 0;
    // Closing a card puts the cursor back on the button that opened it.
    if (fromCard) x4_relocate_focus();
  } else {
    x4_relocate_focus();
  }
  if (g_ui.card == CARD_VOYAGE) {
    if (g_ui.page == 5) x4Item = refitSlot;
    else if (g_ui.page == 1) x4Item = voyageDraft.mode == VOYAGE_TRADE ? 1 : 0;
    else if (g_ui.page == 0 && voyageDestination < MAX_PORTS) {
      for (int row = 0; row < VOYAGE_DESTINATION_ROWS; ++row)
        if (x4_voyage_destination(row) == voyageDestination) x4Item = (int8_t)row;
    }
    else if (g_ui.page == 2 || g_ui.page == 3 || g_ui.page == 4) x4Item = (int8_t)x4_card_index_of_footer(1);
  }
  if (g_ui.card == CARD_VOYAGE_LOAD && !soft) {
    for (int row = 0; row < 2; ++row)
      if (x4_load_good(row) == g_ui.selectedGood) x4Item = (int8_t)row;
  }
  if (g_ui.card == CARD_VOYAGE_DEPARTURE && x4_next_ready_ship() == NO_SHIP) x4Item = 1;
  x4_remember_focus();
  // First-voyage guidance moves the cursor onto the hinted control.
  if (!sim_tutorial_guided()) return;
  uint8_t step = g_campaign.tutorialStep;
  bool shipSelected = g_ui.selectedShip < g_campaign.shipCount;
  if (g_ui.card == CARD_ROUTE && g_ui.page == 0 && step == TUTORIAL_DISPATCH) {
    int index = x4_card_index_of_footer(1);
    if (index >= 0) { x4Zone = X4_ZONE_CARD; x4Item = (int8_t)index; }
  } else if (g_ui.card == CARD_NONE && g_ui.scene == SCENE_PORT && step == TUTORIAL_CONTRACT) {
    int slot = x4_column_slot(X4_CMD_JOBS);
    if (slot >= 0) { x4Zone = X4_ZONE_SCENE; x4Item = (int8_t)slot; }
  } else if (g_ui.card == CARD_NONE && g_ui.scene == SCENE_PORT && step == TUTORIAL_ROUTE && shipSelected) {
    int slot = x4_column_slot(X4_CMD_ROUTE);
    if (slot >= 0) { x4Zone = X4_ZONE_SCENE; x4Item = (int8_t)slot; }
  }
  x4_remember_focus();
}

// Re-anchor the cursor after the game state moved underneath it.
static bool x4_focus_card_item(uint8_t footer, uint8_t id) {
  if (x4Zone != X4_ZONE_CARD) return false;
  X4CardItem items[X4_CARD_MAX];
  int n = x4_card_items(items);
  for (int i = 0; i < n; ++i) if (items[i].footer == footer && items[i].id == id) {
    x4Item = (int8_t)i; x4_remember_focus(); return true;
  }
  return false;
}

static void x4_onboarding_focus() {
  const uint8_t step = sim_onboarding_step();
  if (step < ONBOARD_BUY || step == ONBOARD_VOYAGE || g_ui.selectedShip != 0) return;
  if (g_ui.card == CARD_NONE && x4Zone == X4_ZONE_SCENE && g_ui.scene == SCENE_FLEET && !watchActive) {
    int slot = x4_column_slot(X4_CMD_SHIP_0);
    if (slot >= 0) { x4Item = (int8_t)slot; x4_remember_focus(); }
    return;
  }
  if (g_ui.card == CARD_SHIP) { x4_focus_card_item(0, step == ONBOARD_SAIL ? 1 : 0); return; }
  if (g_ui.card == CARD_TRADE) {
    if (x4_merchant_editing()) x4_focus_card_item(1, 1);           // BUY / SELL
    else if (step == ONBOARD_SAIL) x4_focus_card_item(1, 2);       // back to the ship
    else x4_focus_card_item(0, 0);                                 // the good
    return;
  }
  if (g_ui.card == CARD_VOYAGE && step == ONBOARD_SAIL) {
    if (g_ui.page == 0) x4_focus_card_item(0, 0);                  // Gullhaven
    else if (g_ui.page == 2) x4_focus_card_item(1, 1);             // SAIL
  }
}

static void x4_sync() {
  static bool seenWatch = false;
  if (g_ui.card == CARD_MANIFEST) x4_manifest_clamp();
  bool marketSelectionChanged = g_ui.card == CARD_MARKET && g_ui.selectedGood != x4SeenGood;
  bool structural = g_ui.title != x4SeenTitle || g_ui.scene != x4SeenScene ||
                    g_ui.card != x4SeenCard || g_ui.captainView != x4SeenView || seenWatch != watchActive;
  static uint8_t seenOnboard = 0xFF;
  bool soft = g_ui.page != x4SeenPage || g_campaign.tutorialStep != x4SeenStep ||
              sim_onboarding_step() != seenOnboard;
  if (structural || soft) {
    seenOnboard = sim_onboarding_step();
    seenWatch = watchActive;
    bool returnRules = !structural && g_ui.card == CARD_ROUTE && g_ui.page == 0 && x4SeenPage == 1;
    x4SeenTitle = g_ui.title; x4SeenScene = g_ui.scene; x4SeenCard = g_ui.card;
    x4SeenPage = g_ui.page; x4SeenView = g_ui.captainView;
    x4SeenStep = g_campaign.tutorialStep;
    x4_reset_focus(!structural);
    if (returnRules) {
      X4CardItem items[X4_CARD_MAX];
      int n = x4_card_items(items);
      for (int i = 0; i < n; ++i)
        if (!items[i].footer && items[i].id == 5) { x4Item = (int8_t)i; break; }
      x4_remember_focus();
    }
    x4_onboarding_focus();
    x4SeenGood = g_ui.selectedGood;
    return;
  }
  x4_relocate_focus();
  if (marketSelectionChanged && g_ui.selectedGood < GOOD_COUNT) {
    // Mouse previews and restored selections use the same visible focus as
    // physical row navigation, including goods 6 and 7.
    x4Item = (int8_t)g_ui.selectedGood;
    x4_remember_focus();
  }
  x4SeenGood = g_ui.selectedGood;
}

// The footer slot the physical BACK button stands for: the on-screen BACK in
// the captain detail and recruitment views, CLOSE everywhere else.
static uint8_t x4_back_footer_id() {
  return g_ui.card == CARD_CAPTAINS && g_ui.captainView != 0 ? 1 : 2;
}

static void x4_activate_card_item(const X4CardItem& item) {
  if (item.footer == 2) { x4_run_command(item.id); return; }
  if (g_ui.card == CARD_ROUTE && g_ui.page == 1 && !item.footer && item.id == 4) {
    apply_result(SIM_USE_ROCKER);  // a tap in the middle of the price row means nothing
    g_ui.dirty = 1;
    return;
  }
  bool needsDockedShip = (g_ui.card == CARD_JOBS && !item.footer) ||
                         (g_ui.card == CARD_MARKET && item.footer && item.id < 2);
  if (needsDockedShip && sim_selected_docked_ship() < 0) {
    apply_result(SIM_NO_SHIP_HERE);
    g_ui.dirty = 1;
    return;
  }
  bool routeDraft = g_ui.card == CARD_ROUTE && g_ui.page == 0;
  uint8_t previousLegs = g_ui.routeCount;
  if (item.footer) handle_footer(item.id);
  else {
    HitRegion r = voyage_card_row_region(g_ui.card, g_ui.page, item.id, g_ui.captainView);
    handle_card_row(item.id, r.x + r.w / 2);
    if (g_ui.card == CARD_MARKET) {
      // Selecting a good leads to its explicit trade controls. A second OK
      // buys one; merely browsing goods never spends coins.
      x4Item = (int8_t)x4_card_index_of_footer(0);
      x4_remember_focus();
    }
  }
  if (routeDraft && g_ui.card == CARD_ROUTE && g_ui.page == 0 && g_ui.routeCount != previousLegs) {
    // An added leg is ready to sail. Undo returns to destinations rather than
    // leaving the cursor on a footer that may have changed into RECALL.
    x4Item = !item.footer && g_ui.routeCount > previousLegs ?
      (int8_t)x4_card_index_of_footer(1) : 0;
    x4_remember_focus();
  }
}

// Unlocked ports ordered north to south (then west to east).
static void x4_cycle_port(int dir) {
  render_map_stop_follow();
  uint8_t order[MAX_PORTS];
  int n = 0;
  for (int p = 0; p < MAX_PORTS; ++p) {
    if (!g_ports[p].unlocked) continue;
    int i = n;
    while (i > 0) {
      const PortDef& before = PORT_DEFS[order[i - 1]];
      const PortDef& here = PORT_DEFS[p];
      if (before.y < here.y || (before.y == here.y && before.x <= here.x)) break;
      order[i] = order[i - 1];
      --i;
    }
    order[i] = (uint8_t)p;
    ++n;
  }
  if (!n) return;
  int at = -1;
  for (int i = 0; i < n; ++i) if (order[i] == g_ui.selectedPort) at = i;
  int next = at < 0 ? (dir > 0 ? 0 : n - 1) : (at + dir + n) % n;
  uint8_t port = order[next];
  g_ui.selectedPort = port;
  if(g_campaign.economyVersion<2)select_ship_at_port(port);
  if (g_ui.mapZoom > 1) {
    render_map_focus_port(port);
  }
}

// The rocker: ports, ships, pages, tabs, or the price on the rules row.
static void x4_page(int dir) {
  if (x4_return_report() && g_ui.card == CARD_NONE) return;
  if (g_ui.card == CARD_NONE) {
    if(g_campaign.economyVersion>=2 && g_ui.scene==SCENE_PORT)return;
    const uint8_t tabs[] = {X4_CMD_FLEET, X4_CMD_MAP, X4_CMD_COMPANY};
    int at = watchActive ? 0 : g_ui.scene == SCENE_WORLD || g_ui.scene == SCENE_PORT ? 1 : g_ui.scene == SCENE_COMPANY ? 2 : 0;
    x4_run_command(tabs[(at + dir + 3) % 3]);
    return;
  }
  switch (g_ui.card) {
    case CARD_RETURN_DETAILS: x4_return_footer(dir < 0 ? 0 : 1); break;
    case CARD_TRADE: case CARD_PACKAGES:
      // On the guided voyage the rocker swaps Buy and Sell; Parcels come later.
      x4_merchant_open(sim_onboarding_step() >= ONBOARD_BUY ? 1 - min(1, (int)x4_merchant_mode()) :
                       merchant_next_tab(x4_merchant_mode(), dir));
      break;
    case CARD_VOYAGE_LOAD: x4_voyage_quantity(dir); break;
    case CARD_VOYAGE:
      if (g_ui.page == 0) x4_voyage_footer(dir < 0 ? 0 : 1);
      break;
    case CARD_MORE: case CARD_JOBS: case CARD_SHIPYARD: handle_footer(dir < 0 ? 0 : 1); break;
    case CARD_MANIFEST: {
      int pages = (x4_manifest_count() + CARD_VISIBLE_ROWS - 1) / CARD_VISIBLE_ROWS;
      if (pages > 1) {
        int page = (g_ui.routeScroll / CARD_VISIBLE_ROWS + dir + pages) % pages;
        g_ui.routeScroll = (uint8_t)(page * CARD_VISIBLE_ROWS);
        x4Item = 0;
        x4_remember_focus();
      }
      break;
    }
    case CARD_JOURNAL: handle_footer(dir < 0 ? 1 : 0); break;  // NEWER / OLDER
    case CARD_FLEET: {
      int pages = (g_campaign.shipCount + CARD_VISIBLE_ROWS - 1) / CARD_VISIBLE_ROWS;
      if (pages > 1) g_ui.page = (uint8_t)((g_ui.page + dir + pages) % pages);
      break;
    }
    case CARD_INBOX: {
      int pages = (attention_visible_count() + CARD_VISIBLE_ROWS - 1) / CARD_VISIBLE_ROWS;
      if (pages > 1) g_ui.page = (uint8_t)((g_ui.page + dir + pages) % pages);
      break;
    }
    case CARD_MARKET: {
      // Left sells, right buys, five units of the selected good per press;
      // stops at the first refusal and reports it only if nothing traded.
      int si = sim_selected_docked_ship();
      if (si < 0) { apply_result(SIM_NO_SHIP_HERE); break; }
      int traded = 0;
      int32_t coinsBefore = g_campaign.coins;
      SimResult last = SIM_OK;
      for (int k = 0; k < 5; ++k) {
        SimResult r = dir < 0 ? sim_market_sell((uint8_t)si, g_ui.selectedGood)
                              : sim_market_buy((uint8_t)si, g_ui.selectedGood);
        if (!r) { last = r; break; }
        ++traded;
      }
      if (traded) {
        journal_add(0, "%s %d %s / %+ld coins.", dir < 0 ? "Sold" : "Bought", traded,
                    GOOD_DEFS[g_ui.selectedGood].name, (long)(g_campaign.coins - coinsBefore));
        sim_tutorial_complete(TUTORIAL_MARKET); save_write(); g_ui.dirty = 1;
      }
      else apply_result(last);
      break;
    }
    case CARD_ATLAS:
      g_ui.page = (uint8_t)((g_ui.page + dir + REGION_COUNT) % REGION_COUNT);
      break;
    case CARD_CAPTAINS:
      if (g_ui.captainView == 0) {
        int pages = (g_campaign.captainCount + CARD_VISIBLE_ROWS - 1) / CARD_VISIBLE_ROWS;
        if (pages > 1) g_ui.page = (uint8_t)((g_ui.page + dir + pages) % pages);
      }
      break;
    case CARD_ROUTE:
      if (x4_price_row_focused())
        handle_card_row(4, dir < 0 ? CARD_X + 10 : CARD_X + CARD_W - 10);
      else if (g_ui.page == 0) {
        int count = x4_route_candidate_count();
        if (count > ROUTE_VISIBLE_ROWS) {
          int pages = (count + ROUTE_VISIBLE_ROWS - 1) / ROUTE_VISIBLE_ROWS;
          int page = (g_ui.routeScroll / ROUTE_VISIBLE_ROWS + dir + pages) % pages;
          g_ui.routeScroll = (uint8_t)(page * ROUTE_VISIBLE_ROWS);
          x4Item = 0;
          x4_remember_focus();
        }
      }
      break;
    default: break;
  }
}

bool x4_page_keys_active() {
  if (g_ui.title) return false;
  switch (g_ui.card) {
    case CARD_RETURN_DETAILS: return x4_return_row_count() > 3;
    case CARD_TRADE: return true;
    case CARD_PACKAGES: return true;
    case CARD_VOYAGE_LOAD: return true;
    case CARD_VOYAGE: return
      (g_ui.page == 0 && x4_voyage_destination_count() > VOYAGE_DESTINATION_ROWS);
    case CARD_MORE: return voyage_detail_count() > CARD_VISIBLE_ROWS;
    case CARD_NONE: return true;
    case CARD_SHIPYARD: return !merchantUpgradeReturn;
    case CARD_JOBS: case CARD_JOURNAL: return true;
    case CARD_MANIFEST: return x4_manifest_count() > CARD_VISIBLE_ROWS;
    case CARD_FLEET: return g_campaign.shipCount > CARD_VISIBLE_ROWS;
    case CARD_INBOX: return attention_visible_count() > CARD_VISIBLE_ROWS;
    case CARD_MARKET: return sim_selected_docked_ship() >= 0;
    case CARD_ATLAS: return true;
    case CARD_CAPTAINS: return g_ui.captainView == 0 && g_campaign.captainCount > CARD_VISIBLE_ROWS;
    case CARD_ROUTE:
      return x4_price_row_focused() || (g_ui.page == 0 && x4_route_candidate_count() > ROUTE_VISIBLE_ROWS);
    default: return false;
  }
}

static void x4_press(uint8_t button) {
  bool returning = (uint32_t)(millis() - x4LastInputMs) >= X4_RETURN_IDLE_MS;
  x4LastInputMs = millis();
  x4InputPending = true;
  x4LastPressed = button;
  ++x4PressCount;
  g_ui.dirty = 1;
#if defined(LGFX_SDL)
  if (board_x4_asleep()) { board_x4_host_wake(); return; }
#endif
  uint8_t action = X4_ACTION_OF_BUTTON[button];
  if (action == X4_ACT_POWER) { g_ui.returnQuiet = 1; board_x4_power_off(); return; }
  if (goalMomentActive) {
    if (action == X4_ACT_OK || action == X4_ACT_BACK) x4_goal_moment_close();
    return;  // the other keys cannot act on the screen hidden underneath
  }
  if (storyMomentMode) {
    if (action == X4_ACT_OK || action == X4_ACT_BACK) x4_story_moment_close();
    return;
  }
  if (x4_onboarding_screen()) { x4_onboarding_press(action); return; }
  x4_sync();
  if (returning && !g_ui.title && !x4_return_report() && sim_return_has_updates()) {
    x4_prepare_return(false);
    return;  // the returning press opens the update; it cannot buy or dispatch
  }
  if (x4_return_report() && g_ui.card == CARD_NONE && action == X4_ACT_BACK) {
    x4_finish_return(true);
    return;
  }
  if (g_ui.title) { if (action == X4_ACT_OK) handle_touch(IW / 2, IH / 2); return; }
  if (x4Zone == X4_ZONE_INCIDENT) {
    int count=g_campaign.economyVersion>=2?encounter_choices(g_incident.kind):3;
    if(action==X4_ACT_BACK && g_campaign.economyVersion>=2) {if(!x4_nav_back()){g_ui.card=CARD_NONE;x4_watch_ship();}return;}
    if (action == X4_ACT_UP) x4Item = (int8_t)((x4Item + count-1) % count);
    else if (action == X4_ACT_DOWN) x4Item = (int8_t)((x4Item + 1) % count);
    else if (action == X4_ACT_OK) {
      handle_incident_choice((uint8_t)x4Item);
    }
    return;
  }
  if(g_ui.card==CARD_TUTORIAL && g_campaign.economyVersion>=2) {
    if(action==X4_ACT_BACK || action==X4_ACT_PAGE_PREV) {handle_footer(2);return;}
    if(action==X4_ACT_PAGE_NEXT) {if(g_ui.page+1<MERCHANT_TUTORIAL_PAGES)handle_footer(0);return;}
  }
  if (action == X4_ACT_PAGE_PREV) { x4_page(-1); return; }
  if (action == X4_ACT_PAGE_NEXT) { x4_page(1); return; }
  if (merchant_card(g_ui.card) && x4_merchant_editing()) {
    if (action == X4_ACT_UP || action == X4_ACT_DOWN) {
      if (g_ui.card == CARD_TRADE) x4_merchant_quantity(action == X4_ACT_UP ? 1 : -1);
    } else if (action == X4_ACT_OK) x4_merchant_footer(1);
    else if (action == X4_ACT_BACK) x4_merchant_footer(2);
    return;
  }
  if (g_ui.card != CARD_NONE) {
    X4CardItem items[X4_CARD_MAX];
    int n = x4_card_items(items);
    if (action == X4_ACT_BACK) handle_footer(x4_back_footer_id());
    else if (!n) return;
    else if (action == X4_ACT_UP || action == X4_ACT_DOWN) {
      if (g_ui.card == CARD_MARKET && action == X4_ACT_UP &&
          items[x4Item].footer && items[x4Item].id == 0)
        x4Item = (int8_t)g_ui.selectedGood;
      else x4Item = (int8_t)((x4Item + (action == X4_ACT_UP ? n - 1 : 1)) % n);
      if (g_ui.card == CARD_MENU) g_ui.confirmReset = 0;  // moving off RESET disarms it
      x4_remember_focus();
    }
    else if (action == X4_ACT_OK) { x4_activate_card_item(items[x4Item]); x4_relocate_focus(); }
    return;
  }
  uint8_t column[COLUMN_MAX];
  int n = x4_column(column);
  switch (action) {
    case X4_ACT_UP: x4Item = (int8_t)((x4Item + n - 1) % n); x4_remember_focus(); break;
    case X4_ACT_DOWN: x4Item = (int8_t)((x4Item + 1) % n); x4_remember_focus(); break;
    case X4_ACT_OK: x4_run_command(column[x4Item]); break;
    case X4_ACT_BACK:
      if(g_campaign.economyVersion>=2){if(!x4_nav_back())open_card(CARD_MENU);}
      else if (g_ui.scene == SCENE_WORLD) x4_watch_back(); else open_card(CARD_MENU);
      break;
    default: break;
  }
}

static void x4_sync_input_context() {
  // Drop presses queued while another card, title or scene was opening
  // (including automatic arrivals and incidents). Keeping the held state stops
  // a long OK from activating the new screen.
  static uint64_t seenContext = UINT64_MAX;
  static bool seenReturn = false;
  static uint32_t seenReport = 0;
  uint64_t context = ((uint64_t)g_ui.routeScroll << 56) |
                     ((uint64_t)watchActive << 55) |
                     ((uint64_t)g_ui.title << 48) | ((uint64_t)g_ui.scene << 40) |
                     ((uint64_t)g_ui.card << 32) | ((uint64_t)g_ui.captainView << 24) |
                     ((uint64_t)g_ui.page << 16) | ((uint64_t)g_ui.selectedPort << 8) |
                     g_ui.selectedShip;
  const ReturnReport* report = x4_return_report();
  if (context != seenContext || seenReturn != (report != nullptr) ||
      seenReport != (report ? report->fingerprint : 0)) {
    board_x4_discard_button_events();
    seenContext = context;
    seenReturn = report != nullptr;
    seenReport = report ? report->fingerprint : 0;
  }
}

void x4_input_ready() {
  // The title already invites OK while its two e-paper stamps are running.
  // Remember that one intentional start, then absorb the rest of the gesture
  // so held/repeated OK cannot activate the Fleet action behind the title.
  bool startRequested = false;
  if (board_x4_buffered_input()) {
    startRequested = g_ui.title && board_x4_button_pending(X4_BTN_CONFIRM);
    board_x4_discard_button_events();
    x4_sync_input_context();
  }
  else {
    x4Candidate = x4Stable = board_x4_read_buttons();
    x4CandidateCount = 0;
    startRequested = g_ui.title && (x4Stable & (1u << X4_BTN_CONFIRM));
  }
  x4LastInputMs = millis();
  if (startRequested) {
    x4_press(X4_BTN_CONFIRM);
    x4_sync_input_context();
  }
}

static void x4_input_update() {
  if (board_x4_buffered_input()) {
    x4_sync_input_context();
    int event = board_x4_take_button_event();
    if (event >= 0) x4_press((uint8_t)event);
    return;
  }
  // SDL scripts use the synchronous virtual GPIO path. This also keeps the
  // device usable, with an explicit warning, if sampler startup ever fails.
  uint8_t sample = board_x4_read_buttons();
  if (sample != x4Candidate) { x4Candidate = sample; x4CandidateCount = 0; }
  if (x4CandidateCount < 2) ++x4CandidateCount;
  if (x4CandidateCount == 2 && x4Candidate != x4Stable) {
    uint8_t pressed = (uint8_t)(x4Candidate & ~x4Stable);
    // Releases settle at once; of several new presses only one runs per poll
    // (POWER first), so a frame is drawn between two actions and the other
    // press stays pending rather than being swallowed.
    uint8_t serviced = 0;
    if (pressed & (1u << X4_BTN_POWER)) serviced = 1u << X4_BTN_POWER;
    else if (pressed) serviced = (uint8_t)(pressed & (uint8_t)(0u - pressed));
    x4Stable = (uint8_t)((x4Stable & x4Candidate) | serviced);
    for (uint8_t b = 0; b < X4_BTN_COUNT; ++b)
      if (serviced & (1u << b)) x4_press(b);
  }
}

bool x4_focus_region(HitRegion* out) {
  if (g_ui.title) return false;
  x4_sync();
  if (x4Zone == X4_ZONE_SCENE) {
    uint8_t column[COLUMN_MAX];
    int n = x4_column(column);
    if (!n) return false;
    *out = voyage_scene_region(column[x4Item], x4Item);
    return true;
  }
  if (x4Zone == X4_ZONE_CARD) {
    X4CardItem items[X4_CARD_MAX];
    int n = x4_card_items(items);
    if (!n) return false;
    const X4CardItem& item = items[x4Item];
    *out = item.footer == 2 ? voyage_scene_region(item.id, 0) : item.footer == 1 ?
      voyage_footer_region(g_ui.card, item.id) : voyage_card_row_region(g_ui.card, g_ui.page, item.id, g_ui.captainView);
    return true;
  }
  *out = incident_option_region(x4Item);
  return true;
}

#if defined(SH_HOST_TESTS)
bool input_test_focus_command(uint8_t command) {
  x4_sync();
  if (g_ui.title) return false;
  if (x4Zone == X4_ZONE_CARD) {
    X4CardItem items[X4_CARD_MAX];
    int n = x4_card_items(items);
    for (int i = 0; i < n; ++i) if (items[i].footer == 2 && items[i].id == command) {
      x4Item = (int8_t)i; x4_remember_focus(); g_ui.dirty = 1; return true;
    }
    return false;
  }
  if (x4Zone != X4_ZONE_SCENE) return false;
  int slot = x4_column_slot(command);
  if (slot < 0) return false;
  x4Item = (int8_t)slot;
  x4_remember_focus();
  g_ui.dirty = 1;
  return true;
}

bool input_test_focus_row(int row) {
  x4_sync();
  if (g_ui.title || x4Zone != X4_ZONE_CARD) return false;
  X4CardItem items[X4_CARD_MAX];
  int n = x4_card_items(items);
  for (int i = 0; i < n; ++i) if (!items[i].footer && items[i].id == row) {
    x4Item = (int8_t)i;
    x4_remember_focus();
    g_ui.dirty = 1;
    return true;
  }
  return false;
}
#endif

uint32_t x4_focus_fingerprint() {
  if (!g_ui.title) x4_sync();
  return ((uint32_t)x4Zone << 24) | ((uint32_t)(uint8_t)x4Item << 16);
}

bool x4_take_input_pending() {
  bool pending = x4InputPending;
  x4InputPending = false;
  return pending;
}

uint32_t x4_last_input_ms() { return x4LastInputMs; }
#if defined(SH_HOST_TESTS)
void input_test_set_last_interaction_ms(uint32_t when) {
  x4LastInputMs = when;
  g_ui.returnQuiet = x4_return_report() || (uint32_t)(millis() - when) >= X4_RETURN_IDLE_MS;
}
#endif
int x4_last_pressed() { return x4LastPressed; }
uint32_t x4_press_count() { return x4PressCount; }

void input_update() {
  g_ui.returnQuiet = g_ui.title || x4_return_report() || board_x4_asleep() ||
    voyage_dispatch_card(g_ui.card, g_ui.page) ||
    (uint32_t)(millis() - x4LastInputMs) >= X4_RETURN_IDLE_MS;
  // Arrival acknowledges only its receipt and opens a neutral ship overview.
  // This transition only handles the arrival card; another ship's draft is quiet.
  if (g_ui.card == CARD_ARRIVAL && g_ui.arrivalSummaryShip < g_campaign.shipCount &&
      g_ships[g_ui.arrivalSummaryShip].merchant.state == MERCHANT_ARRIVED) {
    uint8_t index = g_ui.arrivalSummaryShip;
    if (watchActive && index != g_ui.selectedShip) {
      // Another arrival must not steal the watched boat or acknowledge its news.
      g_ui.card = CARD_NONE; g_ui.scene = SCENE_WORLD; g_ui.dirty = 1;
    } else if (apply_result(sim_ack_arrival_receipt(index, g_ships[index].merchant.id))) {
      if (watchActive) {
        g_ui.card = CARD_NONE; g_ui.scene = SCENE_WORLD; g_ui.dirty = 1;
      } else { g_ui.selectedShip = index; x4_merchant_ship(); }
    }
  }
  if(g_campaign.economyVersion>=2 && g_ui.card==CARD_INCIDENT &&
     g_ui.selectedShip<g_campaign.shipCount && g_ships[g_ui.selectedShip].state!=SHIP_AWAITING_DECISION) {
    if(!x4_nav_back()){g_ui.card=CARD_NONE;x4_open_voyage();}board_x4_discard_button_events();
  }
  x4_onboarding_tick();
  x4_goal_moment_update();
  x4_story_moment_update();
  x4_input_update();
  int32_t px = 0, py = 0;
  bool down;
#ifdef SH_HOST_TESTS
  down = sh_test_touch(&px, &py);
  if (!down) down = display.getTouch(&px, &py);
#else
  down = display.getTouch(&px, &py);
#endif
  if (down) {
    int x = px / SCALE;
    int y = py / SCALE;
    if (x < 0) x = 0; if (x >= IW) x = IW - 1;
    if (y < 0) y = 0; if (y >= IH) y = IH - 1;
    if (!lastDown) handle_touch(x, y);
    else if (mapTouchActive) handle_map_touch_move(x, y);
  } else if (lastDown) {
    handle_map_touch_release();
  }
  lastDown = down;
}
