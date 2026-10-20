#pragma once
#include "story_content.h"
// The Grey Passenger (docs/GREY_PASSENGER.md): her offer is a Parcels row that
// hides what and where; taking it reveals the keepsake; delivery pays like a
// parcel and queues a card for a calm screen; Keepsakes rereads it. Declines,
// cooldowns, full holds, chapter order and reloads are checked in the sim.
static bool run_x4_story_test() {
  int checks=0;uint32_t sampler=0;
  auto check=[&](bool ok,const char* text){++checks;printf("story: %s %s (card %u cash %ld offer %u carry %u done %02x)\n",ok?"PASS":"FAIL",text,
    g_ui.card,(long)g_campaign.coins,g_campaign.storyOfferChapter,g_campaign.storyCarrying,g_campaign.storyDelivered);fflush(stdout);return ok;};
  auto frame=[&](){input_update();render_frame();input_update();};
  auto key=[&](uint8_t action){input_update();int button=x4_button_for(action);for(int i=0;i<7;++i,sampler+=10)board_x4_test_sample_buttons(sampler,1u<<button);for(int i=0;i<6;++i,sampler+=10)board_x4_test_sample_buttons(sampler,0);input_update();render_frame();input_update();};
  auto row=[&](int n){if(!input_test_focus_row(n))return false;key(X4_ACT_OK);return true;};
  auto command=[&](uint8_t cmd){if(!input_test_focus_command(cmd))return false;key(X4_ACT_OK);return true;};
  auto capture=[&](const char* name){const char* dir=getenv("SH_X4_STORY_DIR");if(!dir)return;::mkdir(dir,0755);char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);render_frame();dump_ppm_native(path);};
  // Sail ship 0 straight to a port and arrive; the arrival receipt is read.
  auto voyage=[&](uint8_t port){
    MerchantSailQuote q=sim_merchant_sail_offer(0,port);
    if(q.code!=SIM_OK){printf("story: sail offer to %u failed: %s\n",port,sim_result_text(q.code));return false;}
    {SimResult r=sim_merchant_sail(0,q);if(r.code!=SIM_OK){printf("story: sail to %u failed: %s\n",port,sim_result_text(r.code));return false;}}
    sim_advance_minutes(q.minutes+60);
    // Sea encounters pause a voyage until answered; any answer will do here.
    for(int wait=0;wait<20&&g_ships[0].state!=SHIP_DOCKED&&g_ships[0].state!=SHIP_ARRIVED;++wait){
      if(sim_select_incident(0))for(uint8_t o=0;o<3&&!sim_merchant_incident_resolve(o);++o){}
      g_ui.card=CARD_NONE;sim_advance_minutes(q.minutes+60);
    }
    if(g_ships[0].state==SHIP_SAILING||g_ships[0].currentPort!=port){printf("story: arrival at %u failed: state %u port %u\n",port,g_ships[0].state,g_ships[0].currentPort);return false;}
    sim_ack_arrival_receipt(0,g_ships[0].merchant.id);
    g_ui.card=CARD_NONE;g_ui.scene=SCENE_FLEET;return true;
  };
  // A reload lands on the title page; the suite carries on past it.
  auto reload=[&](){bool ok=save_read();g_ui.title=0;return ok;};
  auto offer=[&](){StoryOffer o{};sim_story_offer(0,o);return o;};
  auto keepsakeAboard=[&](){for(int c=0;c<g_ships[0].cargoCount;++c)if(g_ships[0].cargo[c].jobKind==JOB_STORY)return true;return false;};

  input_test_goal_moments(false);sim_test_story(true);
  save_reset();sim_new_campaign(91);g_campaign.onboardingStep=ONBOARD_DONE;g_campaign.tutorialDismissed=1;g_campaign.coins=5000;
  g_ui.title=0;g_ui.scene=SCENE_FLEET;g_ui.card=CARD_NONE;board_x4_test_buffered_input(true);frame();
  if(!check(g_campaign.storyCooldown==STORY_COOLDOWN_START&&g_campaign.storyOfferChapter==STORY_NONE,"a new company starts with her cooldown"))return false;
  if(!check(voyage(1)&&!offer().reward&&voyage(0)&&!offer().reward,"she stays away for the first two arrivals"))return false;
  if(!check(voyage(2)&&offer().chapter==0&&offer().reward==150&&offer().code==SIM_OK,"the third arrival brings the first chapter's offer"))return false;

  // The Parcels tab: her row first, sealed; the detail hides what and where.
  if(!check(command(X4_CMD_SHIP_0)&&g_ui.card==CARD_SHIP&&row(0),"the ship opens trading"))return false;
  key(X4_ACT_PAGE_PREV);
  if(!check(g_ui.card==CARD_PACKAGES&&x4_merchant_story_row(0)&&!x4_merchant_package(0).id,"her sealed offer is the first Parcels row"))return false;
  capture("01-parcels-offer");
  if(!check(row(0)&&x4_merchant_editing()&&x4_merchant_story_selected(),"OK opens her offer without accepting"))return false;
  capture("02-offer-detail");
  const int32_t before=g_campaign.coins;const int used=sim_ship_used_capacity(g_ships[0]);
  key(X4_ACT_OK);
  if(!check(x4_story_moment_active()&&x4_story_moment_mode()==X4_STORY_REVEAL&&x4_story_moment_chapter()==0,"taking it reveals the keepsake"))return false;
  capture("03-reveal");
  if(!check(keepsakeAboard()&&g_campaign.storyCarrying==0&&g_campaign.coins==before&&sim_ship_used_capacity(g_ships[0])==used+1,
    "the keepsake is free and takes exactly one slot"))return false;
  key(X4_ACT_DOWN);
  if(!check(x4_story_moment_active(),"other keys cannot act under the page"))return false;
  key(X4_ACT_OK);
  if(!check(!x4_story_moment_active()&&g_ui.card==CARD_PACKAGES&&!x4_merchant_story_row(0),"OK returns to Parcels without her row"))return false;
  if(!check(reload()&&keepsakeAboard()&&sim_story_carrier()==0,"the keepsake survives a reload"))return false;
  if(!check(sim_merchant_sail_offer(0,1).packagePayout>=150,"the sail review counts the keepsake's pay"))return false;

  // Delivery pays like a parcel and waits for a calm screen.
  key(X4_ACT_BACK);key(X4_ACT_BACK);
  const int32_t sailing=g_campaign.coins;
  if(!check(voyage(1)&&g_campaign.coins==sailing+150&&!keepsakeAboard()&&g_campaign.storyDelivered==1&&g_campaign.storyDeliveries==1,
    "arrival at Gullhaven delivers and pays 150"))return false;
  if(!check(sim_story_card_pending()==0&&reload()&&sim_story_card_pending()==0,"the delivery card survives a power-off"))return false;
  g_ui.card=CARD_TUTORIAL;g_ui.page=0;frame();
  if(!check(!x4_story_moment_active(),"an open guide page holds the card back"))return false;
  g_ui.card=CARD_NONE;frame();
  if(!check(x4_story_moment_active()&&x4_story_moment_mode()==X4_STORY_DELIVERED&&x4_story_moment_chapter()==0,"the card opens once the screen is calm"))return false;
  capture("04-delivered");
  key(X4_ACT_OK);
  if(!check(!x4_story_moment_active()&&sim_story_card_pending()==STORY_NONE&&reload()&&sim_story_card_pending()==STORY_NONE,
    "OK marks the card read for good"))return false;

  // Keepsakes rereads it from Company Details.
  g_ui.card=CARD_MORE;g_ui.page=0;frame();
  int keepsakesRow=-1;for(int r=0;r<CARD_VISIBLE_ROWS;++r){int item=voyage_detail_index(r);if(item>=0&&VOYAGE_DETAIL_CARDS[item]==CARD_KEEPSAKES)keepsakesRow=r;}
  if(!check(keepsakesRow>=0&&row(keepsakesRow)&&g_ui.card==CARD_KEEPSAKES,"Company Details lists Keepsakes after a delivery"))return false;
  capture("05-keepsakes");
  if(!check(row(0)&&x4_story_moment_mode()==X4_STORY_REREAD&&x4_story_moment_chapter()==0,"a keepsake row rereads its page"))return false;
  capture("06-reread");
  key(X4_ACT_BACK);
  if(!check(!x4_story_moment_active()&&g_ui.card==CARD_KEEPSAKES,"BACK returns to the list"))return false;

  // Cooldown after a delivery, then a decline by sailing away.
  g_ui.card=CARD_NONE;
  for(uint8_t p=0;p<MAX_PORTS;++p)g_ports[p].unlocked=1;
  g_campaign.regionMask=g_campaign.chartMask=0x3F;
  if(!check(voyage(0)&&voyage(2)&&voyage(0)&&!offer().reward,"three arrivals pass after a delivery"))return false;
  bool appeared=voyage(1)&&offer().reward==200;  // not Watchman Point, where she last approached
  if(!check(appeared&&offer().chapter>=1&&offer().chapter<=5,"the next offer is a middle chapter at the second rate"))return false;
  const uint8_t declinedPort=g_ships[0].currentPort;
  // The decline happens on departure; the arrival that follows already counts one down.
  if(!check(voyage(0)&&g_campaign.storyOfferChapter==STORY_NONE&&g_campaign.storyCooldown==STORY_COOLDOWN_DECLINED-1&&
    g_campaign.storyLastPort==declinedPort,"sailing away declines and starts the longer cooldown"))return false;
  bool waited=true;for(int i=0;i<STORY_COOLDOWN_DECLINED-1;++i)waited&=voyage(i%2?0:1)&&g_campaign.storyOfferChapter==STORY_NONE;
  if(!check(waited&&g_campaign.storyCooldown==0,"she waits out the decline cooldown"))return false;
  if(!check(voyage(declinedPort)&&g_campaign.storyOfferChapter==STORY_NONE,"she does not repeat the port she was declined at"))return false;
  if(!check(voyage(0)&&offer().reward==200,"she approaches again somewhere else"))return false;

  // A full hold keeps her row but cannot take it.
  {
    Ship saved=g_ships[0];Ship& s=g_ships[0];
    while(sim_ship_used_capacity(s)<sim_ship_capacity(s)&&s.cargoCount<MAX_CARGO_ITEMS){CargoItem c{};c.kind=CARGO_MARKET;c.good=GOOD_GRAIN;c.quantity=1;c.destination=NO_PORT;c.projectRegion=NO_PORT;c.sourcePort=s.currentPort;s.cargo[s.cargoCount++]=c;}
    if(!check(offer().code==SIM_NO_CAPACITY&&sim_story_accept(0).code==SIM_NO_CAPACITY,"a full hold lists her offer but cannot take it"))return false;
    g_ships[0]=saved;
  }
  if(!check(sim_story_accept(0).code==SIM_OK&&keepsakeAboard(),"with a free slot it is taken"))return false;
  {
    const uint8_t chapter=g_campaign.storyCarrying;
    const uint8_t port=STORY_CHAPTER_DEFS[chapter].port;
    MerchantSailQuote q=sim_merchant_sail_offer(0,port);
    if(!check(q.code==SIM_OK&&voyage(port)&&(g_campaign.storyDelivered&(1u<<chapter))&&g_campaign.storyOrder[1]==chapter,
      "the middle keepsake reaches its recipient"))return false;
    g_ui.card=CARD_NONE;frame();key(X4_ACT_OK);
  }

  // The toy boat is offered only after the other six, with her own line.
  g_campaign.storyDelivered=0x3F;g_campaign.storyDeliveries=6;
  for(uint8_t i=0;i<STORY_CHAPTERS;++i)g_campaign.storyOrder[i]=i<6?i:STORY_NONE;
  g_campaign.storyCooldown=0;g_campaign.storyLastPort=NO_PORT;g_campaign.storyCardPending=STORY_NONE;
  if(!check(voyage(g_ships[0].currentPort==0?2:0)&&offer().chapter==6&&offer().line==3&&offer().reward==800,
    "the finale is the toy boat, at 800, in her own words"))return false;
  g_ui.card=CARD_NONE;frame();
  if(!check(command(X4_CMD_SHIP_0)&&row(0),"the ship opens trading at the finale"))return false;
  key(X4_ACT_PAGE_PREV);
  if(!check(g_ui.card==CARD_PACKAGES&&row(0)&&x4_merchant_story_selected(),"her finale offer opens"))return false;
  capture("07-finale-offer");
  key(X4_ACT_OK);key(X4_ACT_OK);key(X4_ACT_BACK);key(X4_ACT_BACK);
  if(!check(g_campaign.storyCarrying==6&&voyage(3)&&g_campaign.storyDelivered==STORY_ALL_DELIVERED,"the toy boat comes home to Mariner Rest"))return false;
  g_ui.card=CARD_NONE;frame();
  if(!check(x4_story_moment_chapter()==6,"the finale has its card"))return false;
  capture("08-finale");
  key(X4_ACT_OK);
  for(int i=0;i<8;++i)voyage(i%2?0:1);
  if(!check(g_campaign.storyOfferChapter==STORY_NONE,"after the finale she is gone for good"))return false;
  g_ui.card=CARD_KEEPSAKES;g_ui.page=1;frame();
  capture("09-keepsakes-page-2");
  if(!check(row(0)&&x4_story_moment_chapter()==6,"the toy boat stays in Keepsakes"))return false;
  key(X4_ACT_OK);
  // Every page, for the layout review: the longest passages and thread lines.
  bool all=true;
  for(int entry=0;entry<STORY_CHAPTERS;++entry){
    g_ui.card=CARD_KEEPSAKES;g_ui.page=(uint8_t)(entry/CARD_VISIBLE_ROWS);frame();
    all&=row(entry%CARD_VISIBLE_ROWS)&&x4_story_moment_chapter()==g_campaign.storyOrder[entry];
    char name[32];snprintf(name,sizeof(name),"10-page-%d",entry+1);capture(name);key(X4_ACT_OK);
  }
  if(!check(all,"every keepsake rereads in delivery order"))return false;
  if(!check(reload()&&g_campaign.storyDelivered==STORY_ALL_DELIVERED,"the completed story reloads"))return false;
  board_x4_test_buffered_input(false);
  ::printf("story: PASS (%d checks)\n",checks);
  return true;
}
