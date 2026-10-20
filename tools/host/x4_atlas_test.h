#pragma once
#include "atlas_content.h"
// The Sea Atlas (docs/SEA_ATLAS.md): a port is charted on the first arrival,
// noted quietly on the ship overview, read from Company Details; regions pay
// once, the full Atlas earns the Chartmakers title; reloads and pre-Atlas
// saves keep or rebuild a valid Atlas.
static bool run_x4_atlas_test() {
  int checks=0;uint32_t sampler=0;
  auto check=[&](bool ok,const char* text){++checks;printf("atlas: %s %s (card %u page %u cash %ld charted %06lx)\n",ok?"PASS":"FAIL",text,
    g_ui.card,g_ui.page,(long)g_campaign.coins,(unsigned long)g_atlas.charted);fflush(stdout);return ok;};
  auto frame=[&](){input_update();render_frame();input_update();};
  auto key=[&](uint8_t action){input_update();int button=x4_button_for(action);for(int i=0;i<7;++i,sampler+=10)board_x4_test_sample_buttons(sampler,1u<<button);for(int i=0;i<6;++i,sampler+=10)board_x4_test_sample_buttons(sampler,0);input_update();render_frame();input_update();};
  auto row=[&](int n){if(!input_test_focus_row(n))return false;key(X4_ACT_OK);return true;};
  auto command=[&](uint8_t cmd){if(!input_test_focus_command(cmd))return false;key(X4_ACT_OK);return true;};
  auto capture=[&](const char* name){const char* dir=getenv("SH_X4_ATLAS_DIR");if(!dir)return;::mkdir(dir,0755);char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);render_frame();dump_ppm_native(path);};
  auto reload=[&](){bool ok=save_read();g_ui.title=0;return ok;};
  auto voyage=[&](uint8_t port){
    MerchantSailQuote q=sim_merchant_sail_offer(0,port);
    if(q.code!=SIM_OK){printf("atlas: sail offer to %u failed: %s\n",port,sim_result_text(q.code));return false;}
    if(sim_merchant_sail(0,q).code!=SIM_OK)return false;
    sim_advance_minutes(q.minutes+60);
    for(int wait=0;wait<20&&g_ships[0].state==SHIP_SAILING;++wait){
      if(sim_select_incident(0))for(uint8_t o=0;o<3&&!sim_merchant_incident_resolve(o);++o){}
      g_ui.card=CARD_NONE;sim_advance_minutes(q.minutes+60);
    }
    g_ui.card=CARD_NONE;g_ui.scene=SCENE_FLEET;
    return g_ships[0].state!=SHIP_SAILING&&g_ships[0].currentPort==port;
  };
  auto pages=[&](){uint8_t p[9];return sim_atlas_voyage_pages(0,p,sizeof(p));};

  input_test_goal_moments(false);sim_test_story(false);
  save_reset();sim_new_campaign(93);g_campaign.onboardingStep=ONBOARD_DONE;g_campaign.tutorialDismissed=1;g_campaign.coins=5000;
  g_ui.title=0;g_ui.scene=SCENE_FLEET;g_ui.card=CARD_NONE;board_x4_test_buffered_input(true);frame();
  if(!check(sim_atlas_count()==1&&sim_atlas_charted(0)&&g_atlas.ship[0]==ATLAS_HOME,"a new company has only its home port charted"))return false;
  if(!check(voyage(1)&&sim_atlas_charted(1)&&g_atlas.ship[1]==0&&g_atlas.day[1]>=1&&pages()==1,"the first arrival at Gullhaven charts it"))return false;
  if(!check(command(X4_CMD_SHIP_0)&&g_ui.card==CARD_SHIP,"the arrived ship opens its overview"))return false;
  capture("01-ship-new-page");
  key(X4_ACT_BACK);g_ui.card=CARD_NONE;
  if(!check(voyage(0)&&pages()==0&&sim_atlas_count()==2,"returning home charts nothing new"))return false;
  if(!check(voyage(1)&&pages()==0&&g_atlas.voyage[1]!=g_ships[0].merchant.id,"a second visit is not a new page"))return false;

  // The fourth Mariner Coast port completes the region, which pays once.
  for(uint8_t p=0;p<MAX_PORTS;++p)g_ports[p].unlocked=1;
  g_campaign.regionMask=g_campaign.chartMask=0x3F;
  const int32_t before=g_campaign.coins,events=g_campaign.incidentNet;
  if(!check(voyage(2)&&g_campaign.coins==before&&!(g_atlas.regionsRewarded&1u),"three of four ports pay nothing yet"))return false;
  if(!check(voyage(3)&&sim_atlas_region_count(0)==4&&(g_atlas.regionsRewarded&1u)&&
    g_campaign.coins==before+ATLAS_REGION_REWARD&&g_campaign.incidentNet==events+ATLAS_REGION_REWARD,"the fourth pays the region reward under Events"))return false;
  if(!check(voyage(0)&&voyage(3)&&g_campaign.coins==before+ATLAS_REGION_REWARD,"the region pays only once"))return false;
  if(!check(reload()&&sim_atlas_count()==4&&(g_atlas.regionsRewarded&1u)&&g_atlas.ship[3]==0,"the Atlas survives a reload"))return false;

  // Company Details -> Sea Atlas, one page per region.
  g_ui.card=CARD_MORE;g_ui.page=0;frame();
  int atlasRow=-1;for(int r=0;r<CARD_VISIBLE_ROWS;++r){int item=voyage_detail_index(r);if(item>=0&&VOYAGE_DETAIL_CARDS[item]==CARD_ATLAS)atlasRow=r;}
  if(!check(atlasRow>=0&&row(atlasRow)&&g_ui.card==CARD_ATLAS&&g_ui.page==0,"Company Details opens the Sea Atlas"))return false;
  capture("02-atlas-mariner-coast");
  if(!check(row(1)&&x4_story_moment_mode()==X4_ATLAS_PAGE&&x4_story_moment_chapter()==1,"a charted row opens its page"))return false;
  capture("03-page-gullhaven");
  key(X4_ACT_DOWN);
  if(!check(x4_story_moment_active(),"other keys cannot act under the page"))return false;
  key(X4_ACT_OK);
  if(!check(!x4_story_moment_active()&&g_ui.card==CARD_ATLAS,"OK returns to the list"))return false;
  key(X4_ACT_PAGE_NEXT);
  if(!check(g_ui.card==CARD_ATLAS&&g_ui.page==1&&!input_test_focus_row(0),"the rocker turns to the next region, where uncharted rows are closed"))return false;
  capture("04-atlas-uncharted");

  // Every page, for the layout review.
  bool all=true;
  for(uint8_t p=0;p<MAX_PORTS;++p){g_atlas.charted|=1u<<p;if(!g_atlas.day[p]){g_atlas.day[p]=(uint16_t)(3+p);g_atlas.ship[p]=0;g_atlas.voyage[p]=1;}}
  for(uint8_t p=0;p<MAX_PORTS;++p){
    g_ui.card=CARD_ATLAS;g_ui.page=(uint8_t)(p/4);frame();
    all&=row(p%4)&&x4_story_moment_chapter()==p;
    char name[40];snprintf(name,sizeof(name),"10-page-%02u",p);capture(name);key(X4_ACT_OK);
  }
  if(!check(all,"all 24 pages open from their regions"))return false;

  // Completing the Atlas: the last uncharted port the ship can reach.
  {
    uint8_t last=NO_PORT;
    for(uint8_t p=4;p<MAX_PORTS&&last==NO_PORT;++p)if(sim_merchant_sail_offer(0,p).code==SIM_OK)last=p;
    if(!check(last!=NO_PORT,"a reachable port remains for the fixture"))return false;
    g_atlas.charted&=~(1u<<last);g_atlas.day[last]=0;g_atlas.ship[last]=0;g_atlas.voyage[last]=0;
    g_atlas.regionsRewarded=(uint8_t)(0x3Fu&~(1u<<PORT_DEFS[last].region));
    const int32_t cash=g_campaign.coins;
    if(!check(!sim_atlas_complete()&&voyage(last)&&sim_atlas_complete()&&g_campaign.coins==cash+ATLAS_REGION_REWARD,
      "the last page completes the Atlas and its region"))return false;
    g_ui.card=CARD_ATLAS;g_ui.page=PORT_DEFS[last].region;frame();capture("05-atlas-complete");
    g_ui.card=CARD_LEDGER;g_ui.page=0;frame();capture("06-ledger-chartmakers");
    if(!check(reload()&&sim_atlas_complete(),"the complete Atlas reloads"))return false;
  }

  // A save from before the Atlas: home, plus wherever ships lie.
  {
    const uint8_t at=g_ships[0].currentPort;const int32_t cash=g_campaign.coins;
    g_atlas.migrate=1;sim_after_load();
    if(!check(!g_atlas.migrate&&sim_atlas_charted(0)&&sim_atlas_charted(at)&&sim_atlas_count()==(at?2:1)&&
      g_atlas.voyage[at]==0&&g_campaign.coins==cash,"migration charts home and docked ports without paying"))return false;
    g_ui.title=0;g_ui.card=CARD_ATLAS;g_ui.page=PORT_DEFS[at].region;frame();
    if(!check(row(at%4)&&x4_story_moment_chapter()==at,"a migrated page opens"))return false;
    capture("07-migrated-page");key(X4_ACT_OK);
  }
  board_x4_test_buffered_input(false);
  ::printf("atlas: PASS (%d checks)\n",checks);
  return true;
}
