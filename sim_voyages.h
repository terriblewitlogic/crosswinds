// Included by sim.cpp after its navigation and ship helpers. Signed voyages
// use their own terms; active legacy contracts continue through the old path.

static constexpr uint32_t BOOKING_REFRESH_MINUTES = 3600u; // six real minutes
static constexpr uint16_t BOOKING_RESERVE = 200;

static bool port_makes(uint8_t port, uint8_t good) {
  return port < MAX_PORTS && good < GOOD_COUNT &&
    (PORT_DEFS[port].produced[0] == good || PORT_DEFS[port].produced[1] == good);
}

static bool port_wants(uint8_t port, uint8_t good) {
  return port < MAX_PORTS && good < GOOD_COUNT &&
    (PORT_DEFS[port].demanded[0] == good || PORT_DEFS[port].demanded[1] == good);
}

uint8_t sim_project_region() {
  return next_locked_region();
}

uint16_t sim_project_target(uint8_t region) {
  return region > 0 && region < REGION_COUNT ? (uint16_t)(8 + region * 4) : 0;
}

uint16_t sim_project_cost(uint8_t region) {
  if (!region || region >= REGION_COUNT) return 0;
  int remaining = 900 * region - g_campaign.projectWorksCredit[region];
  return (uint16_t)(remaining > 0 ? remaining : 0);
}

uint8_t sim_project_port(uint8_t region) {
  return region == 1 ? 3 : (region > 1 && region < REGION_COUNT ? (uint8_t)((region - 1) * 4) : NO_PORT);
}

static int project_remaining(uint8_t region, uint8_t good) {
  int material = good == GOOD_TIMBER ? 0 : good == GOOD_IRONWARE ? 1 : -1;
  if (region >= REGION_COUNT || material < 0) return 0;
  int remaining = sim_project_target(region) - g_campaign.projectCargo[region][material];
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    const BookedVoyage& b = g_ships[i].booking;
    if (b.state == BOOKING_SAILING && b.projectRegion == region)
      remaining -= b.projectQuantities[good];
  }
  return max(0, remaining);
}

static uint8_t project_for_order(uint8_t destination, uint8_t good) {
  uint8_t region = sim_project_region();
  if (region == NO_PORT || g_campaign.deliveredUnits < 48 || destination != sim_project_port(region))
    return NO_PORT;
  return project_remaining(region, good) > 0 ? region : NO_PORT;
}

uint8_t sim_voyage_recommend_good(uint8_t origin, uint8_t destination) {
  if (origin >= MAX_PORTS || destination >= MAX_PORTS || origin == destination) return NO_PORT;
  // A concrete works order takes precedence while its material is still due.
  for (uint8_t good = GOOD_TIMBER; good <= GOOD_IRONWARE; good += 2)
    if (port_makes(origin, good) && project_for_order(destination, good) < REGION_COUNT) return good;
  for (uint8_t i = 0; i < 2; ++i) {
    uint8_t good = PORT_DEFS[origin].produced[i];
    if (port_wants(destination, good)) return good;
  }
  // An unmatched destination remains a closed offer; the quote enforces
  // origin production for both customer-funded and company-funded cargo.
  return PORT_DEFS[destination].demanded[0];
}

static uint16_t booked_leg_minutes(const Ship& ship, int lane, uint8_t from,
                                   const CaptainProfile* captainOverride = nullptr) {
  uint8_t to = LANE_DEFS[lane].a == from ? LANE_DEFS[lane].b : LANE_DEFS[lane].a;
  uint32_t base;
  bool starterLane = (from == 0 && (to == 1 || to == 2)) || (to == 0 && (from == 1 || from == 2));
  bool albionLane = (from == 3 && to < 3) || (to == 3 && from < 3);
  if (from == 0 && to == 1) base = 1200;
  else if (from == 1 && to == 0) base = 1800;
  else if ((from == 0 && to == 2) || (from == 2 && to == 0)) base = 2400;
  else if (albionLane) base = 4800;
  else {
    // Coastal destinations stay quick; ocean crossings provide a reason to
    // choose an enduring vessel. The clock never rounds to a whole day.
    base = (uint32_t)LANE_DEFS[lane].days * 600u;
    int wind = sim_lane_wind((uint8_t)lane, from);
    base = base * 100u / (uint32_t)(100 + wind);
  }
  // Use the novice Sloop as the stated timing reference. Captain skill and
  // fittings still make later ships useful without changing the tutorial.
  int speed = starterLane || albionLane ? SHIP_CLASS_DEFS[ship.classId].speed +
      (module_slot(ship, MODULE_COPPER) >= 0 ? 10 : 0) +
      (module_slot(ship, MODULE_CHARTER_PENNANT) >= 0 ? 5 : 0) : ship_speed(ship);
  if (captainOverride && !starterLane && !albionLane) {
    speed = SHIP_CLASS_DEFS[ship.classId].speed +
      (module_slot(ship, MODULE_COPPER) >= 0 ? 10 : 0) +
      (module_slot(ship, MODULE_CHARTER_PENNANT) >= 0 ? 5 : 0);
    if (captainOverride->trait == TRAIT_NAVIGATOR) speed += 4 + captainOverride->level * 2;
  }
  base = (base * (starterLane || albionLane ? 115u : 121u) + (uint32_t)speed - 1u) / (uint32_t)speed;
  base = ((base + 9u) / 10u) * 10u; // one real second, same in quote and travel
  if (base < 300u) base = 300u;
  if (base > 54000u) base = 54000u;
  return (uint16_t)base;
}

