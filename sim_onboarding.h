// Included at the end of sim.cpp. The X4 first voyage:
// choose and name a captain, buy Textiles at Lantern Quay, sail to Gullhaven,
// sell them. Only the captain screens are explicit steps; the voyage steps are
// read from the starting ship's hold and position each update.
#pragma once

static const uint8_t ONBOARD_TRAITS[3] = { TRAIT_NAVIGATOR, TRAIT_MERCHANT, TRAIT_GUNNER };
// Extra names for renaming; every name fits CaptainProfile::name (13 chars).
static const char* const ONBOARD_EXTRA_NAMES[] = {
  "Iris Hale", "Tomas Wren", "Cora Blythe", "Owen Marsh", "Lena Frost", "Abel Cray"
};
static constexpr uint8_t ONBOARD_NAME_COUNT = 12 + sizeof(ONBOARD_EXTRA_NAMES) / sizeof(ONBOARD_EXTRA_NAMES[0]);

bool sim_onboarding_active() {
  return g_campaign.economyVersion >= 2 && g_campaign.onboardingStep != ONBOARD_DONE &&
         !g_campaign.tutorialDismissed;
}

uint8_t sim_onboarding_step() { return sim_onboarding_active() ? g_campaign.onboardingStep : (uint8_t)ONBOARD_DONE; }

const char* sim_onboarding_name(uint8_t index) {
  index %= ONBOARD_NAME_COUNT;
  return index < 12 ? CAPTAIN_NAMES[index] : ONBOARD_EXTRA_NAMES[index - 12];
}
uint8_t sim_onboarding_name_count() { return ONBOARD_NAME_COUNT; }

// Three candidates from the company seed: always a Navigator, a Merchant and
// a Gunner, with distinct names and distinct faces.
bool sim_onboarding_candidate(uint8_t index, CaptainProfile* out) {
  if (index > 2 || !out) return false;
  memset(out, 0, sizeof(*out));
  const uint8_t first = (uint8_t)(g_campaign.seed % 12u);
  snprintf(out->name, sizeof(out->name), "%s", CAPTAIN_NAMES[(first + index * 4u) % 12u]);
  out->trait = ONBOARD_TRAITS[index];
  out->level = 1;
  out->loyalty = BALANCE.captainStartingLoyalty;
  uint8_t taken = 0;  // portraits of the earlier candidates
  for (uint8_t i = 0; i <= index; ++i) {
    char name[sizeof(out->name)];
    snprintf(name, sizeof(name), "%s", CAPTAIN_NAMES[(first + i * 4u) % 12u]);
    uint8_t face = content_captain_portrait(name, ONBOARD_TRAITS[i]);
    while (taken & (1u << (face & 7))) face = (uint8_t)((face + 1) & 15u);
    taken |= (uint8_t)(1u << (face & 7));
    if (i == index) out->portrait = face;
  }
  return true;
}

static CaptainProfile* onboarding_captain() {
  return g_campaign.shipCount ? sim_ship_captain(0) : nullptr;
}

// The chosen candidate becomes the starting ship's captain: name, trait and
// face replace the placeholder; record and assignment stay.
SimResult sim_onboarding_hire(uint8_t index) {
  CaptainProfile chosen{};
  CaptainProfile* captain = onboarding_captain();
  if (!sim_onboarding_active() || g_campaign.onboardingStep > ONBOARD_NAME || !captain ||
      !sim_onboarding_candidate(index, &chosen)) return SIM_INVALID;
  memcpy(captain->name, chosen.name, sizeof(captain->name));
  captain->trait = chosen.trait;
  captain->portrait = chosen.portrait;
  g_campaign.onboardingStep = ONBOARD_NAME;
  g_ui.dirty = 1;
  return SIM_OK;
}

