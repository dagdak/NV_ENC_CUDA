////////////////////////////////////////////////////////////////////////////
//
// Copyright 1993-2017 NVIDIA Corporation.  All rights reserved.
//
// Please refer to the NVIDIA end user license agreement (EULA) associated
// with this source code for terms and conditions that govern your use of
// this software. Any use, reproduction, disclosure, or distribution of
// this software and related documentation outside the terms of the EULA
// is strictly prohibited.
//
////////////////////////////////////////////////////////////////////////////
// #define READ_RGB_FILE
// #define ENCODE_RGB

#define INIT_CUDA_GL 1
#include<string>
#include "NvEncoderCudaInterop.h"
#include "common/inc/nvUtils.h"
#include "common/inc/nvFileIO.h"
#include "common/inc/helper_string.h"
#include "dynlink_builtin_types.h"

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>

#include <stdint.h>
#include <stddef.h>
#include <sys/mman.h>
#include <unistd.h>

#include <xf86drmMode.h>
#include <xf86drm.h>

#define EGL_EGLEXT_PROTOTYPES 1     //for eglCreateImageKHR()
#define GL_GLEXT_PROTOTYPES 1       //for glEGLImageTargetTexture2DOES()
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>

#if defined(__x86_64) || defined(AMD64) || defined(_M_AMD64) || defined(__aarch64__)
#define PTX_FILE "preproc64_cuda.ptx"
#else//32bit mode
#ifdef READ_RGB_FILE
#define PTX_FILE "rgb2nv12.ptx"
#else
#define PTX_FILE "argb2nv12.ptx"
#endif
//#define PTX_FILE "preproc32_cuda.ptx"
#endif
using namespace std;

#define __cu(a) do { CUresult  ret; if ((ret = (a)) != CUDA_SUCCESS) { fprintf(stderr, "%s has returned CUDA error %d\n", #a, ret); return NV_ENC_ERR_GENERIC;}} while(0)

#define BITSTREAM_BUFFER_SIZE 2*1024*1024

CNvEncoderCudaInterop::CNvEncoderCudaInterop()
{
    m_pNvHWEncoder = new CNvHWEncoder;

    m_cuContext = NULL;
    m_cuModule  = NULL;
    m_cuRGB2NV12Function = NULL;

    m_uEncodeBufferCount = 0;
    memset(&m_stEncoderInput, 0, sizeof(m_stEncoderInput));
    memset(&m_stEOSOutputBfr, 0, sizeof(m_stEOSOutputBfr));

    memset(&m_stEncodeBuffer, 0, sizeof(m_stEncodeBuffer));
    memset(&m_RGBDevPtr, 0, sizeof(m_RGBDevPtr));
    memset(&m_RGBArrayPtr, 0, sizeof(m_RGBArrayPtr));
}

CNvEncoderCudaInterop::~CNvEncoderCudaInterop()
{
    if (m_pNvHWEncoder)
    {
        delete m_pNvHWEncoder;
        m_pNvHWEncoder = NULL;
    }
}

#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "cds_server_render_nvidia.h"

#if !defined(EGL_DRM_MASTER_FD_EXT)
#define EGL_DRM_MASTER_FD_EXT 0x333C
#endif

CDSRenderNvidia::CDSRenderNvidia() {
}

CDSRenderNvidia::CDSRenderNvidia(TEX &tex_info, int prime_fd, int width, int height) {
  tex = tex_info;
  prime_fd_ = prime_fd;
  width_ = width;
  height_ = height;
}

CDSRenderNvidia::~CDSRenderNvidia() {
}

void CDSRenderNvidia::Fatal(const char *format, ...)
{
  va_list ap;

  fprintf(stderr, "ERROR: ");

  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);

  exit(1);
}

/* EGL and GL Functions */
void* CDSRenderNvidia::GetProcAddress(const char *functionName)
{
  void *ptr = (void *) eglGetProcAddress(functionName);

  if (ptr == NULL) {
    Fatal("eglGetProcAddress(%s) failed.\n", functionName);
  }

  return ptr;
}

void CDSRenderNvidia::GetEglExtensionFunctionPointers(void) {
	eglQueryDevicesEXT = (PFNEGLQUERYDEVICESEXTPROC) GetProcAddress("eglQueryDevicesEXT");
	eglQueryDeviceStringEXT = (PFNEGLQUERYDEVICESTRINGEXTPROC) GetProcAddress("eglQueryDeviceStringEXT");
	eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC) GetProcAddress("eglGetPlatformDisplayEXT");
}

void CDSRenderNvidia::GetGlExtensionFunctionPointers(void)
{
	// glGetAttribLocation = (PFNGLGETATTRIBLOCATIONPROC)GetProcAddress("glGetAttribLocation");
}

EGLBoolean CDSRenderNvidia::ExtensionIsSupported(const char *extensionString, const char *ext)
{
  const char *endOfExtensionString;
  const char *currentExtension = extensionString;
  size_t extensionLength;

  if ((extensionString == NULL) || (ext == NULL)) {
    return EGL_FALSE;
  }

  extensionLength = strlen(ext);

  endOfExtensionString = extensionString + strlen(extensionString);

  while (currentExtension < endOfExtensionString) {
    const size_t currentExtensionLength = strcspn(currentExtension, " ");
    if ((extensionLength == currentExtensionLength) &&
        (strncmp(ext, currentExtension,
                 extensionLength) == 0)) {
      return EGL_TRUE;
    }
    currentExtension += (currentExtensionLength + 1);
  }
  return EGL_FALSE;
}

