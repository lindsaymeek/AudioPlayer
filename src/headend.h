
#ifndef HEADEND_H
#define HEADEND_H

/* 
**  PHILIPS ARM 2005 DESIGN CONTEST
**  ENTRY AR1757
**  FLASH CARD AUDIO PLAYER FOR HEAD END UNIT
**                                                                          
**  HEADEND.H:  Head end unit protocol interface                                
*/                                                                           

/* Setup the interface */
void init_headend (void);

/* Poll the interface for commands */
int poll_headend (void);
 
#endif
