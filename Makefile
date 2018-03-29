################################################################################
#
# Copyright 1993-2015 NVIDIA Corporation.  All rights reserved.
#
# Please refer to the NVIDIA end user license agreement (EULA) associated
# with this source code for terms and conditions that govern your use of
# this software. Any use, reproduction, disclosure, or distribution of
# this software and related documentation outside the terms of the EULA
# is strictly prohibited.
#
################################################################################
#
# Makefile project only supported on Mac OSX and Linux Platforms)
#
################################################################################

# OS Name (Linux or Darwin)
OSUPPER = $(shell uname -s 2>/dev/null | tr [:lower:] [:upper:])
OSLOWER = $(shell uname -s 2>/dev/null | tr [:upper:] [:lower:])

# Flags to detect 32-bit or 64-bit OS platform
OS_SIZE = $(shell uname -m | sed -e "s/i.86/32/" -e "s/x86_64/64/")
OS_ARCH = $(shell uname -m | sed -e "s/i386/i686/")

# These flags will override any settings
ifeq ($(i386),1)
	OS_SIZE = 32
	OS_ARCH = i686
endif

ifeq ($(x86_64),1)
	OS_SIZE = 64
	OS_ARCH = x86_64
endif

# Flags to detect either a Linux system (linux) or Mac OSX (darwin)
DARWIN = $(strip $(findstring DARWIN, $(OSUPPER)))

# Common binaries
GCC             ?= clang++

# OS-specific build flags
ifneq ($(DARWIN),)
      LDFLAGS   := -Xlinker -rpath
      CCFLAGS   := -arch $(OS_ARCH)
else
  ifeq ($(OS_SIZE),32)
      LDFLAGS   += -L/usr/lib64 -lnvidia-encode -ldl -lOpenGL -lEGL -ldrm
      CCFLAGS   := -m32
  else
      LDFLAGS   += -L/usr/lib64 -lnvidia-encode -ldl -lOpenGL -lEGL -ldrm
      CCFLAGS   := -m64
  endif
endif

# Debug build flags
ifeq ($(dbg),1)
      CCFLAGS   += -g
      TARGET    := debug
else
      TARGET    := release
endif

# Common includes and paths for CUDA
INCLUDES      := -Icommon/inc -I/usr/include/libdrm -I/system/lib/nvidia/include

# Target rulesddddddddddddddddd
all: build

build: NvEncoderCudaInterop

dynlink_cuda.o: common/src/dynlink_cuda.cpp
	$(GCC) $(CCFLAGS) $(INCLUDES) -o $@ -c $<

NvHWEncoder.o: common/src/NvHWEncoder.cpp common/inc/NvHWEncoder.h
	$(GCC) $(CCFLAGS) $(EXTRA_CCFLAGS) $(INCLUDES) -o $@ -c $<

NvEncoderCudaInterop.o: NvEncoderCudaInterop.cpp common/inc/nvEncodeAPI.h NvEncoderCudaInterop.h
	$(GCC) $(CCFLAGS) $(EXTRA_CCFLAGS) $(INCLUDES) -o $@ -c $<

NvEncoderCudaInterop: NvHWEncoder.o NvEncoderCudaInterop.o dynlink_cuda.o
	$(GCC) $(CCFLAGS) -o $@ $+ $(LDFLAGS) $(EXTRA_LDFLAGS)

run: build
	./NvEncoderCudaInterop

clean:
	rm -rf NvEncoderCudaInterop NvHWEncoder.o NvEncoderCudaInterop.o dynlink_cuda.o 
