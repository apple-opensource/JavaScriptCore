/*
 * Copyright (C) 2006, 2007, 2008, 2009 Apple Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WTF_Platform_h
#define WTF_Platform_h

/* PLATFORM handles OS, operating environment, graphics API, and CPU */
#define PLATFORM(WTF_FEATURE) (defined( WTF_PLATFORM_##WTF_FEATURE ) && WTF_PLATFORM_##WTF_FEATURE)
#define COMPILER(WTF_FEATURE) (defined( WTF_COMPILER_##WTF_FEATURE ) && WTF_COMPILER_##WTF_FEATURE)
#define HAVE(WTF_FEATURE) (defined( HAVE_##WTF_FEATURE ) && HAVE_##WTF_FEATURE)
#define USE(WTF_FEATURE) (defined( WTF_USE_##WTF_FEATURE ) && WTF_USE_##WTF_FEATURE)
#define ENABLE(WTF_FEATURE) (defined( ENABLE_##WTF_FEATURE ) && ENABLE_##WTF_FEATURE)

/* Operating systems - low-level dependencies */

/* PLATFORM(DARWIN) */
/* Operating system level dependencies for Mac OS X / Darwin that should */
/* be used regardless of operating environment */
#ifdef __APPLE__
#define WTF_PLATFORM_DARWIN 1
#include <AvailabilityMacros.h>
#if !defined(MAC_OS_X_VERSION_10_5) || MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_5
#define BUILDING_ON_TIGER 1
#elif !defined(MAC_OS_X_VERSION_10_6) || MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_6
#define BUILDING_ON_LEOPARD 1
#endif
#include <Availability.h>
#include <TargetConditionals.h>
#endif

/* PLATFORM(WIN_OS) */
/* Operating system level dependencies for Windows that should be used */
/* regardless of operating environment */
#if defined(WIN32) || defined(_WIN32)
#define WTF_PLATFORM_WIN_OS 1
#endif

/* PLATFORM(WINCE) */
/* Operating system level dependencies for Windows CE that should be used */
/* regardless of operating environment */
/* Note that for this platform PLATFORM(WIN_OS) is also defined. */
#if defined(_WIN32_WCE)
#define WTF_PLATFORM_WINCE 1
#endif

/* PLATFORM(LINUX) */
/* Operating system level dependencies for Linux-like systems that */
/* should be used regardless of operating environment */
#ifdef __linux__
#define WTF_PLATFORM_LINUX 1
#endif

/* PLATFORM(FREEBSD) */
/* Operating system level dependencies for FreeBSD-like systems that */
/* should be used regardless of operating environment */
#ifdef __FreeBSD__
#define WTF_PLATFORM_FREEBSD 1
#endif

/* PLATFORM(OPENBSD) */
/* Operating system level dependencies for OpenBSD systems that */
/* should be used regardless of operating environment */
#ifdef __OpenBSD__
#define WTF_PLATFORM_OPENBSD 1
#endif

/* PLATFORM(SOLARIS) */
/* Operating system level dependencies for Solaris that should be used */
/* regardless of operating environment */
#if defined(sun) || defined(__sun)
#define WTF_PLATFORM_SOLARIS 1
#endif

#if defined (__S60__) || defined (__SYMBIAN32__)
/* we are cross-compiling, it is not really windows */
#undef WTF_PLATFORM_WIN_OS
#undef WTF_PLATFORM_WIN
#undef WTF_PLATFORM_CAIRO
#define WTF_PLATFORM_S60 1
#define WTF_PLATFORM_SYMBIAN 1
#endif


/* PLATFORM(NETBSD) */
/* Operating system level dependencies for NetBSD that should be used */
/* regardless of operating environment */
#if defined(__NetBSD__)
#define WTF_PLATFORM_NETBSD 1
#endif

/* PLATFORM(UNIX) */
/* Operating system level dependencies for Unix-like systems that */
/* should be used regardless of operating environment */
#if   PLATFORM(DARWIN)     \
   || PLATFORM(FREEBSD)    \
   || PLATFORM(S60)        \
   || PLATFORM(NETBSD)     \
   || defined(unix)        \
   || defined(__unix)      \
   || defined(__unix__)    \
   || defined(_AIX)
