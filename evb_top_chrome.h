#pragma once

#include <lvgl.h>

#include "evb_cluster_settings.h"
#include "evb_phone_state.h"
#include "evb_vehicle_state.h"

/* The header strip shown on every stage: telltales, and phone, temperature and clock while riding. */
void evb_top_chrome_create(lv_obj_t *parent);

/* `self_test` lights every telltale, like the first 1.5 s after power on. The clock shows the
 * real time once the internet or a phone has set it; the temperature is the outside
 * temperature from the internet when Wi-Fi has it. */
void evb_top_chrome_show(const evb_vehicle_state_t *vehicle, const evb_cluster_settings_t *settings, const PhoneState *phone,
                         bool riding, bool self_test);
