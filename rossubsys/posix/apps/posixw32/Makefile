# $Id$
#
# Win32 Terminal Emulator for the POSIX+ subsystem.
#

PATH_TO_TOP = ../../../reactos

PATH_TO_TOP_PSX = ../..

TARGET_NAME = posixw32

TARGET_TYPE = program

TARGET_APPTYPE = console

TARGET_CFLAGS = -I$(PATH_TO_TOP_PSX)/include

TARGET_SDKLIBS = ntdll.a smdll.a kernel32.a

TARGET_OBJECTS = \
	$(TARGET_NAME).o \
	console.o \
	vt100.o

include $(PATH_TO_TOP)/rules.mak

include $(TOOLS_PATH)/helper.mk

# EOF
