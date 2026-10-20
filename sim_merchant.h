// Included after sim_voyages.h. Merchant goods remain owned until an explicit
// exchange sale; free transit and fixed-price packages never imply a sale.

static uint32_t merchant_ship_hash(uint8_t index) {
  const Ship& s = g_ships[index];
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix(g_campaign.seed); mix(sim_return_epoch()); mix(s.id); mix(s.state); mix(s.currentPort);
  mix(s.classId); mix(s.captainId); mix(s.modules[0]); mix(s.modules[1]);
  mix(s.booking.state); mix(s.merchant.state); mix(s.cargoCount);
  for (uint8_t i = 0; i < s.cargoCount; ++i) {
    const CargoItem& c = s.cargo[i];
    mix(c.contractId); mix(c.paidUnitPrice); mix(c.good); mix(c.quantity); mix(c.kind);
    mix(c.jobKind); mix(c.destination); mix(c.projectRegion); mix(c.sourcePort); mix(c.sailed);
  }
  return h;
}

// The only secondary port a trade/donation can unlock is Mariner Rest. Keep
// rollback bounded rather than placing a whole company snapshot on the stack.
struct MerchantUndo {
  Campaign campaign; Ship ship; UiState ui;
  PortState origin, albion;
  Contract originJobs[MAX_CONTRACTS_PER_PORT], albionJobs[MAX_CONTRACTS_PER_PORT];
  JournalEntry journal[3]; ArrivalReceipt receipt;
  uint8_t index, port, head, count;
  explicit MerchantUndo(uint8_t i) : campaign(g_campaign), ship(g_ships[i]), ui(g_ui),
      origin(g_ports[g_ships[i].currentPort]), albion(g_ports[3]), receipt(arrivalReceipts[i]),
      index(i), port(g_ships[i].currentPort), head(g_journalHead), count(g_journalCount) {
    memcpy(originJobs, g_contracts[port], sizeof(originJobs));
    memcpy(albionJobs, g_contracts[3], sizeof(albionJobs));
    for (uint8_t j = 0; j < 3; ++j) journal[j] = g_journal[(head + j) % MAX_JOURNAL];
  }
  void restore() const {
    g_campaign = campaign; g_ships[index] = ship; g_ui = ui;
    g_ports[port] = origin; g_ports[3] = albion;
    memcpy(g_contracts[port], originJobs, sizeof(originJobs));
    memcpy(g_contracts[3], albionJobs, sizeof(albionJobs));
    for (uint8_t j = 0; j < 3; ++j) g_journal[(head + j) % MAX_JOURNAL] = journal[j];
    g_journalHead = head; g_journalCount = count; arrivalReceipts[index] = receipt;
  }
};

