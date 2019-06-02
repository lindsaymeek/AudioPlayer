/* 
**  PHILIPS ARM 2005 DESIGN CONTEST
**  ENTRY AR1757
**  FLASH CARD AUDIO PLAYER FOR HEAD END UNIT
**                                                                          
**  FFS.C:  Stripped down and optimised FAT16 file system routines                                
** 
// Based on the software from:
//
// Apple II 16-bit IDE/ATA interface software
// (c) 2001 stephane.guillard@steria.com
// Hardware and software design available at
// http:// s.guillard.free.fr
*/

//
// Conditional compilation directives
//
//
//#define CCD_DEBUG				// Enable verbose behaviour
//#undef        CCD_DEBUG                               
//#define CCD_STRICTCHECK			// Enforce full checkings of IDE ide_status etc.

//
//
// ****************** I N C L U D E S ******************
//
//

#ifndef SIMULATION

#ifdef CCD_DEBUG
#define DEBUG
#endif

#include "FFs.h"
#include "mmc.h"
#include "types.h"

#include <LPC213x.H>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
	 
static u8 sector[512];

//
// big-little endian swappers
//
inline u16 swap16(u16 x)
{
	u8 *p=(u8 *)&x;
	u8 t;

	t=p[0];
	p[0]=p[1];
	p[1]=t;

	return x;

}

inline u32 swap32(u32 x)
{
	u8 *p=(u8 *)&x;
	u8 t;

	t=p[0];
	p[0]=p[3];
	p[3]=t;
	t=p[1];
	p[1]=p[2];
	p[2]=t;

	return x;
}

//
//
// ****************** C O N S T A N T S ******************
//
//
// General


#define FILE_USED	FALSE
#define FILE_FREE	TRUE

// IDE block size
#define BLOCKSIZE	512

// Max count of file handles
#define MAX_FILES	2

// End of cluster chain
#define CLUSTCHAIN_END		((u16) 0xFFF8)

//
//
// ****************** M A C R O S ******************
//
//

#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))
//
//
// ****************** G L O B A L S ******************
//
//

// Drive geometry
static u32 hd1_start_lba;		// first lba of partiton
static u8	hd1_geom_secperclust;	// sectors per cluster
static u16 hd1_geom_rd_size;		// root directory size in sectors
static u32 hd1_geom_offfat;		// 1st sector after 1st FAT
static u32	hd1_geom_clustsize;	// u8s per cluster
	   

// Main FS struct properties
static u32
	lba_fat,		// FAT 1st sector
	lba_rd,			// Root directory 1st sector
	lba_data,		// Partition data area 1st sector
	lba_curdir,		// Current directory 1st sector (initialized by dir_examine())
	lba_tmpdir;		// Current sector in current directory (used by dir_exnext())

static dirent *pde_cur, de_cur; // de_cur is a copy of the current dirent. pde_cur points at the original in secbuf


// Global file handle table
static file_handle __files[MAX_FILES];

// Sector cache
static u32 lba_incache = -1; // valid LBA starts at 1 so 0 is invalid cache

//
//
// ****************** E X P O R T E D   P R O T O S ******************
//
//


//
//
// ****************** M I S C   U T I L I T Y   F U N C S ******************
//
//

static u16  lba2clust(u32 lba)	// lba2clust() : find cluster number from LBA sector address
{
	if (lba < lba_data) return 0;	// 0 is error code since clust < 2 is error

	return 2 + (u16) ((lba - lba_data) / hd1_geom_secperclust);
}

static u32  clust2lba(u16 clust)	// clust2lba() : find LBA  address of cluster's 1st sector
{
	if (clust < 2) return 0;	// 0 is error code since LBA < 1 is error

	return lba_data + (u32) (clust - 2) * (u32) hd1_geom_secperclust;
}


//
//
// ****************** LV1 - S E C T O R   I N T E R F A C E   F U N C S ******************
//
//

#define secbuf(index) sector[(index)]

u16 inline  peekw(u16 index)
{
	u16 t;

	t=sector[index+1];
	t<<=8;
	t|=sector[index];

	return t;
}

inline u32  peekl(u16 index)
{
	u8 *p;
	u32 t;

	p=sector+index;

	t=p[3];
	t<<=8;
	t|=p[2];
	t<<=8;
	t|=p[1];
	t<<=8;
	t|=p[0];

	return t;
}