static bool booked_route(const Ship& ship, uint8_t destination, VoyageQuote& quote,
                          const CaptainProfile* captainOverride = nullptr) {
  if (destination >= MAX_PORTS || ship.currentPort >= MAX_PORTS || destination == ship.currentPort)
    return false;
  // The route limit is part of the search state. Finding an unrestricted
  // nine-leg optimum must not hide a slightly slower legal eight-leg route.
  uint32_t distance[MAX_ROUTE_LEGS + 1][MAX_PORTS];
  uint8_t previous[MAX_ROUTE_LEGS + 1][MAX_PORTS];
  memset(distance, 0xff, sizeof(distance)); memset(previous, NO_PORT, sizeof(previous));
  distance[0][ship.currentPort] = 0;
  for (int legs = 1; legs <= MAX_ROUTE_LEGS; ++legs) {
    for (int lane = 0; lane < LANE_COUNT; ++lane) {
      const SeaLaneDef& def = LANE_DEFS[lane];
      bool albionCoast = (def.a == 3 && def.b < 3) || (def.b == 3 && def.a < 3);
      if (!albionCoast && def.days > ship_endurance(ship)) continue;
      for (int direction = 0; direction < 2; ++direction) {
        uint8_t from = direction ? def.b : def.a, to = direction ? def.a : def.b;
        if (distance[legs - 1][from] == 0xffffffffu || !g_ports[to].unlocked ||
            SHIP_CLASS_DEFS[ship.classId].draft > PORT_DEFS[to].portClass) continue;
        uint32_t candidate = distance[legs - 1][from] + booked_leg_minutes(ship, lane, from, captainOverride);
        if (candidate < distance[legs][to]) {
          distance[legs][to] = candidate; previous[legs][to] = from;
        }
      }
    }
  }
  int count = -1;
  for (int legs = 1; legs <= MAX_ROUTE_LEGS; ++legs)
    if (distance[legs][destination] != 0xffffffffu &&
        (count < 0 || distance[legs][destination] < distance[count][destination])) count = legs;
  if (count < 0) return false;
  uint8_t reverse[MAX_ROUTE_LEGS], at = destination;
  for (int leg = count; leg > 0; --leg) { reverse[count - leg] = at; at = previous[leg][at]; }
  quote.routeCount = (uint8_t)count;
  quote.minutes = distance[count][destination];
  uint8_t from = ship.currentPort;
  for (int i = 0; i < count; ++i) {
    uint8_t to = reverse[count - i - 1];
    quote.route[i] = to;
    quote.legMinutes[i] = booked_leg_minutes(ship, content_lane_between(from, to), from, captainOverride);
    from = to;
  }
  return true;
}

uint32_t sim_voyage_next_refresh_minute() {
  return (g_campaign.worldMinutes / BOOKING_REFRESH_MINUTES + 1u) * BOOKING_REFRESH_MINUTES;
}

static uint8_t booking_available(uint8_t destination, uint8_t good) {
  const PortState& port = g_ports[destination];
  uint32_t epoch = g_campaign.worldMinutes / BOOKING_REFRESH_MINUTES;
  int used = port.bookingEpoch == epoch ? port.bookingUsed[good] : 0;
  int supply = 12 + PORT_DEFS[destination].region * 4;
  int available = supply - used;
  return (uint8_t)(available > 0 ? available : 0);
}

