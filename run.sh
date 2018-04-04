#make clean && make

#LD_PRELOAD=/usr/local/cuda/lib/libcudart.so 
./NvEncoderCudaInterop -i $PWD/1920x1080_RGB_tosscreen.rgb \
            -o output.h264 \
            -size 1920 1080