// sec_read() : read a sector pointed by lba into secbuf
static void  sec_read(u32 lba)
{
	lba_incache = lba;

#ifdef CCD_DEBUG
	mprintf("Read sector: %u\n\r",lba);
#endif

	mmc_SectorRead(sector,lba);	   
}

#ifdef CCD_DEBUG
static void  sec_dump(void)	// Dump sector buffer on screen as HEX
{
	u16 x;

	for(x=0;x<512;x+=16)
	{
		mprintf("%04x: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n\r",x,
					secbuf(x),secbuf(x+1),secbuf(x+2),secbuf(x+3),secbuf(x+4),secbuf(x+5),secbuf(x+6), secbuf(x+7),
					secbuf(x+8),secbuf(x+9),secbuf(x+10),secbuf(x+11),secbuf(x+12),secbuf(x+13),secbuf(x+14), secbuf(x+15));




	}
}

static void  dbg_dumpprm(void)
{
	mprintf("Start LBA %u\n\r",hd1_start_lba);
}
#endif

static void  sec_get(u32 *sa)	// Read sector into sector buffer. CHS is recalculated from LBA.
{

	// Simple cache mechanism : don't read an already present sector into buffer
	if (lba_incache == *sa)
	{

		return;
	}

	// 2 - Read sector
	sec_read(*sa);
}

//
//
// ****************** LV2 - H A R D   D I S K   I N T E R F A C E   F U N C S ******************
//
//
bool  hd_qry(void)			// hd_qry() : inquiry disk properties and display them
{
	u8 rc;

#ifdef CCD_DEBUG
	mprintf("Query disk info\n\r");
#endif


	rc=mmc_Initialise(); 

	if(rc==TRUE)
	{
		return TRUE;
	}
	else
	{
#ifdef CCD_DEBUG

	mprintf("MMC not responding?\n\r");

#endif
		return FALSE;
	}

}

bool  hd_mbr(void)			// hd_mbr() : read disk Master Boot Record, check it and analyze partition entries
{
#ifdef CCD_DEBUG
	u8 pnum;
	u16 p_offset;
#else
#define p_offset	446
#endif

	// Analyze MBR and find 1st partition boot sector (leave CHS of this BPB in IDE parameters for further read)
#ifdef CCD_DEBUG
	mprintf("Chk MBR\n\r");
#endif

	sec_read(0);	// Read MBR sector


	// Check signature MBR : offset 1FE=0x55 1FF=AA
	if ((secbuf(0x1FE) != 0x55) || (secbuf(0x1FF) != 0xAA))
	{
#ifdef CCD_DEBUG
		mprintf("No\n\r");
#endif
		return FALSE;
	}

#ifdef CCD_DEBUG

	for (pnum = 0 ; pnum < 4 ; ++pnum)	// Scan the 4 partition entries of MBR (starts at secbuf + 446)
	{
		p_offset = 446 + 16 * pnum;	// Offset of partition #pnum struct in MBR

		// Display current part #, and skip if not defined (type == 0)


		mprintf("Partition #%d ",pnum);

#endif

		if (secbuf(p_offset + 4))	// If partition defined (type != 0)..
		{

#ifdef CCD_DEBUG
			// Display type
			mprintf("type %d ", secbuf(p_offset + 4));

#endif
			// Extract start LBA
			hd1_start_lba = peekl(p_offset + 8);

#ifdef CCD_DEBUG
			// Display active Y/N
			mprintf("Active: %s",secbuf(p_offset) == 0x80 ? "Yes " : "No ");

			// Output start  CHS (note that start CHS is the boot sector of the partition)

			mprintf("LBA %u\n\r",hd1_start_lba);
#endif
		}

#ifdef CCD_DEBUG
		else
			mprintf("None\n\r");

	}

	p_offset = 446;

	// extract start LBA
	hd1_start_lba = peekl(p_offset + 8);

#endif

	return (!(secbuf(446 /*p_offset*/ + 4) != 6 && secbuf(446 /*p_offset*/ + 4) != 4));	// Only return 0 if 1st partition type is FAT16
}