/* Core Functions */
EGLDeviceEXT  CDSRenderNvidia::getEglDevice(void)
{
    EGLint numDevices, i;
    EGLDeviceEXT *devices = NULL;
    EGLDeviceEXT device = EGL_NO_DEVICE_EXT;
    EGLBoolean ret;

    const char *clientExtensionString =
        eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);

    if (!ExtensionIsSupported(clientExtensionString, "EGL_EXT_device_base") &&
     		(!ExtensionIsSupported(clientExtensionString, "EGL_EXT_device_enumeration") ||
         !ExtensionIsSupported(clientExtensionString, "EGL_EXT_device_query"))) {
        Fatal("EGL_EXT_device base extensions not found.\n");
    }

    ret = eglQueryDevicesEXT(0, NULL, &numDevices);
    if (!ret) {
        Fatal("Failed to query EGL devices.\n");
    }
    if (numDevices < 1) {
        Fatal("No EGL devices found.\n");
    }

    devices = (void**)calloc(numDevices, sizeof(EGLDeviceEXT));
    if (devices == NULL) {
        Fatal("Memory allocation failure.\n");
    }

    ret = eglQueryDevicesEXT(numDevices, devices, &numDevices);
    if (!ret) {
        Fatal("Failed to query EGL devices.\n");
    }

    for (i = 0; i < numDevices; i++) {
        const char *deviceExtensionString = eglQueryDeviceStringEXT(devices[i], EGL_EXTENSIONS);

        if (ExtensionIsSupported(deviceExtensionString, "EGL_EXT_device_drm")) {
        // if (ExtensionIsSupported(deviceExtensionString, "EGL_NV_device_cuda")) {
            device = devices[i];
            break;
        }
    }
    free(devices);

    if (device == EGL_NO_DEVICE_EXT) {
        Fatal("No EGL_EXT_device_drm-capable EGL device found.\n");
    }

    return device;
}

int CDSRenderNvidia::getDrmFd(EGLDeviceEXT device)
{
    const char *drmDeviceFile;
    int fd;

    drmDeviceFile = eglQueryDeviceStringEXT(device, EGL_DRM_DEVICE_FILE_EXT);
    // drmDeviceFile = eglQueryDeviceStringEXT(device, EGL_CUDA_DEVICE_NV);

    if (drmDeviceFile == NULL) {
        Fatal("No DRM device file found for EGL device.\n");
    }

    fd = open(drmDeviceFile, O_RDWR, 0);

    if (fd < 0) {
        Fatal("Unable to open DRM device file.\n");
    }

    return fd;
}

int CDSRenderNvidia::initEgl() {
  GetEglExtensionFunctionPointers();
  GetGlExtensionFunctionPointers();

  EGLDeviceEXT device = getEglDevice();
  int drmFd = getDrmFd(device);

	EGLint disp_attribs[] = {
		EGL_DRM_MASTER_FD_EXT,
		drmFd,
		EGL_NONE
	};

  EGLint config_attribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 0,
    EGL_NONE,
  };

  EGLint ctx_attribs[] = { EGL_NONE };

  EGLint stream_attribs[] = {
    EGL_NONE
  };

  EGLint surf_attribs[] = {
    EGL_WIDTH, width_,
    EGL_HEIGHT, height_,
    EGL_NONE
  };

  EGLBoolean ret;
  EGLint num_config;
  EGLConfig egl_config;

	egl.disp = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, (void*)device, disp_attribs);
	if (egl.disp == EGL_NO_DISPLAY) {
		Fatal("Failed to get EGLDisplay from EGLDevice.");
	}

	if (!eglInitialize(egl.disp, NULL, NULL)) {
		Fatal("Failed to initialize EGLDisplay.");
	}

  const char *extensionString = eglQueryString(egl.disp, EGL_EXTENSIONS);

  eglBindAPI(EGL_OPENGL_API);

  ret = eglChooseConfig(egl.disp, config_attribs, &egl_config, 1, &num_config);
  if (!ret || !num_config) {
    Fatal("eglChooseConfig() failed.\n");
  }

  egl.ctx = eglCreateContext(egl.disp, egl_config, EGL_NO_CONTEXT, ctx_attribs);
  if (egl.ctx == NULL) {
    Fatal("eglCreateContext() failed.\n");
  }

  egl.surf = eglCreatePbufferSurface(egl.disp, egl_config, surf_attribs);

  ret = eglMakeCurrent(egl.disp, egl.surf, egl.surf, egl.ctx);
  if (!ret) {
    Fatal("Unable to make context and surface current.\n");
  }

  // eglTerminate(egl.disp);

  return ret;
}

void CDSRenderNvidia::runEncoder() {

}

void CDSRenderNvidia::convertRGBToYUV() {

}

void CDSRenderNvidia::encodeYUVToH264() {

}

unsigned char* CDSRenderNvidia::getEncodedData() {
  return 0;
}

unsigned int CDSRenderNvidia::getEncoderdSize() {
  return 0;
}


