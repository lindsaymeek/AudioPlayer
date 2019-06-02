/* 
**  PHILIPS ARM 2005 DESIGN CONTEST
**  ENTRY AR1757
**  FLASH CARD AUDIO PLAYER FOR HEAD END UNIT
**                                                                          
**  TIMING.C:  Timer and Internal / Stereo DAC output generation                                
** 
** 
*/                                                                           


#include <LPC213X.H>                          // LPC21XX Peripheral Registers
#include "timing.h"
#include "types.h"
#include <string.h>

// define this to use inbuilt 10-bit DAC, otherwise use the TLV320DAC23
#define INTERNAL_DAC

// P0.20 is the LRCLK
#define LRCLK_PIN (1L<<20)

static volatile u32 timeval;
static u16 ticks_per_sec;
   
// double sample buffers	
static s16 buffer0[BUFSIZE],buffer1[BUFSIZE],*p;
// current read counter
static u16 cnt=BUFSIZE;
// current sample rate
static int CurrentHz=0;
static volatile u16 buffer_done=0,buffer_active=0;

/* Timer Counter 0 Interrupt executes nominally at 44100 Hz or 88200 Hz for external DAC */

static void tc0 (void) __attribute__ ((interrupt));

static void tc0 (void)
{
#ifdef INTERNAL_DAC
	static s16 x;

	// combine left and right channel for mono output
	x=(*p++ ) >> 1 ;
	x+=(*p++) >> 1;
   DACR		  = 32768+x;
			
  if(!--cnt)
  {
    // swap buffers
  	cnt=BUFSIZE>>1;
	buffer_active ^= 1;
	if(!buffer_active)
		p=buffer0;
	else
		p=buffer1;
	buffer_done=1;
	timeval++;
  }
   		
#else

  // Toggle LRCLK according to sample
  if(cnt & 1)
  	IOSET0 = LRCLK_PIN;
  else
    IOCLR0 = LRCLK_PIN;

  // Load 16-bit sample into output FIFO.. this starts transmission
  SSPDR	 = *p++;

  // Count down and swap over buffers at end				
  if(!--cnt)
  {
    // swap buffers
  	cnt=BUFSIZE;
	buffer_active ^= 1;
	if(!buffer_active)
		p=buffer0;
	else
		p=buffer1;
	buffer_done=1;

	// Timing function
	timeval++;
  }
   		
#endif
      

  T0IR        = 1;                            // Clear interrupt flag
  VICVectAddr = 0xff;                            // Acknowledge Interrupt
} 

// clear buffers to zero values
void clear_buffers(void)
{
	memset(buffer0, 0, sizeof(buffer0));
	memset(buffer1, 0, sizeof(buffer1));
}

// change the dac sampling rate based on a 60MHz clock
void set_dac_rate(int Hz)
{
  if(Hz != CurrentHz)
  {
   
  CurrentHz=Hz;

  T0TCR = 2;                                  // Timer0 Disable & reset

#ifdef INTERNAL_DAC
	T0MR0 = 15000000L / Hz;	
	ticks_per_sec = (256*Hz) / (BUFSIZE>>1);
#else
	T0MR0 = 15000000L / (Hz<<1);	
	ticks_per_sec = (256*(Hz<<1)) / (BUFSIZE>>1);
#endif
  
  T0IR        = 1;                            // Clear interrupt flag
  VICVectAddr = 0;                            // Acknowledge Interrupt
  T0TCR = 1;                                  // Timer0 Enable
  }

}

// mark time point
u32 mark(void)
{
	return timeval;
}

u32 elapsed_sec(u32 timemark)
{
	return (256 * (mark() - timemark)) / ticks_per_sec;
}

// delay ticks function
void delay (int ticks)  
{                             
  u32 i;

  i = mark();
  while ((mark() - i) < ticks);                  
}

// get free sample buffer	
s16 *get_buffer(int (*poll_fn)())
{
	do 
	{
		if(poll_fn())
		{
			// abort
		    clear_buffers();

			return NULL;
		}
	}
	while(!buffer_done) ;
	
	buffer_done=0;

	if(!buffer_active)
		return buffer1;
	else
		return buffer0;
}

#define I2C_EN 64
#define I2C_START 32
#define I2C_STOP 16
#define I2C_SI 8
#define I2C_AA 4

#define DAC_ADDR 26

