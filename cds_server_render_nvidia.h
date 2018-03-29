#pragma once

#include <stdio.h>
#include <stdlib.h>

#define EGL_EGLEXT_PROTOTYPES 1
#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>

typedef struct EglType{
  EGLDisplay disp;
  EGLContext ctx;
  EGLSurface surf;
} EGL;

typedef struct TextureType {
  GLuint id_;
  GLenum unit_;
  GLenum target_;
} TEX;

class CDSRenderNvidia {
public: 
  CDSRenderNvidia();
  CDSRenderNvidia(TEX &tex_info, int prime_fd, int width, int height);
  ~CDSRenderNvidia();

  /* EGL and GL Functions */
  void Fatal(const char *format, ...);
  void* GetProcAddress(const char *functionName);
  void GetEglExtensionFunctionPointers(void);
  void GetGlExtensionFunctionPointers(void);
  EGLBoolean ExtensionIsSupported(const char *extensionString, const char *ext);

  /* Core Functions */
	EGLDeviceEXT getEglDevice(void);
	int getDrmFd(EGLDeviceEXT device);
  int initEgl();
  void runEncoder();
  void convertRGBToYUV();
  void encodeYUVToH264();
  unsigned char* getEncodedData();
  unsigned int getEncoderdSize();

private:
  /* Core Variables */
  EGL egl;
  TEX tex;

  int prime_fd_;
  int width_;
  int height_;

  /* EGL Proc*/
  PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT = NULL;
	PFNEGLQUERYDEVICESTRINGEXTPROC eglQueryDeviceStringEXT = NULL;
	PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT = NULL;

  /* GL Proc */
  // PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation = NULL;
};