static int booked_sailing_cost(const Ship& ship, const VoyageQuote& quote) {
  int base;
  if (quote.origin == 0 && quote.destination == 1) base = 40;
  else if (quote.origin == 1 && quote.destination == 0) base = 32;
  else if (quote.origin == 0 && quote.destination == 2) base = 56;
  else if (quote.origin == 2 && quote.destination == 0) base = 52;
  else if ((quote.origin == 0 && quote.destination == 3) ||
           (quote.origin == 3 && quote.destination == 0)) base = 96;
  else base = 24 + (int)((quote.minutes * 8u + 599u) / 600u);
  // Shared hull costs reward filling a larger ship without making a small
  // coaster redundant on these short runs. Service includes all routine wear.
  int cost = base * (80 + sim_ship_capacity(ship) * 5) / 100;
  return cost > 0 ? cost : 1;
}

static int booked_purchase_unit(uint8_t good) {
  static const uint16_t prices[GOOD_COUNT] = {18,25,35,45,66,85,104,120};
  return prices[good];
}

// Existing regional buyer prices are after duty. Preserve those economics
// outside the explicit Crown fixtures while rounding the whole manifest once.
static int booked_buyer_unit(const Ship& ship, const VoyageQuote& q, uint8_t good) {
  int purchase = booked_purchase_unit(good);
  if (q.origin == 0 && q.destination == 1 && good == GOOD_TEXTILES) return 60;
  if (q.origin == 1 && q.destination == 0 && good == GOOD_GRAIN) return 38;
  if (q.origin == 0 && q.destination == 2 && good == GOOD_IRONWARE) return 80;
  if (q.origin == 2 && q.destination == 0 && good == GOOD_TIMBER) return 55;
  int fourSlotCost = q.sailingCost * 100 / (80 + sim_ship_capacity(ship) * 5);
  return purchase + (fourSlotCost * 2 + 3) / 4 + 6 + PORT_DEFS[q.destination].region * 3;
}

static int booked_crown_gross(const VoyageQuote& q, uint8_t good) {
  if (q.origin == 0 && q.destination == 1 && good == GOOD_TEXTILES) return 60;
  if (q.origin == 0 && q.destination == 2 && good == GOOD_IRONWARE) return 85;
  if (q.origin == 0 && q.destination == 3 && (good == GOOD_TEXTILES || good == GOOD_IRONWARE)) return 100;
  if (q.origin == 1 && q.destination == 0 && good == GOOD_GRAIN) return 40;
  if (q.origin == 2 && q.destination == 0 && good == GOOD_TIMBER) return 58;
  if (q.origin == 3 && q.destination == 0 && good == GOOD_TIMBER) return 80;
  if (q.origin == 3 && q.destination == 0 && good == GOOD_GRAIN) return 65;
  return 0;
}

static int booked_service_unit(const Ship& ship, const VoyageQuote& q, uint8_t good) {
  if (q.origin == 0 && q.destination == 1 && good == GOOD_TEXTILES) return 21;
  if (q.origin == 1 && q.destination == 0 && good == GOOD_GRAIN) return 17;
  if (q.origin == 0 && q.destination == 2 && good == GOOD_IRONWARE) return 30;
  if (q.origin == 2 && q.destination == 0 && good == GOOD_TIMBER) return 26;
  int fourSlotCost = q.sailingCost * 100 / (80 + sim_ship_capacity(ship) * 5);
  int crownGross = booked_crown_gross(q, good);
  int buyer = crownGross ? crownGross * (100 - PORT_DEFS[q.destination].duty) / 100 :
                          booked_buyer_unit(ship, q, good);
  int coefficient = crownGross ? 78 : 79;
  return ((buyer - booked_purchase_unit(good)) * coefficient + fourSlotCost * 5 + 99) / 100;
}

