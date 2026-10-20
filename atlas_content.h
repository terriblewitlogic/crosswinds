// The Sea Atlas: one page per port, charted on first arrival.
// Each entry holds the port's landmark name and the text of its page.
#pragma once
#include <stdint.h>

static constexpr uint8_t ATLAS_PORTS = 24;
static constexpr uint8_t ATLAS_HOME = 0xFE;  // charted by the company from the start
static constexpr uint16_t ATLAS_REGION_REWARD = 100;

struct AtlasEntry { const char* landmark; const char* text; };

static const AtlasEntry ATLAS_ENTRIES[ATLAS_PORTS] = {
  // Lantern Quay
  { "clock tower",
    "Every voyage in the company's books begins under this clock tower. Its two faces have never "
    "agreed: the harbour face runs four minutes fast, so sailors are never late on the tide. "
    "Merchants set their watches by the town face and complain about the harbour one." },
  // Gullhaven
  { "water mill",
    "The great wheel of the Gullhaven mill turns day and night, and most of the coast's flour comes "
    "from here. Children dare each other to walk the harbour wall in a gale. In the evenings, someone "
    "along the wall usually plays a tune for the boats coming home." },
  // Watchman Point
  { "timber warehouse",
    "Timber cut in the hills is floated down to this warehouse and stacked to the rafters. The point "
    "is named for the old watch kept here for returning ships. The watch house is long gone, but the "
    "custom of waving at every arrival never stopped." },
  // Mariner Rest
  { "dockside cranes",
    "Two tall wooden cranes load the ships at Mariner Rest, the deepest harbour on the coast. Above "
    "the harbour stands a memorial stone carrying the names of the town's lost crews. Fresh flowers "
    "appear at its foot every morning, and nobody admits to leaving them." },
  // Spyglass Bay
  { "spice pavilion",
    "Spice is weighed and sold under this open pavilion, in jars sealed with wax. The bay takes its "
    "name from the brass telescope on the customs house roof. The harbourmaster claims it can read a "
    "ship's name before her sails are up." },
  // Stockade Cay
  { "covered pier",
    "A covered pier runs out from what is left of an old log stockade. Nobody agrees on who built the "
    "stockade or who it kept out. Retired sailors sit in the shade of the pier, argue about it, and "
    "correct everyone's charts for free." },
  // Flint Reach
  { "merchant's house",
    "The finest house in Flint Reach belongs to whichever merchant had the best year. It has changed "
    "hands eleven times. Medicines from the inland valleys pass through here, and the reach is well "
    "known for driving a hard bargain over every crate." },
  // Skeleton Key
  { "storage shed",
    "The name frightens visitors more than the place does. Skeleton Key is a quiet island of storage "
    "sheds, cedar and spice. The \"skeleton\" is the bleached frame of an old ship on the beach, which "
    "the islanders have never let anyone take apart." },
  // Granite Haven
  { "tower house",
    "The tall tower house at Granite Haven was built by settlers who arrived with nothing and built "
    "everything by hand. Their workshop tools still hang inside. The town keeps a tradition of "
    "leaving a useful gift on the doorstep of every new arrival." },
  // Balloon Cape
  { "lighthouse",
    "The lighthouse on Balloon Cape was built from the wreck of a ship that ran aground on the point. "
    "Its keeper still uses the ship's bell as the fog signal. Some say the cape's name comes from a "
    "balloon that came down here long ago, with its passengers safe." },
  // Mercy Creek
  { "herb terraces",
    "Medicinal herbs grow on stepped terraces above Mercy Creek. The creek got its name from a fever "
    "ship that was taken in when every other port turned it away. Ships in trouble are still never "
    "charged a harbour fee here." },
  // Foundry Bay
  { "drying racks",
    "The foundry closed years ago, and Foundry Bay now makes cloth. Long racks of dyed fabric dry "
    "along the shore, so the bay changes colour with every batch. The town's watchmakers moved into "
    "the old foundry building and filled it with ticking." },
  // Nautilus Roads
  { "forge",
    "The forge chimney at Nautilus Roads never goes cold. Smiths here make anchors, chain and the "
    "iron fittings no other port can manage. Sailors talk of a strange vessel that comes and goes "
    "without sails, and the smiths will not say whose ironwork it carries." },
  // Coral Library
  { "book hoist",
    "Coral Library is named for its lending library, which lends books to any ship that docks. The "
    "hoist on the quay lifts crates of books aboard and brings returned ones ashore. Borrowers are "
    "trusted to come back, and the librarians keep every ledger since the first." },
  // Nobody Cove
  { "curved-roof house",
    "Ask who lives in the house above Nobody Cove, and you will be told \"nobody.\" The tea and spice "
    "it trades are always ready, priced and paid for. Yet no trader has ever seen the merchant in "
    "person. Letters left on the step are answered by morning." },
  // Pearl Anchorage
  { "trading pavilion",
    "Divers from Pearl Anchorage go down without stones or ropes, and stay down far longer than seems "
    "possible. Their pearls are sold in the pavilion by the water. Tea from the hills is traded there "
    "too, and the anchorage is calm in nearly any weather." },
  // Prospero Quay
  { "bottle kilns",
    "The three bottle kilns of Prospero Quay fire the finest porcelain in the sea. The kiln master is "
    "said to choose firing days by the weather, and the weather is said to go along with it. Visitors "
    "are welcome to watch, but asked not to whistle." },
  // Ariel Bay
  { "stilt houses",
    "The houses of Ariel Bay stand on stilts above the water, joined by lines of drying cloth. Fine "
    "textiles and porcelain are traded from the doorsteps. The wind here changes quickly, and the "
    "people of the bay seem to know which way before it does." },
  // Caliban Reach
  { "spice trays",
    "Spice dries in shallow trays under awnings all along Caliban Reach. Outsiders call the reach "
    "rough and unfriendly. The people who live here say outsiders arrived, took the best land, and "
    "called it theirs. Both sides still trade, and both drive a hard bargain." },
  // Tempest Watch
  { "ingot hoist",
    "Ironware and porcelain leave Tempest Watch on this heavy hoist. The headland gives a long view "
    "of the reach, and storms can be seen gathering hours away. Watchers here ring a bell when one is "
    "coming, and ships always listen." },
  // Whitewater Cay
  { "infirmary on stilts",
    "The infirmary at Whitewater Cay is the last place for medicine before the open ocean. Its "
    "doctors have treated every kind of injury the sea can cause. They keep a book of sailors who "
    "swore they would never sail again, and note how many came back." },
  // Tryworks
  { "timber boom",
    "The great tryworks that gave the town its name have been cold for years. Tryworks turned to "
    "timber instead, and logs now ride in long booms along the quay. Every family still knits its own "
    "pattern into its sailors' sweaters, as they always have." },
  // Ambergris Bay
  { "twin cranes",
    "Two tall cranes load the ships at Ambergris Bay, the busiest harbour on the White Horizon. The "
    "bay is named for a fortune once found floating on its tide. Every child on the shore still "
    "checks the waterline at dawn, just in case." },
  // Last Lantern
  { "watchtower",
    "Last Lantern is the farthest port on any chart. The great lighthouse that gave it its name went "
    "dark sixty years ago, the same autumn the Gannet was lost off the point. Now a watchman on this "
    "small tower raises a flag by day, and nothing at all shines by night." },
};