bool  hd_bpb(void)	// Analyze partition Boot Param Block to find out addresses of FAT and rootdir
{
	u8 i;


	sec_read(hd1_start_lba );

	hd1_geom_rd_size = peekw(17) / 16;/* * 32 / BLOCKSIZE */ 		// Get root dir size
	hd1_geom_secperclust = secbuf(13);								// Get sector per cluster count

	// Check if BPB looks OK
	if (
#ifdef CCD_STRICTCHECK
			(secbuf(  3) != 'M')	// standard FAT BPB signature == "MSWIN4.1"
		||	(secbuf(  4) != 'S')
		||	(secbuf(  9) != '.') 
		|| 	(peekw( 11) != BLOCKSIZE)		// MUST have BLOCKSIZE u8 sectors
#else
		    (secbuf(510) != 0x55)
		||  (secbuf(511) != 0xaa)
		||	(!(peekw( 14)))	// Reserved MUST be >0
		||	(secbuf( 21) != 0xF8)	// Media MUST be 0xF8
		||  (peekw( 11) != BLOCKSIZE)		// MUST have BLOCKSIZE u8 sectors*/
#endif
		
		)
	{
#ifdef CCD_DEBUG
		mprintf("Boot block not found\n\r");
#endif
		return FALSE;
	}

	// Calculate LBA of FAT : reserved + hidden
	// LBA of BPB is calculated from current values of IDE parameters (BPB is the last sector read in secbuf)
	lba_fat = (u32) peekw( 14)
		  + peekl( 28);

	// Remember LBA of 1st sector after 1st FAT
	hd1_geom_offfat = lba_fat + (u32) peekw( 22);

	// Calculate LBA of root dir : FAT + numFAT * fatsize
	// Store root dir into current dir
	lba_curdir = lba_rd = lba_fat + (u32) secbuf(16) * (u32) peekw( 22) ;

	// Calculate LBA of data area : after root dir
	lba_data = lba_rd + (u32) hd1_geom_rd_size;

	// Calculate cluster size in u8s (to speedup later calculation)
	hd1_geom_clustsize = (u32) BLOCKSIZE * (u32) hd1_geom_secperclust;

#ifdef CCD_DEBUG
	mprintf("Fat   LBA =%u\n\r",lba_fat);
	mprintf("Root  LBA =%u\n\r",lba_rd);
	mprintf("Data  LBA =%u\n\r",lba_data);
	mprintf("LFat  LBA =%u\n\r",hd1_geom_offfat);
	mprintf("Clustsize =%u\n\r",hd1_geom_clustsize);
#endif

	// clear out file handles
	for(i=0;i<MAX_FILES;i++)
		__files[i].inuse=0;

	return TRUE;
}

//
//
// ****************** LV3 - C L U S T E R   I N T E R F A C E   F U N C S ******************
//
//
static bool  clust_getfat(u16 clust)	// Read into secbuf the FAT sector containing clust.
								// Return TRUE if Ok, FALSE if error
{
	u32 sa;

	// Find out FAT sector number containing cluster#
	// If we are out of 1st FAT then clust is invalid
	if ((sa = lba_fat + ((u32) (clust >> 8) /* * (u32) 2 / (u32) BLOCKSIZE*/)) // a cluster # is 2 u8s, a sector is BLOCKSIZE u8s
		>= hd1_geom_offfat) return FALSE;

	// Read in FAT sector
	sec_get(&sa);

	return TRUE;
}

static u16  clust_next(u16 clust)	// Find next cluster in chain. Return CLUSTCHAIN_END if no next,
							// and 0 if error
{
	u16 next;

	// Read in FAT sector containing clust
	if (!(clust_getfat(clust))) return 0;

	// Find next cluster entry from FAT
	if ((next = peekw( (clust % (BLOCKSIZE / 2)) * 2)) >= CLUSTCHAIN_END) next = CLUSTCHAIN_END;	// Above, CLUSTCHAIN_END (End Of cluster Chain)

	return next;
}


static u32  clust_nextlba(u32 lba)	// Find next sector in same cluster chain
{
	u32 next = lba + 1;
	u16 c1, c2;

	// Calculate lba and next clusters
	c1 = lba2clust(lba);
	c2 = lba2clust(next);

	// If sectors are in the same cluster, simply return next
	if (c1 == c2) return next;

	// If not, fetch next cluster in cluster chain and return
	// 1st sector of this new cluster
	if ((c2 = clust_next(c1)) < CLUSTCHAIN_END) return clust2lba(c2);

	// Did not find next cluster
	return 0;
}

