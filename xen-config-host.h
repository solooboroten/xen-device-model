#ifndef XEN_CONFIG_HOST_H
#define XEN_CONFIG_HOST_H

#ifdef __MINIOS__
#define CONFIG_STUBDOM
#undef CONFIG_AIO
#define NO_UNIX_SOCKETS 1
#define NO_BLUETOOTH_PASSTHROUGH 1
#endif

#define CONFIG_DM

extern char domain_name[64];
extern int domid, domid_backend;

#include <errno.h>
#include <stdbool.h>

#include "xenctrl.h"
#include "xs.h"
#ifndef CONFIG_STUBDOM
#include "blktaplib.h"
#endif

#define BIOS_SIZE ((256 + 64) * 1024)

#undef CONFIG_GDBSTUB

void main_loop_prepare(void);

extern int xc_handle;
extern int xen_pause_requested;
extern int vcpus;

#define DEFAULT_NETWORK_SCRIPT "/etc/xen/qemu-ifup"
#define DEFAULT_NETWORK_DOWN_SCRIPT "/etc/xen/qemu-ifdown"

#ifdef CONFIG_STUBDOM
extern struct BlockDriver bdrv_vbd;
#endif
struct CharDriverState;
void xenstore_store_serial_port_info(int i, struct CharDriverState *chr,
				     const char *devname);

extern unsigned long *logdirty_bitmap;
extern unsigned long logdirty_bitmap_size;

#endif /*XEN_CONFIG_HOST_H*/
