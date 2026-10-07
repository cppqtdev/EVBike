#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "evb_asset_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One picture as stored in flash: zlib-compressed pixels plus what LVGL needs to draw them. */
typedef struct {
    uint8_t color_format;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint32_t unpacked_size;
    const uint8_t *packed;
    uint32_t packed_size;
} evb_packed_asset_t;

extern const evb_packed_asset_t evb_packed_assets[EVB_ASSET_COUNT];

#ifdef __cplusplus
}
#endif
