#pragma once

#include <lvgl.h>

#include "evb_cluster_settings.h"
#include "evb_vehicle_state.h"

/* The settings menu: the carousel of 11 pages from MenuCarousel.qml and the *Page.qml files.
 * Pages change `settings` (and the demo phone link in `vehicle`) directly. */
void evb_menu_create(lv_obj_t *parent, evb_cluster_settings_t *settings, evb_vehicle_state_t *vehicle);

void evb_menu_open(void);
void evb_menu_close(void);
bool evb_menu_is_open(void);

/* Keeps live values on the open page current (battery, odometer, music position...). */
void evb_menu_refresh(uint32_t elapsed_ms);

/* The "Stop the bike to open the menu" pill, shown for 2 s. */
void evb_menu_show_lock_hint(void);
