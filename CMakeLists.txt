cmake_minimum_required(VERSION 3.0)

set(TARGET NvEncoderCudaInterop)
project(${TARGET})
set(SRCS 
    common/src/dynlink_cuda.cpp
    common/src/NvHWEncoder.cpp 
    common/inc/NvHWEncoder.h
    NvEncoderCudaInterop.cpp 
    common/inc/nvEncodeAPI.h 
    NvEncoderCudaInterop.h
    )
include_directories(
    #.
    #./common/inc/
    /usr/include/libdrm
    /system/include
    #/system/lib/nvidia/include
    )
link_directories(
    #/usr/lib
    #/usr/lib/i386-linux-gnu
    )
add_executable(${TARGET} ${SRCS})
target_link_libraries(
    ${TARGET}
    #-L/usr/lib64 
    -lnvidia-encode 
    -ldl 
    -lGL 
    -lEGL 
    -ldrm
    )