static void booked_prices(const Ship& ship, VoyageQuote& q) {
  q.sailingCost = booked_sailing_cost(ship, q);
  q.dutyPercent = q.mode == VOYAGE_TRADE ? PORT_DEFS[q.destination].duty : 0;
  const int denominator = 100 - q.dutyPercent;
  int64_t grossNumerator = 0;
  int projectUnits = 0;
  for (uint8_t good = 0; good < GOOD_COUNT; ++good) {
    if (!(q.offeredGoods & (1u << good))) continue;
    int purchase = q.mode == VOYAGE_TRADE ? booked_purchase_unit(good) : 0;
    int buyer = q.mode == VOYAGE_TRADE ? booked_buyer_unit(ship, q, good) : booked_service_unit(ship, q, good);
    int explicitGross = q.mode == VOYAGE_TRADE ? booked_crown_gross(q, good) : buyer;
    int numerator = explicitGross ? explicitGross * denominator : buyer * 100;
    q.unitCargoCosts[good] = (uint16_t)purchase;
    q.unitGrossPayments[good] = (uint16_t)((numerator + denominator - 1) / denominator);
    q.unitBuyerPayments[good] = (uint16_t)(explicitGross ? explicitGross * denominator / 100 : buyer);
    q.cargoCost += purchase * q.quantities[good];
    grossNumerator += (int64_t)numerator * q.quantities[good];
    projectUnits += q.projectQuantities[good];
  }
  q.grossPayment = (int32_t)((grossNumerator + denominator - 1) / denominator);
  q.duty = (int32_t)(((int64_t)q.grossPayment * q.dutyPercent + 99) / 100);
  q.buyerPayment = q.grossPayment - q.duty;
  bool full = q.quantity == q.capacity;
  q.bonus = full ? max((int32_t)0, q.buyerPayment - q.cargoCost) / 5 : 0;
  q.payout = q.buyerPayment + q.bonus;
  q.payNow = q.cargoCost + q.sailingCost;
  if (projectUnits && !full && q.mode == VOYAGE_FREIGHT) {
    // Customer-supplied project freight retains its modest guaranteed fee.
    // Company-funded trade uses its exact costs, including a deliberately
    // unprofitable partial load which the player can review and approve.
    int minimumProfit = max(9, (int)q.sailingCost / 5);
    int minimumPayment = q.payNow + minimumProfit;
    if (q.payout < minimumPayment) {
      q.buyerPayment += minimumPayment - q.payout;
      q.grossPayment = (q.buyerPayment * 100 + denominator - 1) / denominator;
      q.duty = q.grossPayment - q.buyerPayment;
      q.payout = minimumPayment;
    }
  }
}

static uint32_t voyage_fingerprint(const VoyageQuote& q) {
  uint32_t hash = 2166136261u;
  auto mix = [&](uint32_t value) { hash = (hash ^ value) * 16777619u; };
  mix(g_ships[q.shipIndex].id); mix(g_campaign.nextVoyageId); mix(g_campaign.seed); mix(sim_return_epoch());
  mix(q.origin); mix(q.destination); mix(q.good); mix(q.quantity); mix(q.mode);
  mix(q.availableQuantity); mix(q.capacity); mix(q.projectRegion); mix(q.minutes);
  mix((uint32_t)q.cargoCost); mix((uint32_t)q.sailingCost); mix((uint32_t)q.buyerPayment);
  mix((uint32_t)q.bonus); mix((uint32_t)q.withheld); mix(q.delayMinutes);
  mix((uint32_t)q.grossPayment); mix((uint32_t)q.duty); mix(q.priceVersion);
  for (uint8_t good = 0; good < GOOD_COUNT; ++good) {
    mix(q.quantities[good]); mix(q.projectQuantities[good]);
    if (q.quantities[good]) mix(q.availableQuantities[good]);
  }
  for (int i = 0; i < q.routeCount; ++i) { mix(q.route[i]); mix(q.legMinutes[i]); }
  return hash ? hash : 1u;
}