NVENCSTATUS CNvEncoderCudaInterop::InitCuda(unsigned int deviceID, const char *exec_path)
{
    CUresult        cuResult            = CUDA_SUCCESS;
    CUdevice        cuDevice            = 0;
    CUcontext       cuContextCurr;
    int  deviceCount = 0;
    int  SMminor = 0, SMmajor = 0;

#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
    typedef HMODULE CUDADRIVER;
#else
    typedef void *CUDADRIVER;
#endif
    CUDADRIVER hHandleDriver = 0;

    // CUDA interfaces
    __cu(cuInit(0, __CUDA_API_VERSION, hHandleDriver));
    
    __cu(cuDeviceGetCount(&deviceCount));
    if (deviceCount == 0)
    {
        return NV_ENC_ERR_NO_ENCODE_DEVICE;
    }

    if (deviceID > (unsigned int)deviceCount-1)
    {
        PRINTERR("Invalid Device Id = %d\n", deviceID);
        return NV_ENC_ERR_INVALID_ENCODERDEVICE;
    }

    // Now we get the actual device
    __cu(cuDeviceGet(&cuDevice, deviceID));

    __cu(cuDeviceComputeCapability(&SMmajor, &SMminor, deviceID));
    if (((SMmajor << 4) + SMminor) < 0x30)
    {
        PRINTERR("GPU %d does not have NVENC capabilities exiting\n", deviceID);
        return NV_ENC_ERR_NO_ENCODE_DEVICE;
    }

    // Create the CUDA Context and Pop the current one
    __cu(cuCtxCreate(&m_cuContext, 0, cuDevice));

    // in this branch we use compilation with parameters
    const unsigned int jitNumOptions = 3;
    CUjit_option *jitOptions = new CUjit_option[jitNumOptions];
    void **jitOptVals = new void *[jitNumOptions];

    // set up size of compilation log buffer
    jitOptions[0] = CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES;
    int jitLogBufferSize = 1024;
    jitOptVals[0] = (void *)(size_t)jitLogBufferSize;

    // set up pointer to the compilation log buffer
    jitOptions[1] = CU_JIT_INFO_LOG_BUFFER;
    char *jitLogBuffer = new char[jitLogBufferSize];
    jitOptVals[1] = jitLogBuffer;

    // set up pointer to set the Maximum # of registers for a particular kernel
    jitOptions[2] = CU_JIT_MAX_REGISTERS;
    int jitRegCount = 32;
    jitOptVals[2] = (void *)(size_t)jitRegCount;

    string ptx_source;
    char *ptx_path = "./data/argb2nv12.ptx";//sdkFindFilePath(PTX_FILE, exec_path);
    if (ptx_path == NULL) {
        PRINTERR("Unable to find ptx file path %s\n", PTX_FILE);
        return NV_ENC_ERR_INVALID_PARAM;
    }
    
    FILE *fp = fopen(ptx_path, "rb");
    if (!fp)
    {
        PRINTERR("Unable to read ptx file %s\n", PTX_FILE);
        return NV_ENC_ERR_INVALID_PARAM;
    }
    fseek(fp, 0, SEEK_END);
    int file_size = ftell(fp);
    char *buf = new char[file_size + 1];
    fseek(fp, 0, SEEK_SET);
    fread(buf, sizeof(char), file_size, fp);
    fclose(fp);
    buf[file_size] = '\0';
    ptx_source = buf;
    delete[] buf;

    cuResult = cuModuleLoadDataEx(&m_cuModule, ptx_source.c_str(), jitNumOptions, jitOptions, (void **)jitOptVals);
    if (cuResult != CUDA_SUCCESS)
    {
        return NV_ENC_ERR_OUT_OF_MEMORY;
    }

    delete[] jitOptions;
    delete[] jitOptVals;
    delete[] jitLogBuffer;

    __cu(cuModuleGetFunction(&m_cuRGB2NV12Function, m_cuModule, "RGB2NV12"));

    __cu(cuCtxPopCurrent(&cuContextCurr));
    return NV_ENC_SUCCESS;
}

NVENCSTATUS CNvEncoderCudaInterop::AllocateIOBuffers(uint32_t uInputWidth, uint32_t uInputHeight)
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;

    m_EncodeBufferQueue.Initialize(m_stEncodeBuffer, m_uEncodeBufferCount);

    CCudaAutoLock cuLock(m_cuContext);

#ifdef READ_RGB_FILE
#ifdef ENCODE_RGB
    __cu(cuMemAllocHost((void **)&m_rgb, uInputWidth*uInputHeight*4));
#else
    __cu(cuMemAllocHost((void **)&m_rgb, uInputWidth*uInputHeight*3));
#endif
    __cu(cuMemAlloc(&m_RGBDevPtr, uInputWidth*uInputHeight*3));
#else
    // CUDA_ARRAY_DESCRIPTOR createParam;
    // createParam.Width = uInputWidth;
    // createParam.Height = uInputHeight;
    // createParam.Format = CU_AD_FORMAT_UNSIGNED_INT32;
	// createParam.NumChannels = 4;
	// __cu(cuArrayCreate(&m_RGBArrayPtr, &createParam));
	__cu(cuMemAlloc(&m_RGBDevPtr, uInputWidth*uInputHeight*4));
