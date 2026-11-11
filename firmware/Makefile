# Firmware build. Run it through the top-level Makefile (`make build`), which
# executes this inside the Docker image where libDaisy and DaisySP live.

TARGET = bass_fx

# ReverbSc lives in the LGPL part of DaisySP.
USE_DAISYSP_LGPL = 1

CPP_SOURCES = main.cpp hothouse.cpp
C_INCLUDES = -I.

LIBDAISY_DIR ?= /opt/libDaisy
DAISYSP_DIR ?= /opt/DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# The effects are header-only, so rebuild when any header changes.
$(BUILD_DIR)/main.o: $(wildcard effects/*.h dsp/*.h) hothouse.h