//
//
// ****************** LV4 - F I L E   T R E E   I N T E R F A C E   F U N C S ******************
//
//
//	From Microsoft Extensible Firmware Initiative FAT32 File System Specification :
//	Special notes about the first u8 (Name[0]) of a FAT directory entry :
//		If Name[0] == 0xE5, then the directory entry is free (there is no file or directory name in this entry).
//		If Name[0] == 0x00, then the directory entry is free (same as for 0xE5), and there are no allocated directory entries after this one (all of the Name[0] u8s in all of the entries after this one are also set to 0).

#define DE_FREE			((u8) 0xE5)
#define DE_FREE_LAST	((u8) 0x00)
#define DE_NONE			((u8) 0xFF)


bool  dir_next(bool bfree)	// dir_next() : return next dirent
							// If bfree is FALSE then we look for an unused dirent.
							// If bfree is TRUE then we look for an used dirent.
{
	u8 c;
	
	// Read current directory sector into buffer
	sec_get(&lba_tmpdir);

	do
	{
		// Go to next dirent
		++pde_cur;

		// Put 1st char of current dirent filename into c;
		c = pde_cur -> Name[0];

		// If we go outside secbuf, go to next sector of directory (if any)
		if (((u32) pde_cur - (u32) sector) >= BLOCKSIZE)
		{
			c = (bfree ? 1 : DE_FREE); // Prepare to loop in there

			if (lba_curdir == lba_rd)	// We're in root dir so we merely go to next sector checking we're below hd1_geom_rd_size
			{
				if (lba_tmpdir - lba_curdir < hd1_geom_rd_size)
					lba_tmpdir++;
				else
					c = (bfree ? DE_NONE : DE_FREE_LAST);
			}
			else	// We're not in root dir so find next sector of current dir in its cluster chain
			{
				if (!(lba_tmpdir = clust_nextlba(lba_tmpdir)))
					c = (bfree ? DE_NONE : DE_FREE_LAST);
			}

			if (c != (bfree ? DE_NONE : 0))	// We are on a new dir sector, read it in and prepare next scan
			{
				sec_get(&lba_tmpdir);

				pde_cur = (dirent *) sector;
				--pde_cur;
			}
		}
	}
	while ((bfree ?		// skip (un)used entries
			/* unused */	((c != DE_FREE_LAST) && (c != DE_FREE) && (c != DE_NONE))
			/* used */ :	(c == DE_FREE)
		   ));

	// If we are on a (un)used entry then return OK
	if ((bfree ? (c != DE_NONE) : (c != DE_FREE_LAST)))
	{
		memcpy(&de_cur,pde_cur,sizeof(de_cur));

		return TRUE;
	}

	return FALSE;
}

bool  dir_examine(bool bfree)	// dir_examine() : start scan of current dir and return first (un)used dirent
{
	// Set current directory sector to 1st sector of current dir
	lba_tmpdir = lba_curdir;

	// "Rewind back" so that dir_next starts scan at 1st dirent
	pde_cur = (dirent *) sector;
	--pde_cur;

	// Find next dirent
	return dir_next(bfree);
}
	 

//
// Convert filename to padded 8.3 format
//


static char *  __create_83_name(char *filename)
{
	static char name83[12];		   
	int cnt;
	char *p,c;

	// copy name portion of filename, converting case
	cnt=0;
	p=filename;
	while(cnt<8)
	{
		c=*p;
		if(c=='.' || c==0)
			break;
		name83[cnt++]=toupper(c);
		p++;
	}

	// pad name out with spaces
	while(cnt<8)
		name83[cnt++]=' ';

	// copy extension portion of filename, converting case
	if(*p++=='.')
	{
		while(cnt<11)
		{
			c=*p;
			if(c==0)
				break;
			name83[cnt++]=toupper(c);
			p++;
		}
	}

	// pad extension out with spaces
	while(cnt<11)
		name83[cnt++]=' ';

	name83[11]=0;

	return name83;
}

bool  dir_findbyname(char *filename)
{
	char *name = __create_83_name(filename);

#ifdef CCD_DEBUG
	char tmp[12];

	mprintf("Open %s\n",name);

#endif


	if (dir_examine(FILE_USED))
	{
	
		do
		{
		    
			if (de_cur.Attr != ATTR_LONG_NAME)
			{
#ifdef CCD_DEBUG
				memcpy(tmp,de_cur.Name,11);
				tmp[11]=0;
				mprintf("Scan %s\n",tmp);
#endif
			    // We stop at first match, only comparing at strlen(filename) maxed at 11
				if (!(strncmp(name,	de_cur.Name,	11	)))
				{
					return TRUE;
				}
			}
		}
		while (dir_next(FILE_USED));
	}

	return FALSE;
}
 