#endif

    for (uint32_t i = 0; i < m_uEncodeBufferCount; i++)
    {
#ifdef ENCODE_RGB
        __cu(cuMemAllocPitch(&m_stEncodeBuffer[i].stInputBfr.pNV12devPtr, (size_t *)&m_stEncodeBuffer[i].stInputBfr.uNV12Stride, uInputWidth * 4, uInputHeight , 16));
        printf("Allocated memory pitch = %d\n", m_stEncodeBuffer[i].stInputBfr.uNV12Stride);
        nvStatus = m_pNvHWEncoder->NvEncRegisterResource(NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR, (void*)m_stEncodeBuffer[i].stInputBfr.pNV12devPtr, uInputWidth, uInputHeight, m_stEncodeBuffer[i].stInputBfr.uNV12Stride, &m_stEncodeBuffer[i].stInputBfr.nvRegisteredResource,NV_ENC_BUFFER_FORMAT_ARGB );
        if (nvStatus != NV_ENC_SUCCESS)
            return nvStatus;
        m_stEncodeBuffer[i].stInputBfr.bufferFmt = NV_ENC_BUFFER_FORMAT_ARGB;
#else
        __cu(cuMemAllocPitch(&m_stEncodeBuffer[i].stInputBfr.pNV12devPtr, (size_t *)&m_stEncodeBuffer[i].stInputBfr.uNV12Stride, uInputWidth, uInputHeight * 3 / 2, 16));

        printf("Allocated memory pitch = %d\n", m_stEncodeBuffer[i].stInputBfr.uNV12Stride);
        nvStatus = m_pNvHWEncoder->NvEncRegisterResource(NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR, (void*)m_stEncodeBuffer[i].stInputBfr.pNV12devPtr, uInputWidth, uInputHeight, m_stEncodeBuffer[i].stInputBfr.uNV12Stride, &m_stEncodeBuffer[i].stInputBfr.nvRegisteredResource);
        if (nvStatus != NV_ENC_SUCCESS)
            return nvStatus;
        m_stEncodeBuffer[i].stInputBfr.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12_PL;
#endif
        m_stEncodeBuffer[i].stInputBfr.dwWidth = uInputWidth;
        m_stEncodeBuffer[i].stInputBfr.dwHeight = uInputHeight;

        nvStatus = m_pNvHWEncoder->NvEncCreateBitstreamBuffer(BITSTREAM_BUFFER_SIZE, &m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer);
        if (nvStatus != NV_ENC_SUCCESS)
            return nvStatus;
        m_stEncodeBuffer[i].stOutputBfr.dwBitstreamBufferSize = BITSTREAM_BUFFER_SIZE;

        if (m_stEncoderInput.enableAsyncMode)
        {
            nvStatus = m_pNvHWEncoder->NvEncRegisterAsyncEvent(&m_stEncodeBuffer[i].stOutputBfr.hOutputEvent);
            if (nvStatus != NV_ENC_SUCCESS)
                return nvStatus;
            m_stEncodeBuffer[i].stOutputBfr.bWaitOnEvent = true;
        }
        else
            m_stEncodeBuffer[i].stOutputBfr.hOutputEvent = NULL;
    }

    m_stEOSOutputBfr.bEOSFlag = TRUE;
    if (m_stEncoderInput.enableAsyncMode)
    {
        nvStatus = m_pNvHWEncoder->NvEncRegisterAsyncEvent(&m_stEOSOutputBfr.hOutputEvent);
        if (nvStatus != NV_ENC_SUCCESS)
            return nvStatus;
    }
    else
        m_stEOSOutputBfr.hOutputEvent = NULL;

    return NV_ENC_SUCCESS;
}

NVENCSTATUS CNvEncoderCudaInterop::ReleaseIOBuffers()
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;

    CCudaAutoLock cuLock(m_cuContext);
        if (m_RGBDevPtr)
        {
            cuMemFree(m_RGBDevPtr);
        }
#ifdef READ_RGB_FILE
        if (m_rgb)
        {
            cuMemFreeHost(m_rgb);
        }
#endif
    for (uint32_t i = 0; i < m_uEncodeBufferCount; i++)
    {
        nvStatus = m_pNvHWEncoder->NvEncUnregisterResource(m_stEncodeBuffer[i].stInputBfr.nvRegisteredResource);
        if (nvStatus != NV_ENC_SUCCESS)
            return nvStatus;

        cuMemFree(m_stEncodeBuffer[i].stInputBfr.pNV12devPtr);

        m_pNvHWEncoder->NvEncDestroyBitstreamBuffer(m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer);
        m_stEncodeBuffer[i].stOutputBfr.hBitstreamBuffer = NULL;

        if (m_stEncoderInput.enableAsyncMode)
        {
            m_pNvHWEncoder->NvEncUnregisterAsyncEvent(m_stEncodeBuffer[i].stOutputBfr.hOutputEvent);
            nvCloseFile(m_stEncodeBuffer[i].stOutputBfr.hOutputEvent);
            m_stEncodeBuffer[i].stOutputBfr.hOutputEvent = NULL;
        }
    }

    if (m_stEOSOutputBfr.hOutputEvent)
    {
        if (m_stEncoderInput.enableAsyncMode)
        {
            m_pNvHWEncoder->NvEncUnregisterAsyncEvent(m_stEOSOutputBfr.hOutputEvent);
            nvCloseFile(m_stEOSOutputBfr.hOutputEvent);
            m_stEOSOutputBfr.hOutputEvent = NULL;
        }
    }

    return NV_ENC_SUCCESS;
}

NVENCSTATUS CNvEncoderCudaInterop::FlushEncoder()
{
    NVENCSTATUS nvStatus = m_pNvHWEncoder->NvEncFlushEncoderQueue(m_stEOSOutputBfr.hOutputEvent);
    if(nvStatus != NV_ENC_SUCCESS)
    {
        assert(0);
        return nvStatus;
    }

    EncodeBuffer *pEncodeBuffer = m_EncodeBufferQueue.GetPending();
    while(pEncodeBuffer)
    {
        m_pNvHWEncoder->ProcessOutput(pEncodeBuffer);
        // UnMap the input buffer after frame is done
        if (pEncodeBuffer->stInputBfr.hInputSurface)
        {
            nvStatus = m_pNvHWEncoder->NvEncUnmapInputResource(pEncodeBuffer->stInputBfr.hInputSurface);
            pEncodeBuffer->stInputBfr.hInputSurface = NULL;
        }
        pEncodeBuffer = m_EncodeBufferQueue.GetPending();
    }
#if defined(NV_WINDOWS)
    if (m_stEncoderInput.enableAsyncMode)
    {
        if (WaitForSingleObject(m_stEOSOutputBfr.hOutputEvent, 500) != WAIT_OBJECT_0)
        {
            assert(0);
            nvStatus = NV_ENC_ERR_GENERIC;
        }
    }
#endif
    return nvStatus;
}

