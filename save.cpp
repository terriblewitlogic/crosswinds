#include "game.h"
#include "story_content.h"
#include "atlas_content.h"
#include <LittleFS.h>
#if defined(ESP_PLATFORM)
#include <esp_partition.h>
#endif
#include <stdlib.h>
#include <string.h>

enum ChunkId : uint8_t { CH_FLEET, CH_PORTS, CH_JOBS, CH_JOURNAL, CH_CORE, CH_COUNT };
static const char* const CH_PATH[CH_COUNT] = {
  "/sh_fleet.sav", "/sh_ports.sav", "/sh_jobs.sav", "/sh_log.sav", "/sh_core.sav"
};
static const char* const BAK_PATH[CH_COUNT] = {
  "/sh_fleet.bak", "/sh_ports.bak", "/sh_jobs.bak", "/sh_log.bak", "/sh_core.bak"
};
static const char* const TMP_PATH = "/sh_tmp.sav";
static constexpr uint32_t CH_MAGIC = 0x31504853u;  // SHP1
static constexpr uint8_t CH_VERSION = 15;
static constexpr uint8_t CH_OLDEST_VERSION = 2;
static constexpr size_t BUF_CAP = 8192;

static uint8_t* buf = nullptr;
static size_t pos = 0;
static bool mounted = false;
static uint32_t lastCrc[CH_COUNT];
static uint32_t expectedCrc[CH_COUNT];
// The generation before the current one, per chunk: a .bak file carries it,
// so a sibling that fell back to its backup is still accepted.
static uint32_t prevCrc[CH_COUNT];
static uint32_t prevExpectedCrc[CH_COUNT];
static bool currentTrusted[CH_COUNT];
// The layout version build_chunk emits; the self-test lowers it to fabricate
// older records, everything else writes the current one.
static uint8_t emitVersion = CH_VERSION;
static bool writesSuspended = false;
static constexpr uint32_t SNAP_MAGIC = 0x42424853u;  // SHBB (version 11)
static constexpr uint32_t SNAP_END = 0x42454853u;    // SHEB
static constexpr uint32_t SNAP_V10_MAGIC = 0x41424853u;
static constexpr uint32_t SNAP_V10_END = 0x41454853u;
static constexpr uint32_t SNAP_V9_MAGIC = 0x39424853u;
static constexpr uint32_t SNAP_V9_END = 0x39454853u;
static constexpr uint32_t SNAP_V8_MAGIC = 0x38424853u;
static constexpr uint32_t SNAP_V8_END = 0x38454853u;
static const char* const SNAP_PATH = "/sh_company.sav";
static const char* const SNAP_BAK = "/sh_company.bak";
static const char* const SNAP_TMP = "/sh_company.tmp";
static uint32_t snapshotGeneration = 0;
static uint32_t snapshotCrc[CH_COUNT]{};
static bool snapshotTrusted = false;
static uint8_t loadedVersion = CH_VERSION;

static uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t crc = ~0u;
  while (n--) {
    crc ^= *p++;
    for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

static void put8(uint8_t v) { if (pos < BUF_CAP) buf[pos] = v; ++pos; }
static void put16(uint16_t v) { put8(v & 0xFF); put8(v >> 8); }
static void put32(uint32_t v) { put16(v & 0xFFFF); put16(v >> 16); }
static void puti32(int32_t v) { put32((uint32_t)v); }
static void puti64(int64_t v) { put32((uint32_t)v); put32((uint32_t)((uint64_t)v >> 32)); }
static void putbytes(const void* src, size_t n) {
  if (pos + n <= BUF_CAP) memcpy(buf + pos, src, n);
  pos += n;
}

struct Reader {
  const uint8_t* data;
  size_t len;
  size_t at;
  bool ok;
  uint8_t u8() {
    if (at + 1 > len) { ok = false; return 0; }
    return data[at++];
  }
  uint16_t u16() { uint16_t a = u8(), b = u8(); return (uint16_t)(a | (b << 8)); }
  uint32_t u32() { uint32_t a = u16(), b = u16(); return a | (b << 16); }
  int32_t i32() { return (int32_t)u32(); }
  int64_t i64() { uint64_t a = u32(), b = u32(); return (int64_t)(a | (b << 32)); }
  void bytes(void* out, size_t n) {
    if (at + n > len) { ok = false; memset(out, 0, n); return; }
    memcpy(out, data + at, n); at += n;
  }
};

static void put_return_tally(const ReturnTally& t) {
  put32(t.revision); put32(t.bookedArrivals); put32(t.legacyArrivals);
  put32(t.bookedCargo); put32(t.legacyCargo);
  puti64(t.bookedCoins); puti64(t.bookedProfit); puti64(t.legacyCoins);
}
static ReturnTally get_return_tally(Reader& r) {
  ReturnTally t{}; t.revision = r.u32(); t.bookedArrivals = r.u32(); t.legacyArrivals = r.u32();
  t.bookedCargo = r.u32(); t.legacyCargo = r.u32();
  t.bookedCoins = r.i64(); t.bookedProfit = r.i64(); t.legacyCoins = r.i64(); return t;
}
static bool valid_return_tally(const ReturnTally& t) {
  if (t.bookedCoins < 0 || t.legacyCoins < 0 || t.bookedProfit > t.bookedCoins) return false;
  if (t.revision) return true;
  return !t.bookedArrivals && !t.legacyArrivals && !t.bookedCargo && !t.legacyCargo &&
    !t.bookedCoins && !t.bookedProfit && !t.legacyCoins;
}
static bool valid_return_ledger(const ReturnLedger& ledger) {
  const ReturnTally& t = ledger.total; const ReturnTally& s = ledger.seen;
  if (!valid_return_tally(t) || !valid_return_tally(s) || s.revision > t.revision ||
      s.bookedArrivals > t.bookedArrivals || s.legacyArrivals > t.legacyArrivals ||
      s.bookedCargo > t.bookedCargo || s.legacyCargo > t.legacyCargo ||
      s.bookedCoins > t.bookedCoins || s.legacyCoins > t.legacyCoins) return false;
  return s.revision != t.revision || (s.bookedArrivals == t.bookedArrivals &&
    s.legacyArrivals == t.legacyArrivals && s.bookedCargo == t.bookedCargo &&
    s.legacyCargo == t.legacyCargo && s.bookedCoins == t.bookedCoins &&
    s.bookedProfit == t.bookedProfit && s.legacyCoins == t.legacyCoins);
}

static void put_cargo(const CargoItem& c) {
  put32(c.contractId); put16(c.paidUnitPrice); put8(c.good); put8(c.quantity);
  put8(c.destination); put8(c.kind); put8(c.jobKind); put8(c.projectRegion);
  if (emitVersion >= 8) { put8(c.sourcePort); put8(c.sailed); }
}

static CargoItem get_cargo(Reader& r, uint8_t version) {
  CargoItem c{};
  c.contractId = r.u32(); c.paidUnitPrice = r.u16(); c.good = r.u8();
  c.quantity = r.u8(); c.destination = r.u8(); c.kind = r.u8();
  c.jobKind = r.u8(); c.projectRegion = r.u8();
  c.sourcePort = version >= 8 ? r.u8() : NO_PORT;
  c.sailed = version >= 8 ? r.u8() : 0;
  return c;
}

static void put_booking(const BookedVoyage& b) {
  put32(b.id); puti32(b.cargoCost); puti32(b.sailingCost); puti32(b.buyerPayment);
  puti32(b.bonus); puti32(b.payout); puti32(b.withheld); puti32(b.payNow);
  puti32(b.arrivalCoins); puti32(b.profit);
  for (int j = 0; j < MAX_ROUTE_LEGS; ++j) put16(b.legMinutes[j]);
  put8(b.state); put8(b.origin); put8(b.destination); put8(b.good);
  put8(b.quantity); put8(b.settledQuantity); put8(b.mode); put8(b.projectRegion);
  put8(b.receiptUnread);
  if (emitVersion >= 10) {
    putbytes(b.quantities, GOOD_COUNT); putbytes(b.projectQuantities, GOOD_COUNT);
    puti32(b.grossPayment); puti32(b.duty); put8(b.priceVersion);
  }
}

static BookedVoyage get_booking(Reader& r, uint8_t version) {
  BookedVoyage b{};
  b.id = r.u32(); b.cargoCost = r.i32(); b.sailingCost = r.i32(); b.buyerPayment = r.i32();
  b.bonus = r.i32(); b.payout = r.i32(); b.withheld = r.i32(); b.payNow = r.i32();
  b.arrivalCoins = r.i32(); b.profit = r.i32();
  for (int j = 0; j < MAX_ROUTE_LEGS; ++j) b.legMinutes[j] = r.u16();
  b.state = r.u8(); b.origin = r.u8(); b.destination = r.u8(); b.good = r.u8();
  b.quantity = r.u8(); b.settledQuantity = r.u8(); b.mode = r.u8(); b.projectRegion = r.u8();
  b.receiptUnread = r.u8();
  if (version >= 10) {
    r.bytes(b.quantities, GOOD_COUNT); r.bytes(b.projectQuantities, GOOD_COUNT);
    b.grossPayment = r.i32(); b.duty = r.i32(); b.priceVersion = r.u8();
  } else if (b.state != BOOKING_NONE && b.good < GOOD_COUNT) {
    // Old signed terms are authoritative. Expand only their manifest; never
    // re-price an active trip or invent a historical destination duty.
    b.quantities[b.good] = b.quantity;
    if (b.projectRegion < REGION_COUNT) b.projectQuantities[b.good] = b.quantity;
    b.grossPayment = b.buyerPayment;
  }
  return b;
}

static void put_merchant(const MerchantVoyage& m) {
  put32(m.id);
  for (uint8_t leg = 0; leg < MAX_ROUTE_LEGS; ++leg) put16(m.legMinutes[leg]);
  puti32(m.packageCoins); put16(m.packagesDelivered);
  put8(m.state); put8(m.origin); put8(m.destination); put8(m.receiptUnread);
}

static MerchantVoyage get_merchant(Reader& r) {
  MerchantVoyage m{}; m.id = r.u32();
  for (uint8_t leg = 0; leg < MAX_ROUTE_LEGS; ++leg) m.legMinutes[leg] = r.u16();
  m.packageCoins = r.i32(); m.packagesDelivered = r.u16();
  m.state = r.u8(); m.origin = r.u8(); m.destination = r.u8(); m.receiptUnread = r.u8();
  return m;
}

// Charted pages carry a day and a charting ship; uncharted ones carry nothing.
static bool valid_atlas_state() {
  const AtlasState& a = g_atlas;
  if (!(a.charted & 1u) || (a.charted >> ATLAS_PORTS) || (a.regionsRewarded & ~0x3Fu)) return false;
  uint8_t full = 0x3F;
  for (uint8_t p = 0; p < ATLAS_PORTS; ++p) {
    if (a.charted & (1u << p)) {
      if (!a.day[p] || (a.ship[p] >= MAX_SHIPS && a.ship[p] != ATLAS_HOME)) return false;
    } else {
      if (a.day[p] || a.ship[p] || a.voyage[p]) return false;
      full &= (uint8_t)~(1u << PORT_DEFS[p].region);
    }
  }
  return !(a.regionsRewarded & ~full);
}

static bool valid_atlas_links() {
  for (uint8_t p = 0; p < ATLAS_PORTS; ++p)
    if ((g_atlas.charted & (1u << p)) && g_atlas.ship[p] != ATLAS_HOME && g_atlas.ship[p] >= g_campaign.shipCount)
      return false;
  return true;
}

// Delivery order, counts and the single live offer or keepsake must agree.
static bool valid_story_state() {
  const Campaign& c = g_campaign;
  if ((c.storyDelivered & ~STORY_ALL_DELIVERED) || c.storyDeliveries > STORY_CHAPTERS) return false;
  uint8_t seen = 0;
  for (uint8_t i = 0; i < STORY_CHAPTERS; ++i) {
    uint8_t chapter = c.storyOrder[i];
    if (i >= c.storyDeliveries) { if (chapter != STORY_NONE) return false; continue; }
    if (chapter >= STORY_CHAPTERS || (seen & (1u << chapter))) return false;
    if ((i == 0) != (chapter == 0)) return false;
    if (chapter == STORY_CHAPTERS - 1 && i != STORY_CHAPTERS - 1) return false;
    seen |= (uint8_t)(1u << chapter);
  }
  auto open = [&](uint8_t chapter) { return chapter < STORY_CHAPTERS && !(seen & (1u << chapter)); };
  bool offer = c.storyOfferChapter != STORY_NONE;
  return seen == c.storyDelivered &&
    (c.storyCarrying == STORY_NONE || open(c.storyCarrying)) &&
    (!offer || (open(c.storyOfferChapter) && c.storyOfferShipId && c.storyOfferPort < MAX_PORTS &&
                c.storyCarrying == STORY_NONE)) &&
    (offer || (!c.storyOfferShipId && c.storyOfferPort == NO_PORT)) &&
    (c.storyLastPort == NO_PORT || c.storyLastPort < MAX_PORTS) &&
    c.storyCooldown <= STORY_COOLDOWN_DECLINED &&
    (c.storyCardPending == STORY_NONE ||
     (c.storyCardPending < STORY_CHAPTERS && (seen & (1u << c.storyCardPending))));
}

static bool valid_cargo(const CargoItem& c, bool warehouse, uint8_t version) {
  if (version >= 14 && c.jobKind == JOB_STORY) {
    uint32_t chapter = c.contractId - 0x53540000u;
    return !warehouse && c.kind == CARGO_CONTRACT && c.good == NO_PORT && c.quantity == 1 &&
      chapter < STORY_CHAPTERS && c.destination == STORY_CHAPTER_DEFS[chapter].port &&
      !c.paidUnitPrice && c.projectRegion == NO_PORT && c.sourcePort < MAX_PORTS &&
      c.sourcePort != c.destination && c.sailed <= 1;
  }
  if (version >= 11 && c.jobKind == JOB_PACKAGE)
    return !warehouse && c.kind == CARGO_CONTRACT && c.good == NO_PORT &&
      c.quantity == 1 && c.contractId && !c.paidUnitPrice &&
      c.destination < MAX_PORTS && c.projectRegion == NO_PORT &&
      c.sourcePort < MAX_PORTS && c.sourcePort != c.destination && c.sailed <= 1;
  return c.good < GOOD_COUNT && c.quantity && c.kind <= CARGO_BOOKED &&
    (!warehouse || c.kind != CARGO_BOOKED) && c.jobKind <= JOB_CHARTER &&
    (c.destination == NO_PORT || c.destination < MAX_PORTS) &&
    (c.projectRegion == NO_PORT || c.projectRegion < REGION_COUNT) &&
    (c.sourcePort == NO_PORT || c.sourcePort < MAX_PORTS) && c.sailed <= 1 &&
    (c.kind != CARGO_MARKET || c.contractId < c.quantity);
}

static void put_route_template(const RouteTemplate& t) {
  put8(t.origin);
  for (int i = 0; i < MAX_ROUTE_LEGS; ++i) put8(t.stops[i]);
  put8(t.stopCount); put8(t.loadMode); put8(t.sellMode); put8(t.buyGood);
  put16(t.maxBuyPrice);
}

static RouteTemplate get_route_template(Reader& r) {
  RouteTemplate t{};
  t.origin = r.u8();
  for (int i = 0; i < MAX_ROUTE_LEGS; ++i) t.stops[i] = r.u8();
  t.stopCount = r.u8(); t.loadMode = r.u8(); t.sellMode = r.u8();
  t.buyGood = r.u8(); t.maxBuyPrice = r.u16();
  return t;
}

static void put_captain_profile(const CaptainProfile& c) {
  putbytes(c.name, sizeof(c.name)); put16(c.xp); put16(c.hireCost);
  put8(c.trait); put8(c.level); put8(c.loyalty); put8(c.scars[0]); put8(c.scars[1]);
  put8(c.scarCount); put8(c.assignedShip); put8(c.currentPort); put8(c.active);
  put16(c.voyages); put16(c.deliveries); put16(c.rescues); put16(c.incidentsSurvived);
  if (emitVersion >= 13) put8(c.portrait);
}

static CaptainProfile get_captain_profile(Reader& r, uint8_t version) {
  CaptainProfile c{};
  r.bytes(c.name, sizeof(c.name)); c.name[sizeof(c.name) - 1] = 0;
  c.xp = r.u16(); c.hireCost = r.u16(); c.trait = r.u8(); c.level = r.u8();
  c.loyalty = r.u8(); c.scars[0] = r.u8(); c.scars[1] = r.u8();
  c.scarCount = r.u8(); c.assignedShip = r.u8(); c.currentPort = r.u8(); c.active = r.u8();
  if (version >= 4) {
    c.voyages = r.u16(); c.deliveries = r.u16();
    c.rescues = r.u16(); c.incidentsSurvived = r.u16();
  }
  c.portrait = version >= 13 ? r.u8() : content_captain_portrait(c.name, c.trait);
  if (c.portrait > 15) r.ok = false;
  return c;
}

static void put_contract(const Contract& c) {
  put32(c.id); put32(c.deadlineMinute); put16(c.reward); put8(c.origin);
  put8(c.destination); put8(c.good); put8(c.quantity); put8(c.kind);
  put8(c.projectRegion); put8(c.active);
}

static Contract get_contract(Reader& r) {
  Contract c{};
  c.id = r.u32(); c.deadlineMinute = r.u32(); c.reward = r.u16();
  c.origin = r.u8(); c.destination = r.u8(); c.good = r.u8();
  c.quantity = r.u8(); c.kind = r.u8(); c.projectRegion = r.u8(); c.active = r.u8();
  return c;
}

static size_t build_chunk(uint8_t id) {
  pos = 0;
  if (id == CH_CORE) {
    put32(g_campaign.seed); put32(g_campaign.rng); put32(g_campaign.worldMinutes);
    put32(g_campaign.nextContractId); put32(g_campaign.nextShipId); puti32(g_campaign.coins);
    put16(g_campaign.companyXp); put16(g_campaign.deliveries);
    put16(g_campaign.contractsCompleted); put16(g_campaign.incidentsResolved);
    for (int i = 0; i < REGION_COUNT; ++i) put16(g_campaign.charterProgress[i]);
    put32(g_campaign.contractRevenue); puti32(g_campaign.marketProfit);
    put32(g_campaign.operatingCosts); put8(g_campaign.rank); put8(g_campaign.regionMask);
    put8(g_campaign.chartMask); put8(g_campaign.visitedMask); put8(g_campaign.shipCount);
    put8(g_campaign.won); put8(g_campaign.paused); put8(g_campaign.soundOn);
    put8(g_campaign.timeScale);
    put8(g_campaign.worldEventType); put8(g_campaign.worldEventGood);
    put8(g_campaign.worldEventRegion); put8(g_campaign.worldEventDays);
    put16(g_campaign.lastMarketDay);
    put8(emitVersion < 10 && g_ui.scene == SCENE_FLEET ? SCENE_COMPANY : g_ui.scene);
    put8(g_ui.selectedPort); put8(g_ui.selectedShip); put8(g_ui.selectedGood);
    for (int i = 0; i < CH_CORE; ++i) put32(emitVersion >= 8 ? 0 : lastCrc[i]);
    puti32(g_campaign.recoveryDebt); put32(g_campaign.nextAttentionId);
    put32(g_campaign.recruitmentWeek); put8(g_campaign.tutorialStep);
    put8(g_campaign.tutorialDismissed); put8(g_campaign.captainCount);
    put8(g_campaign.tutorialFlags);
    put8(g_campaign.stageSeen);
    if (emitVersion >= 7) {
      for (int i = 0; i < CH_CORE; ++i) put32(emitVersion >= 8 ? 0 : prevCrc[i]);
      puti32(g_campaign.capitalSpending); puti32(g_campaign.incidentNet);
    }
    if (emitVersion >= 8) {
      put8(emitVersion < 11 && g_campaign.economyVersion == 2 ? 1 : g_campaign.economyVersion);
      put32(g_campaign.nextVoyageId);
      put32(g_campaign.deliveredUnits); put32(g_campaign.completedVoyages);
      for (int i = 0; i < REGION_COUNT; ++i)
        for (int j = 0; j < 2; ++j) put16(g_campaign.projectCargo[i][j]);
      for (int i = 0; i < REGION_COUNT; ++i) put16(g_campaign.projectWorksCredit[i]);
      put16(g_campaign.legacyFeatureMask);
      put32(g_campaign.standbySessionId); put32(g_campaign.standbyAccountedRealSeconds);
    }
    if (emitVersion >= 9) { put32(g_campaign.returnMilestones); put32(g_campaign.returnMilestonesSeen); }
    if (emitVersion >= 13) put8(g_campaign.onboardingStep);
    if (emitVersion >= 14) {
      put8(g_campaign.storyDelivered); put8(g_campaign.storyDeliveries);
      putbytes(g_campaign.storyOrder, sizeof(g_campaign.storyOrder));
      put8(g_campaign.storyCarrying); put32(g_campaign.storyOfferShipId);
      put8(g_campaign.storyOfferPort); put8(g_campaign.storyOfferChapter);
      put8(g_campaign.storyLastPort); put8(g_campaign.storyCooldown);
      put8(g_campaign.storyCardPending); put8(g_campaign.storyOffers);
    }
    if (emitVersion >= 15) {
      put32(g_atlas.charted); put8(g_atlas.regionsRewarded);
      for (uint8_t p = 0; p < ATLAS_PORTS; ++p) { put32(g_atlas.voyage[p]); put16(g_atlas.day[p]); put8(g_atlas.ship[p]); }
    }
  } else if (id == CH_FLEET) {
    put8(g_campaign.shipCount);
    for (int i = 0; i < g_campaign.shipCount; ++i) {
      const Ship& s = g_ships[i];
      put32(s.id); putbytes(s.name, sizeof(s.name));
      put8(s.classId); put8(s.state); put8(s.currentPort); put8(s.fromPort); put8(s.toPort);
      put8(s.captainId);
      for (int j = 0; j < MAX_ROUTE_LEGS; ++j) put8(s.route[j]);
      put8(s.routeCount); put8(s.routePos); put_route_template(s.routeTemplate);
      put8(s.cargoCount); put8(s.condition);
      put8(s.morale); put8(s.crew); put8(s.modules[0]); put8(s.modules[1]);
      put8(s.incidentType); put8(s.incidentArmed); put8(s.manifestBonus);
      put16(s.legElapsedMinutes); put16(s.legTotalMinutes); put16(s.prepaidCost);
      if (emitVersion >= 7) { puti32(s.voyageEarned); put16(s.voyageDelivered); }
      if (emitVersion >= 8) put_booking(s.booking);
      if (emitVersion >= 9) { put_return_tally(s.returnLedger.total); put_return_tally(s.returnLedger.seen); }
      if (emitVersion >= 11) put_merchant(s.merchant);
      if (emitVersion >= 12) { put16(s.encounterWait); put8(s.lastEncounter); put8(s.sighting); put8(s.autoEncounter); put16(s.autoDelay); }
      for (int j = 0; j < s.cargoCount; ++j) put_cargo(s.cargo[j]);
    }
    put8(g_incident.active); put8(g_incident.shipIndex); put8(g_incident.kind); put8(g_incident.risk);
    put8(g_incidentQueueCount);
    for (int i = 0; i < g_incidentQueueCount; ++i) {
      put8(g_incidentQueue[i].active); put8(g_incidentQueue[i].shipIndex);
      put8(g_incidentQueue[i].kind); put8(g_incidentQueue[i].risk);
    }
    put8(g_campaign.captainCount);
    for (int i = 0; i < g_campaign.captainCount; ++i) put_captain_profile(g_captains[i]);
  } else if (id == CH_PORTS) {
    put8(MAX_PORTS);
    for (int i = 0; i < MAX_PORTS; ++i) {
      const PortState& p = g_ports[i];
      put8(p.unlocked); put8(p.officeLevel); put8(p.warehouseLevel);
      put8(p.shipyardLevel); put8(p.warehouseCount);
      for (int g = 0; g < GOOD_COUNT; ++g) put16(p.stock[g]);
      for (int j = 0; j < p.warehouseCount; ++j) put_cargo(p.warehouse[j]);
      for (int g = 0; g < GOOD_COUNT; ++g)
        for (int d = 0; d < 7; ++d) put8(p.priceHistory[g][d]);
      put8(p.historyHead);
      if (emitVersion >= 8) {
        put32(p.bookingEpoch);
        for (int g = 0; g < GOOD_COUNT; ++g) put16(p.bookingUsed[g]);
      }
      if (emitVersion >= 11) put32(p.packageEpoch);
    }
  } else if (id == CH_JOBS) {
    put8(MAX_PORTS); put8(MAX_CONTRACTS_PER_PORT);
    for (int p = 0; p < MAX_PORTS; ++p)
      for (int j = 0; j < MAX_CONTRACTS_PER_PORT; ++j) {
        const Contract& c = g_contracts[p][j];
        // Only host migration fixtures emit an old wire version. New,
        // unaccepted offers did not exist on that wire; accepted promises
        // cannot be represented there and must never be silently discarded.
        if (emitVersion < 11 && c.kind == JOB_PACKAGE) {
          if (c.active == 2) return 0;
          put_contract(Contract{});
        } else put_contract(c);
      }
  } else if (id == CH_JOURNAL) {
    put8(g_journalHead); put8(g_journalCount);
    for (int i = 0; i < MAX_JOURNAL; ++i) {
      put32(g_journal[i].worldMinute); put8(g_journal[i].severity);
      putbytes(g_journal[i].text, sizeof(g_journal[i].text));
    }
    put8(g_attentionCount);
    for (int i = 0; i < g_attentionCount; ++i) {
      const AttentionEntry& a = g_attention[i];
      put32(a.id); put32(a.worldMinute); put8(a.kind); put8(a.shipIndex);
      put8(a.port); put8(a.urgent); put8(a.unread); put8(a.resolved);
      if (emitVersion >= 7) put8(a.region);
    }
  }
  return pos <= BUF_CAP ? pos : 0;
}

static bool apply_chunk(uint8_t id, const uint8_t* data, size_t len, uint8_t version) {
  Reader r{data, len, 0, true};
  if (id == CH_CORE) {
    g_campaign.seed = r.u32(); g_campaign.rng = r.u32(); g_campaign.worldMinutes = r.u32();
    g_campaign.nextContractId = r.u32(); g_campaign.nextShipId = r.u32();
    g_campaign.coins = r.i32(); g_campaign.companyXp = r.u16();
    g_campaign.deliveries = r.u16(); g_campaign.contractsCompleted = r.u16();
    g_campaign.incidentsResolved = r.u16();
    for (int i = 0; i < REGION_COUNT; ++i) g_campaign.charterProgress[i] = r.u16();
    g_campaign.contractRevenue = r.u32(); g_campaign.marketProfit = r.i32();
    g_campaign.operatingCosts = r.u32(); g_campaign.rank = r.u8();
    g_campaign.regionMask = r.u8(); g_campaign.chartMask = r.u8();
    g_campaign.visitedMask = r.u8(); uint8_t savedShipCount = r.u8();
    g_campaign.won = r.u8(); g_campaign.paused = r.u8(); g_campaign.soundOn = r.u8();
    g_campaign.timeScale = version >= 5 ? r.u8() : 1;
    g_campaign.worldEventType = r.u8(); g_campaign.worldEventGood = r.u8();
    g_campaign.worldEventRegion = r.u8(); g_campaign.worldEventDays = r.u8();
    g_campaign.lastMarketDay = r.u16(); g_ui.scene = r.u8();
    g_ui.selectedPort = r.u8(); g_ui.selectedShip = r.u8(); g_ui.selectedGood = r.u8();
    for (int i = 0; i < CH_CORE; ++i) expectedCrc[i] = r.u32();
    if (version >= 3) {
      g_campaign.recoveryDebt = r.i32(); g_campaign.nextAttentionId = r.u32();
      g_campaign.recruitmentWeek = r.u32(); g_campaign.tutorialStep = r.u8();
      g_campaign.tutorialDismissed = r.u8(); g_campaign.captainCount = r.u8();
      g_campaign.tutorialFlags = r.at < r.len ? r.u8() :
        (g_campaign.tutorialStep >= TUTORIAL_COUNT ? 0xFFu :
         (uint8_t)((1u << g_campaign.tutorialStep) - 1u));
      // Version 6 stores the stage; older saves derive it after loading.
      g_campaign.stageSeen = version >= 6 && r.at < r.len ? r.u8() : 0;
    } else {
      g_campaign.recoveryDebt = 0; g_campaign.nextAttentionId = 0;
      g_campaign.recruitmentWeek = g_campaign.worldMinutes / (7u * 1440u);
      g_campaign.tutorialStep = 8; g_campaign.tutorialDismissed = 1;
      g_campaign.tutorialFlags = 0xFFu;
      g_campaign.captainCount = 0;
      g_campaign.stageSeen = 0;
    }
    // Version 7 records the previous generation's CRCs and the two ledger
    // aggregates; older cores accept only the current generation.
    if (version >= 7 && r.at + 4 * CH_CORE <= r.len)
      for (int i = 0; i < CH_CORE; ++i) prevExpectedCrc[i] = r.u32();
    else
      for (int i = 0; i < CH_CORE; ++i) prevExpectedCrc[i] = expectedCrc[i];
    g_campaign.capitalSpending = version >= 7 && r.at + 4 <= r.len ? r.i32() : 0;
    g_campaign.incidentNet = version >= 7 && r.at + 4 <= r.len ? r.i32() : 0;
    g_campaign.economyVersion = version >= 8 ? r.u8() : 1;
    g_campaign.nextVoyageId = version >= 8 ? r.u32() : 1;
    g_campaign.deliveredUnits = version >= 8 ? r.u32() : g_campaign.deliveries;
    g_campaign.completedVoyages = version >= 8 ? r.u32() : 0;
    for (int i = 0; i < REGION_COUNT; ++i)
      for (int j = 0; j < 2; ++j) g_campaign.projectCargo[i][j] = version >= 8 ? r.u16() : 0;
    for (int i = 0; i < REGION_COUNT; ++i) g_campaign.projectWorksCredit[i] = version >= 8 ? r.u16() : 0;
    g_campaign.legacyFeatureMask = version >= 8 ? r.u16() : 0;
    if (version < 8) {
      // Versions before stageSeen existed still earned their tools. Derive
      // the old progression before switching to Delivered-unit thresholds.
      uint8_t legacyStage = 0;
      if (g_campaign.contractsCompleted || g_campaign.deliveries) {
        legacyStage = 1;
        bool traded = (g_campaign.tutorialFlags & (1u << TUTORIAL_MARKET)) ||
          g_campaign.marketProfit != 0;
        if (g_campaign.rank >= 1 || traded) {
          legacyStage = 2;
          if (savedShipCount >= 2) {
            legacyStage = 3;
            if (g_campaign.rank >= 2) legacyStage = 4;
            if (g_campaign.rank >= 3) legacyStage = 5;
          }
        }
      }
      if (legacyStage > g_campaign.stageSeen) g_campaign.stageSeen = legacyStage;
      for (uint8_t feature = 0; feature < FEATURE_COUNT; ++feature)
        if (g_campaign.stageSeen >= sim_feature_stage(feature))
          g_campaign.legacyFeatureMask |= (uint16_t)(1u << feature);
      for (uint8_t region = 1; region < REGION_COUNT; ++region) {
        if (g_campaign.regionMask & (1u << region)) continue;
        if (g_campaign.chartMask & (1u << region))
          g_campaign.projectWorksCredit[region] = BALANCE.chartCost[region];
        uint32_t prior = g_campaign.charterProgress[region];
        uint32_t priorTarget = BALANCE.charterTarget[region];
        uint32_t target = sim_project_target(region);
        if (prior > priorTarget) prior = priorTarget;
        uint32_t credit = priorTarget ? prior * target * 2u / priorTarget : 0;
        g_campaign.projectCargo[region][0] = (uint16_t)(credit / 2u);
        g_campaign.projectCargo[region][1] = (uint16_t)((credit + 1u) / 2u);
      }
    }
    g_campaign.standbySessionId = version >= 8 ? r.u32() : 0;
    g_campaign.standbyAccountedRealSeconds = version >= 8 ? r.u32() : 0;
    g_campaign.returnMilestones = version >= 9 ? r.u32() : 0;
    g_campaign.returnMilestonesSeen = version >= 9 ? r.u32() : 0;
    g_campaign.onboardingStep = version >= 13 ? r.u8() : ONBOARD_DONE;
    if (g_campaign.onboardingStep >= ONBOARD_STEP_COUNT) r.ok = false;
    if (version >= 14) {
      g_campaign.storyDelivered = r.u8(); g_campaign.storyDeliveries = r.u8();
      r.bytes(g_campaign.storyOrder, sizeof(g_campaign.storyOrder));
      g_campaign.storyCarrying = r.u8(); g_campaign.storyOfferShipId = r.u32();
      g_campaign.storyOfferPort = r.u8(); g_campaign.storyOfferChapter = r.u8();
      g_campaign.storyLastPort = r.u8(); g_campaign.storyCooldown = r.u8();
      g_campaign.storyCardPending = r.u8(); g_campaign.storyOffers = r.u8();
    } else {
      g_campaign.storyDelivered = 0; g_campaign.storyDeliveries = 0;
      memset(g_campaign.storyOrder, STORY_NONE, sizeof(g_campaign.storyOrder));
      g_campaign.storyCarrying = STORY_NONE; g_campaign.storyOfferShipId = 0;
      g_campaign.storyOfferPort = NO_PORT; g_campaign.storyOfferChapter = STORY_NONE;
      g_campaign.storyLastPort = NO_PORT; g_campaign.storyCooldown = STORY_COOLDOWN_START;
      g_campaign.storyCardPending = STORY_NONE; g_campaign.storyOffers = 0;
    }
    if (!valid_story_state()) r.ok = false;
    if (version >= 15) {
      g_atlas.charted = r.u32(); g_atlas.regionsRewarded = r.u8();
      for (uint8_t p = 0; p < ATLAS_PORTS; ++p) {
        g_atlas.voyage[p] = r.u32(); g_atlas.day[p] = r.u16(); g_atlas.ship[p] = r.u8();
      }
      g_atlas.migrate = 0;
      if (!valid_atlas_state()) r.ok = false;
    } else g_atlas.migrate = 1;
    constexpr uint32_t milestoneBits = 0x1fu | (0x3eu << 8) | (0x3eu << 16) | RETURN_MILESTONE_GRAND_CHARTER;
    if ((g_campaign.returnMilestones & ~milestoneBits) ||
        (g_campaign.returnMilestonesSeen & ~g_campaign.returnMilestones)) r.ok = false;
    for (uint8_t region = 0; region < REGION_COUNT; ++region)
      if (g_campaign.projectWorksCredit[region] > 900u * region ||
          g_campaign.projectCargo[region][0] > sim_project_target(region) ||
          g_campaign.projectCargo[region][1] > sim_project_target(region)) r.ok = false;
    if (savedShipCount > MAX_SHIPS || g_ui.selectedPort >= MAX_PORTS ||
        g_ui.scene > (version >= 10 ? SCENE_FLEET : SCENE_COMPANY) ||
        g_campaign.economyVersion > (version >= 11 ? 2 : 1) ||
        g_campaign.rank > 5 || g_campaign.worldEventType > EVENT_TRADE_WINDS ||
        g_campaign.recoveryDebt < 0 || g_campaign.tutorialStep > TUTORIAL_COUNT ||
        g_campaign.captainCount > MAX_CAPTAINS || g_campaign.paused > 1 ||
        g_campaign.stageSeen >= STAGE_COUNT ||
        g_campaign.soundOn > 1 || g_campaign.timeScale == 0 ||
        (g_campaign.timeScale != 1 && g_campaign.timeScale != 2 &&
         g_campaign.timeScale != 4 && g_campaign.timeScale != 8 &&
         g_campaign.timeScale != 16) || g_campaign.won > 1 ||
        !(g_campaign.regionMask & 1u) || (g_campaign.regionMask & ~g_campaign.chartMask) ||
        (g_campaign.regionMask & ~0x3Fu) || (g_campaign.chartMask & ~0x3Fu) ||
        (g_campaign.worldEventGood >= GOOD_COUNT && g_campaign.worldEventGood != NO_PORT) ||
        (g_campaign.worldEventRegion >= REGION_COUNT && g_campaign.worldEventRegion != NO_PORT))
      r.ok = false;
  } else if (id == CH_FLEET) {
    uint8_t count = r.u8();
    if (count > MAX_SHIPS) return false;
    memset(g_ships, 0, sizeof(g_ships));
    struct LegacyCaptainData { char name[14]; uint16_t xp; uint8_t trait, level; };
    LegacyCaptainData legacyCaptain[MAX_SHIPS]{};
    for (int i = 0; i < count; ++i) {
      Ship& s = g_ships[i];
      s.id = r.u32(); r.bytes(s.name, sizeof(s.name));
      s.name[sizeof(s.name) - 1] = 0;
      uint8_t legacySaved[MAX_ROUTE_LEGS]{};
      uint8_t legacySavedCount = 0;
      if (version >= 4) {
        s.classId = r.u8(); s.state = r.u8(); s.currentPort = r.u8();
        s.fromPort = r.u8(); s.toPort = r.u8(); s.captainId = r.u8();
        for (int j = 0; j < MAX_ROUTE_LEGS; ++j) s.route[j] = r.u8();
        s.routeCount = r.u8(); s.routePos = r.u8();
        s.routeTemplate = get_route_template(r);
      } else {
        r.bytes(legacyCaptain[i].name, sizeof(legacyCaptain[i].name));
        legacyCaptain[i].name[sizeof(legacyCaptain[i].name) - 1] = 0;
        legacyCaptain[i].trait = r.u8(); legacyCaptain[i].level = r.u8();
        legacyCaptain[i].xp = r.u16();
        s.classId = r.u8(); s.state = r.u8(); s.currentPort = r.u8();
        s.fromPort = r.u8(); s.toPort = r.u8();
        for (int j = 0; j < MAX_ROUTE_LEGS; ++j) s.route[j] = r.u8();
        for (int j = 0; j < MAX_ROUTE_LEGS; ++j) legacySaved[j] = r.u8();
        s.routeCount = r.u8(); s.routePos = r.u8(); legacySavedCount = r.u8();
      }
      s.cargoCount = r.u8();
      s.condition = r.u8(); s.morale = r.u8(); s.crew = r.u8();
      s.modules[0] = r.u8(); s.modules[1] = r.u8(); s.incidentType = r.u8();
      s.incidentArmed = r.u8(); s.manifestBonus = r.u8();
      s.legElapsedMinutes = r.u16(); s.legTotalMinutes = r.u16(); s.prepaidCost = r.u16();
      if (version >= 7) { s.voyageEarned = r.i32(); s.voyageDelivered = r.u16(); }
      if (version >= 8) s.booking = get_booking(r, version);
      if (version >= 9) {
        s.returnLedger.total = get_return_tally(r); s.returnLedger.seen = get_return_tally(r);
        if (!valid_return_ledger(s.returnLedger)) return false;
      }
      if (version >= 11) s.merchant = get_merchant(r);
      if (version >= 12) { s.encounterWait = r.u16(); s.lastEncounter = r.u8(); s.sighting = r.u8(); s.autoEncounter=r.u8(); s.autoDelay=r.u16();
        if (s.lastEncounter > INCIDENT_COUNT || s.autoEncounter > INCIDENT_COUNT || s.sighting > 4) return false; }
      if (s.classId >= SHIP_CLASS_COUNT || s.state > SHIP_DISABLED ||
          s.cargoCount > MAX_CARGO_ITEMS || s.condition > 100 || s.morale > 100 ||
          s.currentPort >= MAX_PORTS || s.fromPort >= MAX_PORTS || s.toPort >= MAX_PORTS ||
          s.routeCount > MAX_ROUTE_LEGS || legacySavedCount > MAX_ROUTE_LEGS ||
          s.routePos > s.routeCount || s.modules[0] > MODULE_COUNT ||
          s.modules[1] > MODULE_COUNT) return false;
      for (int j = 0; j < s.routeCount; ++j) if (s.route[j] >= MAX_PORTS) return false;
      for (int j = 0; j < s.cargoCount; ++j) {
        s.cargo[j] = get_cargo(r, version);
        const CargoItem& c = s.cargo[j];
        if (!valid_cargo(c, false, version)) return false;
      }
      const BookedVoyage& b = s.booking;
      if (b.state > BOOKING_SETTLED || b.receiptUnread > 1) return false;
      if (b.state != BOOKING_NONE) {
        if (!b.id || b.origin >= MAX_PORTS || b.destination >= MAX_PORTS ||
            b.origin == b.destination || b.good >= GOOD_COUNT || !b.quantity ||
            b.settledQuantity > b.quantity || b.mode > VOYAGE_RELIEF ||
            (b.projectRegion != NO_PORT && b.projectRegion >= REGION_COUNT) ||
            b.cargoCost < 0 || b.sailingCost < 0 || b.buyerPayment < 0 || b.bonus < 0 ||
            b.withheld < 0 || b.withheld > b.payout ||
            (int64_t)b.payout != (int64_t)b.buyerPayment + b.bonus ||
            (int64_t)b.payNow != (int64_t)b.cargoCost + b.sailingCost ||
            (int64_t)b.arrivalCoins != (int64_t)b.payout - b.withheld ||
            (int64_t)b.profit != (int64_t)b.arrivalCoins - b.payNow) return false;
        if (b.priceVersion > 1 || b.grossPayment < 0 || b.duty < 0 ||
            (int64_t)b.grossPayment - b.duty != b.buyerPayment) return false;
        unsigned manifestQuantity = 0, projectQuantity = 0;
        uint8_t firstGood = NO_PORT;
        for (uint8_t good = 0; good < GOOD_COUNT; ++good) {
          if (b.quantities[good] && firstGood == NO_PORT) firstGood = good;
          manifestQuantity += b.quantities[good]; projectQuantity += b.projectQuantities[good];
          if (b.projectQuantities[good] > b.quantities[good] ||
              (b.projectQuantities[good] && (b.projectRegion == NO_PORT ||
               (good != GOOD_TIMBER && good != GOOD_IRONWARE)))) return false;
        }
        if (manifestQuantity != b.quantity || firstGood != b.good ||
            (!projectQuantity && b.projectRegion != NO_PORT)) return false;
        if (b.state == BOOKING_SAILING && (s.state != SHIP_SAILING ||
            b.settledQuantity || s.routePos >= s.routeCount || !s.legTotalMinutes ||
            b.quantity > sim_ship_capacity(s))) return false;
        if (b.state == BOOKING_SETTLED && b.settledQuantity != b.quantity) return false;
      }
      unsigned bookedQuantity = 0;
      unsigned bookedGoods[GOOD_COUNT]{}, projectGoods[GOOD_COUNT]{};
      for (int j = 0; j < s.cargoCount; ++j) if (s.cargo[j].kind == CARGO_BOOKED) {
        const CargoItem& c = s.cargo[j];
        if (b.state != BOOKING_SAILING || c.contractId != b.id ||
            c.destination != b.destination || c.sourcePort != b.origin ||
            (c.projectRegion != NO_PORT && c.projectRegion != b.projectRegion)) return false;
        bookedQuantity += c.quantity;
        bookedGoods[c.good] += c.quantity;
        if (c.projectRegion != NO_PORT) projectGoods[c.good] += c.quantity;
      }
      if (b.state == BOOKING_SAILING && bookedQuantity != b.quantity) return false;
      if (b.state == BOOKING_SAILING) for (int j = 0; j < s.cargoCount; ++j)
        if (s.cargo[j].kind != CARGO_BOOKED) return false;
      if (b.state == BOOKING_SAILING) for (uint8_t good = 0; good < GOOD_COUNT; ++good)
        if (bookedGoods[good] != b.quantities[good] || projectGoods[good] != b.projectQuantities[good]) return false;
      const MerchantVoyage& m = s.merchant;
      if (m.state > MERCHANT_ARRIVED || m.receiptUnread > 1 || m.packageCoins < 0) return false;
      if (m.state != MERCHANT_NONE) {
        if (!m.id || m.origin >= MAX_PORTS || m.destination >= MAX_PORTS ||
            m.origin == m.destination || b.state == BOOKING_SAILING) return false;
        if (m.state == MERCHANT_SAILING && ((s.state != SHIP_SAILING && s.state != SHIP_AWAITING_DECISION) ||
            s.routePos >= s.routeCount || !s.legTotalMinutes ||
            s.route[s.routeCount - 1] != m.destination || m.receiptUnread ||
            s.legTotalMinutes != m.legMinutes[s.routePos])) return false;
        if (m.state == MERCHANT_SAILING)
          for (uint8_t leg = 0; leg < s.routeCount; ++leg)
            if (!m.legMinutes[leg]) return false;
      }
      if (version == 3) {
        s.captainId = r.u8();
        s.routeTemplate = get_route_template(r);
      } else if (version == 2) {
        s.captainId = (uint8_t)i;
        s.routeTemplate.origin = s.currentPort;
        s.routeTemplate.stopCount = legacySavedCount;
        memcpy(s.routeTemplate.stops, legacySaved, legacySavedCount);
        s.routeTemplate.buyGood = NO_PORT;
      }
      if ((s.captainId >= MAX_CAPTAINS && s.captainId != NO_SHIP) ||
          s.routeTemplate.stopCount > MAX_ROUTE_LEGS ||
          s.routeTemplate.loadMode > ROUTE_LOAD_ENDPOINT ||
          s.routeTemplate.sellMode > ROUTE_SELL_PROFIT ||
          (s.routeTemplate.buyGood >= GOOD_COUNT && s.routeTemplate.buyGood != NO_PORT)) return false;
      if (s.routeTemplate.stopCount) {
        if (s.routeTemplate.origin >= MAX_PORTS) return false;
        uint8_t from = s.routeTemplate.origin;
        for (int j = 0; j < s.routeTemplate.stopCount; ++j) {
          uint8_t to = s.routeTemplate.stops[j];
          if (to >= MAX_PORTS || content_lane_between(from, to) < 0) return false;
          from = to;
        }
      }
    }
    g_campaign.shipCount = count;
    g_incident.active = r.u8(); g_incident.shipIndex = r.u8();
    g_incident.kind = r.u8(); g_incident.risk = r.u8();
    g_incidentQueueCount = r.u8();
    if (g_incidentQueueCount > MAX_INCIDENTS) return false;
    memset(g_incidentQueue, 0, sizeof(g_incidentQueue));
    for (int i = 0; i < g_incidentQueueCount; ++i) {
      g_incidentQueue[i].active = r.u8(); g_incidentQueue[i].shipIndex = r.u8();
      g_incidentQueue[i].kind = r.u8(); g_incidentQueue[i].risk = r.u8();
      if (!g_incidentQueue[i].active || g_incidentQueue[i].shipIndex >= count ||
          g_incidentQueue[i].kind >= INCIDENT_COUNT || g_incidentQueue[i].risk > RISK_DANGEROUS)
        return false;
    }
    memset(g_captains, 0, sizeof(g_captains));
    if (version >= 3) {
      g_campaign.captainCount = r.u8();
      if (g_campaign.captainCount > MAX_CAPTAINS) return false;
      for (int i = 0; i < g_campaign.captainCount; ++i)
        g_captains[i] = get_captain_profile(r, version);
    } else {
      g_campaign.captainCount = count;
      for (int i = 0; i < count; ++i) {
        CaptainProfile& c = g_captains[i];
        snprintf(c.name, sizeof(c.name), "%s", legacyCaptain[i].name);
        c.xp = legacyCaptain[i].xp; c.trait = legacyCaptain[i].trait;
        c.level = legacyCaptain[i].level; c.loyalty = BALANCE.captainStartingLoyalty;
        c.assignedShip = (uint8_t)i; c.currentPort = g_ships[i].currentPort; c.active = 1;
      }
    }
    for (int i = 0; i < g_campaign.captainCount; ++i) {
      const CaptainProfile& c = g_captains[i];
      if (!c.active || c.trait > TRAIT_QUARTERMASTER || c.level < 1 ||
          c.level > BALANCE.maxCaptainLevel || c.loyalty > 100 ||
          c.scarCount > BALANCE.maxCaptainScars ||
          (c.assignedShip != NO_SHIP && c.assignedShip >= count) || c.currentPort >= MAX_PORTS)
        return false;
    }
    for (int i = 0; i < count; ++i) {
      uint8_t captainId = g_ships[i].captainId;
      if (captainId == NO_SHIP) continue;
      if (captainId >= g_campaign.captainCount || !g_captains[captainId].active ||
          g_captains[captainId].assignedShip != i) return false;
    }
    for (int i = 0; i < g_campaign.captainCount; ++i) {
      uint8_t shipId = g_captains[i].assignedShip;
      if (shipId != NO_SHIP && g_ships[shipId].captainId != i) return false;
    }
    if (g_incident.active && (g_incident.shipIndex >= count ||
        g_incident.kind >= INCIDENT_COUNT || g_incident.risk > RISK_DANGEROUS)) return false;
  } else if (id == CH_PORTS) {
    if (r.u8() != MAX_PORTS) return false;
    for (int i = 0; i < MAX_PORTS; ++i) {
      PortState& p = g_ports[i];
      p.unlocked = r.u8(); p.officeLevel = r.u8(); p.warehouseLevel = r.u8();
      p.shipyardLevel = r.u8(); p.warehouseCount = r.u8();
      if (p.unlocked > 1 || p.officeLevel > 4 || p.warehouseLevel > 3 ||
          p.shipyardLevel > 3 || p.warehouseCount > MAX_WAREHOUSE_ITEMS) return false;
      for (int g = 0; g < GOOD_COUNT; ++g) {
        p.stock[g] = r.u16();
        if (p.stock[g] > 200) return false;
      }
      for (int j = 0; j < p.warehouseCount; ++j) {
        p.warehouse[j] = get_cargo(r, version);
        const CargoItem& c = p.warehouse[j];
        if (!valid_cargo(c, true, version)) return false;
      }
      if (version >= 3) {
        for (int g = 0; g < GOOD_COUNT; ++g)
          for (int d = 0; d < 7; ++d) p.priceHistory[g][d] = r.u8();
        p.historyHead = r.u8();
        if (p.historyHead >= 7) return false;
      } else {
        p.historyHead = 0;
        for (int g = 0; g < GOOD_COUNT; ++g) {
          uint16_t price = content_market_price((uint8_t)i, (uint8_t)g, true);
          uint16_t normalized = price * 100u / GOOD_DEFS[g].basePrice;
          if (normalized > 255) normalized = 255;
          for (int d = 0; d < 7; ++d) p.priceHistory[g][d] = (uint8_t)normalized;
        }
      }
      p.bookingEpoch = version >= 8 ? r.u32() : 0;
      for (int g = 0; g < GOOD_COUNT; ++g) p.bookingUsed[g] = version >= 8 ? r.u16() : 0;
      p.packageEpoch = version >= 11 ? r.u32() : UINT32_MAX;
    }
  } else if (id == CH_JOBS) {
    if (r.u8() != MAX_PORTS || r.u8() != MAX_CONTRACTS_PER_PORT) return false;
    for (int p = 0; p < MAX_PORTS; ++p) {
      for (int j = 0; j < MAX_CONTRACTS_PER_PORT; ++j) {
        Contract& c = g_contracts[p][j];
        c = get_contract(r);
        if (version >= 11 && c.active && c.kind == JOB_PACKAGE) {
          if (c.active > 2 || !c.id || c.origin != p || c.destination >= MAX_PORTS ||
              c.destination == c.origin || c.good != NO_PORT || c.quantity != 1 ||
              c.deadlineMinute || c.projectRegion != NO_PORT || !c.reward) return false;
          continue;
        }
        if (c.active > 2 || (c.active && (c.origin >= MAX_PORTS ||
            c.destination >= MAX_PORTS || c.good >= GOOD_COUNT || c.kind > JOB_CHARTER ||
            !c.quantity || (c.projectRegion != 0xFF && c.projectRegion >= REGION_COUNT))))
          return false;
      }
    }
  } else if (id == CH_JOURNAL) {
    g_journalHead = r.u8(); g_journalCount = r.u8();
    if (g_journalHead >= MAX_JOURNAL || g_journalCount > MAX_JOURNAL) return false;
    for (int i = 0; i < MAX_JOURNAL; ++i) {
      g_journal[i].worldMinute = r.u32(); g_journal[i].severity = r.u8();
      if (g_journal[i].severity > 2) return false;
      r.bytes(g_journal[i].text, sizeof(g_journal[i].text));
      g_journal[i].text[sizeof(g_journal[i].text) - 1] = 0;
    }
    memset(g_attention, 0, sizeof(g_attention));
    g_attentionCount = 0;
    if (version >= 3) {
      g_attentionCount = r.u8();
      if (g_attentionCount > MAX_ATTENTION) return false;
      for (int i = 0; i < g_attentionCount; ++i) {
        AttentionEntry& a = g_attention[i];
        a.id = r.u32(); a.worldMinute = r.u32(); a.kind = r.u8();
        a.shipIndex = r.u8(); a.port = r.u8(); a.urgent = r.u8();
        a.unread = r.u8(); a.resolved = r.u8();
        a.region = version >= 7 ? r.u8() : 0xFF;
        if (a.kind > ATTENTION_RECOVERY || (a.region != 0xFF && a.region >= REGION_COUNT) ||
            (a.shipIndex != NO_SHIP && a.shipIndex >= g_campaign.shipCount) ||
            (a.port != NO_PORT && a.port >= MAX_PORTS) || a.urgent > 1 ||
            a.unread > 1 || a.resolved > 1) return false;
      }
    }
  }
  return r.ok && r.at == r.len;
}

static void header_put(uint8_t* h, int at, uint32_t v) {
  h[at] = v & 0xFF; h[at + 1] = (v >> 8) & 0xFF;
  h[at + 2] = (v >> 16) & 0xFF; h[at + 3] = (v >> 24) & 0xFF;
}

static uint32_t header_get(const uint8_t* h, int at) {
  return (uint32_t)h[at] | ((uint32_t)h[at + 1] << 8) |
         ((uint32_t)h[at + 2] << 16) | ((uint32_t)h[at + 3] << 24);
}

#ifdef SH_HOST_TESTS
// Only used to fabricate old-format snapshots for migration tests. Production
// never overwrites the legacy files; they remain an untouched migration backup.
static bool write_chunk(uint8_t id) {
  size_t len = build_chunk(id);
  if (!len || !buf) return false;
  uint32_t crc = crc32(buf, len);
  if (crc == lastCrc[id] && currentTrusted[id] && LittleFS.exists(CH_PATH[id])) return true;
  uint8_t h[16]{};
  header_put(h, 0, CH_MAGIC); h[4] = id; h[5] = emitVersion;
  header_put(h, 8, (uint32_t)len); header_put(h, 12, crc);
  File f = LittleFS.open(TMP_PATH, "w");
  if (!f) return false;
  bool ok = f.write(h, sizeof(h)) == sizeof(h) && f.write(buf, len) == len;
  f.close();
  if (!ok) { LittleFS.remove(TMP_PATH); return false; }

  bool hadTrustedCurrent = currentTrusted[id] && LittleFS.exists(CH_PATH[id]);
  if (hadTrustedCurrent) {
    LittleFS.remove(BAK_PATH[id]);
    if (!LittleFS.rename(CH_PATH[id], BAK_PATH[id])) {
      LittleFS.remove(TMP_PATH);
      return false;
    }
  } else if (LittleFS.exists(CH_PATH[id])) {
    LittleFS.remove(CH_PATH[id]);
  }
  if (!LittleFS.rename(TMP_PATH, CH_PATH[id])) {
    if (!LittleFS.exists(CH_PATH[id]) && LittleFS.exists(BAK_PATH[id]))
      LittleFS.rename(BAK_PATH[id], CH_PATH[id]);
    LittleFS.remove(TMP_PATH);
    currentTrusted[id] = LittleFS.exists(CH_PATH[id]);
    return false;
  }
  if (crc != lastCrc[id]) prevCrc[id] = lastCrc[id];  // the generation now in .bak
  lastCrc[id] = crc;
  currentTrusted[id] = true;
  return true;
}
#endif

static bool read_chunk_path(uint8_t id, const char* path, bool isCurrent, bool enforceExpected) {
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  uint8_t h[16];
  if (f.read(h, sizeof(h)) != sizeof(h)) { f.close(); return false; }
  uint32_t len = header_get(h, 8), crc = header_get(h, 12);
  uint8_t version = h[5];
  if (header_get(h, 0) != CH_MAGIC || h[4] != id ||
      version < CH_OLDEST_VERSION || version > CH_VERSION || len > BUF_CAP) {
    f.close(); return false;
  }
  if (enforceExpected && crc != expectedCrc[id]) { f.close(); return false; }
  size_t got = f.read(buf, len); f.close();
  if (got != len || crc32(buf, len) != crc) return false;
  if (!apply_chunk(id, buf, len, version)) return false;
  lastCrc[id] = crc;
  if (id == CH_CORE) loadedVersion = version;
  currentTrusted[id] = isCurrent;
  return true;
}

static bool read_chunk(uint8_t id, bool enforceExpected) {
  currentTrusted[id] = false;
  if (read_chunk_path(id, CH_PATH[id], true, enforceExpected)) return true;
  return read_chunk_path(id, BAK_PATH[id], false, enforceExpected);
}

// Each bundle is a complete company. A commit footer and per-chunk CRCs are
// checked before any bytes are applied. Recovery chooses an entire generation,
// so a banked payment can never be paired with its older, undelivered cargo.
static constexpr uint8_t SNAP_ORDER[CH_COUNT] = {CH_CORE, CH_PORTS, CH_JOBS, CH_FLEET, CH_JOURNAL};
static uint32_t snapshot_digest(uint32_t digest, const uint8_t* h) {
  return (digest * 16777619u) ^ crc32(h, 16);
}

static uint32_t snapshot_magic(uint8_t version, bool footer) {
  if (version == 8) return footer ? SNAP_V8_END : SNAP_V8_MAGIC;
  if (version == 9) return footer ? SNAP_V9_END : SNAP_V9_MAGIC;
  if (version == 10) return footer ? SNAP_V10_END : SNAP_V10_MAGIC;
  return footer ? SNAP_END : SNAP_MAGIC;
}

// A keepsake aboard must be the one chapter the campaign says is being carried.
static bool valid_story_links() {
  unsigned aboard = 0;
  for (uint8_t ship = 0; ship < g_campaign.shipCount; ++ship)
    for (uint8_t slot = 0; slot < g_ships[ship].cargoCount; ++slot) {
      const CargoItem& cargo = g_ships[ship].cargo[slot];
      if (cargo.jobKind != JOB_STORY) continue;
      if (cargo.contractId - 0x53540000u != g_campaign.storyCarrying ||
          sim_ship_used_capacity(g_ships[ship]) > sim_ship_capacity(g_ships[ship])) return false;
      ++aboard;
    }
  return aboard == (g_campaign.storyCarrying == STORY_NONE ? 0u : 1u);
}

// A package is an accepted promise, never a market stack. Both directions of
// the link are checked only after the complete company has been decoded.
static bool valid_package_links() {
  for (uint8_t ship = 0; ship < g_campaign.shipCount; ++ship)
    for (uint8_t slot = 0; slot < g_ships[ship].cargoCount; ++slot) {
      const CargoItem& cargo = g_ships[ship].cargo[slot];
      if (cargo.jobKind != JOB_PACKAGE) continue;
      unsigned matches = 0;
      for (uint8_t port = 0; port < MAX_PORTS; ++port)
        for (uint8_t job = 0; job < MAX_CONTRACTS_PER_PORT; ++job) {
          const Contract& c = g_contracts[port][job];
          if (c.active && c.id == cargo.contractId) {
            if (c.active != 2 || c.kind != JOB_PACKAGE || c.origin != cargo.sourcePort ||
                c.destination != cargo.destination) return false;
            ++matches;
          }
        }
      if (matches != 1 || sim_ship_used_capacity(g_ships[ship]) > sim_ship_capacity(g_ships[ship])) return false;
    }
  for (uint8_t port = 0; port < MAX_PORTS; ++port)
    for (uint8_t job = 0; job < MAX_CONTRACTS_PER_PORT; ++job) {
      const Contract& c = g_contracts[port][job];
      if (!c.active || c.kind != JOB_PACKAGE) continue;
      unsigned copies = 0, identities = 0;
      for (uint8_t otherPort = 0; otherPort < MAX_PORTS; ++otherPort)
        for (uint8_t otherJob = 0; otherJob < MAX_CONTRACTS_PER_PORT; ++otherJob) {
          const Contract& other = g_contracts[otherPort][otherJob];
          identities += other.active && other.id == c.id;
        }
      for (uint8_t ship = 0; ship < g_campaign.shipCount; ++ship)
        for (uint8_t slot = 0; slot < g_ships[ship].cargoCount; ++slot) {
          const CargoItem& cargo = g_ships[ship].cargo[slot];
          copies += cargo.kind == CARGO_CONTRACT && cargo.jobKind == JOB_PACKAGE && cargo.contractId == c.id;
        }
      if (identities != 1 || copies != (c.active == 2 ? 1u : 0u)) return false;
    }
  return true;
}

static bool scan_snapshot(const char* path, bool apply, uint32_t* generation,
                          uint32_t* crcs, uint8_t* wireVersion = nullptr) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  uint8_t h[16];
  bool ok = f.read(h, 16) == 16;
  uint8_t version = ok ? h[4] : 0;
  ok = ok && (version >= 8 && version <= CH_VERSION) && h[5] == CH_COUNT &&
    header_get(h, 0) == snapshot_magic(version, false);
  uint32_t gen = ok ? header_get(h, 8) : 0;
  uint32_t digest = ok ? crc32(h, 16) : 0;
  if (ok && apply) sim_new_campaign(0x51A17EEDu);
  for (int i = 0; i < CH_COUNT && ok; ++i) {
    ok = f.read(h, 16) == 16 && header_get(h, 0) == CH_MAGIC &&
      h[4] == SNAP_ORDER[i] && h[5] == version;
    if (!ok) break;
    uint32_t len = header_get(h, 8), crc = header_get(h, 12);
    digest = snapshot_digest(digest, h);
    ok = len && len <= BUF_CAP && f.read(buf, len) == len && crc32(buf, len) == crc;
    if (ok && crcs) crcs[h[4]] = crc;
    if (ok && apply) ok = apply_chunk(h[4], buf, len, version);
  }
  if (ok && apply && version >= 11) ok = valid_package_links();
  if (ok && apply && version >= 14) ok = valid_story_links();
  if (ok && apply && version >= 15) ok = valid_atlas_links();
  if (ok) ok = f.read(h, 16) == 16 && header_get(h, 0) == snapshot_magic(version, true) &&
    header_get(h, 4) == gen && header_get(h, 8) == digest && header_get(h, 12) == ~digest;
  uint8_t trailing;
  if (ok) ok = f.read(&trailing, 1) == 0;
  f.close();
  if (ok && generation) *generation = gen;
  if (ok && wireVersion) *wireVersion = version;
  return ok;
}

