#include "game.h"
#include <cstdio>
bool save_write() { return true; }
void audio_begin() {}
void audio_tick() {}
void audio_notice() {}
void audio_alert() {}
int main() {
  std::puts("class,capacity,origin,destination,good,real_minutes,freight_pay_now,freight_net,trade_pay_now,trade_net,trade_advantage_percent");
  for (uint8_t cls = 0; cls < SHIP_CLASS_COUNT; ++cls) {
    sim_new_campaign(51); g_campaign.onboardingStep = ONBOARD_DONE; g_campaign.coins = 100000; g_campaign.deliveredUnits = 192;
    g_campaign.regionMask = g_campaign.chartMask = 63; g_campaign.rank = 5;
    for (int p = 0; p < MAX_PORTS; ++p) { g_ports[p].unlocked = 1; for (int g = 0; g < GOOD_COUNT; ++g) g_ports[p].stock[g] = 200; }
    g_ships[0].classId = cls;
    for (uint8_t origin = 0; origin < MAX_PORTS; ++origin) {
      if (SHIP_CLASS_DEFS[cls].draft > PORT_DEFS[origin].portClass) continue;
      g_ships[0].currentPort = origin;
      for (uint8_t destination = 0; destination < MAX_PORTS; ++destination)
        for (uint8_t good = 0; good < GOOD_COUNT; ++good) {
          VoyageQuote freight = sim_voyage_offer(0,destination,VOYAGE_FREIGHT,good,0);
          VoyageQuote trade = sim_voyage_offer(0,destination,VOYAGE_TRADE,good,0);
          if (freight.code != SIM_OK || trade.code != SIM_OK || freight.quantity != freight.capacity || trade.quantity != trade.capacity) continue;
          std::printf("%s,%u,%s,%s,%s,%.3f,%ld,%ld,%ld,%ld,%.3f\n", SHIP_CLASS_DEFS[cls].name,trade.capacity,
            PORT_DEFS[origin].name,PORT_DEFS[destination].name,GOOD_DEFS[good].name,trade.minutes/600.0,
            (long)freight.payNow,(long)freight.profit,(long)trade.payNow,(long)trade.profit,
            100.0*(trade.profit-freight.profit)/freight.profit);
        }
    }
  }
}