static MerchantTradeQuote merchant_trade_for_ship(uint8_t index, const Ship& source,
    bool buying, const uint8_t* quantities, bool estimate) {
  MerchantTradeQuote q{}; q.code = SIM_INVALID; q.shipIndex = index;
  q.port = source.currentPort; q.buying = buying; q.estimateOnly = estimate;
  if (index >= g_campaign.shipCount || q.port >= MAX_PORTS || !quantities) return q;
  memcpy(q.quantities, quantities, GOOD_COUNT);
  if (!ship_at_port(source) || source.state == SHIP_DISABLED || source.booking.state == BOOKING_SAILING) {
    q.code = SIM_BAD_SHIP_STATE; return q;
  }
  unsigned total = 0;
  for (uint8_t g = 0; g < GOOD_COUNT; ++g) {
    total += quantities[g];
    q.unitPrices[g] = buying ? content_market_price(q.port, g, true) : captain_sale_price(source, content_market_price(q.port, g, false));
    if (buying) q.availableQuantities[g] = (uint8_t)min((int)g_ports[q.port].stock[g],
      max(0, sim_ship_capacity(source) - sim_ship_used_capacity(source)));
    else for (uint8_t c = 0; c < source.cargoCount; ++c)
      if (source.cargo[c].kind == CARGO_MARKET && source.cargo[c].good == g)
        q.availableQuantities[g] = (uint8_t)min(255, (int)q.availableQuantities[g] + source.cargo[c].quantity);
  }
  if (total > 255u) { q.code = SIM_NO_CAPACITY; return q; }
  q.quantity = (uint8_t)total;
  Ship ship = source;
  uint16_t stock[GOOD_COUNT]; memcpy(stock, g_ports[q.port].stock, sizeof(stock));
  int32_t coins = g_campaign.coins, debt = g_campaign.recoveryDebt, profit = g_campaign.marketProfit;
  for (uint8_t g = 0; g < GOOD_COUNT; ++g) for (uint8_t unitIndex = 0; unitIndex < quantities[g]; ++unitIndex) {
    MarketUnit unit{};
    SimResult result = market_trade_unit(ship, g, buying, stock[g], coins, debt, profit, unit);
    if (!result) { q.code = result.code; return q; }
    q.total += unit.price; q.withholding += unit.withheld;
    q.costBasis += buying ? unit.price : unit.price - unit.profit;
  }
  q.cashDelta = coins - g_campaign.coins;
  q.profit = buying ? 0 : q.cashDelta - q.costBasis;
  q.code = total ? SIM_OK : SIM_CARGO_UNAVAILABLE;
  if (!estimate) {
    uint32_t h = merchant_ship_hash(index);
    auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix(buying); mix((uint32_t)g_campaign.coins); mix((uint32_t)g_campaign.recoveryDebt);
    mix((uint32_t)q.total); mix((uint32_t)q.cashDelta); mix((uint32_t)q.costBasis);
    for (uint8_t g = 0; g < GOOD_COUNT; ++g) { mix(quantities[g]); mix(q.unitPrices[g]); }
    q.fingerprint = h ? h : 1u;
  }
  return q;
}

MerchantTradeQuote sim_merchant_trade_offer(uint8_t index, bool buying, const uint8_t* quantities) {
  return merchant_trade_for_ship(index, g_ships[index < MAX_SHIPS ? index : 0], buying, quantities, false);
}

MerchantTradeQuote sim_merchant_sale_estimate(uint8_t index, uint8_t destination) {
  MerchantTradeQuote q{}; q.code = SIM_INVALID; q.estimateOnly = 1;
  if (index >= g_campaign.shipCount || destination >= MAX_PORTS) return q;
  Ship future = g_ships[index]; future.currentPort = destination; future.state = SHIP_DOCKED;
  future.booking.state = BOOKING_NONE;
  uint8_t quantities[GOOD_COUNT]{};
  for (uint8_t i = 0; i < future.cargoCount; ++i) {
    const CargoItem& c = future.cargo[i];
    if (c.kind == CARGO_MARKET && c.good < GOOD_COUNT)
      quantities[c.good] = (uint8_t)min(255, (int)quantities[c.good] + c.quantity);
  }
  return merchant_trade_for_ship(index, future, false, quantities, true);
}

SimResult sim_merchant_trade_apply(uint8_t index, const MerchantTradeQuote& quote) {
  if (index >= g_campaign.shipCount || quote.shipIndex != index || quote.estimateOnly ||
      !quote.fingerprint || quote.buying > 1) return SIM_INVALID;
  MerchantTradeQuote fresh = sim_merchant_trade_offer(index, quote.buying, quote.quantities);
  if (fresh.fingerprint != quote.fingerprint) return SIM_STALE_PREVIEW;
  if (fresh.code != SIM_OK) return fresh.code;
  if (quote.port != fresh.port || quote.quantity != fresh.quantity || quote.total != fresh.total ||
      quote.cashDelta != fresh.cashDelta || quote.profit != fresh.profit ||
      quote.costBasis != fresh.costBasis || quote.withholding != fresh.withholding) return SIM_STALE_PREVIEW;
  MerchantUndo before(index);
  Ship& ship = g_ships[index];
  unsigned delivered = 0;
  for (uint8_t g = 0; g < GOOD_COUNT; ++g) for (uint8_t i = 0; i < fresh.quantities[g]; ++i) {
    MarketUnit unit{};
    SimResult result = market_trade_unit(ship, g, fresh.buying, g_ports[ship.currentPort].stock[g],
      g_campaign.coins, g_campaign.recoveryDebt, g_campaign.marketProfit, unit);
    if (!result) { before.restore(); return result; }
    delivered += unit.delivered;
  }
  g_campaign.deliveredUnits += delivered;
  g_campaign.companyXp = (uint16_t)min((uint32_t)65535u, (uint32_t)g_campaign.companyXp + delivered);
  g_campaign.deliveries = (uint16_t)min((uint32_t)65535u, (uint32_t)g_campaign.deliveries + delivered);
  mark_market_traded(); update_rank();
  journal_add(1, fresh.buying ? "Bought %u cargo for %ld." : "Sold %u cargo; %ld coins banked.",
    fresh.quantity, (long)(fresh.buying ? fresh.total : fresh.cashDelta));
  g_ui.dirty = 1;
  if (!save_write()) { before.restore(); return SIM_SAVE_FAILED; }
  return SIM_OK;
}

