#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

static constexpr uint32_t SIM_WORLD_MINUTES_PER_REAL_SECOND = 10u;

// Xteink X4: 4.26" 800x480 e-paper held in landscape. The four front buttons
// form a column on the right edge; a button column and their legend sit there.
static constexpr int IW = 800;
static constexpr int IH = 480;
static constexpr int SCALE = 1;
static constexpr int TOPBAR_H = 36;
static constexpr int ACTIONBAR_H = 0;
static constexpr int TICKER_H = 36;
static constexpr int PANEL_W = 224;  // button column and margins
static constexpr int SCREEN_W = IW * SCALE;
static constexpr int SCREEN_H = IH * SCALE;
static constexpr int VIEW_X = 0;
static constexpr int VIEW_Y = TOPBAR_H;
static constexpr int VIEW_W = IW - PANEL_W;
static constexpr int VIEW_H = IH - TOPBAR_H - TICKER_H - ACTIONBAR_H;
static constexpr int PANEL_X = VIEW_W;
static constexpr int TICKER_Y = IH - ACTIONBAR_H - TICKER_H;
static constexpr int ACTIONBAR_Y = IH - ACTIONBAR_H;

static constexpr int MAP_W = 216;
static constexpr int MAP_H = 108;
static constexpr int MAX_PORTS = 24;
static constexpr int MAX_LANES = 48;
static constexpr int MAX_SHIPS = 12;
static constexpr int MAX_CONTRACTS_PER_PORT = 10;
static constexpr int MAX_CARGO_ITEMS = 16;
static constexpr int MAX_WAREHOUSE_ITEMS = 12;
static constexpr int MAX_ROUTE_LEGS = 8;
static constexpr int MAX_INCIDENTS = 8;
static constexpr int MAX_JOURNAL = 48;
static constexpr int MAX_ATTENTION = 24;
static constexpr int MAX_CAPTAINS = 16;
static constexpr int GOOD_COUNT = 8;
static constexpr int REGION_COUNT = 6;
static constexpr int SHIP_CLASS_COUNT = 5;
static constexpr int MODULE_COUNT = 7;
static constexpr int MODULE_SHOP_COUNT = 6;  // fittings on the yard's page; the pennant is earned
static constexpr uint8_t NO_PORT = 0xFF;
static constexpr uint8_t NO_SHIP = 0xFF;
static constexpr uint8_t NO_LANE = 0xFF;

enum GoodId : uint8_t {
  GOOD_GRAIN, GOOD_TIMBER, GOOD_TEXTILES, GOOD_IRONWARE,
  GOOD_TEA, GOOD_SPICES, GOOD_MEDICINES, GOOD_PORCELAIN
};

enum PortClass : uint8_t { PORT_ANCHORAGE = 1, PORT_HARBOR = 2, PORT_DEEPWATER = 3 };
enum ShipState : uint8_t { SHIP_DOCKED, SHIP_SAILING, SHIP_AWAITING_DECISION,
                           SHIP_ARRIVED, SHIP_DISABLED };
enum JobKind : uint8_t { JOB_FREIGHT, JOB_MAIL, JOB_PASSENGER, JOB_CHARTER, JOB_PACKAGE, JOB_STORY };
enum CargoKind : uint8_t { CARGO_MARKET, CARGO_CONTRACT, CARGO_BOOKED };
enum SceneKind : uint8_t { SCENE_WORLD, SCENE_PORT, SCENE_COMPANY, SCENE_FLEET };
enum CardKind : uint8_t {
  CARD_NONE, CARD_JOBS, CARD_MANIFEST, CARD_ROUTE, CARD_MARKET,
  CARD_FLEET, CARD_LEDGER, CARD_INCIDENT, CARD_SHIPYARD,
  CARD_CHARTER, CARD_MENU, CARD_HELP, CARD_INBOX, CARD_EVENT,
  CARD_CAPTAINS, CARD_TUTORIAL, CARD_JOURNAL, CARD_MORE, CARD_ARRIVAL,
  CARD_PROGRESS, CARD_OUTCOME, CARD_VOYAGE, CARD_RETURN_DETAILS,
  CARD_VOYAGE_LOAD, CARD_VOYAGE_DEPARTURE, CARD_TRADE, CARD_PACKAGES, CARD_SHIP, CARD_PORT_PRICES, CARD_PORT_SHIPS, CARD_PORT_PICKER,
  CARD_KEEPSAKES, CARD_ATLAS
};
enum IncidentKind : uint8_t { INCIDENT_PIRATES, INCIDENT_STORM, INCIDENT_REEF,
                              INCIDENT_ILLNESS, INCIDENT_INSPECTION,
                              INCIDENT_MUTINY, INCIDENT_RESCUE, INCIDENT_TRADE,
                              INCIDENT_DOUBLE, INCIDENT_ISLAND, INCIDENT_NIGHT_MARKET, INCIDENT_STRANGER, INCIDENT_COUNT };
enum RiskBand : uint8_t { RISK_FAVORABLE, RISK_EVEN, RISK_DANGEROUS };
enum CaptainTrait : uint8_t { TRAIT_NAVIGATOR, TRAIT_GUNNER, TRAIT_MERCHANT,
                              TRAIT_SURGEON, TRAIT_QUARTERMASTER };
enum WorldEventKind : uint8_t { EVENT_NONE, EVENT_SHORTAGE, EVENT_GLUT,
                                EVENT_PRIVATEERS, EVENT_TRADE_WINDS };
enum ModuleId : uint8_t {
  MODULE_LONG_CASKS, MODULE_BULKHEADS, MODULE_BERTHS,
  MODULE_SWIVEL_GUNS, MODULE_COPPER, MODULE_STORM_RIG,
  MODULE_CHARTER_PENNANT  // the Grand Charter's reward
};

