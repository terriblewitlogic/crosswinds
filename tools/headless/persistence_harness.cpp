#include "game.h"
#include "economy_fixtures.h"
#include <LittleFS.h>
#include <cstdio>
void audio_begin() {}
void audio_tick() {}
void audio_notice() {}
void audio_alert() {}
static int failures;
static void check(bool ok, const char* message) {
  if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
static bool corrupt_current() {
  File f = LittleFS.open("/sh_company.sav", "r+");
  uint8_t byte = 0;
  bool ok = f && f.seek(60) && f.read(&byte, 1) == 1;
  byte ^= 0xA5;
  ok &= f.seek(60) && f.write(&byte, 1) == 1;
  f.flush(); f.close(); return ok;
}
static void booked_round_trip() {
  save_reset(); historical_campaign(51, 1);
  // Explicit historical signed-order fixture; normal new companies use v2.
  check(save_write(), "initial company commits");
  VoyageQuote freight = sim_voyage_offer(0, 1, VOYAGE_FREIGHT, GOOD_TEXTILES, 4);
  check(freight.code == SIM_OK && freight.payNow == 40 && freight.arrivalCoins == 100,
    "opening freight uses reviewed terms");
  check(sim_voyage_dispatch(0, freight), "signed freight commits");
  uint32_t id = g_ships[0].booking.id;
  check(save_read() && g_ships[0].booking.id == id && g_campaign.coins == 460 &&
    g_ships[0].cargoCount == 1 && g_ports[1].bookingUsed[GOOD_TEXTILES] == 4,
    "dispatch reload keeps cash, order, cargo and buyer reservation together");
  sim_advance_minutes(freight.minutes);
  check(save_read() && g_campaign.coins == 560 && g_campaign.deliveredUnits == 4 &&
    g_ships[0].booking.state == BOOKING_SETTLED && g_ships[0].booking.receiptUnread &&
    g_ships[0].booking.profit == 60 && g_ships[0].booking.arrivalCoins == 100 &&
    !g_ships[0].cargoCount, "automatic arrival receipt is durable with its payment");
  for (int i = 0; i < 8; ++i) {
    check(save_read(), "repeated settled arrival loads");
    sim_advance_minutes(60);
    check(g_campaign.coins == 560 && g_campaign.deliveredUnits == 4,
      "revisiting arrival never pays or awards progress twice");
  }
  // The real loads above migrate to v2. Resume only this historical test's
  // signed-price model before constructing its second promised order.
  g_campaign.economyVersion = 1;
  VoyageQuote trade = sim_voyage_offer(0, 0, VOYAGE_TRADE, GOOD_GRAIN, 4);
  check(trade.code == SIM_OK && trade.payNow == 104 && trade.arrivalCoins == 168,
    "return booked trade uses reviewed terms");
  check(sim_voyage_dispatch(0, trade), "booked trade commits");
  g_campaign.standbySessionId = 73; g_campaign.standbyAccountedRealSeconds = 0;
  check(save_write(), "standby starts from committed departing company");
  save_suspend(true);
  sim_advance_minutes(trade.minutes);
  g_campaign.standbyAccountedRealSeconds = trade.minutes / SIM_WORLD_MINUTES_PER_REAL_SECOND;
  save_suspend(false);
  check(save_write(), "catch-up and elapsed checkpoint commit together");
  check(save_read() && g_campaign.coins == 624 && g_campaign.deliveredUnits == 8 &&
    g_campaign.standbyAccountedRealSeconds == trade.minutes / SIM_WORLD_MINUTES_PER_REAL_SECOND,
    "standby checkpoint and trade payout survive together");
  check(corrupt_current() && save_read() && g_campaign.coins == 456 &&
    g_campaign.deliveredUnits == 4 && g_campaign.standbyAccountedRealSeconds == 0 &&
    g_ships[0].booking.state == BOOKING_SAILING,
    "damaged settled bundle falls back to the entire uncredited departure");
  save_suspend(true); sim_advance_minutes(trade.minutes);
  g_campaign.standbyAccountedRealSeconds = trade.minutes / SIM_WORLD_MINUTES_PER_REAL_SECOND;
  save_suspend(false); check(save_write(), "replayed elapsed time commits once");
  check(save_read() && g_campaign.coins == 624 && g_campaign.deliveredUnits == 8,
    "replaying an interrupted catch-up never duplicates trade payment");
  g_ships[0].booking.receiptUnread = 0;
  check(save_write() && save_read() && !g_ships[0].booking.receiptUnread &&
    g_campaign.coins == 624, "receipt acknowledgement persists without a new payout");

  // Provenance is carried in explicit fields, independently of the fractional
  // purchase basis that already occupies the old market contract identifier.
  g_campaign.stageSeen = 5;
  g_campaign.legacyFeatureMask = 0xffff;  // earned warehouse tools survive migration
  g_ports[0].warehouseLevel = 1;
  CargoItem lot{}; lot.kind = CARGO_MARKET; lot.good = GOOD_TIMBER;
  lot.quantity = 3; lot.paidUnitPrice = 17; lot.contractId = 2;
  lot.sourcePort = 2; lot.sailed = 1; lot.destination = lot.projectRegion = NO_PORT;
  g_ships[0].cargo[0] = lot; g_ships[0].cargoCount = 1;
  check(sim_transfer_to_warehouse(0, 0) && save_write() && save_read(), "sailed lot survives warehouse save");
  check(g_ports[0].warehouse[0].sourcePort == 2 && g_ports[0].warehouse[0].sailed == 1 &&
    g_ports[0].warehouse[0].contractId == 2 && g_ports[0].warehouse[0].paidUnitPrice == 17,
    "warehouse preserves exact cost and delivery provenance independently");
  check(sim_transfer_from_warehouse(0, 0) && save_write() && save_read() &&
    g_ships[0].cargo[0].sourcePort == 2 && g_ships[0].cargo[0].sailed == 1,
    "warehouse retrieval preserves provenance after another reload");
  save_reset();
}
int main() {
  if (!save_begin()) { std::fprintf(stderr, "Could not mount test filesystem\n"); return 1; }
  if (!save_selftest()) return 1;
  booked_round_trip();
  std::printf("Booked persistence: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}