static bool read_snapshot(const char* path, bool trusted) {
  uint32_t gen = 0, crcs[CH_COUNT]{};
  uint8_t version = 0;
  if (!scan_snapshot(path, false, &gen, crcs, &version) || !scan_snapshot(path, true, nullptr, nullptr))
    return false;
  snapshotGeneration = gen;
  memcpy(snapshotCrc, crcs, sizeof(snapshotCrc));
  snapshotTrusted = trusted;
  loadedVersion = version;
  return true;
}

static bool write_snapshot() {
  uint32_t crcs[CH_COUNT]{};
  bool same = snapshotTrusted && LittleFS.exists(SNAP_PATH);
  for (uint8_t id = 0; id < CH_COUNT; ++id) {
    size_t len = build_chunk(id);
    if (!len) return false;
    crcs[id] = crc32(buf, len);
    same &= crcs[id] == snapshotCrc[id];
  }
  if (same) return true;
  File f = LittleFS.open(SNAP_TMP, "w");
  if (!f) return false;
  uint8_t h[16]{};
  header_put(h, 0, snapshot_magic(emitVersion, false)); h[4] = emitVersion; h[5] = CH_COUNT;
  uint32_t generation = snapshotGeneration + 1;
  header_put(h, 8, generation);
  uint32_t digest = crc32(h, 16);
  bool ok = f.write(h, 16) == 16;
  for (int i = 0; i < CH_COUNT && ok; ++i) {
    uint8_t id = SNAP_ORDER[i];
    size_t len = build_chunk(id);
    memset(h, 0, sizeof(h));
    header_put(h, 0, CH_MAGIC); h[4] = id; h[5] = emitVersion;
    header_put(h, 8, (uint32_t)len); header_put(h, 12, crcs[id]);
    digest = snapshot_digest(digest, h);
    ok = len && f.write(h, 16) == 16 && f.write(buf, len) == len;
  }
  memset(h, 0, sizeof(h)); header_put(h, 0, snapshot_magic(emitVersion, true)); header_put(h, 4, generation);
  header_put(h, 8, digest); header_put(h, 12, ~digest);
  ok = ok && f.write(h, 16) == 16;
  f.flush(); f.close();
  if (!ok || !scan_snapshot(SNAP_TMP, false, nullptr, nullptr)) {
    LittleFS.remove(SNAP_TMP); return false;
  }
  if (snapshotTrusted && LittleFS.exists(SNAP_PATH)) {
    LittleFS.remove(SNAP_BAK);
    if (!LittleFS.rename(SNAP_PATH, SNAP_BAK)) return false;
  } else if (LittleFS.exists(SNAP_PATH)) {
    if (!LittleFS.remove(SNAP_PATH)) return false;
  }
  if (!LittleFS.rename(SNAP_TMP, SNAP_PATH)) {
    snapshotTrusted = false;
    return false;  // the previous committed bundle remains in .bak
  }
  snapshotGeneration = generation;
  memcpy(snapshotCrc, crcs, sizeof(snapshotCrc));
  snapshotTrusted = true;
  return true;
}