enum SimResultCode : uint8_t {
  SIM_OK, SIM_INVALID, SIM_NO_FUNDS, SIM_NO_CAPACITY, SIM_NO_BERTHS,
  SIM_BAD_SHIP_STATE, SIM_BAD_ROUTE, SIM_FACILITY_REQUIRED, SIM_RANK_REQUIRED,
  SIM_DEBT_UNAVAILABLE, SIM_CONTRACT_UNAVAILABLE, SIM_STALE_PREVIEW,
  SIM_CAPTAIN_REQUIRED, SIM_WRONG_PORT, SIM_FULL, SIM_PORT_LOCKED,
  SIM_LANE_UNAVAILABLE, SIM_DRAFT_LIMIT, SIM_ENDURANCE_LIMIT,
  SIM_PROJECT_INCOMPLETE, SIM_ALREADY_OWNED, SIM_WAREHOUSE_FULL,
  SIM_CARGO_UNAVAILABLE, SIM_CAPTAIN_UNAVAILABLE, SIM_TEMPLATE_REQUIRED,
  SIM_LOCKED, SIM_USE_ROCKER, SIM_LOW_BATTERY, SIM_NO_SHIP_HERE,
  SIM_SAVE_FAILED, SIM_RESULT_COUNT
};

// Staged unlocks. A company starts with the first voyage only; each stage
// opens one group of features once the player has a reason to want it. The
// stage is derived from campaign state and never goes backwards.
static constexpr int STAGE_COUNT = 6;
enum Feature : uint8_t {
  FEATURE_MARKET, FEATURE_CARGO, FEATURE_PASSENGERS, FEATURE_ZOOM,
  FEATURE_FLEET, FEATURE_YARD, FEATURE_LEDGER, FEATURE_CAPTAINS, FEATURE_INCIDENTS,
  FEATURE_ROUTE_RULES, FEATURE_FITTINGS, FEATURE_PORT_WORKS, FEATURE_WAREHOUSE,
  FEATURE_EVENTS, FEATURE_CHARTERS, FEATURE_CAPTAIN_RECORDS, FEATURE_COUNT
};
static_assert(FEATURE_COUNT <= 16, "legacy feature entitlement mask must fit");

struct SimResult {
  SimResultCode code;
  int32_t value;
  constexpr SimResult(SimResultCode c = SIM_OK, int32_t v = 0) : code(c), value(v) {}
  constexpr operator bool() const { return code == SIM_OK; }
};

enum AttentionKind : uint8_t {
  ATTENTION_INCIDENT, ATTENTION_ARRIVAL, ATTENTION_DISABLED,
  ATTENTION_CHARTER, ATTENTION_EVENT, ATTENTION_RECOVERY
};

enum RouteLoadMode : uint8_t { ROUTE_LOAD_NONE, ROUTE_LOAD_ENDPOINT };
enum RouteSellMode : uint8_t { ROUTE_SELL_NONE, ROUTE_SELL_PROFIT };
enum CaptainScar : uint8_t { SCAR_NONE, SCAR_STORM, SCAR_PIRATE, SCAR_REEF };
enum TutorialMilestone : uint8_t {
  TUTORIAL_PORT, TUTORIAL_CONTRACT, TUTORIAL_CARGO, TUTORIAL_ROUTE,
  TUTORIAL_DISPATCH, TUTORIAL_ARRIVAL, TUTORIAL_MARKET, TUTORIAL_FLEET,
  TUTORIAL_COUNT
};

struct GoodDef {
  const char* name;
  const char* shortName;
  uint16_t basePrice;
  uint8_t marginBonus;  // extra market points where the good is made or wanted
};

struct PortDef {
  const char* name;
  uint8_t x, y;
  uint8_t region;
  uint8_t portClass;
  uint8_t produced[2];
  uint8_t demanded[2];
  uint8_t duty;
  uint8_t specialty;
};

struct BalanceDef {
  uint16_t rankXp[6];
  uint16_t charterTarget[REGION_COUNT];
  uint16_t charterCost[REGION_COUNT];
  uint16_t chartCost[REGION_COUNT];
  uint8_t consolidationPercent;
  uint8_t consolidationBonusPercent;
  uint8_t debtWithholdPercent;
  uint8_t recoveryCondition;
  uint8_t laterProjectCredit;
  uint8_t maxCaptainLevel;
  uint8_t maxCaptainScars;
  uint8_t marketBuyPercent;
  uint8_t marketSellPercent;
  uint8_t marketMinPercent;
  uint8_t marketMaxPercent;
  uint8_t productionDiscountPoints;
  uint8_t demandPremiumPoints;
  uint8_t marketStockTarget;
  uint8_t marketStockPressurePerUnit;
  uint8_t marketStockPressureLimit;
  uint8_t specialtyStockPercent;
  uint8_t specialtyBuyPercent;
  uint8_t worldEventMarketPoints;
  uint8_t tradeWindBonus;
  uint8_t privateerHazardBonus;
  uint8_t lateRewardPercent;
  uint8_t captainXpPerLevel;
  uint8_t captainStartingLoyalty;
  uint8_t captainRecruitLoyalty;
  uint8_t captainLoyaltyNeutral;
  uint8_t captainLoyaltyStep;
  uint8_t captainLoyaltyMaxModifier;
  uint8_t captainScarModifier;
  uint8_t captainScarConditionThreshold;
  uint8_t incidentFavorableBonus;
  uint8_t incidentDangerousPenalty;
  uint8_t repairCostPerPoint;
  uint8_t recoveryDepartureReserve;
  uint16_t captainRecruitBaseCost;
  uint16_t captainRecruitLevelCost;
  uint8_t berthingPerDay;    // daily charge for a ship left in harbour
  uint8_t berthingPerClass;  // added per ship class above the sloop
};

