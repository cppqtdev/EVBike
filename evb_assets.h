#pragma once

#include <lvgl.h>

#include "evb_asset_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the picture ready to draw, unpacking it into PSRAM the first time.
 * Returns NULL only when PSRAM is out of space. */
const lv_image_dsc_t *evb_asset(evb_asset_id_t id);

/* Gives the PSRAM back. Delete every LVGL object that still shows the picture first. */
void evb_asset_unload(evb_asset_id_t id);

#ifdef __cplusplus
}
#endif
