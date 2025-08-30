PSPDEV ?= /usr/local/pspdev
PSPSDK ?= $(PSPDEV)/psp/sdk

TARGET = calc-ios
OBJS   = src/main.o

INCDIR  =
CFLAGS  = -O2 -G0 -Wall
CXXFLAGS= $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
PSP_EBOOT_TITLE = iOS Calc
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_ICON = assets/ICON0.PNG
PSP_EBOOT_PIC1 = assets/PIC1.PNG
LIBS = -lpspdebug -lpspdisplay -lpspctrl

include $(PSPSDK)/lib/build.mak
