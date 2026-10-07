#include "evb_ride_screen.h"

#include <lvgl.h>
#include <string.h>

#include "evb_fonts.h"
#include "evb_images.h"

/*
 * EVBikes ride screen for the 800x480 panel.
 *
 * The design is 1280x480. The shell art (outline, glow, bar channels, header and dock
 * housings) is cropped to design x 64..1216 and squashed to 800 wide; it is baked into
 * evb_img_shell_eco / evb_img_shell_sport. Text, digits and icons keep their design size
 * and sit at the squashed position of their design centre.
 */

namespace {

constexpr int SCREEN_WIDTH = 800;
constexpr int SCREEN_HEIGHT = 480;

constexpr int BAR_SEGMENT_COUNT = 8;
constexpr int SPEED_DIGIT_COUNT = 3;
constexpr int TRIP_DIGIT_COUNT = 4;
constexpr int TELLTALE_COUNT = 7;
constexpr int GEAR_COUNT = 3;
constexpr int BAR_TRACK_WIDTH = 140;
constexpr int LOW_BATTERY_PERCENT = 15;
constexpr int HOT_PACK_TEMP_C = 55;

struct ModeColors {
    lv_color_t segment_low;
    lv_color_t segment_high;
    lv_color_t label_on_lit_segment;
    lv_color_t accent;
    lv_color_t speed_foot;
    lv_color_t speed_unit;
};

const ModeColors &colors_for_mode(evb_ride_mode_t mode)
{
    static const ModeColors eco = {
        lv_color_hex(0x274337),
        lv_color_hex(0xB2FFE9),
        lv_color_hex(0x2F9C80),
        lv_color_hex(0x6FFFD8),
        lv_color_hex(0xA6CFC5),
        lv_color_hex(0xBBCECB),
    };
    static const ModeColors sport = {
        lv_color_hex(0x8A2414),
        lv_color_hex(0xFF6A34),
        lv_color_hex(0xD0452A),
        lv_color_hex(0xF0552E),
        lv_color_hex(0xCDAA9E),
        lv_color_hex(0xCBBEB9),
    };
    return mode == EVB_RIDE_MODE_SPORT ? sport : eco;
}

const lv_color_t TEXT_PRIMARY = lv_color_hex(0xDEDEDE);
const lv_color_t TEXT_SECONDARY = lv_color_hex(0xA8A8A8);
const lv_color_t TEXT_MUTED = lv_color_hex(0x606060);
const lv_color_t SEGMENT_OFF = lv_color_hex(0x3B3E3D);
const lv_color_t SEGMENT_TOP = lv_color_hex(0xCDCDCD);
const lv_color_t SEGMENT_RED_ZONE = lv_color_hex(0xB04A2C);
const lv_color_t SPEED_DIGIT = lv_color_hex(0xD3D3D4);
const lv_color_t TELLTALE_GREEN = lv_color_hex(0x3DDC84);
const lv_color_t TELLTALE_BLUE = lv_color_hex(0x3D8BFF);
const lv_color_t TELLTALE_AMBER = lv_color_hex(0xFFB020);
const lv_color_t TELLTALE_RED = lv_color_hex(0xF0303F);

/* Design bar segments, already moved to panel x. Index 0 is the bottom segment. */
const lv_point_t POWER_SEGMENT_POSITIONS[BAR_SEGMENT_COUNT] = {
    {162, 383}, {132, 361}, {104, 339}, {77, 307}, {65, 253}, {52, 200}, {41, 148}, {31, 98},
};
const lv_point_t RPM_SEGMENT_POSITIONS[BAR_SEGMENT_COUNT] = {
    {585, 383}, {614, 361}, {644, 339}, {673, 307}, {699, 253}, {711, 200}, {724, 148}, {736, 98},
};
const float SEGMENT_GRADIENT_STOPS[BAR_SEGMENT_COUNT - 1] = {0.0f, 0.17f, 0.33f, 0.5f, 0.67f, 0.83f, 1.0f};

const lv_image_dsc_t *const POWER_SEGMENT_IMAGES[BAR_SEGMENT_COUNT] = {
    &evb_img_seg_l0, &evb_img_seg_l1, &evb_img_seg_l2, &evb_img_seg_l3,
    &evb_img_seg_l4, &evb_img_seg_l5, &evb_img_seg_l6, &evb_img_seg_l7,
};
const lv_image_dsc_t *const RPM_SEGMENT_IMAGES[BAR_SEGMENT_COUNT] = {
    &evb_img_seg_r0, &evb_img_seg_r1, &evb_img_seg_r2, &evb_img_seg_r3,
    &evb_img_seg_r4, &evb_img_seg_r5, &evb_img_seg_r6, &evb_img_seg_r7,
};
const lv_image_dsc_t *const SPEED_DIGIT_IMAGES[10] = {
    &evb_img_speed_0, &evb_img_speed_1, &evb_img_speed_2, &evb_img_speed_3, &evb_img_speed_4,
    &evb_img_speed_5, &evb_img_speed_6, &evb_img_speed_7, &evb_img_speed_8, &evb_img_speed_9,
};
const lv_image_dsc_t *const SPEED_FOOT_IMAGES[10] = {
    &evb_img_speed_foot_0, &evb_img_speed_foot_1, &evb_img_speed_foot_2, &evb_img_speed_foot_3,
    &evb_img_speed_foot_4, &evb_img_speed_foot_5, &evb_img_speed_foot_6, &evb_img_speed_foot_7,
    &evb_img_speed_foot_8, &evb_img_speed_foot_9,
};

/* Speed digit pictures are cropped; these place the crop where the design cell puts it. */
constexpr int SPEED_TWO_DIGIT_FIRST_X = 109;
constexpr int SPEED_THREE_DIGIT_FIRST_X = 51;
constexpr int SPEED_DIGIT_PITCH = 88;
constexpr int SPEED_DIGIT_Y = 128;
constexpr int SPEED_UNIT_Y = 215;
constexpr int SPEED_UNIT_GAP = 1;

constexpr int BATTERY_TRACK_X = 244;
constexpr int TEMP_TRACK_X = 416;

enum TelltaleIndex {
    TELLTALE_LEFT,
    TELLTALE_HIGH_BEAM,
    TELLTALE_LOW_BEAM,
    TELLTALE_WARNING,
    TELLTALE_ABS,
    TELLTALE_BATTERY,
    TELLTALE_RIGHT,
};

struct TelltaleLook {
    const lv_image_dsc_t *image;
    int center_x;
    lv_color_t off_color;
};

const TelltaleLook TELLTALES[TELLTALE_COUNT] = {
    {&evb_img_tt_left, 254, lv_color_hex(0x8F8F8F)},
    {&evb_img_tt_high_beam, 301, lv_color_hex(0x515151)},
    {&evb_img_tt_low_beam, 355, lv_color_hex(0x515151)},
    {&evb_img_tt_warning, 406, lv_color_hex(0x636363)},
    {&evb_img_tt_abs, 460, lv_color_hex(0x3B3B3B)},
    {&evb_img_tt_battery, 515, lv_color_hex(0x5D5D5D)},
    {&evb_img_tt_right, 568, lv_color_hex(0x8F8F8F)},
};

const char *const GEAR_LETTERS[GEAR_COUNT] = {"R", "P", "D"};
const int GEAR_CENTER_X[GEAR_COUNT] = {234, 255, 276};

struct RideScreen {
    evb_ride_screen_handlers_t handlers;