NVENCSTATUS CNvEncoderCudaInterop::Deinitialize()
{
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;

    ReleaseIOBuffers();

    nvStatus = m_pNvHWEncoder->NvEncDestroyEncoder();
    if (nvStatus != NV_ENC_SUCCESS)
    {
        assert(0);
    }

    __cu(cuCtxDestroy(m_cuContext));

    return NV_ENC_SUCCESS;
}

NVENCSTATUS CNvEncoderCudaInterop::ConvertRGBToNV12(EncodeBuffer *pEncodeBuffer, unsigned char *rgb, int width, int height)
{
    CCudaAutoLock cuLock(m_cuContext);
#ifdef READ_RGB_FILE
	__cu(cuMemcpyHtoD(m_RGBDevPtr, rgb, width * height * 3));//copy RGB
#endif
#define BLOCK_X 16
#define BLOCK_Y 32
    int rgbHeight = height / 2;
    int rgbWidth = width / 2 ;
#ifdef READ_RGB_FILE
    int rgbPitch = width * 3;
#else
    int rgbPitch = width * 4;
#endif
    dim3 block(BLOCK_X, BLOCK_Y, 1);
    dim3 grid((rgbWidth + BLOCK_X - 1) / BLOCK_X, (rgbHeight + BLOCK_Y - 1) / BLOCK_Y, 1);
#undef BLOCK_Y
#undef BLOCK_X

	CUdeviceptr dNV12 = pEncodeBuffer->stInputBfr.pNV12devPtr;
void *args[6] = { &m_RGBDevPtr, &dNV12, &rgbWidth, &rgbHeight, &rgbPitch, &pEncodeBuffer->stInputBfr.uNV12Stride };
	__cu(cuLaunchKernel(m_cuRGB2NV12Function, grid.x, grid.y, grid.z,
				block.x, block.y, block.z,
				0,
				NULL, args, NULL));
	CUresult cuResult = cuStreamQuery(NULL);
	if (!((cuResult == CUDA_SUCCESS) || (cuResult == CUDA_ERROR_NOT_READY)))
	{
		return NV_ENC_ERR_GENERIC;
	}

    return NV_ENC_SUCCESS;
}

NVENCSTATUS CNvEncoderCudaInterop::LoadRGB(EncodeBuffer *pEncodeBuffer, unsigned char *rgb, int width, int height){

    CCudaAutoLock cuLock(m_cuContext);
    __cu(cuMemcpyHtoD(pEncodeBuffer->stInputBfr.pNV12devPtr, rgb, width * height * 4));//copy ARGB

    return NV_ENC_SUCCESS;
}

NVENCSTATUS loadRGBframe(unsigned char *rgbInput, HANDLE hInputRGBFile, uint32_t frmIdx, uint32_t width, uint32_t height, uint32_t &numBytesRead)
{
    uint64_t fileOffset;
    uint32_t result;
#ifdef ENCODE_RGB
    uint32_t dwInFrameSize = width * height * 4;
#else
    uint32_t dwInFrameSize = width * height * 3;
#endif
    fileOffset = (uint64_t)dwInFrameSize * frmIdx;
    result = nvSetFilePointer64(hInputRGBFile, fileOffset, NULL, FILE_BEGIN);
    if (result == INVALID_SET_FILE_POINTER)
    {
        return NV_ENC_ERR_INVALID_PARAM;

    }
#ifdef ENCODE_RGB
    unsigned char* input;

    input = (unsigned char*)malloc(sizeof(unsigned char)* width * height * 3);
    nvReadFile(hInputRGBFile, input, dwInFrameSize, &numBytesRead, NULL);//RGB
    for(int i = 0; i < height; i++){
        for(int j = 0; j < width; j++ ){
            rgbInput[i*width*4 + j*4] = input[i*width*3 + j * 3 +2];
            rgbInput[i*width*4 + j*4 + 1] = input[i*width*3 + j*3 + 1];
            rgbInput[i*width*4 + j*4 + 2] = input[i*width*3 + j*3];
            rgbInput[i*width*4 + j*4 + 3] = 255;
        }
    }
    if (input)
    {
        free(input);

    }
#else
    nvReadFile(hInputRGBFile, rgbInput, dwInFrameSize, &numBytesRead, NULL);//RGB
#endif
    return NV_ENC_SUCCESS;
}

NVENCSTATUS loadData(unsigned char *yuvInput, unsigned char *inputData, uint32_t width, uint32_t height, uint32_t &numBytesRead)
{
    memcpy(yuvInput + width*height, inputData, width*height/4);
    memcpy(yuvInput + width*height + width*height/4, inputData + width*height/4 , width*height/4);
    memcpy(yuvInput , inputData +  width*height/2, width*height);
    return NV_ENC_SUCCESS;
}