// Write to a nominated external DAC register via I2C bus
static bool write_dac(u8 reg,u16 value)
{
	bool lsb_sent=FALSE;

	I20CONCLR = I2C_START|I2C_STOP|I2C_SI|I2C_AA;	// Clear STA, STOP, SI, AA
	I20CONSET = I2C_EN;			// I2EN

	I20CONSET = I2C_START;		// Start bit

	// Form 16-bit write value for DAC
	value = (value & 511) | (reg << 9);

	for(;;)
	{
	 while(!(I20CONSET & I2C_SI)) ;

	 switch(I20STAT) {
	 	case 0x08:
		case 0x10:
			I20DAT = DAC_ADDR << 1;			// send address with write request
			I20CONCLR = I2C_STOP | I2C_SI;	// clear SI and STOP bit
			lsb_sent=FALSE;
			break;
 		case 0x18:
			I20DAT = (u8)(value >> 8);
			I20CONCLR =  I2C_STOP | I2C_SI | I2C_START;	// clear SI and STOP and START bit
			break;
		case 0x38:
			I20CONCLR = I2C_STOP | I2C_SI;
			I20CONSET = I2C_START;
			break;
		case 0x28:
			if(!lsb_sent)
			{
				I20DAT = (u8)value;
				lsb_sent=TRUE;
				I20CONCLR =  I2C_STOP | I2C_SI | I2C_START;	// clear SI and STOP and START bit
			}
			else
			{
				I20CONSET = I2C_STOP;
				I20CONCLR = I2C_SI | I2C_START;
				return TRUE;
			}
		case 0x20:
		case 0x30:
	 	default:
			I20CONCLR = I2C_STOP | I2C_SI | I2C_START;
			return FALSE; 
		}
	}
	 
	return FALSE;
}



/* Setup the DAC/Timing Interrupt */
void init_timing (void) 
{
	clear_buffers();

  // configure SPI1 for SPI CPOL=1 CPHA=0 16-bit format, maximum frequency
  SSPCR0 = 64|15;
  // maximum frequency 15 MHz (PCLK=60MHz/4)
  SSPCPSR = 1;	
  // configure master mode and enable
  SSPCR1 = 2;

  // DAC I2C initialisation

  // Set SCLK to 375kHz
  I20SCLH = 20;
  I20SCLL = 20;

#ifndef INTERNAL_DAC

  write_dac(0, 0x80);	// left not muted, simultaneous update
  write_dac(1, 0x80);	// right not muted, simultaneous update
  write_dac(2, 0x80 | 0x20 | 0x10); // left headphone output muted
  write_dac(3, 0x80 | 0x20 | 0x10); // right headphone output muted
  write_dac(4, 0x10);   // dac on
  write_dac(5, 8|4) ;   // soft mute, de-emphasis 44.1kHz
  write_dac(6, 4|2|1);      // power on, clk, osc, output, dac
  write_dac(7, 1);      // left justified, slave mode, 16-bit data
  write_dac(8, 0x20 | 2 | 1);      // usb mode, filter type=1, sample rate=44.118 khz

#endif
 
  // Enable internal DAC output  	
  PINSEL1 = (PINSEL1 | (1L<<19)) & ~(1L<<18);		
 
  // Enable SSP pins SCLK (P0.17), MISO (P0.18), MOSI (P0.19)
  PINSEL1 = (PINSEL1 | (1L<<3) | (1L<<5) | (1L<<7)) & ~((1L<<2) | (1L<<4) | (1L<<6));

  // Make SSEL (P0.20) a GPIO output for LRCLK
  IODIR0 |= LRCLK_PIN;
  IOCLR0 =LRCLK_PIN;

	// set interrupt vector in 0
	VICVectAddr0 = (unsigned long)tc0;          

  buffer_active=0;
  p=buffer0;
#ifdef INTERNAL_DAC
  cnt=BUFSIZE>>1;
#else
  cnt=BUFSIZE;
#endif

  buffer_done=1;

  T0MCR = 3;                                  // Interrupt and Reset on MR0

  // default settings
  set_dac_rate(44100);
	
  VICVectCntl0 = 0x20 | 4;                    // use it for Timer 0 Interrupt

  VICIntEnable |= 0x00000010;                  // Enable Timer0 Interrupts
 
}
  
// 
// software timing loop for non-irq based timing during initialisation
//
void delay_100ms(void)
{
	u32 i;
	
	for(i=0;i<2000000L;i++) ;
}

