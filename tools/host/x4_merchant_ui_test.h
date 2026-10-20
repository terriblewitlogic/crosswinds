#pragma once

static bool run_x4_merchant_standby_test() {
  int checks = 0;
  auto check = [&](bool ok, const char* label) {
    ++checks; ::printf("merchant standby: %s %s\n", ok ? "PASS" : "FAIL", label);
    return ok;
  };
  auto goods = [&]() {
    int n = 0;
    for (int c = 0; c < g_ships[0].cargoCount; ++c)
      if (g_ships[0].cargo[c].kind == CARGO_MARKET && g_ships[0].cargo[c].good == GOOD_TEXTILES)
        n += g_ships[0].cargo[c].quantity;
    return n;
  };
  save_reset(); board_x4_test_standby_lose_clock(); sim_new_campaign(42); g_ui.title = 0; g_campaign.onboardingStep = ONBOARD_DONE;
  g_ships[0].cargoCount = 0;  // without the starter gift, three goods and a parcel fill the Sloop
  uint8_t quantities[GOOD_COUNT]{}; quantities[GOOD_TEXTILES] = 3;
  auto purchase = sim_merchant_trade_offer(0, true, quantities);
  if (!check(purchase.code == SIM_OK && sim_merchant_trade_apply(0, purchase), "purchase three owned goods")) return false;
  PackageOffer package{};
  for (int i = 0; i < sim_package_count(0); ++i) {
    auto candidate = sim_package_offer(0, i);
    if (candidate.destination == 1) { package = candidate; break; }
  }
  if (!check(package.id && sim_package_accept(0, package), "accept a free package beside merchandise")) return false;
  auto sail = sim_merchant_sail_offer(0, 1);
  if (!check(sail.code == SIM_OK && sim_merchant_sail(0, sail), "begin merchant voyage")) return false;
  constexpr uint64_t epoch = 1000000000u;
  uint32_t rate = SIM_WORLD_MINUTES_PER_REAL_SECOND * g_campaign.timeScale;
  uint32_t duration = sail.minutes / rate + (sail.minutes % rate != 0);
  uint32_t half = duration / 2;
  int32_t sailingCash = g_campaign.coins;
  if (!check(board_x4_test_standby_begin(epoch, 7654), "persist a real board standby checkpoint")) return false;
  uint64_t halfway = epoch + (uint64_t)half * 1000000u;
  if (!check(board_x4_test_standby_resume(halfway, true, false) &&
        g_ships[0].state == SHIP_SAILING && goods() == 3 && g_campaign.coins == sailingCash &&
        g_campaign.standbyAccountedRealSeconds == half, "sleep advances sailing without selling or paying early")) return false;
  uint32_t partialWorld = g_campaign.worldMinutes;
  if (!check(save_read() && board_x4_test_standby_resume(halfway, true, false) &&
        g_campaign.worldMinutes == partialWorld && goods() == 3 && g_campaign.coins == sailingCash,
        "reload and repeated wake do not replay elapsed time")) return false;
  save_suspend(true);
  g_campaign.standbyAccountedRealSeconds = duration;
  sim_advance_minutes((duration - half) * rate);
  save_suspend(false);
  if (!check(save_read() && goods() == 3 && g_campaign.coins == sailingCash &&
        g_ships[0].state == SHIP_SAILING && g_campaign.standbyAccountedRealSeconds == half,
        "interrupted arrival commit restores cargo, cash, and clock together")) return false;
  uint64_t arrival = epoch + (uint64_t)duration * 1000000u;
  if (!check(board_x4_test_standby_resume(arrival, true, false) &&
        g_ships[0].merchant.state == MERCHANT_ARRIVED && goods() == 3 &&
        sim_ship_used_capacity(g_ships[0]) == 3 && g_campaign.coins == sailingCash + package.reward,
        "timer arrival pays exactly one package and keeps all trade goods")) return false;
  int32_t arrivalCash = g_campaign.coins; uint32_t arrivalWorld = g_campaign.worldMinutes;
  if (!check(save_read() && goods() == 3 && g_campaign.coins == arrivalCash &&
        g_ships[0].merchant.receiptUnread, "arrival and unread receipt survive reload")) return false;
  board_x4_test_standby_resume(arrival, true, true);
  board_x4_test_standby_resume(epoch + 86400000000ull, true, true);
  if (!check(goods() == 3 && g_campaign.coins == arrivalCash &&
        g_campaign.worldMinutes == arrivalWorld && g_ships[0].state != SHIP_SAILING,
        "later wakes never resell goods, repay packages, or redispatch")) return false;
  ::printf("merchant standby: PASS (%d checks; real board reconciliation and atomic saves)\n", checks);
  return true;
}

