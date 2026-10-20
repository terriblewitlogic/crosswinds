#include "game.h"

const GoodDef GOOD_DEFS[GOOD_COUNT] = {
  // marginBonus widens the made/wanted price swing for cheap goods so a hold
  // of grain pays back its outlay faster than a hold of tea, even though tea
  // still earns more per slot.
  { "Grain",      "GRN",  24, 20 },
  { "Timber",     "TMB",  38, 16 },
  { "Textiles",   "TXT",  62, 10 },
  { "Ironware",   "IRN",  78,  6 },
  { "Tea",        "TEA",  96,  0 },
  { "Spices",     "SPC", 124,  0 },
  { "Medicines",  "MED", 148,  0 },
  { "Porcelain",  "POR", 172,  0 },
};

const char* const REGION_NAMES[REGION_COUNT] = {
  "Mariner Coast", "Spyglass Isles", "Castaway Reach", "Nobody's Sea",
  "Tempest Reach", "White Horizon"
};

const BalanceDef BALANCE = {
  { 0, 6, 18, 36, 64, 100 },
  { 0, 6, 5, 5, 5, 5 },
  { 0, 550, 500, 700, 1000, 1500 },
  { 0, 150, 250, 350, 500, 700 },
  80, 25, 25, 25, 2, 5, 2,
  110, 90, 50, 200, 25, 30, 12, 4, 40, 125, 90, 25, 8, 2, 80,
  8, 70, 65, 50, 10, 5, 5, 50, 25, 20, 4, 40, 180, 120,
  2, 1
};

