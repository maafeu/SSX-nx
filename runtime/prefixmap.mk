#---------------------------------------------------------------------------------
# prefixmap.mk -- -ffile-prefix-map flags, so that the debug info and __FILE__
# name the port's files from its folder (./source/..., ./runtime/...) and the
# libraries built beside it (../libnx32/..., ../mesa32/...), not the folders of
# the machine that built them. runtime.mk and launcher.mk include it after
# setting PREFIX_MAP_ROOT (the port's folder); it sets PREFIX_MAPS.
#
# In devkitPro's MSYS2 on Windows the compiler is given C:/... paths (and names
# its working folder C:\...), so those spellings are mapped too. GCC uses the
# last map that matches, so the port's folder comes after its parent. MIT.
#---------------------------------------------------------------------------------
PREFIX_MAP_UP   := $(abspath $(PREFIX_MAP_ROOT)/..)
PREFIX_MAPS     := -ffile-prefix-map=$(PREFIX_MAP_UP)=.. -ffile-prefix-map=$(PREFIX_MAP_ROOT)=.
PREFIX_MAP_WIN  := $(shell cygpath -m '$(PREFIX_MAP_ROOT)' 2>/dev/null)
ifneq ($(PREFIX_MAP_WIN),)
PREFIX_MAPS     += -ffile-prefix-map=$(shell cygpath -m '$(PREFIX_MAP_UP)')=.. -ffile-prefix-map=$(PREFIX_MAP_WIN)=.
endif