    lv_obj_t *shell;
    lv_obj_t *power_segments[BAR_SEGMENT_COUNT];
    lv_obj_t *rpm_segments[BAR_SEGMENT_COUNT];
    lv_obj_t *max_label;

    lv_obj_t *speed_digits[SPEED_DIGIT_COUNT];
    lv_obj_t *speed_feet[SPEED_DIGIT_COUNT];
    lv_obj_t *speed_unit;

    lv_obj_t *orbit_glow;

    lv_obj_t *trip_digits[TRIP_DIGIT_COUNT];
    lv_obj_t *trip_reflections[TRIP_DIGIT_COUNT];

    lv_obj_t *range_value;
    lv_obj_t *odometer_value;

    lv_obj_t *battery_icon;
    lv_obj_t *battery_text;
    lv_obj_t *battery_fill_window;
    lv_obj_t *battery_fill;
    lv_obj_t *pack_temp_text;
    lv_obj_t *temp_fill_window;
    lv_obj_t *temp_fill;
    lv_obj_t *temp_ruler;
    lv_obj_t *thermo_icon;

    lv_obj_t *gear_labels[GEAR_COUNT];
    lv_obj_t *mode_glow;
    lv_obj_t *mode_label;
    lv_obj_t *alerts_icon;

    lv_obj_t *telltales[TELLTALE_COUNT];
    lv_obj_t *bluetooth_icon;
    lv_obj_t *signal_icon;
    lv_obj_t *ambient_temp_text;
    lv_obj_t *ambient_temp_degree;
    lv_obj_t *ambient_temp_unit;
    lv_obj_t *clock_text;
    lv_obj_t *clock_meridiem;