// A device that has never held saves has a blank partition, which LittleFS will
// not mount. Only that case is formatted; anything else stays untouched.
static bool saves_partition_blank() {
#if defined(ESP_PLATFORM)
  const esp_partition_t* part =
    esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  if (!part) return false;
  uint8_t sector[256];
  for (uint32_t off = 0; off < 8192; off += sizeof(sector)) {
    if (esp_partition_read(part, off, sector, sizeof(sector)) != ESP_OK) return false;
    for (size_t i = 0; i < sizeof(sector); ++i)
      if (sector[i] != 0xFF) return false;
  }
  return true;
#else
  return false;
#endif
}

bool save_begin() {
  mounted = LittleFS.begin(false);  // an unreadable filesystem must never erase a company
  if (!mounted && saves_partition_blank()) mounted = LittleFS.begin(true);
  if (!mounted) return false;
  if (!buf) buf = (uint8_t*)malloc(BUF_CAP);
  memset(lastCrc, 0, sizeof(lastCrc));
  memset(expectedCrc, 0, sizeof(expectedCrc));
  memset(prevCrc, 0, sizeof(prevCrc));
  memset(prevExpectedCrc, 0, sizeof(prevExpectedCrc));
  memset(currentTrusted, 0, sizeof(currentTrusted));
  snapshotGeneration = 0; snapshotTrusted = false; writesSuspended = false;
  loadedVersion = CH_VERSION;
  memset(snapshotCrc, 0, sizeof(snapshotCrc));
  return buf != nullptr;
}

