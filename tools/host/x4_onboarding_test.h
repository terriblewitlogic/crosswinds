#pragma once
// The guided first voyage, driven only by physical keys after the title: the
// focus must already rest on each next control, so every step below is OK.
static bool run_x4_onboarding_test() {
  int checks=0;uint32_t sampler=0;
  auto check=[&](bool ok,const char* text){++checks;printf("onboarding: %s %s (step %u card %u scene %u)\n",ok?"PASS":"FAIL",text,sim_onboarding_step(),g_ui.card,g_ui.scene);fflush(stdout);return ok;};
  auto frame=[&](){input_update();render_frame();};
  auto key=[&](uint8_t action){input_update();int button=x4_button_for(action);for(int i=0;i<7;++i,sampler+=10)board_x4_test_sample_buttons(sampler,1u<<button);for(int i=0;i<6;++i,sampler+=10)board_x4_test_sample_buttons(sampler,0);input_update();render_frame();input_update();};
  auto capture=[&](const char* name){const char* dir=getenv("SH_X4_ONBOARDING_DIR");if(!dir)return;::mkdir(dir,0755);char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);render_frame();dump_ppm_native(path);};
  auto owned=[&](uint8_t good){unsigned n=0;for(int c=0;c<g_ships[0].cargoCount;++c)if(g_ships[0].cargo[c].kind==CARGO_MARKET&&g_ships[0].cargo[c].good==good)n+=g_ships[0].cargo[c].quantity;return n;};
  input_test_goal_moments(true);

  // The guided path.
  save_reset();sim_new_campaign(91);board_x4_test_buffered_input(true);frame();
  if(!check(g_ui.title&&sim_onboarding_step()==ONBOARD_CAPTAIN,"a new company starts at the title, before its captain"))return false;
  key(X4_ACT_OK);
  if(!check(x4_onboarding_screen()&&g_ui.card==CARD_NONE,"the title leads to the captain choice, not the old tour"))return false;
  CaptainProfile candidates[3];
  for(uint8_t i=0;i<3;++i)sim_onboarding_candidate(i,&candidates[i]);
  if(!check(candidates[0].trait==TRAIT_NAVIGATOR&&candidates[1].trait==TRAIT_MERCHANT&&candidates[2].trait==TRAIT_GUNNER&&
      strcmp(candidates[0].name,candidates[1].name)&&strcmp(candidates[1].name,candidates[2].name)&&
      candidates[0].portrait!=candidates[1].portrait&&candidates[1].portrait!=candidates[2].portrait&&candidates[0].portrait!=candidates[2].portrait,
      "three distinct captains: a Navigator, a Merchant and a Gunner"))return false;
  capture("01-choose-captain");
  key(X4_ACT_DOWN);key(X4_ACT_OK);
  const CaptainProfile* captain=sim_ship_captain(0);
  if(!check(sim_onboarding_step()==ONBOARD_NAME&&captain&&captain->trait==TRAIT_MERCHANT&&!strcmp(captain->name,candidates[1].name)&&
      captain->portrait==candidates[1].portrait,"OK hires the highlighted Merchant onto the starting ship"))return false;
  capture("02-name-captain");
  key(X4_ACT_DOWN);
  if(!check(strcmp(captain->name,candidates[1].name)&&captain->portrait==candidates[1].portrait,"DOWN renames the captain; the face stays"))return false;
  char chosenName[16];snprintf(chosenName,sizeof(chosenName),"%s",captain->name);
  key(X4_ACT_OK);
  if(!check(sim_onboarding_step()==ONBOARD_BUY&&g_ui.card==CARD_SHIP&&!owned(GOOD_GRAIN)&&!strcmp(captain->name,chosenName),
      "OK keeps the name, drops the Grain gift and opens the ship"))return false;
  capture("03-ship-guided");
  key(X4_ACT_OK);
  if(!check(g_ui.card==CARD_TRADE&&x4_merchant_mode()==0&&x4_merchant_good_count()==1&&x4_merchant_good(0)==GOOD_TEXTILES,
      "TRADE has focus and opens Buy with Textiles only"))return false;
  key(X4_ACT_PAGE_NEXT);key(X4_ACT_PAGE_NEXT);
  if(!check(g_ui.card==CARD_TRADE&&x4_merchant_mode()==0,"the rocker swaps Buy and Sell only; Parcels wait"))return false;
  key(X4_ACT_OK);
  if(!check(x4_merchant_editing()&&x4_merchant_trade_quote().quantity==sim_ship_capacity(g_ships[0]),"the good has focus and starts at a full hold"))return false;
  capture("04-buy-guided");
  key(X4_ACT_OK);
  if(!check(owned(GOOD_TEXTILES)==(unsigned)sim_ship_capacity(g_ships[0])&&sim_onboarding_step()==ONBOARD_SAIL,"BUY has focus and fills the hold"))return false;
  key(X4_ACT_OK);
  if(!check(g_ui.card==CARD_SHIP,"BACK has focus after buying"))return false;
  key(X4_ACT_OK);
  uint8_t only[MAX_PORTS];
  if(!check(g_ui.card==CARD_VOYAGE&&sim_merchant_destinations(0,only,MAX_PORTS)==1&&only[0]==ONBOARD_PORT_TO,
      "SAIL has focus and the only destination is Gullhaven"))return false;
  if(!check(sim_merchant_sail_offer(0,2).code==SIM_LOCKED,"other ports refuse while on rails"))return false;
  capture("05-sail-guided");
  key(X4_ACT_OK);key(X4_ACT_OK);
  if(!check(g_ships[0].merchant.state==MERCHANT_SAILING&&sim_onboarding_step()==ONBOARD_VOYAGE,"Gullhaven and SAIL have focus; the voyage begins"))return false;
  capture("06-voyage-guided");
  g_ships[0].incidentArmed=0;sim_advance_minutes(g_ships[0].merchant.legMinutes[0]+1);frame();
  if(!check(sim_onboarding_step()==ONBOARD_SELL&&g_ships[0].currentPort==ONBOARD_PORT_TO,"arrival at Gullhaven moves to selling"))return false;
  if(g_ui.card!=CARD_SHIP)key(X4_ACT_OK);  // Watch: open the arrived ship
  if(!check(g_ui.card==CARD_SHIP,"the arrived ship opens"))return false;
  key(X4_ACT_OK);
  if(!check(g_ui.card==CARD_TRADE&&x4_merchant_mode()==1,"TRADE has focus and opens Sell"))return false;
  key(X4_ACT_OK);
  if(!check(x4_merchant_editing()&&x4_merchant_trade_quote().quantity==owned(GOOD_TEXTILES),"the Textiles have focus and every unit is selected"))return false;
  const int32_t before=g_campaign.coins;
  key(X4_ACT_OK);
  if(!check(!owned(GOOD_TEXTILES)&&g_campaign.coins>before&&sim_onboarding_step()==ONBOARD_DONE&&
      x4_goal_moment_active()&&x4_goal_moment_step()==GOAL_MOMENT_FIRST_PROFIT&&x4_goal_moment_value()>0,
      "SELL has focus; the sale ends the rails with the first-profit moment"))return false;
  capture("07-first-profit");
  key(X4_ACT_OK);
  if(!check(!x4_goal_moment_active()&&sim_merchant_destinations(0,only,MAX_PORTS)>1,"every port is open afterwards"))return false;
  key(X4_ACT_PAGE_NEXT);  // Sell -> Parcels
  if(!check(g_ui.card==CARD_PACKAGES,"Parcels are open afterwards"))return false;

  // Skip: BACK on the captain screen hires the highlighted captain, keeps the
  // gift and leaves the rails.
  save_reset();sim_new_campaign(92);frame();key(X4_ACT_OK);
  key(X4_ACT_DOWN);key(X4_ACT_DOWN);key(X4_ACT_BACK);
  captain=sim_ship_captain(0);
  if(!check(sim_onboarding_step()==ONBOARD_DONE&&!x4_onboarding_screen()&&captain&&captain->trait==TRAIT_GUNNER&&owned(GOOD_GRAIN)==2,
      "BACK skips: the highlighted Gunner is hired and the Grain gift stays"))return false;
  if(!check(sim_merchant_destinations(0,only,MAX_PORTS)>1,"a skipped company sails anywhere"))return false;

  // The captain's trait shows where it acts.
  save_reset();sim_new_campaign(93);frame();key(X4_ACT_OK);key(X4_ACT_DOWN);key(X4_ACT_OK);key(X4_ACT_OK);
  const uint8_t gift[GOOD_COUNT]={0};(void)gift;
  sim_onboarding_skip(0);
  g_ships[0].cargoCount=0;g_ships[0].cargo[g_ships[0].cargoCount++]=CargoItem{};
  CargoItem& bale=g_ships[0].cargo[0];bale.kind=CARGO_MARKET;bale.good=GOOD_GRAIN;bale.quantity=1;bale.jobKind=JOB_FREIGHT;
  bale.destination=bale.projectRegion=bale.sourcePort=NO_PORT;
  uint8_t sale[GOOD_COUNT]={};sale[GOOD_GRAIN]=1;
  const uint16_t merchantPrice=sim_merchant_trade_offer(0,false,sale).unitPrices[GOOD_GRAIN];
  sim_ship_captain(0)->trait=TRAIT_NAVIGATOR;
  const uint16_t plainPrice=sim_merchant_trade_offer(0,false,sale).unitPrices[GOOD_GRAIN];
  if(!check(merchantPrice==(uint16_t)((plainPrice*105u+50u)/100u)&&merchantPrice>plainPrice,"a Merchant captain sells for 5% more"))return false;

  board_x4_test_buffered_input(false);
  ::printf("onboarding: PASS (%d checks)\n",checks);
  return true;
}