    evb_vehicle_state_t shown;
    bool has_shown_state;
};

RideScreen screen;

/* ---------- small builders ---------- */

lv_obj_t *add_picture(lv_obj_t *parent, const lv_image_dsc_t *image, int x, int y)
{
    lv_obj_t *picture = lv_image_create(parent);
    lv_image_set_src(picture, image);
    lv_obj_set_pos(picture, x, y);
    lv_obj_set_clickable(picture, false);
    return picture;
}

void tint_picture(lv_obj_t *picture, lv_color_t color)
{
    lv_obj_set_style_image_recolor(picture, color, 0);
    lv_obj_set_style_image_recolor_opa(picture, LV_OPA_COVER, 0);
}

lv_obj_t *add_tinted_picture(lv_obj_t *parent, const lv_image_dsc_t *image, int x, int y, lv_color_t color)
{
    lv_obj_t *picture = add_picture(parent, image, x, y);
    tint_picture(picture, color);
    return picture;
}

lv_obj_t *add_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text);
    return label;
}

/* Keeps the top edge at `top` and the horizontal centre at `center_x` whatever the text width. */
void place_by_top_center(lv_obj_t *obj, int center_x, int top)
{
    lv_obj_align(obj, LV_ALIGN_TOP_MID, center_x - SCREEN_WIDTH / 2, top);
}

void place_by_center(lv_obj_t *obj, int center_x, int center_y)
{
    lv_obj_align(obj, LV_ALIGN_CENTER, center_x - SCREEN_WIDTH / 2, center_y - SCREEN_HEIGHT / 2);
}

/* A plain box with no theme look, used as a clipping window or a colour block. */
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

lv_obj_t *add_color_block(lv_obj_t *parent, int x, int y, int width, int height, lv_color_t color, lv_opa_t opa, int radius)
{
    lv_obj_t *block = add_plain_box(parent, x, y, width, height);
    lv_obj_set_style_bg_color(block, color, 0);
    lv_obj_set_style_bg_opa(block, opa, 0);
    lv_obj_set_style_radius(block, radius, 0);
    return block;
}

lv_obj_t *add_fade_line(lv_obj_t *parent, int center_x, int y, lv_color_t color)
{
    return add_tinted_picture(parent, &evb_img_fade_line, center_x - (int)evb_img_fade_line.header.w / 2, y, color);
}

lv_color_t mix_colors(lv_color_t from, lv_color_t to, float amount)
{
    return lv_color_mix(to, from, (uint8_t)(amount * 255.0f + 0.5f));
}