static bool run_x4_merchant_ui_test() {
  // A company from before the guided first voyage: the six-page tour is the
  // walkthrough here (tools/host/x4_onboarding_test.h covers onboarding).
  save_reset(); sim_new_campaign(42); g_campaign.onboardingStep = ONBOARD_DONE;
  board_x4_test_buffered_input(true);
  uint32_t sampler = 0; int checks = 0;
  auto check = [&](bool ok, const char* label) {
    ++checks; ::printf("merchant UI: %s %s (check%d card%u page%u cash%ld)\n",ok?"PASS":"FAIL",label,checks,g_ui.card,g_ui.page,(long)g_campaign.coins);
    ::fflush(stdout); return ok;
  };
  auto key = [&](uint8_t action) {
    input_update(); int button = x4_button_for(action);
    for (int i=0;i<7;++i,sampler+=10) board_x4_test_sample_buttons(sampler,(uint8_t)(1u<<button));
    for (int i=0;i<6;++i,sampler+=10) board_x4_test_sample_buttons(sampler,0);
    input_update();render_frame();input_update();
  };
  auto footer = [&](int id) {
    HitRegion want = voyage_footer_region(g_ui.card,id);
    for (int attempt=0;attempt<20;++attempt) {
      HitRegion have{};
      if (x4_focus_region(&have) && have.x==want.x && have.y==want.y && have.w==want.w) {key(X4_ACT_OK);return true;}
      key(X4_ACT_DOWN);
    }
    return false;
  };
  auto row = [&](int n) {if (!input_test_focus_row(n))return false; key(X4_ACT_OK);return true;};
  auto command = [&](uint8_t n) {if(!input_test_focus_command(n))return false;key(X4_ACT_OK);return true;};
  auto capture = [&](const char* name) {
    const char* dir=getenv("SH_X4_MERCHANT_DIR");if(!dir)return;
    ::mkdir(dir,0755);char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);
    render_frame();dump_ppm_native(path);
  };
  auto owned = [&](uint8_t good) {int n=0;for(int c=0;c<g_ships[0].cargoCount;++c)if(g_ships[0].cargo[c].kind==CARGO_MARKET &&g_ships[0].cargo[c].good==good)n+=g_ships[0].cargo[c].quantity;return n;};
  render_frame();key(X4_ACT_OK);
  if(!check(g_ui.card==CARD_TUTORIAL && g_ui.page==0,"first title confirmation opens tutorial"))return false;
  for(int page=0;page<MERCHANT_TUTORIAL_PAGES;++page) {
    if(!check(g_ui.card==CARD_TUTORIAL && g_ui.page==page,"tutorial advances one screen at a time"))return false;
    char name[40];snprintf(name,sizeof(name),"tutorial-%d",page+1);capture(name);
    if(page==1) {
      key(X4_ACT_BACK);if(!check(g_ui.page==0 && g_ui.card==CARD_TUTORIAL,"Back revisits previous tutorial screen"))return false;
      key(X4_ACT_PAGE_NEXT);if(!check(g_ui.page==1,"rocker browses tutorial without switching main tabs"))return false;
    }
    key(X4_ACT_OK);
  }
  if(!check(g_ui.card==CARD_NONE && g_campaign.tutorialDismissed && g_campaign.coins==500 && owned(GOOD_GRAIN)==2,
    "finishing tutorial reaches Fleet without a transaction"))return false;
  if(!check(save_read() && g_campaign.tutorialDismissed && owned(GOOD_GRAIN)==2,"tutorial completion survives reload"))return false;
  g_ui.title=1;key(X4_ACT_OK);
  if(!check(g_ui.card!=CARD_TUTORIAL,"completed tutorial is not shown again on startup"))return false;
  g_ui.card=CARD_HELP;g_ui.page=0;input_update();
  input_test_touch(CARD_FOOTER[0].x+20,CARD_FOOTER[0].y+20);input_update();render_frame();
  if(!check(g_ui.card==CARD_TUTORIAL && g_ui.page==0,"Help replays tutorial from the beginning"))return false;
  key(X4_ACT_DOWN);key(X4_ACT_OK);
  if(!check(g_ui.card==CARD_HELP && g_campaign.tutorialDismissed,"Skip from a replay returns to Help"))return false;
  key(X4_ACT_BACK);
  if(!check(g_campaign.economyVersion==2 &&g_ui.scene==SCENE_FLEET,"new company starts merchant economy"))return false;
  key(X4_ACT_PAGE_NEXT);
  if(!check(g_ui.scene==SCENE_WORLD,"rocker selects World tab"))return false;
  uint8_t priorPort=g_ui.selectedPort;
  if(!check(command(X4_CMD_PAGE_NEXT)&&g_ui.selectedPort!=priorPort,"explicit next-port control browses map ports"))return false;
  key(X4_ACT_PAGE_NEXT); key(X4_ACT_PAGE_NEXT);
  if(!check(g_ui.scene==SCENE_FLEET,"rocker cycles Company and Fleet tabs"))return false;
  capture("01-fleet");
  if(!check(command(X4_CMD_SHIP_0)&&g_ui.card==CARD_SHIP,"ready boat opens neutral ship overview"))return false;
  capture("01b-ship");
  if(!check(row(2)&&g_ui.card==CARD_SHIPYARD&&g_ui.page==1,"ship Upgrades opens only that task"))return false;
  key(X4_ACT_PAGE_NEXT);
  if(!check(g_ui.page==1,"isolated upgrades cannot switch into ship buying"))return false;
  key(X4_ACT_BACK);
  if(!check(g_ui.card==CARD_SHIP,"Back from upgrades returns to ship"))return false;
  if(!check(row(0)&&g_ui.card==CARD_TRADE&&x4_merchant_mode()==0,"Trade opens Buy for the partially loaded starter ship"))return false;
  if(!check(merchant_tab_region(0).x < merchant_tab_region(1).x && merchant_tab_region(1).x < merchant_tab_region(2).x,
    "tabs appear Buy, Sell, Parcels from left to right"))return false;
  key(X4_ACT_PAGE_NEXT);
  if(!check(x4_merchant_mode()==1,"right rocker chooses Sell for the starter gift"))return false;
  capture("01c-starter-sell");
  if(!check(row(0)&&x4_merchant_trade_quote().quantity==2,"starter Grain starts at everything aboard for sale"))return false;
  key(X4_ACT_UP);
  if(!check(x4_merchant_trade_quote().quantity==2&&g_ui.noticeCode!=SIM_CARGO_UNAVAILABLE,"UP past everything aboard stops quietly"))return false;
  key(X4_ACT_BACK);
  if(!check(owned(GOOD_GRAIN)==2&&g_campaign.coins==500,"cancelling starter sale keeps gift aboard"))return false;
  row(0); key(X4_ACT_DOWN);
  auto starterSale=x4_merchant_trade_quote();
  if(!check(starterSale.quantity==1,"DOWN trims the starter sale to one unit"))return false;
  key(X4_ACT_OK);
  if(!check(owned(GOOD_GRAIN)==1&&g_campaign.coins==500+starterSale.cashDelta,
    "first sale earns its quoted cash and leaves remaining Grain"))return false;
  input_update(); row(0); key(X4_ACT_OK);
  if(!check(!owned(GOOD_GRAIN)&&!g_campaign.deliveredUnits,"selling starter gift does not invent an import"))return false;
  // Continue the existing purchase fixture with its original cash baseline.
  g_campaign.coins=500; g_campaign.marketProfit=0;
  key(X4_ACT_PAGE_NEXT);
  if(!check(x4_merchant_mode()==2,"right rocker advances Sell to Parcels"))return false;
  key(X4_ACT_PAGE_NEXT);
  if(!check(x4_merchant_mode()==0,"right rocker wraps Parcels to Buy"))return false;
  key(X4_ACT_PAGE_PREV);
  if(!check(x4_merchant_mode()==2,"left rocker wraps Buy to Parcels"))return false;
  key(X4_ACT_PAGE_NEXT);
  capture("02-buy");
  int textiles=-1;for(int r=0;r<2;++r)if(x4_merchant_good(r)==GOOD_TEXTILES)textiles=r;
  if(!check(textiles>=0&&input_test_focus_row(textiles),"Makes shelf includes Textiles"))return false;
  key(X4_ACT_OK);
  const uint8_t mostTextiles=x4_merchant_trade_quote().quantity;
  key(X4_ACT_UP);
  if(!check(mostTextiles>=3&&x4_merchant_trade_quote().quantity==mostTextiles,"Buy starts at the most the hold and purse allow"))return false;
  for(int i=0;i<=mostTextiles;++i) key(X4_ACT_DOWN);
  if(!check(!x4_merchant_trade_quote().quantity,"quantity stops at zero"))return false;
  key(X4_ACT_OK);
  if(!check(x4_merchant_editing()&&g_campaign.coins==500,"zero quantity cannot commit"))return false;
  key(X4_ACT_UP); key(X4_ACT_UP);
  key(X4_ACT_BACK);
  if(!check(!x4_merchant_editing() && !x4_merchant_trade_quote().quantity && g_campaign.coins==500,"Back cancels item quantity without spending"))return false;
  input_test_focus_row(textiles); key(X4_ACT_OK); key(X4_ACT_PAGE_NEXT);
  if(!check(x4_merchant_mode()==1&&!x4_merchant_editing()&&!x4_merchant_trade_quote().quantity&&g_campaign.coins==500,"switching tabs discards unconfirmed quantity"))return false;
  key(X4_ACT_PAGE_PREV);
  input_test_focus_row(textiles); key(X4_ACT_OK);
  while(x4_merchant_trade_quote().quantity>3) key(X4_ACT_DOWN);
  MerchantTradeQuote buy=x4_merchant_trade_quote();
  if(!check(buy.code==SIM_OK&&buy.quantity==3&&g_campaign.coins==500&&!owned(GOOD_TEXTILES),"quantity edits quote three owned goods without spending"))return false;
  capture("03-buy-three");
  if(!check(footer(1)&&owned(GOOD_TEXTILES)==3&&g_campaign.coins==500+buy.cashDelta,"Buy commits exact purchase and keeps goods aboard"))return false;
  int32_t purchaseCash=g_campaign.coins;
  key(X4_ACT_PAGE_PREV);
  if(!check(g_ui.card==CARD_PACKAGES,"rocker switches Buy to Parcels"))return false;
  capture("04-packages");
  int selected=-1;PackageOffer parcel{};
  for(int page=0;page<8&&selected<0;++page) {
    for(int r=0;r<2;++r) {PackageOffer q=x4_merchant_package(r);if(q.id&&q.destination==1) {selected=r;parcel=q;break;}}
    if(selected<0)row(6);
  }
  if(!check(selected>=0&&parcel.code==SIM_OK,"free Gullhaven package available"))return false;
  if(!check(row(selected)&&x4_merchant_editing(),"OK opens parcel review without accepting"))return false;
  capture("04b-parcel-review"); key(X4_ACT_OK);
  if(!check(sim_ship_used_capacity(g_ships[0])==4&&g_campaign.coins==purchaseCash,"package accepts free and uses exactly one slot"))return false;
  bool packageClean=false;
  for(int c=0;c<g_ships[0].cargoCount;++c) {const CargoItem& item=g_ships[0].cargo[c];if(item.jobKind==JOB_PACKAGE)packageClean=item.good==NO_PORT&&item.quantity==1&&item.paidUnitPrice==0;}
  if(!check(packageClean,"package has no merchandise good or purchase price"))return false;
  key(X4_ACT_BACK);
  if(!check(g_ui.card==CARD_SHIP && row(1)&&g_ui.card==CARD_VOYAGE&&g_ui.page==0,"Sail is separate from trading on the ship overview"))return false;
  capture("05-destinations");
  int destination=-1;for(int r=0;r<2;++r)if(x4_voyage_destination(r)==1)destination=r;
  if(!check(destination>=0&&row(destination)&&g_ui.page==2,"destination opens free sailing review"))return false;
  MerchantSailQuote sail=x4_merchant_sail_quote();
  if(!check(sail.code==SIM_OK&&sail.packagePayout==parcel.reward,"review distinguishes fixed package payout from sale estimate"))return false;
  capture("06-sail-review");
  if(!check(footer(1)&&g_ui.card==CARD_NONE&&x4_watch_active()&&g_campaign.coins==purchaseCash&&owned(GOOD_TEXTILES)==3,"Sail is free and never prepays a goods sale"))return false;
  capture("06b-departure");
  if(!check(g_ui.scene==SCENE_WORLD&&x4_watch_active(),"Watch follows merchant voyage"))return false;
  key(X4_ACT_BACK);
  if(!check(g_ui.scene==SCENE_FLEET && g_ui.card==CARD_NONE && !x4_watch_active(),"Back after sailing returns to Fleet without reopening the committed quote"))return false;
  if(!check(command(X4_CMD_SHIP_0)&&voyage_watch_screen(),"Fleet reopens the sailing ship"))return false;
  capture("07-watch");
  sim_advance_minutes(sail.minutes);render_frame();input_update();
  if(!check(g_ships[0].state==SHIP_ARRIVED&&owned(GOOD_TEXTILES)==3,"arrival retains every owned trade good"))return false;
  if(!check(g_campaign.coins==purchaseCash+parcel.reward&&sim_ship_used_capacity(g_ships[0])==3,"only the completed package pays at arrival"))return false;
  if(!check(voyage_watch_screen(),"arrival stays on Watch until Open ship"))return false;
  capture("08a-arrived-watch");
  if(!check(command(X4_CMD_WATCH_READY),"Open ship is the explicit arrival action"))return false;
  capture("08-arrival");
  if(!check(g_ui.card==CARD_SHIP,"arrival opens neutral ship overview"))return false;
  if(!check(row(0)&&g_ui.card==CARD_TRADE&&x4_merchant_mode()==0&&owned(GOOD_TEXTILES)==3,"parcel delivery frees a slot so arrival Trade defaults to Buy"))return false;
  if(!check(!g_ships[0].merchant.receiptUnread && g_ships[0].returnLedger.seen.legacyArrivals == g_ships[0].returnLedger.total.legacyArrivals,
      "reading the sole merchant receipt acknowledges its exact arrival"))return false;
  key(X4_ACT_PAGE_NEXT);
  capture("09-sell");
  if(!check(input_test_focus_row(0),"owned cargo available to sell"))return false;
  key(X4_ACT_OK);
  if(!check(x4_merchant_trade_quote().quantity == 3, "Sell starts at all three units aboard"))return false;
  key(X4_ACT_DOWN); key(X4_ACT_DOWN);
  auto one = x4_merchant_trade_quote(); int32_t singleCash = g_campaign.coins;
  if(!check(one.quantity == 1 && footer(1) && owned(GOOD_TEXTILES) == 2 && g_campaign.coins == singleCash + one.cashDelta,
      "sell one unit and retain the other two"))return false;
  input_test_focus_row(0);
  key(X4_ACT_OK); key(X4_ACT_UP);
  if(!check(x4_merchant_trade_quote().quantity == 2, "Sell cannot exceed the remaining owned units"))return false;
  MerchantTradeQuote sale=x4_merchant_trade_quote();
  if(!check(sale.quantity==2&&sale.code==SIM_OK&&sale.profit>0,"actual destination quote shows positive resale profit"))return false;
  capture("10-sell-review");
  int32_t beforeSale=g_campaign.coins;
  if(!check(footer(1)&&!owned(GOOD_TEXTILES)&&g_campaign.coins==beforeSale+sale.cashDelta,"explicit Sell banks exactly today's quote"))return false;
  if(!check(g_campaign.deliveredUnits>=3,"resold imports advance company progression"))return false;
  key(X4_ACT_BACK);
  if(!check(g_ui.card==CARD_SHIP,"Back from trading returns to ship"))return false;
  key(X4_ACT_BACK);
  if(!check(voyage_watch_screen(),"Back from a ship opened on Watch returns to that journey"))return false;
  key(X4_ACT_BACK);
  if(!check(g_ui.card==CARD_NONE&&g_ui.scene==SCENE_FLEET,"Watch Back returns to Fleet"))return false;
  if(!check(command(X4_CMD_COMPANY),"Company remains reachable"))return false;
  capture("11-company");
  if (x4_return_report()) key(X4_ACT_BACK);
  if (g_ui.scene != SCENE_COMPANY) command(X4_CMD_COMPANY);
  // Migrated companies also use the new market through Company Details.
  // Old contract/custom-route entry points must not reintroduce a second loop.
  uint16_t retainedFeatures = g_campaign.legacyFeatureMask;
  g_campaign.legacyFeatureMask = 0xffff;
  bool hasOldLoop = false;
  for (int i = 0; i < VOYAGE_DETAIL_COUNT; ++i)
    if (voyage_detail_available(i) && (VOYAGE_DETAIL_CARDS[i] == CARD_JOBS || VOYAGE_DETAIL_CARDS[i] == CARD_ROUTE || VOYAGE_DETAIL_CARDS[i] == CARD_MARKET || VOYAGE_DETAIL_CARDS[i] == CARD_FLEET)) hasOldLoop = true;
  if (!check(!hasOldLoop, "retained Company tools cannot reintroduce legacy contracts or paid routing")) return false;
  g_campaign.legacyFeatureMask = retainedFeatures;
  if (!check(command(X4_CMD_LEDGER) && g_ui.card == CARD_MORE, "Company Details remains reachable")) return false;
  int marketRow = -1;
  for (int r = 0; r < CARD_VISIBLE_ROWS; ++r) {
    int item = voyage_detail_index(r);
    if (item >= 0 && VOYAGE_DETAIL_CARDS[item] == CARD_MARKET) marketRow = r;
  }
  if (!check(marketRow < 0, "Company management does not duplicate ship trading")) return false;
  g_campaign.legacyFeatureMask = 0xffff;
  g_ui.card = CARD_SHIPYARD; g_ui.page = 0; input_update();
  key(X4_ACT_PAGE_NEXT);
  if(!check(g_ui.page==2,"Company yard skips ship-specific fittings"))return false;
  key(X4_ACT_PAGE_NEXT);
  if(!check(g_ui.page==0,"Company yard returns to buying ships"))return false;
  key(X4_ACT_BACK); command(X4_CMD_FLEET); command(X4_CMD_SHIP_0);
  g_campaign.coins = 4000;
  if(!check(row(2) && g_ui.card==CARD_SHIPYARD && g_ui.page==1,"Ship retains access to fitting shop"))return false;
  capture("19-upgrades");
  if(!check(row(0) && g_ui.card==CARD_VOYAGE && g_ui.page==5 && row(0) && g_ui.page==4,
      "fitting selection leads through a slot to its review"))return false;
  capture("20-fitting-review");
  int32_t fittingCash = g_campaign.coins;
  if(!check(footer(1) && g_campaign.coins < fittingCash && g_ui.card==CARD_SHIPYARD && g_ui.page==1,
      "fitting purchase returns to Ship Upgrades instead of Company"))return false;
  key(X4_ACT_BACK);
  if(!check(g_ui.card==CARD_SHIP,"Back after a fitting purchase returns to the same ship"))return false;
  g_campaign.legacyFeatureMask = retainedFeatures;
  // Arrive with a full merchandise hold, then check explicit tab choice and
  // default recalculation after one unit is sold. Use real transactions/travel.
  g_ui.card=CARD_NONE;g_ui.scene=SCENE_FLEET;g_ui.selectedShip=0;
  g_campaign.coins=5000;g_ui.returnQuiet=1;
  uint8_t load[GOOD_COUNT]{};load[PORT_DEFS[g_ships[0].currentPort].produced[0]]=sim_ship_capacity(g_ships[0]);
  auto fullPurchase=sim_merchant_trade_offer(0,true,load);
  if(!check(sim_merchant_trade_apply(0,fullPurchase),"full-arrival fixture buys to the fitted ship capacity"))return false;
  auto fullVoyage=sim_merchant_sail_offer(0,0);
  if(!check(sim_merchant_sail(0,fullVoyage),"full cargo sails home"))return false;
  g_ships[0].incidentArmed=0;sim_advance_minutes(fullVoyage.minutes);
  int32_t fullCash=g_campaign.coins;
  if(!check(save_write()&&save_read()&&sim_ship_used_capacity(g_ships[0])==sim_ship_capacity(g_ships[0]),
    "full arrival survives reload"))return false;
  g_ui.title=0;g_ui.card=CARD_NONE;g_ui.scene=SCENE_FLEET;input_update();
  if(!check(command(X4_CMD_SHIP_0)&&g_ui.card==CARD_SHIP&&row(0)&&x4_merchant_mode()==1&&g_campaign.coins==fullCash,
    "full arrival Trade opens Sell with no automatic sale"))return false;
  capture("22-full-arrival-sell");
  key(X4_ACT_PAGE_PREV);
  if(!check(x4_merchant_mode()==0,"full ship can explicitly choose Buy with the left rocker"))return false;
  key(X4_ACT_PAGE_NEXT);row(0);
  while(x4_merchant_trade_quote().quantity>1) key(X4_ACT_DOWN);
  key(X4_ACT_OK);
  if(!check(x4_merchant_mode()==1&&sim_ship_used_capacity(g_ships[0])==sim_ship_capacity(g_ships[0])-1,
    "selling one unit keeps the current Sell tab"))return false;
  key(X4_ACT_BACK);
  if(!check(g_ui.card==CARD_SHIP&&row(0)&&x4_merchant_mode()==0,
    "reopening Trade defaults to Buy once space is available"))return false;
  // Optional reference cards are rendered directly for copy/layout inspection.
  // The transaction and travel flow above uses actual queued button actions.
  g_ui.card = CARD_HELP; g_ui.dirty = 1; capture("17-merchant-notes");
  g_ui.card = CARD_TUTORIAL; g_ui.dirty = 1; capture("18-merchant-tutorial");
  g_ui.card = CARD_VOYAGE_DEPARTURE; g_ui.page = 0; g_ui.dirty = 1; capture("21-legacy-departure");
  board_x4_test_buffered_input(false);
  ::printf("merchant UI: PASS (%d checks; buy/package/carry/manual-sell/Watch/progression)\n",checks);
  return true;
}