//
//
// ****************** LV6 - P O S I X   L I B   P R O T O S ******************
//
//

static inline bool  __h_in_use(u8 handle)
{
	// Limit collateral damages in case of wrong handle
	if (handle >= MAX_FILES) return FALSE;

	return __files[handle].inuse;
}

static u8  __h_findfree(void)
{
	u8 hnum;

	for (hnum = 0 ; (hnum < MAX_FILES) && (__h_in_use(hnum)) ; ++hnum);

	return hnum;
}

s8  close(u8 handle)
									/* Close file handle
										Parameter
											handle	Handle referring to open file

										Returns
											0 if the file was successfully closed.
											A return value of -1 indicates an error,
											in which case errno is set to EBADF,
											indicating an invalid file-handle parameter.
									*/
{
#ifdef CCD_DEBUG
	mprintf("close %d",handle);
#endif

	if (!(__h_in_use(handle)))	return -1;

	__files[handle].inuse = FALSE;

#ifdef CCD_DEBUG
	mprintf("close() OK");
#endif

	return 0;
}


s8  eof(u8 handle)
									/* Tests for end-of-file.
										Parameter
											handle	Handle referring to open file

										Returns
											1 if the current position is end of file,
											or 0 if it is not. A return value of -1
											indicates an error; in this case, errno
											is set to EBADF, which indicates an invalid
											file handle.
									*/
{
	if (!(__h_in_use(handle)))	return -1;

	if (__files[handle].pos + 1 > __files[handle].size) return 1;

	return 0;
}

s8  open(char *filename, u8 oflag, u8 pmode)
									/* Open a file.
										Parameters
											filename	Filename
											oflag		Type of operations allowed
											pmode		Permission mode

										Returns
											Returns a file handle for the opened file.
											A return value of -1 indicates an error,
											in which case errno is set to one of the following
											values:
												EACCES	Tried to open read-only file for
														writing, or file’s sharing mode does
														not allow specified operations, or
														given path is directory
												EEXIST	O_CREAT and O_EXCL flags specified,
														but filename already exists
												EINVAL	Invalid oflag or pmode argument
												EMFILE	No more file handles available
														(too many open files)
												ENOENT	File or path not found

											oflag is an integer expression formed from one
											or more of the following manifest constants
											or constant combinations :
												O_APPEND	Moves file pointer to end of
														file before every write operation.
												O_CREAT		Creates and opens new file for
														writing. Has no effect if file
														specified by filename exists. pmode
														argument is required when O_CREAT is
														specified.
												O_CREAT | O_EXCL	Returns error value
														if file specified by filename
														exists. Applies only when used
														with O_CREAT.
												O_RDONLY	Opens file for reading only;
														cannot be specified with O_RDWR
														or O_WRONLY.
												O_RDWR	Opens file for both reading and
														writing; you cannot specify this
														flag with O_RDONLY or O_WRONLY.
												O_TRUNC	Opens file and truncates it to zero
														length; file must have write
														permission. You cannot specify this
														flag with O_RDONLY. O_TRUNC used
														with O_CREAT opens an existing file
														or creates a new file.
														Warning   The O_TRUNC flag destroys
														the contents of the specified file.
												O_WRONLY	Opens file for writing only;
														cannot be specified with O_RDONLY
														or O_RDWR.

											To specify the file access mode, you must specify
											either O_RDONLY, O_RDWR, or O_WRONLY. There is no
											default value for the access mode.

											The pmode argument is required only when O_CREAT
											is specified. If the file already exists, pmode
											is ignored. Otherwise, pmode specifies the file
											permission settings, which are set when the new
											file is closed the first time. open applies the
											current file-permission mask to pmode before
											setting the permissions (for more information,
											see umask). pmode is an integer expression
											containing one or both of the following manifest
											constants :
												S_IREAD		Reading only permitted
												S_IWRITE	Writing permitted (effectively
													permits reading and writing)
												S_IREAD | S_IWRITE	Reading and writing
													permitted
									*/
{
	// Find a free handle
	u8 handle = __h_findfree();
	file_handle *fd = &(__files[handle]);
	u32 curlba,next_lba;

	// Check if file already exists
	bool bexist;

#ifdef CCD_DEBUG
	mprintf("open(%s mode %d)\n\r",filename,oflag);
#endif

	bexist = dir_findbyname(filename);

	// If no more handle available then error EMFILE
	if (handle >= MAX_FILES) return -1;

	// If asked to create a new file which already exists, error
	if (bexist && (oflag & O_CREAT) && (oflag & O_EXCL)) return -1;

	// If asked to truncate a file without write permission, error
	if (bexist && (oflag & O_TRUNC) && (pde_cur -> Attr & ATTR_READ_ONLY)) return -1;

	// If asked to create or truncate, error
	if ((oflag & O_CREAT) || (oflag & O_TRUNC)) return -1;

	// Now we are left with O_APPEND, O_RDONLY, O_RDWR. File must exist.
	if (!bexist) return -1;

	// If asked for meaningless combinations of RDWR, WRONLY and RDONLY then error
	if ((oflag & O_RDWR)	&& (oflag & O_RDONLY)) return -1;
	if ((oflag & O_RDWR)	&& (oflag & O_WRONLY)) return -1;
	if ((oflag & O_RDONLY)	&& (oflag & O_WRONLY)) return -1;

	// Everything is OK, open file
	fd -> size = peekl((u16)((char *)&pde_cur->FileSize - (char*)sector));
	fd -> clust = peekw((u16)((char *)&pde_cur->FstClusLO - (char*)sector));
	fd -> pos = (oflag & O_APPEND ? pde_cur -> FileSize : 0);
	fd -> mode = oflag & (O_RDWR | O_WRONLY | O_RDONLY);
	fd -> inuse = TRUE;
	fd -> dirlba = lba_tmpdir;
	fd -> dirptr = pde_cur;
	fd -> curlba = clust2lba(fd -> clust);

	// Scan for contigous clusters
	curlba = fd->curlba;
	fd->sector_rl=0;			
	next_lba=clust_nextlba(curlba);
	while(next_lba && (next_lba == curlba+1))
	{
		fd->sector_rl++;
		curlba=next_lba;
		next_lba=clust_nextlba(curlba);
	}

	return handle;
}

