#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "evb_phone_state.h"

/* The call screen (CallScreen.qml) and the message toast (NotificationToast.qml), drawn
 * over every stage. Answer, decline and end go to the phone through evb_bluetooth. */
void evb_phone_overlays_create(lv_obj_t *parent);

/* `moving` hides message text while riding, like the EVBikes toast. */
void evb_phone_overlays_show(const PhoneState *phone, bool moving, uint32_t now_ms);
