#include "evb_vehicle_simulator.h"

namespace {

constexpr uint32_t INDICATOR_BLINK_MS = 400;
constexpr uint32_t BATTERY_DRAIN_MS = 20000;
constexpr int BATTERY_REFILL_BELOW_PERCENT = 8;
constexpr int BATTERY_REFILL_TO_PERCENT = 80;
constexpr float RANGE_KM_PER_BATTERY_PERCENT = 0.5f;
constexpr float BRAKING_KMH_PER_S = 9.0f;

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
    float speed_kmh;
    float trip_km;
    float odometer_km;
    int leg_index;
    uint32_t leg_elapsed_ms;
    uint32_t blink_elapsed_ms;
    uint32_t drain_elapsed_ms;
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

void move_speed_towards(evb_vehicle_state_t *vehicle, int target_speed_kmh, float seconds)
{
    float change = (float)target_speed_kmh - sim.speed_kmh;
    float limit = (change > 0.0f ? acceleration_limit_kmh_per_s(vehicle->ride_mode) : BRAKING_KMH_PER_S) * seconds;
    if (change > limit) {
        change = limit;
    } else if (change < -limit) {
        change = -limit;
    }
    sim.speed_kmh += change;

    float acceleration = change / seconds;
    vehicle->speed_kmh = (int)(sim.speed_kmh + 0.5f);
    vehicle->power_percent = clamp_percent(acceleration * 8.0f + sim.speed_kmh * 0.5f);
    vehicle->motor_rpm_percent = clamp_percent(sim.speed_kmh * 1.1f);
    vehicle->gear = sim.speed_kmh < 0.5f && target_speed_kmh == 0 ? EVB_GEAR_PARK : EVB_GEAR_DRIVE;
}

void move_to_next_leg_when_done(const evb_vehicle_state_t *vehicle, uint32_t elapsed_ms)
{
    const RideLeg &leg = DEMO_RIDE[sim.leg_index];
    if (vehicle->speed_kmh == leg.target_speed_kmh) {
        sim.leg_elapsed_ms += elapsed_ms;
    }
    if (sim.leg_elapsed_ms >= leg.hold_ms) {
        sim.leg_index = (sim.leg_index + 1) % DEMO_RIDE_LEG_COUNT;
        sim.leg_elapsed_ms = 0;
    }
}

void blink_indicator(evb_vehicle_state_t *vehicle, Indicator indicator, uint32_t elapsed_ms)
{
    sim.blink_elapsed_ms = (sim.blink_elapsed_ms + elapsed_ms) % (2 * INDICATOR_BLINK_MS);
    bool lamp_lit = sim.blink_elapsed_ms < INDICATOR_BLINK_MS;
    vehicle->indicator_left_on = indicator == Indicator::Left && lamp_lit;
    vehicle->indicator_right_on = indicator == Indicator::Right && lamp_lit;
}

void add_travelled_distance(evb_vehicle_state_t *vehicle, float seconds)
{
    float km = sim.speed_kmh * seconds / 3600.0f;
    sim.trip_km += km;
    sim.odometer_km += km;
    vehicle->trip_km = (int)sim.trip_km;
    vehicle->odometer_km = (int)sim.odometer_km;
}

void drain_battery(evb_vehicle_state_t *vehicle, uint32_t elapsed_ms)
{
    sim.drain_elapsed_ms += elapsed_ms;
    if (sim.drain_elapsed_ms >= BATTERY_DRAIN_MS) {
        sim.drain_elapsed_ms = 0;
        vehicle->battery_percent--;
        if (vehicle->battery_percent < BATTERY_REFILL_BELOW_PERCENT) {
            vehicle->battery_percent = BATTERY_REFILL_TO_PERCENT;
        }
    }
    vehicle->range_km = (int)(vehicle->battery_percent * RANGE_KM_PER_BATTERY_PERCENT);
    vehicle->pack_temp_c = 52 + vehicle->power_percent / 10;
}

} // namespace

void evb_vehicle_simulator_reset(evb_vehicle_state_t *vehicle)
{
    sim = Simulator();
    sim.trip_km = 9.0f;
    sim.odometer_km = 9.0f;

    *vehicle = evb_vehicle_state_t();
    vehicle->battery_percent = 40;
    vehicle->pack_temp_c = 60;
    vehicle->range_km = 20;
    vehicle->odometer_km = 9;
    vehicle->trip_km = 9;
    vehicle->ambient_temp_c = 27;
    vehicle->tyre_front_psi_x10 = 320;
    vehicle->tyre_rear_psi_x10 = 320;
    vehicle->gear = EVB_GEAR_PARK;
    vehicle->ride_mode = EVB_RIDE_MODE_ECO;
    vehicle->low_beam_on = true;
}

void evb_vehicle_simulator_step(evb_vehicle_state_t *vehicle, uint32_t elapsed_ms, bool may_ride)
{
    float seconds = elapsed_ms / 1000.0f;
    if (may_ride) {
        move_speed_towards(vehicle, DEMO_RIDE[sim.leg_index].target_speed_kmh, seconds);
        move_to_next_leg_when_done(vehicle, elapsed_ms);
        blink_indicator(vehicle, DEMO_RIDE[sim.leg_index].indicator, elapsed_ms);
    } else {
        move_speed_towards(vehicle, 0, seconds);
        blink_indicator(vehicle, Indicator::None, elapsed_ms);
    }
    vehicle->charger_plugged = !may_ride && vehicle->speed_kmh == 0;
    add_travelled_distance(vehicle, seconds);
    drain_battery(vehicle, elapsed_ms);
}
