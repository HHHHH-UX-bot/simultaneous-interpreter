#ifndef NETWORK_UI_SD_BOOT_RUNTIME_H
#define NETWORK_UI_SD_BOOT_RUNTIME_H
int sd_boot_active(void);
void sd_boot_power_screen(void);
void sd_boot_display_ready(void);
void sd_boot_tick(void);
/* Screen, touch host and runtime configuration are done; the TF card model
   loader starts after this. */
int sd_boot_settled(void);
#endif
