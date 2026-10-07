#include "evb_ride_screen.h"

#include "evb_ui_kit.h"

/*
 * EVBikes ride screen for the 800x480 panel.
 *
 * The design is 1280x480. The shell art (outline, glow, bar channels, header and dock
 * housings) is cropped to design x 64..1216 and squashed to 800 wide; it is baked into
 * the SHELL_* pictures. Text, digits and icons keep their design size and sit at the
 * squashed position of their design centre.
 */

using namespace evb;

namespace {

constexpr int BAR_SEGMENT_COUNT = 8;
constexpr int SPEED_DIGIT_COUNT = 3;
constexpr int TRIP_DIGIT_COUNT = 4;
constexpr int GEAR_COUNT = 3;
constexpr int BAR_TRACK_WIDTH = 140;
constexpr int LOW_BATTERY_PERCENT = 15;
constexpr int HOT_PACK_TEMP_C = 55;
constexpr float MILES_PER_KM = 0.621371f;

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

const lv_color_t SEGMENT_OFF = lv_color_hex(0x3B3E3D);
const lv_color_t SEGMENT_TOP = lv_color_hex(0xCDCDCD);
const lv_color_t SEGMENT_RED_ZONE = lv_color_hex(0xB04A2C);
const lv_color_t SPEED_DIGIT = lv_color_hex(0xD3D3D4);

/* Design bar segments, already moved to panel x. Index 0 is the bottom segment. */
const lv_point_t POWER_SEGMENT_POSITIONS[BAR_SEGMENT_COUNT] = {
    {162, 383}, {132, 361}, {104, 339}, {77, 307}, {65, 253}, {52, 200}, {41, 148}, {31, 98},
};
const lv_point_t RPM_SEGMENT_POSITIONS[BAR_SEGMENT_COUNT] = {
    {585, 383}, {614, 361}, {644, 339}, {673, 307}, {699, 253}, {711, 200}, {724, 148}, {736, 98},
};
const float SEGMENT_GRADIENT_STOPS[BAR_SEGMENT_COUNT - 1] = {0.0f, 0.17f, 0.33f, 0.5f, 0.67f, 0.83f, 1.0f};

const evb_asset_id_t POWER_SEGMENT_PICTURES[BAR_SEGMENT_COUNT] = {
    EVB_ASSET_SEG_L0, EVB_ASSET_SEG_L1, EVB_ASSET_SEG_L2, EVB_ASSET_SEG_L3,
    EVB_ASSET_SEG_L4, EVB_ASSET_SEG_L5, EVB_ASSET_SEG_L6, EVB_ASSET_SEG_L7,
};
const evb_asset_id_t RPM_SEGMENT_PICTURES[BAR_SEGMENT_COUNT] = {
    EVB_ASSET_SEG_R0, EVB_ASSET_SEG_R1, EVB_ASSET_SEG_R2, EVB_ASSET_SEG_R3,
    EVB_ASSET_SEG_R4, EVB_ASSET_SEG_R5, EVB_ASSET_SEG_R6, EVB_ASSET_SEG_R7,
};
const evb_asset_id_t SPEED_DIGIT_PICTURES[10] = {
    EVB_ASSET_SPEED_0, EVB_ASSET_SPEED_1, EVB_ASSET_SPEED_2, EVB_ASSET_SPEED_3, EVB_ASSET_SPEED_4,
    EVB_ASSET_SPEED_5, EVB_ASSET_SPEED_6, EVB_ASSET_SPEED_7, EVB_ASSET_SPEED_8, EVB_ASSET_SPEED_9,
};
const evb_asset_id_t SPEED_FOOT_PICTURES[10] = {
    EVB_ASSET_SPEED_FOOT_0, EVB_ASSET_SPEED_FOOT_1, EVB_ASSET_SPEED_FOOT_2, EVB_ASSET_SPEED_FOOT_3,
    EVB_ASSET_SPEED_FOOT_4, EVB_ASSET_SPEED_FOOT_5, EVB_ASSET_SPEED_FOOT_6, EVB_ASSET_SPEED_FOOT_7,
    EVB_ASSET_SPEED_FOOT_8, EVB_ASSET_SPEED_FOOT_9,
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
constexpr int DOCK_TOP = 410;
constexpr int SETTINGS_ICON_X = 530;

const char *const GEAR_LETTERS[GEAR_COUNT] = {"R", "P", "D"};
const int GEAR_CENTER_X[GEAR_COUNT] = {234, 255, 276};

struct RideScreen {
    evb_ride_screen_handlers_t handlers;

    lv_obj_t *layer;
    lv_obj_t *power_segments[BAR_SEGMENT_COUNT];
    lv_obj_t *rpm_segments[BAR_SEGMENT_COUNT];
    lv_obj_t *max_label;

    lv_obj_t *ride_view;
    lv_obj_t *speed_digits[SPEED_DIGIT_COUNT];
    lv_obj_t *speed_feet[SPEED_DIGIT_COUNT];
    lv_obj_t *speed_unit;
    lv_obj_t *orbit_glow;
    lv_obj_t *trip_digits[TRIP_DIGIT_COUNT];
    lv_obj_t *trip_reflections[TRIP_DIGIT_COUNT];
    lv_obj_t *range_value;
    lv_obj_t *range_unit;
    lv_obj_t *odometer_value;
    lv_obj_t *odometer_unit;

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
    lv_obj_t *settings_band;
    lv_obj_t *settings_icon;

    evb_vehicle_state_t shown;
    bool shown_use_miles;
    bool has_shown_state;
};

RideScreen screen;

int lit_segment_count(int percent)
{
    return (clamp_int(percent, 0, 100) * BAR_SEGMENT_COUNT + 50) / 100;
}

int distance_in_shown_unit(int km, bool use_miles)
{
    return use_miles ? (int)(km * MILES_PER_KM + 0.5f) : km;
}

lv_obj_t *add_fade_line(lv_obj_t *parent, int center_x, int y, lv_color_t color)
{
    return add_tinted_picture(parent, EVB_ASSET_FADE_LINE, center_x - picture_width(EVB_ASSET_FADE_LINE) / 2, y, color);
}

void forward_tap(lv_event_t *event)
{
    evb_ride_screen_tap_handler_t handler = (evb_ride_screen_tap_handler_t)lv_event_get_user_data(event);
    if (handler != NULL) {
        handler();
    }
}

/* A transparent touch area over a dock item, bigger than the icon so it is easy to hit with a glove. */
void add_dock_touch_area(lv_obj_t *parent, int center_x, int width, evb_ride_screen_tap_handler_t handler)
{
    add_touch_area(parent, center_x - width / 2, DOCK_TOP - 5, width, 65, forward_tap, (void *)handler);
}

/* ---------- build ---------- */

void build_bars(lv_obj_t *parent)
{
    for (int i = 0; i < BAR_SEGMENT_COUNT; i++) {
        screen.power_segments[i] = add_tinted_picture(parent, POWER_SEGMENT_PICTURES[i],
                                                       POWER_SEGMENT_POSITIONS[i].x, POWER_SEGMENT_POSITIONS[i].y, SEGMENT_OFF);
        screen.rpm_segments[i] = add_tinted_picture(parent, RPM_SEGMENT_PICTURES[i],
                                                     RPM_SEGMENT_POSITIONS[i].x, RPM_SEGMENT_POSITIONS[i].y, SEGMENT_OFF);
    }

    screen.max_label = add_text(parent, &evb_font_bold_italic_12, colors_for_mode(EVB_RIDE_MODE_ECO).label_on_lit_segment, "MAX");
    place_by_top_center(screen.max_label, 48, 131);
    place_by_top_center(add_text(parent, &evb_font_bold_italic_12, color::text_primary, "AMP"), 188, 399);
    place_by_top_center(add_text(parent, &evb_font_bold_italic_12, color::text_primary, "× 8"), 752, 131);
    place_by_top_center(add_text(parent, &evb_font_bold_italic_12, color::text_primary, "× 4"), 698, 327);
    place_by_top_center(add_text(parent, &evb_font_bold_italic_12, color::text_primary, "RPM"), 612, 399);
}

void build_speed(lv_obj_t *parent)
{
    for (int i = 0; i < SPEED_DIGIT_COUNT; i++) {
        screen.speed_digits[i] = add_tinted_picture(parent, EVB_ASSET_SPEED_0, 0, SPEED_DIGIT_Y, SPEED_DIGIT);
        screen.speed_feet[i] = add_tinted_picture(parent, EVB_ASSET_SPEED_FOOT_0, 0, SPEED_DIGIT_Y,
                                                  colors_for_mode(EVB_RIDE_MODE_ECO).speed_foot);
    }
    screen.speed_unit = add_tinted_picture(parent, EVB_ASSET_SPEED_UNIT_KPH, 0, SPEED_UNIT_Y,
                                           colors_for_mode(EVB_RIDE_MODE_ECO).speed_unit);
}

void build_bike_orbit(lv_obj_t *parent)
{
    constexpr int center_x = 400;
    add_tinted_picture(parent, EVB_ASSET_ORBIT, center_x - 100, 176, lv_color_hex(0xC9CED0));
    screen.orbit_glow = add_tinted_picture(parent, EVB_ASSET_GLOW_SPOT, center_x - 75, 178,
                                           colors_for_mode(EVB_RIDE_MODE_ECO).accent);
    lv_obj_set_style_image_opa(screen.orbit_glow, 46, 0);
    add_picture(parent, EVB_ASSET_BIKE, center_x - 90, 92);

    lv_obj_t *knob = add_color_block(parent, center_x + 52, 206, 14, 14, lv_color_hex(0xE9ECED), LV_OPA_COVER, LV_RADIUS_CIRCLE);
    add_color_block(knob, 3, 5, 3, 3, color::neutral1c, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    add_color_block(knob, 8, 5, 3, 3, color::neutral1c, LV_OPA_COVER, LV_RADIUS_CIRCLE);
}

void build_trip_counter(lv_obj_t *parent)
{
    constexpr int left = 564;
    constexpr int top = 130;
    constexpr int cell_pitch = 38;
    constexpr int cell_width = 32;

    lv_obj_t *title = add_text(parent, &evb_font_bold_italic_19, lv_color_hex(0xB3D0CA), "TRIP");
    place_by_top_right(title, left + 144, top - 2);

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

lv_obj_t *add_distance_reading(lv_obj_t *row, const char *name, lv_obj_t **unit)
{
    lv_obj_t *title = add_text(row, &evb_font_bold_italic_16, lv_color_hex(0x8FB4AB), name);
    sit_on_baseline_of(title, &evb_font_bold_italic_16, &evb_font_bold_italic_30);
    lv_obj_set_style_pad_right(title, 2, 0);

    lv_obj_t *value = add_text(row, &evb_font_bold_italic_30, color::text_primary, "0");

    *unit = add_text(row, &evb_font_italic_16, color::text_primary, "km");
    sit_on_baseline_of(*unit, &evb_font_italic_16, &evb_font_bold_italic_30);
    return value;
}

void build_range_and_odometer(lv_obj_t *parent)
{
    constexpr int top = 322;
    add_fade_line(parent, 400, top, color::stroke2);
    add_fade_line(parent, 400, top + 35, color::stroke);

    lv_obj_t *row = add_plain_box(parent, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 5, 0);
    place_by_top_center(row, 400, top + 1);

    screen.range_value = add_distance_reading(row, "RANGE", &screen.range_unit);
    add_plain_box(row, 0, 0, 30, 1);
    screen.odometer_value = add_distance_reading(row, "ODO", &screen.odometer_unit);
}

void build_battery_and_temperature(lv_obj_t *parent)
{
    constexpr int top = 363;
    constexpr int track_y = top + 20;

    screen.battery_icon = add_tinted_picture(parent, EVB_ASSET_ICON_BATTERY_BOLT, 218, top + 8, lv_color_hex(0x77706E));
    screen.battery_text = add_text(parent, &evb_font_italic_14, color::text_primary, "0%");
    lv_obj_set_pos(screen.battery_text, BATTERY_TRACK_X + 6, top);
    add_tinted_picture(parent, EVB_ASSET_BATTERY_TRACK, BATTERY_TRACK_X, track_y, lv_color_hex(0x383737));
    screen.battery_fill_window = add_plain_box(parent, BATTERY_TRACK_X, track_y, 0, picture_height(EVB_ASSET_BATTERY_FILL));
    screen.battery_fill = add_picture(screen.battery_fill_window, EVB_ASSET_BATTERY_FILL, 0, 0);

    add_tinted_picture(parent, EVB_ASSET_TEMP_TRACK, TEMP_TRACK_X, track_y, lv_color_hex(0x383737));
    screen.temp_fill_window = add_plain_box(parent, TEMP_TRACK_X + BAR_TRACK_WIDTH, track_y, 0, picture_height(EVB_ASSET_TEMP_FILL));
    screen.temp_fill = add_picture(screen.temp_fill_window, EVB_ASSET_TEMP_FILL, 0, 0);
    screen.temp_ruler = add_tinted_picture(screen.temp_fill_window, EVB_ASSET_TEMP_RULER, 0, 0, color::pure_white);
    lv_obj_set_style_image_opa(screen.temp_ruler, LV_OPA_60, 0);

    lv_obj_t *degree_unit = add_text(parent, &evb_font_italic_16, color::text_primary, "°c");
    lv_obj_set_pos(degree_unit, TEMP_TRACK_X + BAR_TRACK_WIDTH - 14, top);
    screen.pack_temp_text = add_text(parent, &evb_font_italic_16, color::text_primary, "0");
    place_by_top_right(screen.pack_temp_text, TEMP_TRACK_X + BAR_TRACK_WIDTH - 14, top - 2);

    screen.thermo_icon = add_tinted_picture(parent, EVB_ASSET_ICON_THERMO, 558, top + 8, lv_color_hex(0xA7C9BA));
}

void build_dock(lv_obj_t *parent)
{
    constexpr int mode_center_x = 399;

    for (int i = 0; i < GEAR_COUNT; i++) {
        screen.gear_labels[i] = add_text(parent, &evb_font_regular_18, color::text_secondary, GEAR_LETTERS[i]);
        place_by_center(screen.gear_labels[i], GEAR_CENTER_X[i], DOCK_TOP + 25);
    }

    add_tinted_picture(parent, EVB_ASSET_ICON_COMPASS, 324 - 18, DOCK_TOP + 13, color::text_cool_muted);

    screen.mode_glow = add_tinted_picture(parent, EVB_ASSET_MODE_GLOW, mode_center_x - 70, DOCK_TOP - 8,
                                          colors_for_mode(EVB_RIDE_MODE_ECO).accent);
    lv_obj_set_style_image_opa(screen.mode_glow, 41, 0);
    add_tinted_picture(parent, EVB_ASSET_MODE_CHIP, mode_center_x - 48, DOCK_TOP, color::neutral22);
    screen.mode_label = add_text(parent, &evb_font_bold_italic_26, colors_for_mode(EVB_RIDE_MODE_ECO).accent, "ECO");
    place_by_center(screen.mode_label, mode_center_x, DOCK_TOP + 26);

    screen.alerts_icon = add_tinted_picture(parent, EVB_ASSET_ICON_BELL, 478 - 12, DOCK_TOP + 13, color::text_slate);

    screen.settings_band = add_full_screen_layer(parent);
    add_tinted_picture(screen.settings_band, EVB_ASSET_DOCK_BAND, 490, DOCK_TOP + 10, lv_color_hex(0x020202));
    add_tinted_picture(screen.settings_band, EVB_ASSET_DOCK_BAND_LIP, 490, DOCK_TOP + 10, color::neutral3a);
    set_shown(screen.settings_band, false);
    screen.settings_icon = add_tinted_picture(parent, EVB_ASSET_ICON_SETTINGS, SETTINGS_ICON_X, DOCK_TOP + 13, color::text_slate);

    add_dock_touch_area(parent, mode_center_x, 110, screen.handlers.on_ride_mode_tapped);
    add_dock_touch_area(parent, 478, 56, screen.handlers.on_alerts_tapped);
    add_dock_touch_area(parent, SETTINGS_ICON_X + 12, 60, screen.handlers.on_settings_tapped);
}

/* ---------- show ---------- */

void show_bar(lv_obj_t *const segments[], int percent, const ModeColors &colors, bool red_zone_top)
{
    int lit = lit_segment_count(percent);
    for (int i = 0; i < BAR_SEGMENT_COUNT - 1; i++) {
        lv_color_t segment_color = i < lit ? mix_colors(colors.segment_low, colors.segment_high, SEGMENT_GRADIENT_STOPS[i]) : SEGMENT_OFF;
        tint_picture(segments[i], segment_color);
    }
    lv_color_t top_color = SEGMENT_OFF;
    if (red_zone_top) {
        top_color = SEGMENT_RED_ZONE;
    } else if (lit >= BAR_SEGMENT_COUNT) {
        top_color = SEGMENT_TOP;
    }
    tint_picture(segments[BAR_SEGMENT_COUNT - 1], top_color);
}

void show_speed(int speed_kmh, bool use_miles)
{
    int speed = clamp_int(use_miles ? (int)(speed_kmh * MILES_PER_KM + 0.5f) : speed_kmh, 0, 999);
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
            show_picture(screen.speed_digits[i], SPEED_DIGIT_PICTURES[digit]);
            show_picture(screen.speed_feet[i], SPEED_FOOT_PICTURES[digit]);
            lv_obj_set_x(screen.speed_digits[i], x);
            lv_obj_set_x(screen.speed_feet[i], x);
        }
        set_shown(screen.speed_digits[i], visible);
        set_shown(screen.speed_feet[i], visible);
    }
    int last_digit_right = first_x + (shown_digits - 1) * SPEED_DIGIT_PITCH + picture_width(EVB_ASSET_SPEED_0);
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

void show_distance_unit(bool use_miles)
{
    const char *unit = use_miles ? "mi" : "km";
    lv_label_set_text(screen.range_unit, unit);
    lv_label_set_text(screen.odometer_unit, unit);
    show_picture(screen.speed_unit, use_miles ? EVB_ASSET_SPEED_UNIT_MPH : EVB_ASSET_SPEED_UNIT_KPH);
}

void show_battery(int battery_percent)
{
    int percent = clamp_int(battery_percent, 0, 100);
    bool low = percent <= LOW_BATTERY_PERCENT;
    lv_label_set_text_fmt(screen.battery_text, "%d%%", percent);
    lv_obj_set_width(screen.battery_fill_window, BAR_TRACK_WIDTH * percent / 100);
    show_picture(screen.battery_fill, low ? EVB_ASSET_BATTERY_FILL_LOW : EVB_ASSET_BATTERY_FILL);
    tint_picture(screen.battery_icon, low ? color::red : lv_color_hex(0x77706E));
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
        lv_obj_set_style_text_color(screen.gear_labels[i], selected ? color::text_primary : color::text_secondary, 0);
    }
}

void show_ride_mode(evb_ride_mode_t mode)
{
    const ModeColors &colors = colors_for_mode(mode);
    bool sport = mode == EVB_RIDE_MODE_SPORT;

    lv_obj_set_style_text_color(screen.max_label, colors.label_on_lit_segment, 0);
    tint_picture(screen.orbit_glow, colors.accent);
    tint_picture(screen.mode_glow, colors.accent);
    tint_picture(screen.speed_unit, colors.speed_unit);
    for (int i = 0; i < SPEED_DIGIT_COUNT; i++) {
        tint_picture(screen.speed_feet[i], colors.speed_foot);
    }

    lv_label_set_text(screen.mode_label, sport ? "SPORTS" : "ECO");
    lv_obj_set_style_text_font(screen.mode_label, sport ? &evb_font_bold_italic_19 : &evb_font_bold_italic_26, 0);
    lv_obj_set_style_text_color(screen.mode_label, sport ? color::text_primary : colors.accent, 0);
}

} // namespace

void evb_ride_screen_create(lv_obj_t *parent, const evb_ride_screen_handlers_t *handlers)
{
    screen = RideScreen();
    if (handlers != NULL) {
        screen.handlers = *handlers;
    }
    screen.layer = add_full_screen_layer(parent);

    build_bars(screen.layer);
    screen.ride_view = add_full_screen_layer(screen.layer);
    build_speed(screen.ride_view);
    build_bike_orbit(screen.ride_view);
    build_trip_counter(screen.ride_view);
    build_range_and_odometer(screen.ride_view);
    build_battery_and_temperature(screen.layer);
    build_dock(screen.layer);
}

void evb_ride_screen_show_state(const evb_vehicle_state_t *vehicle, const evb_cluster_settings_t *settings)
{
    const evb_vehicle_state_t &old = screen.shown;
    bool first = !screen.has_shown_state;
    bool mode_changed = first || vehicle->ride_mode != old.ride_mode;
    bool unit_changed = first || settings->use_miles != screen.shown_use_miles;
    const ModeColors &colors = colors_for_mode(vehicle->ride_mode);

    if (unit_changed) {
        show_distance_unit(settings->use_miles);
    }
    if (mode_changed) {
        show_ride_mode(vehicle->ride_mode);
    }
    if (mode_changed || vehicle->power_percent != old.power_percent) {
        show_bar(screen.power_segments, vehicle->power_percent, colors, false);
    }
    if (mode_changed || vehicle->motor_rpm_percent != old.motor_rpm_percent) {
        show_bar(screen.rpm_segments, vehicle->motor_rpm_percent, colors, true);
    }
    if (unit_changed || vehicle->speed_kmh != old.speed_kmh) {
        show_speed(vehicle->speed_kmh, settings->use_miles);
    }
    if (unit_changed || vehicle->trip_km != old.trip_km) {
        show_trip(distance_in_shown_unit(vehicle->trip_km, settings->use_miles));
    }
    if (unit_changed || vehicle->range_km != old.range_km) {
        lv_label_set_text_fmt(screen.range_value, "%d", distance_in_shown_unit(vehicle->range_km, settings->use_miles));
    }
    if (unit_changed || vehicle->odometer_km != old.odometer_km) {
        lv_label_set_text_fmt(screen.odometer_value, "%d", distance_in_shown_unit(vehicle->odometer_km, settings->use_miles));
    }
    if (first || vehicle->battery_percent != old.battery_percent) {
        show_battery(vehicle->battery_percent);
    }
    if (first || vehicle->pack_temp_c != old.pack_temp_c) {
        show_pack_temperature(vehicle->pack_temp_c);
    }
    if (first || vehicle->gear != old.gear) {
        show_gear(vehicle->gear);
    }
    if (first || vehicle->alerts_muted != old.alerts_muted) {
        show_picture(screen.alerts_icon, vehicle->alerts_muted ? EVB_ASSET_ICON_MUTE : EVB_ASSET_ICON_BELL);
    }

    screen.shown = *vehicle;
    screen.shown_use_miles = settings->use_miles;
    screen.has_shown_state = true;
}

void evb_ride_screen_show_menu_open(bool menu_open)
{
    set_shown(screen.ride_view, !menu_open);
    set_shown(screen.settings_band, menu_open);
    tint_picture(screen.settings_icon, menu_open ? color::text_primary : color::text_slate);
}