#define WTF_PLATFORM_UNIX 1
#endif

/* Operating environments */

/* PLATFORM(CHROMIUM) */
/* PLATFORM(QT) */
/* PLATFORM(GTK) */
/* PLATFORM(MAC) */
/* PLATFORM(WIN) */
#if defined(BUILDING_CHROMIUM__)
#define WTF_PLATFORM_CHROMIUM 1
#elif defined(BUILDING_QT__)
#define WTF_PLATFORM_QT 1

/* PLATFORM(KDE) */
#if defined(BUILDING_KDE__)
#define WTF_PLATFORM_KDE 1
#endif

#elif defined(BUILDING_WX__)
#define WTF_PLATFORM_WX 1
#elif defined(BUILDING_GTK__)
#define WTF_PLATFORM_GTK 1
#elif PLATFORM(DARWIN)
#define WTF_PLATFORM_MAC 1
#elif PLATFORM(WIN_OS)
#define WTF_PLATFORM_WIN 1
#endif

/* PLATFORM(IPHONE) */
#if (defined(TARGET_OS_EMBEDDED) && TARGET_OS_EMBEDDED) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
#define WTF_PLATFORM_IPHONE 1
#endif

/* PLATFORM(IPHONE_SIMULATOR) */
#if defined(TARGET_IPHONE_SIMULATOR) && TARGET_IPHONE_SIMULATOR
#define WTF_PLATFORM_IPHONE 1
#define WTF_PLATFORM_IPHONE_SIMULATOR 1
#else
#define WTF_PLATFORM_IPHONE_SIMULATOR 0
#endif

#if !defined(WTF_PLATFORM_IPHONE)
#define WTF_PLATFORM_IPHONE 0
#endif

/* Graphics engines */

/* PLATFORM(CG) and PLATFORM(CI) */
#define WTF_PLATFORM_CG 1

/* PLATFORM(SKIA) for Win/Linux, CG/CI for Mac */
#if PLATFORM(CHROMIUM)
#if PLATFORM(DARWIN)
#define WTF_PLATFORM_CG 1
#define WTF_PLATFORM_CI 1
#define WTF_USE_ATSUI 1
#else
#define WTF_PLATFORM_SKIA 1
#endif
#endif

/* Makes PLATFORM(WIN) default to PLATFORM(CAIRO) */
/* FIXME: This should be changed from a blacklist to a whitelist */
#if !PLATFORM(MAC) && !PLATFORM(QT) && !PLATFORM(WX) && !PLATFORM(CHROMIUM)
#define WTF_PLATFORM_CAIRO 1
#endif

/* CPU */

/* PLATFORM(PPC) */
#if   defined(__ppc__)     \
   || defined(__PPC__)     \
   || defined(__powerpc__) \
   || defined(__powerpc)   \
   || defined(__POWERPC__) \
   || defined(_M_PPC)      \
   || defined(__PPC)
#define WTF_PLATFORM_PPC 1
#define WTF_PLATFORM_BIG_ENDIAN 1
#endif

/* PLATFORM(PPC64) */
#if   defined(__ppc64__) \
   || defined(__PPC64__)
#define WTF_PLATFORM_PPC64 1
#define WTF_PLATFORM_BIG_ENDIAN 1
#endif

/* PLATFORM(ARM) */
#if   defined(arm) \
   || defined(__arm__)
#define WTF_PLATFORM_ARM 1
#if defined(__ARMEB__)
#define WTF_PLATFORM_BIG_ENDIAN 1
#elif !defined(__ARM_EABI__) && !defined(__ARMEB__) && !defined(__VFP_FP__)
#define WTF_PLATFORM_MIDDLE_ENDIAN 1
#endif
#if !defined(__ARM_EABI__)
#define WTF_PLATFORM_FORCE_PACK 1
#endif
#define ARM_ARCH_VERSION 3
#if defined(__ARM_ARCH_4__) || defined(__ARM_ARCH_4T__)
#undef ARM_ARCH_VERSION
#define ARM_ARCH_VERSION 4
#endif
#if defined(__ARM_ARCH_5__) || defined(__ARM_ARCH_5T__) \
        || defined(__ARM_ARCH_5E__) || defined(__ARM_ARCH_5TE__) \
        || defined(__ARM_ARCH_5TEJ__)