const PortDef PORT_DEFS[MAX_PORTS] = {
  { "Lantern Quay",     105, 30, 0, PORT_DEEPWATER, { GOOD_TEXTILES, GOOD_IRONWARE }, { GOOD_GRAIN, GOOD_TIMBER }, 5, GOOD_TEXTILES },
  { "Gullhaven",         99, 42, 0, PORT_HARBOR,    { GOOD_GRAIN, GOOD_GRAIN }, { GOOD_TEXTILES, GOOD_TEXTILES }, 0, GOOD_GRAIN },
  { "Watchman Point",    108, 19, 0, PORT_HARBOR,    { GOOD_TIMBER, GOOD_TIMBER }, { GOOD_IRONWARE, GOOD_IRONWARE }, 5, GOOD_TIMBER },
  { "Mariner Rest",     70, 34, 0, PORT_DEEPWATER, { GOOD_TIMBER, GOOD_GRAIN }, { GOOD_TEXTILES, GOOD_TEA }, 10, GOOD_TIMBER },

  { "Spyglass Bay",   61, 55, 1, PORT_DEEPWATER, { GOOD_GRAIN, GOOD_SPICES }, { GOOD_IRONWARE, GOOD_TEXTILES }, 10, GOOD_SPICES },
  { "Stockade Cay",      66, 61, 1, PORT_ANCHORAGE, { GOOD_SPICES, GOOD_TIMBER }, { GOOD_MEDICINES, GOOD_IRONWARE }, 0, GOOD_SPICES },
  { "Flint Reach",     55, 64, 1, PORT_HARBOR,    { GOOD_GRAIN, GOOD_MEDICINES }, { GOOD_TEXTILES, GOOD_PORCELAIN }, 5, GOOD_MEDICINES },
  { "Skeleton Key",     73, 64, 1, PORT_ANCHORAGE, { GOOD_TIMBER, GOOD_SPICES }, { GOOD_GRAIN, GOOD_IRONWARE }, 0, GOOD_TIMBER },

  { "Granite Haven",       76, 79, 2, PORT_DEEPWATER, { GOOD_GRAIN, GOOD_MEDICINES }, { GOOD_IRONWARE, GOOD_TEA }, 5, GOOD_GRAIN },
  { "Balloon Cape", 112, 91, 2, PORT_DEEPWATER, { GOOD_TIMBER, GOOD_GRAIN }, { GOOD_TEXTILES, GOOD_PORCELAIN }, 10, GOOD_TIMBER },
  { "Mercy Creek",     91, 68, 2, PORT_ANCHORAGE, { GOOD_MEDICINES, GOOD_SPICES }, { GOOD_TIMBER, GOOD_IRONWARE }, 0, GOOD_MEDICINES },
  { "Foundry Bay",      68, 95, 2, PORT_HARBOR,    { GOOD_GRAIN, GOOD_TEXTILES }, { GOOD_TEA, GOOD_MEDICINES }, 5, GOOD_TEXTILES },

  { "Nautilus Roads",     126, 80, 3, PORT_HARBOR,    { GOOD_TIMBER, GOOD_IRONWARE }, { GOOD_TEA, GOOD_SPICES }, 5, GOOD_IRONWARE },
  { "Coral Library", 148, 67, 3, PORT_DEEPWATER, { GOOD_TEA, GOOD_TEXTILES }, { GOOD_IRONWARE, GOOD_MEDICINES }, 10, GOOD_TEA },
  { "Nobody Cove",   138, 87, 3, PORT_ANCHORAGE, { GOOD_SPICES, GOOD_TEA }, { GOOD_GRAIN, GOOD_TIMBER }, 0, GOOD_SPICES },
  { "Pearl Anchorage",      158, 79, 3, PORT_DEEPWATER, { GOOD_TEA, GOOD_MEDICINES }, { GOOD_TEXTILES, GOOD_PORCELAIN }, 5, GOOD_MEDICINES },

  { "Prospero Quay",      178, 56, 4, PORT_DEEPWATER, { GOOD_PORCELAIN, GOOD_TEA }, { GOOD_TIMBER, GOOD_SPICES }, 10, GOOD_PORCELAIN },
  { "Ariel Bay",   184, 72, 4, PORT_HARBOR,    { GOOD_TEXTILES, GOOD_PORCELAIN }, { GOOD_GRAIN, GOOD_IRONWARE }, 5, GOOD_TEXTILES },
  { "Caliban Reach",    172, 88, 4, PORT_DEEPWATER, { GOOD_SPICES, GOOD_TEA }, { GOOD_MEDICINES, GOOD_TIMBER }, 0, GOOD_SPICES },
  { "Tempest Watch",     193, 42, 4, PORT_HARBOR,    { GOOD_PORCELAIN, GOOD_IRONWARE }, { GOOD_GRAIN, GOOD_SPICES }, 5, GOOD_IRONWARE },

  { "Whitewater Cay",   208, 84, 5, PORT_ANCHORAGE, { GOOD_SPICES, GOOD_MEDICINES }, { GOOD_IRONWARE, GOOD_TEXTILES }, 0, GOOD_MEDICINES },
  { "Tryworks",       34, 49, 5, PORT_DEEPWATER, { GOOD_TIMBER, GOOD_GRAIN }, { GOOD_TEA, GOOD_PORCELAIN }, 10, GOOD_TIMBER },
  { "Ambergris Bay",       43, 88, 5, PORT_DEEPWATER, { GOOD_GRAIN, GOOD_IRONWARE }, { GOOD_TEXTILES, GOOD_MEDICINES }, 5, GOOD_IRONWARE },
  { "Last Lantern",    15, 70, 5, PORT_HARBOR,    { GOOD_TIMBER, GOOD_SPICES }, { GOOD_GRAIN, GOOD_PORCELAIN }, 0, GOOD_SPICES },
};

