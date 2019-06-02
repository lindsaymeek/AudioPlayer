
#ifndef _MMC_H
#define _MMC_H

/*
**  PHILIPS ARM 2005 DESIGN CONTEST
**  ENTRY AR1757
**  FLASH CARD AUDIO PLAYER FOR HEAD END UNIT
**
**  Multimedia card interface routines
*/

#include "types.h"

bool mmc_Initialise(void);

bool mmc_SectorRead(u8 *sector,u32 lba);

#endif