#undef ARM_ARCH_VERSION
#define ARM_ARCH_VERSION 5
#endif
#if defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) \
     || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6Z__) \
     || defined(__ARM_ARCH_6ZK__)
#undef ARM_ARCH_VERSION
#define ARM_ARCH_VERSION 6
#endif
#if defined(__ARM_ARCH_7A__)
#undef ARM_ARCH_VERSION
#define ARM_ARCH_VERSION 7
#endif
#endif /* ARM */
#define PLATFORM_ARM_ARCH(N) (PLATFORM(ARM) && ARM_ARCH_VERSION >= N)

/* PLATFORM(X86) */
#if   defined(__i386__) \
   || defined(i386)     \
   || defined(_M_IX86)  \
   || defined(_X86_)    \
   || defined(__THW_INTEL)
#define WTF_PLATFORM_X86 1
#endif

/* PLATFORM(X86_64) */
#if   defined(__x86_64__) \
   || defined(_M_X64)
#define WTF_PLATFORM_X86_64 1
#endif

/* PLATFORM(SH4) */
#if defined(__SH4__)
#define WTF_PLATFORM_SH4 1
#endif

/* PLATFORM(SPARC64) */
#if defined(__sparc64__)
#define WTF_PLATFORM_SPARC64 1
#define WTF_PLATFORM_BIG_ENDIAN 1
#endif

/* PLATFORM(WINCE) && PLATFORM(QT)
   We can not determine the endianess at compile time. For
   Qt for Windows CE the endianess is specified in the
   device specific makespec
*/
#if PLATFORM(WINCE) && PLATFORM(QT)
#   include <QtGlobal>
#   undef WTF_PLATFORM_BIG_ENDIAN
#   undef WTF_PLATFORM_MIDDLE_ENDIAN
#   if Q_BYTE_ORDER == Q_BIG_EDIAN
#       define WTF_PLATFORM_BIG_ENDIAN 1
#   endif
#endif

/* Compiler */

/* COMPILER(MSVC) */
#if defined(_MSC_VER)
#define WTF_COMPILER_MSVC 1
#if _MSC_VER < 1400
#define WTF_COMPILER_MSVC7 1
#endif
#endif

/* COMPILER(RVCT) */
#if defined(__CC_ARM) || defined(__ARMCC__)
#define WTF_COMPILER_RVCT 1
#endif

/* COMPILER(GCC) */
/* --gnu option of the RVCT compiler also defines __GNUC__ */
#if defined(__GNUC__) && !COMPILER(RVCT)
#define WTF_COMPILER_GCC 1
#endif

/* COMPILER(MINGW) */
#if defined(MINGW) || defined(__MINGW32__)
#define WTF_COMPILER_MINGW 1
#endif

/* COMPILER(BORLAND) */
/* not really fully supported - is this relevant any more? */
#if defined(__BORLANDC__)
#define WTF_COMPILER_BORLAND 1
#endif

/* COMPILER(CYGWIN) */
/* not really fully supported - is this relevant any more? */
#if defined(__CYGWIN__)
#define WTF_COMPILER_CYGWIN 1
#endif

/* COMPILER(WINSCW) */
#if defined(__WINSCW__)
#define WTF_COMPILER_WINSCW 1
#endif

#if (PLATFORM(IPHONE) || PLATFORM(MAC) || PLATFORM(WIN)) && !defined(ENABLE_JSC_MULTIPLE_THREADS)
#define ENABLE_JSC_MULTIPLE_THREADS 1
#endif

/* for Unicode, KDE uses Qt */
#if PLATFORM(KDE) || PLATFORM(QT)
#define WTF_USE_QT4_UNICODE 1
#elif PLATFORM(SYMBIAN)
#define WTF_USE_SYMBIAN_UNICODE 1
#elif PLATFORM(GTK)
/* The GTK+ Unicode backend is configurable */
#else
#define WTF_USE_ICU_UNICODE 1
#endif


