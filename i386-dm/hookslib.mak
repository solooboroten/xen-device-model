
BAD_LIBOBJS += exec.o cpu-exec.o tcg%.o translate.o host-utils.o
BAD_LIBOBJS += fpu/%.o helper.o

LIBOBJS := $(filter-out $(BAD_LIBOBJS), $(LIBOBJS))
