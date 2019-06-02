/* 
**  PHILIPS ARM 2005 DESIGN CONTEST
**  ENTRY AR1757
**  FLASH CARD AUDIO PLAYER FOR HEAD END UNIT
**                                                                          
**  HEADEND.C:  Head end unit protocol interface (Unilink)                              
*/                                                                           


#include <LPC213X.H>                          // LPC21XX Peripheral Registers
#include "headend.h"
#include "types.h"
#include "serial.h"
#include "timing.h"
#include "control.h"
#include <string.h>
#include <stdio.h>

// Define this for some debugging info
#define DBG_HEAD

#define UNICLK_MASK (1<<10)
#define UNIDAT_MASK (1<<11)
#define UNIBUS_MASK (1<<16)
#define UNIRST_MASK (1<<15)
 	
static bool active=FALSE; // device has been selected by headend as playback device

#define STATE_IDLE 0x80
#define STATE_CHANGING 0x40
#define STATE_CHANGED 0x20
#define STATE_PLAY 0x00

static int report_state=STATE_IDLE; // IDLE

static int state=0;		  	// protocol reciever state machine 
static int dpystate=0; 		// head end display update state
static int breakstate=0;    // slave break generator state machine	
static u8 myid,mybit;
static u8 sum,rad,tad,cmd1,cmd2;
static u8 data11,data12,data13,data14,data21,data22,data23,data24,data25;

volatile static void (*cmd)(void)=NULL;
 
#define TICKS2_PER_MS 15000

// Main ISR entry point
static void isr_entry (void) __attribute__ ((interrupt));

// Dynamically controlled handler
static void bus_gap (void) ;
static void bus_rx (void) ;
static void bus_tx (void) ;
static void (*isr_handler)(void);

static u8 outbyte;
static u8 rd,wr,buffer[256];

static int Interpret(void);

#ifdef DBG_HEAD

static char dbg_buf[256];
static u8 dbg_rd=0;
static volatile u8 dbg_wr=0;

// background char fetch
static char getds(void)
{
	if(dbg_rd != dbg_wr)
		return dbg_buf[dbg_rd++];
	else
		return 0;
}

#define putd(c) dbg_buf[dbg_wr++]=(c)

static void putds(char *s)
{
	while(*s)
		putd(*s++);
}

#endif

static long LASTCR0=0;
static int gap_cnt=0;

// Nested interrupt handler. Derived from Philips App Note 10381
static void isr_entry(void)
{

 	// save SPSR
	asm("mrs r0,spsr");
	asm("stmfd sp!,{r0}");

	// ack interrupt sources to prevent infinite nesting
	T1IR = 16+1;
	// disable edge capture to stop rapid interrupts during rx/tx
	T1CCR = 0;

	// enable non-FIQ interrupts, switch to supervisor mode (user stack)
   	asm("msr cpsr_c,#0x5f");

	// save C working registers
	asm("stmfd sp!,{r0,r1,r2,r3,ip,lr}");

	// dispatch to handler
	isr_handler();

	// restore C working registers
	asm("ldmfd sp!,{r0,r1,r2,r3,ip,lr}");

	// disable all interrupts, back to IRQ mode
	asm("msr cpsr_c,#0xd2");
  
  	// restore SPSR
	asm("ldmfd sp!,{r0}");
	asm("msr spsr_cf,r0");
	
	// update VIC		 
	VICVectAddr=0;

}

// initialise the gap detection interrupt

static void setup_gap(void)
{
	gap_cnt=0;
	LASTCR0=T1CR0;
	T1TC=0;
	T1MR0 = TICKS2_PER_MS/2;

  	// Enable periodic interrupt
	T1MCR = 3;
	// Disable edge capture interrupt but enable both edge detection
	T1CCR = 3;

//	VICVectAddr1 = (unsigned long)bus_gap;  
	isr_handler = bus_gap;

}

/* 4ms clock gap detection interrupt and slave break generator */
/* This runs every 500us */

