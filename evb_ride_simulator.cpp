#include "evb_ride_simulator.h"

#include <lvgl.h>

#include "evb_ride_screen.h"
#include "evb_vehicle_state.h"

namespace {

constexpr uint32_t TICK_MS = 50;
constexpr uint32_t INDICATOR_BLINK_MS = 400;
constexpr uint32_t BATTERY_DRAIN_MS = 20000;
constexpr int BATTERY_REFILL_BELOW_PERCENT = 8;
constexpr int BATTERY_REFILL_TO_PERCENT = 80;
constexpr float RANGE_KM_PER_BATTERY_PERCENT = 0.5f;

enum class Indicator {
    None,
    Left,
    Right,
};

/* One leg of the demo ride: drive towards a speed and hold it for a while. */
struct RideLeg {
    int target_speed_kmh;
    uint32_t hold_ms;
    Indicator indicator;
};

const RideLeg DEMO_RIDE[] = {
    {0, 3000, Indicator::None},
    {57, 8000, Indicator::None},
    {35, 4000, Indicator::Left},
    {72, 8000, Indicator::None},
    {20, 4000, Indicator::Right},
    {45, 6000, Indicator::None},
    {0, 4000, Indicator::None},
};
constexpr int DEMO_RIDE_LEG_COUNT = sizeof(DEMO_RIDE) / sizeof(DEMO_RIDE[0]);

struct Simulator {
    evb_vehicle_state_t state;
    float speed_kmh;
    float trip_km;
    float odometer_km;
    int leg_index;
    uint32_t leg_elapsed_ms;
    uint32_t blink_elapsed_ms;
    uint32_t drain_elapsed_ms;
    uint32_t clock_elapsed_ms;
};

Simulator sim;

float acceleration_limit_kmh_per_s(evb_ride_mode_t mode)
{
    return mode == EVB_RIDE_MODE_SPORT ? 12.0f : 6.0f;
}

int clamp_percent(float value)
{
    if (value < 0.0f) {
        return 0;
    }
    if (value > 100.0f) {
        return 100;
    }
    return (int)(value + 0.5f);
}

void move_speed_towards_leg_target(float seconds)
{
    const RideLeg &leg = DEMO_RIDE[sim.leg_index];
    float target = (float)leg.target_speed_kmh;
    float change = target - sim.speed_kmh;
    float limit = (change > 0.0f ? acceleration_limit_kmh_per_s(sim.state.ride_mode) : 9.0f) * seconds;
    if (change > limit) {
        change = limit;
    } else if (change < -limit) {
        change = -limit;
    }
    sim.speed_kmh += change;

    float acceleration = change / seconds;
    sim.state.speed_kmh = (int)(sim.speed_kmh + 0.5f);
    sim.state.power_percent = clamp_percent(acceleration * 8.0f + sim.speed_kmh * 0.5f);
    sim.state.motor_rpm_percent = clamp_percent(sim.speed_kmh * 1.1f);
    sim.state.gear = sim.speed_kmh < 0.5f && leg.target_speed_kmh == 0 ? EVB_GEAR_PARK : EVB_GEAR_DRIVE;
}

void move_to_next_leg_when_done(uint32_t elapsed_ms)
{
    const RideLeg &leg = DEMO_RIDE[sim.leg_index];
    bool reached = sim.state.speed_kmh == leg.target_speed_kmh;
    if (reached) {
        sim.leg_elapsed_ms += elapsed_ms;
    }
    if (sim.leg_elapsed_ms >= leg.hold_ms) {
        sim.leg_index = (sim.leg_index + 1) % DEMO_RIDE_LEG_COUNT;
        sim.leg_elapsed_ms = 0;
    }
}

void blink_indicator_of_current_leg(uint32_t elapsed_ms)
{
    Indicator indicator = DEMO_RIDE[sim.leg_index].indicator;
    sim.blink_elapsed_ms = (sim.blink_elapsed_ms + elapsed_ms) % (2 * INDICATOR_BLINK_MS);
    bool lamp_lit = sim.blink_elapsed_ms < INDICATOR_BLINK_MS;
    sim.state.indicator_left_on = indicator == Indicator::Left && lamp_lit;
    sim.state.indicator_right_on = indicator == Indicator::Right && lamp_lit;
}

void add_travelled_distance(float seconds)
{
    float km = sim.speed_kmh * seconds / 3600.0f;
    sim.trip_km += km;
    sim.odometer_km += km;
    sim.state.trip_km = (int)sim.trip_km;
    sim.state.odometer_km = (int)sim.odometer_km;
}

void drain_battery(uint32_t elapsed_ms)
{
    sim.drain_elapsed_ms += elapsed_ms;
    if (sim.drain_elapsed_ms >= BATTERY_DRAIN_MS) {
        sim.drain_elapsed_ms = 0;
        sim.state.battery_percent--;
        if (sim.state.battery_percent < BATTERY_REFILL_BELOW_PERCENT) {
            sim.state.battery_percent = BATTERY_REFILL_TO_PERCENT;
        }
    }
    sim.state.range_km = (int)(sim.state.battery_percent * RANGE_KM_PER_BATTERY_PERCENT);
    sim.state.pack_temp_c = 52 + sim.state.power_percent / 10;
}

void advance_clock(uint32_t elapsed_ms)
{
    sim.clock_elapsed_ms += elapsed_ms;
    if (sim.clock_elapsed_ms >= 60000) {
        sim.clock_elapsed_ms -= 60000;
        sim.state.clock_minutes++;
        if (sim.state.clock_minutes == 60) {
            sim.state.clock_minutes = 0;
            sim.state.clock_hours = (sim.state.clock_hours + 1) % 24;
        }
    }
}

void step_demo_ride(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    float seconds = TICK_MS / 1000.0f;
    move_speed_towards_leg_target(seconds);
    move_to_next_leg_when_done(TICK_MS);
    blink_indicator_of_current_leg(TICK_MS);
    add_travelled_distance(seconds);
    drain_battery(TICK_MS);
    advance_clock(TICK_MS);
    evb_ride_screen_show_state(&sim.state);
}

void toggle_ride_mode(void)
{
    sim.state.ride_mode = sim.state.ride_mode == EVB_RIDE_MODE_ECO ? EVB_RIDE_MODE_SPORT : EVB_RIDE_MODE_ECO;
    evb_ride_screen_show_state(&sim.state);
}

void toggle_alerts_muted(void)
{
    sim.state.alerts_muted = !sim.state.alerts_muted;
    evb_ride_screen_show_state(&sim.state);
}

/* The design's ride frame: 57 km/h, 40 %, 60 C, trip 9, 01:11 am. */
void load_design_ride_values(void)
{
    sim = Simulator();
    sim.state.speed_kmh = 0;
    sim.state.battery_percent = 40;
    sim.state.pack_temp_c = 60;
    sim.state.range_km = 20;
    sim.state.ambient_temp_c = 27;
    sim.state.clock_hours = 1;
    sim.state.clock_minutes = 11;
    sim.state.gear = EVB_GEAR_PARK;
    sim.state.ride_mode = EVB_RIDE_MODE_ECO;
    sim.state.low_beam_on = true;
    sim.state.phone_connected = true;
    sim.trip_km = 9.0f;
    sim.odometer_km = 9.0f;
    sim.state.trip_km = 9;
    sim.state.odometer_km = 9;
}

} // namespace

extern "C" void evb_ride_simulator_start(void)
{
    load_design_ride_values();

    evb_ride_screen_handlers_t handlers = {};
    handlers.on_ride_mode_tapped = toggle_ride_mode;
    handlers.on_alerts_tapped = toggle_alerts_muted;
    evb_ride_screen_create(&handlers);
    evb_ride_screen_show_state(&sim.state);

    lv_timer_create(step_demo_ride, TICK_MS, NULL);
}