struct AttentionEntry {
  uint32_t id;
  uint32_t worldMinute;
  uint8_t kind;
  uint8_t shipIndex;
  uint8_t port;
  uint8_t urgent;
  uint8_t unread;
  uint8_t resolved;
  uint8_t region;  // charter notices: the region, else 0xFF (v7)
};

struct RouteTemplate {
  uint8_t origin;
  uint8_t stops[MAX_ROUTE_LEGS];
  uint8_t stopCount;
  uint8_t loadMode;
  uint8_t sellMode;
  uint8_t buyGood;
  uint16_t maxBuyPrice;
};

struct CaptainProfile {
  char name[14];
  uint16_t xp;
  uint16_t hireCost;
  uint8_t trait;
  uint8_t level;
  uint8_t loyalty;
  uint8_t scars[2];
  uint8_t scarCount;
  uint8_t assignedShip;
  uint8_t currentPort;
  uint8_t active;
  uint16_t voyages;
  uint16_t deliveries;
  uint16_t rescues;
  uint16_t incidentsSurvived;
  uint8_t portrait;  // index into the captain portraits; stored from save v13
};

struct SeaLaneDef {
  uint8_t a, b;
  uint8_t days;
  int8_t windAB;
  uint8_t hazard;
  int8_t seasonAB;
};

struct ShipClassDef {
  const char* name;
  uint8_t hold;
  uint8_t berths;
  uint8_t speed;
  uint8_t enduranceDays;
  uint8_t draft;
  uint8_t guns;
  uint8_t storm;
  uint8_t rank;
  uint16_t cost;
};

struct ModuleDef {
  const char* name;
  const char* effect;
  uint16_t cost;
};

struct CargoItem {
  uint32_t contractId;  // contract ID, or market stack cost remainder (v7 layout unchanged)
  uint16_t paidUnitPrice;
  uint8_t good;
  uint8_t quantity;
  uint8_t destination;
  uint8_t kind;
  uint8_t jobKind;
  uint8_t projectRegion;
  uint8_t sourcePort;    // exact lot provenance; NO_PORT for pre-v8 purchases
  uint8_t sailed;        // physically carried away from source; never set by transfer
};

enum VoyageMode : uint8_t { VOYAGE_FREIGHT, VOYAGE_TRADE, VOYAGE_RELIEF };
enum BookingState : uint8_t { BOOKING_NONE, BOOKING_SAILING, BOOKING_SETTLED };

// A signed order remains with its ship as an arrival receipt until the next
// departure. Financial amounts are locked at dispatch and settle with the
// whole company in one save transaction.
struct BookedVoyage {
  uint32_t id;
  int32_t cargoCost;
  int32_t sailingCost;
  int32_t buyerPayment;
  int32_t bonus;
  int32_t payout;
  int32_t withheld;
  int32_t payNow;
  int32_t arrivalCoins;
  int32_t profit;
  uint16_t legMinutes[MAX_ROUTE_LEGS];
  uint8_t state;
  uint8_t origin;
  uint8_t destination;
  uint8_t good;
  uint8_t quantity;
  uint8_t settledQuantity;
  uint8_t mode;
  uint8_t projectRegion;
  uint8_t receiptUnread;
  uint8_t quantities[GOOD_COUNT];
  uint8_t projectQuantities[GOOD_COUNT];  // earmarked subset, never resold
  int32_t grossPayment;
  int32_t duty;
  uint8_t priceVersion;  // 0: retained pre-v10 terms without a duty breakdown
};

struct VoyageQuote {
  uint32_t fingerprint;
  uint32_t minutes;       // world minutes; /600 gives real minutes at normal speed
  int32_t cargoCost;
  int32_t sailingCost;
  int32_t buyerPayment;
  int32_t bonus;
  int32_t payout;
  int32_t withheld;
  int32_t payNow;
  int32_t arrivalCoins;
  int32_t profit;
  uint16_t legMinutes[MAX_ROUTE_LEGS];
  uint16_t delayMinutes;  // disclosed maximum additional world minutes
  SimResultCode code;
  uint8_t shipIndex;
  uint8_t origin;
  uint8_t destination;
  uint8_t good;
  uint8_t quantity;
  uint8_t availableQuantity;
  uint8_t capacity;
  uint8_t mode;
  uint8_t projectRegion;
  uint8_t routeCount;
  uint8_t route[MAX_ROUTE_LEGS];
  uint8_t planningOnly;   // a possible return after arrival; never dispatchable
  uint8_t quantities[GOOD_COUNT];        // exact requested manifest; never auto-filled
  uint8_t projectQuantities[GOOD_COUNT]; // subset earmarked for projectRegion
  uint8_t availableQuantities[GOOD_COUNT]; // currently bookable, each capped at hold size
  uint8_t offeredGoods;                 // bit per compatible good, even when buyer is full
  uint8_t projectGoods;                 // compatible goods currently earmarked for the dock
  uint16_t unitCargoCosts[GOOD_COUNT];
  uint16_t unitBuyerPayments[GOOD_COUNT]; // after duty; aggregate quote is authoritative
  uint16_t unitGrossPayments[GOOD_COUNT]; // before destination duty; aggregate rounding applies
  int32_t grossPayment;
  int32_t duty;
  uint8_t dutyPercent;
  uint8_t priceVersion;
};

enum MerchantState : uint8_t { MERCHANT_NONE, MERCHANT_SAILING, MERCHANT_ARRIVED };
// Transit and its last receipt are independent of retained signed bookings.
// Owned market cargo is never consumed by arrival.
struct MerchantVoyage {
  uint32_t id;
  uint16_t legMinutes[MAX_ROUTE_LEGS];
  int32_t packageCoins;
  uint16_t packagesDelivered;
  uint8_t state, origin, destination, receiptUnread;
};

