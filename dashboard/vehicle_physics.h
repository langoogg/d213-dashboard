/* vehicle_physics.h - LVGL 移植 from Qt Frame::physicsUpdate() */
#ifndef VEHICLE_PHYSICS_H
#define VEHICLE_PHYSICS_H

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SPEED  280.0
#define MAX_RPM    8000.0
#define IDLE_RPM   800.0
#define DOWN_SHIFT_RPM  1500.0
#define NEUTRAL_RPM_RATE 3000.0
#define FUEL_TANK_LITERS 55.0
#define COOLANT_MIN 50.0
#define COOLANT_MAX  130.0
#define COAST_DECEL   15.0
#define FUEL_RATE     0.00275
#define TEMP_SMOOTH   2.0

typedef struct {
    double start, end, start_accel, end_accel;
} AccelSegment;

typedef struct {
    double speed;           /* km/h */
    double rpm;             /* RPM */
    double physics_speed;
    double physics_rpm;
    int    dsg_gear;        /* 1-7 */
    int    gear;            /* 0=P, 1=N, 2=D */
    int    engine_on;
    int    throttle;
    double fuel_liters;     /* 升, 满箱55L */
    double coolant_temp;    /* 摄氏度 */
    double odometer;
    double trip_a;
} VehiclePhysics;

extern AccelSegment g_accel_segs[];
extern int g_accel_seg_count;
extern double g_shift_rpms[];
extern double g_gear_ratios[];

void vehicle_physics_init(VehiclePhysics *p);
void vehicle_physics_update(VehiclePhysics *p, double dt);

#ifdef __cplusplus
}
#endif
#endif