NVENCSTATUS CNvEncoderCudaInterop::MapGLTexToCU(CUdeviceptr* cudevicePtr, GLuint colorTex, int width, int height){
    CCudaAutoLock cuLock(m_cuContext);
    __cu(cuGraphicsGLRegisterImage(&m_graphicsResource, colorTex, GL_TEXTURE_2D, CU_GRAPHICS_MAP_RESOURCE_FLAGS_READ_ONLY));
    __cu(cuGraphicsMapResources(1, &m_graphicsResource, 0));

    __cu(cuGraphicsSubResourceGetMappedArray( &m_RGBArrayPtr, m_graphicsResource, 0, 0 ));


    CopyArrayToDevice(cudevicePtr, width, height);
	__cu(cuGraphicsUnmapResources(1, &m_graphicsResource, 0));//deinitialize
#if 0
    uint8_t* Buffer;
    __cu(cuMemAllocHost((void **)&Buffer, width * height * 4));

    CUDA_MEMCPY2D cpyParam;
    memset(&cpyParam, 0, sizeof(cpyParam));
    cpyParam.srcMemoryType = CU_MEMORYTYPE_ARRAY;
    cpyParam.srcArray = m_RGBArrayPtr;
    // cpyParam.srcPitch = width * 4;
    cpyParam.dstMemoryType = CU_MEMORYTYPE_HOST;
    cpyParam.dstHost = (void*)Buffer;
    cpyParam.dstPitch = width * 4;
    cpyParam.WidthInBytes = width * 4;
    cpyParam.Height = height;
    __cu(cuMemcpy2D(&cpyParam));

    size_t bufferSize = width * height * 4;
   std::ofstream outFile;
    outFile.open("cuda_texture.ppm", std::ios::binary);
    outFile << "P6" << "\n"
        << width << " " << height << "\n"
        << "255\n";
    outFile.write((char*)Buffer, bufferSize);
    cuMemFreeHost(Buffer);
#endif
return NV_ENC_SUCCESS;
}

NVENCSTATUS CNvEncoderCudaInterop::CopyArrayToDevice(CUdeviceptr* cudevicePtr, int width, int height){
    CCudaAutoLock cuLock(m_cuContext);
    __cu(cuGraphicsSubResourceGetMappedArray( &m_RGBArrayPtr, m_graphicsResource, 0, 0 ));

	CUDA_MEMCPY2D cpyParam;
    memset(&cpyParam, 0, sizeof(cpyParam));
	cpyParam.srcMemoryType = CU_MEMORYTYPE_ARRAY;
	cpyParam.srcArray = m_RGBArrayPtr;
	cpyParam.srcXInBytes = 0;
	cpyParam.srcY = 0;
	//cpyParam.srcPitch = width * 4;
	cpyParam.dstMemoryType = CU_MEMORYTYPE_DEVICE;
	cpyParam.dstDevice = *cudevicePtr;
	cpyParam.dstPitch = width * 4;
	cpyParam.dstXInBytes = 0;
	cpyParam.dstY = 0;
	cpyParam.WidthInBytes = width * 4; 
	cpyParam.Height = height;
	__cu(cuMemcpy2DUnaligned(&cpyParam));
    
    return NV_ENC_SUCCESS;
}

void PrintHelp()
{
    printf("Usage : NvEncoderCudaInterop \n"
            "-i <string>                  Specify input yuv420 file\n"
        "-o <string>                  Specify output bitstream file\n"
        "-size <int int>              Specify input resolution <width height>\n"
        "\n### Optional parameters ###\n"
        "-startf <integer>            Specify start index for encoding. Default is 0\n"
        "-endf <integer>              Specify end index for encoding. Default is end of file\n"
        "-codec <integer>             Specify the codec \n"
        "                                 0: H264\n"
        "                                 1: HEVC\n"
        "-preset <string>             Specify the preset for encoder settings\n"
        "                                 hq : nvenc HQ \n"
        "                                 hp : nvenc HP \n"
        "                                 lowLatencyHP : nvenc low latency HP \n"
        "                                 lowLatencyHQ : nvenc low latency HQ \n"
        "                                 lossless : nvenc Lossless HP \n"
        "-fps <integer>               Specify encoding frame rate\n"
        "-goplength <integer>         Specify gop length\n"
        "-numB <integer>              Specify number of B frames\n"
        "-bitrate <integer>           Specify the encoding average bitrate\n"
        "-vbvMaxBitrate <integer>     Specify the vbv max bitrate\n"
        "-vbvSize <integer>           Specify the encoding vbv/hrd buffer size\n"
        "-rcmode <integer>            Specify the rate control mode\n"
        "                                 0:  Constant QP mode\n"
        "                                 1:  Variable bitrate mode\n"
        "                                 2:  Constant bitrate mode\n"
        "                                 8:  low-delay CBR, high quality\n"
        "                                 16: CBR, high quality (slower)\n"
        "                                 32: VBR, high quality (slower)\n"
        "-qp <integer>                Specify qp for Constant QP mode\n"
        "-i_qfactor <float>           Specify qscale difference between I-frames and P-frames\n"
        "-b_qfactor <float>           Specify qscale difference between P-frames and B-frames\n" 
        "-i_qoffset <float>           Specify qscale offset between I-frames and P-frames\n"
        "-b_qoffset <float>           Specify qscale offset between P-frames and B-frames\n" 
        "-deviceID <integer>          Specify the GPU device on which encoding will take place\n"
        "-help                        Prints Help Information\n\n"
        );
}

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <ctype.h>
#include <iostream>
#include <fstream>

void captureFramebuffer(GLuint framebuffer, uint32_t width, uint32_t height, const std::string& path)
{
  size_t numBytes = width * height * 3;
  uint8_t* rgb = new uint8_t[numBytes];

  glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb);

  std::ofstream outFile;
  outFile.open(path.c_str(), std::ios::binary);

  outFile << "P6" << "\n"
    << width << " " << height << "\n"
    << "255\n";

  outFile.write((char*) rgb, numBytes);

  delete[] rgb;
}