struct MerchantTradeQuote {
  uint32_t fingerprint;
  int32_t total, cashDelta, profit, costBasis, withholding;
  SimResultCode code;
  uint8_t shipIndex, port, buying, estimateOnly, quantity;
  uint8_t quantities[GOOD_COUNT];
  uint8_t availableQuantities[GOOD_COUNT];
  uint16_t unitPrices[GOOD_COUNT];
};

struct MerchantSailQuote {
  uint32_t fingerprint, minutes;
  int32_t saleEstimate, costBasis, packagePayout;
  SimResultCode code;
  uint16_t legMinutes[MAX_ROUTE_LEGS];
  uint8_t shipIndex, origin, destination, routeCount;
  uint8_t route[MAX_ROUTE_LEGS];
};

struct PackageOffer {
  uint32_t id, fingerprint, minutes;
  uint16_t reward;
  SimResultCode code;
  uint8_t shipIndex, origin, destination;
};

struct Contract {
  uint32_t id;
  uint32_t deadlineMinute;
  uint16_t reward;
  uint8_t origin;
  uint8_t destination;
  uint8_t good;
  uint8_t quantity;
  uint8_t kind;
  uint8_t projectRegion;
  uint8_t active;
};

// Cumulative per-ship results and their independently acknowledged watermark.
// Individual booking-receipt visibility is deliberately separate.
struct ReturnTally {
  uint32_t revision;
  uint32_t bookedArrivals, legacyArrivals;
  uint32_t bookedCargo, legacyCargo;
  int64_t bookedCoins, bookedProfit, legacyCoins;
};
struct ReturnLedger { ReturnTally total, seen; };

enum ReturnMilestone : uint32_t {
  RETURN_MILESTONE_TRADE = 1u << 0,
  RETURN_MILESTONE_NEW_ALBION = 1u << 1,
  RETURN_MILESTONE_SECOND_SHIP = 1u << 2,
  RETURN_MILESTONE_FITTINGS = 1u << 3,
  RETURN_MILESTONE_PROJECTS = 1u << 4,
  RETURN_MILESTONE_REGION_BASE = 1u << 8,       // shift by region index
  RETURN_MILESTONE_PROJECT_READY_BASE = 1u << 16, // shift by region index
  RETURN_MILESTONE_GRAND_CHARTER = 1u << 24
};
struct ReturnRow {
  ReturnTally mark; // exact frozen cumulative watermark, never recomputed on ack
  uint32_t shipId, bookingId;
  uint32_t bookedArrivals, legacyArrivals, deliveredUnits;
  int64_t bookedCoins, bookedProfit, legacyCoins;
  uint8_t shipIndex, state, port, destination;
};
struct ReturnNoticeMark {
  uint32_t id, worldMinute;
  uint8_t shipIndex, kind, port, region;
};
struct ReturnReport {
  ReturnRow rows[MAX_SHIPS];
  ReturnNoticeMark notices[MAX_ATTENTION];
  uint32_t companySeed, companyEpoch, fingerprint;
  uint32_t bookedArrivals, legacyArrivals, deliveredUnits, milestoneMask;
  int64_t bookedCoins, bookedProfit, legacyCoins;
  uint16_t eventShipMask, decisionShipMask, repairShipMask, readyShipMask;
  uint8_t rowCount, noticeCount, legacyNoticeCount;
  uint8_t readyCount, sailingCount, decisionCount, repairCount, preferredShip, meaningful;
  char milestone[96]; // highest-priority newly earned milestone, or empty
};

struct Ship {
  uint32_t id;
  char name[16];
  uint8_t captainId;
  uint8_t classId;
  uint8_t state;
  uint8_t currentPort;
  uint8_t fromPort;
  uint8_t toPort;
  uint8_t route[MAX_ROUTE_LEGS];
  uint8_t routeCount;
  uint8_t routePos;
  RouteTemplate routeTemplate;
  uint8_t cargoCount;
  uint8_t condition;
  uint8_t morale;
  uint8_t crew;
  uint8_t modules[2];
  uint8_t incidentType;
  uint8_t incidentArmed;
  uint16_t encounterWait; // remaining response window in world minutes, v12
  uint8_t autoEncounter; // kind + 1 of most recent automatic response
  uint16_t autoDelay;
  uint8_t lastEncounter; // kind + 1, for immediate-repeat protection
  uint8_t sighting; // transient voyage marvel, persisted for consistent wake

  uint8_t manifestBonus;
  uint16_t legElapsedMinutes;
  uint16_t legTotalMinutes;
  uint16_t prepaidCost;
  int32_t voyageEarned;      // arrival report totals across the voyage's legs (v7)
  uint16_t voyageDelivered;
  BookedVoyage booking;
  MerchantVoyage merchant;
  ReturnLedger returnLedger;
  CargoItem cargo[MAX_CARGO_ITEMS];
};

struct PortState {
  uint8_t unlocked;
  uint8_t officeLevel;
  uint8_t warehouseLevel;
  uint8_t shipyardLevel;
  uint8_t warehouseCount;
  uint16_t stock[GOOD_COUNT];
  uint8_t priceHistory[GOOD_COUNT][7];
  uint8_t historyHead;
  uint32_t bookingEpoch;
  uint16_t bookingUsed[GOOD_COUNT];
  uint32_t packageEpoch; // finite package board changes only at elapsed epochs
  CargoItem warehouse[MAX_WAREHOUSE_ITEMS];
};