long  lseek(u8 handle, s32 offset, u8 origin)
									/* Move a file pointer to the specified location.
										Parameters
											handle	Handle referring to open file
											offset	Number of u8s from origin
											origin	Initial position
											origin argument must be one of the following
											constants :
												SEEK_SET	Beginning of file
												SEEK_CUR	Current position of file pointer
												SEEK_END	End of file

										Returns
											the offset, in u8s, of the new position from
											the beginning of the file. Returns -1L to
											indicate an error and sets errno either to EBADF,
											meaning the file handle is invalid, or to EINVAL,
											meaning the value for origin is invalid or the
											position specified by offset is before the
											beginning of the file.
									*/
{
	file_handle *fd = &(__files[handle]);
	u16 clust, clustcnt;
	u8 secoff;

	if (!(__h_in_use(handle)))	return -1;

	// Recalculate the offset from the start of the file
	switch(origin)
	{
		default:
			return -1;

		case SEEK_SET:
			break;

		case SEEK_CUR :	//	Current position of file pointer
			offset += fd -> pos;
			break;

		case SEEK_END :	//	End of file
			offset = fd -> size - 1 + offset;
			break;
	}

	// Process special offsets
	if (offset > fd -> size ) return -1;

	if (offset == fd -> pos)
		return offset;


	// Update current pos
	fd -> pos = offset;

	// Calculate cluster / sector number of new pos
	clustcnt =  offset / hd1_geom_clustsize;
	secoff = (u8) ((offset % hd1_geom_clustsize) / (u32) BLOCKSIZE);

	// Update current LBA
	clust = fd -> clust;
	while ((clustcnt--) && (clust < CLUSTCHAIN_END)) clust = clust_next(clust);

	if (clust < CLUSTCHAIN_END)
	{
	
		fd -> curlba = clust2lba(clust) + secoff;
	}
	else
		fd->curlba = 0; // flag for extending chain

	return offset;
}

