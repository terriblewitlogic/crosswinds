// The Grey Passenger: a recurring courier offers one sealed parcel at a time.
// Included by sim.cpp after sim_merchant.h; the chapter text is in story_content.h.
#pragma once
#include "story_content.h"

static constexpr uint32_t STORY_CARGO_BASE = 0x53540000u;  // "ST"; never a contract id
static bool storyArrivalDelivered = false;  // a delivery on this arrival already set the cooldown
#if defined(SH_HOST_TESTS)
static bool storyEnabled = true;
void sim_test_story(bool enabled) { storyEnabled = enabled; }
#endif

static void story_reset() {
  g_campaign.storyDelivered = 0; g_campaign.storyDeliveries = 0;
  memset(g_campaign.storyOrder, STORY_NONE, sizeof(g_campaign.storyOrder));
  g_campaign.storyCarrying = STORY_NONE;
  g_campaign.storyOfferShipId = 0; g_campaign.storyOfferPort = NO_PORT;
  g_campaign.storyOfferChapter = STORY_NONE; g_campaign.storyLastPort = NO_PORT;
  g_campaign.storyCooldown = STORY_COOLDOWN_START;
  g_campaign.storyCardPending = STORY_NONE; g_campaign.storyOffers = 0;
}

static inline bool story_cargo(const CargoItem& c) {
  return c.kind == CARGO_CONTRACT && c.jobKind == JOB_STORY;
}

static void story_clear_offer(uint8_t cooldown) {
  if (g_campaign.storyOfferChapter == STORY_NONE) return;
  g_campaign.storyLastPort = g_campaign.storyOfferPort;
  g_campaign.storyOfferShipId = 0; g_campaign.storyOfferPort = NO_PORT;
  g_campaign.storyOfferChapter = STORY_NONE;
  g_campaign.storyCooldown = cooldown;
}

static int story_offer_ship() {
  if (g_campaign.storyOfferChapter == STORY_NONE) return -1;
  for (uint8_t i = 0; i < g_campaign.shipCount; ++i)
    if (g_ships[i].id == g_campaign.storyOfferShipId) return i;
  return -1;
}

// Her offer lasts while its ship stays docked where she found it.
static bool story_offer_live() {
  int index = story_offer_ship();
  if (index < 0) return false;
  const Ship& s = g_ships[index];
  return ship_at_port(s) && s.state != SHIP_DISABLED && s.merchant.state != MERCHANT_SAILING &&
         s.currentPort == g_campaign.storyOfferPort;
}

static bool story_chapter_reachable(const Ship& s, uint8_t chapter, uint32_t* minutes) {
  uint8_t port = STORY_CHAPTER_DEFS[chapter].port;
  if (port == s.currentPort || !g_ports[port].unlocked) return false;
  VoyageQuote route{};
  if (!booked_route(s, port, route)) return false;
  if (minutes) *minutes = route.minutes;
  return true;
}

// Chapter 1 first and the toy boat last; the middle five follow the open map,
// nearest first.
static uint8_t story_pick_chapter(const Ship& s) {
  const uint8_t done = g_campaign.storyDelivered;
  if (!(done & 1u)) return story_chapter_reachable(s, 0, nullptr) ? 0 : STORY_NONE;
  const uint8_t last = STORY_CHAPTERS - 1;
  if ((done | (1u << last)) == STORY_ALL_DELIVERED)
    return story_chapter_reachable(s, last, nullptr) ? last : STORY_NONE;
  uint8_t best = STORY_NONE; uint32_t bestMinutes = UINT32_MAX;
  for (uint8_t chapter = 1; chapter < last; ++chapter) {
    uint32_t minutes = 0;
    if (!(done & (1u << chapter)) && story_chapter_reachable(s, chapter, &minutes) && minutes < bestMinutes) {
      best = chapter; bestMinutes = minutes;
    }
  }
  return best;
}

// A completed merchant voyage: count down, then perhaps approach this ship.
static void story_on_arrival(Ship& s) {
  bool delivered = storyArrivalDelivered; storyArrivalDelivered = false;
#if defined(SH_HOST_TESTS)
  if (!storyEnabled) return;
#endif
  if (sim_onboarding_step() != ONBOARD_DONE || g_campaign.storyDelivered == STORY_ALL_DELIVERED) return;
  if (g_campaign.storyOfferChapter != STORY_NONE && !story_offer_live())
    story_clear_offer(STORY_COOLDOWN_DECLINED);
  if (delivered) return;
  if (g_campaign.storyCooldown) { --g_campaign.storyCooldown; return; }
  if (g_campaign.storyOfferChapter != STORY_NONE || g_campaign.storyCarrying != STORY_NONE) return;
  if (s.state == SHIP_DISABLED || s.merchant.state != MERCHANT_ARRIVED) return;
  if (s.currentPort == g_campaign.storyLastPort) return;
  uint8_t chapter = story_pick_chapter(s);
  if (chapter == STORY_NONE) return;
  g_campaign.storyOfferShipId = s.id; g_campaign.storyOfferPort = s.currentPort;
  g_campaign.storyOfferChapter = chapter;
  ++g_campaign.storyOffers;
}

