// Read-only company inspection using the production wire validator and loader.
// Build with sim.cpp/content.cpp (save.cpp is included below, not linked twice).
#include "../../save.cpp"
#include <cstdio>
#include <sys/stat.h>
void audio_begin() {} void audio_tick() {} void audio_notice() {} void audio_alert() {}
static void quoted(const char* s) {
  std::putchar('"');
  for (const unsigned char* p=(const unsigned char*)s; *p; ++p) {
    if (*p=='"' || *p=='\\') { std::putchar('\\'); std::putchar(*p); }
    else if (*p < 32) std::printf("\\u%04x", *p);
    else std::putchar(*p);
  }
  std::putchar('"');
}
struct Object {
  bool first=true;
  Object() { std::putchar('{'); }
  ~Object() { std::putchar('}'); }
  void key(const char* k) { if (!first) std::putchar(','); first=false; quoted(k); std::putchar(':'); }
  template<class T> void number(const char* k,T n) { key(k); std::printf("%lld",(long long)n); }
  void string(const char* k,const char* s) { key(k); quoted(s); }
  template<class T> void array(const char* k,const T* a,int count) {
    key(k); std::putchar('['); for(int i=0;i<count;++i) { if(i)std::putchar(',');std::printf("%lld",(long long)a[i]); } std::putchar(']');
  }
};
#define F(x) o.number(#x,s.x)
static void cargo(const CargoItem& s) { Object o;
  F(contractId);
  F(paidUnitPrice);
  F(good);
  F(quantity);
  F(destination);
  F(kind);
  F(jobKind);
  F(projectRegion);
  F(sourcePort);
  F(sailed);
}
static void booking(const BookedVoyage& s) { Object o;
  F(id);
  F(cargoCost);
  F(sailingCost);
  F(buyerPayment);
  F(bonus);
  F(payout);
  F(withheld);
  F(payNow);
  F(arrivalCoins);
  F(profit);
  F(state);
  F(origin);
  F(destination);
  F(good);
  F(quantity);
  F(settledQuantity);
  F(mode);
  F(projectRegion);
  F(receiptUnread);
  o.array("legMinutes",s.legMinutes,MAX_ROUTE_LEGS);
}
static void captain(const CaptainProfile& s) { Object o;
  F(xp);
  F(hireCost);
  F(trait);
  F(level);
  F(loyalty);
  F(scarCount);
  F(assignedShip);
  F(currentPort);
  F(active);
  F(voyages);
  F(deliveries);
  F(rescues);
  F(incidentsSurvived);
  o.string("name",s.name);
  o.array("scars",s.scars,2);
}
static void incident(const Incident& s) { Object o;
  F(active);
  F(shipIndex);
  F(kind);
  F(risk);
}
static void contract(const Contract& s) { Object o;
  F(id);
  F(deadlineMinute);
  F(reward);
  F(origin);
  F(destination);
  F(good);
  F(quantity);
  F(kind);
  F(projectRegion);
  F(active);
}
static void attention(const AttentionEntry& s) { Object o;
  F(id);
  F(worldMinute);
  F(kind);
  F(shipIndex);
  F(port);
  F(urgent);
  F(unread);
  F(resolved);
  F(region);
}
static void routeTemplate(const RouteTemplate& s) { Object o;
  F(origin);
  F(stopCount);
  F(loadMode);
  F(sellMode);
  F(buyGood);
  F(maxBuyPrice);
  o.array("stops",s.stops,s.stopCount);
}
static void returnTally(const ReturnTally& s) { Object o;
  F(revision); F(bookedArrivals); F(legacyArrivals); F(bookedCargo); F(legacyCargo);
  F(bookedCoins); F(bookedProfit); F(legacyCoins);
}
static void ship(const Ship& s) {
  Object o;
  F(id); o.string("name",s.name); F(captainId); F(classId);
  o.string("className",SHIP_CLASS_DEFS[s.classId].name);
  F(state); F(currentPort); F(fromPort); F(toPort); F(routeCount); F(routePos);
  o.array("route",s.route,s.routeCount); o.key("routeTemplate"); routeTemplate(s.routeTemplate);
  F(cargoCount); F(condition); F(morale); F(crew); o.array("modules",s.modules,2);
  F(incidentType); F(incidentArmed); F(manifestBonus); F(legElapsedMinutes); F(legTotalMinutes);
  F(prepaidCost); F(voyageEarned); F(voyageDelivered); o.key("booking"); booking(s.booking);
  o.key("returnTotal"); returnTally(s.returnLedger.total);
  o.key("returnSeen"); returnTally(s.returnLedger.seen);
  o.key("cargo"); std::putchar('[');for(int i=0;i<s.cargoCount;++i){if(i)std::putchar(',');cargo(s.cargo[i]);}std::putchar(']');
}
static void port(const PortState& s,int id) {
  Object o; o.number("id",id); o.string("name",PORT_DEFS[id].name);
  F(unlocked); F(officeLevel); F(warehouseLevel); F(shipyardLevel); F(warehouseCount);
  o.array("stock",s.stock,GOOD_COUNT); F(historyHead); F(bookingEpoch); o.array("bookingUsed",s.bookingUsed,GOOD_COUNT);
  o.key("warehouse");std::putchar('[');for(int i=0;i<s.warehouseCount;++i){if(i)std::putchar(',');cargo(s.warehouse[i]);}std::putchar(']');
}
static void campaign(const Campaign& s) { Object o;
  F(seed);
  F(rng);
  F(worldMinutes);
  F(nextContractId);
  F(nextShipId);
  F(coins);
  F(recoveryDebt);
  F(nextAttentionId);
  F(recruitmentWeek);
  F(companyXp);
  F(deliveries);
  F(contractsCompleted);
  F(incidentsResolved);
  F(contractRevenue);
  F(marketProfit);
  F(operatingCosts);
  F(capitalSpending);
  F(incidentNet);
  F(rank);
  F(regionMask);
  F(chartMask);
  F(visitedMask);
  F(shipCount);
  F(won);
  F(paused);
  F(soundOn);
  F(timeScale);
  F(worldEventType);
  F(worldEventGood);
  F(worldEventRegion);
  F(worldEventDays);
  F(lastMarketDay);
  F(tutorialStep);
  F(tutorialFlags);
  F(tutorialDismissed);
  F(captainCount);
  F(stageSeen);
  F(economyVersion);
  F(nextVoyageId);
  F(deliveredUnits);
  F(completedVoyages);
  F(legacyFeatureMask);
  F(standbySessionId);
  F(standbyAccountedRealSeconds);
  F(returnMilestones); F(returnMilestonesSeen);
  o.array("charterProgress",s.charterProgress,REGION_COUNT);
  o.array("projectWorksCredit",s.projectWorksCredit,REGION_COUNT);
}
static void state() {
  Object o; o.key("campaign");campaign(g_campaign);
  o.key("projectCargo");std::putchar('[');for(int i=0;i<REGION_COUNT;++i){if(i)std::putchar(',');std::printf("[%u,%u]",g_campaign.projectCargo[i][0],g_campaign.projectCargo[i][1]);}std::putchar(']');
  o.key("ships");std::putchar('[');for(int i=0;i<g_campaign.shipCount;++i){if(i)std::putchar(',');ship(g_ships[i]);}std::putchar(']');
  o.key("captains");std::putchar('[');for(int i=0;i<g_campaign.captainCount;++i){if(i)std::putchar(',');captain(g_captains[i]);}std::putchar(']');
  o.key("ports");std::putchar('[');for(int i=0;i<MAX_PORTS;++i){if(i)std::putchar(',');port(g_ports[i],i);}std::putchar(']');
  o.key("contracts");std::putchar('[');bool first=true;for(int i=0;i<MAX_PORTS;++i)for(int j=0;j<MAX_CONTRACTS_PER_PORT;++j)if(g_contracts[i][j].active){if(!first)std::putchar(',');first=false;contract(g_contracts[i][j]);}std::putchar(']');
  o.key("incident");incident(g_incident);
  o.key("incidentQueue");std::putchar('[');for(int i=0;i<g_incidentQueueCount;++i){if(i)std::putchar(',');incident(g_incidentQueue[i]);}std::putchar(']');
  o.key("attention");std::putchar('[');for(int i=0;i<g_attentionCount;++i){if(i)std::putchar(',');attention(g_attention[i]);}std::putchar(']');
  o.key("journal");std::putchar('[');for(int i=0;i<g_journalCount;++i){if(i)std::putchar(',');const JournalEntry& j=g_journal[(g_journalHead+MAX_JOURNAL-g_journalCount+i)%MAX_JOURNAL];Object e;e.number("worldMinute",j.worldMinute);e.number("severity",j.severity);e.string("text",j.text);}std::putchar(']');
}
int main(int argc,char** argv) {
  struct stat st{};
  if(argc!=2 || stat(argv[1],&st)!=0 || !S_ISDIR(st.st_mode)) { std::fprintf(stderr,"Usage: inspect_save EXTRACTED_COPY_DIRECTORY\n");return 2; }
  setenv("SH_FS_DIR",argv[1],1);
  if(!save_begin()) return 3;
  save_suspend(true);
  bool loaded=false;const char* source="";
  if(LittleFS.exists(SNAP_PATH)||LittleFS.exists(SNAP_BAK)) {
    if(read_snapshot(SNAP_PATH,true)){loaded=true;source=SNAP_PATH;}
    else if(read_snapshot(SNAP_BAK,false)){loaded=true;source=SNAP_BAK;}
  } else {
    const char* cores[]={CH_PATH[CH_CORE],BAK_PATH[CH_CORE]};
    for(int candidate=0;candidate<2&&!loaded;++candidate) {
      sim_new_campaign(0x51A17EEDu);
      if(!read_chunk_path(CH_CORE,cores[candidate],candidate==0,false))continue;
      loaded=read_chunk(CH_PORTS,true)&&read_chunk(CH_JOBS,true)&&read_chunk(CH_FLEET,true)&&read_chunk(CH_JOURNAL,true);
      if(loaded)source=cores[candidate];
    }
  }
  if(!loaded){std::fprintf(stderr,"No coherent company could be read; input files were not changed.\n");return 4;}
  {Object o;o.string("source",source);o.number("snapshotGeneration",snapshotGeneration);
  o.number("loadedVersion",loadedVersion);
  o.key("wire_loaded");state();
  if (loadedVersion < 9) sim_return_migrate_legacy();
  sim_after_load();
  o.key("after_load");state();}
  std::putchar('\n');return 0;
}
