#pragma once

#include <lvgl.h>

#include "evb_cluster_settings.h"
#include "evb_phone_state.h"
#include "evb_vehicle_state.h"

/* The settings menu: the carousel of pages from MenuCarousel.qml and the *Page.qml files, plus
 * Wi-Fi and System pages. Pages change `settings` directly; `phone` is the app's copy of the
 * phone, Bluetooth and Wi-Fi state, read again every frame. */
void evb_menu_create(lv_obj_t *parent, evb_cluster_settings_t *settings, evb_vehicle_state_t *vehicle, const PhoneState *phone);

void evb_menu_open(void);
void evb_menu_close(void);
bool evb_menu_is_open(void);

/* Keeps live values on the open page current (battery, music position, phone and Wi-Fi). */
void evb_menu_refresh(uint32_t elapsed_ms, uint32_t now_ms);

/* The "Stop the bike to open the menu" pill, shown for 2 s. */
void evb_menu_show_lock_hint(void);