struct Campaign {
  uint32_t seed;
  uint32_t rng;
  uint32_t worldMinutes;
  uint32_t nextContractId;
  uint32_t nextShipId;
  int32_t coins;
  int32_t recoveryDebt;
  uint32_t nextAttentionId;
  uint32_t recruitmentWeek;
  uint16_t companyXp;
  uint16_t deliveries;
  uint16_t contractsCompleted;
  uint16_t incidentsResolved;
  uint16_t charterProgress[REGION_COUNT];
  uint32_t contractRevenue;
  int32_t marketProfit;
  uint32_t operatingCosts;
  int32_t capitalSpending;  // ships, fittings, port works, repairs, charts, charters, captains (v7)
  int32_t incidentNet;      // coins gained or lost in incidents, signed (v7)
  uint8_t rank;
  uint8_t regionMask;
  uint8_t chartMask;
  uint8_t visitedMask;
  uint8_t shipCount;
  uint8_t won;
  uint8_t paused;
  uint8_t soundOn;
  uint8_t timeScale;
  uint8_t worldEventType;
  uint8_t worldEventGood;
  uint8_t worldEventRegion;
  uint8_t worldEventDays;
  uint16_t lastMarketDay;
  uint8_t tutorialStep;
  uint8_t tutorialFlags;
  uint8_t tutorialDismissed;
  uint8_t onboardingStep;  // OnboardingStep; ONBOARD_DONE for companies past (or before) onboarding
  uint8_t captainCount;
  uint8_t stageSeen;  // highest stage reached; unlock notices fire once
  uint8_t economyVersion;  // 1: booked voyages; 0 retained for legacy test scenarios
  uint32_t nextVoyageId;
  uint32_t deliveredUnits;
  uint32_t completedVoyages;
  uint16_t projectCargo[REGION_COUNT][2];  // Timber, Ironware, physically delivered
  uint16_t projectWorksCredit[REGION_COUNT];  // preserved value of paid legacy charts
  uint16_t legacyFeatureMask;  // already earned tools, independent of new tutorial stages
  uint32_t standbySessionId;
  uint32_t standbyAccountedRealSeconds;
  uint32_t returnMilestones, returnMilestonesSeen;
  // The Grey Passenger (save format 14). Chapters are 0-6;
  // STORY_NONE (0xFF) marks an empty chapter field.
  uint8_t storyDelivered;       // bit per delivered chapter
  uint8_t storyDeliveries;      // count; drives pay and the courier's thread line
  uint8_t storyOrder[7];        // chapter at each delivery position
  uint8_t storyCarrying;        // chapter aboard some ship
  uint32_t storyOfferShipId;    // ship holding her live offer, 0 for none
  uint8_t storyOfferPort, storyOfferChapter;
  uint8_t storyLastPort;        // where she last approached; she won't repeat it next
  uint8_t storyCooldown;        // completed merchant arrivals before she may approach
  uint8_t storyCardPending;     // delivered chapter whose card the player hasn't seen
  uint8_t storyOffers;          // offers made; rotates her opening line
};

struct Incident {
  uint8_t active;
  uint8_t shipIndex;
  uint8_t kind;
  uint8_t risk;
};

// A read-only preview of successive immediate trades. Prices include duty;
// total is gross spend/proceeds, treasuryDelta is signed net cash, and
// withholding is debt repaid. stopReason is OK when all requested units fit.
struct MarketQuote {
  int32_t total;
  int32_t treasuryDelta;
  int32_t withholding;
  uint8_t quantity;
  SimResultCode stopReason;
};

static constexpr uint8_t MERCHANT_TUTORIAL_PAGES = 6;

// The X4 first voyage: choose a captain, buy Textiles at Lantern Quay, sail to
// Gullhaven and sell them. Steps after NAME are re-derived from the ship's
// hold and position (sim_onboarding_update), so wandering off never strands
// the player. Zero is "done", so saves from before v13 never onboard.
enum OnboardingStep : uint8_t {
  ONBOARD_DONE, ONBOARD_CAPTAIN, ONBOARD_NAME, ONBOARD_BUY, ONBOARD_SAIL,
  ONBOARD_VOYAGE, ONBOARD_SELL, ONBOARD_STEP_COUNT
};
static constexpr uint8_t ONBOARD_PORT_FROM = 0, ONBOARD_PORT_TO = 1;  // Lantern Quay -> Gullhaven

struct MerchantIncidentChoice {
  const char* title;
  SimResultCode code;
  int16_t coins;
  uint16_t delayMinutes;
  uint8_t riskPercent, good;
  uint8_t gainGood, giveGood, parcel, gainQuantity;
  const char* benefit;
};
MerchantIncidentChoice sim_merchant_incident_choice(uint8_t option);
SimResult sim_merchant_incident_resolve(uint8_t option, bool automatic = false);
const char* sim_merchant_outcome_action();
bool sim_select_incident(uint8_t ship);

struct IncidentCargoChange {
  int16_t quantityDelta;
  uint8_t kind;
  uint8_t jobKind;
  uint8_t good;
};

// Just-completed decision, independent of the next queued incident. This is
// transient presentation state, never serialized or reconstructed from history.
struct IncidentOutcome {
  uint8_t merchant;  // transient report: use merchant choice wording
  uint32_t serial;
  uint32_t worldMinute;
  int32_t treasuryDelta;
  int32_t delayMinutes;  // signed change in remaining leg time; hide if disabled
  int16_t hullDelta;     // final hull minus initial hull; disabled recovery may raise it
  int16_t moraleDelta;
  int16_t crewDelta;
  int16_t captainLoyaltyDelta;
  uint16_t captainXpDelta;
  uint8_t valid;
  uint8_t shipIndex;
  uint8_t classId;
  uint8_t kind;
  uint8_t option;
  uint8_t risk;
  uint8_t disabled;
  uint8_t hullAfter;
  uint8_t moraleAfter;
  uint8_t crewAfter;
  uint8_t captainId;
  uint8_t captainTrait;
  uint8_t captainLevelBefore;
  uint8_t captainLevelAfter;
  uint8_t captainLoyaltyAfter;
  uint8_t captainNewScarMask;  // bits indexed by CaptainScar; excludes SCAR_NONE
  uint8_t cargoCount;
  char shipName[16];
  char captainName[14];
  IncidentCargoChange cargo[MAX_CARGO_ITEMS];
};

