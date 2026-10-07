#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EVB_RIDE_MODE_ECO,
    EVB_RIDE_MODE_SPORT,
} evb_ride_mode_t;

typedef enum {
    EVB_GEAR_REVERSE,
    EVB_GEAR_PARK,
    EVB_GEAR_DRIVE,
} evb_gear_t;

/* Everything the ride screen shows. Fill it from the simulator now, from CAN later. */
typedef struct {
    int speed_kmh;
    int power_percent;
    int motor_rpm_percent;
    int battery_percent;
    int pack_temp_c;
    int range_km;
    int odometer_km;
    int trip_km;
    int ambient_temp_c;
    int tyre_front_psi_x10;
    int tyre_rear_psi_x10;
    int fault_code;
    int clock_hours;
    int clock_minutes;
    evb_gear_t gear;
    evb_ride_mode_t ride_mode;
    bool indicator_left_on;
    bool indicator_right_on;
    bool high_beam_on;
    bool low_beam_on;
    bool vehicle_warning_on;
    bool abs_fault_on;
    bool side_stand_down;
    bool charger_plugged;
    bool phone_connected;
    bool alerts_muted;
} evb_vehicle_state_t;