bool save_exists() {
  return mounted && (LittleFS.exists(SNAP_PATH) || LittleFS.exists(SNAP_BAK) ||
    LittleFS.exists(CH_PATH[CH_CORE]) || LittleFS.exists(BAK_PATH[CH_CORE]));
}

void save_suspend(bool suspended) { writesSuspended = suspended; }

bool save_write() {
  if (writesSuspended) return true;
  if (!mounted || !buf) return false;
  return write_snapshot();
}

bool save_read() {
  if (!mounted || !buf) return false;
  snapshotTrusted = false;
  if (LittleFS.exists(SNAP_PATH) || LittleFS.exists(SNAP_BAK)) {
    if (!read_snapshot(SNAP_PATH, true) && !read_snapshot(SNAP_BAK, false)) return false;
  } else {
    // Migrate only a coherent legacy generation; leave all old files intact.
    bool loaded = false;
    const char* corePaths[2] = {CH_PATH[CH_CORE], BAK_PATH[CH_CORE]};
    for (int candidate = 0; candidate < 2 && !loaded; ++candidate) {
      sim_new_campaign(0x51A17EEDu);
      if (!read_chunk_path(CH_CORE, corePaths[candidate], candidate == 0, false)) continue;
      loaded = read_chunk(CH_PORTS, true) && read_chunk(CH_JOBS, true) &&
        read_chunk(CH_FLEET, true) && read_chunk(CH_JOURNAL, true);
    }
    if (!loaded) return false;
  }
  if (loadedVersion < 9) sim_return_migrate_legacy();
  sim_after_load();
  return true;
}

