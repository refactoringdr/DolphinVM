/******************************************************************************

	File: STExternal.h

	Description:

	VM representation of Smalltalk external interfacing classes.

	N.B. Some of the classes here defined are well known to the VM, and must not
	be modified in the image. Note also that these classes may also have
	a representation in the assembler modules (so see istasm.inc)

******************************************************************************/

#ifndef _IST_STEXTERNAL_H_
#define _IST_STEXTERNAL_H_

#include "STObject.h"

class ExternalStructure;
typedef TOTE<ExternalStructure> StructureOTE;

class ExternalStructure //: public Object
{
public:
	BytesOTE*	m_contents;

	static StructureOTE* __fastcall NewRefStruct(BehaviorOTE* classPointer, void* ptr);
	static OTE* __fastcall NewPointer(BehaviorOTE* classPointer, void* ptr);
	static OTE* __fastcall New(BehaviorOTE* classPointer, void* pContents);
};

// Really a subclass of ByteArray
class DWORDBytes //: public Object
{
public:
	DWORD m_dwValue;
};

class ExternalHandle;
typedef TOTE<ExternalHandle> HandleOTE;
ostream& operator<<(ostream& st, const HandleOTE* oteHandle);

class ExternalHandle //: public Object
{
	// ExternalHandle is really a variable byte subclass of length 4
public:
	HANDLE m_handle;

	static HandleOTE* New(HANDLE hValue);
	static HANDLE handleFromOop(Oop oopHandle);
	static Oop IntegerOrNew(HANDLE hValue);
};

class ExternalAddress;
typedef TOTE<ExternalAddress> AddressOTE;

class ExternalAddress //: public Object
{
	// ExternalAddress is really a variable byte subclass of length 4
public:
	void* m_pointer;

	static AddressOTE* New(void* ptrValue);
};

class CallbackDescriptor //: public Object
{
public:
	Oop m_convention;
	Oop	m_returnDescriptor;
	Oop m_arguments[];
};

class DescriptorBytes;
typedef TOTE<DescriptorBytes> DescriptorOTE;

// Not a real class, but useful all the same
class DescriptorBytes //: public Object
{
public:
	BYTE	m_callingConvention;
	BYTE	m_argumentCount;
	BYTE	m_returnType;
	BYTE	m_returnClass;
	BYTE	m_args[];

	static unsigned argsLen(DescriptorOTE* ote) { return ote->getSize() - offsetof(DescriptorBytes, m_args); }
};

class ExternalDescriptor //: public Object
{
public:
	DescriptorOTE* m_descriptor;	// Byte array of descriptor bytes
	OTE* m_literals[];
};

///////////////////////////////////////////////////////////////////////////////

inline AddressOTE* ExternalAddress::New(void* ptr)
{
	return reinterpret_cast<AddressOTE*>(Interpreter::NewDWORD(DWORD(ptr), Pointers.ClassExternalAddress));
}

inline HandleOTE* ExternalHandle::New(HANDLE hValue)
{
	return reinterpret_cast<HandleOTE*>(Interpreter::NewDWORD(DWORD(hValue), Pointers.ClassExternalHandle));
}

// Answer either a SmallInteger or an ExternalHandle (if too big) to hold
// the specified handle.
inline Oop ExternalHandle::IntegerOrNew(HANDLE hValue)
{
	return ObjectMemoryIsPositiveIntegerValue(hValue) ?
		ObjectMemoryIntegerObjectOf(hValue) :
		Oop(New(hValue));
}

// Answer either a SmallInteger or an ExternalHandle (if too big) to hold
// the specified handle.
inline HANDLE ExternalHandle::handleFromOop(Oop oopHandle)
{
	if (ObjectMemoryIsIntegerObject(oopHandle))
		return HANDLE(ObjectMemoryIntegerValueOf(oopHandle));
	else
	{
		HandleOTE* oteHandle = reinterpret_cast<HandleOTE*>(oopHandle);
		ExternalHandle* eh = oteHandle->m_location;
		return eh->m_handle;
	}
}

#endif	// EOF