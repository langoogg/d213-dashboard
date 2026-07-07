/* vehicle_physics.c - 移植自 Qt Frame::physicsUpdate() */
#include "vehicle_physics.h"
#include <math.h>

AccelSegment g_accel_segs[] = {
    {0, 100, 24.4, 24.4},
    {100, 200, 12.8, 11.0},
    {200, 210, 9.5, 9.5},
    {210, 220, 8.0, 8.0},
    {220, 230, 6.5, 6.5},
    {230, 240, 5.2, 5.2},
    {240, 250, 4.0, 4.0},
    {250, 260, 2.9, 2.9},
    {260, 270, 1.8, 1.8},
    {270, 280, 0.8, 0.8}
};
int g_accel_seg_count = 10;
double g_shift_rpms[] = {7500, 7300, 6900, 6600, 6300, 6000, 5800};
double g_gear_ratios[] = {0, 104.0, 82.0, 54.0, 33.0, 25.5, 24.5, 20.5};

static double calc_accel(double spd) {
    int i;
    for (i = 0; i < g_accel_seg_count; i++) {
        if (spd >= g_accel_segs[i].start && spd < g_accel_segs[i].end) {
            double t = (spd - g_accel_segs[i].start) /
                       (g_accel_segs[i].end - g_accel_segs[i].start);
            return g_accel_segs[i].start_accel +
                   t * (g_accel_segs[i].end_accel - g_accel_segs[i].start_accel);
        }
    }
    return 0.0;
}

static double gear_ratio(int gear) {
    if (gear >= 1 && gear <= 7) return g_gear_ratios[gear];
    return 20.0;
}

void vehicle_physics_init(VehiclePhysics *p) {
    p->speed = 0; p->rpm = IDLE_RPM;
    p->physics_speed = 0; p->physics_rpm = IDLE_RPM;
    p->dsg_gear = 1; p->gear = 0; p->engine_on = 0;
    p->throttle = 0;
    p->fuel_liters = 41.25;   /* 55L * 0.75 */
    p->coolant_temp = 65.0;
    p->odometer = 0; p->trip_a = 0;
}

void vehicle_physics_update(VehiclePhysics *p, double dt) {
    if (!p->engine_on || p->gear == 0) {
        p->physics_speed = 0;
        p->physics_rpm = IDLE_RPM;
        p->dsg_gear = 1;
        p->speed = 0;
        p->rpm = (int)IDLE_RPM;
        return;
    }

    if (p->gear == 1) { /* N 档空轰, 合理速率 */
        p->physics_speed = 0;
        p->speed = 0;
        if (p->throttle) {
            p->physics_rpm = fmin(MAX_RPM, p->physics_rpm + NEUTRAL_RPM_RATE * dt);
        } else {
            p->physics_rpm = fmax(IDLE_RPM, p->physics_rpm - NEUTRAL_RPM_RATE * dt);
        }
        p->rpm = (int)p->physics_rpm;
        return;
    }

    if (p->gear == 2) { /* D 档 */
        double spd = p->physics_speed;
        if (p->throttle) {
            double acc = calc_accel(spd);
            spd = fmin(MAX_SPEED, spd + acc * dt);
        } else {
            spd = fmax(0, spd - COAST_DECEL * dt);
        }
        p->physics_speed = spd;

        int ideal = p->dsg_gear;
        double cur_rpm = spd * gear_ratio(p->dsg_gear);
        double shift_rpm = (p->dsg_gear - 1 < 7)
            ? g_shift_rpms[p->dsg_gear - 1] : 7000.0;

        if (p->throttle) {
            if (cur_rpm > shift_rpm && p->dsg_gear < 7)
                ideal = p->dsg_gear + 1;
        } else {
            if (cur_rpm < DOWN_SHIFT_RPM && p->dsg_gear > 1)
                ideal = p->dsg_gear - 1;
        }

        if (ideal != p->dsg_gear) p->dsg_gear = ideal;

        double calc_rpm = spd * gear_ratio(p->dsg_gear);
        if (calc_rpm < IDLE_RPM) calc_rpm = IDLE_RPM;
        if (!p->throttle && spd < 1.0) calc_rpm = IDLE_RPM;

        p->physics_rpm = calc_rpm;
        p->speed = (int)spd;
        p->rpm = (int)calc_rpm;

        double dist = spd * dt / 3600.0;
        p->odometer += dist;
        p->trip_a += dist;

        /* 油耗: 换算为实际升/秒 */
        double instant = p->throttle ? (12.0 + spd / 100.0 * 8.0) : (5.0 + spd / MAX_SPEED * 15.0);
        p->fuel_liters = fmax(0, p->fuel_liters - instant * FUEL_RATE * dt);

        /* 水温: 实际°C, 50~130 */
        double target = COOLANT_MIN + 80.0 * (0.3 + (p->rpm / MAX_RPM) * 0.6);
        p->coolant_temp += (target - p->coolant_temp) * TEMP_SMOOTH * dt;
    }
}
