#pragma once

// A journey spans every booked/merchant leg; changing legs must not reset the
// progress bar or replace the two terminal ports with an intermediate stop.
struct WatchJourney {
  uint8_t origin, destination;
  uint32_t total, remaining;
  uint16_t fraction;
};

static inline WatchJourney watch_journey(const Ship& ship) {
  WatchJourney j{ship.fromPort, ship.toPort, ship.legTotalMinutes, 0, 0};
  const uint16_t* legs = nullptr;
  if (ship.merchant.state != MERCHANT_NONE) {
    j.origin = ship.merchant.origin; j.destination = ship.merchant.destination;
    legs = ship.merchant.legMinutes;
  } else if (ship.booking.state != BOOKING_NONE) {
    j.origin = ship.booking.origin; j.destination = ship.booking.destination;
    legs = ship.booking.legMinutes;
  }
  j.remaining = ship.legTotalMinutes > ship.legElapsedMinutes ?
    ship.legTotalMinutes - ship.legElapsedMinutes : 0;
  if (legs) {
    j.total = 0;
    for (int leg = 0; leg < ship.routeCount && leg < MAX_ROUTE_LEGS; ++leg) {
      j.total += leg == ship.routePos ? ship.legTotalMinutes : legs[leg];
      if (leg > ship.routePos) j.remaining += legs[leg];
    }
  }
  if (ship.state == SHIP_ARRIVED || ship.state == SHIP_DOCKED) j.remaining = 0;
  j.total = max(j.total, j.remaining);
  j.fraction = j.total ? (uint16_t)((uint64_t)(j.total - j.remaining) * 10000u / j.total) : 10000;
  if (j.origin >= MAX_PORTS) j.origin = ship.currentPort < MAX_PORTS ? ship.currentPort : 0;
  if (j.destination >= MAX_PORTS) j.destination = j.origin;
  return j;
}

// The illustrated route is a journey diagram, not the geographical chart.
static inline void watch_ship_point(uint16_t fraction, int* x, int* y) {
  // One cubic curve joins both quay markers; ship and trail use the same
  // geometry, so there are no straight connector segments or discontinuities.
  const double t = min((uint32_t)10000, (uint32_t)fraction) / 10000.0;
  const double u = 1.0 - t;
  *x = (int)(u*u*u*160 + 3*u*u*t*320 + 3*u*t*t*480 + t*t*t*680 + .5);
  *y = (int)(u*u*u*358 + 3*u*u*t*390 + 3*u*t*t*278 + t*t*t*344 + .5);
}