#if PLATFORM(CHROMIUM) && PLATFORM(DARWIN)
#define WTF_PLATFORM_CF 1
#define WTF_USE_PTHREADS 1
#endif

#define DONT_FINALIZE_ON_MAIN_THREAD 1
#define ENABLE_CONTEXT_MENUS 0
#define ENABLE_DRAG_SUPPORT 0
#define ENABLE_FTPDIR 1
#define ENABLE_GEOLOCATION 1
#define ENABLE_ICONDATABASE 0
#define ENABLE_INSPECTOR 0
#define ENABLE_JIT 0
#define ENABLE_MAC_JAVA_BRIDGE 0
#define ENABLE_NETSCAPE_PLUGIN_API 0
#define ENABLE_ORIENTATION_EVENTS 1
#define ENABLE_REPAINT_THROTTLING 1
#define ENABLE_RESPECT_EXIF_ORIENTATION 1
#define HAVE_READLINE 1
#define HAVE_RUNLOOP_TIMER 0
#define WTF_PLATFORM_CF 1
#define WTF_USE_PTHREADS 1

#if PLATFORM(WIN)
#define WTF_USE_WININET 1
#endif

#if PLATFORM(WX)
#define ENABLE_ASSEMBLER 1
#define WTF_USE_CURL 1
#define WTF_USE_PTHREADS 1
#endif

#if PLATFORM(GTK)
#if HAVE(PTHREAD_H)
#define WTF_USE_PTHREADS 1
#endif
#endif

#if !defined(HAVE_ACCESSIBILITY)
#define HAVE_ACCESSIBILITY 1
#endif /* !defined(HAVE_ACCESSIBILITY) */

#if PLATFORM(UNIX) && !PLATFORM(SYMBIAN)
#define HAVE_SIGNAL_H 1
#endif

#if PLATFORM(DARWIN)

#define HAVE_ERRNO_H 1
#define HAVE_LANGINFO_H 1
#define HAVE_MMAP 1
#define HAVE_MERGESORT 1
#define HAVE_SBRK 1
#define HAVE_STRINGS_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TIMEB_H 1


#define HAVE_MADV_FREE 1

#elif PLATFORM(WIN_OS)

#define HAVE_FLOAT_H 1
#if PLATFORM(WINCE)
#define HAVE_ERRNO_H 0
#else
#define HAVE_SYS_TIMEB_H 1
#endif
#define HAVE_VIRTUALALLOC 1

#elif PLATFORM(SYMBIAN)

#define HAVE_ERRNO_H 1
#define HAVE_MMAP 0
#define HAVE_SBRK 1

#define HAVE_SYS_TIME_H 1
#define HAVE_STRINGS_H 1

#if !COMPILER(RVCT)
#define HAVE_SYS_PARAM_H 1
#endif

#else

/* FIXME: is this actually used or do other platforms generate their own config.h? */

#define HAVE_ERRNO_H 1
#define HAVE_LANGINFO_H 1
#define HAVE_MMAP 1
#define HAVE_SBRK 1
#define HAVE_STRINGS_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_TIME_H 1

#endif

/* ENABLE macro defaults */

/* fastMalloc match validation allows for runtime verification that
   new is matched by delete, fastMalloc is matched by fastFree, etc. */
#if !defined(ENABLE_FAST_MALLOC_MATCH_VALIDATION)
#define ENABLE_FAST_MALLOC_MATCH_VALIDATION 0
#endif

#if !defined(ENABLE_ICONDATABASE)
#define ENABLE_ICONDATABASE 1
#endif

#if !defined(ENABLE_DATABASE)
#define ENABLE_DATABASE 1
#endif

#if !defined(ENABLE_JAVASCRIPT_DEBUGGER)
#define ENABLE_JAVASCRIPT_DEBUGGER 1
#endif

#if !defined(ENABLE_FTPDIR)
#define ENABLE_FTPDIR 1
#endif

#if !defined(ENABLE_DASHBOARD_SUPPORT)
#define ENABLE_DASHBOARD_SUPPORT 0
#endif

