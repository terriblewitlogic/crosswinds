#pragma once
// Crosswinds: original encounters echoing maritime literature. Costs and rewards
// remain explicit in the shared quote; the journal holds the longer atmosphere.
static inline bool encounter_opportunity(uint8_t kind) { return kind >= INCIDENT_RESCUE; }
static inline int encounter_choices(uint8_t kind) { return kind == INCIDENT_TRADE ? 3 : (kind == INCIDENT_ILLNESS || kind == INCIDENT_INSPECTION || kind == INCIDENT_MUTINY) ? 1 : 2; }
static inline uint8_t encounter_fallback(uint8_t kind) {
  return encounter_opportunity(kind) ? (kind == INCIDENT_TRADE ? 2 : 1) : 0;
}
static inline uint8_t encounter_art(uint8_t kind) { return kind < INCIDENT_COUNT ? kind : 0; }
static inline const char* encounter_title(uint8_t kind) {
  static const char* const names[] = {"The torn chart","The albatross","The white wake","Lotus fever",
    "Sealed orders","The captain's oath","The castaway's parcel","The ivory token",
    "The other watch","The uncharted island","Captain Nobody","The last chart"};
  return kind < INCIDENT_COUNT ? names[kind] : "Encounter at sea";
}
static inline const char* encounter_report(uint8_t kind) {
  static const char* const reports[] = {"A chart marks spice crates under pirate guns.","An albatross circles cloth bales in the storm.",
    "A white whale passes tea crates on the reef.","A sweet island scent has left our crew feverish.","A cutter signals for our sealed papers.",
    "The crew asks us to abandon a captain's grudge.","A castaway asks us to carry a sealed parcel.","A trader with an ivory token offers cargo.",
    "Our ship sails beside us, with a different crew.","Grain waits on a shore absent from our charts.",
    "A boat surfaces. Its captain offers spices.","A one-legged sailor offers a chart to deliver."};
  return kind < INCIDENT_COUNT ? reports[kind] : "The captain reports an encounter.";
}
static inline const char* encounter_journal(uint8_t kind) {
  static const char* const notes[]={
    "The chart ends at a cross. Someone has erased the way home.",
    "The bird follows us. No hand is raised against it.",
    "We let the white shape pass. A merchant need not be a hunter.",
    "The crew remember the flowers, but not the island's name.",
    "The orders bear a seal from a port no one remembers visiting.",
    "A ship belongs to the living crew, not a captain's obsession.",
    "The parcel is dry. Everything else the castaway owns is soaked.",
    "A whale is carved on the token. Its eye has been rubbed smooth.",
    "Their watch bell rings a moment after ours. We do not answer.",
    "Tools lie beside the granary. Its benefactor has left no name.",
    "The captain calls himself Nobody. The sea closes over his boat.",
    "The chart has two halves. Our parcel carries only one."
  };
  return kind<INCIDENT_COUNT?notes[kind]:"The sea keeps its own account.";
}
static inline const char* encounter_auto_note(uint8_t kind) {
  static const char* const notes[]={"Passed the pirate guns","Sheltered with the albatross","Left the white wake","Crew recovered","Papers accepted","Heard the crew"};
  return kind<INCIDENT_RESCUE?notes[kind]:"Passed an offer";
}