static void bus_gap(void)
{
	

	if(!breakstate)
	{
	 // did edge capture occur? 
	 if(T1CR0 != LASTCR0)
	 {
		LASTCR0 = T1CR0;
		gap_cnt=0;
	 }
	 else
		gap_cnt++;

	 if(gap_cnt >= 8)
	 {
		// 4ms gap found

		// Disable periodic interrupt
		T1MCR = 2;
		// Enable edge capture for receive	
		T1CCR = 1 | 4;
	  	// Reset receiver state machine
	    state=0;
		// Set up receiver interrupt 
		isr_handler=bus_rx;
#ifdef DBG_HEAD
		putds("GAP\n\r");
#endif
	}
   }
   else
   {
   		switch(breakstate) {
			default:
				breakstate=0;
				gap_cnt=0;
				break;
			case 1:
		   		// wait for data low > 6ms
   				if(IOPIN0 & UNIDAT_MASK)
					gap_cnt=0;
				else
					gap_cnt++;

				if(gap_cnt >= 12)
				{
					breakstate++;
					gap_cnt=0;
				}
				break;
			case 2:
				// wait for 4ms
				if(++gap_cnt >= 8)
				{
					breakstate++;
					gap_cnt=0;
					// drive data line low
					IODIR0 |= UNIDAT_MASK;
					IOCLR0 = UNIDAT_MASK;
				}
				break;
			case 3:
				// force data line low for 4ms
				if(++gap_cnt >= 8)
				{
					// disable slave break generator 
					breakstate=0;
					// release line
					IODIR0 &= ~UNIDAT_MASK;
					// release gap detector
					LASTCR0 = T1CR0;
#ifdef DBG_HEAD
	putds("Break Done\n\r");
#endif

				}
				break;

		}




   }

}
 
/* Clock edge capture receive interrupt */

static void bus_rx (void)
{
	static u8 i,readbyte;
	
	// Disable further capture interrupts
	T1CCR = 0;
 
 	// Disable capture 0 input and allow CPU to read pin state 	
  	PINSEL0 &= ~(1L<<21);
   
    readbyte=0;

	for(i=0;i<8;i++)
	{
		readbyte <<= 1;

		while(!(IOPIN0 & UNICLK_MASK)) ; // wait for clock high

		if(!(IOPIN0 & UNIDAT_MASK))		 // latch inverted data
			readbyte |= 1;

		while(IOPIN0 & UNICLK_MASK) ;    // wait for clock low
	}
	
	// re-enable capture pin on clock
	PINSEL0 |= (1L<<21);

	T1CCR = 1 | 4;

    // processing deadline is 0.9ms according to cleggy's document
   
	switch(state) 
	{
		default:
		case 0:
			
		 		rad=readbyte;
				if(!rad)
					setup_gap();
				else
				{
					sum=rad;
					state=3;
				}
	
				break;

		case 3:

					tad=readbyte;
					sum+=tad;
					state++;
					break;

		case 4:
					cmd1=readbyte;
					sum+=cmd1;
					state++;
					break;

		case 5:
					cmd2=readbyte;
					sum+=cmd2;
					state++;
					break;

		case 6:				
					// checksum
					if(readbyte != (sum&255))
					{
#ifdef DBG_HEAD
						putds("SUM1?\n\r");
#endif
						setup_gap();
					}
					else
					{
						if(cmd1 & 128)
							state++;
						else
						{
							if(cmd1 & 64)
								state=11;
							else
								state=18;
						}

					}
					break;

			// medium
			case 7:
		
					data11 = readbyte;
					sum+=data11;
					state++;
					break;

			case 8:
					data12 = readbyte;
					sum+=data12;
					state++;
					break;

			case 9:
					data13 = readbyte;
					sum+=data13;
					state++;
					break;

			case 10:
					data14 = readbyte;
					sum+=data14;
				 
					if(cmd1 & 64)
						state++;
					else
						state+=18-10;

					break;

			// long
			case 11:

 					data21 = readbyte;
					sum+=data21;
					state++;
					break;

			case 13:
					data22 = readbyte;
					sum+=data22;
					state++;
					break;

			case 14:
					data23 = readbyte;
					sum+=data23;
					state++;
					break;

			case 15:
					data24 = readbyte;
					sum+=data24;
					state++;
					break;

			case 16:
					data25 = readbyte;
					sum+=data25;
					state++;
				    break;

			case 17:		 
			 		// checksum
					if(readbyte != (sum&255))
					{
#ifdef DBG_HEAD
						putds("SUM2?\n\r");
#endif
						setup_gap();
					}
					else
					    state++;
					break;

			case 18:
					
				 if(!readbyte)
				 {
	  				// clear transmit buffer
					rd=wr=0;

					// process command and form response if any
				 	Interpret();

				 }
#ifdef DBG_HEAD
				 else
				 {

				 	putds("PKT?\n\r");
				 }
#endif				
				// go back to gap detector if nothing to transmit
				if(rd==wr)
					setup_gap();

				break;
				
				 
	}
	
   
} 
   
  
static void bus_tx (void)
{
	u8 i;
	
	IODIR0 |= UNIDAT_MASK;

	T1CCR = 0;
	// Disable capture signal diversion
	PINSEL0 &= ~(1L<<21);

	for(i=0;i<8;i++)
	{
		IOCLR0 = UNIDAT_MASK;
		
		while(!(IOPIN0 & UNICLK_MASK)) ;
		
		if(!(outbyte & 128))
			IOSET0 = UNIDAT_MASK;
			
		while(IOPIN0 & UNICLK_MASK);
		
		outbyte <<= 1;
	}
	
	// release data line

	IODIR0 &= ~UNIDAT_MASK;
	// Enable capture signal diversion
   	PINSEL0 |= (1L<<21);
    T1CCR = 1|4;	

	// wait for transmission to complete before restarting
	if(rd == wr)
		setup_gap();
	else
		outbyte = buffer[rd++];
	
} 
 

