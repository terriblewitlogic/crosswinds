#pragma once
static bool run_x4_navigation_test() {
  int checks=0;uint32_t sampler=0;
  auto check=[&](bool ok,const char* text){++checks;printf("navigation: %s %s (card %u scene %u page %u ship %u port %u)\n",ok?"PASS":"FAIL",text,g_ui.card,g_ui.scene,g_ui.page,g_ui.selectedShip,g_ui.selectedPort);fflush(stdout);return ok;};
  auto frame=[&](){input_update();render_frame();};
  auto key=[&](uint8_t action){input_update();int button=x4_button_for(action);for(int i=0;i<7;++i,sampler+=10)board_x4_test_sample_buttons(sampler,1u<<button);for(int i=0;i<6;++i,sampler+=10)board_x4_test_sample_buttons(sampler,0);input_update();render_frame();input_update();};
  auto command=[&](uint8_t cmd){if(!input_test_focus_command(cmd))return false;key(X4_ACT_OK);return true;};
  auto row=[&](int r){if(!input_test_focus_row(r))return false;key(X4_ACT_OK);return true;};
  auto capture=[&](const char* name){const char* dir=getenv("SH_X4_NAVIGATION_DIR");if(!dir)return;::mkdir(dir,0755);char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);render_frame();dump_ppm_native(path);};
  save_reset();sim_new_campaign(42);g_campaign.tutorialDismissed=1;g_campaign.coins=20000;g_campaign.deliveredUnits=60;g_campaign.completedVoyages=10;
  g_ui.title=0;g_ui.scene=SCENE_FLEET;g_ui.card=CARD_NONE;board_x4_test_buffered_input(true);frame();
  key(X4_ACT_BACK);if(!check(g_ui.card==CARD_MENU,"Fleet root Back opens Settings"))return false;
  key(X4_ACT_BACK);if(!check(g_ui.card==CARD_NONE && g_ui.scene==SCENE_FLEET,"Settings Back restores Fleet"))return false;
  command(X4_CMD_MAP);uint8_t ship=g_ui.selectedShip;
  for(int i=0;i<MAX_PORTS && g_ui.selectedPort!=1;++i)command(X4_CMD_PAGE_NEXT);
  if(!check(g_ui.selectedPort==1 && g_ui.selectedShip==ship,"map port browsing does not select a different ship"))return false;
  g_ui.mapZoom=1;
  for(uint8_t level : {2,4,6,8,12,1}) {
    command(X4_CMD_ZOOM);
    if(!check(g_ui.mapZoom==level,"map zoom cycles through all six levels"))return false;
    char name[32];snprintf(name,sizeof(name),"zoom-%u",level);capture(name);
  }
  command(X4_CMD_ZOOM);uint8_t zoom=g_ui.mapZoom,fx=g_ui.mapFocusX,fy=g_ui.mapFocusY;
  command(X4_CMD_PORT);capture("port");
  if(!check(g_ui.scene==SCENE_PORT && x4_column_slot(X4_CMD_FLEET)<0 && x4_column_slot(X4_CMD_BACK)>=0,"Port is a child with Back, not another tab hub"))return false;
  command(X4_CMD_MARKET);capture("prices");
  if(!check(g_ui.card==CARD_PORT_PRICES && g_ui.selectedPort==1,"remote port opens its own read-only prices"))return false;
  int32_t cash=g_campaign.coins;key(X4_ACT_OK);key(X4_ACT_BACK);
  if(!check(g_ui.scene==SCENE_PORT && g_ui.card==CARD_NONE && g_campaign.coins==cash,"price browsing cannot buy or change ship context"))return false;
  command(X4_CMD_CARGO);capture("ships-empty");
  if(!check(g_ui.card==CARD_PORT_SHIPS && x4_port_ship(0)==NO_SHIP,"empty port cannot fall back to the previously selected ship"))return false;
  key(X4_ACT_BACK);command(X4_CMD_LEDGER);capture("incoming-empty");
  if(!check(g_ui.card==CARD_PORT_SHIPS && g_ui.captainView==1 && x4_port_ship(0)==NO_SHIP,"Incoming is scoped to this port"))return false;
  key(X4_ACT_BACK);key(X4_ACT_BACK);
  if(!check(g_ui.scene==SCENE_WORLD && g_ui.card==CARD_NONE && g_ui.selectedPort==1 && g_ui.mapZoom==zoom && g_ui.mapFocusX==fx && g_ui.mapFocusY==fy,"Back restores the map port, zoom and position"))return false;
  key(X4_ACT_BACK);if(!check(g_ui.card==CARD_MENU,"World root Back opens Settings"))return false;
  key(X4_ACT_BACK);if(!check(g_ui.scene==SCENE_WORLD && g_ui.card==CARD_NONE,"Settings Back restores World"))return false;
  command(X4_CMD_FLEET);
  g_ui.selectedPort=0;
  if(!check(sim_buy_ship(0),"fixture creates a second local ship"))return false;
  command(X4_CMD_MAP);for(int i=0;i<MAX_PORTS && g_ui.selectedPort!=0;++i)command(X4_CMD_PAGE_NEXT);
  command(X4_CMD_PORT);command(X4_CMD_CARGO);capture("ships-here");
  if(!check(x4_port_ship(1)==1 && row(1) && g_ui.card==CARD_SHIP && g_ui.selectedShip==1,"port list opens the explicitly chosen second ship"))return false;
  row(0);if(!check(g_ui.card==CARD_TRADE && g_ui.selectedShip==1,"Trade keeps the chosen ship"))return false;
  key(X4_ACT_BACK);if(!check(g_ui.card==CARD_SHIP && g_ui.selectedShip==1,"Trade Back returns to that ship"))return false;
  key(X4_ACT_BACK);if(!check(g_ui.card==CARD_PORT_SHIPS && g_ui.selectedPort==0,"Ship Back returns to the port list that opened it"))return false;
  key(X4_ACT_BACK);key(X4_ACT_BACK);command(X4_CMD_FLEET);
  command(X4_CMD_COMPANY);command(X4_CMD_LEDGER);capture("company-tools");
  // Every available tool must return to the same Company tools page.
  for(int wanted=0;wanted<VOYAGE_DETAIL_COUNT;++wanted) {
    if(!voyage_detail_available(wanted))continue;
    int target=VOYAGE_DETAIL_CARDS[wanted];int visible=0;
    while(voyage_detail_index(visible)!=wanted)++visible;
    g_ui.page=visible/CARD_VISIBLE_ROWS;frame();uint8_t parentPage=g_ui.page;
    if(!check(row(visible%CARD_VISIBLE_ROWS),"Company tool has a visible entry"))return false;
    char name[48];snprintf(name,sizeof(name),"tool-%u",target);capture(name);
    key(X4_ACT_BACK);
    if(!check(g_ui.card==CARD_MORE && g_ui.page==parentPage,"tool Back restores its parent page"))return false;
    (void)target;
  }
  // Nested Help and Ledger branches use the same parent rules.
  auto tool=[&](uint8_t card){for(int i=0;i<voyage_detail_count();++i)if(VOYAGE_DETAIL_CARDS[voyage_detail_index(i)]==card){g_ui.page=i/CARD_VISIBLE_ROWS;frame();return row(i%CARD_VISIBLE_ROWS);}return false;};
  if(!check(tool(CARD_MANIFEST) && g_ui.card==CARD_PORT_SHIPS && g_ui.captainView==2,"Company cargo first asks which ship"))return false;
  if(!check(row(1) && g_ui.card==CARD_MANIFEST && g_ui.selectedShip==1,"cargo inspection belongs to the chosen ship"))return false;
  capture("cargo-chosen");key(X4_ACT_BACK);
  if(!check(g_ui.card==CARD_PORT_SHIPS,"cargo Back returns to ship selection"))return false;
  key(X4_ACT_BACK);
  if(!check(tool(CARD_SHIPYARD) && g_ui.card==CARD_PORT_PICKER,"Company shipyard first asks which port"))return false;
  if(!check(row(1) && g_ui.card==CARD_SHIPYARD && g_ui.selectedPort==1,"shipyard belongs to the explicitly chosen port"))return false;
  capture("yard-chosen");key(X4_ACT_BACK);
  if(!check(g_ui.card==CARD_PORT_PICKER,"yard Back returns to port selection"))return false;
  key(X4_ACT_BACK);
  if(tool(CARD_CAPTAINS)) {
    if(!check(row(0) && g_ui.captainView==1,"captain list opens a dossier"))return false;
    input_test_touch(CARD_FOOTER[0].x+20,CARD_FOOTER[0].y+20);frame();
    if(!check(g_ui.card==CARD_PORT_SHIPS && g_ui.captainView==4,"captain assignment asks for a named ship"))return false;
    capture("captain-ship");key(X4_ACT_BACK);
    if(!check(g_ui.card==CARD_CAPTAINS && g_ui.captainView==1,"assignment Back returns to the dossier"))return false;
    key(X4_ACT_BACK);if(!check(g_ui.card==CARD_CAPTAINS && !g_ui.captainView,"dossier Back returns to captain list"))return false;
    input_test_touch(CARD_FOOTER[1].x+20,CARD_FOOTER[1].y+20);frame();
    if(!check(g_ui.card==CARD_PORT_PICKER && g_ui.captainView==1,"Recruit asks which port"))return false;
    capture("recruit-port");row(0);
    if(!check(g_ui.card==CARD_CAPTAINS && g_ui.captainView==2,"chosen port opens recruitment"))return false;
    capture("recruit-chosen");key(X4_ACT_BACK);
    if(!check(g_ui.card==CARD_PORT_PICKER,"Recruit Back returns to its port selector"))return false;
    key(X4_ACT_BACK);if(!check(g_ui.card==CARD_CAPTAINS && !g_ui.captainView,"port selection Back returns to captain list"))return false;
    key(X4_ACT_BACK);
  }
  if(!check(tool(CARD_HELP),"Help opens from Company tools"))return false;
  input_test_touch(CARD_FOOTER[0].x+20,CARD_FOOTER[0].y+20);frame();
  if(!check(g_ui.card==CARD_TUTORIAL,"Help opens tutorial as a child"))return false;
  input_test_touch(CARD_FOOTER[1].x+20,CARD_FOOTER[1].y+20);frame();
  if(!check(g_ui.card==CARD_HELP,"leaving replay returns to Help"))return false;
  key(X4_ACT_BACK);tool(CARD_LEDGER);
  input_test_touch(CARD_FOOTER[1].x+20,CARD_FOOTER[1].y+20);frame();
  if(!check(g_ui.card==CARD_JOURNAL,"Ledger opens Journal"))return false;
  key(X4_ACT_BACK);if(!check(g_ui.card==CARD_LEDGER,"Journal Back returns to Ledger"))return false;
  key(X4_ACT_BACK);key(X4_ACT_BACK);
  if(!check(g_ui.scene==SCENE_COMPANY && g_ui.card==CARD_NONE,"Company tools Back returns to Company"))return false;
  key(X4_ACT_BACK);if(!check(g_ui.card==CARD_MENU,"Company root Back opens Settings"))return false;
  key(X4_ACT_BACK);if(!check(g_ui.scene==SCENE_COMPANY && g_ui.card==CARD_NONE,"Settings Back restores Company"))return false;
  command(X4_CMD_FLEET);
  command(X4_CMD_COMPANY);command(X4_CMD_LEDGER);
  attention_add(ATTENTION_ARRIVAL,0,0,false);
  if(!check(tool(CARD_INBOX),"Notices opens from tools"))return false;
  row(0);
  if(!check(g_ui.card==CARD_SHIP && g_ui.selectedShip==0,"arrival notice opens its named ship overview"))return false;
  key(X4_ACT_BACK);if(!check(g_ui.card==CARD_INBOX,"notice destination Back returns to Notices"))return false;
  key(X4_ACT_BACK);key(X4_ACT_BACK);command(X4_CMD_FLEET);
  g_campaign.regionMask|=2u;command(X4_CMD_COMPANY);command(X4_CMD_YARD);
  if(voyage_needs_range()) {
    input_test_touch(CARD_FOOTER[1].x+20,CARD_FOOTER[1].y+20);frame();
    if(!check(g_ui.card==CARD_PORT_SHIPS && g_ui.captainView==3,"range goal asks which sloop"))return false;
    capture("range-ship");key(X4_ACT_BACK);
    if(!check(g_ui.card==CARD_PROGRESS,"range selector Back returns to goal"))return false;
  }
  key(X4_ACT_BACK);command(X4_CMD_FLEET);
  // An incoming voyage can arrive while viewed; Back keeps the same port list.
  auto sail=sim_merchant_sail_offer(1,1);if(!check(sim_merchant_sail(1,sail),"incoming fixture dispatches second ship"))return false;
  g_ships[1].incidentArmed=0;
  command(X4_CMD_MAP);for(int i=0;i<MAX_PORTS && g_ui.selectedPort!=1;++i)command(X4_CMD_PAGE_NEXT);
  command(X4_CMD_PORT);command(X4_CMD_LEDGER);capture("incoming");
  if(!check(x4_port_ship(0)==1 && row(0) && x4_watch_active() && g_ui.selectedShip==1,"incoming row watches its named voyage"))return false;
  capture("incoming-watch");sim_advance_minutes(30000);frame();key(X4_ACT_BACK);
  if(!check(g_ui.card==CARD_PORT_SHIPS && g_ui.selectedPort==1 && x4_port_ship(0)==NO_SHIP,"after arrival Back refreshes the same incoming list"))return false;
  key(X4_ACT_BACK);key(X4_ACT_BACK);command(X4_CMD_FLEET);
  command(X4_CMD_MAP);for(int i=0;i<MAX_PORTS && g_ui.selectedPort!=0;++i)command(X4_CMD_PAGE_NEXT);
  command(X4_CMD_PORT);command(X4_CMD_CARGO);row(0);row(1);row(0);
  if(!check(g_ui.card==CARD_VOYAGE && g_ui.page==2,"port-origin ship can review a voyage"))return false;
  input_test_touch(voyage_footer_region(CARD_VOYAGE,1).x+20,voyage_footer_region(CARD_VOYAGE,1).y+20);frame();
  if(!check(voyage_watch_screen() && g_ships[0].state==SHIP_SAILING,"confirmed departure opens Watch"))return false;
  key(X4_ACT_BACK);
  if(!check(g_ui.card==CARD_PORT_SHIPS && g_ui.selectedPort==0,"departure Back returns to the originating port list, never the committed quote"))return false;
  board_x4_test_buffered_input(false);printf("navigation: PASS (%d checks)\n",checks);return true;
}
