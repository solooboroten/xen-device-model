OBJS += $(SOUND_HW) $(AUDIODRV) mixeng.o 
CPPFLAGS += -DHAS_AUDIO
QEMU_PROG=qemu-dm
include ../xen-hooks.mak
