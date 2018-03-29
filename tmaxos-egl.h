#ifndef __TMAX_OS_EGL__
#define __TMAX_OS_EGL__

struct __EGLTmaxPlatform;
typedef struct __EGLTmaxPlatform EGLTmaxPlatform;

struct tosEGLPlatformData;

/* Extension naming conventions
 *  To define EGL extensions for TmaxOS, keep rules shown below and refer to
 * a section "Naming Conventions" in a documentation "www.khronos.org/registry/OpenGL/docs/rules.html"
 * ------------------------------------------------------
 * For extension names,
 *  API:        EGL
 *  CATEGORY:   TOS
 *  NAME:       lower-case, one or more words separated by underscore(_)
 *
 * For a name of token associated with a defined extension,
 *  API:        EGL
 *  CATEGORY:   TMAXOS
 *  NAME:       upper-case, one or more words separated by underscore(_)
 *
 * Caution!
 *  A short term 'TOS' is used only in a CATEGORY of extension name.
 * Except the case, all word meaning a 'TOS platform' should be placed 'TMAXOS' for uppercase or 'tmaxos' for lowercase rather 'TOS'
 */

#define EGL_TMAXOS_PLATFORM_VERSION_MAJOR		1
#define EGL_TMAXOS_PLATFORM_VERSION_MINOR		0
#define EGL_TMAXOS_PLATFORM_VERSION_PATCH		0

#define EGL_EXTERNAL_PLATFORM_VERSION_MAJOR		EGL_TMAXOS_PLATFORM_VERSION_MAJOR
#define EGL_EXTERNAL_PLATFORM_VERSION_MINOR		EGL_TMAXOS_PLATFORM_VERSION_MINOR

#define EGL_EXTERNAL_PLATFORM_UNLOADEGLEXTERNALPLATFORM_SINCE_MAJOR 1
#define EGL_EXTERNAL_PLATFORM_UNLOADEGLEXTERNALPLATFORM_SINCE_MINOR 0

#define EGL_EXTERNAL_PLATFORM_GETHOOKADDRESS_SINCE_MAJOR            1
#define EGL_EXTERNAL_PLATFORM_GETHOOKADDRESS_SINCE_MINOR            0

#define EGL_EXTERNAL_PLATFORM_ISVALIDNATIVEDISPLAY_SINCE_MAJOR      1
#define EGL_EXTERNAL_PLATFORM_ISVALIDNATIVEDISPLAY_SINCE_MINOR      0

#define EGL_EXTERNAL_PLATFORM_GETPLATFORMDISPLAY_SINCE_MAJOR        1
#define EGL_EXTERNAL_PLATFORM_GETPLATFORMDISPLAY_SINCE_MINOR        0

#define EGL_EXTERNAL_PLATFORM_QUERYSTRING_SINCE_MAJOR               1
#define EGL_EXTERNAL_PLATFORM_QUERYSTRING_SINCE_MINOR               0

#define EGL_EXTERNAL_PLATFORM_GETINTERNALHANDLE_SINCE_MAJOR         1
#define EGL_EXTERNAL_PLATFORM_GETINTERNALHANDLE_SINCE_MINOR         0

#define EGL_EXTERNAL_PLATFORM_GETOBJECTLABEL_SINCE_MAJOR            1
#define EGL_EXTERNAL_PLATFORM_GETOBJECTLABEL_SINCE_MINOR            0

#ifndef EGL_EXT_platform_tmaxos
#define EGL_EXT_platform_tmaxos 1
#define EGL_PLATFORM_TMAXOS_EXT				0x7654
#endif

#ifndef EGL_EGLEXT_PROTPTYPES
#define EGL_EGLEXT_PROTOTYPES
#endif

#include <EGL/eglexternalplatform.h>

/* XXX khronos eglext.h does not yet have EGL_DRM_MASTER_FD_EXT */
#if !defined(EGL_DRM_MASTER_FD_EXT)
#define EGL_DRM_MASTER_FD_EXT           0x333C
#endif

typedef unsigned int tosEGLNativeDisplay; // Simple FD

// External EGLDisplay object: tosEGLDisplay
//  tosEGLDisplay is a external object sended out to application layer by eglGetDisplay() or eglGetPlatformDisplay()
typedef struct tosEGLDisplay
{
	tosEGLNativeDisplay		nativeDpy;	// = FD_DisplayServer
	int						platform;	// = EGL_PLATFORM_TMAX_EXT
	struct tosEGLPlatformData*		tosData;
	int						ownNativeDpy;	// 1 = owner of display, 0 = shared display
    
    // Internal objects
    // It is understood as native window to the EGL driver
	EGLDeviceEXT			eglDevice;  // Physical device object
    EGLDisplay				eglDisplay;	// internal EGLDisplay object
} tosEGLDisplay;

#if 1 // BEGIN EXTERNAL WINDOW

typedef struct tosEGLWindow  {
    int                     width, height;
    int                     dx, dy;
} tosEGLWindow;

#include <sys/socket.h>

typedef union tosEGLControlMSGclientAttach {
    struct cmsghdr          cmsg;
    char                    data[ CMSG_SPACE( sizeof( int ) * 16) ];
} tosEGLControlMSGclientAttach;

#endif // END EXTERNAL WINDOW

#if 1 // BEGIN EXTERNAL SURFACE

