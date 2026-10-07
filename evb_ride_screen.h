#pragma once

#include <lvgl.h>

#include "evb_cluster_settings.h"
#include "evb_phone_state.h"
#include "evb_vehicle_state.h"

typedef void (*evb_ride_screen_tap_handler_t)(void);

typedef struct {
    evb_ride_screen_tap_handler_t on_ride_mode_tapped;
    evb_ride_screen_tap_handler_t on_alerts_tapped;
    evb_ride_screen_tap_handler_t on_settings_tapped;
    evb_ride_screen_tap_handler_t on_navigation_tapped;
} evb_ride_screen_handlers_t;

/* Builds the ride screen (bars, speed, bike, trip, range/odo, battery and temperature bars and
 * the dock) on its own layer. The shell picture and the header are drawn by the app. */
void evb_ride_screen_create(lv_obj_t *parent, const evb_ride_screen_handlers_t *handlers);

/* Updates only the parts of the screen whose values changed. `nav` is the route on the map view. */
void evb_ride_screen_show_state(const evb_vehicle_state_t *vehicle, const evb_cluster_settings_t *settings, const NavState *nav,
                                bool phone_connected);

/* The compass in the dock swaps the bike for the navigation map, as in RideView.qml. */
void evb_ride_screen_show_map(bool map_shown);

/* While the menu is open its page takes the middle of the screen and the dock marks the settings icon. */
void evb_ride_screen_show_menu_open(bool menu_open);