SimResult sim_onboarding_rename(uint8_t nameIndex) {
  CaptainProfile* captain = onboarding_captain();
  if (!sim_onboarding_active() || g_campaign.onboardingStep != ONBOARD_NAME || !captain) return SIM_INVALID;
  snprintf(captain->name, sizeof(captain->name), "%s", sim_onboarding_name(nameIndex));
  g_ui.dirty = 1;
  return SIM_OK;
}

// Removes the new-company Grain gift: the guided first trade replaces it.
static void onboarding_drop_gift() {
  if (!g_campaign.shipCount) return;
  Ship& ship = g_ships[0];
  for (int i = 0; i < ship.cargoCount; ++i) {
    const CargoItem& c = ship.cargo[i];
    if (c.kind != CARGO_MARKET || c.good != GOOD_GRAIN || c.sourcePort != NO_PORT || c.sailed) continue;
    for (int j = i + 1; j < ship.cargoCount; ++j) ship.cargo[j - 1] = ship.cargo[j];
    --ship.cargoCount;
    return;
  }
}

SimResult sim_onboarding_confirm_name() {
  CaptainProfile* captain = onboarding_captain();
  if (!sim_onboarding_active() || g_campaign.onboardingStep != ONBOARD_NAME || !captain) return SIM_INVALID;
  onboarding_drop_gift();
  g_campaign.onboardingStep = ONBOARD_BUY;
  journal_add(1, "Captain %s takes command of %s.", captain->name, g_ships[0].name);
  g_ui.dirty = 1;
  return save_write() ? SIM_OK : SIM_SAVE_FAILED;
}

// Leaves the guided voyage. Before the captain is chosen, the highlighted
// candidate is hired; the Grain gift stays aboard if it is still there.
SimResult sim_onboarding_skip(uint8_t candidate) {
  if (!sim_onboarding_active()) return SIM_INVALID;
  if (g_campaign.onboardingStep == ONBOARD_CAPTAIN) sim_onboarding_hire(candidate);
  g_campaign.onboardingStep = ONBOARD_DONE;
  g_campaign.tutorialDismissed = 1;
  g_ui.dirty = 1;
  return save_write() ? SIM_OK : SIM_SAVE_FAILED;
}

static unsigned onboarding_textiles(const Ship& ship) {
  unsigned units = 0;
  for (int i = 0; i < ship.cargoCount; ++i)
    if (ship.cargo[i].kind == CARGO_MARKET && ship.cargo[i].good == GOOD_TEXTILES) units += ship.cargo[i].quantity;
  return units;
}

// Re-derives the voyage step. Returns true once, when the Textiles are sold at
// Gullhaven and the company leaves the rails.
bool sim_onboarding_update() {
  if (!sim_onboarding_active() || g_campaign.onboardingStep < ONBOARD_BUY || !g_campaign.shipCount) return false;
  const Ship& ship = g_ships[0];
  const unsigned textiles = onboarding_textiles(ship);
  const uint8_t before = g_campaign.onboardingStep;
  uint8_t step = before;
  if (ship.merchant.state == MERCHANT_SAILING) step = ONBOARD_VOYAGE;
  else if (ship_at_port(ship) && ship.currentPort == ONBOARD_PORT_TO) {
    if (textiles) step = ONBOARD_SELL;
    else if (before >= ONBOARD_VOYAGE) step = ONBOARD_DONE;
  } else if (ship_at_port(ship) && ship.currentPort == ONBOARD_PORT_FROM)
    step = textiles ? ONBOARD_SAIL : ONBOARD_BUY;
  if (step == before) return false;
  g_campaign.onboardingStep = step;
  g_ui.dirty = 1;
  if (step != ONBOARD_DONE) return false;
  journal_add(2, "First voyage complete: parcels and every port are open.");
  save_write();
  return true;
}

// While on rails the sail list is Gullhaven only, and only once Textiles are
// aboard.
bool sim_onboarding_allows_destination(uint8_t shipIndex, uint8_t port) {
  if (!sim_onboarding_active() || shipIndex != 0) return true;
  return g_campaign.onboardingStep == ONBOARD_SAIL && port == ONBOARD_PORT_TO;
}
