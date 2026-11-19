#---------------------------------------------------------------------------------
# SSX (EA Sports, 2012; the Android build com.ea.ssx 0.0.8430) -- Nintendo
# Switch wrapper (32-bit / AArch32).
#
# Ships NO game code and NO game assets: the player's own APK, found in
# sd:/switch/ssx-nx/ by what is in it whatever its name, is read at run time;
# its two libraries are unpacked from it on the first launch
# (source/ssx_main.c), its assets are read in place (source/ssx_assets.c).
#
# The build is the android32 runtime's (runtime/runtime.mk: devkitARM +
# libnx32 + mesa32). ./build.sh runs it natively in devkitPro's MSYS2 (no
# Docker); libnx32 and mesa32 are found next to this folder
# (../libnx32/prefix, ../mesa32/prefix) or where SSX_LIBNX32 / SSX_PORTLIBS
# point. Output: ssx_nx.nsp, which the launcher NRO carries (launcher/), or
# which an emulator runs directly.
#---------------------------------------------------------------------------------
TARGET               := ssx_nx
PORT_NPDM_PROGRAM_ID := 0x0100000000001055
# devkitARM's bare-metal int32_t is 'long'; the runtime, libnx32 and mesa32 are
# written for (and here built with) the ILP32 convention, int32_t 'int', as in
# the vita2hos toolchain image they were made in. Same size and C ABI.
SSX_I32 := -U__INT32_TYPE__ -D__INT32_TYPE__=int -U__UINT32_TYPE__ "-D__UINT32_TYPE__=unsigned int"            -U__INT_LEAST32_TYPE__ -D__INT_LEAST32_TYPE__=int -U__UINT_LEAST32_TYPE__ "-D__UINT_LEAST32_TYPE__=unsigned int"
PORT_CFLAGS  := $(SSX_I32)
PORT_ASFLAGS := $(SSX_I32)
# libnx's nvMapCreate/nvMapClose through source/ssx_globjs.c's counters: the GPU
# memory the driver holds (every nouveau buffer is an nvmap of heap memory).
PORT_LDFLAGS := -Wl,--wrap=nvMapCreate -Wl,--wrap=nvMapClose
# libnx and Mesa aborts leave a report (source/ssx_abort.c)
PORT_LDFLAGS += -Wl,--wrap=fatalThrow -Wl,--wrap=diagAbortWithResult
# The movies: ffmpeg32 (MP4 demuxer, MPEG-4 Part 2 video, AAC; LGPL), built
# next to this folder (../deps/ffmpeg32/prefix) or where SSX_FFMPEG points.
# Without it the movies are skipped (source/ssx_video.c).
SSX_FFMPEG ?= $(CURDIR)/../deps/ffmpeg32/prefix
SSX_VIDEO  := $(if $(wildcard $(SSX_FFMPEG)/lib/libavcodec.a),1,0)
PORT_STAMP := -video$(SSX_VIDEO)
ifeq ($(SSX_VIDEO),1)
PORT_LIBS  := -L$(SSX_FFMPEG)/lib -lavformat -lavcodec -lavutil
endif
include runtime/runtime.mk

# FFmpeg's headers need int-sized enums (-fno-short-enums); only ssx_movie.c
# includes them, and shares no enum with the rest.
$(BUILD)/ssx_movie.o: $(SOURCES)/ssx_movie.c $(RENDERER_STAMP) | $(BUILD) $(BUILD)/dcr_build.h
	@echo $(notdir $<)
	@$(CC) -MMD -MP $(CFLAGS) -fno-short-enums -I$(SSX_FFMPEG)/include -DSSX_VIDEO=$(SSX_VIDEO) -c $< -o $@

# The shader cache's name for this Mesa (ssx_shadercache.c): the hash of the
# Mesa libraries linked; a new Mesa starts a new cache.
SSX_MESA_LIBS := $(wildcard $(PORTLIBS)/lib/libEGL.a $(PORTLIBS)/lib/libGLESv2.a $(PORTLIBS)/lib/libglapi.a $(PORTLIBS)/lib/libdrm_nouveau.a)
SSX_MESA_ID := $(shell cat $(SSX_MESA_LIBS) 2>/dev/null | sha1sum | cut -c1-16)
$(BUILD)/ssx_shadercache.o: CFLAGS += -DSSX_MESA_ID=\"$(SSX_MESA_ID)\"
$(BUILD)/ssx_shadercache.o: $(SSX_MESA_LIBS)

# pow on the FPU with the same results as newlib's soft-float pow: no fused
# multiply-add (ssx_vfpmath.c).
$(BUILD)/ssx_vfpmath.o: CFLAGS += -ffp-contract=off -fno-math-errno
$(BUILD)/ssx_vfpmathf.o: CFLAGS += -ffp-contract=off

# sinf/cosf/acosf/asinf on the FPU (ssx_vfpmathf.c): newlib's own fdlibm
# kernels from devkitARM's FPU multilib (armv6k/fpu: VFPv2, no fused
# multiply-add, so the same IEEE results as the soft-float build), their
# symbols renamed vfp_* so that nothing else links to them, their ABI tag
# dropped (they pass floats in VFP registers; ssx_vfpmathf.c calls them with
# pcs("aapcs-vfp")). Taken from the toolchain at build time: nothing of it is
# kept in this repository.
VFP_LIBM_A    := $(DEVKITARM)/arm-none-eabi/lib/armv6k/fpu/libm.a
VFP_LIBM_OBJS := kf_sin kf_cos ef_rem_pio2 kf_rem_pio2 ef_acos ef_asin ef_sqrt sf_fabs sf_floor sf_scalbn
VFP_LIBM_SYMS := __kernel_sinf __kernel_cosf __ieee754_rem_pio2f __kernel_rem_pio2f __ieee754_acosf \
                 __ieee754_asinf __ieee754_sqrtf fabsf floorf scalbnf
$(BUILD)/vfp_libm.o: $(VFP_LIBM_A) Makefile | $(BUILD)
	@echo $(notdir $@)
	@rm -rf $(BUILD)/vfp_libm && mkdir -p $(BUILD)/vfp_libm
	@cd $(BUILD)/vfp_libm && $(PREFIX)ar x $(VFP_LIBM_A) $(addprefix libm_a-,$(addsuffix .o,$(VFP_LIBM_OBJS)))
	@for f in $(BUILD)/vfp_libm/*.o; do \
	  $(OBJCOPY) -R .ARM.attributes $(foreach s,$(VFP_LIBM_SYMS),--redefine-sym $(s)=vfp_$(s)) $$f || exit 1; done
	@$(PREFIX)ld -r $(BUILD)/vfp_libm/*.o -o $@
	@! $(PREFIX)nm $@ | grep -v " vfp_" | grep -E " [TU] " || (echo "vfp_libm.o: a symbol left unrenamed" >&2; exit 1)
$(TARGET).elf: $(BUILD)/vfp_libm.o
LIBS := $(BUILD)/vfp_libm.o $(LIBS)

# The NSO: devkitPro's elf2nso takes AArch64 only (the vita2hos image's took
# 32-bit ELFs too): tools/elf2nso32.py writes it instead. PYTHON is set by
# ./build.sh (a Python 3 the build shell can run).
PYTHON ?= python3
$(BUILD)/$(TARGET).nso: $(TARGET).elf tools/elf2nso32.py | $(BUILD)
	@$(PYTHON) tools/elf2nso32.py $< $@