#if !defined(ENABLE_CONTEXT_MENUS)
#define ENABLE_CONTEXT_MENUS 1
#endif

#if !defined(ENABLE_DRAG_SUPPORT)
#define ENABLE_DRAG_SUPPORT 1
#endif

#if !defined(ENABLE_INSPECTOR)
#define ENABLE_INSPECTOR 1
#endif

#if !defined(ENABLE_MAC_JAVA_BRIDGE)
#define ENABLE_MAC_JAVA_BRIDGE 0
#endif

#if !defined(ENABLE_NETSCAPE_PLUGIN_API)
#define ENABLE_NETSCAPE_PLUGIN_API 1
#endif

#if !defined(WTF_USE_PLUGIN_HOST_PROCESS)
#define WTF_USE_PLUGIN_HOST_PROCESS 0
#endif

#if !defined(ENABLE_ORIENTATION_EVENTS)
#define ENABLE_ORIENTATION_EVENTS 0
#endif

#if !defined(ENABLE_RESPECT_EXIF_ORIENTATION)
#define ENABLE_RESPECT_EXIF_ORIENTATION 0
#endif

#if !defined(ENABLE_OPCODE_STATS)
#define ENABLE_OPCODE_STATS 0
#endif

#define ENABLE_SAMPLING_COUNTERS 0
#define ENABLE_SAMPLING_FLAGS 0
#define ENABLE_OPCODE_SAMPLING 0
#define ENABLE_CODEBLOCK_SAMPLING 0
#if ENABLE(CODEBLOCK_SAMPLING) && !ENABLE(OPCODE_SAMPLING)
#error "CODEBLOCK_SAMPLING requires OPCODE_SAMPLING"
#endif
#if ENABLE(OPCODE_SAMPLING) || ENABLE(SAMPLING_FLAGS)
#define ENABLE_SAMPLING_THREAD 1
#endif

#if !defined(ENABLE_GEOLOCATION)
#define ENABLE_GEOLOCATION 0
#endif

#if !defined(ENABLE_TEXT_CARET)
#define ENABLE_TEXT_CARET 1
#endif

#if !defined(ENABLE_ON_FIRST_TEXTAREA_FOCUS_SELECT_ALL)
#define ENABLE_ON_FIRST_TEXTAREA_FOCUS_SELECT_ALL 0
#endif

#if !defined(WTF_USE_JSVALUE64) && !defined(WTF_USE_JSVALUE32) && !defined(WTF_USE_JSVALUE32_64)
#if PLATFORM(X86_64) && PLATFORM(MAC)
#define WTF_USE_JSVALUE64 1
#elif PLATFORM(PPC64) || PLATFORM(QT) || (PLATFORM(IPHONE) && PLATFORM_ARM_ARCH(6) && !defined(__llvm__))
// FIXME: <rdar://problem/7478149> gcc-4.2 compiler bug with USE(JSVALUE32_64) and armv6 target
#define WTF_USE_JSVALUE32 1
#else
#define WTF_USE_JSVALUE32_64 1
#endif
#endif // !defined(WTF_USE_JSVALUE64) && !defined(WTF_USE_JSVALUE32) && !defined(WTF_USE_JSVALUE32_64)

#if !defined(ENABLE_REPAINT_THROTTLING)
#define ENABLE_REPAINT_THROTTLING 0
#endif

#if !defined(ENABLE_JIT)
/* The JIT is tested & working on x86_64 Mac */
#if PLATFORM(X86_64) && PLATFORM(MAC)
    #define ENABLE_JIT 1
/* The JIT is tested & working on x86 Mac */
#elif PLATFORM(X86) && PLATFORM(MAC)
    #define ENABLE_JIT 1
    #define WTF_USE_JIT_STUB_ARGUMENT_VA_LIST 1
#elif PLATFORM_ARM_ARCH(7) && PLATFORM(IPHONE)
    /* Under development, temporarily disabled until 16Mb link range limit in assembler is fixed. */
    #define ENABLE_JIT 0
    #define ENABLE_JIT_OPTIMIZE_NATIVE_CALL 0
