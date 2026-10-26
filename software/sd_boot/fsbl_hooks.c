/* FSBL hooks for the Z7-Nano interpreter SD boot. Replaces the generated
   fsbl_hooks.c (scripts/build_sd_boot_fsbl.py). Before handoff, copy the
   optional z7cfg.bin from the already mounted SD volume into reserved OCM;
   the firmware validates and wipes it. No file means no runtime config. */
#include "xparameters.h"
#include "fsbl.h"
#include "xstatus.h"
#include "fsbl_hooks.h"
#include "sd_boot.h"   /* copied next to this file by the build script */
/* Same SD presence test as the generated sd.c (2026.1 uses the SDT name). */
#if defined(XPAR_PS7_SD_0_S_AXI_BASEADDR) || defined(XPAR_XSDPS_0_BASEADDR)
#define Z7_FSBL_HAS_SD 1
#include "ff.h"
#endif

u32 FsblHookBeforeBitstreamDload(void) {return XST_SUCCESS;}
u32 FsblHookAfterBitstreamDload(void) {return XST_SUCCESS;}

u32 FsblHookBeforeHandoff(void)
{
	volatile u32 *config = (volatile u32 *)SD_BOOT_CONFIG_ADDRESS;
	for (u32 i = 0; i < SD_BOOT_CONFIG_BYTES / 4; ++i) config[i] = 0;
#ifdef Z7_FSBL_HAS_SD
	static FIL file;
	UINT got = 0;
	if (f_open(&file, "0:/z7cfg.bin", FA_READ) == FR_OK) {
		if (f_size(&file) <= SD_BOOT_CONFIG_BYTES)
			(void)f_read(&file, (void *)SD_BOOT_CONFIG_ADDRESS, SD_BOOT_CONFIG_BYTES, &got);
		(void)f_close(&file);
	}
	fsbl_printf(DEBUG_GENERAL, "Z7 config bytes: %d\r\n", (int)got);
#endif
	return XST_SUCCESS;
}

void FsblHookFallback(void)
{
	while (1);
}