/* Setup the interface */
void init_headend (void) 
{
	myid=0; 		// no assigned ID yet
	active=FALSE;	// playback device not selected
 	report_state=STATE_IDLE;
	cmd=NULL;

	#ifdef DBG_HEAD
		dbg_rd=dbg_wr=0;
	#endif

  // Disable Timer1 & Reset

  T1TCR = 2;
    	  
  // Enable capture 0 input  	
  PINSEL0 = (PINSEL0 | (1L<<21)) & ~(1L<<20);		
 
  // Set ISR entry point
  VICVectAddr1 = (unsigned long)isr_entry;  

  // Setup 4ms gap detection on clk line
  setup_gap();
  
  rd=wr=0;
  
  state=0;
  
  // no slave break requested
  breakstate=0;
                       
  VICVectCntl1 = 0x20 | 5;                    // use it for Timer 1 Interrupt
 
  VICIntEnable |= 0x20;               	     // Enable Timer1  

  T1TCR = 1;                                  // Timer1 Enable 

   
}

static inline void SendByte(u8 x)
{

	buffer[wr++]=x;

	sum += x;

}

static void SendStart(u8 val)
{
	sum = val;
  
	outbyte=val;
	
	// change isr over to transmit algorithm     
  	isr_handler = bus_tx;
}

static void SendEnd(void)
{
	SendByte(sum);
	SendByte(0);

}

void IssueSlaveBreak(void)
{
	// activate break state machine
	breakstate=1;

#ifdef DBG_HEAD
	putds("Slave Break\n\r");
#endif	
}

static int hex2bcd(int x)
{
	return (x%10)+((x/10)<<4);
}


static void RespondSlavePoll(void)
{
	u8 save;

	if(rad != myid)
		return;

	if(!playing)
	{
		report_state=STATE_IDLE;
		return;
	}

	switch(dpystate) 
	{
			default:
		 	case 0:

			SendStart(0x77);	// Seeking to CD
			SendByte(myid);
			SendByte(0xC0);
			SendByte(0x40);
			save=sum;SendByte(sum);sum=save;
			SendByte(0);
			SendByte(0);
			SendByte(0);
			SendByte(0);
			SendByte(0);
			SendByte(trackn);
			SendByte(0);
			SendByte(0);
			SendByte((dirn << 4)|1);

			dpystate=1;
			report_state=STATE_CHANGING;

			break;

			case 1:

			SendStart(0x70);	// Seeking to track
			SendByte(myid);
			SendByte(0xC0);
			SendByte(0x20);
			save=sum;SendByte(sum);sum=save;
			SendByte(0);
			SendByte(0);
			SendByte(0);
			SendByte(0);
			SendByte(0);
			SendByte(trackn);
			SendByte(0);
			SendByte(0);
			SendByte((dirn << 4)|0xd);

			dpystate++;
			report_state=STATE_CHANGED;
		
			break;
   
			case 2:

			SendStart(0x70);	// Seeking within track
			SendByte(myid);
			SendByte(0xC0);
			SendByte(0x00);
			save=sum;SendByte(sum);sum=save;
			SendByte(0);
			SendByte(0);
			SendByte(0);
			SendByte(0);
			SendByte(0);
			SendByte(trackn);
			SendByte(0);
			SendByte(0);
			SendByte((dirn << 4)|0xd);

			dpystate++;
			break;

		    case 3:
			
			SendStart(0x70);
			SendByte(myid); // Track status
			SendByte(0x90);
			SendByte(0x00);
			save=sum;SendByte(sum);sum=save;
			SendByte(trackn);
			SendByte(hex2bcd(secs/60));
			SendByte(hex2bcd(secs%60));
			SendByte((dirn<<4)|0xe);

			report_state=STATE_PLAY;
			
			break;

	}

 	SendEnd();

}
	   
static void RespondAnyone2(void)
{
	u8 save;

	SendByte(0x8C);
 	SendByte(0x10);

	save=sum; 
	SendByte(sum); 
	sum=save;

 	SendByte(0xFF);
 	SendByte(0xFF);
 	SendByte(0xFF);
 	SendByte(0xFF);

	SendEnd();
}

