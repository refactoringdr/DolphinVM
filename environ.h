/******************************************************************************

	File: Environ.h

	Description:

	Portable environment definitions for Dolphin Smalltalk

******************************************************************************/
#ifndef _ST_ENVIRON_H_
#define _ST_ENVIRON_H_

#define TODO(s)
#include <malloc.h>
#include <stdlib.h>

#ifdef WIN32
	#pragma warning(push, 3)

	#ifndef STRICT
	#define STRICT
	#endif

	// Modify the following defines if you have to target a platform prior to the ones specified below.
	// Refer to MSDN for the latest info on corresponding values for different platforms.
	#ifndef WINVER				// Allow use of features specific to Windows XP and later (we no longer support 9x, 2000 or NT4)
	#define WINVER 0x0501
	#endif

	#ifndef _WIN32_WINNT		
	#define _WIN32_WINNT 0x0501
	#endif						

	#ifndef _WIN32_WINDOWS
	#define _WIN32_WINDOWS 0x0501
	#endif

	#ifndef _WIN32_IE
	#define _WIN32_IE 0x0500	// Target IE 5.0 or later.
	#endif

	// Define some things for ATL, in case there is any in this project

	#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// some CString constructors will be explicit

	// turns off ATL's hiding of some common and often safely ignored warning messages
	#define _ATL_ALL_WARNINGS

	//#pragma warning ( disable : 4244 4514 4201 )
	// 4705 statement has no effect

	#ifdef _AFX
		#include <afxwin.h>         // MFC core and standard components
		#include <afxext.h>         // MFC extensions
	#else
		#include <windows.h>
		#include <winver.h>
	#endif
	#include <mmsystem.h>

	#pragma warning(pop)

	#ifndef VM
		// Compiler part of .exe versions, so null out the imports of the DolphinIF functions
		#define _DOLPHINIMPORT extern
	#endif

	#include "TraceStream.h"

	extern tracestream thinDump;
	#define TRACESTREAM thinDump

	#ifndef _AFX
		// Defines for thin (non MFC) version

		#ifdef _DEBUG
			#define _CRTDBG_MAP_ALLOC
			#define DEBUG_ONLY
			#define TRACE				::trace
			#define VERIFY				_ASSERTE
		#else
			#include <crtdbg.h>
			#define DEBUG_ONLY(f)      ((void)0)
			inline void __cdecl DolphinTrace(LPCTSTR, ...)  {}
			#define TRACE				1 ? ((void)0) : ::trace
			#define VERIFY
		#endif
		
		#include <crtdbg.h>

		#ifdef DEBUG
			#define ASSUME		_ASSERTE
		#else
			// VC6 optimization
			#define ASSUME(e)    (__assume(e))
		#endif

		#define ASSERT_VALID(pOb)  ((void)0)
		#define AfxCheckMemory()

		#ifdef _M_IX86
			#define	DEBUGBREAK()			{ _asm int 3 }
		#else
			#define DEBUGBREAK()			DebugBreak()
		#endif


	#else
		// Defines for MFC version

		#if defined(_DEBUG)
			#define NEW DEBUG_NEW
		#else
			#define NEW new
		#endif

		#define DEBUGBREAK()	AfxDebugBreak()
	#endif

	#ifdef DEBUG
		#ifndef ASSERT
			#define ASSERT		_ASSERTE
		#endif
	#else
		// VC6 optimization
		#undef ASSERT
		#define ASSERT    __assume
	#endif

	#ifdef _DEBUG
		#define HARDASSERT(e)		{ if (!(e)) { DEBUGBREAK(); } }
	#else
		#define	HARDASSERT(e)		((void)0)
	#endif

	enum { dwPageSize = 0x1000 };						// 4Kb
	enum { dwAllocationGranularity = 0x10000 };			// 64Kb

#else
	#error "Not implemented for other platforms yet"
#endif

#pragma warning (disable : 4201)

#endif
