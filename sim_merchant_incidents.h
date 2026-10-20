static char merchantOutcomeAction[48]{};
const char* sim_merchant_outcome_action(){return merchantOutcomeAction;}
// Merchant incidents quote their complete cost before a decision. Parcels are
// never at risk; a zero-coin company always has a safe way to resume sailing.
bool sim_select_incident(uint8_t ship) {
  if (g_incident.active && g_incident.shipIndex == ship) return true;
  for (uint8_t i = 0; i < g_incidentQueueCount; ++i) if (g_incidentQueue[i].shipIndex == ship) {
    Incident selected = g_incidentQueue[i];
    if (g_incident.active) g_incidentQueue[i] = g_incident;
    else { for (int j = i; j + 1 < g_incidentQueueCount; ++j) g_incidentQueue[j] = g_incidentQueue[j + 1]; --g_incidentQueueCount; }
    g_incident = selected; return true;
  }
  return false;
}

static uint8_t encounter_cargo(const Ship& s) {
  // Choose the least valuable owned good, independent of storage order.
  uint8_t good = NO_PORT;
  for (int i=0;i<s.cargoCount;++i) if(s.cargo[i].kind==CARGO_MARKET && s.cargo[i].quantity) {
    uint8_t candidate=s.cargo[i].good;
    if(good==NO_PORT || GOOD_DEFS[candidate].basePrice<GOOD_DEFS[good].basePrice ||
       (GOOD_DEFS[candidate].basePrice==GOOD_DEFS[good].basePrice && candidate<good)) good=candidate;
  }
  return good;
}
static int encounter_contract_slot(const Ship& s) {
  for(int i=0;i<MAX_CONTRACTS_PER_PORT;++i) if(!g_contracts[s.currentPort][i].active) return i;
  for(int i=0;i<MAX_CONTRACTS_PER_PORT;++i) if(g_contracts[s.currentPort][i].active==1) return i;
  return -1;
}
MerchantIncidentChoice sim_merchant_incident_choice(uint8_t option) {
  MerchantIncidentChoice q{}; q.code=SIM_INVALID; q.good=q.gainGood=q.giveGood=NO_PORT;
  if(!g_incident.active || g_incident.kind>=INCIDENT_COUNT || option>=encounter_choices(g_incident.kind) || g_incident.shipIndex>=g_campaign.shipCount) return q;
  const Ship& s=g_ships[g_incident.shipIndex]; const uint8_t kind=g_incident.kind;
  if(s.merchant.state!=MERCHANT_SAILING || s.state!=SHIP_AWAITING_DECISION) return q;
  q.code=SIM_OK;
  q.gainQuantity=1;
  static const char* const waits[]={"Keep clear","Shelter with the bird","Leave the white wake","Care for the crew","Present our papers","Hear the crew"};
  if(kind<INCIDENT_RESCUE) {
    if(!option) q.title=waits[kind];
    else {
      q.title=kind==INCIDENT_PIRATES?"Salvage 2 Spices":kind==INCIDENT_STORM?"Salvage 2 Textiles":"Salvage 2 Tea";
      q.riskPercent=g_incident.risk==RISK_DANGEROUS?35:g_incident.risk==RISK_FAVORABLE?15:25;
      q.good=encounter_cargo(s);
      q.gainGood=kind==INCIDENT_PIRATES?GOOD_SPICES:kind==INCIDENT_STORM?GOOD_TEXTILES:GOOD_TEA;
      q.gainQuantity=2;
      if(q.good==NO_PORT)q.code=SIM_INVALID;
      if(kind==INCIDENT_REEF && ship_captain_profile(s) && captain_trait(s)==TRAIT_NAVIGATOR) {
        q.riskPercent=(uint8_t)max(5,(int)q.riskPercent-15);q.benefit="Navigator";
      }
      if(kind==INCIDENT_STORM && ship_storm(s)>=2) {q.riskPercent=(uint8_t)max(5,(int)q.riskPercent-15);q.benefit="Storm protection";}
      if(kind==INCIDENT_PIRATES && (ship_guns(s)>0 || (ship_captain_profile(s) && captain_trait(s)==TRAIT_GUNNER))) {
        q.riskPercent=(uint8_t)max(5,(int)q.riskPercent-15);q.benefit=ship_guns(s)>0?"Guns":"Gunner";
      }
    }
  } else if(kind==INCIDENT_RESCUE) {
    q.title=option==0?"Carry their parcel":"Continue sailing";
    if(!option)q.parcel=1;
  } else if(kind==INCIDENT_TRADE) {
    q.title=option==0?"Buy 1 Tea":option==1?"Buy 1 Spices":"Continue sailing";
    if(option<2){q.gainGood=option?GOOD_SPICES:GOOD_TEA;q.coins=-(GOOD_DEFS[q.gainGood].basePrice*70/100);}
  } else if(option==1) q.title="Continue sailing";
  else if(kind==INCIDENT_DOUBLE) {
    q.title="Exchange 1 cargo unit";q.giveGood=encounter_cargo(s);
    q.gainGood=q.giveGood==GOOD_TEA?GOOD_SPICES:GOOD_TEA;
    if(q.giveGood==NO_PORT)q.code=SIM_INVALID;
  } else if(kind==INCIDENT_ISLAND) {
    q.title="Gather 1 Grain";q.gainGood=GOOD_GRAIN;
  } else if(kind==INCIDENT_NIGHT_MARKET) {
    q.title="Buy 1 Spices";q.gainGood=GOOD_SPICES;q.coins=-(GOOD_DEFS[GOOD_SPICES].basePrice*60/100);
  } else if(kind==INCIDENT_STRANGER) {
    q.title="Accept the parcel";q.parcel=1;

  }
  if(q.parcel && encounter_contract_slot(s)<0)q.code=SIM_CONTRACT_UNAVAILABLE;
  if(q.gainGood<GOOD_COUNT || q.parcel) {
    bool removesSlot=false;
    if(q.giveGood<GOOD_COUNT)for(int i=0;i<s.cargoCount;++i)if(s.cargo[i].kind==CARGO_MARKET && s.cargo[i].good==q.giveGood){removesSlot=s.cargo[i].quantity==1;break;}
    if((sim_ship_used_capacity(s)+(q.parcel?1:q.gainQuantity)-(q.giveGood<GOOD_COUNT?1:0)>sim_ship_capacity(s)) ||
       (s.cargoCount>=MAX_CARGO_ITEMS && !removesSlot))q.code=SIM_NO_CAPACITY;
  }
  if(q.coins<0 && g_campaign.coins < -q.coins)q.code=SIM_NO_FUNDS;
  return q;
}

