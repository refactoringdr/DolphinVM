/******************************************************************************

	File: SearchPrim.cpp

	Description:

	Implementation of the Interpreter class' primitive methods for searching
	objects

******************************************************************************/
#include "Ist.h"
#pragma code_seg(PRIM_SEG)

#include "ObjMem.h"
#include "Interprt.h"
#include "InterprtPrim.inl"

// Smalltalk classes
#include "STBehavior.h"

// Uses object identity to locate the next occurrence of the argument in the receiver from
// the specified index to the specified index
BOOL __fastcall Interpreter::primitiveNextIndexOfFromTo()
{
	Oop integerPointer = stackTop();
	if (!ObjectMemoryIsIntegerObject(integerPointer))
		return primitiveFailure(0);				// to not an integer
	const SMALLINTEGER to = ObjectMemoryIntegerValueOf(integerPointer);

	integerPointer = stackValue(1);
	if (!ObjectMemoryIsIntegerObject(integerPointer))
		return primitiveFailure(1);				// from not an integer
	SMALLINTEGER from = ObjectMemoryIntegerValueOf(integerPointer);

	Oop valuePointer = stackValue(2);
	OTE* receiverPointer = reinterpret_cast<OTE*>(stackValue(3));

//	#ifdef _DEBUG
		if (ObjectMemoryIsIntegerObject(receiverPointer))
			return primitiveFailure(2);				// Not valid for SmallIntegers
//	#endif

	Oop answer = ZeroPointer;
	if (to >= from)
	{
		if (!receiverPointer->isPointers())
		{
			// Search a byte object
			BytesOTE* oteBytes = reinterpret_cast<BytesOTE*>(receiverPointer);

			if (ObjectMemoryIsIntegerObject(valuePointer))// Arg MUST be an Integer to be a member
			{
				const MWORD byteValue = ObjectMemoryIntegerValueOf(valuePointer);
				if (byteValue < 256)	// Only worth looking for 0..255
				{
					const SMALLINTEGER length = oteBytes->bytesSize();
					// We can only be in here if to>=from, so if to>=1, then => from >= 1
					// furthermore if to <= length then => from <= length
					if (from < 1 || to > length)
						return primitiveFailure(2);

					// Search is in bounds, lets do it
			
					VariantByteObject* bytes = oteBytes->m_location;

					from--;
					while (from < to)
						if (bytes->m_fields[from++] == byteValue)
						{
							answer = ObjectMemoryIntegerObjectOf(from);
							break;
						}
				}
			}
		}
		else
		{
			// Search a pointer object - but only the indexable vars
			
			PointersOTE* oteReceiver = reinterpret_cast<PointersOTE*>(receiverPointer);
			VariantObject* receiver = oteReceiver->m_location;
			Behavior* behavior = receiverPointer->m_oteClass->m_location;
			const MWORD length = oteReceiver->pointersSize();
			const MWORD fixedFields = behavior->m_instanceSpec.m_fixedFields;

			// Similar reasoning with to/from as for byte objects, but here we need to
			// take account of the fixed fields.
			if (from < 1 || (to + fixedFields > length))
				return primitiveFailure(2);	// Out of bounds

			Oop* indexedFields = receiver->m_fields + fixedFields;
			from--;
			while (from < to)
				if (indexedFields[from++] == valuePointer)
				{
					answer = ObjectMemoryIntegerObjectOf(from);
					break;
				}
		}

	}
	else
		answer = ZeroPointer; 		// Range is non-inclusive, cannot be there

	stackValue(3) = answer;
	pop(3);
	return primitiveSuccess();
}

// Initialize the Boyer-Moorer skip array
inline void __stdcall bmInitSkip(const BYTE* p, const int M, int* skip)
{
	for (int j=0;j<256;j++)
		skip[j] = M;
	for (int j=0;j < M;j++)
		skip[p[j]] = M-j-1;
}


// Returns -1 for not found, or zero based index if found
int __stdcall bmSearch(const BYTE* string, const int N, const BYTE* p, const int M, /*const int* skip, */const int startAt)
{
	int skip[256];
	bmInitSkip(p,M,skip);

	const int n = N - startAt;
	const BYTE* a = string + startAt;

	int i, j;
	for (i=j=M-1; j >= 0; j--,i--)
	{
		BYTE c;
		while ((c = a[i]) != p[j])
		{
			const int t = skip[c];
			i += (M-j > t) ? M-j : t;
			if (i >= n) 
				return -1;
			j = M-1;
			_ASSERTE(i >= 0);
			_ASSERTE(j >= 0);
		}
	}
	return i+1+startAt;
}

int __stdcall bruteSearch(const BYTE* a, const int N, const BYTE* p, const int M, const int startAt)
{
	int i, j;
	for (i=startAt,j=0; j < M && i < N;i++,j++)
	{
		if (a[i] != p[j])
		{
			i -= j;
			j = -1;
		}
	}
	return j == M ? i - M : -1;
}

// N.B. startAt is now zero based
inline int __stdcall stringSearch(const BYTE* a, const int N, const BYTE* p, const int M, const int startAt)
{
	// In order for it to be worth initiating the skip array, we have to have enough characters to search
	return N >= 512
		? bmSearch(a, N, p, M, /*NULL, */startAt)
		: bruteSearch(a, N, p, M, startAt);
}


// Uses object identity to locate the next occurrence of the argument in the receiver from
// the specified index to the specified index
BOOL __fastcall Interpreter::primitiveStringSearch()
{
	Oop integerPointer = stackTop();
	if (!ObjectMemoryIsIntegerObject(integerPointer))
		return primitiveFailure(0);				// startingAt not an integer
	const SMALLINTEGER startingAt = ObjectMemoryIntegerValueOf(integerPointer);

	Oop oopSubString = stackValue(1);
	BytesOTE* oteReceiver = reinterpret_cast<BytesOTE*>(stackValue(2));

	if (ObjectMemory::fetchClassOf(oopSubString) != oteReceiver->m_oteClass)
		return primitiveFailure(2);

	// We know it can't be a SmallInteger because it has the same class as the receiver
	BytesOTE* oteSubString = reinterpret_cast<BytesOTE*>(oopSubString);

	VariantByteObject* bytesPattern = oteSubString->m_location;
	VariantByteObject* bytesReceiver = oteReceiver->m_location;
	const int M = oteSubString->bytesSize();
	const int N = oteReceiver->bytesSize();

	// Check 'startingAt' is in range
	if (startingAt < 1 || startingAt > N)
		return primitiveFailure(1);	// out of bounds

	int nOffset = M == 0 || ((startingAt + M) - 1 > N)
					? -1 
					: stringSearch(bytesReceiver->m_fields, N, bytesPattern->m_fields, M, startingAt - 1);
	
	stackValue(2) = ObjectMemoryIntegerObjectOf(nOffset+1);
	pop(2);
	return primitiveSuccess();
}
