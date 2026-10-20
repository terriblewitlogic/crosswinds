#pragma once
// Goal moments: a completed NEXT GOAL rung covers the screen until OK or BACK,
// leaves the task underneath untouched, waits for calm screens, and never
// replays progress a reload already contained.
static bool run_x4_goal_moment_test() {
  int checks=0;uint32_t sampler=0;
  auto check=[&](bool ok,const char* text){++checks;printf("goal moment: %s %s (card %u scene %u step %u)\n",ok?"PASS":"FAIL",text,g_ui.card,g_ui.scene,voyage_goal_step());fflush(stdout);return ok;};
  auto frame=[&](){input_update();render_frame();};
  auto key=[&](uint8_t action){input_update();int button=x4_button_for(action);for(int i=0;i<7;++i,sampler+=10)board_x4_test_sample_buttons(sampler,1u<<button);for(int i=0;i<6;++i,sampler+=10)board_x4_test_sample_buttons(sampler,0);input_update();render_frame();input_update();};
  auto command=[&](uint8_t cmd){if(!input_test_focus_command(cmd))return false;key(X4_ACT_OK);return true;};
  auto capture=[&](const char* name){const char* dir=getenv("SH_X4_GOAL_MOMENT_DIR");if(!dir)return;::mkdir(dir,0755);char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);render_frame();dump_ppm_native(path);};
  input_test_goal_moments(true);
  save_reset();sim_new_campaign(77);g_campaign.tutorialDismissed=1;g_campaign.coins=5000;g_campaign.deliveredUnits=11;
  g_ui.title=0;g_ui.scene=SCENE_FLEET;g_ui.card=CARD_NONE;board_x4_test_buffered_input(true);frame();
  if(!check(!x4_goal_moment_active()&&voyage_goal_step()==GOAL_STEP_MARINER_REST,"a loaded company starts without a moment"))return false;
  command(X4_CMD_MAP);
  const uint8_t scene=g_ui.scene,port=g_ui.selectedPort,zoom=g_ui.mapZoom;
  g_campaign.deliveredUnits=12;frame();
  if(!check(x4_goal_moment_active()&&x4_goal_moment_step()==GOAL_STEP_MARINER_REST,"the twelfth delivery opens the Mariner Rest moment"))return false;
  capture("01-mariner-rest");
  key(X4_ACT_DOWN);key(X4_ACT_PAGE_NEXT);
  if(!check(x4_goal_moment_active()&&g_ui.scene==scene&&g_ui.selectedPort==port,"other keys cannot act on the screen underneath"))return false;
  key(X4_ACT_OK);
  if(!check(!x4_goal_moment_active()&&g_ui.scene==scene&&g_ui.card==CARD_NONE&&g_ui.selectedPort==port&&g_ui.mapZoom==zoom,
    "OK returns to exactly the screen it covered"))return false;
  frame();
  if(!check(!x4_goal_moment_active(),"a completed rung is celebrated once"))return false;
  // An arrival summary on screen holds the moment until it is read.
  g_ui.card=CARD_ARRIVAL;g_campaign.deliveredUnits=24;
  if(!check(sim_buy_ship(0),"fixture buys the second Sloop"))return false;
  frame();
  if(!check(!x4_goal_moment_active(),"an open arrival summary holds the moment back"))return false;
  g_ui.card=CARD_NONE;frame();
  if(!check(x4_goal_moment_active()&&x4_goal_moment_step()==GOAL_STEP_SECOND_SLOOP,"the second Sloop moment follows once the screen is calm"))return false;
  capture("02-second-sloop");
  key(X4_ACT_BACK);
  if(!check(!x4_goal_moment_active()&&g_ui.card==CARD_NONE,"BACK also dismisses the moment"))return false;
  // Another company arriving with its progress made is not celebrated.
  save_reset();sim_new_campaign(78);g_campaign.tutorialDismissed=1;g_campaign.coins=20000;
  g_campaign.deliveredUnits=60;g_campaign.completedVoyages=10;
  if(!check(sim_buy_ship(0),"fixture gives the new company a second ship"))return false;
  g_ships[1].classId=1;g_campaign.regionMask|=2u;
  g_ui.title=0;g_ui.scene=SCENE_FLEET;g_ui.card=CARD_NONE;frame();
  if(!check(!x4_goal_moment_active()&&voyage_goal_step()>=GOAL_STEP_REGION_DOCKS,"a new company does not replay its own progress"))return false;
  {
    const uint8_t region=sim_project_region();
    g_campaign.regionMask|=(uint8_t)(1u<<region);frame();
    if(!check(x4_goal_moment_active()&&x4_goal_moment_step()>=GOAL_STEP_REGION_DOCKS&&x4_goal_moment_region()==region,
      "a built dock opens its region's moment"))return false;
    capture("03-region");
    key(X4_ACT_OK);
  }
  g_campaign.regionMask=0x3F;g_campaign.won=0;frame();key(X4_ACT_OK);
  g_campaign.won=1;frame();
  if(!check(x4_goal_moment_active()&&x4_goal_moment_step()==GOAL_STEP_GRAND_CHARTER,"the Grand Charter has its own moment"))return false;
  capture("04-grand-charter");
  key(X4_ACT_OK);
  board_x4_test_buffered_input(false);
  ::printf("goal moment: PASS (%d checks)\n",checks);
  return true;
}