uint8_t sim_merchant_destinations(uint8_t index, uint8_t* out, uint8_t capacity) {
  if (index >= g_campaign.shipCount || !out || !capacity) return 0;
  Ship ship = g_ships[index];
  if (ship.merchant.state == MERCHANT_SAILING) ship.currentPort = ship.merchant.destination;
  else if (ship.booking.state == BOOKING_SAILING) ship.currentPort = ship.booking.destination;
  uint8_t ports[MAX_PORTS]; uint32_t times[MAX_PORTS]; uint8_t count = 0;
  for (uint8_t port = 0; port < MAX_PORTS; ++port) {
    if (!g_ports[port].unlocked || port == ship.currentPort || !sim_onboarding_allows_destination(index, port)) continue;
    VoyageQuote route{};
    if (!booked_route(ship, port, route)) continue;
    uint8_t slot = count++;
    while (slot && times[slot - 1] > route.minutes) {
      ports[slot] = ports[slot - 1]; times[slot] = times[slot - 1]; --slot;
    }
    ports[slot] = port; times[slot] = route.minutes;
  }
  uint8_t written = min(count, capacity); memcpy(out, ports, written); return written;
}

MerchantSailQuote sim_merchant_sail_offer(uint8_t index, uint8_t destination) {
  MerchantSailQuote q{}; q.code = SIM_INVALID; q.shipIndex = index; q.destination = destination;
  if (index >= g_campaign.shipCount || destination >= MAX_PORTS) return q;
  const Ship& ship = g_ships[index]; q.origin = ship.currentPort;
  if (!ship_at_port(ship) || ship.state == SHIP_DISABLED || ship.booking.state == BOOKING_SAILING ||
      ship.merchant.state == MERCHANT_SAILING) { q.code = SIM_BAD_SHIP_STATE; return q; }
  if (!sim_ship_captain(index)) { q.code = SIM_CAPTAIN_REQUIRED; return q; }
  if (!g_ports[destination].unlocked) { q.code = SIM_PORT_LOCKED; return q; }
  if (!sim_onboarding_allows_destination(index, destination)) { q.code = SIM_LOCKED; return q; }
  if (sim_ship_used_capacity(ship) > sim_ship_capacity(ship)) { q.code = SIM_NO_CAPACITY; return q; }
  VoyageQuote route{};
  if (!booked_route(ship, destination, route)) { q.code = SIM_BAD_ROUTE; return q; }
  q.minutes = route.minutes; q.routeCount = route.routeCount;
  memcpy(q.route, route.route, sizeof(q.route)); memcpy(q.legMinutes, route.legMinutes, sizeof(q.legMinutes));
  MerchantTradeQuote estimate = sim_merchant_sale_estimate(index, destination);
  q.saleEstimate = estimate.cashDelta; q.costBasis = estimate.costBasis;
  for (uint8_t i = 0; i < ship.cargoCount; ++i) {
    const CargoItem& cargo = ship.cargo[i];
    if (cargo.kind == CARGO_BOOKED) { q.code = SIM_CONTRACT_UNAVAILABLE; return q; }
    if (cargo.kind == CARGO_CONTRACT && cargo.jobKind == JOB_STORY) {
      for (uint8_t leg = 0; leg < q.routeCount; ++leg)
        if (q.route[leg] == cargo.destination) { q.packagePayout += sim_story_reward(); break; }
      continue;
    }
    if (cargo.kind != CARGO_CONTRACT || cargo.jobKind != JOB_PACKAGE) continue;
    Contract* contract = find_contract(cargo.contractId);
    if (!contract || contract->active != 2) { q.code = SIM_CONTRACT_UNAVAILABLE; return q; }
    for (uint8_t leg = 0; leg < q.routeCount; ++leg)
      if (q.route[leg] == cargo.destination) { q.packagePayout += contract->reward; break; }
  }
  uint32_t h = merchant_ship_hash(index);
  auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix(g_campaign.nextVoyageId); mix(q.destination); mix(q.minutes);
  mix((uint32_t)q.saleEstimate); mix((uint32_t)q.costBasis); mix((uint32_t)q.packagePayout);
  for (uint8_t i = 0; i < q.routeCount; ++i) { mix(q.route[i]); mix(q.legMinutes[i]); }
  q.fingerprint = h ? h : 1u; q.code = SIM_OK; return q;
}