static VoyageQuote voyage_manifest_for_ship(uint8_t shipIndex, const Ship& ship, uint8_t destination,
                                            uint8_t mode, const uint8_t* quantities,
                                            int32_t availableCoins, bool planning) {
  VoyageQuote q{};
  q.code = SIM_INVALID; q.shipIndex = shipIndex; q.destination = destination;
  q.origin = q.projectRegion = q.good = NO_PORT; q.mode = mode; q.planningOnly = planning; q.priceVersion = 1;
  if (shipIndex >= g_campaign.shipCount || destination >= MAX_PORTS || mode > VOYAGE_TRADE || !quantities) return q;
  if (g_campaign.economyVersion >= 2) { q.code = SIM_LOCKED; return q; }
  memcpy(q.quantities, quantities, GOOD_COUNT);
  q.origin = ship.currentPort; q.capacity = (uint8_t)sim_ship_capacity(ship);
  if (!ship_at_port(ship) || ship.state == SHIP_DISABLED || ship.booking.state == BOOKING_SAILING) {
    q.code = SIM_BAD_SHIP_STATE; return q;
  }
  if (!sim_ship_captain(shipIndex)) { q.code = SIM_CAPTAIN_REQUIRED; return q; }
  if (ship.cargoCount) { q.code = SIM_NO_CAPACITY; return q; }
  if (!g_ports[destination].unlocked) { q.code = SIM_PORT_LOCKED; return q; }
  if (!booked_route(ship, destination, q)) { q.code = SIM_BAD_ROUTE; return q; }
  unsigned units = 0, availableTotal = 0;
  bool unavailable = false;
  for (uint8_t good = 0; good < GOOD_COUNT; ++good) {
    units += quantities[good];
    if (quantities[good] && q.good == NO_PORT) q.good = good;
    uint8_t project = project_for_order(destination, good);
    if (!port_makes(q.origin, good) || (!port_wants(destination, good) && project == NO_PORT)) {
      if (quantities[good]) unavailable = true;
      continue;
    }
    q.offeredGoods |= (uint8_t)(1u << good);
    int available;
    if (project < REGION_COUNT) {
      // Material orders have one finite quota, independent of recurring buyers.
      q.projectGoods |= (uint8_t)(1u << good);
      available = project_remaining(project, good);
      if (quantities[good]) { q.projectRegion = project; q.projectQuantities[good] = quantities[good]; }
    } else available = booking_available(destination, good);
    if (mode == VOYAGE_TRADE) available = min(available, (int)g_ports[q.origin].stock[good]);
    q.availableQuantities[good] = (uint8_t)max(0, min(available, (int)q.capacity));
    availableTotal += q.availableQuantities[good];
    if (quantities[good] > q.availableQuantities[good]) unavailable = true;
  }
  q.availableQuantity = (uint8_t)min((unsigned)q.capacity, availableTotal);
  if (units > q.capacity) { q.code = SIM_NO_CAPACITY; return q; }
  q.quantity = (uint8_t)units;
  booked_prices(ship, q);  // Populate unit prices even for an empty load builder.
  if (!units || unavailable) { q.code = SIM_CONTRACT_UNAVAILABLE; return q; }
  int32_t debt = available_unreserved_debt(g_campaign.recoveryDebt);
  int32_t withholdingBase = q.mode == VOYAGE_TRADE ? q.payout - q.cargoCost : q.payout;
  q.withheld = debt > 0 ? min(debt, max((int32_t)0, withholdingBase) * BALANCE.debtWithholdPercent / 100) : 0;
  q.arrivalCoins = q.payout - q.withheld;
  q.profit = q.arrivalCoins - q.payNow;
  q.delayMinutes = 0;
  q.fingerprint = voyage_fingerprint(q);
  if (availableCoins < q.payNow) q.code = SIM_NO_FUNDS;
  else if (q.profit <= 0 && q.mode != VOYAGE_TRADE) q.code = SIM_CONTRACT_UNAVAILABLE;
  else q.code = SIM_OK;
  return q;
}

VoyageQuote sim_voyage_manifest_offer(uint8_t shipIndex, uint8_t destination, uint8_t mode,
                                     const uint8_t* quantities) {
  return voyage_manifest_for_ship(shipIndex, g_ships[shipIndex < MAX_SHIPS ? shipIndex : 0],
                                   destination, mode, quantities, g_campaign.coins, false);
}

VoyageQuote sim_return_voyage_manifest_offer(uint8_t shipIndex, uint8_t destination, uint8_t mode,
                                            const uint8_t* quantities) {
  VoyageQuote unavailable{}; unavailable.code = SIM_BAD_SHIP_STATE; unavailable.planningOnly = 1;
  if (shipIndex >= g_campaign.shipCount || g_ships[shipIndex].booking.state != BOOKING_SAILING) return unavailable;
  Ship future = g_ships[shipIndex];
  future.currentPort = future.fromPort = future.toPort = future.booking.destination;
  future.state = SHIP_DOCKED; future.cargoCount = 0; future.booking.state = BOOKING_SETTLED;
  int64_t projected = (int64_t)g_campaign.coins + future.booking.arrivalCoins;
  int32_t coins = (int32_t)min(projected, (int64_t)INT32_MAX);
  return voyage_manifest_for_ship(shipIndex, future, destination, mode, quantities, coins, true);
}

static VoyageQuote voyage_single_offer(uint8_t shipIndex, uint8_t destination, uint8_t mode,
                                       uint8_t good, uint8_t quantity, bool planning) {
  uint8_t amounts[GOOD_COUNT]{};
  auto offer = [&]() { return planning ? sim_return_voyage_manifest_offer(shipIndex, destination, mode, amounts) :
                                         sim_voyage_manifest_offer(shipIndex, destination, mode, amounts); };
  VoyageQuote metadata = offer();
  if (good == NO_PORT && metadata.origin < MAX_PORTS)
    good = sim_voyage_recommend_good(metadata.origin, destination);
  if (good >= GOOD_COUNT) return metadata;
  amounts[good] = quantity ? quantity : metadata.availableQuantities[good];
  VoyageQuote q = offer();
  // Keep a requested good identifiable even when no units are available.
  // Per-good availability lives in availableQuantities for every caller.
  q.good = good;
  return q;
}