SimResult sim_merchant_incident_resolve(uint8_t option, bool automatic) {
  if(automatic && (!g_incident.active || option!=encounter_fallback(g_incident.kind)))return SIM_INVALID;
  MerchantIncidentChoice q = sim_merchant_incident_choice(option);
  if (q.code != SIM_OK) return q.code;
  const uint8_t index = g_incident.shipIndex;
  MerchantUndo undo(index);
  const Incident incident = g_incident;
  const IncidentOutcome oldOutcome = g_incidentOutcome;
  Incident queue[MAX_INCIDENTS]; memcpy(queue, g_incidentQueue, sizeof(queue));
  const uint8_t queueCount = g_incidentQueueCount, attentionCount = g_attentionCount;
  AttentionEntry attention[MAX_ATTENTION]; memcpy(attention, g_attention, sizeof(attention));
  CaptainProfile* captain = sim_ship_captain(index);
  const CaptainProfile beforeCaptain = captain ? *captain : CaptainProfile{};
  Ship& s = g_ships[index];
  uint32_t delay = q.delayMinutes;
  const bool salvageFailed=q.riskPercent && rng_range(100)<q.riskPercent;
  if (salvageFailed) {
    if (q.good < GOOD_COUNT) {
      for (int i = 0; i < s.cargoCount; ++i) if (s.cargo[i].kind == CARGO_MARKET && s.cargo[i].good == q.good) {
        market_remove_unit(s.cargo[i]); if (!s.cargo[i].quantity) cargo_remove(s, i); break;
      }
    }
  }
  uint16_t exchangeBasis=0;
  if(q.giveGood<GOOD_COUNT)for(int i=0;i<s.cargoCount;++i)if(s.cargo[i].kind==CARGO_MARKET && s.cargo[i].good==q.giveGood){
    exchangeBasis=s.cargo[i].paidUnitPrice;market_remove_unit(s.cargo[i]);if(!s.cargo[i].quantity)cargo_remove(s,i);break;
  }
  if(q.parcel) {
    Contract& contract=g_contracts[s.currentPort][encounter_contract_slot(s)];
    contract={};contract.id=g_campaign.nextContractId++;contract.origin=s.currentPort;
    contract.destination=s.merchant.destination;contract.kind=JOB_PACKAGE;contract.good=NO_PORT;
    contract.quantity=1;contract.reward=25;contract.active=2;contract.projectRegion=NO_PORT;
    CargoItem item{};item.contractId=contract.id;item.kind=CARGO_CONTRACT;item.jobKind=JOB_PACKAGE;
    item.good=NO_PORT;item.quantity=1;item.destination=contract.destination;item.projectRegion=NO_PORT;item.sourcePort=s.currentPort;
    s.cargo[s.cargoCount++]=item;
  }
  if (q.gainGood < GOOD_COUNT && !salvageFailed) {
    CargoItem cargo{}; cargo.good = q.gainGood; cargo.quantity = q.gainQuantity; cargo.kind = CARGO_MARKET;
    cargo.jobKind = JOB_FREIGHT; cargo.destination = NO_PORT; cargo.projectRegion = NO_PORT;
    cargo.sourcePort = s.currentPort; cargo.sailed = 1; cargo.paidUnitPrice = q.giveGood<GOOD_COUNT ? exchangeBasis : (uint16_t)max(0,-q.coins);
    s.cargo[s.cargoCount++] = cargo;
  }
  g_campaign.coins += q.coins; g_campaign.incidentNet += q.coins;
  s.legTotalMinutes = (uint16_t)min((uint32_t)65535, (uint32_t)s.legTotalMinutes + delay);
  s.merchant.legMinutes[s.routePos] = s.legTotalMinutes;
  s.state = SHIP_SAILING; s.encounterWait=0;
  ++g_campaign.incidentsResolved; gain_captain_xp(s, 1);
  if (captain && captain->incidentsSurvived < 65535) ++captain->incidentsSurvived;
  capture_incident_outcome(incident, option, undo.ship, undo.campaign.coins, captain ? &beforeCaptain : nullptr);
  if(incident.kind==INCIDENT_DOUBLE && option==0)s.sighting=4;
  g_incidentOutcome.merchant = 1;
  if(automatic) {s.autoEncounter=incident.kind+1;s.autoDelay=(uint16_t)(delay+(encounter_opportunity(incident.kind)?0:300));++s.returnLedger.total.revision;}
  if(q.riskPercent) {
    if(salvageFailed)journal_add(1,"%s: recovery failed; lost 1 %s.",s.name,GOOD_DEFS[q.good].name);
    else journal_add(1,"%s: recovered %u %s.",s.name,q.gainQuantity,GOOD_DEFS[q.gainGood].name);
  } else journal_add(1,"%s: %s.",s.name,q.title);
  if(!automatic)journal_add(0,"%s",encounter_journal(incident.kind));
  g_incident = {}; attention_resolve(ATTENTION_INCIDENT, index); activate_next_incident();
  g_ui.dirty = 1;
  if (save_write()) {
    if(automatic) g_incidentOutcome=oldOutcome;
    else snprintf(merchantOutcomeAction,sizeof(merchantOutcomeAction),"%s",q.riskPercent?(salvageFailed?"Recovery failed":"Cargo recovered"):q.title);
    return SIM_OK;
  }
  undo.restore(); g_incident = incident; g_incidentOutcome = oldOutcome;
  memcpy(g_incidentQueue, queue, sizeof(queue)); g_incidentQueueCount = queueCount;
  memcpy(g_attention, attention, sizeof(attention)); g_attentionCount = attentionCount;
  if (captain) *captain = beforeCaptain;
  return SIM_SAVE_FAILED;
}