SimResult sim_merchant_sail(uint8_t index, const MerchantSailQuote& quote) {
  if (index >= g_campaign.shipCount || quote.shipIndex != index || !quote.fingerprint) return SIM_INVALID;
  MerchantSailQuote fresh = sim_merchant_sail_offer(index, quote.destination);
  if (fresh.fingerprint != quote.fingerprint) return SIM_STALE_PREVIEW;
  if (fresh.code != SIM_OK) return fresh.code;
  if (quote.origin != fresh.origin || quote.minutes != fresh.minutes || quote.routeCount != fresh.routeCount ||
      quote.saleEstimate != fresh.saleEstimate || quote.costBasis != fresh.costBasis ||
      quote.packagePayout != fresh.packagePayout || memcmp(quote.route, fresh.route, fresh.routeCount) ||
      memcmp(quote.legMinutes, fresh.legMinutes, fresh.routeCount * sizeof(uint16_t))) return SIM_STALE_PREVIEW;
  MerchantUndo before(index);
  Ship& ship = g_ships[index];
  ship.booking = {}; ship.merchant = {};
  MerchantVoyage& m = ship.merchant;
  m.id = g_campaign.nextVoyageId++; m.state = MERCHANT_SAILING;
  m.origin = fresh.origin; m.destination = fresh.destination;
  memcpy(m.legMinutes, fresh.legMinutes, sizeof(m.legMinutes));
  ship.routeCount = fresh.routeCount; ship.routePos = 0;
  memcpy(ship.route, fresh.route, sizeof(ship.route));
  ship.prepaidCost = 0; ship.manifestBonus = 0;
  arm_leg(ship);
  story_on_departure(index);
  for (uint8_t step = TUTORIAL_PORT; step <= TUTORIAL_DISPATCH; ++step) sim_tutorial_complete(step);
  g_ui.selectedShip = index; g_ui.selectedPort = fresh.destination; g_ui.dirty = 1;
  journal_add(1, "%s sailed for %s.", ship.name, PORT_DEFS[fresh.destination].name);
  if (!save_write()) { before.restore(); return SIM_SAVE_FAILED; }
  return SIM_OK;
}

