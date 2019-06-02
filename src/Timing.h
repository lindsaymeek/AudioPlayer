#ifndef TIMING_H
#define TIMING_H

/* 
**  PHILIPS ARM 2005 DESIGN CONTEST
**  ENTRY AR1757
**  FLASH CARD AUDIO PLAYER FOR HEAD END UNIT
**                                                                          
**  TIMING.H:  Timer and Stereo DAC output generation                                
*/  

#include "types.h"

extern void init_timing(void);

// set sample frequency
void set_dac_rate(int Hz);

// timing functions
u32 mark(void);

u32 elapsed_sec(u32 mark);

void delay(int ticks);
   
void delay_100ms(void);

// size of output buffer. must be multiple of 512
#define BUFSIZE 2048
	
// get free sample buffer (double buffered) calling the designated polling function	
s16 *get_buffer(int (*poll_fn)());

// clear sample buffers
void clear_buffers(void);

#endif
