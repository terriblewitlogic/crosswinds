#pragma once
// Fixtures for the historical economies: 0 is the contract board and spot
// market, 1 is signed voyages. sim_new_campaign builds a v2 merchant company,
// with package offers on every board and a starter gift of Grain aboard. The
// older suites rebuild the boards under their own rules and drop the gift, so
// they start from the company those economies started from.
#include "game.h"
#include <string.h>

static inline void historical_campaign(uint32_t seed, uint8_t economyVersion) {
  sim_new_campaign(seed);
  g_campaign.economyVersion = economyVersion;
  memset(g_ships[0].cargo, 0, sizeof(g_ships[0].cargo));
  g_ships[0].cargoCount = 0;
  memset(g_contracts, 0, sizeof(g_contracts));
  for (uint8_t p = 0; p < MAX_PORTS; ++p)
    if (g_ports[p].unlocked) sim_generate_jobs(p, true);
}