const SeaLaneDef LANE_DEFS[MAX_LANES] = {
  { 0, 1, 4, 12, 1, 0 }, { 0, 2, 5, -10, 2, 0 }, { 0, 3, 13, 15, 2, 0 },
  { 1, 2, 7, -8, 2, 0 }, { 1, 3, 12, 18, 2, 0 },
  { 3, 4, 10, 18, 2, 6 }, { 1, 4, 17, 12, 3, 8 },
  { 4, 5, 3, 10, 2, 0 }, { 4, 6, 4, -8, 2, 0 }, { 5, 7, 3, 14, 3, 0 },
  { 6, 7, 4, 8, 3, 0 }, { 4, 7, 5, -10, 3, 0 },
  { 6, 8, 13, 12, 3, 8 }, { 4, 10, 11, 10, 3, 6 }, { 10, 8, 7, 16, 2, 0 },
  { 10, 9, 15, 14, 4, -8 }, { 8, 11, 8, 8, 3, 0 }, { 11, 9, 14, 18, 4, 8 },
  { 8, 9, 18, 12, 4, -6 },
  { 9, 12, 10, 14, 4, 10 }, { 9, 14, 13, 18, 4, 12 }, { 12, 14, 5, 10, 3, 8 },
  { 12, 13, 14, 8, 4, 12 }, { 14, 13, 9, -12, 4, -10 }, { 13, 15, 7, 12, 3, 10 },
  { 14, 15, 8, 14, 3, 10 },
  { 15, 16, 12, 10, 3, 10 }, { 15, 18, 8, 15, 4, 12 }, { 13, 16, 11, -8, 4, -10 },
  { 16, 17, 5, 10, 2, 0 }, { 17, 18, 6, 12, 3, 0 }, { 16, 19, 9, -12, 4, 0 },
  { 17, 19, 9, -10, 4, 0 }, { 16, 18, 9, 12, 3, 0 },
  { 18, 20, 16, 16, 4, 8 }, { 17, 20, 18, 12, 4, 8 }, { 19, 20, 20, -10, 5, -8 },
  { 20, 23, 12, 18, 4, 10 }, { 23, 21, 16, 12, 4, 8 }, { 23, 22, 18, -10, 5, -8 },
  { 21, 22, 15, -12, 5, -8 }, { 22, 11, 21, 10, 5, 10 }, { 21, 20, 25, -15, 5, -10 },
  { 7, 10, 10, 12, 3, 6 }, { 3, 21, 28, -18, 5, -12 },
};

const int LANE_COUNT = 45;

const ShipClassDef SHIP_CLASS_DEFS[SHIP_CLASS_COUNT] = {
  { "Sloop",    4, 1, 115, 12, 1, 1, 1, 0, 1200 },
  { "Schooner", 6, 1, 125, 18, 1, 1, 2, 1, 1600 },
  { "Brig",     8, 2,  96, 24, 2, 4, 2, 2, 2700 },
  { "Packet",   6, 4, 136, 20, 2, 2, 2, 2, 3300 },
  { "Indiaman",14, 2,  82, 36, 3, 5, 3, 4, 7200 },
};

const ModuleDef MODULE_DEFS[MODULE_COUNT] = {
  { "Long casks",  "+6 days, -1 hold",  420 },
  { "Bulkheads",   "-20% hull damage, -1 hold", 520 },
  { "Extra berths", "+2 passenger",      460 },
  { "Swivel guns", "+2 guns",           620 },
  { "Copper hull", "+10% speed",         760 },
  { "Storm rig",   "+2 storm handling", 680 },
  { "Charter pennant", "+1 hold, +1 gun, +5% speed", 900 },  // after the Grand Charter
};

int content_lane_between(uint8_t a, uint8_t b) {
  for (int i = 0; i < LANE_COUNT; ++i) {
    if ((LANE_DEFS[i].a == a && LANE_DEFS[i].b == b) ||
        (LANE_DEFS[i].a == b && LANE_DEFS[i].b == a)) return i;
  }
  return -1;
}

int content_hops(uint8_t from, uint8_t to, bool unlockedOnly) {
  if (from == to) return 0;
  uint8_t dist[MAX_PORTS];
  uint8_t q[MAX_PORTS];
  for (int i = 0; i < MAX_PORTS; ++i) dist[i] = 0xFF;
  int head = 0, tail = 0;
  dist[from] = 0; q[tail++] = from;
  while (head < tail) {
    uint8_t p = q[head++];
    for (int i = 0; i < LANE_COUNT; ++i) {
      int n = -1;
      if (LANE_DEFS[i].a == p) n = LANE_DEFS[i].b;
      else if (LANE_DEFS[i].b == p) n = LANE_DEFS[i].a;
      if (n < 0 || dist[n] != 0xFF) continue;
      if (unlockedOnly && !g_ports[n].unlocked) continue;
      dist[n] = dist[p] + 1;
      if (n == to) return dist[n];
      q[tail++] = (uint8_t)n;
    }
  }
  return -1;
}

uint16_t content_market_price(uint8_t port, uint8_t good, bool buying) {
  if (port >= MAX_PORTS || good >= GOOD_COUNT) return 0;
  return content_market_price_at_stock(port, good, buying, g_ports[port].stock[good]);
}

