#pragma once

// X4's pocket-world voice. These are immutable descriptions of the existing
// ports and rules, not new simulation state. Indexed IDs match content.cpp.
#include <stdint.h>
#include <stddef.h>

struct X4RegionPresentation {
  const char* tagline;
  const char* projectName;
  const char* projectPurpose;
};

struct X4PortPresentation {
  const char* landmark;
  const char* description;
};

struct X4IncidentPresentation {
  const char* title;
  const char* flavor[2];
};

static const X4RegionPresentation X4_REGION_PRESENTATIONS[6] = {
  { "Where every outward voyage begins.", "The Harbor Lights", "Keep the home harbors supplied." },
  { "A chart is only half a promise.", "The Island Landings", "Carry ironware to equip the island landings." },
  { "A shore rebuilt from what the sea returns.", "The Castaway Workshops", "Carry ironware to equip the island workshops." },
  { "There are harbors beneath the surface.", "The Coral Exchange", "Carry tea to establish the distant exchange." },
  { "Here even the weather has an author.", "The Lantern Kilns", "Carry timber to supply the island kilns." },
  { "A wide sea, and a reason to come home.", "The Last Lights", "Carry ironware to equip the far harbors." },
};

static const X4PortPresentation X4_PORT_PRESENTATIONS[24] = {
  { "The Crooked Clock", "The clock leans. The textile stacks stand straight." },
  { "The Grain Wheel", "Grain sacks queue beside the old mill wheel." },
  { "The Log Slip", "Timber waits nose to tail below the tall storehouse." },
  { "The Tall Gantries", "The cranes reach high. The sea reaches farther." },
  { "The Spice Canopy", "A broad awning shelters a very fragrant market." },
  { "The Little Landing", "A narrow jetty welcomes shallow-draft ships." },
  { "The Apothecary Balcony", "Medicine cases leave by the shaded back stairs." },
  { "The Timber Racks", "Every spare corner seems to hold another log." },
  { "The Grain Tower", "The grain tower keeps watch over a generous quay." },
  { "The Stout Lighthouse", "A squat little light faces a great deal of ocean." },
  { "The Herb Terraces", "Medicine bundles dry above this small anchorage." },
  { "The Drying Frames", "Bolts of cloth stretch their legs by the water." },
  { "The Forge Chimney", "Iron tools and timber fill the waterside sheds." },
  { "The Tea Gantry", "Tea chests pass beneath the long, sheltering eaves." },
  { "The Low Pavilion", "A little tea pavilion overlooks a shallow landing." },
  { "The Covered Store", "Medicine cases wait beneath a splendidly big roof." },
  { "The Kiln Cluster", "Porcelain crates leave the kilns with great care." },
  { "The Cloth Bridges", "Cloth banners link the stepped waterfront sheds." },
  { "The Spice Courts", "Spice baskets take up almost every sunny corner." },
  { "The Iron Hoist", "The harbor hoist is proud of its heavy lifting." },
  { "The Clinic Jetty", "A tiny dispensary serves this faraway anchorage." },
  { "The Log Boom", "A long line of timber waits for a very long voyage." },
  { "The Twin Cranes", "Two cranes share a quay, and plenty of ironware." },
  { "The Signal Tower", "One cheerful tower marks a very distant landing." },
};

static const X4IncidentPresentation X4_INCIDENT_PRESENTATIONS[8] = {
  { "An Unwelcome Sail", {
    "A privateer turns toward your ship. Your captain awaits orders.",
    "That sail has followed every turn. The crew watches the captain." } },
  { "The Albatross", {
    "The horizon has disappeared behind a wall of weather.",
    "The rigging hums. The captain studies the approaching storm." } },
  { "The White Wake", {
    "The pilot points at the channel. Then at your keel.",
    "White water marks the reef. There is room, but not much." } },
  { "Fever Below Deck", {
    "The crew needs care. The ship still has a voyage to finish.",
    "A sailor leaves breakfast untouched. The captain calls for help." } },
  { "Sealed Orders", {
    "A customs officer arrives with a very tidy stack of forms.",
    "The inspector has brought a ledger. It is not a small ledger." } },
  { "A Word with the Captain", {
    "The crew has gathered on deck. They would like to be heard.",
    "The boatswain clears a throat. The rest of the crew waits." } },
  { "Someone Out There", {
    "A hand waves above a small boat. Your lookout calls again.",
    "There are survivors in the water beyond the floating cargo." } },
  { "A Floating Shop", {
    "A passing trader waves a sample with considerable enthusiasm.",
    "A merchant comes alongside. There is room for a small bargain." } },
};

static const char* const X4_STAGE_GUIDANCE[6] = {
  "Load a contract, plot its destination and dispatch your ship.",
  "Your first delivery opens the market. Try a small trade.",
  "A second ship needs a captain. Build the company at your pace.",
  "Two ships, more possibilities: fittings, stores and route plans.",
  "Support a regional project, buy its chart and open new harbors.",
  "Visit all six regions, then bring a company ship home to Lantern Quay.",
};

static const char* const X4_STAGE_CELEBRATIONS[6] = {
  "Your first voyage",
  "Trading established",
  "Company expansion",
  "Fleet operations",
  "Regional trade",
  "The Grand Charter",
};

static inline const X4RegionPresentation& x4_region(uint8_t id) {
  return X4_REGION_PRESENTATIONS[id < 6 ? id : 0];
}

static inline const X4PortPresentation& x4_port(uint8_t id) {
  return X4_PORT_PRESENTATIONS[id < 24 ? id : 0];
}

static inline const X4IncidentPresentation& x4_incident(uint8_t kind) {
  return X4_INCIDENT_PRESENTATIONS[kind < 8 ? kind : 0];
}

static inline const char* x4_incident_flavor(uint8_t kind, uint8_t variant) {
  return x4_incident(kind).flavor[variant & 1u];
}

static inline const char* x4_stage_guidance(uint8_t stage) {
  return X4_STAGE_GUIDANCE[stage < 6 ? stage : 5];
}

// Recruitment preview and the hired captain share immutable name + trait.
// This never touches the gameplay RNG or depends on roster slot/assignment.
static inline uint8_t x4_captain_portrait(const char* name, uint8_t trait) {
  return content_captain_portrait(name, trait);
}
