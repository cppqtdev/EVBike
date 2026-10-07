#include "evb_ui_kit.h"

#include "evb_asset_data.h"

namespace evb {

lv_obj_t *add_picture(lv_obj_t *parent, evb_asset_id_t asset, int x, int y)
{
    lv_obj_t *picture = lv_image_create(parent);
    lv_image_set_src(picture, evb_asset(asset));
    lv_obj_set_pos(picture, x, y);
    lv_obj_set_clickable(picture, false);
    return picture;
}

void tint_picture(lv_obj_t *picture, lv_color_t color)
{
    lv_obj_set_style_image_recolor(picture, color, 0);
    lv_obj_set_style_image_recolor_opa(picture, LV_OPA_COVER, 0);
}

lv_obj_t *add_tinted_picture(lv_obj_t *parent, evb_asset_id_t asset, int x, int y, lv_color_t color)
{
    lv_obj_t *picture = add_picture(parent, asset, x, y);
    tint_picture(picture, color);
    return picture;
}

void show_picture(lv_obj_t *picture, evb_asset_id_t asset)
{
    lv_image_set_src(picture, evb_asset(asset));
}

int picture_width(evb_asset_id_t asset)
{
    return evb_packed_assets[asset].width;
}

int picture_height(evb_asset_id_t asset)
{
    return evb_packed_assets[asset].height;
}

lv_obj_t *add_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t *add_plain_box(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, width, height);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_clickable(box, false);
    return box;
}

lv_obj_t *add_full_screen_layer(lv_obj_t *parent)
{
    return add_plain_box(parent, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

lv_obj_t *add_color_block(lv_obj_t *parent, int x, int y, int width, int height, lv_color_t color, lv_opa_t opa, int radius)
{
    lv_obj_t *block = add_plain_box(parent, x, y, width, height);
    lv_obj_set_style_bg_color(block, color, 0);
    lv_obj_set_style_bg_opa(block, opa, 0);
    lv_obj_set_style_radius(block, radius, 0);
    return block;
}

void paint_vertical_gradient(lv_obj_t *box, lv_color_t top, lv_color_t bottom)
{
    lv_obj_set_style_bg_color(box, top, 0);
    lv_obj_set_style_bg_grad_color(box, bottom, 0);
    lv_obj_set_style_bg_grad_dir(box, LV_GRAD_DIR_VER, 0);
}

void paint_horizontal_gradient(lv_obj_t *box, lv_color_t left, lv_color_t right)
{
    lv_obj_set_style_bg_color(box, left, 0);
    lv_obj_set_style_bg_grad_color(box, right, 0);
    lv_obj_set_style_bg_grad_dir(box, LV_GRAD_DIR_HOR, 0);
}

void place_by_top_center(lv_obj_t *obj, int center_x, int top)
{
    lv_obj_align(obj, LV_ALIGN_TOP_MID, center_x - SCREEN_WIDTH / 2, top);
}

/* Works inside any full-width parent, whatever its height: the object is moved up by half its own height. */
void place_by_center(lv_obj_t *obj, int center_x, int center_y)
{
    lv_obj_align(obj, LV_ALIGN_TOP_MID, center_x - SCREEN_WIDTH / 2, center_y);
    lv_obj_set_style_translate_y(obj, lv_pct(-50), 0);
}

void place_by_top_right(lv_obj_t *obj, int right, int top)
{
    lv_obj_align(obj, LV_ALIGN_TOP_RIGHT, right - SCREEN_WIDTH, top);
}

void call_on_tap(lv_obj_t *obj, lv_event_cb_t handler, void *context)
{
    lv_obj_set_clickable(obj, true);
    lv_obj_add_event_cb(obj, handler, LV_EVENT_CLICKED, context);
}

lv_obj_t *add_touch_area(lv_obj_t *parent, int x, int y, int width, int height, lv_event_cb_t handler, void *context)
{
    lv_obj_t *area = add_plain_box(parent, x, y, width, height);
    call_on_tap(area, handler, context);
    return area;
}

static void apply_opacity(void *obj, int32_t opacity)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)opacity, 0);
}

void fade_to(lv_obj_t *obj, lv_opa_t opacity, uint32_t duration_ms)
{
    lv_opa_t current = lv_obj_get_style_opa(obj, LV_PART_MAIN);
    lv_anim_delete(obj, apply_opacity);
    if (current == opacity) {
        return;
    }
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, apply_opacity);
    lv_anim_set_values(&anim, current, opacity);
    lv_anim_set_duration(&anim, duration_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_start(&anim);
}

void set_shown(lv_obj_t *obj, bool shown)
{
    lv_obj_set_hidden(obj, !shown);
}

lv_color_t mix_colors(lv_color_t from, lv_color_t to, float amount)
{
    return lv_color_mix(to, from, (uint8_t)(amount * 255.0f + 0.5f));
}

int clamp_int(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

} // namespace evb