struct JournalEntry {
  uint32_t worldMinute;
  uint8_t severity;
  char text[64];
};

struct UiState {
  uint8_t scene;
  uint8_t card;
  uint8_t selectedPort;
  uint8_t selectedShip;
  uint8_t selectedGood;
  uint8_t page;
  uint8_t routeDraft[MAX_ROUTE_LEGS];
  uint8_t routeCount;
  uint8_t dirty;
  uint8_t title;
  uint8_t confirmReset;
  uint8_t attentionUnreadOnly;
  uint8_t selectedAttention;
  uint8_t routePrepArmed;
  uint8_t mapZoom;
  uint8_t mapFocusX;
  uint8_t mapFocusY;
  uint8_t captainView;
  uint8_t selectedCaptain;
  SimResultCode noticeCode;
  uint32_t noticeUntilMs;
  // Transient arrival summary; intentionally excluded from save chunks.
  uint8_t arrivalSummaryVisible;
  uint8_t arrivalSummaryShip;
  uint8_t arrivalSummaryPort;
  uint8_t arrivalSummaryDelivered;
  int32_t arrivalSummaryCoins;
  uint8_t arrivalSummaryWon;  // the arrival that completed the Grand Charter
  uint8_t returnQuiet;        // transient: group idle/boot arrivals without modal interruption
  uint8_t routeScroll;        // first destination row shown on the route card
};

struct RoutePreparation {
  uint32_t fingerprint;
  int32_t projectedCoins;
  uint16_t sales;
  uint16_t purchases;
  uint16_t operatingCost;
  uint16_t etaDays;
  uint8_t risk;
  uint8_t holdUsed;
  uint8_t holdCapacity;
  uint8_t berthsUsed;
  uint8_t berthCapacity;
  uint8_t computed;
  uint8_t valid;
  uint8_t reversed;
  uint8_t loadedContracts;
  uint8_t boughtUnits;
  uint8_t soldUnits;
};

struct RouteEstimate {
  uint8_t valid;
  uint8_t risk;
  uint16_t days;
  uint16_t realSeconds;
  uint16_t cost;
  uint32_t revenue;
  int32_t profit;
  uint32_t netRevenue;  // after recovery-debt withholding, as delivery pays it
  int32_t netProfit;
  SimResultCode code;
  const char* reason;
};

extern const GoodDef GOOD_DEFS[GOOD_COUNT];
extern const PortDef PORT_DEFS[MAX_PORTS];
extern const SeaLaneDef LANE_DEFS[MAX_LANES];
extern const int LANE_COUNT;
extern const ShipClassDef SHIP_CLASS_DEFS[SHIP_CLASS_COUNT];
extern const ModuleDef MODULE_DEFS[MODULE_COUNT];
extern const char* const REGION_NAMES[REGION_COUNT];
extern const BalanceDef BALANCE;

extern Campaign g_campaign;
// The Sea Atlas (sim_atlas.h, save v15): kept apart from Campaign so the
// merchant undo snapshot on the stack does not grow. Ships are never removed,
// so a ship slot identifies who charted a port.
struct AtlasState {
  uint32_t charted;           // bit per port
  uint32_t voyage[24];        // merchant voyage id that charted it (0: before the Atlas)
  uint16_t day[24];           // day charted, from 1
  uint8_t ship[24];           // ship slot, or ATLAS_HOME
  uint8_t regionsRewarded;    // bit per region paid for
  uint8_t migrate;            // not saved: a pre-Atlas save awaits sim_atlas_migrate
};
extern AtlasState g_atlas;
extern PortState g_ports[MAX_PORTS];
extern Contract g_contracts[MAX_PORTS][MAX_CONTRACTS_PER_PORT];
extern Ship g_ships[MAX_SHIPS];
extern Incident g_incident;
extern IncidentOutcome g_incidentOutcome;
extern Incident g_incidentQueue[MAX_INCIDENTS];
extern uint8_t g_incidentQueueCount;
extern uint8_t g_journalHead;
extern uint8_t g_journalCount;
extern uint8_t g_attentionCount;
extern RoutePreparation g_routePreparation;
extern UiState g_ui;

extern JournalEntry g_journal[MAX_JOURNAL];
extern AttentionEntry g_attention[MAX_ATTENTION];
extern CaptainProfile g_captains[MAX_CAPTAINS];

uint32_t rng_next();
uint32_t rng_range(uint32_t n);
int content_lane_between(uint8_t a, uint8_t b);
int content_hops(uint8_t from, uint8_t to, bool unlockedOnly);
uint16_t content_market_price(uint8_t port, uint8_t good, bool buying);
uint16_t content_market_price_at_stock(uint8_t port, uint8_t good, bool buying, uint16_t stock);
const char* content_rank_name(uint8_t rank);
// Name of what a cargo item carries: the good, or Mail / Passengers for those jobs.
const char* content_cargo_name(uint8_t cargoKind, uint8_t jobKind, uint8_t good, bool shortName);
const char* content_trait_name(uint8_t trait);
// The portrait a captain is first given, from name and trait (saves before v13).
uint8_t content_captain_portrait(const char* name, uint8_t trait);

