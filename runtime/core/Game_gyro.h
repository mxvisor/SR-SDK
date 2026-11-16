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

/* The gyroscope of the host (a phone, a tablet) as a pointer: the mod
 * Gyro_Mouse of the cfg (off by default).  Turning the device moves the pointer
 * of the picture: a turn about the screen's vertical axis moves it across, a
 * tilt about the horizontal one moves it up and down, whichever way the screen
 * is turned.  The motion is counted here in fractions of the picture (4:3) and
 * a pointer of the platform takes it (Game_GyroTake): the DOS mouse driver
 * moves its own position by it, and a touch away from the on-screen buttons
 * puts that position under the finger and presses the button.  A layout of
 * the on-screen buttons may give a button the mouse's left button while the
 * mod is on (Game_GyroMod, TOUCHGAMEPAD_MOUSE_LEFT of touchgamepad.h): a
 * switch of the mod places the buttons again.
 *
 * SDL2 reads the sensor on the thread that pumps the events, and on Android
 * only on the thread that opened it: the sensor is opened, read and closed in
 * Game_GyroPump, which that thread calls before each pump.  Nothing is opened
 * while the mod is off.  Game_GyroTake may be called from any thread. */

#if !defined(_GAME_GYRO_H_INCLUDED_)
#define _GAME_GYRO_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

/* the cfg keys Gyro_Mouse=on/off and Gyro_Mouse_Speed=1..9 (5: the width of
 * the picture in a turn of 38 degrees); nonzero if it is one of them */
int  Game_GyroConfig(const char *key, const char *value);
/* the thread that pumps SDL's events, before each pump: opens or closes the
 * sensor as the mod is turned on or off, and counts its motion */
void Game_GyroPump(void);
/* nonzero while the mod is on, with a sensor or not */
int  Game_GyroMod(void);
/* nonzero while the mod is on and the sensor is open */
int  Game_GyroOn(void);
/* the motion since the last call, in widths (*dx, right) and heights (*dy,
 * down) of the picture */
void Game_GyroTake(double *dx, double *dy);
/* the panel's items of the mod, for the GAME_OSD_ITEMS() of a game */
void Game_GyroAddItems(void);

#ifdef __cplusplus
}
#endif

#endif /* _GAME_GYRO_H_INCLUDED_ */
