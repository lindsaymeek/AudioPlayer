
/*
**  PHILIPS ARM 2005 DESIGN CONTEST
**  ENTRY AR1757
**  FLASH CARD AUDIO PLAYER FOR HEAD END UNIT
**
** Mainline
*/

#include <LPC213x.H>                            /* LPC21xx definitions */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "timing.h"
#include "ffs.h"
#include "serial.h"
#include "headend.h"
#include "control.h"

char file[16];  		// active file
char dir[16];			// active directory
int dirs,tracks;	 	// total # of dirs and tracks
int dirn,trackn;	 	// current play position
bool playing;				// play active
static bool repeat;		 	// track repeat active
static bool shuffle;	  	// track shuffle active
static u32 timemark;	  	// track timer (ticks)
int secs;				  	// converted to seconds
static int last_secs;	  	// elapsed second detector

// playback control 

void stop(void)
{

	playing=FALSE;
}

void play(void)
{
#ifndef SIMULATION
	if(trackn == scan_tracks(dirn,trackn,file,dir))
#else
	if(1)
#endif
	{
		playing=TRUE;

	    puts(file);	
		puts(" playing\n\r");


	}
	else
		stop();
	
}

void restart_dir(void)
{
#ifndef SIMULATION
	tracks=scan_tracks(dirn,-1,file,dir);
#else
	tracks=1;
#endif

	trackn=1;

    puts(dir);
	puts(" ");
    puts(itoa(tracks,8));	
	puts(" tracks found\n\r");

	play();
}

void restart_disk(void)
{
	shuffle=FALSE;
	repeat=FALSE;

	secs=0;
	last_secs=-1;

#ifndef SIMULATION
	dirs=scan_dirs(-1,dir);
#else
	dirs=1;
#endif

    puts(itoa(dirs,8));	
	puts(" directories found\n\r");

	restart_dir();
	stop();
}


void prev_track(void)
{
	if(repeat)
		play();
	else
	{
	 if(!shuffle)
	 {
	  if(trackn > 0)
	  {
		trackn--;
		play();
	  }
	  else
		stop();
     }
	 else
	 {
	   if(tracks)
	   {
	    trackn = (rand() % tracks-1)+1;
	    play();
	   }
	   else
	    stop();
	 }
	}
}

void next_track(void)
{ 
    if(repeat)
		play();
	else
	{

	if(!shuffle)
	 {
	  if(trackn < tracks)
	  {
		trackn++;
		play();
	  }
	  else
		stop();
     }
	 else
	 {
	   if(tracks)
	   {
	    trackn = (rand() % tracks-1)+1;
	    play();
	   }
	   else
	    stop();
	 }
	}

}

void prev_dir(void)
{
	if(dirn > 0)
	{
		dirn--;
		restart_dir();
	}
}

void next_dir(void)
{
	if(dirn < dirs)
	{
		dirn++;
		restart_dir();
	}
}

void toggle_shuffle(void)
{
	shuffle ^= TRUE;
}

void toggle_repeat(void)
{
	repeat ^= TRUE;
}

	
// 
// background polling function
//
int poll(void)
{

	if(playing)
	{

	  secs=elapsed_sec(timemark);

		if(last_secs != secs)
		{
	//		puts(itoa(secs));puts("\r");
			last_secs=secs;
		}

	}
	else
		secs=0;
   
   // scan head end for commands
   if(poll_headend())
		return 1;

	// serial control used during testing
	if(kbhit())
	{
		switch(tolower(getchar()))
		{
			default:
				return 0;
			case 'p':
				play(); break;
			case 's':
				stop(); break;
			case '6':
				next_track(); break;
			case '4':
				prev_track(); break;
			case '8':
				next_dir(); break;
			case '2':
				prev_dir(); break;
		
			case 'r':
				toggle_repeat(); return 0;
			case '?':
				toggle_shuffle(); return 0;
				
		}

		return 1;
	
	}
	else
		return 0;
}


#ifndef SIMULATION

// read a long from the file 
static bool rdl(int fd,u32 *x)
{
	return sizeof(u32)==read(fd,(u8 *)x,sizeof(u32));
}

