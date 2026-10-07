#pragma once

#include "evb_vehicle_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*evb_ride_screen_tap_handler_t)(void);

typedef struct {
    evb_ride_screen_tap_handler_t on_ride_mode_tapped;
    evb_ride_screen_tap_handler_t on_alerts_tapped;
} evb_ride_screen_handlers_t;

/* Builds the EVBikes ride screen on the active LVGL screen. Call with the LVGL lock held. */
void evb_ride_screen_create(const evb_ride_screen_handlers_t *handlers);

/* Updates only the parts of the screen whose values changed. Call with the LVGL lock held. */
void evb_ride_screen_show_state(const evb_vehicle_state_t *state);

#ifdef __cplusplus
}
#endif
