// Included inside input.cpp's X4 implementation.
struct MerchantUiDraft {
  MerchantTradeQuote trade{};
  MerchantSailQuote sail{};
  uint8_t quantities[GOOD_COUNT]{};
  bool selling = false;
  bool editing = false;
};
static MerchantUiDraft merchantDraft{};
bool x4_merchant_editing() { return merchantDraft.editing; }
bool x4_ship_upgrades() { return merchantUpgradeReturn; }
const MerchantTradeQuote& x4_merchant_trade_quote() { return merchantDraft.trade; }
const MerchantSailQuote& x4_merchant_sail_quote() { return merchantDraft.sail; }
const uint8_t* x4_merchant_quantities() { return merchantDraft.quantities; }
uint8_t x4_merchant_mode() { return g_ui.card == CARD_PACKAGES ? 2 : merchantDraft.selling ? 1 : 0; }
static int merchant_good_at(int index) {
  if (g_ui.selectedShip >= g_campaign.shipCount) return -1;
  const Ship& ship = g_ships[g_ui.selectedShip];
  int count = 0;
  for (int good = 0; good < GOOD_COUNT; ++good) {
    bool eligible = false;
    if (!merchantDraft.selling) eligible = PORT_DEFS[ship.currentPort].produced[0] == good || PORT_DEFS[ship.currentPort].produced[1] == good;
    // The guided first voyage trades Textiles only.
    if (eligible && sim_onboarding_step() >= ONBOARD_BUY && good != GOOD_TEXTILES) eligible = false;
    else for (int c = 0; c < ship.cargoCount; ++c)
      if (ship.cargo[c].kind == CARGO_MARKET && ship.cargo[c].good == good) eligible = true;
    if (eligible && count++ == index) return good;
  }
  return -1;
}
int x4_merchant_good(int row) { return merchant_good_at(g_ui.page * 2 + row); }
// The Grey Passenger's offer, when this ship has one, is the first Parcels row.
static int merchant_story_shift() {
  StoryOffer offer{};
  return g_ui.card == CARD_PACKAGES && sim_story_offer(g_ui.selectedShip, offer) ? 1 : 0;
}
bool x4_merchant_story_row(int row) { return merchant_story_shift() && g_ui.page * 2 + row == 0; }
bool x4_merchant_story_selected() { return merchant_story_shift() && g_ui.selectedGood == 0; }
uint8_t x4_merchant_good_count() {
  if (g_ui.card == CARD_PACKAGES) return (uint8_t)(sim_package_count(g_ui.selectedShip) + merchant_story_shift());
  int count = 0; while (merchant_good_at(count) >= 0) ++count; return (uint8_t)count;
}
static PackageOffer merchant_package_at(int index) {
  index -= merchant_story_shift();
  if (index < 0) { PackageOffer none{}; none.code = SIM_CONTRACT_UNAVAILABLE; return none; }
  return sim_package_offer(g_ui.selectedShip, (uint8_t)index);
}
PackageOffer x4_merchant_package(int row) { return merchant_package_at(g_ui.page * 2 + row); }
PackageOffer x4_merchant_selected_package() { return merchant_package_at(g_ui.selectedGood); }
static void x4_merchant_refresh() {
  merchantDraft.trade = sim_merchant_trade_offer(g_ui.selectedShip, !merchantDraft.selling, merchantDraft.quantities);
}
static void x4_merchant_open(int mode) {
  if (g_ui.selectedShip >= g_campaign.shipCount) return;
  const Ship& ship = g_ships[g_ui.selectedShip];
  if (ship.state == SHIP_SAILING) { x4_watch_ship(); return; }
  if (mode < 0) {
    // Re-evaluate the selected ship on entry, after arrival delivered parcels.
    // Full merchandise holds need room; explicit rocker choices stay explicit.
    bool merchandise = false;
    for (int c = 0; c < ship.cargoCount; ++c)
      merchandise |= ship.cargo[c].kind == CARGO_MARKET && ship.cargo[c].quantity > 0;
    const bool inPort = ship.state == SHIP_DOCKED || ship.state == SHIP_ARRIVED;
    mode = inPort && merchandise && sim_ship_used_capacity(ship) >= sim_ship_capacity(ship) ? 1 : 0;
    if (sim_onboarding_step() == ONBOARD_SELL) mode = 1;
  }
  // Parcels open when the guided first voyage ends.
  if (mode == 2 && sim_onboarding_step() >= ONBOARD_BUY) mode = 0;
  merchantDraft = {}; merchantDraft.selling = mode == 1;
  g_ui.selectedPort = ship.currentPort;
  open_card(mode == 2 ? CARD_PACKAGES : CARD_TRADE);
  g_ui.captainView = (uint8_t)mode;
  g_ui.selectedGood = mode == 2 ? 0 : (uint8_t)merchant_good_at(0);
  x4_merchant_refresh();
  board_x4_discard_button_events();
}
static void x4_merchant_ship() {
  if (g_ui.selectedShip >= g_campaign.shipCount) return;
  const Ship& arriving=g_ships[g_ui.selectedShip];
  if(arriving.merchant.state==MERCHANT_ARRIVED && arriving.merchant.receiptUnread &&
     !apply_result(sim_ack_arrival_receipt(g_ui.selectedShip,arriving.merchant.id)))return;
  x4_nav_push();
  merchantDraft = {}; merchantUpgradeReturn = false; watchActive = false;
  g_ui.selectedPort = g_ships[g_ui.selectedShip].currentPort;
  open_card(CARD_SHIP); g_ui.captainView = 0;
  board_x4_discard_button_events();
}
static void x4_merchant_ship_row(int row) {
  if (row == 0) x4_merchant_open();
  else if (row == 1) x4_merchant_routes();
  else if (row == 2) {
    if (sim_onboarding_step() >= ONBOARD_BUY) { apply_result(SIM_LOCKED); return; }  // after the first voyage
    merchantUpgradeReturn = true;
    open_card(CARD_SHIPYARD);
    g_ui.page = 1;
  }
}
static void x4_merchant_cancel_edit() {
  merchantDraft.editing = false;
  memset(merchantDraft.quantities, 0, GOOD_COUNT);
  g_ui.captainView = x4_merchant_mode();
  x4_merchant_refresh(); g_ui.dirty = 1;
  board_x4_discard_button_events();
}
static void x4_merchant_quantity(int dir) {
  if (g_ui.card != CARD_TRADE || g_ui.selectedGood >= GOOD_COUNT) return;
  uint8_t good = g_ui.selectedGood;
  uint8_t next[GOOD_COUNT]; memcpy(next, merchantDraft.quantities, GOOD_COUNT);
  if (dir < 0 && next[good]) --next[good];
  else if (dir > 0 && next[good] < 255) ++next[good];
  else return;
  MerchantTradeQuote candidate = sim_merchant_trade_offer(g_ui.selectedShip, !merchantDraft.selling, next);
  if (dir < 0 || candidate.code == SIM_OK) memcpy(merchantDraft.quantities, next, GOOD_COUNT);
  // Past everything aboard, a sale simply stops; the hint already reads "All
  // aboard". A purchase says why it stopped (coins, hold space, stock).
  else if (!(merchantDraft.selling && candidate.code == SIM_CARGO_UNAVAILABLE)) apply_result(candidate.code);
  x4_merchant_refresh(); g_ui.dirty = 1;
}
// A newly chosen good starts at the most the ship can trade (the whole hold when
// buying, everything aboard when selling); LEFT trims it. Feasibility is
// monotonic, so the largest amount is found by bisection.
static void x4_merchant_quantity_max() {
  if (g_ui.card != CARD_TRADE || g_ui.selectedGood >= GOOD_COUNT) return;
  uint8_t good = g_ui.selectedGood;
  uint8_t next[GOOD_COUNT]; memcpy(next, merchantDraft.quantities, GOOD_COUNT);
  int low = 0, high = 255;
  SimResultCode firstFailure = SIM_OK;
  while (low < high) {
    int mid = (low + high + 1) / 2;
    next[good] = (uint8_t)mid;
    MerchantTradeQuote candidate = sim_merchant_trade_offer(g_ui.selectedShip, !merchantDraft.selling, next);
    if (candidate.code == SIM_OK) low = mid;
    else { high = mid - 1; if (mid == 1) firstFailure = candidate.code; }
  }
  next[good] = (uint8_t)low;
  memcpy(merchantDraft.quantities, next, GOOD_COUNT);
  if (!low && firstFailure != SIM_OK) apply_result(firstFailure);  // why not even one fits
  x4_merchant_refresh(); g_ui.dirty = 1;
}
static void x4_merchant_routes() {
  if (g_ui.selectedShip >= g_campaign.shipCount) return;
  if (g_ships[g_ui.selectedShip].state == SHIP_SAILING) { x4_watch_ship(); return; }
  voyageOriginUi = g_ui;
  open_card(CARD_VOYAGE); g_ui.routeScroll = 0;
  voyageDestination = NO_PORT; merchantDraft.sail = {};
}
static void x4_merchant_row(int row) {
  if (row >= 2 && row <= 4) { x4_merchant_open(row - 2); return; }
  if (row >= 5) {
    int pages = max(1, (x4_merchant_good_count() + 1) / 2);
    g_ui.page = (uint8_t)((g_ui.page + (row == 5 ? pages - 1 : 1)) % pages);
    g_ui.selectedGood = g_ui.card == CARD_PACKAGES ? g_ui.page * 2 : (uint8_t)x4_merchant_good(0);
    g_ui.dirty = 1; return;
  }
  if (row < 0 || row > 1) return;
  if (g_ui.card == CARD_PACKAGES) {
    if (!x4_merchant_story_row(row) && !x4_merchant_package(row).id) return;
    g_ui.selectedGood = g_ui.page * 2 + row;
  } else {
    int good = x4_merchant_good(row); if (good < 0) return;
    g_ui.selectedGood = (uint8_t)good;
    memset(merchantDraft.quantities, 0, GOOD_COUNT);
  }
  merchantDraft.editing = true;
  g_ui.captainView = x4_merchant_mode() + 4;
  if (g_ui.card == CARD_TRADE) x4_merchant_quantity_max();
  g_ui.dirty = 1; board_x4_discard_button_events();
}
static void x4_merchant_footer(int id) {
  if (id == 2) {
    if (merchantDraft.editing) x4_merchant_cancel_edit();
    else x4_merchant_ship();
    return;
  }
  if (id != 1 || !merchantDraft.editing) return;
  if (g_ui.card == CARD_PACKAGES && x4_merchant_story_selected()) {
    StoryOffer offer{}; sim_story_offer(g_ui.selectedShip, offer);
    if (apply_result(sim_story_accept(g_ui.selectedShip))) {
      g_ui.page = 0; g_ui.selectedGood = 0; x4_merchant_cancel_edit();
      x4_story_moment_open(X4_STORY_REVEAL, offer.chapter);
    }
    g_ui.dirty = 1; return;
  }
  if (g_ui.card == CARD_PACKAGES) {
    PackageOffer offer = x4_merchant_selected_package();
    if (apply_result(sim_package_accept(g_ui.selectedShip, offer))) {
      g_ui.page = 0; g_ui.selectedGood = 0; x4_merchant_cancel_edit();
    }
    g_ui.dirty = 1; return;
  }
  if (!merchantDraft.trade.quantity) return;
  SimResult result = sim_merchant_trade_apply(g_ui.selectedShip, merchantDraft.trade);
  if (apply_result(result)) {
    x4_merchant_cancel_edit();
    g_ui.page = 0; g_ui.selectedGood = (uint8_t)merchant_good_at(0);
  }
  x4_merchant_refresh(); g_ui.dirty = 1;
}