int CNvEncoderCudaInterop::EncodeMain(int argc, char *argv[])
{
    HANDLE hInput;
    uint32_t numBytesRead = 0;
    unsigned long long lStart, lEnd, lFreq;
    int numFramesEncoded = 0;
    NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    bool bError = false;
    EncodeBuffer *pEncodeBuffer;
    EncodeConfig encodeConfig;

    //// 0. Save argv infomation into encodeConfig
    memset(&encodeConfig, 0, sizeof(EncodeConfig));
    encodeConfig.endFrameIdx = INT_MAX;
    encodeConfig.bitrate = 5000000;
    encodeConfig.rcMode = NV_ENC_PARAMS_RC_CONSTQP;
    encodeConfig.gopLength = NVENC_INFINITE_GOPLENGTH;
    encodeConfig.deviceType = NV_ENC_CUDA;
    encodeConfig.codec = NV_ENC_H264;
    encodeConfig.fps = 30;
    encodeConfig.qp = 28;
    encodeConfig.i_quant_factor = DEFAULT_I_QFACTOR;
    encodeConfig.b_quant_factor = DEFAULT_B_QFACTOR;  
    encodeConfig.i_quant_offset = DEFAULT_I_QOFFSET;
    encodeConfig.b_quant_offset = DEFAULT_B_QOFFSET; 
    encodeConfig.presetGUID = NV_ENC_PRESET_DEFAULT_GUID;
    encodeConfig.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;

    nvStatus = m_pNvHWEncoder->ParseArguments(&encodeConfig, argc, argv);
    if (nvStatus != NV_ENC_SUCCESS)
    {
        PrintHelp();
        return 1;
    }
    if (!encodeConfig.inputFileName || !encodeConfig.outputFileName || encodeConfig.width == 0 || encodeConfig.height == 0)
    {
        PrintHelp();
        return 1;
    }

    encodeConfig.fOutput = fopen(encodeConfig.outputFileName, "wb");
    if (encodeConfig.fOutput == NULL)
    {
        PRINTERR("Failed to create \"%s\"\n", encodeConfig.outputFileName);
        return 1;
    }

    //input file open
    hInput = nvOpenFile(encodeConfig.inputFileName);
    if (hInput == INVALID_HANDLE_VALUE)
    {
        PRINTERR("Failed to open \"%s\"\n", encodeConfig.inputFileName);
        return 1;
    }

    nvStatus = InitCuda(encodeConfig.deviceID, argv[0]);
    if (nvStatus != NV_ENC_SUCCESS)
        return nvStatus;

    nvStatus = m_pNvHWEncoder->Initialize((void*)m_cuContext, NV_ENC_DEVICE_TYPE_CUDA);
    if (nvStatus != NV_ENC_SUCCESS)
        return 1;

    encodeConfig.presetGUID = m_pNvHWEncoder->GetPresetGUID(encodeConfig.encoderPreset, encodeConfig.codec);

    printf("Encoding input           : \"%s\"\n", encodeConfig.inputFileName);
    printf("         output          : \"%s\"\n", encodeConfig.outputFileName);
    printf("         codec           : \"%s\"\n", encodeConfig.codec == NV_ENC_HEVC ? "HEVC" : "H264");
    printf("         size            : %dx%d\n", encodeConfig.width, encodeConfig.height);
    printf("         bitrate         : %d bits/sec\n", encodeConfig.bitrate);
    printf("         vbvMaxBitrate   : %d bits/sec\n", encodeConfig.vbvMaxBitrate);
    printf("         vbvSize         : %d bits\n", encodeConfig.vbvSize);
    printf("         fps             : %d frames/sec\n", encodeConfig.fps);
    printf("         rcMode          : %s\n", encodeConfig.rcMode == NV_ENC_PARAMS_RC_CONSTQP ? "CONSTQP" :
                                              encodeConfig.rcMode == NV_ENC_PARAMS_RC_VBR ? "VBR" :
                                              encodeConfig.rcMode == NV_ENC_PARAMS_RC_CBR ? "CBR" :
                                              encodeConfig.rcMode == NV_ENC_PARAMS_RC_VBR_MINQP ? "VBR MINQP (deprecated)" :
                                              encodeConfig.rcMode == NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ ? "CBR_LOWDELAY_HQ" :
                                              encodeConfig.rcMode == NV_ENC_PARAMS_RC_CBR_HQ ? "CBR_HQ" :
                                              encodeConfig.rcMode == NV_ENC_PARAMS_RC_VBR_HQ ? "VBR_HQ" : "UNKNOWN");
    if (encodeConfig.gopLength == NVENC_INFINITE_GOPLENGTH)
        printf("         goplength       : INFINITE GOP \n");
    else
        printf("         goplength       : %d \n", encodeConfig.gopLength);
    printf("         B frames        : %d \n", encodeConfig.numB);
    printf("         QP              : %d \n", encodeConfig.qp);
    printf("         preset          : %s\n", (encodeConfig.presetGUID == NV_ENC_PRESET_LOW_LATENCY_HQ_GUID) ? "LOW_LATENCY_HQ" :
        (encodeConfig.presetGUID == NV_ENC_PRESET_LOW_LATENCY_HP_GUID) ? "LOW_LATENCY_HP" :
        (encodeConfig.presetGUID == NV_ENC_PRESET_HQ_GUID) ? "HQ_PRESET" :
        (encodeConfig.presetGUID == NV_ENC_PRESET_HP_GUID) ? "HP_PRESET" :
        (encodeConfig.presetGUID == NV_ENC_PRESET_LOSSLESS_HP_GUID) ? "LOSSLESS_HP" :
        (encodeConfig.presetGUID == NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID) ? "LOW_LATENCY_DEFAULT" : "DEFAULT");
    printf("\n");

    nvStatus = m_pNvHWEncoder->CreateEncoder(&encodeConfig);
    if (nvStatus != NV_ENC_SUCCESS)
        return 1;

    m_uEncodeBufferCount = encodeConfig.numB + 4;
    m_stEncoderInput.enableAsyncMode = encodeConfig.enableAsyncMode;

    nvStatus = AllocateIOBuffers(encodeConfig.width, encodeConfig.height);
    if (nvStatus != NV_ENC_SUCCESS)
        return 1;

    NvQueryPerformanceCounter(&lStart);

#ifndef READ_RGB_FILE
    TEX texInfo;
    int primeFd = 0;
    int width = 1920;
    int height = 1080;

    CDSRenderNvidia cds_render(texInfo, primeFd, width, height);
    cds_render.initEgl();
    
    GLuint colorTex;
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    
    GLuint offsreen_fbo;
    glGenFramebuffers(1, &offsreen_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, offsreen_fbo);
    glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, colorTex, 0);

    glClearColor(0.0f, 0.0f, 0.5f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, -0.5f, 0.0f);
    glVertex3f(-0.5f, 0.5f, 0.0f);
    glVertex3f(0.5f, 0.5f, 0.0f);
    glEnd();

    // captureFramebuffer(offsreen_fbo, width, height, "gl_texture.ppm");
    glBindFramebuffer(GL_READ_FRAMEBUFFER, offsreen_fbo);
