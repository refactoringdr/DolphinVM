/******************************************************************************

	File: Ist.h

	Description:

	Dolphin Smalltalk precompiled header file

******************************************************************************/

#ifndef _IST_IST_H_
#define _IST_IST_H_

// Prevent executable bloat caused by aligning all sections on 4k boundaries.
// Note that this doesn't actually prevent the exe/dll loading on Win9X, but
// theoretically makes it slightly slower to load. Frankly this is not likely
// to be noticeable in practice, certainly not for the very small stub files.
// This means that instead of being bloated to 32Kb, the stubs are a more
// reasonable 13Kb. For VC.Net this needs to be set as a linker option on the
// project, as the #pragma comment is not ignored.
#if _MSC_VER < 1300
#pragma comment(linker, "/OPT:NOWIN98")
#endif

#if defined(VMDLL) && defined(TO_GO)
	#error("Project config is incompatible (TO_GO and VM are mutually exclusive)")
#endif

#if defined(VMDLL) || defined(TO_GO)
	#define VM 1
#endif

// Enable templated overloads for secure version of old-style CRT functions that manipulate buffers but take no size arguments

#undef _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1
#undef _CRT_SECURE_CPP_OVERLOAD_SECURE_NAMES
#define _CRT_SECURE_CPP_OVERLOAD_SECURE_NAMES 1 

// Prevent redefinition of QWORD type in <windns.h>
#define _WINDNS_INCLUDED_

// Prevent warning of redefinition of WIN32_LEAN_AND_MEAN in atldef.h
#define ATL_NO_LEAN_AND_MEAN

#pragma warning(disable:4711)	// Function selected for automatic inline expansion
#pragma warning(disable:4786)	// Browser identifier truncated to 255 characters

#pragma warning(push,3)
#pragma warning(disable:4530)
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#pragma warning(default:4530)
using namespace std;
#pragma warning(pop)

#include "Environ.h"
//#include "version.h"

typedef signed char		SBYTE;
typedef short			SWORD;
typedef long			SDWORD;

// The basic word size of the machine
typedef UINT_PTR	MWORD;
typedef INT_PTR 	SMALLINTEGER;	// Optimized SmallInteger; same size as MWORD
typedef MWORD		SMALLUNSIGNED;	// Unsigned optimized SmallInteger; same size as MWORD	
typedef MWORD		Oop;

// Define this is using a 16-bit word
// as it conditionally compiles in MethodHeaderExtension which
// can otherwise be held in the MethodHeader itself
//#define SMALLWORD
#define MWORDBITS	(sizeof(MWORD)*8)		// Number of bits in an MWORD

class ObjectMemory;
class Interpreter;

#if defined(_DEBUG) && defined(_AFX)
	#define PROFILING
#endif

/*
 * Macros to round numbers (borrowed from CRT)
 *
 * _ROUND2 = rounds a number up to a power of 2
 * _ROUND = rounds a number up to any other numer
 *
 * n = number to be rounded
 * pow2 = must be a power of two value
 * r = any number
 */

#define _ROUND2(n,pow2) \
        ( ( (n) + (pow2) - 1) & ~((pow2) - 1) )

#define _ROUND(n,r) \
        ( ( ((n)/(r)) + (((n)%(r))?1:0) ) * (r))

#ifndef _AFX
	HMODULE __stdcall GetResLibHandle();
	HINSTANCE GetApplicationInstance();
	void DolphinExitInstance();
	HMODULE GetModuleContaining(LPCVOID);
#else
	#ifdef _AFX
		#define GetResLibHandle AfxGetInstanceHandle
		#define GetApplicationInstance AfxGetInstanceHandle
		#define DolphinMessageBox AfxMessageBox
	#endif
#endif

#include "CritSect.h"
extern CMonitor traceMonitor;

#define TRACELOCK()	CAutoLock<tracestream> lock(TRACESTREAM)

LPSTR __stdcall GetErrorText(DWORD win32ErrorCode);
LPSTR __stdcall GetLastErrorText();
int __cdecl DolphinMessageBox(int idPrompt, UINT flags, ...);
void __cdecl trace(const char* szFormat, ...);
void __cdecl DebugCrashDump(LPCTSTR szFormat, ...);
void __cdecl DebugDump(LPCTSTR szFormat, ...);
HRESULT __cdecl ReportError(int nPrompt, ...);
__declspec(noreturn) void __cdecl RaiseFatalError(int nCode, int nArgs, ...);
__declspec(noreturn) void __stdcall FatalException(const EXCEPTION_RECORD& exRec);
__declspec(noreturn) void __stdcall DolphinExit(int nExitCode);

BOOL __stdcall GetVersionInfo(VS_FIXEDFILEINFO* lpInfoOut);
HMODULE GetModuleContaining(LPCVOID pFunc);

#include <unknwn.h>
#include "segdefs.h"

#ifdef _CONSOLE
#define _ATL_NO_COM_SUPPORT
#endif

#endif
