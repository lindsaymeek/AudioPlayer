/***********************************************************************/
/*                                                                     */
/*  SYSCALLS.C:  System Calls Remapping                                */
/*                                                                     */
/***********************************************************************/

#include <stdlib.h>


extern int putchar (int ch);

void _exit (int n) {
label:  goto label; /* endless loop */
}


int write (int file, char * ptr, int len) {
  int i;

  for (i = 0; i < len; i++) putchar (*ptr++);
  return len;
}

int isatty (int fd) {
  return 1;
}

#define HEAP_LIMIT 0x40008000

caddr_t sbrk (int incr) {
  extern char   end asm ("end");	/* Defined by the linker */
  static char * heap_end;
         char * prev_heap_end;

  if (heap_end == NULL) heap_end = &end;
  prev_heap_end = heap_end;
  
  if (heap_end + incr >= (char *)HEAP_LIMIT) {
    abort ();	   /* Out of Memory */ 
  }  
  heap_end += incr;

  return (caddr_t) prev_heap_end;
}