void sim_new_campaign(uint32_t seed);
VoyageQuote sim_voyage_offer(uint8_t shipIndex, uint8_t destination, uint8_t mode,
                           uint8_t good, uint8_t quantity);
// All eight quantities are explicit; an empty manifest returns offer metadata
// but cannot sail. Every booked cargo unit occupies one existing hold slot.
VoyageQuote sim_voyage_manifest_offer(uint8_t shipIndex, uint8_t destination,
                                     uint8_t mode, const uint8_t* quantities);
VoyageQuote sim_return_voyage_manifest_offer(uint8_t shipIndex, uint8_t destination,
                                            uint8_t mode, const uint8_t* quantities);
uint32_t sim_voyage_next_refresh_minute(); // elapsed world-minute boundary, never an expiry
SimResult sim_voyage_dispatch(uint8_t shipIndex, const VoyageQuote& quote);
uint8_t sim_voyage_destinations(uint8_t shipIndex, uint8_t* out, uint8_t capacity);
uint8_t sim_voyage_recommend_good(uint8_t origin, uint8_t destination);
VoyageQuote sim_repeat_voyage_quote(uint8_t shipIndex);
VoyageQuote sim_return_voyage_quote(uint8_t shipIndex, uint8_t destination,
                                  uint8_t mode, uint8_t good, uint8_t quantity);
uint8_t sim_project_region();
uint16_t sim_project_target(uint8_t region);
uint16_t sim_project_cost(uint8_t region);
uint8_t sim_project_port(uint8_t region);
SimResult sim_project_contribute(uint8_t region);
void sim_after_load();
bool sim_tutorial_complete(uint8_t milestone);
void sim_tutorial_reconcile();
bool sim_tutorial_guided();
uint8_t sim_stage();
bool sim_feature_unlocked(uint8_t feature);
uint8_t sim_feature_stage(uint8_t feature);
void sim_stage_reconcile(bool announce);
// Clear state the current stage hides (an older save can carry it): a world
// event, incidents, charter offers and their notices.
void sim_scrub_locked_state();
const char* sim_stage_name(uint8_t stage);
const char* sim_stage_unlocks(uint8_t stage);
const char* sim_stage_condition(uint8_t stage);
// Live progress toward a stage's condition, e.g. "14 of 25 XP"; empty when none applies.
void sim_stage_progress(uint8_t stage, char* out, size_t cap);
void sim_advance_minutes(uint32_t minutes);
void sim_refresh_day();
void sim_generate_jobs(uint8_t port, bool force);
SimResult sim_load_contract(uint8_t shipIndex, uint8_t port, uint8_t slot);
SimResult sim_market_buy(uint8_t shipIndex, uint8_t good);
SimResult sim_market_sell(uint8_t shipIndex, uint8_t good);
MarketQuote sim_market_quote(uint8_t shipIndex, uint8_t good, bool buying, uint8_t requested = 1);
MerchantTradeQuote sim_merchant_trade_offer(uint8_t shipIndex, bool buying, const uint8_t* quantities);
SimResult sim_merchant_trade_apply(uint8_t shipIndex, const MerchantTradeQuote& quote);
MerchantTradeQuote sim_merchant_sale_estimate(uint8_t shipIndex, uint8_t destination);
MerchantSailQuote sim_merchant_sail_offer(uint8_t shipIndex, uint8_t destination);
SimResult sim_merchant_sail(uint8_t shipIndex, const MerchantSailQuote& quote);
uint8_t sim_merchant_destinations(uint8_t shipIndex, uint8_t* out, uint8_t capacity);
uint8_t sim_package_count(uint8_t shipIndex);
PackageOffer sim_package_offer(uint8_t shipIndex, uint8_t row);
SimResult sim_package_accept(uint8_t shipIndex, const PackageOffer& offer);
// The Grey Passenger's sealed parcel (sim_story.h). A live offer belongs to
// one docked ship; code is SIM_OK or SIM_NO_CAPACITY when it is listed.
struct StoryOffer { uint8_t shipIndex, chapter, line; uint16_t reward; SimResultCode code; };
bool sim_story_offer(uint8_t shipIndex, StoryOffer& out);
SimResult sim_story_accept(uint8_t shipIndex);
uint8_t sim_story_card_pending();         // chapter, or STORY_NONE
void sim_story_ack_card();
int sim_story_carrier();                  // ship index carrying a keepsake, or -1
uint16_t sim_story_reward();              // pay for the next delivery
bool sim_atlas_charted(uint8_t port);
uint8_t sim_atlas_count();
uint8_t sim_atlas_region_count(uint8_t region);
bool sim_atlas_complete();
void sim_atlas_migrate();
uint8_t sim_atlas_voyage_pages(uint8_t shipIndex, uint8_t* ports, uint8_t cap);
#if defined(SH_HOST_TESTS)
void sim_test_story(bool enabled);        // scripted suites keep her away
#endif
SimResult sim_merchant_donate(uint8_t shipIndex, uint8_t good, uint8_t quantity);
SimResult sim_transfer_to_warehouse(uint8_t shipIndex, uint8_t cargoIndex);
SimResult sim_transfer_from_warehouse(uint8_t shipIndex, uint8_t warehouseIndex);
SimResult sim_route_add(uint8_t port);
SimResult sim_route_can_add(uint8_t port);  // exact append validation without changing the draft
void sim_route_clear();
void sim_route_undo();  // no-op for an empty draft; preserves the saved route template
SimResult sim_route_recall();
SimResult sim_route_template_save(uint8_t shipIndex);
SimResult sim_route_prepare_preview(uint8_t shipIndex);
SimResult sim_route_prepare_apply(uint8_t shipIndex);
void sim_route_template_clear(uint8_t shipIndex);
RouteEstimate sim_estimate_route(uint8_t shipIndex, const uint8_t* route, uint8_t count);
// Days this ship needs for one lane from a port: wind-adjusted, capped like the voyage clock.
int sim_leg_days(uint8_t shipIndex, int lane, uint8_t from);
int sim_lane_wind(uint8_t laneIndex, uint8_t fromPort);
int sim_lane_hazard(uint8_t laneIndex);
SimResult sim_dispatch(uint8_t shipIndex);
void sim_resolve_incident(uint8_t option);
// Coins an incident option is certain to cost (negative) or pay (positive).
// False when the outcome depends on the roll or on cargo.
bool sim_incident_option_cost(uint8_t option, int* coins);
SimResult sim_buy_ship(uint8_t classId);
// Why a hull cannot be bought at the selected port right now, or SIM_OK.
SimResultCode sim_ship_purchase_block(uint8_t classId);
SimResult sim_buy_port_upgrade(uint8_t kind);
uint8_t sim_port_upgrade_max_level(uint8_t kind);  // the highest level the works will sell
SimResult sim_buy_module(uint8_t moduleId);
SimResult sim_repair_ship(uint8_t shipIndex);
SimResult sim_claim_recovery();
VoyageQuote sim_recovery_quote();
SimResult sim_claim_recovery(const VoyageQuote& quote);
SimResult sim_replace_module(uint8_t shipIndex, uint8_t slot, uint8_t moduleId);
uint16_t sim_ship_package_cost(uint8_t classId);
bool sim_recovery_available();  // would sim_claim_recovery succeed right now
SimResult sim_purchase_chart(uint8_t region);
SimResult sim_purchase_charter(uint8_t region);
SimResult sim_assign_captain(uint8_t captainId, uint8_t shipIndex);
SimResult sim_hire_captain(uint8_t candidate);
bool sim_recruit_candidate(uint8_t port, uint8_t candidate, CaptainProfile* out);
CaptainProfile* sim_ship_captain(uint8_t shipIndex);
const char* sim_result_text(SimResultCode code);
void sim_set_notice(SimResult result);
// region tells CHARTER READY notices for different regions apart; 0xFF otherwise.
void attention_add(uint8_t kind, uint8_t shipIndex, uint8_t port, bool urgent, uint8_t region = 0xFF);
void attention_select(uint8_t index);
bool attention_has_arrival_report(uint8_t index);
void attention_clear_read();
ReturnReport sim_return_report();
// X4 first voyage (sim_onboarding.h).
bool sim_onboarding_active();
uint8_t sim_onboarding_step();            // ONBOARD_DONE when not onboarding
bool sim_onboarding_candidate(uint8_t index, CaptainProfile* out);  // 0..2
SimResult sim_onboarding_hire(uint8_t index);
const char* sim_onboarding_name(uint8_t index);
uint8_t sim_onboarding_name_count();
SimResult sim_onboarding_rename(uint8_t nameIndex);
SimResult sim_onboarding_confirm_name();
SimResult sim_onboarding_skip(uint8_t candidate);
bool sim_onboarding_update();             // true once, when the first voyage completes
bool sim_onboarding_allows_destination(uint8_t shipIndex, uint8_t port);
uint32_t sim_return_epoch(); // transient identity changes on reset and coherent reload
bool sim_return_has_updates();
SimResult sim_ack_return_report(const ReturnReport& report);
SimResult sim_ack_arrival_receipt(uint8_t shipIndex, uint32_t bookingId);
void sim_return_migrate_legacy(); // save loader only: seed trustworthy pre-v9 unread receipts
void attention_resolve(uint8_t kind, uint8_t shipIndex);  // settle a ship's notices of one kind
uint8_t attention_unread_count();
int attention_visible_count();  // rows shown under the current unread filter
uint8_t attention_urgent_unread_count();  // unread rows that need a decision
bool attention_kind_open(uint8_t kind);  // false while the kind's feature is locked
int sim_ship_used_capacity(const Ship& ship);
int sim_ship_capacity(const Ship& ship);
int sim_ship_passenger_load(const Ship& ship);
int sim_ship_berths(const Ship& ship);
// Trade/jobs need an operational vessel; harbor artwork also shows damaged ones.
int sim_selected_docked_ship(bool includeDisabled = false);
void journal_add(uint8_t severity, const char* fmt, ...);

