#pragma once

#include <lvgl.h>

#include "evb_assets.h"
#include "evb_fonts.h"

/* Shared colours and small LVGL builders used by every EVBikes screen. */
namespace evb {

constexpr int SCREEN_WIDTH = 800;
constexpr int SCREEN_HEIGHT = 480;

/* Design (1280 wide) x to panel (800 wide) x, for parts that follow the squashed shell. */
inline int panel_x_of_design_x(int design_x)
{
    return (design_x - 64) * 800 / 1152;
}

/* Menu and boot content keeps its design size and is only moved so the design centre lands on the panel centre. */
inline int panel_x_of_page_x(int design_x)
{
    return design_x - 245;
}

namespace color {
inline const lv_color_t black = lv_color_hex(0x000000);
inline const lv_color_t white = lv_color_hex(0xFEFEFE);
inline const lv_color_t pure_white = lv_color_hex(0xFFFFFF);
inline const lv_color_t teal = lv_color_hex(0x6FFFD8);
inline const lv_color_t red = lv_color_hex(0xE3263A);
inline const lv_color_t green = lv_color_hex(0x3DDC84);
inline const lv_color_t mint = lv_color_hex(0x8CF5A8);
inline const lv_color_t mint_highlight = lv_color_hex(0x5FD6B4);
inline const lv_color_t text_primary = lv_color_hex(0xDEDEDE);
inline const lv_color_t text_secondary = lv_color_hex(0xA8A8A8);
inline const lv_color_t text_muted = lv_color_hex(0x606060);
inline const lv_color_t text_soft_white = lv_color_hex(0xDADDDE);
inline const lv_color_t text_cool = lv_color_hex(0xC9CDCF);
inline const lv_color_t text_cool_muted = lv_color_hex(0xC3C8CA);
inline const lv_color_t text_slate = lv_color_hex(0xA7ADB0);
inline const lv_color_t text_steel = lv_color_hex(0x9FA4A7);
inline const lv_color_t divider_cool = lv_color_hex(0x8C9194);
inline const lv_color_t icon_muted = lv_color_hex(0x7B8285);
inline const lv_color_t stroke = lv_color_hex(0x3B3B3B);
inline const lv_color_t stroke2 = lv_color_hex(0x5C5C5C);
inline const lv_color_t surface = lv_color_hex(0x191919);
inline const lv_color_t surface_sunken = lv_color_hex(0x101010);
inline const lv_color_t surface_raised = lv_color_hex(0x242424);
inline const lv_color_t surface_selected = lv_color_hex(0x2A2A2A);
inline const lv_color_t housing = lv_color_hex(0x141414);
inline const lv_color_t success_muted = lv_color_hex(0x2F8A6C);
inline const lv_color_t neutral18 = lv_color_hex(0x181818);
inline const lv_color_t neutral1c = lv_color_hex(0x1C1C1C);
inline const lv_color_t neutral1d = lv_color_hex(0x1D1D1D);
inline const lv_color_t neutral1f = lv_color_hex(0x1F1F1F);
inline const lv_color_t neutral20 = lv_color_hex(0x202020);
inline const lv_color_t neutral21 = lv_color_hex(0x212121);
inline const lv_color_t neutral22 = lv_color_hex(0x222222);
inline const lv_color_t neutral28 = lv_color_hex(0x282828);
inline const lv_color_t neutral3a = lv_color_hex(0x3A3A3A);
inline const lv_color_t neutral3d = lv_color_hex(0x3D3D3D);
inline const lv_color_t neutral3e = lv_color_hex(0x3E3E3E);
inline const lv_color_t neutral40 = lv_color_hex(0x404040);
inline const lv_color_t neutral4e = lv_color_hex(0x4E4E4E);
inline const lv_color_t neutral4f = lv_color_hex(0x4F4F4F);
} // namespace color

/* Pictures and text */
lv_obj_t *add_picture(lv_obj_t *parent, evb_asset_id_t asset, int x, int y);
lv_obj_t *add_tinted_picture(lv_obj_t *parent, evb_asset_id_t asset, int x, int y, lv_color_t color);
void tint_picture(lv_obj_t *picture, lv_color_t color);
void show_picture(lv_obj_t *picture, evb_asset_id_t asset);
int picture_width(evb_asset_id_t asset);
int picture_height(evb_asset_id_t asset);
lv_obj_t *add_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);

/* Boxes */
lv_obj_t *add_plain_box(lv_obj_t *parent, int x, int y, int width, int height);
lv_obj_t *add_full_screen_layer(lv_obj_t *parent);
lv_obj_t *add_color_block(lv_obj_t *parent, int x, int y, int width, int height, lv_color_t color, lv_opa_t opa, int radius);
void paint_vertical_gradient(lv_obj_t *box, lv_color_t top, lv_color_t bottom);
void paint_horizontal_gradient(lv_obj_t *box, lv_color_t left, lv_color_t right);

/* Placement on the 800x480 panel */
void place_by_top_center(lv_obj_t *obj, int center_x, int top);
void place_by_center(lv_obj_t *obj, int center_x, int center_y);
void place_by_top_right(lv_obj_t *obj, int right, int top);

/* Touch */
void call_on_tap(lv_obj_t *obj, lv_event_cb_t handler, void *context);
lv_obj_t *add_touch_area(lv_obj_t *parent, int x, int y, int width, int height, lv_event_cb_t handler, void *context);

/* Animation: opacity is inherited by children, so fading a layer fades everything on it. */
void fade_to(lv_obj_t *obj, lv_opa_t opacity, uint32_t duration_ms);
void set_shown(lv_obj_t *obj, bool shown);

lv_color_t mix_colors(lv_color_t from, lv_color_t to, float amount);
int clamp_int(int value, int low, int high);

} // namespace evb
