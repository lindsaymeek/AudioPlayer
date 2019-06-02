/*
**  PHILIPS ARM 2005 DESIGN CONTEST
**  ENTRY AR1757
**  FLASH CARD AUDIO PLAYER FOR HEAD END UNIT
**
**  SERIAL.C:  Low Level Unbuffered Serial Routines                                      
*/                                                                            

#include <LPC213x.H>                     /* LPC21xx definitions               */
#include <stdarg.h>
#include "serial.h"

// Define this to use UART1 else UART0

//#define PROTOTYPE1

#define CR     0x0D

void init_serial(void)
{
 /* initialize the serial interface   */
#ifdef PROTOTYPE1
  PINSEL0 |= 0x00050000;  		  /* Enable RxD1 and TxD1                     */
  U1LCR = 0x83;                   /* 8 bits, no Parity, 1 Stop bit            */
  U1DLL = 97;                     /* 9600 Baud Rate @ 15MHz VPB Clock         */
  U1LCR = 0x03;                   /* DLAB = 0                                 */
#else
  PINSEL0 |= 0x00000005;  		  /* Enable RxD0 and TxD0                     */
  U0LCR = 0x83;                   /* 8 bits, no Parity, 1 Stop bit            */
  U0DLL = 97;                     /* 9600 Baud Rate @ 15MHz VPB Clock         */
  U0LCR = 0x03;                   /* DLAB = 0                                 */
#endif
}

int putchar (int ch)  
{                  /* Write character to Serial Port    */
#ifdef PROTOTYPE1
  if (ch == '\n')  {
    while (!(U1LSR & 0x20));
    U1THR = CR;                          /* output CR */
  }
  while (!(U1LSR & 0x20));
  return (U1THR = ch);
#else
  if (ch == '\n')  {
    while (!(U0LSR & 0x20));
    U0THR = CR;                          /* output CR */
  }
  while (!(U0LSR & 0x20));
  return (U0THR = ch);
#endif
}

int  puts     (const char *s)
{
  while(*s) putchar(*s++);
  return 0;
}

int kbhit(void) 
{
#ifdef PROTOTYPE1 
  if(!(U1LSR & 0x01))
#else
  if(!(U0LSR & 0x01))
#endif
  	return 0;
  else 
  	return 1;

}

int getchar (void)  
{                    /* Read character from Serial Port   */
#ifdef PROTOTYPE1
  return (U1RBR);
#else
  return (U0RBR);
#endif
}

char hex[16]="0123456789ABCDEF";

//
// hex number printer used during debugging
//
char *itoa(int n,int bits)
{
	static char str[32];

	int i,j;

	for(i=bits-4,j=0;i>=0;i-=4,j++)
	{
		str[j]=hex[(n >> i)&15];
	}

	str[j]=0;

	return str;	
}