static void merchant_refresh_packages(uint8_t port) {
  const uint32_t epoch = g_campaign.worldMinutes / BOOKING_REFRESH_MINUTES;
  if (g_ports[port].packageEpoch == epoch) return;
  g_ports[port].packageEpoch = epoch;
  Contract* board = g_contracts[port];
  for (uint8_t i = 0; i < MAX_CONTRACTS_PER_PORT; ++i)
    if (board[i].active != 2) board[i] = {};
  // A neutral Schooner determines customer pay. The accepting ship changes
  // travel time, never the promised reward. Four offers per elapsed epoch.
  Ship reference{}; reference.classId = 1; reference.currentPort = port;
  reference.captainId = NO_SHIP;
  uint8_t ports[MAX_PORTS]; uint32_t times[MAX_PORTS]; uint8_t count = 0;
  for (uint8_t p = 0; p < MAX_PORTS; ++p) {
    if (p == port || !g_ports[p].unlocked) continue;
    VoyageQuote route{}; if (!booked_route(reference, p, route)) continue;
    uint8_t at = count++;
    while (at && times[at - 1] > route.minutes) { times[at] = times[at - 1]; ports[at] = ports[at - 1]; --at; }
    times[at] = route.minutes; ports[at] = p;
  }
  if (!count) return;
  uint8_t issued = 0;
  for (uint8_t slot = 0; slot < MAX_CONTRACTS_PER_PORT && issued < 4; ++slot) {
    if (board[slot].active) continue;
    // Keep the nearest two options, then rotate later offers across the coast.
    uint8_t pick = issued < 2 ? (uint8_t)(issued % count) : (uint8_t)((epoch + port + issued) % count);
    Contract c{}; c.id = g_campaign.nextContractId++; c.origin = port; c.destination = ports[pick];
    c.good = NO_PORT; c.quantity = 1; c.kind = JOB_PACKAGE; c.projectRegion = NO_PORT;
    c.reward = (uint16_t)min((uint32_t)65535u, 8u + 4u * ((times[pick] + 599u) / 600u));
    c.active = 1; board[slot] = c; ++issued;
  }
  compact_contracts(port);
}

static PackageOffer package_offer_for_contract(uint8_t index, const Contract& c) {
  PackageOffer q{}; q.code = SIM_CONTRACT_UNAVAILABLE; q.shipIndex = index;
  q.id = c.id; q.origin = c.origin; q.destination = c.destination; q.reward = c.reward;
  if (index >= g_campaign.shipCount || c.active != 1 || c.kind != JOB_PACKAGE || c.quantity != 1) return q;
  const Ship& ship = g_ships[index];
  if (!ship_at_port(ship) || ship.state == SHIP_DISABLED || ship.currentPort != c.origin) {
    q.code = SIM_BAD_SHIP_STATE; return q;
  }
  VoyageQuote route{};
  if (!booked_route(ship, c.destination, route)) { q.code = SIM_BAD_ROUTE; return q; }
  q.minutes = route.minutes;
  uint32_t h = merchant_ship_hash(index);
  auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix(c.id); mix(c.origin); mix(c.destination); mix(c.reward); mix(q.minutes);
  q.fingerprint = h ? h : 1u;
  q.code = sim_ship_used_capacity(ship) < sim_ship_capacity(ship) && ship.cargoCount < MAX_CARGO_ITEMS ?
    SIM_OK : SIM_NO_CAPACITY;
  return q;
}

uint8_t sim_package_count(uint8_t index) {
  if (index >= g_campaign.shipCount || g_ships[index].currentPort >= MAX_PORTS) return 0;
  uint8_t count = 0;
  for (const Contract& c : g_contracts[g_ships[index].currentPort]) {
    PackageOffer q = package_offer_for_contract(index, c);
    if (q.code == SIM_OK || q.code == SIM_NO_CAPACITY) ++count;
  }
  return count;
}

PackageOffer sim_package_offer(uint8_t index, uint8_t row) {
  PackageOffer q{}; q.code = SIM_CONTRACT_UNAVAILABLE;
  if (index >= g_campaign.shipCount || g_ships[index].currentPort >= MAX_PORTS) return q;
  for (const Contract& c : g_contracts[g_ships[index].currentPort]) {
    q = package_offer_for_contract(index, c);
    if ((q.code == SIM_OK || q.code == SIM_NO_CAPACITY) && !row--) return q;
  }
  q = {}; q.code = SIM_CONTRACT_UNAVAILABLE; return q;
}

