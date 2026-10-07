#include "evb_assets.h"

#include <stdlib.h>
#include <string.h>

#include "evb_asset_data.h"

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include "miniz.h"
#else
#include <zlib.h>
#endif

/* Public in LVGL 9.6 but declared in an internal header (misc/cache/instance/lv_image_cache.h). */
extern "C" void lv_image_cache_drop(const void *src);

namespace {

lv_image_dsc_t unpacked_pictures[EVB_ASSET_COUNT];
uint8_t *unpacked_pixels[EVB_ASSET_COUNT];

uint8_t *allocate_in_psram(size_t size)
{
#ifdef ESP_PLATFORM
    return (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return (uint8_t *)malloc(size);
#endif
}

void release_from_psram(uint8_t *pixels)
{
#ifdef ESP_PLATFORM
    heap_caps_free(pixels);
#else
    free(pixels);
#endif
}

/* The decompressor state is about 11 KB, too big for the LVGL task stack, so it lives on the heap. */
bool unpack_into(const evb_packed_asset_t &packed, uint8_t *pixels)
{
#ifdef ESP_PLATFORM
    tinfl_decompressor *decompressor = (tinfl_decompressor *)malloc(sizeof(tinfl_decompressor));
    if (decompressor == NULL) {
        return false;
    }
    tinfl_init(decompressor);
    size_t packed_size = packed.packed_size;
    size_t unpacked_size = packed.unpacked_size;
    tinfl_status status = tinfl_decompress(decompressor, packed.packed, &packed_size, pixels, pixels, &unpacked_size,
                                           TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(decompressor);
    return status == TINFL_STATUS_DONE && unpacked_size == packed.unpacked_size;
#else
    uLongf unpacked_size = packed.unpacked_size;
    int status = uncompress(pixels, &unpacked_size, packed.packed, packed.packed_size);
    return status == Z_OK && unpacked_size == packed.unpacked_size;
#endif
}

} // namespace

extern "C" const lv_image_dsc_t *evb_asset(evb_asset_id_t id)
{
    if (unpacked_pixels[id] != NULL) {
        return &unpacked_pictures[id];
    }

    const evb_packed_asset_t &packed = evb_packed_assets[id];
    uint8_t *pixels = allocate_in_psram(packed.unpacked_size);
    if (pixels == NULL) {
        LV_LOG_ERROR("No PSRAM left for picture %d", (int)id);
        return NULL;
    }
    if (!unpack_into(packed, pixels)) {
        LV_LOG_ERROR("Picture %d is damaged", (int)id);
        release_from_psram(pixels);
        return NULL;
    }

    lv_image_dsc_t &picture = unpacked_pictures[id];
    memset(&picture, 0, sizeof(picture));
    picture.header.magic = LV_IMAGE_HEADER_MAGIC;
    picture.header.cf = packed.color_format;
    picture.header.w = packed.width;
    picture.header.h = packed.height;
    picture.header.stride = packed.stride;
    picture.data_size = packed.unpacked_size;
    picture.data = pixels;
    unpacked_pixels[id] = pixels;
    return &picture;
}

extern "C" void evb_asset_unload(evb_asset_id_t id)
{
    if (unpacked_pixels[id] == NULL) {
        return;
    }
    lv_image_cache_drop(&unpacked_pictures[id]);
    release_from_psram(unpacked_pixels[id]);
    unpacked_pixels[id] = NULL;
}