VoyageQuote sim_voyage_offer(uint8_t shipIndex, uint8_t destination, uint8_t mode,
                           uint8_t good, uint8_t quantity) {
  return voyage_single_offer(shipIndex, destination, mode, good, quantity, false);
}

VoyageQuote sim_return_voyage_quote(uint8_t shipIndex, uint8_t destination, uint8_t mode,
                                  uint8_t good, uint8_t quantity) {
  return voyage_single_offer(shipIndex, destination, mode, good, quantity, true);
}

uint8_t sim_voyage_destinations(uint8_t shipIndex, uint8_t* out, uint8_t capacity) {
  if (!out || !capacity || shipIndex >= g_campaign.shipCount) return 0;
  Ship future = g_ships[shipIndex];
  if (future.booking.state == BOOKING_SAILING) future.currentPort = future.booking.destination;
  uint8_t count = 0;
  uint32_t times[MAX_PORTS]; uint8_t ports[MAX_PORTS]; int total = 0;
  for (uint8_t p = 0; p < MAX_PORTS; ++p) {
    if (!g_ports[p].unlocked || p == future.currentPort) continue;
    bool matching = false;
    for (int i = 0; i < 2; ++i) {
      uint8_t good = PORT_DEFS[future.currentPort].produced[i];
      if (port_wants(p, good) || project_for_order(p, good) < REGION_COUNT) matching = true;
    }
    if (!matching) continue;
    VoyageQuote q{};
    if (!booked_route(future, p, q)) continue;
    int slot = total++;
    while (slot && times[slot - 1] > q.minutes) {
      times[slot] = times[slot - 1]; ports[slot] = ports[slot - 1]; --slot;
    }
    times[slot] = q.minutes; ports[slot] = p;
  }
  for (int i = 0; i < total && count < capacity; ++i) out[count++] = ports[i];
  return count;
}

static void install_booking(Ship& ship, const VoyageQuote& q) {
  ship.merchant = {};
  BookedVoyage& b = ship.booking;
  b = {};
  b.id = g_campaign.nextVoyageId++;
  b.state = BOOKING_SAILING; b.origin = q.origin; b.destination = q.destination;
  b.good = q.good; b.quantity = q.quantity; b.mode = q.mode; b.projectRegion = q.projectRegion;
  b.cargoCost = q.cargoCost; b.sailingCost = q.sailingCost; b.buyerPayment = q.buyerPayment;
  b.bonus = q.bonus; b.payout = q.payout; b.withheld = q.withheld;
  b.payNow = q.payNow; b.arrivalCoins = q.arrivalCoins; b.profit = q.profit;
  memcpy(b.quantities, q.quantities, GOOD_COUNT);
  memcpy(b.projectQuantities, q.projectQuantities, GOOD_COUNT);
  b.grossPayment = q.priceVersion ? q.grossPayment : q.buyerPayment;
  b.duty = q.duty; b.priceVersion = q.priceVersion;
  // Protected harbor jobs retain the existing scalar quote API.
  unsigned units = 0;
  for (uint8_t good = 0; good < GOOD_COUNT; ++good) units += b.quantities[good];
  if (!units && q.good < GOOD_COUNT) {
    b.quantities[q.good] = q.quantity;
    if (q.projectRegion < REGION_COUNT) b.projectQuantities[q.good] = q.quantity;
  }
  memcpy(b.legMinutes, q.legMinutes, sizeof(b.legMinutes));
  ship.routeCount = q.routeCount; ship.routePos = 0;
  memcpy(ship.route, q.route, sizeof(ship.route));
  ship.prepaidCost = (uint16_t)q.sailingCost; ship.manifestBonus = 0;
  for (uint8_t good = 0; good < GOOD_COUNT; ++good) for (uint8_t project = 0; project < 2; ++project) {
    uint8_t count = project ? b.projectQuantities[good] : b.quantities[good] - b.projectQuantities[good];
    if (!count) continue;
    CargoItem item{};
    item.contractId = b.id; item.kind = CARGO_BOOKED; item.good = good;
    item.quantity = count; item.destination = q.destination; item.sourcePort = q.origin;
    item.paidUnitPrice = q.unitCargoCosts[good];
    item.jobKind = JOB_FREIGHT; item.projectRegion = project ? q.projectRegion : NO_PORT;
    ship.cargo[ship.cargoCount++] = item;
  }
  arm_leg(ship);
}