#ifdef ENCODE_RGB
#else
    MapGLTexToCU(&m_RGBDevPtr, colorTex, width, height);
#endif
#endif

    numBytesRead = 0;
    //for (int frm = encodeConfig.startFrameIdx; frm <= encodeConfig.endFrameIdx; frm++)
    // loadRGBframe(m_rgb, hInput, 0, encodeConfig.width, encodeConfig.height, numBytesRead);
    for (int frm = 0; frm <= 2; frm++)
    {

        pEncodeBuffer = m_EncodeBufferQueue.GetAvailable();
        if(!pEncodeBuffer)
        {
            pEncodeBuffer = m_EncodeBufferQueue.GetPending();
            m_pNvHWEncoder->ProcessOutput(pEncodeBuffer);
            // UnMap the input buffer after frame done
            if (pEncodeBuffer->stInputBfr.hInputSurface)
            {
                nvStatus = m_pNvHWEncoder->NvEncUnmapInputResource(pEncodeBuffer->stInputBfr.hInputSurface);
                pEncodeBuffer->stInputBfr.hInputSurface = NULL;
            }
            pEncodeBuffer = m_EncodeBufferQueue.GetAvailable();
        }
#ifdef READ_RGB_FILE
        printf("Debug: %dth read data...\n", frm);
        if(frm ==0)
        loadRGBframe(m_rgb, hInput, frm, encodeConfig.width, encodeConfig.height, numBytesRead);
        //if (numBytesRead == 0)
        //break;
#else
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, offsreen_fbo);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
        glColor3f(frm/100.0f, 0.0f,0.0f);
        glVertex3f(0.0f, -0.5f, 0.0f);
        glColor3f(0.0f, frm/100.0f,0.0f);
        glVertex3f(-0.5f, 0.5f, 0.0f);
        glColor3f(0.0f, 0.0f, frm/100.0f);
        glVertex3f(0.5f, 0.5f, 0.0f);
        glEnd();

        glBindFramebuffer(GL_READ_FRAMEBUFFER, offsreen_fbo);
        printf("Debug: %dth frame encoding...\n", frm);
        // CopyArrayToDevice( width, height );
#ifdef ENCODE_RGB
        MapGLTexToCU(&pEncodeBuffer->stInputBfr.pNV12devPtr, colorTex, width, height);
#else
        MapGLTexToCU(&m_RGBDevPtr, colorTex, width, height);
#endif
#endif


#if defined(ENCODE_RGB)
#ifdef READ_RGB_FILE
        LoadRGB(pEncodeBuffer, m_rgb, encodeConfig.width, encodeConfig.height);
#endif
#else
        ConvertRGBToNV12(pEncodeBuffer, m_rgb, encodeConfig.width, encodeConfig.height);
#endif
        nvStatus = m_pNvHWEncoder->NvEncMapInputResource(pEncodeBuffer->stInputBfr.nvRegisteredResource, &pEncodeBuffer->stInputBfr.hInputSurface);
        if (nvStatus != NV_ENC_SUCCESS)
        {
            PRINTERR("Failed to Map input buffer %p\n", pEncodeBuffer->stInputBfr.hInputSurface);
            return nvStatus;
        }
        m_pNvHWEncoder->NvEncEncodeFrame(pEncodeBuffer, NULL, encodeConfig.width, encodeConfig.height);
        numFramesEncoded++;
    }

    FlushEncoder();

    if (numFramesEncoded > 0)
    {
        NvQueryPerformanceCounter(&lEnd);
        NvQueryPerformanceFrequency(&lFreq);
        double elapsedTime = (double)(lEnd - lStart);
        printf("Encoded %d frames in %6.2fms\n", numFramesEncoded, (elapsedTime*1000.0)/lFreq);
        printf("Avergage Encode Time : %6.2fms\n", ((elapsedTime*1000.0)/numFramesEncoded)/lFreq);
    }

    if (encodeConfig.fOutput)
    {
        fclose(encodeConfig.fOutput);
    }

    if (hInput)
    {
        nvCloseFile(hInput);
    }

    Deinitialize();
    
    return bError ? 1 : 0;
}

int main(int argc, char **argv)
{
    CNvEncoderCudaInterop nvEncoder;
    return nvEncoder.EncodeMain(argc, argv);
}