SimResult sim_package_accept(uint8_t index, const PackageOffer& offer) {
  if (index >= g_campaign.shipCount || offer.shipIndex != index || !offer.fingerprint) return SIM_INVALID;
  Contract* contract = find_contract(offer.id);
  if (!contract) return SIM_CONTRACT_UNAVAILABLE;
  PackageOffer fresh = package_offer_for_contract(index, *contract);
  if (fresh.fingerprint != offer.fingerprint) return SIM_STALE_PREVIEW;
  if (fresh.code != SIM_OK) return fresh.code;
  if (offer.origin != fresh.origin || offer.destination != fresh.destination ||
      offer.reward != fresh.reward || offer.minutes != fresh.minutes) return SIM_STALE_PREVIEW;
  MerchantUndo before(index);
  Ship& ship = g_ships[index]; CargoItem item{};
  item.contractId = contract->id; item.kind = CARGO_CONTRACT; item.jobKind = JOB_PACKAGE;
  item.good = NO_PORT; item.quantity = 1; item.destination = contract->destination;
  item.projectRegion = NO_PORT; item.sourcePort = ship.currentPort;
  ship.cargo[ship.cargoCount++] = item; contract->active = 2;
  compact_contracts(ship.currentPort);
  journal_add(0, "Parcel aboard for %s.", PORT_DEFS[item.destination].name);
  g_ui.dirty = 1;
  if (!save_write()) { before.restore(); return SIM_SAVE_FAILED; }
  return SIM_OK;
}

SimResult sim_merchant_donate(uint8_t index, uint8_t good, uint8_t quantity) {
  if (index >= g_campaign.shipCount || !quantity || (good != GOOD_TIMBER && good != GOOD_IRONWARE)) return SIM_INVALID;
  Ship& ship = g_ships[index]; uint8_t region = sim_project_region();
  if (!ship_at_port(ship) || ship.state == SHIP_DISABLED) return SIM_BAD_SHIP_STATE;
  if (region >= REGION_COUNT || g_campaign.deliveredUnits < 48) return SIM_LOCKED;
  if (ship.currentPort != sim_project_port(region)) return SIM_WRONG_PORT;
  if (quantity > project_remaining(region, good)) return SIM_PROJECT_INCOMPLETE;
  unsigned imported = 0;
  for (uint8_t i = 0; i < ship.cargoCount; ++i) {
    const CargoItem& c = ship.cargo[i];
    if (c.kind == CARGO_MARKET && c.good == good && c.sailed && c.sourcePort < MAX_PORTS &&
        c.sourcePort != ship.currentPort) imported += c.quantity;
  }
  if (quantity > imported) return SIM_CARGO_UNAVAILABLE;
  MerchantUndo before(index);
  uint8_t remaining = quantity; uint32_t basis = 0;
  for (uint8_t i = 0; i < ship.cargoCount && remaining;) {
    CargoItem& c = ship.cargo[i];
    if (c.kind != CARGO_MARKET || c.good != good || !c.sailed || c.sourcePort >= MAX_PORTS ||
        c.sourcePort == ship.currentPort) { ++i; continue; }
    while (c.quantity && remaining) { basis += c.paidUnitPrice; market_remove_unit(c); --remaining; }
    if (!c.quantity) cargo_remove(ship, i); else ++i;
  }
  g_campaign.projectCargo[region][good == GOOD_TIMBER ? 0 : 1] += quantity;
  g_campaign.capitalSpending += basis;
  g_campaign.deliveredUnits += quantity;
  g_campaign.companyXp = (uint16_t)min((uint32_t)65535u, (uint32_t)g_campaign.companyXp + quantity);
  g_campaign.deliveries = (uint16_t)min((uint32_t)65535u, (uint32_t)g_campaign.deliveries + quantity);
  update_rank();
  journal_add(1, "Dock received %u %s.", quantity, GOOD_DEFS[good].name);
  g_ui.dirty = 1;
  if (!save_write()) { before.restore(); return SIM_SAVE_FAILED; }
  return SIM_OK;
}