SimResult sim_voyage_dispatch(uint8_t shipIndex, const VoyageQuote& quote) {
  if (g_campaign.economyVersion >= 2) return SIM_LOCKED;
  if (shipIndex >= g_campaign.shipCount || quote.shipIndex != shipIndex ||
      quote.mode > VOYAGE_TRADE || quote.planningOnly || !quote.fingerprint) return SIM_INVALID;
  VoyageQuote fresh = sim_voyage_manifest_offer(shipIndex, quote.destination, quote.mode, quote.quantities);
  if (fresh.fingerprint != quote.fingerprint) return SIM_STALE_PREVIEW;
  if (fresh.code != SIM_OK) return fresh.code;
  Ship& ship = g_ships[shipIndex];
  PortState& buyer = g_ports[fresh.destination];
  // The committed snapshot is the transaction boundary. A failed write must
  // not leave a paid voyage visible only in volatile memory.
  Campaign campaignBefore = g_campaign;
  Ship shipBefore = ship;
  PortState buyerBefore = buyer;
  PortState originBefore = g_ports[fresh.origin];
  UiState uiBefore = g_ui;
  ArrivalReceipt receiptBefore = arrivalReceipts[shipIndex];
  JournalEntry journalBefore = g_journal[g_journalHead];
  uint8_t journalHeadBefore = g_journalHead, journalCountBefore = g_journalCount;
  uint32_t epoch = g_campaign.worldMinutes / BOOKING_REFRESH_MINUTES;
  if (buyer.bookingEpoch != epoch) { memset(buyer.bookingUsed, 0, sizeof(buyer.bookingUsed)); buyer.bookingEpoch = epoch; }
  for (uint8_t good = 0; good < GOOD_COUNT; ++good) {
    buyer.bookingUsed[good] += fresh.quantities[good] - fresh.projectQuantities[good];
    if (fresh.mode == VOYAGE_TRADE) g_ports[fresh.origin].stock[good] -= fresh.quantities[good];
  }
  g_campaign.coins -= fresh.payNow;
  g_campaign.operatingCosts += fresh.sailingCost;
  install_booking(ship, fresh);
  for (uint8_t step = TUTORIAL_PORT; step <= TUTORIAL_DISPATCH; ++step) sim_tutorial_complete(step);
  if (fresh.mode == VOYAGE_TRADE) mark_market_traded();
  g_ui.selectedShip = shipIndex; g_ui.selectedPort = fresh.destination;
  g_ui.card = CARD_NONE; g_ui.scene = SCENE_WORLD;
  sim_route_clear();
  journal_add(1, "%s sailed: %+ld net.", ship.name, (long)fresh.profit);
  if (!save_write()) {
    g_campaign = campaignBefore; ship = shipBefore; buyer = buyerBefore;
    g_ports[fresh.origin] = originBefore; g_ui = uiBefore;
    arrivalReceipts[shipIndex] = receiptBefore;
    g_journal[journalHeadBefore] = journalBefore;
    g_journalHead = journalHeadBefore; g_journalCount = journalCountBefore;
    return SIM_SAVE_FAILED;
  }
  return SIM_OK;
}

VoyageQuote sim_repeat_voyage_quote(uint8_t shipIndex) {
  VoyageQuote unavailable{}; unavailable.code = SIM_TEMPLATE_REQUIRED;
  if (shipIndex >= g_campaign.shipCount) return unavailable;
  const Ship& ship = g_ships[shipIndex];
  if (ship.booking.state != BOOKING_SETTLED) return unavailable;
  uint8_t mode = ship.booking.mode == VOYAGE_RELIEF ? VOYAGE_FREIGHT : ship.booking.mode;
  // At the destination, the useful repeat is a fresh return load. No cargo
  // or cash is reserved by consulting this suggestion.
  return sim_voyage_offer(shipIndex, ship.booking.origin, mode, NO_PORT, 0);
}