#define TMAXOS_EGLSTREAM_WAIT       (-1)
#define TMAXOS_EGLSTREAM_LINKED     (-2)
#define TMAXOS_EGLSTREAM_DAMAGE     (-3)
#define TMAXOS_EGLSTREAM_ACQUIRED   (-4)

#include <pthread.h>
typedef struct tosEGLSurfaceContext {
    EGLSurface              eglSurface;             // eglSurface, connected to server
    EGLStreamKHR            eglStream;              // by eglStream,
    EGLBoolean              isOffscreen;            // is rendered to offscreen if isOffscreen, or not.
    EGLBoolean              isAttached;             // And it is called isAttached.

    int                     useDamageThread;
    pthread_t               damageThreadID;
    EGLSyncKHR              damageThreadSync;
    int                     damageThreadFlush;
    int                     damageThreadShutdown;
    EGLuint64KHR            framesProduced;
    EGLuint64KHR            framesFinished;
    EGLuint64KHR            framesProcessed;
} tosEGLSurfaceContext;

typedef struct tosEGLSurface {
    tosEGLDisplay*          tosDisplay; // 직접적인 주소 소유가 좋을지는 확신할 수 없음.
    EGLConfig               eglConfig;
    EGLint*                 configAttribs;

    tosEGLWindow*           tosWindow;
    int                     width;
    int                     height;
    int                     dx;
    int                     dy;

    tosEGLSurfaceContext    context;

    EGLint                  swapInterval;
} tosEGLSurface;

#endif // END EXTERNAL SURFACE

// TODO: Move this to a private header that included from both server or client once
#if 1
static const char* TEMPORARY_SOCKET_PATH = "/tmp/eglStreamSocket";

#define CAST_TO( type, mem, shell ) (type)((unsigned char*)(shell) - (unsigned char*)&(((type)(0))->mem))

#endif


/*
 * FIXME: Remove both EGL_EXT_stream_acquire_mode and
 *        EGL_NV_output_drm_flip_event definitions below once both extensions
 *        get published by Khronos and incorportated into Khronos' header files
 */
#ifndef EGL_NV_stream_attrib
#define EGL_NV_stream_attrib 1
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI EGLStreamKHR EGLAPIENTRY eglCreateStreamAttribNV(EGLDisplay dpy, const EGLAttrib *attrib_list);
EGLAPI EGLBoolean EGLAPIENTRY eglSetStreamAttribNV(EGLDisplay dpy, EGLStreamKHR stream, EGLenum attribute, EGLAttrib value);
EGLAPI EGLBoolean EGLAPIENTRY eglQueryStreamAttribNV(EGLDisplay dpy, EGLStreamKHR stream, EGLenum attribute, EGLAttrib *value);
EGLAPI EGLBoolean EGLAPIENTRY eglStreamConsumerAcquireAttribNV(EGLDisplay dpy, EGLStreamKHR stream, const EGLAttrib *attrib_list);
EGLAPI EGLBoolean EGLAPIENTRY eglStreamConsumerReleaseAttribNV(EGLDisplay dpy, EGLStreamKHR stream, const EGLAttrib *attrib_list);
#endif
typedef EGLStreamKHR (EGLAPIENTRYP PFNEGLCREATESTREAMATTRIBNVPROC) (EGLDisplay dpy, const EGLAttrib *attrib_list);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSETSTREAMATTRIBNVPROC) (EGLDisplay dpy, EGLStreamKHR stream, EGLenum attribute, EGLAttrib value);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLQUERYSTREAMATTRIBNVPROC) (EGLDisplay dpy, EGLStreamKHR stream, EGLenum attribute, EGLAttrib *value);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSTREAMCONSUMERACQUIREATTRIBNVPROC) (EGLDisplay dpy, EGLStreamKHR stream, const EGLAttrib *attrib_list);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSTREAMCONSUMERRELEASEATTRIBNVPROC) (EGLDisplay dpy, EGLStreamKHR stream, const EGLAttrib *attrib_list);
#endif /* EGL_NV_stream_attrib */

#ifndef EGL_EXT_stream_acquire_mode
#define EGL_EXT_stream_acquire_mode 1
#define EGL_CONSUMER_AUTO_ACQUIRE_EXT         0x332B
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSTREAMCONSUMERACQUIREATTRIBEXTPROC) (EGLDisplay dpy, EGLStreamKHR stream, const EGLAttrib *attrib_list);
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI EGLBoolean EGLAPIENTRY eglStreamConsumerAcquireAttribEXT (EGLDisplay dpy, EGLStreamKHR stream, const EGLAttrib *attrib_list);
#endif
#endif /* EGL_EXT_stream_acquire_mode */

#ifndef EGL_NV_output_drm_flip_event
#define EGL_NV_output_drm_flip_event 1
#define EGL_DRM_FLIP_EVENT_DATA_NV            0x333E
#endif /* EGL_NV_output_drm_flip_event */

//#ifndef EGL_WL_wayland_eglstream
//#define EGL_WL_wayland_eglstream 1
//#define EGL_WAYLAND_EGLSTREAM_WL              0x334B
//#endif /* EGL_WL_wayland_eglstream */

#ifndef EGL_TOS_tmaxos_eglstream
#define EGL_TOS_tmaxos_eglstream 1
#define EGL_TMAXOS_EGLSTREAM_TOS              0x7655
#endif /* EGL_TOS_tmax_eglstream */


#endif