static void x4_merchant_voyage_row(int row) {
  int destination = x4_voyage_destination(row);
  if (destination < 0) return;
  voyageDestination = (uint8_t)destination;
  merchantDraft.sail = sim_merchant_sail_offer(g_ui.selectedShip, voyageDestination);
  g_ui.page = 2; g_ui.dirty = 1;
}
static void x4_merchant_voyage_footer(int id) {
  if (id == 2 || (g_ui.page == 2 && id == 0)) {
    if (g_ui.page == 2 && id == 2) { g_ui.page = 0; g_ui.dirty = 1; }
    else x4_merchant_ship();
    return;
  }
  if (g_ui.page == 0) {
    int pages = max(1, (x4_voyage_destination_count() + 1) / 2);
    g_ui.routeScroll = (uint8_t)(((g_ui.routeScroll / 2 + (id == 0 ? pages - 1 : 1)) % pages) * 2);
    g_ui.dirty = 1; return;
  }
  SimResult result = sim_merchant_sail(g_ui.selectedShip, merchantDraft.sail);
  if (apply_result(result)) {
    // A committed voyage goes straight to Watch; Back returns to Fleet,
    // never to a stale confirmation that could send the ship again.
    uint8_t departing=g_ui.selectedShip;
    x4_nav_departed();g_ui.selectedShip=departing;
    x4_watch_ship();
  }
  else merchantDraft.sail = sim_merchant_sail_offer(g_ui.selectedShip, voyageDestination);
  g_ui.dirty = 1;
}
static bool x4_merchant_touch(int x, int y) {
  if (!merchant_card(g_ui.card)) return false;
  for (int row = 2; row < 7; ++row) {
    if (row >= 5 && (merchantDraft.editing || x4_merchant_good_count() <= 2)) continue;
    HitRegion r = merchant_control_region(row);
    if (hit_test(&r, 1, x, y) >= 0) { x4_merchant_row(row); return true; }
  }
  if (merchantDraft.editing) return true;
  for (int row = 0; row < 2; ++row) {
    HitRegion r = merchant_row_region(row);
    if (hit_test(&r, 1, x, y) >= 0) { x4_merchant_row(row); return true; }
  }
  return true;
}
int x4_merchant_donation_good() {
  if (g_campaign.economyVersion < 2 || g_campaign.deliveredUnits < 48 || g_ui.selectedShip >= g_campaign.shipCount) return -1;
  uint8_t region = sim_project_region();
  if (region >= REGION_COUNT) return -1;
  const Ship& ship = g_ships[g_ui.selectedShip];
  if ((ship.state != SHIP_DOCKED && ship.state != SHIP_ARRIVED) || ship.currentPort != sim_project_port(region)) return -1;
  for (int material = 0; material < 2; ++material) {
    uint8_t good = material ? GOOD_IRONWARE : GOOD_TIMBER;
    if (g_campaign.projectCargo[region][material] >= sim_project_target(region)) continue;
    for (int c = 0; c < ship.cargoCount; ++c) {
      const CargoItem& cargo = ship.cargo[c];
      if (cargo.kind == CARGO_MARKET && cargo.good == good && cargo.quantity && cargo.sailed &&
          cargo.sourcePort < MAX_PORTS && cargo.sourcePort != ship.currentPort) return good;
    }
  }
  return -1;
}
