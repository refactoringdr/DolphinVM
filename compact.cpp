/******************************************************************************

	File: Compact.cpp

	Description:

	Object Memory management class compacting GC functions

	N.B. Some functions are inlined in this module as they are auto-inlined by
	the compiler anyway, and prepending the qualifier tells the qualifier that
	it does not need an out-of-line copy for calls from outside the module (there
	aren't any) thus saving a little code space for these private functions.

******************************************************************************/

#include "Ist.h"

#pragma code_seg(GC_SEG)

#include "ObjMem.h"
#include "Interprt.h"

#pragma auto_inline(off)

// Answer the index of the last occuppied OT entry
unsigned __stdcall ObjectMemory::lastOTEntry()
{
	HARDASSERT(m_pOT);
//	HARDASSERT(m_nInCritSection > 0);

	unsigned i = m_nOTSize-1;
	const OTE* pOT = m_pOT;
	while (pOT[i].isFree())
	{
		ASSERT(i >= 0);
		i--;
	}

	return i;
}


// Compact an object by updating all the Oops it contains using the
// forwarding pointers in the old OT.
void ObjectMemory::compactObject(OTE* ote)
{
	// We shouldn't come in here unless OTE already fixed for this object
	HARDASSERT(ote >= m_pOT && ote < m_pFreePointerList);

	// First fix up the class (remember that the new object pointer is stored in the
	// old one's object location slot
	compactOop(ote->m_oteClass);

	if (ote->isPointers())
	{
		VariantObject* varObj = static_cast<VariantObject*>(ote->m_location);
		const MWORD lastPointer = ote->pointersSize();
		for (MWORD i = 0; i < lastPointer; i++)
		{
			// This will get nicely optimised by the Compiler
			Oop fieldPointer = varObj->m_fields[i];

			// We don't need to visit SmallIntegers and objects we've already visited
			if (!isIntegerObject(fieldPointer))
			{
				OTE* fieldOTE = reinterpret_cast<OTE*>(fieldPointer);
				// If pointing at a free'd object ,then it has been moved
				if (fieldOTE->isFree())
				{
					// Should be one of the old OT entries, pointing outside the o
					Oop movedTo = reinterpret_cast<Oop>(fieldOTE->m_location);
					HARDASSERT(movedTo >= (Oop)m_pOT && movedTo < (Oop)m_pFreePointerList);
					// Get the new OTE from the old ...
					varObj->m_fields[i] = movedTo;
				}
			}
		}
	}
	// else, we don't even need to look at the body of byte objects any more
}

// Perform a compacting GC
size_t ObjectMemory::compact()
{
	TRACE("OT size %d. Compacting...\n", m_nOTSize);
	EmptyZct();

	// First perform a normal GC
	reclaimInaccessibleObjects(GCNormal);

	Interpreter::freePools();

	// Walk the OT from the bottom to locate free entries, and from the top to locate candidates to move
	// 

	size_t moved = 0;
	OTE* last = m_pOT + m_nOTSize - 1;
	OTE* first = m_pOT;
#pragma warning (disable : 4127)
	while(true)
#pragma warning (default : 4127)
	{
		// Look for a tail ender
		while (last > first && last->isFree())
			last--;
		// Look for a free slot
		while (first < last && !first->isFree())
			first++;
		if (first == last)
			break;	// Met in the middle, we're done
		
		HARDASSERT(first->isFree());
		HARDASSERT(!last->isFree());

		// Copy the tail ender over the free slot
		*first = *last;
		moved++;
		// Leave forwarding pointer in the old slot
		last->m_location = first;
		last->beFree();
		last->m_flags.m_count = 0;
		// Advance last as we've moved this slot
		last--;
	}

	HARDASSERT(last == first);
	// At this point, last == first, and the first free slot will be that after last

	TRACE("%d OTEs compacted\n", moved);

	// Now we can update the objects using the forwarding pointers in the old slots

	// We must remove the const. spaces memory protect for the duration of the pointer update
	ProtectConstSpace(PAGE_READWRITE);

	// New head of free list is first OTE after the single contiguous block of used OTEs
	// Need to set this before compacting as 
	m_pFreePointerList = last+1;

	// Now run through the new OT and update the Oops in the objects
	OTE* pOTE = m_pOT;
	while (pOTE <= last)
	{
		compactObject(pOTE);
		pOTE++;
	}

	// Note that this copies VMPointers to cache area
	ProtectConstSpace(PAGE_READONLY);

	#ifdef _DEBUG
		m_nFreeOTEs = m_pOT + m_nOTSize - m_pFreePointerList;
	#endif

	// We must inform the interpreter that it needs to update any cached Oops from the forward pointers
	// before we rebuild the free list (which will destroy those pointers to the new OTEs)
	Interpreter::OnCompact();

	// The last used slot will be the slot before the first entry in the free list
	// Using this, round up from the last used slot to the to commit granularity, then uncommit any later slots
	// 
	TODO("Free whole pages beyond the start of the free pointer list here as free entries now contiguous")

	// Now fix up the free list
	const OTE* end = m_pOT + m_nOTSize;
	OTE* cur = m_pFreePointerList;
	while (cur < end)
	{
		HARDASSERT(cur->isFree());
		cur->m_location = cur + 1;
		cur++;
	}

	// Could do this before or after check refs, since that can account for Zct state
	PopulateZct();

	CHECKREFERENCES

	HeapCompact();

//	if (m_pFreePointerList == NULL)
//		_asm int 3;

	//trace("... compacted. OT size %d.\n", m_nOTSize);

	Interpreter::scheduleFinalization();

	return m_pFreePointerList - m_pOT;
}
