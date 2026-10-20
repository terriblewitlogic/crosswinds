#pragma once
// Parent frames contain navigation context only, never copies of the company,
// cargo, prices, voyage progress, or committed transaction drafts.
struct X4NavFrame {
  uint8_t scene,card,port,ship,good,page,view,captain,scroll,zoom,focusX,focusY;
  int8_t focus;
  bool watch,upgrades;
};
struct X4NavTrail { X4NavFrame frames[12]{}; uint8_t count=0; uint32_t epoch=0; };
static X4NavTrail navTrail{},returnPreviousNav{};
static X4NavFrame worldRootView{};static bool worldRootValid=false;
static uint8_t nav_family(uint8_t card) {return card==CARD_PACKAGES?CARD_TRADE:card;}
static void x4_nav_validate() {
  if(navTrail.epoch!=sim_return_epoch()){navTrail={};navTrail.epoch=sim_return_epoch();}
}
static void x4_nav_reset() {
  if(navTrail.epoch!=sim_return_epoch())worldRootValid=false;
  else if(g_ui.scene==SCENE_WORLD && g_ui.card==CARD_NONE && !watchActive) {
    worldRootView.port=g_ui.selectedPort;worldRootView.zoom=g_ui.mapZoom;
    worldRootView.focusX=g_ui.mapFocusX;worldRootView.focusY=g_ui.mapFocusY;worldRootValid=true;
  }
  navTrail={};navTrail.epoch=sim_return_epoch();
}
static void x4_nav_restore_world() {
  if(!worldRootValid || (navTrail.count && navTrail.frames[navTrail.count-1].watch))return;
  g_ui.selectedPort=worldRootView.port;g_ui.mapZoom=worldRootView.zoom;
  g_ui.mapFocusX=worldRootView.focusX;g_ui.mapFocusY=worldRootView.focusY;
}
static X4NavFrame x4_nav_frame() {
  return {g_ui.scene,g_ui.card,g_ui.selectedPort,g_ui.selectedShip,g_ui.selectedGood,g_ui.page,
    g_ui.captainView,g_ui.selectedCaptain,g_ui.routeScroll,g_ui.mapZoom,g_ui.mapFocusX,g_ui.mapFocusY,
    x4Item,watchActive,merchantUpgradeReturn};
}
static void x4_nav_push() {
  if(g_campaign.economyVersion<2 || navOpeningSubject)return;
  x4_nav_validate();
  X4NavFrame f=x4_nav_frame();
  if(navTrail.count) {
    const auto& p=navTrail.frames[navTrail.count-1];
    if(p.scene==f.scene && nav_family(p.card)==nav_family(f.card) && p.watch==f.watch && (p.port==f.port || f.scene==SCENE_FLEET || f.card==CARD_PORT_SHIPS))return;
  }
  if(navTrail.count==12) {memmove(navTrail.frames,navTrail.frames+1,11*sizeof(X4NavFrame));--navTrail.count;}
  navTrail.frames[navTrail.count++]=f;
}
static void x4_nav_enter(uint8_t target) {
  if(g_campaign.economyVersion<2 || navOpeningSubject || g_ui.title || target==CARD_RETURN_DETAILS || x4_return_report())return;
  x4_nav_validate();
  if(nav_family(target)==nav_family(g_ui.card))return;
  // Returning to an existing ancestor replaces completed child tasks.
  for(int i=navTrail.count-1;i>=0;--i)if(nav_family(navTrail.frames[i].card)==nav_family(target)) {
    navTrail.count=(uint8_t)i;return;
  }
  if(g_ui.card==CARD_INCIDENT || g_ui.card==CARD_OUTCOME)return;
  if(navTrail.count && g_ui.card==CARD_NONE) {
    const auto& p=navTrail.frames[navTrail.count-1];
    if(p.card==CARD_NONE && p.scene==g_ui.scene)return;
  }
  x4_nav_push();
}
static bool x4_nav_back() {
  if(g_campaign.economyVersion<2 || x4_return_report())return false;
  x4_nav_validate();
  if(!navTrail.count)return false;
  X4NavFrame f=navTrail.frames[--navTrail.count];
  g_ui.scene=f.scene;g_ui.card=f.card;g_ui.selectedPort=f.port;g_ui.selectedShip=f.ship;
  g_ui.selectedGood=f.good;g_ui.page=f.page;g_ui.captainView=f.view;g_ui.selectedCaptain=f.captain;
  g_ui.routeScroll=f.scroll;g_ui.mapZoom=f.zoom;g_ui.mapFocusX=f.focusX;g_ui.mapFocusY=f.focusY;
  watchActive=f.watch;merchantUpgradeReturn=f.upgrades;
  if(f.card==CARD_SHIP && f.ship<g_campaign.shipCount) {
    const Ship& ship=g_ships[f.ship];g_ui.selectedPort=ship.currentPort;
    if(ship.state==SHIP_SAILING || ship.state==SHIP_AWAITING_DECISION){g_ui.card=CARD_NONE;g_ui.scene=SCENE_WORLD;watchActive=true;}
  }
  if(watchActive)render_map_follow_selected_ship();else render_map_stop_follow();
  x4Zone=g_ui.card==CARD_NONE?X4_ZONE_SCENE:X4_ZONE_CARD;x4Item=f.focus;
  x4SeenTitle=g_ui.title;x4SeenScene=g_ui.scene;x4SeenCard=g_ui.card;
  x4SeenPage=g_ui.page;x4SeenView=g_ui.captainView;x4SeenStep=g_campaign.tutorialStep;
  int count=0;
  if(g_ui.card==CARD_NONE){uint8_t commands[COLUMN_MAX];count=x4_column(commands);}
  else {uint8_t rows[X4_CARD_ROW_MAX];count=x4_rows(rows);uint8_t mask=x4_footer_mask();for(int bit=0;bit<3;++bit)count+=(mask>>bit)&1;}
  x4Item=(int8_t)max(0,min((int)x4Item,count-1));x4_remember_focus();
  x4_onboarding_focus();  // the guided voyage points at its next control
  g_ui.dirty=1;board_x4_discard_button_events();return true;
}
static void x4_nav_departed() {
  x4_nav_validate();
  // Strip ship/task frames; a committed Sail can never be reached by Back.
  while(navTrail.count && navTrail.frames[navTrail.count-1].card!=CARD_PORT_SHIPS &&
    navTrail.frames[navTrail.count-1].card!=CARD_NONE)--navTrail.count;
  if(navTrail.count)x4_nav_back();
  else {g_ui.card=CARD_NONE;g_ui.scene=SCENE_FLEET;g_ui.page=g_ui.selectedShip/VOYAGE_FLEET_ROWS;}
}
// Port lists resolve their own subject and never use the fleet selection.
uint8_t x4_port_ship(int row) {
  int wanted=g_ui.page*CARD_VISIBLE_ROWS+row;
  for(uint8_t i=0;i<g_campaign.shipCount;++i) {
    const Ship& s=g_ships[i];bool sailing=s.state==SHIP_SAILING || s.state==SHIP_AWAITING_DECISION;
    bool match=g_ui.captainView>=2 ? (g_ui.captainView==3?!sailing && s.classId==0:true) :
      g_ui.captainView ? sailing && s.merchant.destination==g_ui.selectedPort : !sailing && s.currentPort==g_ui.selectedPort;
    if(match && wanted--==0)return i;
  }
  return NO_SHIP;
}

uint8_t x4_yard_port(int row) {
  int wanted=g_ui.page*CARD_VISIBLE_ROWS+row;
  for(uint8_t p=0;p<MAX_PORTS;++p)if(g_ports[p].unlocked && wanted--==0)return p;
  return NO_PORT;
}

static bool x4_nav_parent_is(uint8_t card){x4_nav_validate();return navTrail.count && navTrail.frames[navTrail.count-1].card==card;}
static void x4_nav_drop_parent(uint8_t card){if(x4_nav_parent_is(card))--navTrail.count;}
