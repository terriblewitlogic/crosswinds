// The Grey Passenger: seven keepsakes carried home for lost sailors.
// The text lives in flash; the journal only records a short line per delivery.
#pragma once
#include <stdint.h>

static constexpr uint8_t STORY_CHAPTERS = 7;
static constexpr uint8_t STORY_NONE = 0xFF;
static constexpr uint8_t STORY_ALL_DELIVERED = (1u << STORY_CHAPTERS) - 1u;

struct StoryChapter {
  const char* keepsake;   // "The tin whistle"
  const char* item;       // "tin whistle", for the journal line
  const char* recipient;  // "Bet Harrow"
  uint8_t port;           // PORT_DEFS index
  const char* passage;
};

// Chapter 0 is always first and chapter 6 always last; 1-5 come in any order.
static const StoryChapter STORY_CHAPTER_DEFS[STORY_CHAPTERS] = {
  { "The tin whistle", "tin whistle", "Bet Harrow", 1,
    "Bet Harrow knows the whistle the moment she sees it. It belonged to her son, Sam, who played it on "
    "the harbour wall every evening until she begged him to stop. At eleven he went to sea as a cabin boy "
    "and took it with him. His ship never came back. \"Twenty years I've listened for that whistle,\" she says." },
  { "The brass compass", "brass compass", "Joseph Quill", 5,
    "This compass belonged to Joseph Quill's older brother, Daniel, a navigator lost at sea thirty years "
    "ago. It never pointed quite north. \"Daniel used to say it didn't point north, it pointed home, and "
    "home was wherever I was.\" Joseph sets it on the table. The needle swings, settles, and points "
    "straight at him." },
  { "The stopped watch", "stopped watch", "Ines Varga", 11,
    "Ines Varga's father made this watch for her husband, Tomas, on their wedding day. When Tomas went to "
    "sea, he promised to be home by the tenth of May. He never came. The watch stopped at ten past four. "
    "Ines winds it, and it starts ticking again. She sets it on the mantel, where she can hear it." },
  { "The overdue book", "overdue book", "the librarian", 13,
    "This book was borrowed from the Coral Library fifty-one years ago by Thomas Abernathy, a ship's "
    "surgeon who took it on his last voyage. The librarian finds his notes in the margins, right up to the "
    "final page. He read it to the very end. \"We'll put it back on the shelf just as it is,\" she says, "
    "and waives the late fee." },
  { "The unsent letter", "unsent letter", "Constance Hale", 17,
    "A young sailor named Will Tarrant wrote this letter to Constance Hale sixty years ago. His ship sank "
    "before he could post it. Constance is ninety-one now. The letter ends: \"If you'll have me, I'll be "
    "home by harvest, and I'll never sail again.\" She folds it carefully. \"I would have, Will,\" she "
    "says quietly." },
  { "The gansey", "gansey", "Ruth Kessock", 21,
    "In Tryworks, every family knits its own pattern into its sailors' sweaters, so that if the sea "
    "returns a man, his family will know him. Ruth Kessock knitted this one for her husband, Ewan. There's "
    "the dropped stitch she never fixed. Ewan was lost in a winter storm and never found. \"Now we can "
    "bury something of him,\" she says." },
  { "The toy boat", "toy boat", "Tobias Wren", 3,
    "When Tobias Wren was nine, he carved this little boat for his big sister, Ada, to take on her first "
    "voyage. Her ship, the Gannet, sank off Last Lantern that autumn. For sixty years he has kept a lamp "
    "lit in his window for her. He holds the boat and looks out at the empty quay. That night, he lets "
    "the lamp go out." },
};

// Appended to the delivery card by delivery count (0-based), not by chapter,
// so the courier's mystery always unfolds in the same order.
static const char* const STORY_THREAD_LINES[STORY_CHAPTERS - 1] = {
  "You ask about the woman in the grey coat who gave you the parcel. Nobody here has seen her.",
  "An old sailor remembers a woman in a grey coat from when he was a boy. He says she looked exactly the same then.",
  "You check the harbour records for the day she found you. No ship came in that day.",
  "Her coat is always soaking wet, even on dry days.",
  "Someone knows her name: Ada Wren, of the Gannet. But the Gannet sank sixty years ago.",
  "Ada Wren's name is carved on the memorial stone at Mariner Rest, with the rest of the Gannet's crew.",
};

static const char* const STORY_OFFER_LINES[3] = {
  "\"Will you carry something home for me?\"",
  "\"Someone has been waiting a long time for this.\"",
  "\"It isn't heavy. It pays well.\"",
};
static const char* const STORY_FINALE_OFFER = "\"This one's mine. Will you take me home?\"";

// Pay by delivery count, so it always rises whatever the middle order.
static const uint16_t STORY_REWARDS[STORY_CHAPTERS] = { 150, 200, 250, 300, 400, 500, 800 };

// Arrivals (fleet-wide, completed merchant voyages) before she approaches again.
static constexpr uint8_t STORY_COOLDOWN_START = 2;
static constexpr uint8_t STORY_COOLDOWN_DELIVERED = 3;
static constexpr uint8_t STORY_COOLDOWN_DECLINED = 5;

// Her name is known once thread line 5 ("Someone knows her name") has been read.
static inline bool story_name_known(uint8_t deliveries) { return deliveries >= 5; }
static inline const char* story_courier_name(uint8_t deliveries) {
  return story_name_known(deliveries) ? "Ada Wren" : "The Grey Passenger";
}
