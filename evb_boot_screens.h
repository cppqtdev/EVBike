#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "evb_cluster_settings.h"

/* Start animation, fingerprint unlock and the pre-ride check, as in SplashScreen.qml,
 * AuthScreen.qml and PreRideScreen.qml. Each screen lives on its own layer that the
 * app deletes when the stage ends. */

constexpr int EVB_SPLASH_STEP_MS = 220;
constexpr int EVB_SPLASH_LAST_STEP = 26;

typedef struct {
    void (*on_profile_tapped)(int profile_index);
    void (*on_fingerprint_tapped)(void);
} evb_auth_handlers_t;

void evb_splash_create(lv_obj_t *parent);
void evb_splash_show_step(int step);
void evb_splash_destroy(void);

void evb_auth_create(lv_obj_t *parent, const evb_auth_handlers_t *handlers);
void evb_auth_show(evb_auth_state_t auth_state, int profile_index, uint32_t elapsed_ms);
void evb_auth_destroy(void);

void evb_pre_ride_create(lv_obj_t *parent, void (*on_ready_tapped)(void));
void evb_pre_ride_show(bool side_stand_down, bool phone_connected);
void evb_pre_ride_destroy(void);

/* Gives back the PSRAM of every boot-only picture. Call after the three screens are destroyed. */
void evb_boot_screens_release_pictures(void);
