/**
 *
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is furnished to do
 *  so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

/* The gyroscope as a pointer (Game_gyro.h).
 *
 * The sensor gives the angular speed about the axes of the device as it is
 * held upright (SDL_sensor.h: x to the right, y up, z out of the screen, in
 * radians a second, counterclockwise); the axes do not turn with the screen,
 * so the display's orientation picks the screen's axes out of them.  The
 * speed of the last reading (SDL_SensorUpdate, here: SDL's pump reads the
 * sensors only while their events are on) is counted over the time since the
 * pump before:
 * SDL2 on Android gives the readings no time of their own, and the pump comes
 * every 10 ms at most (the sensor sends 60 a second). */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <SDL.h>

#include "Game_gyro.h"
#include "Game_osd.h"
#include "touchgamepad.h"
#include "Game_hooks.h"

/* below this the speed is the sensor's drift, not a turn (radians a second) */
#define GYRO_DEAD_ZONE 0.01

static SDL_atomic_t gyro_wanted;        /* the mod: the panel's and the cfg's */
static SDL_atomic_t gyro_open;          /* the pump's: the sensor is open */
static SDL_atomic_t gyro_speed = { 5 }; /* Gyro_Mouse_Speed */
static SDL_SpinLock gyro_lock;
static double gyro_dx, gyro_dy;         /* under gyro_lock: the motion not yet taken */

/* the pump's own */
static SDL_Sensor *gyro_sensor;
static int gyro_failed;                 /* no sensor: not looked for again */
static Uint64 gyro_last;

int Game_GyroConfig(const char *key, const char *value)
{
    if (strcasecmp(key, "Gyro_Mouse") == 0)
    {
        if (strcasecmp(value, "on") == 0) SDL_AtomicSet(&gyro_wanted, 1);
        else if (strcasecmp(value, "off") == 0) SDL_AtomicSet(&gyro_wanted, 0);
        return 1;
    }
    if (strcasecmp(key, "Gyro_Mouse_Speed") == 0)
    {
        const int speed = atoi(value);
        if (speed >= 1 && speed <= 9) SDL_AtomicSet(&gyro_speed, speed);
        return 1;
    }
    return 0;
}

static void gyro_close(void)
{
    if (gyro_sensor != NULL) SDL_SensorClose(gyro_sensor);
    gyro_sensor = NULL;
    SDL_AtomicSet(&gyro_open, 0);
    SDL_AtomicLock(&gyro_lock);
    gyro_dx = gyro_dy = 0;
    SDL_AtomicUnlock(&gyro_lock);
}

static void gyro_start(void)
{
    int index;

    if (SDL_InitSubSystem(SDL_INIT_SENSOR) != 0)
    {
        fprintf(stderr, "Gyro_Mouse: SDL: %s\n", SDL_GetError());
        gyro_failed = 1;
        return;
    }
    /* the readings are taken from the sensor, not from the queue; with its
     * events off SDL's pump no longer reads the sensors, Game_GyroPump does */
    SDL_EventState(SDL_SENSORUPDATE, SDL_IGNORE);
    for (index = 0; index < SDL_NumSensors(); index++)
    {
        if (SDL_SensorGetDeviceType(index) != SDL_SENSOR_GYRO) continue;
        gyro_sensor = SDL_SensorOpen(index);
        if (gyro_sensor != NULL) break;
    }
    if (gyro_sensor == NULL)
    {
        fprintf(stderr, "Gyro_Mouse: no gyroscope\n");
        SDL_QuitSubSystem(SDL_INIT_SENSOR);
        gyro_failed = 1;
        return;
    }
    fprintf(stderr, "Gyro_Mouse: %s\n", SDL_SensorGetName(gyro_sensor));
    gyro_last = SDL_GetPerformanceCounter();
    SDL_AtomicSet(&gyro_open, 1);
}

static double dead_zone(double speed)
{
    return fabs(speed) < GYRO_DEAD_ZONE ? 0 : speed;
}