/* The JIT is tested & working on x86 Windows */
#elif PLATFORM(X86) && PLATFORM(WIN)
    #define ENABLE_JIT 1
#endif
#endif

#if ENABLE(JIT)
#ifndef ENABLE_JIT_OPTIMIZE_CALL
#define ENABLE_JIT_OPTIMIZE_CALL 1
#endif
#ifndef ENABLE_JIT_OPTIMIZE_NATIVE_CALL
#define ENABLE_JIT_OPTIMIZE_NATIVE_CALL 1
#endif
#ifndef ENABLE_JIT_OPTIMIZE_PROPERTY_ACCESS
#define ENABLE_JIT_OPTIMIZE_PROPERTY_ACCESS 1
#endif
#ifndef ENABLE_JIT_OPTIMIZE_METHOD_CALLS
#define ENABLE_JIT_OPTIMIZE_METHOD_CALLS 1
#endif
#endif

#if PLATFORM(X86) && COMPILER(MSVC)
#define JSC_HOST_CALL __fastcall
#elif PLATFORM(X86) && COMPILER(GCC)
#define JSC_HOST_CALL __attribute__ ((fastcall))
#else
#define JSC_HOST_CALL
#endif

#if COMPILER(GCC) && !ENABLE(JIT)
#define HAVE_COMPUTED_GOTO 1
#endif

#if ENABLE(JIT) && defined(COVERAGE)
    #define WTF_USE_INTERPRETER 0
#else
    #define WTF_USE_INTERPRETER 1
#endif

/* Yet Another Regex Runtime. */
/* YARR supports x86 & x86-64, and has been tested on Mac and Windows. */
#if (!defined(ENABLE_YARR_JIT) && PLATFORM(X86) && PLATFORM(MAC)) \
 || (!defined(ENABLE_YARR_JIT) && PLATFORM(X86_64) && PLATFORM(MAC)) \
 /* Under development, temporarily disabled until 16Mb link range limit in assembler is fixed. */ \
 || (!defined(ENABLE_YARR_JIT) && PLATFORM_ARM_ARCH(7) && PLATFORM(IPHONE) && 0) \
 || (!defined(ENABLE_YARR_JIT) && PLATFORM(X86) && PLATFORM(WIN))
#define ENABLE_YARR 1
#define ENABLE_YARR_JIT 1
#endif
/* Sanity Check */
#if ENABLE(YARR_JIT) && !ENABLE(YARR)
#error "YARR_JIT requires YARR"
#endif

#if ENABLE(JIT) || ENABLE(YARR_JIT)
#define ENABLE_ASSEMBLER 1
#endif
/* Setting this flag prevents the assembler from using RWX memory; this may improve
   security but currectly comes at a significant performance cost. */
#if PLATFORM_ARM_ARCH(7) && PLATFORM(IPHONE)
#define ENABLE_ASSEMBLER_WX_EXCLUSIVE 1
#else
#define ENABLE_ASSEMBLER_WX_EXCLUSIVE 0
#endif

#if !defined(ENABLE_PAN_SCROLLING) && PLATFORM(WIN_OS)
#define ENABLE_PAN_SCROLLING 1
#endif

#if !defined(ENABLE_ACTIVEX_TYPE_CONVERSION_WMPLAYER)
#define ENABLE_ACTIVEX_TYPE_CONVERSION_WMPLAYER 1
#endif

/* Use the QtXmlStreamReader implementation for XMLTokenizer */
#if PLATFORM(QT)
#if !ENABLE(XSLT)
#define WTF_USE_QXMLSTREAM 1
#endif
#endif

#if !PLATFORM(QT)
#define WTF_USE_FONT_FAST_PATH 1
#endif

/* Accelerated compositing */
#if PLATFORM(MAC)
#if !defined(BUILDING_ON_TIGER) && !defined(BUILDING_ON_LEOPARD)
#define WTF_USE_ACCELERATED_COMPOSITING 1
#endif
#endif

#define WTF_USE_ACCELERATED_COMPOSITING 1

#endif /* WTF_Platform_h */
