#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Drives the ride screen with a looping demo ride until real CAN data is wired in.
 * Builds the screen too. Call once with the LVGL lock held. */
void evb_ride_simulator_start(void);

#ifdef __cplusplus
}
#endif
