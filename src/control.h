
#ifndef CONTROL_H
#define CONTROL_H

/*
**  PHILIPS ARM 2005 DESIGN CONTEST
**  ENTRY AR1757
**  FLASH CARD AUDIO PLAYER FOR HEAD END UNIT
**                                                                          
**  CONTROL.H: Play control interface
*/

extern void next_track(void);
extern void next_dir(void);
extern void prev_track(void);
extern void prev_dir(void);
extern void toggle_repeat(void);
extern void toggle_shuffle(void);
extern void play(void);
extern void stop(void);
extern bool playing;
extern int trackn,dirn,secs;
extern char *hex;

#endif
