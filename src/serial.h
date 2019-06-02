
#ifndef SERIAL_H
#define SERIAL_H

void init_serial(void) ;

int putchar (int ch) ;                  /* Write character to Serial Port    */

int getchar (void) ;                   /* Read character from Serial Port   */

int kbhit(void);

char *itoa(int n,int bits);

#endif
