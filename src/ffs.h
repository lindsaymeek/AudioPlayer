
#ifndef _FFS_H
#define _FFS_H

#include "types.h"
#include <stdio.h>

/*

Description:	FAT16 file system POSIX interface

*/

// FAT stuff
#define ATTR_READ_ONLY  ((u8) 0x01)
#define ATTR_HIDDEN 	((u8) 0x02)
#define ATTR_SYSTEM 	((u8) 0x04)
#define ATTR_VOLUME_ID 	((u8) 0x08)
#define ATTR_DIRECTORY	((u8) 0x10)
#define ATTR_ARCHIVE  	((u8) 0x20)
#define ATTR_LONG_NAME 	(ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)
#define ATTR_ALL 		~ATTR_VOLUME_ID

// fstat stuff
#define S_IREAD        0x01         /* read permission, owner */
#define S_IWRITE       0x02         /* write permission, owner */

// open stuff
#define O_RDONLY       0x01  /* open for reading only */
#define O_WRONLY       0x02  /* open for writing only */
#define O_RDWR         0x04  /* open for reading and writing */
#define O_APPEND       0x08  /* writes done at eof */

#define O_CREAT        0x10  /* create and open file */
#define O_TRUNC        0x20  /* open and truncate */
#define O_EXCL         0x40  /* open only if file doesn't already exist */

// lseek stuff (attention, various values found on various systems)
#undef SEEK_CUR
#undef SEEK_END
#undef SEEK_SET
#define SEEK_CUR	0
#define SEEK_END	1
#define SEEK_SET	2

bool hd_qry(void);			// hd_qry() : inquiry disk properties and display them

bool hd_mbr(void);			// hd_mbr() : read disk Master Boot Record, check it and analyze partition entries

bool hd_bpb(void);	// Analyze partition Boot Param Block to find out addresses of FAT and rootdir

s8  close(u8 handle);
									/* Close file handle
										Parameter
											handle	Handle referring to open file

										Returns
											0 if the file was successfully closed.
											A return value of -1 indicates an error,
											in which case errno is set to EBADF,
											indicating an invalid file-handle parameter.
									*/

s8  eof(u8 handle);
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

s8  open(char *filename, u8 oflag, u8 pmode);
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

s16  read(u8 handle, u8 *buffer, u16 count);
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

s16 read_sectors(u8 handle,u8 *buffer,u16 sectors);
  

long  lseek(u8 handle, s32 offset, u8 origin);
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
 
// FAT directory entry


typedef struct
{
	char	 Name[11];			//	 8+3 name.
	u8	 Attr;				//	 File attributes - The upper two bits of the attribute u8 are reserved and should always be set to 0 when a file is created and never modified or looked at after that.
	u8	 NTRes;				//	 Reserved for use by Windows NT. Set value to 0 when a file is created and never modify or look at it after that.
	u8	 CrtTimeTenth;		//	 Millisecond stamp at file creation time. This field actually contains a count of tenths of a second. The granularity of the seconds part of CrtTime is 2 seconds so this field is a count of tenths of a second and its valid value range is 0-199 inclusive.
	u16	 CrtTime;			//	 Time file was created.
	u16	 CrtDate;			//	 Date file was created.
	u16	 LstAccDate;		//	 Last access date. Note that there is no last access time, only a date. This is the date of last read or write. In the case of a write, this should be set to the same date as WrtDate.
	u16	 FstClusHI;			//	 High word of this entry's first cluster number (always 0 for a FAT12 or FAT16 volume).
	u16	 WrtTime;			//	 Time of last write. Note that file creation is considered a write.
	u16	 WrtDate;			//	 Date of last write. Note that file creation is considered a write.
	u16	 FstClusLO;			//	 Low word of this entry's first cluster number.
	u32	 FileSize;			//	 32-bit DWORD holding this file's size in u8s.
} dirent __attribute__((packed));


typedef struct
{
	u16	clust;		// 1st cluster of file (as found in dirent)
	u32 sector_rl;	// contigous sectors found (minimises rescanning of FAT)
	u32 curlba;		// LBA of current sector being read / written to
	u32	pos;		// Current file pointer (u8 offset from start of file)
	u32	size;		// Current file size (u8 count of file)
	u8	inuse;		// In-use flag
	u8	mode;		// O_RDWR, O_RDONLY, O_WRONLY
	
	u32 dirlba;		// LBA of dir sector
	dirent *dirptr;	// ptr of file in secbuf when dirlba is in secbuf
} file_handle;

// scan to a particular directory, or return total count
s16 scan_dirs(int index,char *dirname);	

s16 scan_tracks(int dirno,int fileno,char *filename,char *dirname);

#endif