s16  read(u8 handle, u8 *buffer, u16 count)
									/* Reads data from a file.
											Parameters
												handle	Handle referring to open file
												buffer	Storage location for data
												count	Maximum number of u8s

											Returns
												the number of u8s read, which may be less
												than count if there are fewer than count u8s
												left in the file.
												If the function tries to read at end of file,
												it returns 0. If the handle is invalid, or the
												file is not open for reading, the function
												returns -1 and sets errno to EBADF.
									*/
{
	file_handle *fd = &(__files[handle]);

	u16
		offset_start = (u16) (fd -> pos & (BLOCKSIZE-1)),
		u8s_read = 0,
		u8s_toread,
		u8s_leftinsec;

	if (!(__h_in_use(handle)))	return -1;

	// If asked to read from a non read file then error
	if (!((fd -> mode & O_RDWR) || (fd -> mode & O_RDONLY)))	return -1;

	while ((u8s_read < count) && !(eof(handle)))
	{
		// Read in the file current sector
		sec_get(&(fd -> curlba));

		// 1 - calculate how many u8s are left in the current sector
		u8s_leftinsec = BLOCKSIZE - offset_start;

		// 2 - calculate how many u8s we can thus read in the current sector
		u8s_toread = min(count - u8s_read, u8s_leftinsec);


		// 3 - read these u8s in the user buffer
		memcpy(buffer+u8s_read, sector+offset_start,u8s_toread);

		// 4 - increment the read u8s counter
		u8s_read += u8s_toread;

		// 5 - if necessary go to next sector
		if (u8s_toread == u8s_leftinsec)
		{
			if(fd->sector_rl)
			{	 
				fd->sector_rl--;
				fd->curlba++;
			}
			else
			{
			    
				fd -> curlba = clust_nextlba(fd -> curlba);
			}
		}

		// 6 - zero offset_start, which is only useful in 1st pass
		offset_start = 0;
	}

	// Adjust file pos
	fd -> pos += u8s_read;

	return u8s_read;
}

//
// read N sectors. file position must be aligned on sector offset
// 
s16 read_sectors(u8 handle,u8 *buffer,u16 sectors)
{
	file_handle *fd = &(__files[handle]);
   	u16 actual;

	if (!(__h_in_use(handle)))	return -1;

	// If asked to read from a non read file then error
	if (!((fd -> mode & O_RDWR) || (fd -> mode & O_RDONLY)))	return -1;

	if(fd->pos & 511) return -1;

	actual=0;

	while(!eof(handle) && sectors!=0)
	{
	  mmc_SectorRead(buffer,fd -> curlba);	   	

	  fd->pos+=512;
  	  buffer+=512;
	  sectors--;
	  actual++;

	  if(fd->sector_rl)
			{	 
      		
				fd->sector_rl--;
				fd->curlba++;
			}
			else
			{
			    
				fd -> curlba = clust_nextlba(fd -> curlba);
			}
   }

	return actual;	  
}

//
// count number of directories, or seek to a particular directory index
//
s16 scan_dirs(int no,char *dirname)
{
	s16 count=0;

	// back to root directory
	lba_curdir = lba_rd;
	strcpy(dirname,"TOP");

	if(!no)
		return 0;

	if (dir_examine(FILE_USED))
	{
	
		do
		{
				if(de_cur.Attr & ATTR_DIRECTORY)
				{
					if(++count==no)
					{
						if(dirname!=NULL)
						{
 							memcpy(dirname,&de_cur.Name[0],11);
							dirname[11]=0;
						}

						// change current directory
						lba_curdir = clust2lba(peekw((u16)((char *)&pde_cur->FstClusLO - (char*)sector)));

						return count;
					}
				}
		
		}
		while (dir_next(FILE_USED));
	}

  	return count;
}

//
// count number of tracks within a given directory, or seek to a particular track
//
s16 scan_tracks(int dirno,int fileno,char *filename,char *dirname)
{
	s16 count=0;
	int i;

	// scan to appropriate directory
	if(scan_dirs(dirno,dirname)!=dirno)	
	{
			return -1; // not found
	}
	
	if (dir_examine(FILE_USED))
	{
		do
		{
			if (de_cur.Attr != ATTR_LONG_NAME && (!(de_cur.Attr & ATTR_DIRECTORY)))
			{
				if(!strncmp("WAV", &de_cur.Name[8], 3))
				{
					if(++count == fileno)
					{
					 if(filename!=NULL)
					 {
					    memcpy(filename, &de_cur.Name[0], 11);
					    filename[11]=0;

					    // convert name back to standard filename convention
					    for(i=7; i>0; i--)
						{
							if(filename[i]!=' ')
								break;
						}
		
						filename[i+1]=0;
		
						strcat(filename, ".WAV");

					 }
					 return count;
					}		
				}
			}
		}
		while (dir_next(FILE_USED));
	}

	return count;
   	
}

#endif