bool display_begin();
void render_frame();
void render_tick();
bool render_topbar_inbox_hit(int x, int y);
bool render_topbar_event_hit(int x, int y);
int render_map_port_at(int x, int y);
int render_map_ship_at(int x, int y);
void render_map_toggle_zoom(int x, int y);
bool render_map_zoom_button_hit(int x, int y);
void render_map_pan_by(int startX, int startY, int currentX, int currentY,
                       uint8_t startFocusX, uint8_t startFocusY);
void render_note_touch(int x, int y);
void render_mark_dirty(int x, int y, int w, int h);
#ifdef SH_HOST_TESTS
uint32_t render_host_notice_mask();
void render_host_reset_notice_mask();
bool render_host_ui_skin_selftest();
#endif
void input_update();

bool save_begin();
bool save_exists();
bool save_read();
bool save_write();
// Batch simulation catch-up with its elapsed-time checkpoint in one commit.
// Call save_write again after unsuspending and check that final return value.
void save_suspend(bool suspended);
// Set aside an unreadable save (renamed, never deleted) before starting over.
void save_quarantine();
extern bool g_bootResumed;  // the boot loaded a company rather than starting one
#if defined(SH_HOST_TESTS)
void input_test_touch(int x, int y);  // host tests drive the touch profiles
#endif
bool save_reset();
bool save_selftest();

void audio_begin();
void audio_tick();
void audio_notice();
void audio_alert();
