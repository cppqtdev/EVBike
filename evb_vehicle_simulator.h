#pragma once

#include <stdint.h>

#include "evb_vehicle_state.h"

/* Stands in for the bike's CAN data until it is wired in. */

/* The design's values: 0 km/h, 40 %, 60 C, trip 9, 01:11 am, parked. */
void evb_vehicle_simulator_reset(evb_vehicle_state_t *vehicle);

/* Moves the demo ride on by `elapsed_ms`. While `may_ride` is false the bike brakes and stays parked. */
void evb_vehicle_simulator_step(evb_vehicle_state_t *vehicle, uint32_t elapsed_ms, bool may_ride);
