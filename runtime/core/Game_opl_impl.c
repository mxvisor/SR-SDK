/* The implementation of emu8950 (extern/emu8950: Mitsutaka Okazaki's
 * Y8950/YM3526/YM3812 emulator, MIT license), the FM chip of Game_opl.c.  This
 * file is C, not C++, as Game_tsf_impl.c is; emuadpcm.c, the Y8950's ADPCM
 * unit, is in Game_opl_adpcm_impl.c (the two have static functions of the
 * same name). */

#include "emu8950.c"
