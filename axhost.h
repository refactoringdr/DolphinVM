/******************************************************************************

	File: AxHost.h

	Description:

	Dolphin Smalltalk Active-X Control Host header file

******************************************************************************/

#ifndef _AXHOST_H_
#define _AXHOST_H_

#if defined(_CONSOLE)
#error "Not for use in CONSOLE build
#endif 

#pragma code_seg(ATL_SEG)

#define _ATL_ALL_WARNINGS
#include <atlbase.h>

// Disable "conditional expression is constant coming from ATL header files
#pragma warning(disable:4127)
#define _ATLWIN_IMPL
#include <atlwin.h>
#pragma warning(default:4127)

#undef ATLAPI
#define ATLAPI extern "C" HRESULT __stdcall
#undef ATLAPI_
#define ATLAPI_(x) extern "C" x __stdcall
#undef ATLINLINE
#define ATLINLINE
#include <atliface.h>

#if _MSC_VER < 1300
	#define _ATLHOST_IMPL
#endif

#include "atlhost.h"

#include "ActiveXHost.h"
#include "ActiveXHost_i.c"

#pragma code_seg()

#endif