// read a long from the file
static bool rdw(int fd,u16 *x)
{
	return sizeof(u16)==read(fd,(u8 *)x,sizeof(u16));
}

//
// Stream wav file to DAC 
//
static bool play_wav(char *file)
{

	u32 x,size,sample_rate;
	u16 y,bits_per_sample;
	bool rc;	
	s16 *tbuffer;
	u16 actual;
    int fd=-1;

	rc=FALSE;

	fd=open(file,O_RDONLY,0);
	if(fd < 0)
		goto  end;

	if(!rdl(fd,&x) || x!=0x46464952)	// RIFF
		goto   end;

	if(!rdl(fd,&size))
		goto   end;

	if(!rdl(fd,&x) || x!=0x45564157) // WAVE
		goto   end;
			
	if(!rdl(fd,&x) || x!=0x20746d66) // FMT
		goto   end;

	if(!rdl(fd,&x) || x!=16) // subchunk size
		goto   end;

	if(!rdw(fd,&y) || y!=1) // audio format
		goto   end;
		 
	if(!rdw(fd,&y) || y!=2)	// channels	.. must be stereo
		goto   end;

	if(!rdl(fd,&sample_rate))
		goto   end;

	set_dac_rate(sample_rate);
 	
	// skip byte rate
	rdl(fd,&x);
	// skip block align
	rdw(fd,&y);
	// bits per sample only likes 16 at the moment
	if(!rdw(fd,&bits_per_sample) || bits_per_sample!=16)
		goto   end;

   	// chunk2
	if(!rdl(fd,&x) || x!=0x61746164)
		goto   end;

	// size of sample data
	if(!rdl(fd,&size))
		goto   end;

	// align file position on sector boundary
	lseek(fd, 0, SEEK_SET);

	size = size >> 9;

	// reset time mark
	timemark = mark();
	last_secs=-1;
	tbuffer=NULL;

   	// main streaming loop
	while(size)
	{
		// request sample buffer from DAC data pump
		tbuffer=get_buffer(poll);

		// abort?
		if(tbuffer==NULL)
			break;

		// load disk sectors directly into it
		actual=read_sectors(fd, (u8 *)tbuffer, BUFSIZE>>8);

		// done track?
		if(!actual)
			break;

		// adjust length
		size-=actual;
	}

	rc=TRUE;
	   
	clear_buffers();

	// advance if no problems
	if(tbuffer!=NULL)
		next_track();

end:
	
	if(fd >= 0)
		close(fd);

  	return rc;
	
}


#endif

void halt(void)
{
  unsigned j;		  							/* LED var */
 
 while (1)  {                                  /* Loop forever */
    for (j = 0x010000; j < 0x800000; j <<= 1) { /* Blink LED 0,1,2,3,4,5,6 */
      IOSET1 = j;                               /* Turn on LED */
   		delay_100ms();                             /* call wait function */
      IOCLR1 = j;                               /* Turn off LED */
	  

    }
    for (j = 0x800000; j > 0x010000; j >>=1 ) { /* Blink LED 7,6,5,4,3,2,1 */
      IOSET1 = j;                               /* Turn on LED */
   		delay_100ms();                             /* call wait function */
      IOCLR1 = j;                               /* Turn off LED */


    }
  }
}

//
// mainline
//
int main (int argc,char **argv)  
{

  IODIR1 = 0xFF0000;                            /* P1.16..23 defined as Outputs */

  // start up the DAC/timing subsystem	 
  init_timing();
  
  // fire up serial interface
  init_serial();
  
  // start head-end interface
  init_headend();

#ifndef SIMULATION
  
 // initialise the MMC
  
 if(TRUE==hd_qry())
 {
 	if(TRUE==hd_mbr())
	{
 		if(TRUE==hd_bpb())	
		{
			restart_disk();

#endif
			
			for(;;)
			{
			 if(!playing) 
			 	puts("Stopped\n\r");
			 while(!playing)
				poll();
#ifndef SIMULATION
			 play_wav(file);
#endif
			}	

#ifndef SIMULATION
		}	
	}
  }
#endif

  // never gets here
  halt();
  
  return 0;
}