int clamp_int(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

int lit_segment_count(int percent)
{
    return (clamp_int(percent, 0, 100) * BAR_SEGMENT_COUNT + 50) / 100;
}

/* ---------- build ---------- */

void build_bars(lv_obj_t *parent)
{
    for (int i = 0; i < BAR_SEGMENT_COUNT; i++) {
        screen.power_segments[i] = add_tinted_picture(parent, POWER_SEGMENT_IMAGES[i],
                                                       POWER_SEGMENT_POSITIONS[i].x, POWER_SEGMENT_POSITIONS[i].y, SEGMENT_OFF);
        screen.rpm_segments[i] = add_tinted_picture(parent, RPM_SEGMENT_IMAGES[i],
                                                     RPM_SEGMENT_POSITIONS[i].x, RPM_SEGMENT_POSITIONS[i].y, SEGMENT_OFF);
    }

    screen.max_label = add_text(parent, &evb_font_label_12, colors_for_mode(EVB_RIDE_MODE_ECO).label_on_lit_segment, "MAX");
    place_by_top_center(screen.max_label, 48, 131);
    place_by_top_center(add_text(parent, &evb_font_label_12, TEXT_PRIMARY, "AMP"), 188, 399);
    place_by_top_center(add_text(parent, &evb_font_label_12, TEXT_PRIMARY, "× 8"), 752, 131);
    place_by_top_center(add_text(parent, &evb_font_label_12, TEXT_PRIMARY, "× 4"), 698, 327);
    place_by_top_center(add_text(parent, &evb_font_label_12, TEXT_PRIMARY, "RPM"), 612, 399);
}

void build_speed(lv_obj_t *parent)
{
    for (int i = 0; i < SPEED_DIGIT_COUNT; i++) {
        screen.speed_digits[i] = add_tinted_picture(parent, &evb_img_speed_0, 0, SPEED_DIGIT_Y, SPEED_DIGIT);
        screen.speed_feet[i] = add_tinted_picture(parent, &evb_img_speed_foot_0, 0, SPEED_DIGIT_Y,
                                                  colors_for_mode(EVB_RIDE_MODE_ECO).speed_foot);
    }
    screen.speed_unit = add_tinted_picture(parent, &evb_img_speed_unit_kph, 0, SPEED_UNIT_Y,
                                           colors_for_mode(EVB_RIDE_MODE_ECO).speed_unit);
}

void build_bike_orbit(lv_obj_t *parent)
{
    constexpr int center_x = 400;
    add_tinted_picture(parent, &evb_img_orbit, center_x - 100, 176, lv_color_hex(0xC9CED0));
    screen.orbit_glow = add_tinted_picture(parent, &evb_img_glow_spot, center_x - 75, 178,
                                           colors_for_mode(EVB_RIDE_MODE_ECO).accent);
    lv_obj_set_style_image_opa(screen.orbit_glow, 46, 0);
    add_picture(parent, &evb_img_bike, center_x - 90, 92);

    lv_obj_t *knob = add_color_block(parent, center_x + 52, 206, 14, 14, lv_color_hex(0xE9ECED), LV_OPA_COVER, LV_RADIUS_CIRCLE);
    add_color_block(knob, 3, 5, 3, 3, lv_color_hex(0x1C1C1C), LV_OPA_COVER, LV_RADIUS_CIRCLE);
    add_color_block(knob, 8, 5, 3, 3, lv_color_hex(0x1C1C1C), LV_OPA_COVER, LV_RADIUS_CIRCLE);
}

void build_trip_counter(lv_obj_t *parent)
{
    constexpr int left = 564;
    constexpr int top = 130;
    constexpr int cell_pitch = 38;
    constexpr int cell_width = 32;

    lv_obj_t *title = add_text(parent, &evb_font_label_19, lv_color_hex(0xB3D0CA), "TRIP");
    lv_obj_align(title, LV_ALIGN_TOP_RIGHT, -(SCREEN_WIDTH - (left + 144)), top - 2);

    for (int i = 0; i < TRIP_DIGIT_COUNT; i++) {
        int x = left + i * cell_pitch;
        lv_obj_t *cell = add_color_block(parent, x, top + 27, cell_width, 23, lv_color_hex(0x1C1F23), LV_OPA_COVER, 3);
        lv_obj_t *reflection = add_color_block(parent, x, top + 54, cell_width, 22, lv_color_hex(0x0A0C0E), 217, 3);

        screen.trip_digits[i] = add_text(cell, &evb_font_italic_18, lv_color_hex(0xD0D0D2), "0");
        lv_obj_align(screen.trip_digits[i], LV_ALIGN_TOP_MID, 0, 1);
        screen.trip_reflections[i] = add_text(reflection, &evb_font_italic_18, lv_color_hex(0xD0D0D2), "0");
        lv_obj_set_style_text_opa(screen.trip_reflections[i], 77, 0);
        lv_obj_align(screen.trip_reflections[i], LV_ALIGN_TOP_MID, 0, 1);
    }
}

/* Small labels sit on the same baseline as the big numbers next to them. */
void sit_on_baseline_of(lv_obj_t *small_label, const lv_font_t *small_font, const lv_font_t *big_font)
{
    lv_obj_set_style_translate_y(small_label, small_font->base_line - big_font->base_line, 0);
}

lv_obj_t *add_distance_reading(lv_obj_t *row, const char *name)
{
    lv_obj_t *title = add_text(row, &evb_font_label_16, lv_color_hex(0x8FB4AB), name);
    sit_on_baseline_of(title, &evb_font_label_16, &evb_font_value_30);
    lv_obj_set_style_pad_right(title, 2, 0);

    lv_obj_t *value = add_text(row, &evb_font_value_30, TEXT_PRIMARY, "0");

    lv_obj_t *unit = add_text(row, &evb_font_italic_16, TEXT_PRIMARY, "km");
    sit_on_baseline_of(unit, &evb_font_italic_16, &evb_font_value_30);
    return value;
}

void build_range_and_odometer(lv_obj_t *parent)
{
    constexpr int top = 322;
    add_fade_line(parent, 400, top, lv_color_hex(0x5C5C5C));
    add_fade_line(parent, 400, top + 35, lv_color_hex(0x3B3B3B));

    lv_obj_t *row = add_plain_box(parent, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 5, 0);
    place_by_top_center(row, 400, top + 1);

    screen.range_value = add_distance_reading(row, "RANGE");
    add_plain_box(row, 0, 0, 30, 1);
    screen.odometer_value = add_distance_reading(row, "ODO");
}

void build_battery_and_temperature(lv_obj_t *parent)
{
    constexpr int top = 363;
    constexpr int track_y = top + 20;
    constexpr int battery_track_x = BATTERY_TRACK_X;
    constexpr int temp_track_x = TEMP_TRACK_X;

    screen.battery_icon = add_tinted_picture(parent, &evb_img_icon_battery_bolt, 218, top + 8, lv_color_hex(0x77706E));
    screen.battery_text = add_text(parent, &evb_font_italic_14, TEXT_PRIMARY, "0%");
    lv_obj_set_pos(screen.battery_text, battery_track_x + 6, top);
    add_tinted_picture(parent, &evb_img_battery_track, battery_track_x, track_y, lv_color_hex(0x383737));
    screen.battery_fill_window = add_plain_box(parent, battery_track_x, track_y, 0, evb_img_battery_fill.header.h);
    screen.battery_fill = add_picture(screen.battery_fill_window, &evb_img_battery_fill, 0, 0);

    add_tinted_picture(parent, &evb_img_temp_track, temp_track_x, track_y, lv_color_hex(0x383737));
    screen.temp_fill_window = add_plain_box(parent, temp_track_x + BAR_TRACK_WIDTH, track_y, 0, evb_img_temp_fill.header.h);
    screen.temp_fill = add_picture(screen.temp_fill_window, &evb_img_temp_fill, 0, 0);
    screen.temp_ruler = add_tinted_picture(screen.temp_fill_window, &evb_img_temp_ruler, 0, 0, lv_color_white());
    lv_obj_set_style_image_opa(screen.temp_ruler, LV_OPA_60, 0);

    lv_obj_t *degree_unit = add_text(parent, &evb_font_italic_16, TEXT_PRIMARY, "°c");
    lv_obj_set_pos(degree_unit, temp_track_x + BAR_TRACK_WIDTH - 14, top);
    screen.pack_temp_text = add_text(parent, &evb_font_italic_16, TEXT_PRIMARY, "0");
    lv_obj_align(screen.pack_temp_text, LV_ALIGN_TOP_RIGHT, -(SCREEN_WIDTH - (temp_track_x + BAR_TRACK_WIDTH - 14)), top - 2);

    screen.thermo_icon = add_tinted_picture(parent, &evb_img_icon_thermo, 558, top + 8, lv_color_hex(0xA7C9BA));
}

void forward_tap(lv_event_t *event)
{
    evb_ride_screen_tap_handler_t handler = (evb_ride_screen_tap_handler_t)lv_event_get_user_data(event);
    if (handler != NULL) {
        handler();
    }
}

/* A transparent touch area over a dock item, bigger than the icon so it is easy to hit with a glove. */
void add_touch_area(lv_obj_t *parent, int center_x, int center_y, int width, int height, evb_ride_screen_tap_handler_t handler)
{
    lv_obj_t *area = add_plain_box(parent, center_x - width / 2, center_y - height / 2, width, height);
    lv_obj_set_clickable(area, true);
    lv_obj_add_event_cb(area, forward_tap, LV_EVENT_CLICKED, (void *)handler);
}

void build_dock(lv_obj_t *parent)
{
    constexpr int top = 410;
    constexpr int mode_center_x = 399;

    for (int i = 0; i < GEAR_COUNT; i++) {
        screen.gear_labels[i] = add_text(parent, &evb_font_regular_18, TEXT_SECONDARY, GEAR_LETTERS[i]);
        place_by_center(screen.gear_labels[i], GEAR_CENTER_X[i], top + 25);
    }

    add_tinted_picture(parent, &evb_img_icon_compass, 324 - 18, top + 13, lv_color_hex(0xC3C8CA));

    screen.mode_glow = add_tinted_picture(parent, &evb_img_mode_glow, mode_center_x - 70, top - 8,
                                          colors_for_mode(EVB_RIDE_MODE_ECO).accent);
    lv_obj_set_style_image_opa(screen.mode_glow, 41, 0);
    add_tinted_picture(parent, &evb_img_mode_chip, mode_center_x - 48, top, lv_color_hex(0x222222));
    screen.mode_label = add_text(parent, &evb_font_mode_26, colors_for_mode(EVB_RIDE_MODE_ECO).accent, "ECO");
    place_by_center(screen.mode_label, mode_center_x, top + 26);

    screen.alerts_icon = add_tinted_picture(parent, &evb_img_icon_bell, 478 - 12, top + 13, lv_color_hex(0xA7ADB0));
    add_tinted_picture(parent, &evb_img_icon_settings, 542 - 12, top + 13, lv_color_hex(0xA7ADB0));

    add_touch_area(parent, mode_center_x, top + 25, 110, 60, screen.handlers.on_ride_mode_tapped);
    add_touch_area(parent, 478, top + 25, 56, 60, screen.handlers.on_alerts_tapped);
}

void build_header(lv_obj_t *parent)
{
    for (int i = 0; i < TELLTALE_COUNT; i++) {
        screen.telltales[i] = add_tinted_picture(parent, TELLTALES[i].image, TELLTALES[i].center_x - 22, 9, TELLTALES[i].off_color);
    }

    screen.bluetooth_icon = add_tinted_picture(parent, &evb_img_icon_bluetooth, 126, 20, TEXT_MUTED);
    screen.ambient_temp_text = add_text(parent, &evb_font_regular_24, TEXT_PRIMARY, "--");
    lv_obj_set_pos(screen.ambient_temp_text, 167, 18);
    screen.ambient_temp_degree = add_text(parent, &evb_font_regular_18, TEXT_PRIMARY, "°");
    screen.ambient_temp_unit = add_text(parent, &evb_font_regular_15, TEXT_SECONDARY, "C");

    screen.signal_icon = add_tinted_picture(parent, &evb_img_icon_signal, 604, 25, TEXT_MUTED);
    screen.clock_text = add_text(parent, &evb_font_regular_22, TEXT_PRIMARY, "--:--");
    lv_obj_set_pos(screen.clock_text, 636, 20);
    screen.clock_meridiem = add_text(parent, &evb_font_regular_16, TEXT_PRIMARY, "am");
}

/* ---------- show ---------- */

void show_bar(lv_obj_t *const segments[], int percent, const ModeColors &colors, bool red_zone_top)
{
    int lit = lit_segment_count(percent);
    for (int i = 0; i < BAR_SEGMENT_COUNT - 1; i++) {
        lv_color_t color = i < lit ? mix_colors(colors.segment_low, colors.segment_high, SEGMENT_GRADIENT_STOPS[i]) : SEGMENT_OFF;
        tint_picture(segments[i], color);
    }
    lv_color_t top_color = SEGMENT_OFF;
    if (red_zone_top) {
        top_color = SEGMENT_RED_ZONE;
    } else if (lit >= BAR_SEGMENT_COUNT) {
        top_color = SEGMENT_TOP;
    }
    tint_picture(segments[BAR_SEGMENT_COUNT - 1], top_color);
}

void show_speed(int speed_kmh)
{
    int speed = clamp_int(speed_kmh, 0, 999);
    bool has_hundreds = speed >= 100;
    int first_x = has_hundreds ? SPEED_THREE_DIGIT_FIRST_X : SPEED_TWO_DIGIT_FIRST_X;
    int shown_digits = has_hundreds ? 3 : 2;
    int divisor = has_hundreds ? 100 : 10;

    for (int i = 0; i < SPEED_DIGIT_COUNT; i++) {
        bool visible = i < shown_digits;
        if (visible) {
            int digit = (speed / divisor) % 10;
            divisor /= 10;
            int x = first_x + i * SPEED_DIGIT_PITCH;
            lv_image_set_src(screen.speed_digits[i], SPEED_DIGIT_IMAGES[digit]);
            lv_image_set_src(screen.speed_feet[i], SPEED_FOOT_IMAGES[digit]);
            lv_obj_set_x(screen.speed_digits[i], x);
            lv_obj_set_x(screen.speed_feet[i], x);
        }
        lv_obj_set_hidden(screen.speed_digits[i], !visible);
        lv_obj_set_hidden(screen.speed_feet[i], !visible);
    }
    int last_digit_right = first_x + (shown_digits - 1) * SPEED_DIGIT_PITCH + evb_img_speed_0.header.w;
    lv_obj_set_x(screen.speed_unit, last_digit_right + SPEED_UNIT_GAP);
}

void show_trip(int trip_km)
{
    int value = clamp_int(trip_km, 0, 9999);
    int divisor = 1000;
    for (int i = 0; i < TRIP_DIGIT_COUNT; i++) {
        char digit[2] = {(char)('0' + (value / divisor) % 10), '\0'};
        divisor /= 10;
        lv_label_set_text(screen.trip_digits[i], digit);
        lv_label_set_text(screen.trip_reflections[i], digit);
    }
}

void show_battery(int battery_percent)
{
    int percent = clamp_int(battery_percent, 0, 100);
    bool low = percent <= LOW_BATTERY_PERCENT;
    lv_label_set_text_fmt(screen.battery_text, "%d%%", percent);
    lv_obj_set_width(screen.battery_fill_window, BAR_TRACK_WIDTH * percent / 100);
    lv_image_set_src(screen.battery_fill, low ? &evb_img_battery_fill_low : &evb_img_battery_fill);
    tint_picture(screen.battery_icon, low ? lv_color_hex(0xE3263A) : lv_color_hex(0x77706E));
}

/* The temperature bar fills from its right end, like the design: 10 C is empty, 70 C is full. */
void show_pack_temperature(int pack_temp_c)
{
    int percent = clamp_int((pack_temp_c - 10) * 100 / 60, 0, 100);
    int fill_width = BAR_TRACK_WIDTH * percent / 100;
    int empty_width = BAR_TRACK_WIDTH - fill_width;
    lv_obj_set_x(screen.temp_fill_window, TEMP_TRACK_X + empty_width);
    lv_obj_set_width(screen.temp_fill_window, fill_width);
    lv_obj_set_x(screen.temp_fill, -empty_width);
    lv_obj_set_x(screen.temp_ruler, -empty_width);

    lv_label_set_text_fmt(screen.pack_temp_text, "%d", pack_temp_c);
    tint_picture(screen.thermo_icon, pack_temp_c >= HOT_PACK_TEMP_C ? lv_color_hex(0xD9644A) : lv_color_hex(0xA7C9BA));
}

void show_gear(evb_gear_t gear)
{
    for (int i = 0; i < GEAR_COUNT; i++) {
        bool selected = (int)gear == i;
        lv_obj_set_style_text_font(screen.gear_labels[i], selected ? &evb_font_regular_26 : &evb_font_regular_18, 0);
        lv_obj_set_style_text_color(screen.gear_labels[i], selected ? TEXT_PRIMARY : TEXT_SECONDARY, 0);
    }
}

void show_ride_mode(const evb_vehicle_state_t *state)
{
    const ModeColors &colors = colors_for_mode(state->ride_mode);
    bool sport = state->ride_mode == EVB_RIDE_MODE_SPORT;

    lv_image_set_src(screen.shell, sport ? &evb_img_shell_sport : &evb_img_shell_eco);
    lv_obj_set_style_text_color(screen.max_label, colors.label_on_lit_segment, 0);
    tint_picture(screen.orbit_glow, colors.accent);
    tint_picture(screen.mode_glow, colors.accent);
    tint_picture(screen.speed_unit, colors.speed_unit);
    for (int i = 0; i < SPEED_DIGIT_COUNT; i++) {
        tint_picture(screen.speed_feet[i], colors.speed_foot);
    }

    lv_label_set_text(screen.mode_label, sport ? "SPORTS" : "ECO");
    lv_obj_set_style_text_font(screen.mode_label, sport ? &evb_font_label_19 : &evb_font_mode_26, 0);
    lv_obj_set_style_text_color(screen.mode_label, sport ? TEXT_PRIMARY : colors.accent, 0);
}

void show_telltale(int index, bool on, lv_color_t on_color)
{
    tint_picture(screen.telltales[index], on ? on_color : TELLTALES[index].off_color);
}

void show_telltales(const evb_vehicle_state_t *state)
{
    show_telltale(TELLTALE_LEFT, state->indicator_left_on, TELLTALE_GREEN);
    show_telltale(TELLTALE_HIGH_BEAM, state->high_beam_on, TELLTALE_BLUE);
    show_telltale(TELLTALE_LOW_BEAM, state->low_beam_on, TELLTALE_GREEN);
    show_telltale(TELLTALE_WARNING, state->vehicle_warning_on, TELLTALE_AMBER);
    show_telltale(TELLTALE_ABS, state->abs_fault_on, TELLTALE_AMBER);
    bool battery_low = state->battery_percent <= LOW_BATTERY_PERCENT;
    show_telltale(TELLTALE_BATTERY, battery_low, state->battery_percent <= 5 ? TELLTALE_RED : TELLTALE_AMBER);
    show_telltale(TELLTALE_RIGHT, state->indicator_right_on, TELLTALE_GREEN);
}

void show_clock(int hours, int minutes)
{
    int twelve_hour = hours % 12 == 0 ? 12 : hours % 12;
    lv_label_set_text_fmt(screen.clock_text, "%02d:%02d", twelve_hour, minutes);
    lv_label_set_text(screen.clock_meridiem, hours < 12 ? "am" : "pm");
    lv_obj_align_to(screen.clock_meridiem, screen.clock_text, LV_ALIGN_OUT_RIGHT_TOP, 1, 7);
}

void show_ambient_temperature(int ambient_temp_c)
{
    lv_label_set_text_fmt(screen.ambient_temp_text, "%d", ambient_temp_c);
    lv_obj_align_to(screen.ambient_temp_degree, screen.ambient_temp_text, LV_ALIGN_OUT_RIGHT_TOP, 1, -6);
    lv_obj_align_to(screen.ambient_temp_unit, screen.ambient_temp_text, LV_ALIGN_OUT_RIGHT_TOP, 5, 7);
}

bool have_telltale_inputs_changed(const evb_vehicle_state_t &now, const evb_vehicle_state_t &before)
{
    return now.indicator_left_on != before.indicator_left_on
        || now.indicator_right_on != before.indicator_right_on
        || now.high_beam_on != before.high_beam_on
        || now.low_beam_on != before.low_beam_on
        || now.vehicle_warning_on != before.vehicle_warning_on
        || now.abs_fault_on != before.abs_fault_on
        || now.battery_percent != before.battery_percent;
}

void show_phone_link(bool connected)
{
    lv_color_t color = connected ? TEXT_PRIMARY : TEXT_MUTED;
    tint_picture(screen.bluetooth_icon, color);
    tint_picture(screen.signal_icon, color);
}

} // namespace

