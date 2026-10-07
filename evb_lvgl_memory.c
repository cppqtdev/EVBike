/*
 * LVGL memory in PSRAM.
 *
 * Used when lv_conf.h sets LV_USE_STDLIB_MALLOC to LV_STDLIB_CUSTOM. With the C library
 * malloc, every small LVGL object (labels, styles, buttons) lands in internal RAM, which
 * Wi-Fi, Bluetooth, TLS and the mDNS task also need. Internal RAM is used only when
 * PSRAM is full.
 */
#include <lvgl.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include <esp_heap_caps.h>

#define EVB_LVGL_PREFERRED_MEMORY (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define EVB_LVGL_FALLBACK_MEMORY (MALLOC_CAP_DEFAULT)

void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void *lv_malloc_core(size_t size)
{
    return heap_caps_malloc_prefer(size, 2, EVB_LVGL_PREFERRED_MEMORY, EVB_LVGL_FALLBACK_MEMORY);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    return heap_caps_realloc_prefer(p, new_size, 2, EVB_LVGL_PREFERRED_MEMORY, EVB_LVGL_FALLBACK_MEMORY);
}

void lv_free_core(void *p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
    LV_UNUSED(mon_p);
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}

#endif