bool save_reset() {
  if (!mounted) return false;
  for (int i = 0; i < CH_COUNT; ++i) {
    LittleFS.remove(CH_PATH[i]);
    LittleFS.remove(BAK_PATH[i]);
  }
  LittleFS.remove(TMP_PATH);
  LittleFS.remove(SNAP_PATH); LittleFS.remove(SNAP_BAK); LittleFS.remove(SNAP_TMP);
  snapshotGeneration = 0; snapshotTrusted = false;
  memset(snapshotCrc, 0, sizeof(snapshotCrc));
  memset(lastCrc, 0, sizeof(lastCrc));
  memset(expectedCrc, 0, sizeof(expectedCrc));
  memset(prevCrc, 0, sizeof(prevCrc));
  memset(prevExpectedCrc, 0, sizeof(prevExpectedCrc));
  memset(currentTrusted, 0, sizeof(currentTrusted));
  return true;
}

// An unreadable save is set aside under an .orphan name rather than deleted,
// so a torn core never takes the still-valid fleet, ports, jobs and journal
// files with it, and the bytes stay on the device for recovery.
void save_quarantine() {
  if (!mounted) return;
  char orphan[40];
  for (int i = 0; i < CH_COUNT; ++i) {
    const char* paths[2] = { CH_PATH[i], BAK_PATH[i] };
    for (int k = 0; k < 2; ++k) {
      if (!LittleFS.exists(paths[k])) continue;
      snprintf(orphan, sizeof(orphan), "%s.orphan", paths[k]);
      LittleFS.remove(orphan);
      LittleFS.rename(paths[k], orphan);
    }
  }
  LittleFS.remove(TMP_PATH);
  const char* snapshots[2] = {SNAP_PATH, SNAP_BAK};
  for (const char* path : snapshots) if (LittleFS.exists(path)) {
    snprintf(orphan, sizeof(orphan), "%s.orphan", path);
    LittleFS.remove(orphan); LittleFS.rename(path, orphan);
  }
  LittleFS.remove(SNAP_TMP);
  snapshotGeneration = 0; snapshotTrusted = false;
  memset(snapshotCrc, 0, sizeof(snapshotCrc));
  memset(lastCrc, 0, sizeof(lastCrc));
  memset(expectedCrc, 0, sizeof(expectedCrc));
  memset(prevCrc, 0, sizeof(prevCrc));
  memset(prevExpectedCrc, 0, sizeof(prevExpectedCrc));
  memset(currentTrusted, 0, sizeof(currentTrusted));
}