static void RespondAnyone(void)
{

	if(myid)
		return;	// don't need another ID
 
	SendStart(0x10);  
	SendByte(0xD0);	// Device class is MD changer

	RespondAnyone2();
 
#ifdef DBG_HEAD
	putds("RespondAnyone\n\r");
#endif

}


static void RespondAppoint(void)
{

	myid  = rad;
	mybit = cmd2;
 
	SendStart(0x10);
    SendByte(myid);

	RespondAnyone2();

#ifdef DBG_HEAD 
	putds("ID\n\r");
#endif
}

static void RespondHello(void)
{
	if(myid)
		return;

 	SendStart(0x10);
	SendByte(0x18);
	SendByte(0x04);
	SendByte(0x00);
  
	SendEnd();

#ifdef DBG_HEAD
	putds("RespondHello\n\r");
#endif

}

static void RespondPing(void)
{

	if(myid != rad)
		return;

	SendStart(0x10);
	SendByte(myid);
	SendByte(0);

	SendByte(report_state);	

	SendEnd();

	IssueSlaveBreak();	// Update master

#ifdef DBG_HEAD
	putds("Ping\n\r");
#endif

}

static void SendBit(u8 mask)
{
	if((mybit ^ mask) & 0xe0)
		SendByte(0);
	else
	{
		if(mybit & 0x10)
			SendByte(mybit & 15);
		else
			SendByte((mybit >> 4)& 240);
	}
}

static void RespondMasterPoll(void)
{
	u8 save;

	if(!active)
		return;

	SendStart(0x10);
	SendByte(0x18);

	SendByte(0x82);

	SendBit(0);

	save=sum; SendByte(sum); sum=save;

	SendBit(0x20);
	SendBit(0x40);
	SendBit(0x60);
	SendBit(0x80);

	SendEnd();

#ifdef DBG_HEAD
	putds("RespondMasterPoll\n\r");
#endif
}

//
// interpret incoming head end command and send response packet (if applicable)
//
static int Interpret(void)
{

	switch(cmd1)
	{
		default:

#ifdef DBG_HEAD
	putds("CMD1?\n\r");
#endif	
			break;
		case 1:

			switch(cmd2) 
			{
				default:

#ifdef DBG_HEAD
	putds("Cmd01?\n\r");
#endif	
					break;
				case 2: 
					RespondAnyone();
					break;
				case 0x11:
					RespondHello();
					break;
				case 0x12:
					RespondPing();
					break;
				case 0x13:
					RespondSlavePoll();
					break;
				case 0x15:
					RespondMasterPoll();
					break;
			}
			
			break;
		case 2: 
			RespondAppoint();
			break;
		case 0x20:
			if(rad == myid)
			{
	  		    active=TRUE;
#ifdef DBG_HEAD
	putds("Play\n\r");
#endif
				cmd=play;
				dpystate=0; // restart display sequence
				report_state=STATE_IDLE;
		 		IssueSlaveBreak();
			}
			break;
	
		case 0x26: 
#ifdef DBG_HEAD
	putds("Next\n\r");
#endif
			cmd=next_track;
			dpystate=1;
			return 1;

		case 0x27:
#ifdef DBG_HEAD
	putds("Prev\n\r");
#endif
			cmd=prev_track;
			dpystate=1;
			return 1;

		case 0x34:
#ifdef DBG_HEAD
	putds("Repeat\n\r");
#endif
			cmd=toggle_repeat;
			break;

		case 0x35:
#ifdef DBG_HEAD
	putds("Shuffle\n\r");
#endif
			cmd=toggle_shuffle;
			break;

		case 0x28:
#ifdef DBG_HEAD
	putds("Next Dir\n\r");
#endif
			cmd=next_dir;
			dpystate=1;
			return 1;

		case 0x29:
#ifdef DBG_HEAD
	putds("Prev Dir\n\r");
#endif
			cmd=prev_dir;
			dpystate=1;
			return 1;

		case 0x87:
			if(cmd2 == 0x6b)
			{
#ifdef DBG_HEAD
	putds("Stop\n\r");
#endif
				cmd=stop;
				active=FALSE;
				report_state=STATE_IDLE;
				return 1;
			}
			break;

		case 0xF0:
			active=FALSE;
#ifdef DBG_HEAD
	putds("Stop\n\r");
#endif
			cmd=stop;
			report_state=STATE_IDLE;
			return 1;

	}
	return 0;
}

//
// polling function for receiving headend commands
//
int poll_headend(void)
{
#ifdef DBG_HEAD
    char c=getds();

	if(c)
		putchar(c);
#endif

	if(cmd != NULL)
	{
		cmd();
		cmd=NULL;
		return 1;
	}
	else
   		return 0;

}