extern "C" void evb_ride_screen_create(const evb_ride_screen_handlers_t *handlers)
{
    memset(&screen, 0, sizeof(screen));
    if (handlers != NULL) {
        screen.handlers = *handlers;
    }

    lv_obj_t *root = lv_screen_active();
    lv_obj_clean(root);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    screen.shell = add_picture(root, &evb_img_shell_eco, 0, 0);

    build_bars(root);
    build_speed(root);
    build_bike_orbit(root);
    build_trip_counter(root);
    build_range_and_odometer(root);
    build_battery_and_temperature(root);
    build_dock(root);
    build_header(root);
}

extern "C" void evb_ride_screen_show_state(const evb_vehicle_state_t *state)
{
    const evb_vehicle_state_t &old = screen.shown;
    bool first = !screen.has_shown_state;
    bool mode_changed = first || state->ride_mode != old.ride_mode;
    const ModeColors &colors = colors_for_mode(state->ride_mode);

    if (mode_changed) {
        show_ride_mode(state);
    }
    if (mode_changed || state->power_percent != old.power_percent) {
        show_bar(screen.power_segments, state->power_percent, colors, false);
    }
    if (mode_changed || state->motor_rpm_percent != old.motor_rpm_percent) {
        show_bar(screen.rpm_segments, state->motor_rpm_percent, colors, true);
    }
    if (first || state->speed_kmh != old.speed_kmh) {
        show_speed(state->speed_kmh);
    }
    if (first || state->trip_km != old.trip_km) {
        show_trip(state->trip_km);
    }
    if (first || state->range_km != old.range_km) {
        lv_label_set_text_fmt(screen.range_value, "%d", state->range_km);
    }
    if (first || state->odometer_km != old.odometer_km) {
        lv_label_set_text_fmt(screen.odometer_value, "%d", state->odometer_km);
    }
    if (first || state->battery_percent != old.battery_percent) {
        show_battery(state->battery_percent);
    }
    if (first || state->pack_temp_c != old.pack_temp_c) {
        show_pack_temperature(state->pack_temp_c);
    }
    if (first || state->gear != old.gear) {
        show_gear(state->gear);
    }
    if (first || have_telltale_inputs_changed(*state, old)) {
        show_telltales(state);
    }
    if (first || state->alerts_muted != old.alerts_muted) {
        lv_image_set_src(screen.alerts_icon, state->alerts_muted ? &evb_img_icon_mute : &evb_img_icon_bell);
    }
    if (first || state->phone_connected != old.phone_connected) {
        show_phone_link(state->phone_connected);
    }
    if (first || state->ambient_temp_c != old.ambient_temp_c) {
        show_ambient_temperature(state->ambient_temp_c);
    }
    if (first || state->clock_hours != old.clock_hours || state->clock_minutes != old.clock_minutes) {
        show_clock(state->clock_hours, state->clock_minutes);
    }

    screen.shown = *state;
    screen.has_shown_state = true;
}