uint16_t content_market_price_at_stock(uint8_t port, uint8_t good, bool buying, uint16_t stock) {
  if (port >= MAX_PORTS || good >= GOOD_COUNT) return 0;
  int modifier = 100;
  const PortDef& pd = PORT_DEFS[port];
  bool produced = pd.produced[0] == good || pd.produced[1] == good;
  bool demanded = pd.demanded[0] == good || pd.demanded[1] == good;
  if (produced) modifier -= BALANCE.productionDiscountPoints;
  if (demanded) modifier += BALANCE.demandPremiumPoints;
  int pressure = (BALANCE.marketStockTarget - (int)stock) *
                 BALANCE.marketStockPressurePerUnit;
  if (pressure < -(int)BALANCE.marketStockPressureLimit)
    pressure = -(int)BALANCE.marketStockPressureLimit;
  if (pressure > BALANCE.marketStockPressureLimit)
    pressure = BALANCE.marketStockPressureLimit;
  modifier += pressure;
  if (g_campaign.worldEventRegion == pd.region && g_campaign.worldEventGood == good) {
    if (g_campaign.worldEventType == EVENT_SHORTAGE) modifier += BALANCE.worldEventMarketPoints;
    else if (g_campaign.worldEventType == EVENT_GLUT) modifier -= BALANCE.worldEventMarketPoints;
  }
  if (modifier < BALANCE.marketMinPercent) modifier = BALANCE.marketMinPercent;
  if (modifier > BALANCE.marketMaxPercent) modifier = BALANCE.marketMaxPercent;
  int price = GOOD_DEFS[good].basePrice * modifier / 100;
  // The per-good margin bonus is applied after the clamp: a producing port's
  // stock keeps the modifier at the floor, which would otherwise swallow it.
  if (produced) price = price * (100 - GOOD_DEFS[good].marginBonus) / 100;
  if (demanded) price = price * (100 + GOOD_DEFS[good].marginBonus) / 100;
  if (pd.specialty == good) price = price * BALANCE.specialtyBuyPercent / 100;
  price = price * (buying ? BALANCE.marketBuyPercent : BALANCE.marketSellPercent) / 100;
  if (buying) price = (price * (100 + pd.duty) + 99) / 100;
  else price = price * (100 - pd.duty) / 100;
  return (uint16_t)(price < 1 ? 1 : price);
}

const char* content_cargo_name(uint8_t cargoKind, uint8_t jobKind, uint8_t good, bool shortName) {
  if (cargoKind == CARGO_CONTRACT && jobKind == JOB_PACKAGE) return "Parcel";
  if (cargoKind == CARGO_CONTRACT && jobKind == JOB_STORY) return "Keepsake";
  if (cargoKind == CARGO_CONTRACT && jobKind == JOB_MAIL) return "Mail";
  if (cargoKind == CARGO_CONTRACT && jobKind == JOB_PASSENGER) return shortName ? "Pass." : "Passengers";
  if (cargoKind == CARGO_CONTRACT && jobKind == JOB_CHARTER) return shortName ? "Charter" : "Charter cargo";
  if (good >= GOOD_COUNT) return "cargo";
  return shortName ? GOOD_DEFS[good].shortName : GOOD_DEFS[good].name;
}

const char* content_rank_name(uint8_t rank) {
  static const char* const names[] = {
    "Trader", "Broker", "Merchant", "Shipowner", "Magnate", "Grand Charter"
  };
  return names[rank < 6 ? rank : 5];
}

uint8_t content_captain_portrait(const char* name, uint8_t trait) {
  uint32_t hash = 2166136261u;
  if (name) for (const unsigned char* p = (const unsigned char*)name; *p; ++p) {
    hash ^= *p;
    hash *= 16777619u;
  }
  hash ^= trait;
  hash *= 16777619u;
  return (uint8_t)((hash ^ (hash >> 16)) & 15u);
}

const char* content_trait_name(uint8_t trait) {
  static const char* const names[] = {
    "Navigator", "Gunner", "Merchant", "Surgeon", "Quartermaster"
  };
  return names[trait < 5 ? trait : 0];
}
