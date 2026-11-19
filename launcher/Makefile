#---------------------------------------------------------------------------------
# ssx-android.nro: the launcher (64-bit) that installs the 32-bit game program
# (../ssx_nx.nsp) for a sphaira forwarder's icon, or runs the setup screen.
# The shared launcher (runtime/launcher/); this file names it. Build the game
# program first (../build.sh), then ./build.sh here.
#---------------------------------------------------------------------------------
APP_TITLE    := SSX
APP_AUTHOR   := Maafeu
APP_VERSION  := 0.1.0
TARGET       := ssx-android
PORT_PAYLOAD := ssx_nx
TOPDIR ?= $(CURDIR)
A32    ?= $(TOPDIR)/../runtime
include $(A32)/launcher/launcher.mk
