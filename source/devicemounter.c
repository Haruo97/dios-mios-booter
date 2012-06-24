﻿#include <gccore.h>
#include <ogc/mutex.h>
#include <ogc/system.h>
#include <ogc/usbstorage.h>
#include <ogc/lwp_watchdog.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <fat.h>
#include <sdcard/wiisd_io.h>
#include <unistd.h>
#include <time.h>

#include "devicemounter.h"

#define MAX_SECTOR_SIZE	4096

//these are the only stable and speed is good
#define CACHE 8
#define SECTORS 64

bool _SD_Inited = false;
bool _USB_Inited = false;

typedef struct _PARTITION_RECORD {
	u8 status;                              /* Partition status; see above */
	u8 chs_start[3];                        /* Cylinder-head-sector address to first block of partition */
	u8 type;                                /* Partition type; see above */
	u8 chs_end[3];                          /* Cylinder-head-sector address to last block of partition */
	u32 lba_start;                          /* Local block address to first sector of partition */
	u32 block_count;                        /* Number of blocks in partition */
} __attribute__((__packed__)) PARTITION_RECORD;


typedef struct _MASTER_BOOT_RECORD {
	u8 code_area[446];                      /* Code area; normally empty */
	PARTITION_RECORD partitions[4];         /* 4 primary partitions */
	u16 signature;                          /* MBR signature; 0xAA55 */
} __attribute__((__packed__)) MASTER_BOOT_RECORD;

#define PARTITION_TYPE_LINUX    0x83
#define le32(i) (((((u32) i) & 0xFF) << 24) | ((((u32) i) & 0xFF00) << 8) | \
				((((u32) i) & 0xFF0000) >> 8) | ((((u32) i) & 0xFF000000) >> 24))

s32 USBDevice_Init()
{
	time_t start = time(0);

	while(time(0) - start < 10) // 10 sec
	{
		if(__io_usbstorage.startup() && __io_usbstorage.isInserted())
			break;
		usleep(200000); // 1/5 sec
	}

    if(!__io_usbstorage.startup() || !__io_usbstorage.isInserted())
        return -1;

	int i;
	MASTER_BOOT_RECORD *mbr = (MASTER_BOOT_RECORD *) malloc(MAX_SECTOR_SIZE);
	char *BootSector = (char *) malloc(MAX_SECTOR_SIZE);
	if(!mbr || !BootSector)
		return -1;

	__io_usbstorage.readSectors(0, 1, mbr);

	for(i = 0; i < 4; ++i)
	{
		if(mbr->partitions[i].type == 0)
			continue;

		__io_usbstorage.readSectors(le32(mbr->partitions[i].lba_start), 1, BootSector);

		if(*((u16 *)(BootSector + 0x1FE)) == 0x55AA)
		{
			//! Partition typ can be missleading the correct partition format. Stupid lazy ass Partition Editors.
			if(memcmp(BootSector + 0x36, "FAT", 3) == 0 || memcmp(BootSector + 0x52, "FAT", 3) == 0)
			{
				free(BootSector);
				fatMount(DeviceName[USB], &__io_usbstorage, le32(mbr->partitions[i].lba_start), CACHE, SECTORS);
				free(mbr);
				_USB_Inited = true;
				return 1;
			}
		}
    }
	free(mbr);
	free(BootSector);

	return -1;
}

bool USBDevice_Inited()
{
	return _USB_Inited;
}

void USBDevice_deInit()
{
	fatUnmount("usb:/");
	//Let's not shutdown so it stays awake for the application
	__io_usbstorage.shutdown();
	USB_Deinitialize();
	_USB_Inited = false;
}

s32 SDCard_Init()
{
	if(!__io_wiisd.startup() || !__io_wiisd.isInserted())
		return -1;

	if(fatMount(DeviceName[SD], &__io_wiisd, 0, CACHE, SECTORS))
	{
		_SD_Inited = true;
		return 1;
	}

	return -1;
}

bool SDCard_Inited()
{
	return _SD_Inited;
}

void SDCard_deInit()
{
	//closing all open Files write back the cache and then shutdown em!
	fatUnmount("sd:/");
	//Let's not shutdown so it stays awake for the application
	__io_wiisd.shutdown();
	_SD_Inited = false;
}
