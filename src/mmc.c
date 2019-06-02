/*
**  PHILIPS ARM 2005 DESIGN CONTEST
**  ENTRY AR1757
**  FLASH CARD AUDIO PLAYER FOR HEAD END UNIT
**
**  Multimedia card interface routines
*/

#include <LPC213x.H>                            /* LPC21xx definitions */ 
#include <stdio.h>
#include "mmc.h"
#include "types.h"
#include "timing.h"
	 
#ifndef SIMULATION

static u16 ReadTimeoutBytes;

static inline void spi_HOLD(void)
{
	IOCLR0 = 8;
}

static inline void spi_RELEASE(void)
{
	IOSET0 = 8;
}

static inline void spi_WRITE(u8 *buf,u16 len)
{
	u16 t;

	while(len--)
	{
		S0SPDR = *buf++;
		while(!(S0SPSR & 128)) ; 
		t=S0SPDR;
	}
}

static inline void spi_READ(u8 *buf,u16 len)
{
	while(len--)
	{
		S0SPDR = 0xff;
		while(!(S0SPSR & 128)) ; 
		*buf++=S0SPDR;
	}
}

static inline u16    spi_GetR1Response(void)
{
    u16   x;
	u8 response;

    // Wait upto 8 bytes for a R1 (8-bit) response

    for (x=0; x<8; x++)
    {
		spi_READ(&response,1);

		if(response != 0xFF)
			break;

		
		

    }
    return  response;
}

static u8 frame[6];


static  inline u8    spi_CMD(u8 cmd,u32 data)
{

	data <<= 9;

	frame[1]=(u8)(data >> 24);
	frame[2]=(u8)(data >> 16);
	frame[3]=(u8)(data >> 8);
	frame[4]=(u8)data;

	frame[0]=cmd;
	frame[5]=0xff;

    spi_WRITE(frame,6);

    // Wait for response token
    return  spi_GetR1Response();
}


#define INIT_TIMEOUT 5000

// Initialise the MMC controller. Return FALSE if not found
bool mmc_Initialise(void)
{
	static const u32 time_exponent_lut[8] = { 1000000000L, 100000000L, 10000000L, 1000000L, 100000L, 10000, 1000, 100 };
	static const u8 time_mantissa_lut[16] = { 0, 10, 12, 13, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 70, 80 };
 	u8 response,x,Nsac;
	u16 timeout;
	u32 Naac;
  	u8 csd[16];

   	
	// Select SPI pin functions and P0.3 as the CS
  	PINSEL0 |= 0x5500; 
	PINSEL0 &= ~(0xAAC0);

	// Select P0.3 as an output and deassert it (/CS)
	IODIR0 |= 8;
	spi_RELEASE();

	// Select baud rate 125 kbps
	S0SPCCR = 6; // 120; 

	// Master, CPHA=1, CPOL=1, no IRQ,  MSB first
	S0SPCR = 8+16+32;

	// clock dummy bytes to initialise shift register on MMC card
	x=0xff;
	spi_WRITE(&x,1);
 	spi_WRITE(&x,1);
 
	delay_100ms();

	spi_HOLD();	

	
	frame[0]=0x40;
	frame[1]=frame[2]=frame[3]=frame[4]=0;
	frame[5]=0x95;

	spi_WRITE(frame,6);
	response=spi_GetR1Response();
	spi_READ(&x,1);
	spi_RELEASE();
	
	//
    // Now send CMD1 (SEND_OP_COND) command
    //
	// Continue polling the device with this command until it clears the idle bit or we time out
	//
	timeout = 0;

    while ((response&1) && (timeout < INIT_TIMEOUT))
    {
		spi_HOLD();
   		response = spi_CMD(0x41,0);
        spi_READ(&x,1);		// send 8 clocks after response sequence
		spi_RELEASE();

        if(response&1)
        {
			delay_100ms();
			timeout+=100;
		}

    }


    if((response &1 ) || timeout >= INIT_TIMEOUT)
	{
	    return FALSE;
	}


	//
	// Read the CSD structure
	//
	spi_HOLD();
    response = spi_CMD(0x49,0);
    if (!response)
    {
		// Command valid ... wait for READ Token
        for (x=0; x<9; x++)
        {
            spi_READ(&response,1);
            if (response!=0xff)
                break;
        }

    }


    if (response==0xFE)
    {
		spi_READ(csd,16);

        // Get 2 more to clear CRC from MMC
        spi_READ(&response,1);
		spi_READ(&response,1);
	
		// Extra read to keep MMC happy
		spi_READ(&response,1);

        spi_RELEASE();

		// fetch raw Taac

		x = csd[1] & 127;

		// work out Naac based on clock frequency and SPI frequency

		Naac = ( time_mantissa_lut[(x >> 3) & 15] * 125000 )  / time_exponent_lut[x & 7] ;

		Nsac = csd[2] ;

		ReadTimeoutBytes = 1+(u16)((Naac + Nsac*1000)/8);

		puts("MMC OK\n");

        return  TRUE;
    }

	spi_RELEASE();


	return FALSE;
}

// Read a sector into the buffer
bool mmc_SectorRead(u8 *sector,u32 lba)
{
	u8 response;
	register u8 *buf;
	int x;

	spi_HOLD();
	response = spi_CMD(0x51,lba);

    if (!response)
    {   // Command valid ... wait for READ Token
        for (x=0; x<ReadTimeoutBytes; x++)
        {
            spi_READ(&response,1);
            if (response==0xFE)
                break;
        }
    }
    if (response==0xFE)
    {
		// Get the 512 bytes into our buffer!
		buf=sector;

		for(x=0;x<512;x++)
		{
			S0SPDR = 0xff;
			while(!(S0SPSR & 128)) ; 
			*buf++=S0SPDR;
		}
        
        // Get 2 more to clear CRC from MMC
        spi_READ(&response,1);
        spi_READ(&response,1);

		// 8 clocks after read to keep MMC happy
        spi_READ(&response,1);

        spi_RELEASE();

        return  TRUE;
    }
  
  	spi_RELEASE();
		
	return FALSE;
}

#endif