SimResult sim_project_contribute(uint8_t region) {
  if (!region || region >= REGION_COUNT) return SIM_INVALID;
  if (g_campaign.regionMask & (1u << region)) return SIM_ALREADY_OWNED;
  if (region != sim_project_region() || g_campaign.deliveredUnits < 48) return SIM_LOCKED;
  uint16_t target = sim_project_target(region);
  if (g_campaign.projectCargo[region][0] < target || g_campaign.projectCargo[region][1] < target)
    return SIM_PROJECT_INCOMPLETE;
  uint16_t cost = sim_project_cost(region);
  if (g_campaign.coins < cost + BOOKING_RESERVE) return SIM_NO_FUNDS;
  Campaign campaignBefore = g_campaign;
  uint8_t unlockedBefore[MAX_PORTS];
  for (int p = 0; p < MAX_PORTS; ++p) unlockedBefore[p] = g_ports[p].unlocked;
  UiState uiBefore = g_ui;
  uint8_t headBefore = g_journalHead, countBefore = g_journalCount;
  JournalEntry journalBefore = g_journal[headBefore];
  g_campaign.coins -= cost; g_campaign.capitalSpending += cost;
  g_campaign.regionMask |= (uint8_t)(1u << region);
  g_campaign.chartMask |= (uint8_t)(1u << region);
  for (uint8_t p = 0; p < MAX_PORTS; ++p)
    if (PORT_DEFS[p].region == region) g_ports[p].unlocked = 1;
  return_reconcile_milestones();
  journal_add(2, "%s dock complete. New ports open.", REGION_NAMES[region]);
  if (!save_write()) {
    g_campaign = campaignBefore; g_ui = uiBefore;
    for (int p = 0; p < MAX_PORTS; ++p) g_ports[p].unlocked = unlockedBefore[p];
    g_journalHead = headBefore; g_journalCount = countBefore; g_journal[headBefore] = journalBefore;
    return SIM_SAVE_FAILED;
  }
  // Signed offers need no generated board. Optional legacy boards can be
  // rebuilt after this committed unlock, and again when loading the save.
  for (uint8_t p = 0; p < MAX_PORTS; ++p)
    if (PORT_DEFS[p].region == region) sim_generate_jobs(p, false);
  return SIM_OK;
}

static void settle_booking(Ship& ship) {
  BookedVoyage& b = ship.booking;
  if (b.state != BOOKING_SAILING || b.destination != ship.currentPort) return;
  int actual = 0;
  uint8_t actualProject[GOOD_COUNT]{};
  for (int i = ship.cargoCount - 1; i >= 0; --i)
    if (ship.cargo[i].kind == CARGO_BOOKED && ship.cargo[i].contractId == b.id) {
      const CargoItem& c = ship.cargo[i];
      actual += c.quantity;
      if (c.good < GOOD_COUNT && c.projectRegion == b.projectRegion && b.projectRegion < REGION_COUNT)
        actualProject[c.good] = (uint8_t)min((int)b.projectQuantities[c.good],
                                          (int)actualProject[c.good] + c.quantity);
      cargo_remove(ship, i);
    }
  uint8_t remaining = (uint8_t)(b.quantity - b.settledQuantity);
  uint8_t delivered = (uint8_t)min(actual, (int)remaining);
  // Ordinary signed voyages cannot lose cargo. Proportional settlement also
  // remains honest for a deliberately repaired/corrupt partial order.
  int32_t credited = b.quantity ? b.arrivalCoins * delivered / b.quantity : 0;
  int32_t gross = b.quantity ? b.payout * delivered / b.quantity : 0;
  int32_t withheld = b.quantity ? b.withheld * delivered / b.quantity : 0;
  int32_t cost = b.quantity ? b.cargoCost * delivered / b.quantity : 0;
  b.settledQuantity += delivered; b.state = BOOKING_SETTLED; b.receiptUnread = 1;
  b.arrivalCoins = credited; b.profit = credited - b.payNow;
  g_campaign.coins += credited;
  g_campaign.recoveryDebt -= min(g_campaign.recoveryDebt, withheld);
  if (b.mode == VOYAGE_TRADE) g_campaign.marketProfit += gross - cost;
  else g_campaign.contractRevenue += gross;
  g_campaign.deliveredUnits += delivered;
  g_campaign.companyXp = (uint16_t)min((uint32_t)65535u, (uint32_t)g_campaign.companyXp + delivered);
  g_campaign.deliveries = (uint16_t)min((uint32_t)65535u, (uint32_t)g_campaign.deliveries + delivered);
  if (g_campaign.contractsCompleted < 65535u) ++g_campaign.contractsCompleted;
  ++g_campaign.completedVoyages;
  if (b.projectRegion < REGION_COUNT) for (uint8_t material = 0; material < 2; ++material) {
    uint8_t good = material ? GOOD_IRONWARE : GOOD_TIMBER;
    uint16_t& progress = g_campaign.projectCargo[b.projectRegion][material];
    progress = (uint16_t)min((uint32_t)sim_project_target(b.projectRegion),
                           (uint32_t)progress + actualProject[good]);
  }
  ship.voyageEarned += credited;
  ship.voyageDelivered = delivered;
  return_record_booked(ship, credited, b.profit, delivered);
  CaptainProfile* captain = sim_ship_captain((uint8_t)(&ship - g_ships));
  if (captain) captain->deliveries = (uint16_t)min((uint32_t)65535u, (uint32_t)captain->deliveries + delivered);
  gain_captain_xp(ship, delivered);
  update_rank();
}
