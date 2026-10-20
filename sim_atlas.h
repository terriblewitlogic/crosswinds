// The Sea Atlas: a port's page is charted the first time a company ship
// docks there. Included by sim.cpp; the page text is in atlas_content.h.
#pragma once
#include "atlas_content.h"

AtlasState g_atlas;
static constexpr uint32_t ATLAS_ALL = (1u << ATLAS_PORTS) - 1u;

static void atlas_reset() {
  memset(&g_atlas, 0, sizeof(g_atlas));
  g_atlas.charted = 1u;  // Lantern Quay, the company's home port
  g_atlas.day[0] = 1; g_atlas.ship[0] = ATLAS_HOME;
}

bool sim_atlas_charted(uint8_t port) { return port < ATLAS_PORTS && (g_atlas.charted & (1u << port)); }

uint8_t sim_atlas_count() {
  uint8_t n = 0;
  for (uint8_t p = 0; p < ATLAS_PORTS; ++p) n += sim_atlas_charted(p);
  return n;
}

uint8_t sim_atlas_region_count(uint8_t region) {
  uint8_t n = 0;
  for (uint8_t p = 0; p < ATLAS_PORTS; ++p) n += PORT_DEFS[p].region == region && sim_atlas_charted(p);
  return n;
}

bool sim_atlas_complete() { return (g_atlas.charted & ATLAS_ALL) == ATLAS_ALL; }

static bool atlas_region_full(uint8_t region) {
  for (uint8_t p = 0; p < ATLAS_PORTS; ++p)
    if (PORT_DEFS[p].region == region && !sim_atlas_charted(p)) return false;
  return true;
}

static void atlas_record(uint8_t port, uint8_t ship, uint32_t voyage) {
  g_atlas.charted |= 1u << port;
  g_atlas.day[port] = (uint16_t)min((uint32_t)65535u, g_campaign.worldMinutes / 1440u + 1u);
  g_atlas.ship[port] = ship; g_atlas.voyage[port] = voyage;
}

// Called from arrive_leg on every leg: the ship really did dock there.
static void atlas_chart(Ship& s) {
  const uint8_t port = s.currentPort;
  if (port >= ATLAS_PORTS || sim_atlas_charted(port)) return;
  atlas_record(port, (uint8_t)(&s - g_ships), s.merchant.id);
  journal_add(1, "New Atlas page: %s.", PORT_DEFS[port].name);
  const uint8_t region = PORT_DEFS[port].region;
  if (atlas_region_full(region) && !(g_atlas.regionsRewarded & (1u << region))) {
    g_atlas.regionsRewarded |= (uint8_t)(1u << region);
    g_campaign.coins += ATLAS_REGION_REWARD; g_campaign.incidentNet += ATLAS_REGION_REWARD;
    journal_add(1, "%s charted: the chart office pays %u.", REGION_NAMES[region], ATLAS_REGION_REWARD);
  }
  if (sim_atlas_complete()) journal_add(2, "The Sea Atlas is complete. Chartmakers!");
}

// Saves from before the Atlas: the home port and wherever ships lie now.
// Regions that turn out complete count as rewarded, without paying.
void sim_atlas_migrate() {
  atlas_reset();
  for (uint8_t i = 0; i < g_campaign.shipCount; ++i) {
    const Ship& s = g_ships[i];
    if (s.state != SHIP_SAILING && s.currentPort < ATLAS_PORTS && !sim_atlas_charted(s.currentPort))
      atlas_record(s.currentPort, i, 0);
  }
  for (uint8_t region = 0; region < REGION_COUNT; ++region)
    if (atlas_region_full(region)) g_atlas.regionsRewarded |= (uint8_t)(1u << region);
}

// Pages this ship charted on the voyage it has just finished; the note stays
// on its overview until it sails again.
uint8_t sim_atlas_voyage_pages(uint8_t index, uint8_t* ports, uint8_t cap) {
  if (index >= g_campaign.shipCount) return 0;
  const Ship& s = g_ships[index];
  if (s.merchant.state != MERCHANT_ARRIVED || !s.merchant.id) return 0;
  uint8_t n = 0;
  for (uint8_t p = 0; p < ATLAS_PORTS; ++p)
    if (sim_atlas_charted(p) && g_atlas.ship[p] == index && g_atlas.voyage[p] == s.merchant.id) {
      if (n < cap && ports) ports[n] = p;
      ++n;
    }
  return n;
}