bool save_selftest() {
#ifdef SH_HOST_TESTS
  // A pre-v8 company loads with signed voyages; only the X4 moves it on to
  // the merchant economy.
  constexpr uint8_t SAVE_TEST_LOADED_ECONOMY = 2;
  auto corrupt = [](const char* path) {
    File f = LittleFS.open(path, "r+");
    if (!f || !f.seek(20)) { f.close(); return false; }
    uint8_t value = 0;
    if (f.read(&value, 1) != 1 || !f.seek(20)) { f.close(); return false; }
    value ^= 0xA5;
    bool ok = f.write(&value, 1) == 1;
    f.flush();
    f.close();
    return ok;
  };

  bool ok = true;
  bool reported = false;
  auto step = [&](const char* name) {
    if (!ok && !reported) { Serial.printf("save selftest: failed at %s\n", name); reported = true; }
  };
  save_reset();
  sim_new_campaign(0x12345678u);
  g_campaign.stageSeen = 2;  // incidents are open, so the pending decision survives the load
  g_campaign.coins = 4321;
  g_campaign.timeScale = 8;
  g_campaign.recoveryDebt = 123;
  g_campaign.companyXp = 17;
  g_ports[0].stock[GOOD_TEA] = 9;
  // Market stacks retain their exact fractional cost basis in the existing v7
  // contractId slot, including while stored ashore. Exercise both save chunks.
  CargoItem marketBasis{};
  marketBasis.kind = CARGO_MARKET; marketBasis.jobKind = JOB_FREIGHT;
  marketBasis.good = GOOD_GRAIN; marketBasis.destination = NO_PORT;
  marketBasis.projectRegion = 0xFF; marketBasis.quantity = 3;
  marketBasis.paidUnitPrice = 17; marketBasis.contractId = 2;
  g_ships[0].cargo[0] = marketBasis; g_ships[0].cargoCount = 1;
  marketBasis.quantity = 4; marketBasis.paidUnitPrice = 29; marketBasis.contractId = 1;
  g_ports[0].warehouse[0] = marketBasis; g_ports[0].warehouseCount = 1;
  snprintf(g_ships[0].name, sizeof(g_ships[0].name), "Snapshot A");
  g_ships[0].state = SHIP_AWAITING_DECISION;
  g_ships[0].routeTemplate.origin = 0;
  g_ships[0].routeTemplate.stops[0] = 1;
  g_ships[0].routeTemplate.stopCount = 1;
  g_captains[0].voyages = 12;
  g_captains[0].deliveries = 34;
  g_incident = Incident{1, 0, INCIDENT_PIRATES, RISK_EVEN};
  g_contracts[0][0].reward = 777;
  journal_add(1, "snapshot A marker");
  attention_add(ATTENTION_ARRIVAL, 0, 0, false);
  Serial.printf("save profile v%u: core=%u fleet=%u ports=%u jobs=%u journal=%u bytes\n", (unsigned)CH_VERSION,
                (unsigned)build_chunk(CH_CORE), (unsigned)build_chunk(CH_FLEET),
                (unsigned)build_chunk(CH_PORTS), (unsigned)build_chunk(CH_JOBS),
                (unsigned)build_chunk(CH_JOURNAL));
  if (!save_write()) { Serial.println("save selftest: snapshot A write failed"); return false; }

  g_campaign.coins = 5432;
  g_ports[0].stock[GOOD_TEA] = 3;
  snprintf(g_ships[0].name, sizeof(g_ships[0].name), "Snapshot B");
  g_contracts[0][0].reward = 999;
  journal_add(1, "snapshot B marker");
  if (!save_write() || !corrupt(SNAP_PATH)) {
    Serial.println("save selftest: snapshot B setup failed");
    return false;
  }
  ok &= save_read();
  ok &= g_campaign.coins == 4321 && g_campaign.companyXp == 17 &&
        g_campaign.recoveryDebt == 123 && g_campaign.timeScale == 8;
  ok &= g_ports[0].stock[GOOD_TEA] == 9;
  ok &= g_ships[0].cargoCount == 1 && g_ships[0].cargo[0].quantity == 3 &&
        g_ships[0].cargo[0].paidUnitPrice == 17 && g_ships[0].cargo[0].contractId == 2;
  ok &= g_ports[0].warehouseCount == 1 && g_ports[0].warehouse[0].quantity == 4 &&
        g_ports[0].warehouse[0].paidUnitPrice == 29 && g_ports[0].warehouse[0].contractId == 1;
  ok &= !strcmp(g_ships[0].name, "Snapshot A") && g_ships[0].routeTemplate.stopCount == 1;
  ok &= g_captains[0].voyages == 12 && g_captains[0].deliveries == 34;
  ok &= g_contracts[0][0].reward == 777;
  ok &= g_incident.active && g_ships[0].state == SHIP_AWAITING_DECISION;
  ok &= g_attentionCount >= 1;
  step("snapshot round trip");

  // No component is allowed to fall back independently. Damage each bundle
  // component in turn; treasury, fleet and port demand must all return to A.
  for (int broken = 0; broken < CH_COUNT; ++broken) {
    save_reset(); sim_new_campaign(0xA000u + broken);
    g_campaign.coins = 7000;
    g_campaign.standbySessionId = 19;
    g_campaign.standbyAccountedRealSeconds = 120;
    g_campaign.deliveredUnits = 4;
    g_ports[0].bookingUsed[GOOD_GRAIN] = 4;
    snprintf(g_ships[0].name, sizeof(g_ships[0].name), "Whole A");
    ok &= save_write();
    g_campaign.coins = 9000;
    g_campaign.standbyAccountedRealSeconds = 240;
    g_campaign.deliveredUnits = 8;
    g_ports[0].bookingUsed[GOOD_GRAIN] = 8;
    snprintf(g_ships[0].name, sizeof(g_ships[0].name), "Whole B");
    ok &= save_write();
    File f = LittleFS.open(SNAP_PATH, "r+");
    size_t at = 16;
    uint8_t h[16];
    for (int i = 0; i <= broken && ok; ++i) {
      ok &= f.seek(at) && f.read(h, 16) == 16;
      if (i == broken) {
        uint8_t byte = 0;
        ok &= f.seek(at + 16) && f.read(&byte, 1) == 1;
        byte ^= 0xA5;
        ok &= f.seek(at + 16) && f.write(&byte, 1) == 1;
      }
      at += 16 + header_get(h, 8);
    }
    f.close();
    ok &= save_read();
    ok &= g_campaign.coins == 7000 && g_campaign.deliveredUnits == 4 &&
      g_campaign.standbyAccountedRealSeconds == 120 && g_campaign.standbySessionId == 19 &&
      g_ports[0].bookingUsed[GOOD_GRAIN] == 4 && !strcmp(g_ships[0].name, "Whole A");
  }
  step("whole-company fallback for every damaged component");

  // A torn temporary write is never considered committed. A power loss after
  // rotating the current bundle but before its replacement still loads .bak.
  save_reset(); sim_new_campaign(0xA700u); g_campaign.coins = 7100;
  ok &= save_write();
  { File f = LittleFS.open(SNAP_TMP, "w"); uint8_t partial[9]{};
    ok &= f && f.write(partial, sizeof(partial)) == sizeof(partial); f.close(); }
  g_campaign.coins = 0; ok &= save_read() && g_campaign.coins == 7100;
  ok &= LittleFS.rename(SNAP_PATH, SNAP_BAK);
  g_campaign.coins = 0; ok &= save_read() && g_campaign.coins == 7100;
  ok &= save_write() && save_read() && g_campaign.coins == 7100;
  step("interrupted write and rename recovery");

  // Exercise v2 core and fleet layouts directly and verify their derived v5 state.
  sim_new_campaign(0xB200u);
  g_ui.scene = SCENE_PORT;  // this legacy wire format pre-dates Fleet home
  g_ships[0].cargoCount = 0;  // the fleet record below carries no stacks; drop the new-company gift
  pos = 0;
  put32(g_campaign.seed); put32(g_campaign.rng); put32(g_campaign.worldMinutes);
  put32(g_campaign.nextContractId); put32(g_campaign.nextShipId); puti32(g_campaign.coins);
  put16(g_campaign.companyXp); put16(g_campaign.deliveries);
  put16(g_campaign.contractsCompleted); put16(g_campaign.incidentsResolved);
  for (int i = 0; i < REGION_COUNT; ++i) put16(g_campaign.charterProgress[i]);
  put32(g_campaign.contractRevenue); puti32(g_campaign.marketProfit);
  put32(g_campaign.operatingCosts); put8(g_campaign.rank); put8(g_campaign.regionMask);
  put8(g_campaign.chartMask); put8(g_campaign.visitedMask); put8(g_campaign.shipCount);
  put8(g_campaign.won); put8(g_campaign.paused); put8(g_campaign.soundOn);
  put8(g_campaign.worldEventType); put8(g_campaign.worldEventGood);
  put8(g_campaign.worldEventRegion); put8(g_campaign.worldEventDays);
  put16(g_campaign.lastMarketDay); put8(g_ui.scene); put8(g_ui.selectedPort);
  put8(g_ui.selectedShip); put8(g_ui.selectedGood);
  for (int i = 0; i < CH_CORE; ++i) put32(0);
  size_t legacyCoreLen = pos;
  g_campaign.recoveryDebt = 999;
  g_campaign.tutorialDismissed = 0;
  ok &= apply_chunk(CH_CORE, buf, legacyCoreLen, 2);
  ok &= g_campaign.recoveryDebt == 0 && g_campaign.tutorialDismissed == 1 &&
        g_campaign.tutorialStep == 8 && g_campaign.timeScale == 1;

  pos = 0;
  put8(1);
  {
    const Ship& s = g_ships[0];
    char legacyName[14] = "Legacy Mara";
    uint8_t legacyRoute[MAX_ROUTE_LEGS]{};
    legacyRoute[0] = 1;
    put32(s.id); putbytes(s.name, sizeof(s.name)); putbytes(legacyName, sizeof(legacyName));
    put8(TRAIT_MERCHANT); put8(2); put16(9);
    put8(s.classId); put8(s.state); put8(s.currentPort); put8(s.fromPort); put8(s.toPort);
    for (int j = 0; j < MAX_ROUTE_LEGS; ++j) put8(s.route[j]);
    for (int j = 0; j < MAX_ROUTE_LEGS; ++j) put8(legacyRoute[j]);
    put8(s.routeCount); put8(s.routePos); put8(1); put8(s.cargoCount);
    put8(s.condition); put8(s.morale); put8(s.crew); put8(s.modules[0]); put8(s.modules[1]);
    put8(s.incidentType); put8(s.incidentArmed); put8(s.manifestBonus);
    put16(s.legElapsedMinutes); put16(s.legTotalMinutes); put16(s.prepaidCost);
  }
  put8(0); put8(0); put8(0); put8(0); put8(0);
  size_t legacyFleetLen = pos;
  memset(g_captains, 0, sizeof(g_captains));
  ok &= apply_chunk(CH_FLEET, buf, legacyFleetLen, 2);
  ok &= g_campaign.captainCount == 1 && g_captains[0].active &&
        !strcmp(g_captains[0].name, "Legacy Mara") &&
        g_ships[0].routeTemplate.stopCount == 1 && g_ships[0].routeTemplate.stops[0] == 1;

  // Version 3 carried both legacy ship fields and the new roster/template fields.
  // Decode them, but retain only the latter in the live v5 model.
  sim_new_campaign(0xB301u);
  g_ships[0].cargoCount = 0;  // as above, the record carries no stacks
  pos = 0;
  put8(1);
  {
    const Ship& s = g_ships[0];
    char legacyName[14] = "Embedded";
    uint8_t legacyRoute[MAX_ROUTE_LEGS]{};
    legacyRoute[0] = 2;
    put32(s.id); putbytes(s.name, sizeof(s.name)); putbytes(legacyName, sizeof(legacyName));
    put8(TRAIT_GUNNER); put8(1); put16(2);
    put8(s.classId); put8(s.state); put8(s.currentPort); put8(s.fromPort); put8(s.toPort);
    for (int j = 0; j < MAX_ROUTE_LEGS; ++j) put8(s.route[j]);
    for (int j = 0; j < MAX_ROUTE_LEGS; ++j) put8(legacyRoute[j]);
    put8(s.routeCount); put8(s.routePos); put8(1); put8(s.cargoCount);
    put8(s.condition); put8(s.morale); put8(s.crew); put8(s.modules[0]); put8(s.modules[1]);
    put8(s.incidentType); put8(s.incidentArmed); put8(s.manifestBonus);
    put16(s.legElapsedMinutes); put16(s.legTotalMinutes); put16(s.prepaidCost);
    put8(0);
    RouteTemplate templateRoute{};
    templateRoute.origin = 0; templateRoute.stops[0] = 1;
    templateRoute.stopCount = 1; templateRoute.buyGood = NO_PORT;
    put_route_template(templateRoute);
  }
  put8(0); put8(0); put8(0); put8(0); put8(0);
  put8(1);
  {
    char rosterName[14] = "Roster Mara";
    putbytes(rosterName, sizeof(rosterName)); put16(11); put16(300);
    put8(TRAIT_MERCHANT); put8(2); put8(72); put8(SCAR_STORM); put8(SCAR_NONE);
    put8(1); put8(0); put8(0); put8(1);
  }
  size_t legacyV3FleetLen = pos;
  ok &= apply_chunk(CH_FLEET, buf, legacyV3FleetLen, 3);
  ok &= !strcmp(g_captains[0].name, "Roster Mara") && g_captains[0].xp == 11 &&
        g_captains[0].voyages == 0 && g_ships[0].captainId == 0 &&
        g_ships[0].routeTemplate.stopCount == 1 && g_ships[0].routeTemplate.stops[0] == 1;

  sim_new_campaign(0xB300u);
  g_campaign.tutorialStep = TUTORIAL_DISPATCH;
  g_campaign.tutorialFlags = 0xFFu;
  emitVersion = 6;  // the v3 record is the v6 layout minus its three trailing bytes
  size_t currentCoreLen = build_chunk(CH_CORE);
  emitVersion = CH_VERSION;
  // v3 has no time-scale byte after the three sound/pause flags, and it also
  // predates the explicit tutorial-flags byte and the stage byte that end the
  // current record.
  memmove(buf + 64, buf + 65, currentCoreLen - 65);
  size_t legacyV3CoreLen = currentCoreLen - 3;
  g_campaign.tutorialFlags = 0;
  ok &= apply_chunk(CH_CORE, buf, legacyV3CoreLen, 3);
  ok &= g_campaign.tutorialFlags == 0x0Fu &&
        g_campaign.tutorialStep == TUTORIAL_DISPATCH;
  step("v2 and v3 layouts");

  // Version 6 records (no previous CRCs, ledger aggregates, voyage totals or
  // notice region) load with their defaults, whether fabricated by emitting
  // the older layout or by trimming the tail of a current core.
  sim_new_campaign(0xC600u);
  g_campaign.capitalSpending = 55; g_campaign.incidentNet = -7;
  g_ships[0].voyageEarned = 99; g_ships[0].voyageDelivered = 3;
  attention_add(ATTENTION_ARRIVAL, 0, 0, false);
  g_attention[0].region = 2;
  emitVersion = 6;
  size_t core6 = build_chunk(CH_CORE);
  ok &= apply_chunk(CH_CORE, buf, core6, 6);
  ok &= g_campaign.capitalSpending == 0 && g_campaign.incidentNet == 0;
  for (int i = 0; i < CH_CORE; ++i) ok &= prevExpectedCrc[i] == expectedCrc[i];
  g_ships[0].voyageEarned = 99; g_ships[0].voyageDelivered = 3;
  size_t fleet6 = build_chunk(CH_FLEET);
  ok &= apply_chunk(CH_FLEET, buf, fleet6, 6);
  ok &= g_ships[0].voyageEarned == 0 && g_ships[0].voyageDelivered == 0;
  g_attention[0].region = 2;
  size_t journal6 = build_chunk(CH_JOURNAL);
  ok &= apply_chunk(CH_JOURNAL, buf, journal6, 6);
  ok &= g_attentionCount >= 1 && g_attention[0].region == 0xFF;
  emitVersion = CH_VERSION;
  g_campaign.capitalSpending = 55; g_campaign.incidentNet = -7;
  emitVersion = 7;
  size_t core7 = build_chunk(CH_CORE);
  emitVersion = CH_VERSION;
  ok &= core7 >= 24 && apply_chunk(CH_CORE, buf, core7 - 24, 6);
  ok &= g_campaign.capitalSpending == 0 && g_campaign.incidentNet == 0;
  ok &= apply_chunk(CH_CORE, buf, build_chunk(CH_CORE), CH_VERSION);
  g_campaign.capitalSpending = 55; g_campaign.incidentNet = -7;
  ok &= apply_chunk(CH_CORE, buf, build_chunk(CH_CORE), CH_VERSION) &&
        g_campaign.capitalSpending == 55 && g_campaign.incidentNet == -7;
  step("v6 records load with defaults");

  // A sibling chunk whose current file is corrupt comes back from its backup,
  // one generation behind, rather than from the fresh-company default.
  save_reset();
  sim_new_campaign(0xBAC0u);
  snprintf(g_ships[0].name, sizeof(g_ships[0].name), "Gen A");
  ok &= save_write();
  snprintf(g_ships[0].name, sizeof(g_ships[0].name), "Gen B");
  ok &= save_write();
  ok &= corrupt(SNAP_PATH) && save_read();
  ok &= !strcmp(g_ships[0].name, "Gen A");
  step("backup one generation behind is accepted");

  // An unreadable core is set aside, never deleted, before starting over.
  save_reset();
  sim_new_campaign(0xC0DEu);
  ok &= save_write();
  {
    File f = LittleFS.open(SNAP_PATH, "r+");
    uint8_t ancient = 1;  // below CH_OLDEST_VERSION
    ok &= f && f.seek(5) && f.write(&ancient, 1) == 1;
    f.close();
  }
  ok &= !save_read();
  save_quarantine();
  ok &= !save_exists() && !LittleFS.exists(SNAP_PATH) && LittleFS.exists("/sh_company.sav.orphan");
  step("unreadable save is quarantined");
  save_reset();

  save_reset();
  sim_new_campaign(0xC708u);
  g_campaign.coins = 8123; g_campaign.recoveryDebt = 71;
  g_campaign.deliveries = 9; g_campaign.stageSeen = 3;
  g_campaign.chartMask |= 2u; g_campaign.charterProgress[1] = 3;
  CargoItem legacyLot{};
  legacyLot.kind = CARGO_MARKET; legacyLot.good = GOOD_GRAIN;
  legacyLot.quantity = 3; legacyLot.paidUnitPrice = 17; legacyLot.contractId = 2;
  legacyLot.destination = NO_PORT; legacyLot.projectRegion = NO_PORT;
  g_ships[0].cargo[0] = legacyLot; g_ships[0].cargoCount = 1;
  emitVersion = 7;
  for (uint8_t id = 0; id < CH_COUNT; ++id) ok &= write_chunk(id);
  emitVersion = CH_VERSION;
  ok &= save_read() && g_campaign.coins == 8123 && g_campaign.recoveryDebt == 71 &&
    g_campaign.deliveredUnits == 9 && g_campaign.economyVersion == SAVE_TEST_LOADED_ECONOMY &&
    g_campaign.projectWorksCredit[1] == 150 && sim_project_cost(1) == 750 &&
    g_campaign.projectCargo[1][0] == 6 && g_campaign.projectCargo[1][1] == 6 &&
    (g_campaign.legacyFeatureMask & (1u << FEATURE_WAREHOUSE)) &&
    g_campaign.stageSeen >= 3 && g_ships[0].cargo[0].sourcePort == NO_PORT &&
    g_ships[0].cargo[0].contractId == 2 && g_ships[0].booking.state == BOOKING_NONE;
  ok &= save_write() && LittleFS.exists(CH_PATH[CH_CORE]) && save_read();
  ok &= g_campaign.coins == 8123 && g_ships[0].cargo[0].contractId == 2;
  step("v7 migration preserves assets, debt, basis and source backup");
  save_reset();

  sim_new_campaign(0xDEADu);
  ok &= save_write() && corrupt(SNAP_PATH) && !save_read();
  step("corrupt lone core is refused");
  Serial.println(ok ? "save selftest: PASS" : "save selftest: FAIL");
  save_reset();
  return ok;
#else
  Serial.println("save selftest is available in the desktop host");
  return true;
#endif
}