void Game_GyroPump(void)
{
    float data[3];
    double right, up, seconds, scale;
    Uint64 now;

    if (!SDL_AtomicGet(&gyro_wanted))
    {
        if (gyro_sensor != NULL)
        {
            gyro_close();
            SDL_QuitSubSystem(SDL_INIT_SENSOR);
        }
        return;
    }
    if (gyro_sensor == NULL)
    {
        if (!gyro_failed) gyro_start();
        return;
    }

    now = SDL_GetPerformanceCounter();
    seconds = (double)(now - gyro_last) / (double)SDL_GetPerformanceFrequency();
    gyro_last = now;
    /* a pause of the program (Android parks the pump) is no turn */
    if (seconds > 0.1) return;
    SDL_SensorUpdate();
    if (SDL_SensorGetData(gyro_sensor, data, 3) != 0) return;

    /* the speeds about the screen's axes: right (x), up (y) */
    switch (SDL_GetDisplayOrientation(0))
    {
        case SDL_ORIENTATION_LANDSCAPE:         /* turned counterclockwise: its right side up */
            right = -data[1];
            up = data[0];
            break;
        case SDL_ORIENTATION_LANDSCAPE_FLIPPED: /* its left side up */
            right = data[1];
            up = -data[0];
            break;
        case SDL_ORIENTATION_PORTRAIT_FLIPPED:
            right = -data[0];
            up = -data[1];
            break;
        default:
            right = data[0];
            up = data[1];
            break;
    }

    /* widths of the picture a radian: 0.3 for each step of the speed; a turn
     * to the left (about up, counterclockwise) moves to the left, a tilt of
     * the top towards the player (about right) moves up; the picture is 4:3 */
    scale = 0.3 * SDL_AtomicGet(&gyro_speed) * seconds;
    SDL_AtomicLock(&gyro_lock);
    gyro_dx -= dead_zone(up) * scale;
    gyro_dy -= dead_zone(right) * scale * 4.0 / 3.0;
    SDL_AtomicUnlock(&gyro_lock);
}

int Game_GyroMod(void)
{
    return SDL_AtomicGet(&gyro_wanted);
}

int Game_GyroOn(void)
{
    return SDL_AtomicGet(&gyro_wanted) && SDL_AtomicGet(&gyro_open);
}

void Game_GyroTake(double *dx, double *dy)
{
    SDL_AtomicLock(&gyro_lock);
    *dx = gyro_dx;
    *dy = gyro_dy;
    gyro_dx = gyro_dy = 0;
    SDL_AtomicUnlock(&gyro_lock);
}

/* the panel's items: the mod, and its speed */
static void gyro_item_press(void)
{
    SDL_AtomicSet(&gyro_wanted, !SDL_AtomicGet(&gyro_wanted));
    TouchGamepad_Relayout();
}

static const char *gyro_item_state(void)
{
    return SDL_AtomicGet(&gyro_wanted) ? "on" : "off";
}

static const Game_OsdItem gyro_item = {
    "Gyroscope as mouse", NULL, gyro_item_press, gyro_item_state, "Gyro_Mouse", NULL, NULL, NULL, NULL
};

static const char *const speed_names[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

static int speed_item_active(void)
{
    return SDL_AtomicGet(&gyro_wanted);
}

static void speed_item_choose(int index)
{
    if (index >= 0 && index < 9) SDL_AtomicSet(&gyro_speed, index + 1);
}

static void speed_item_press(void)
{
    speed_item_choose(SDL_AtomicGet(&gyro_speed) % 9);
}

static void speed_item_step(int dir)
{
    speed_item_choose(SDL_AtomicGet(&gyro_speed) - 1 + dir);
}

static const char *speed_item_state(void)
{
    return speed_names[SDL_AtomicGet(&gyro_speed) - 1];
}

static int speed_item_count(void)
{
    return 9;
}

static const char *speed_item_value(int index)
{
    return speed_names[index];
}

static const Game_OsdItem speed_item = {
    "Gyroscope speed", speed_item_active, speed_item_press, speed_item_state, "Gyro_Mouse_Speed", speed_item_step,
    speed_item_count, speed_item_value, speed_item_choose
};

void Game_GyroAddItems(void)
{
    Game_OsdAddItem(&gyro_item);
    Game_OsdAddItem(&speed_item);
}