// Sailing away without her parcel is a decline.
static void story_on_departure(uint8_t index) {
  if (story_offer_ship() == (int)index) story_clear_offer(STORY_COOLDOWN_DECLINED);
}

// The keepsake is lost with its ship; she will offer the chapter again.
static void story_on_cargo_lost(const CargoItem& c) {
  if (story_cargo(c)) g_campaign.storyCarrying = STORY_NONE;
}

uint16_t sim_story_reward() {
  return STORY_REWARDS[g_campaign.storyDeliveries < STORY_CHAPTERS ? g_campaign.storyDeliveries : STORY_CHAPTERS - 1];
}

// Called from deliver_at_port with the keepsake at its destination. Accounting
// matches a parcel so the arrival receipt and return ledger stay exact.
static int32_t story_deliver(Ship& s, int cargoIndex) {
  const CargoItem& cargo = s.cargo[cargoIndex];
  uint8_t chapter = (uint8_t)(cargo.contractId - STORY_CARGO_BASE);
  if (chapter != g_campaign.storyCarrying || chapter >= STORY_CHAPTERS ||
      cargo.destination != STORY_CHAPTER_DEFS[chapter].port) return -1;
  int32_t reward = sim_story_reward();
  g_campaign.coins += reward; g_campaign.contractRevenue += (uint32_t)reward;
  ++g_campaign.deliveredUnits;
  if (g_campaign.companyXp < 65535u) ++g_campaign.companyXp;
  if (g_campaign.deliveries < 65535u) ++g_campaign.deliveries;
  if (g_campaign.contractsCompleted < 65535u) ++g_campaign.contractsCompleted;
  s.merchant.packageCoins += (uint32_t)reward; ++s.merchant.packagesDelivered;
  CaptainProfile* captain = sim_ship_captain((uint8_t)(&s - g_ships));
  if (captain && captain->deliveries < 65535u) ++captain->deliveries;
  g_campaign.storyDelivered |= (uint8_t)(1u << chapter);
  g_campaign.storyOrder[g_campaign.storyDeliveries++] = chapter;
  g_campaign.storyCarrying = STORY_NONE;
  g_campaign.storyCooldown = STORY_COOLDOWN_DELIVERED;
  g_campaign.storyCardPending = chapter;
  storyArrivalDelivered = true;
  cargo_remove(s, cargoIndex);
  journal_add(1, "Delivered the %s to %s.", STORY_CHAPTER_DEFS[chapter].item,
              STORY_CHAPTER_DEFS[chapter].recipient);
  return reward;
}

bool sim_story_offer(uint8_t index, StoryOffer& out) {
  out = {}; out.shipIndex = index; out.chapter = STORY_NONE; out.code = SIM_CONTRACT_UNAVAILABLE;
  if (index >= g_campaign.shipCount || story_offer_ship() != (int)index || !story_offer_live()) return false;
  const Ship& s = g_ships[index];
  out.chapter = g_campaign.storyOfferChapter;
  out.reward = sim_story_reward();
  out.line = out.chapter == STORY_CHAPTERS - 1 ? 3 : (uint8_t)((g_campaign.storyOffers + 2) % 3);
  out.code = sim_ship_used_capacity(s) < sim_ship_capacity(s) && s.cargoCount < MAX_CARGO_ITEMS ?
    SIM_OK : SIM_NO_CAPACITY;
  return true;
}

SimResult sim_story_accept(uint8_t index) {
  StoryOffer offer{};
  if (!sim_story_offer(index, offer)) return SIM_CONTRACT_UNAVAILABLE;
  if (offer.code != SIM_OK) return offer.code;
  MerchantUndo before(index);
  Ship& s = g_ships[index];
  CargoItem item{};
  item.contractId = STORY_CARGO_BASE + offer.chapter; item.kind = CARGO_CONTRACT; item.jobKind = JOB_STORY;
  item.good = NO_PORT; item.quantity = 1; item.destination = STORY_CHAPTER_DEFS[offer.chapter].port;
  item.projectRegion = NO_PORT; item.sourcePort = s.currentPort;
  s.cargo[s.cargoCount++] = item;
  g_campaign.storyCarrying = offer.chapter;
  g_campaign.storyLastPort = s.currentPort;
  g_campaign.storyOfferShipId = 0; g_campaign.storyOfferPort = NO_PORT;
  g_campaign.storyOfferChapter = STORY_NONE;
  journal_add(0, "Keepsake aboard for %s.", PORT_DEFS[item.destination].name);
  g_ui.dirty = 1;
  if (!save_write()) { before.restore(); return SIM_SAVE_FAILED; }
  return SIM_OK;
}

uint8_t sim_story_card_pending() { return g_campaign.storyCardPending; }

void sim_story_ack_card() {
  if (g_campaign.storyCardPending == STORY_NONE) return;
  g_campaign.storyCardPending = STORY_NONE;
  g_ui.dirty = 1;
  save_write();
}

int sim_story_carrier() {
  if (g_campaign.storyCarrying == STORY_NONE) return -1;
  for (uint8_t i = 0; i < g_campaign.shipCount; ++i)
    for (uint8_t c = 0; c < g_ships[i].cargoCount; ++c)
      if (story_cargo(g_ships[i].cargo[c])) return i;
  return -1;
}
