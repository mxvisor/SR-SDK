/* The guest's data at the addresses of the original (Game_guestdata.c);
 * guest_data_in_memory=yes in game.cfg */

#if !defined(_GAME_GUESTDATA_H_INCLUDED_)
#define _GAME_GUESTDATA_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

/* how much of the guest's memory from pointer_offset up the data needs */
unsigned int Game_GuestLowMemory(void);
/* maps the guest's low memory and copies the data of the game into it */
void Game_GuestDataStart(void);

#ifdef __cplusplus
}
#endif

#endif /* _GAME_GUESTDATA_H_INCLUDED_ */
