/******************************************************************************

	File: GC.cpp

	Description:

	Object Memory management class garbage collection routines

	N.B. Some functions are inlined in this module as they are auto-inlined by
	the compiler anyway, and prepending the qualifier tells the compiler that
	it does not need an out-of-line copy for calls from outside the module (there
	aren't any) thus saving a little code space for these private functions.

******************************************************************************/

#include "Ist.h"

#pragma code_seg(GC_SEG)

#include "ObjMem.h"
#include "Interprt.h"

// Smalltalk classes
#include "STBehavior.h"		// We need to check class flags such as indexability, etc,
#include "STArray.h"		// VMPointers (roots) are stored in an Array
#include "STProcess.h"

#define _CRTBLD
#include "winheap.h"
#undef _CRTBLD

// The pointers in const space
extern VMPointers _Pointers;

#ifdef _DEBUG
	#define VERBOSEGC
	static bool ignoreRefCountErrors = false;
#endif

#ifdef VERBOSEGC
	#pragma warning (disable : 4786)
	#include <yvals.h>
	#undef _HAS_EXCEPTIONS
	#include <map>
	typedef std::map<BehaviorOTE*, int> MAPCLASSOTE2INT;
#endif

enum { NoWeakMask = 0, GCNoWeakness = 1 };
static BYTE WeaknessMask;

void ObjectMemory::ClearGCInfo()
{
}

///////////////////////////////////////////////////////////////////////////////

inline Oop ObjectMemory::corpsePointer()
{
	return _Pointers.Corpse;
}

// lastPointerOf includes the object header, sizeBitsOf()/mwordSizeOf() does NOT
inline MWORD lastStrongPointerOf(OTE* ote)
{
	HARDASSERT(ote->isPointers());

#if !defined(_AFX)
	// TODO: Check code generated here, may be slower than previously
	if (ote->flagsAllMask(WeaknessMask))
	{
		const Behavior* behavior = ote->m_oteClass->m_location;
		return ObjectHeaderSize + behavior->fixedFields();
	}
	else
#endif
		return ote->getWordSize();
}

void ObjectMemory::MarkObjectsAccessibleFromRoot(OTE* rootOTE)
{
	BYTE curMark = 	*reinterpret_cast<BYTE*>(&m_spaceOTEBits[OTEFlags::NormalSpace]);
	if ((rootOTE->getFlagsByte() ^ curMark) & OTE::MarkMask)	// Already accessible from roots of world?
		markObjectsAccessibleFrom(rootOTE);
}

void ObjectMemory::markObjectsAccessibleFrom(OTE* ote)
{
	HARDASSERT(!isIntegerObject(ote));
	//HARDASSERT(!hasCurrentMark(ote));
	
	// First toggle the mark bit to the new mark
	markObject(ote);

	BYTE curMark = 	*reinterpret_cast<BYTE*>(&m_spaceOTEBits[OTEFlags::NormalSpace]);

	// The class is always visited, but is now in the OTE which means we may not need
	// to visit the object body at all
	BehaviorOTE* oteClass = ote->m_oteClass;
	if ((oteClass->getFlagsByte() ^ curMark) & OTE::MarkMask)	// Already accessible from roots of world?
		markObjectsAccessibleFrom(reinterpret_cast<POTE>(oteClass));

	if (ote->isBytes())
		return;

	const MWORD lastPointer = lastStrongPointerOf(ote);
	Oop* pFields = reinterpret_cast<Oop*>(ote->m_location);
	for (MWORD i = ObjectHeaderSize; i < lastPointer; i++)
	{
		// This will get nicely optimised by the Compiler
		Oop fieldPointer = pFields[i];
		// Perform tests to see if marking necessary to save a call
		// We don't need to visit SmallIntegers and objects we've already visited
		if (!isIntegerObject(fieldPointer))
		{
			OTE* oteField = reinterpret_cast<OTE*>(fieldPointer);

			// By Xoring current mark mask with existing one we should only get > 1 if they
			// don't actually match, and therefore we haven't visited here yet.
			if ((oteField->getFlagsByte() ^ curMark) & OTE::MarkMask)	// Already accessible from roots of world?
				markObjectsAccessibleFrom(oteField);
		}
	}
}

OTEFlags ObjectMemory::nextMark()
{
	OTEFlags oldMark = m_spaceOTEBits[OTEFlags::NormalSpace];
	// Toggle the "visited" mark - all objects will then have previous mark
	BOOL newMark = oldMark.m_mark ? FALSE : TRUE;
	for (unsigned i=0;i<OTEFlags::NumSpaces;i++)
		m_spaceOTEBits[i].m_mark = newMark;
	return oldMark;
}

void ObjectMemory::asyncGC(DWORD gcFlags)
{
	EmptyZct();
	reclaimInaccessibleObjects(gcFlags);
	PopulateZct();

	Interpreter::scheduleFinalization();
}

void ObjectMemory::reclaimInaccessibleObjects(DWORD gcFlags)
{
	// Assign flags to static, as we use some deeply recursive routines
	// and we don't want to pass down to the depths. When we want to turn off
	// weakness we mask with the free bit, which obviously can't be set on any
	// live object so the test will always fail
	WeaknessMask = static_cast<BYTE>(gcFlags & GCNoWeakness ? OTE::FreeMask : OTE::WeakMask);

#ifdef _DEBUG
	trace("GC: Reclaiming inaccessible objects...\n");
#endif

	// Get the Oop to use for corpses from the interpreter (it's a global)
	Oop corpse = corpsePointer();
	HARDASSERT(!isIntegerObject(corpse));

	#ifndef _AFX
		if (corpse == Oop(_Pointers.Nil))
		{
			tracelock lock(TRACESTREAM);
			TRACESTREAM << "GC: WARNING, attempted GC before Corpse registered." << endl;
			return;	// Refuse to garbage collect if the corpse is invalid
		}
	#else
		// This check is disabled for MFC version, because that does not support
		// weak references anyway
	#endif
	
	#ifdef _DEBUG
		checkReferences();
	#endif

	#ifdef VERBOSEGC
		MAPCLASSOTE2INT lossMap;
	#endif

	// Move to the "next" GC mark (really a toggle). We'll need the old mark to rescue objects
	OTEFlags oldMark = nextMark();
	
	// Starting from the roots of the world, recursively visit all objects which are still reachable
	// along a chain of strong references. We may later need to 'rescue' some unmarked objects
	// reachable from dying objects which are queued for finalization. Should these rescued objects
	// also be finalizable, then this will delay their finalization until their parent has disappeared.
	markObjectsAccessibleFrom(pointerFromIndex(0));
	Interpreter::MarkRoots();

	// Every object reachable from the roots of the world will now have the current mark bit,
	// any objects with the old mark bit can be discarded.

	// Now locate all the unmarked objects, and visit any object referenced from finalizable
	// unmarked objects. Also nil the corpses of any weak objects, and queue them for finalization
	unsigned	nMaxUnmarked = 0, nUnmarked = 0;
	OTE**		pUnmarked = 0;

	const OTE* pEnd = m_pOT+m_nOTSize;							// Loop invariant
	BYTE curMark = 	*reinterpret_cast<BYTE*>(&m_spaceOTEBits[OTEFlags::NormalSpace]);
	for (OTE* ote=m_pOT+OTBase; ote < pEnd; ote++)
	{
		BYTE oteFlags = ote->getFlagsByte();
		if (!(oteFlags & OTE::FreeMask))								// Already free'd?
		{
			// By Xoring current mark mask with existing one we should only get > 1 if they
			// don't actually match 
			if ((oteFlags ^ curMark) & OTE::MarkMask)			// Accessible from roots of world?
			{
				// Inaccessible object found, if finalizable, then we need to rescue it by
				// visiting all the objects it references
				if (nUnmarked == nMaxUnmarked)
				{
					if (nMaxUnmarked == 0)
						nMaxUnmarked = 512;
					else
						nMaxUnmarked = nMaxUnmarked << 1;
					pUnmarked = static_cast<OTE**>(realloc(pUnmarked, nMaxUnmarked*sizeof(OTE*)));
				}
				pUnmarked[nUnmarked++] = ote;

#ifndef _AFX
				// If the object is finalizable, rescue it by visiting all objects accessible from it
				if (oteFlags & OTE::FinalizeMask)
				{
					markObjectsAccessibleFrom(ote);
					// We must ensure that if a finalizable object is circularly referenced, directly
					// or indirectly, that we don't prevent it ever being finalized.
					ote->setMark(oldMark.m_mark);
				}
#endif
			}
		}
	}

#if !defined(_AFX)
	// Another scan to nil out weak references. This has to be a separate scan from the finalization
	// candidate scan so that we don't end up nilling out weak references to objects that are accessible
	// from finalizable objects
	unsigned queuedForBereavement=0;
	for (OTE* ote=m_pOT+OTBase; ote < pEnd; ote++)
	{
		BYTE oteFlags = ote->getFlagsByte();
		if (!(oteFlags & OTE::FreeMask))								// Already free'd?
		{
			// We check all weak objects for bereavements, whether they are marked or not, as they
			// need to be notified of losses, whether or not they're about to disappear themselves,
			// in case they need to take appropriate action.
			if ((oteFlags & WeaknessMask) == WeaknessMask)
			{
				#ifdef _AFX
					HARDASSERT(FALSE);
				#endif
				HARDASSERT((oteFlags & OTE::WeakMask) == OTE::WeakMask);
				// Yeehaa, let's add some Corpses
				SMALLINTEGER losses = 0;
				PointersOTE* otePointers = reinterpret_cast<PointersOTE*>(ote);
				const MWORD size = otePointers->pointersSize();
				VariantObject* weakObj = otePointers->m_location;
				const Behavior* weakObjClass = ote->m_oteClass->m_location;
				const MWORD fixedFields = weakObjClass->fixedFields();
				for (MWORD j=fixedFields;j<size;j++)
				{
					Oop fieldPointer = weakObj->m_fields[j];
					if (!ObjectMemoryIsIntegerObject(fieldPointer))
					{
						OTE* fieldOTE = reinterpret_cast<OTE*>(fieldPointer);
						BYTE fieldFlags = fieldOTE->getFlagsByte();
						if (fieldFlags & OTE::FreeMask)
						{
							#if defined(_DEBUG) && 0
								TRACESTREAM << "Weakling " << ote << " loses reference to freed object " <<
									(UINT)fieldOTE << "/" << indexOfObject(fieldOTE) << endl;
							#endif
							
							weakObj->m_fields[j] = corpse;
							losses++;
						}
						else if ((fieldFlags ^ curMark) & OTE::MarkMask)
						{
							HARDASSERT(!fieldOTE->hasCurrentMark());
							// We must correctly maintain ref. count of dying object,
							// just in case it is in (or will be in) the finalization queue
							#if defined(_DEBUG) && 0
								TRACESTREAM << "Weakling " << ote << " loses reference to " <<
									fieldOTE << "(" << (UINT)fieldOTE << "/" << indexOfObject(fieldOTE) << " refs " <<
									int(ote->m_flags.m_count) << ")" << endl;
							#endif	
							decRefs(fieldOTE);
							weakObj->m_fields[j] = corpse;
							losses++;
						}
					}
				}

				// If any bereavements were suffered, then inform the weak object
				if (losses && weakObjClass->isMourner())
				{
					queuedForBereavement++;
					Interpreter::queueForBereavementOf(ote, integerObjectOf(losses));
					#ifdef _DEBUG
					{
						tracelock lock(TRACESTREAM);
						TRACESTREAM << "Weakling: " << ote << " (" << UINT(ote) << " lost " << losses << " elements" << endl;
					}
					#endif
					// We must also ensure that it and its referenced objects are marked since we're
					// rescuing it.
					markObjectsAccessibleFrom(ote);
				}
			}
		}
	}
#endif	// !defined(_AFX)

	#ifdef _DEBUG
	{
		// Ensure the permanent objects have the current mark too
		const OTE* pEndPerm = m_pOT + NumPermanent;
		for (OTE* ote = m_pOT; ote < pEndPerm; ote++)
			markObject(ote);
	}
	#endif

	// Now sweep through the unmarked objects, and finalize/deallocate any objects which are STILL
	// unmarked
	unsigned deletions=0;
	unsigned queuedForFinalize=0;
	const unsigned loopEnd = nUnmarked;
	for (unsigned i=0;i<loopEnd;i++)
	{
		OTE* ote = pUnmarked[i];
		BYTE oteFlags = ote->getFlagsByte();
		HARDASSERT(!(oteFlags & OTE::FreeMask));
		if ((oteFlags ^ curMark) & OTE::MarkMask)	// Still unmarked?
		{
			// Object still unmarked, so either deallocate it OR queue it for finalization
			HARDASSERT(!ote->hasCurrentMark());
#ifndef _AFX
			// We found a dying object, finalize it if necessary
			if (oteFlags & OTE::FinalizeMask)
			{
				#if 0//def _DEBUG
					TRACESTREAM << "Finalizing " << ote << endl;
				#endif

				Interpreter::basicQueueForFinalization(ote);
				// Prevent a second finalization
				ote->beUnfinalizable();
				// We must ensure the object has the current mark so that it doesn't cock up the
				// next GC in case it survives that long
				markObject(ote);
				queuedForFinalize++;
			}
			else
#endif // _AFX
			{
				// It doesn't want finalizing, so we can free it
				// Countdown all refs from objects which are to be
				// deallocated - but not recursively, since marking
				// has already identified ALL objects which need to
				// be freed - thus maintaining correct reference counts
				// on objects which survive the garbage collect

				// First we remove the reference to the class
				BehaviorOTE* classPointer = ote->m_oteClass;

				#ifdef VERBOSEGC
					lossMap[classPointer]++;
				#endif

				if (classPointer->isFree())
				{
					#ifdef _DEBUG
					{
						tracelock lock(TRACESTREAM);
						TRACESTREAM << "GC WARNING: " << LPVOID(ote) << '/' << i
							<< " (size " << ote->getSize() << ") has freed class " 
							<< LPVOID(classPointer) << '/' << classPointer->getIndex() << endl; 
					}
					#endif
				}
				else
				{
					classPointer->decRefs();
				}

				// If not a pointer object, then nothing further to do
				if (ote->isPointers())
				{
					PointersOTE* otePointers = reinterpret_cast<PointersOTE*>(ote);
					const MWORD lastPointer = otePointers->pointersSize();
					VariantObject* varObj = otePointers->m_location;
					for (unsigned f = 0; f < lastPointer; f++)
					{
						Oop fieldPointer = varObj->m_fields[f];
						if (!isIntegerObject(fieldPointer))
						{
							OTE* fieldOTE = reinterpret_cast<OTE*>(fieldPointer);
							// Could have been previously deleted during GC
							if (!fieldOTE->isFree())
								fieldOTE->decRefs();
						}
					}
				}

				// We must ensure count really zero as some deallocation routines may not do this
				// (normally objects are only deallocated when the count hits zero)
				ote->m_flags.m_count = 0;
				deallocate(ote);
				deletions++;
			}
		}
	}

	free(pUnmarked);

	#ifdef VERBOSEGC
	{
		for (MAPCLASSOTE2INT::iterator it=lossMap.begin(); it != lossMap.end(); it++)
		{
			BehaviorOTE* classPointer = (*it).first;
			int val = (*it).second;
			{
				tracelock lock(TRACESTREAM);
				if (classPointer->isFree())
					TRACESTREAM << "GC: " << val << " objects of a free'd class ("
						<< LPVOID(classPointer) << '(' << classPointer->getIndex() << ") were deallocated" << endl;
				else
					TRACESTREAM << "GC: " << dec << val << ' ' << classPointer << "'s were deallocated" << endl;
			}
		}

		lossMap.clear();
	}
	#endif

	__sbh_heapmin();

	#ifdef _DEBUG
		checkReferences();
	#endif

#if defined(VERBOSEGC) && !defined(_AFX)
	{
		tracelock lock(TRACESTREAM);
		TRACESTREAM << "GC: Completed, " << deletions << " objects reclaimed, "
				<< queuedForFinalize << " queued for finalization, "
				<< queuedForBereavement << " weak lose elements" << endl;
	}
#endif
}

void ObjectMemory::addVMRefs()
{
	// Deliberately max out ref. counts of VM ref'd objects so that ref. counting ops 
	// not needed
	Array* globalPointers = (Array*)&_Pointers;
	const unsigned loopEnd = NumPointers;
	for (unsigned i=0;i<loopEnd;i++)
	{
		Oop obj = globalPointers->m_elements[i];
		if (!isIntegerObject(obj))
			reinterpret_cast<OTE*>(obj)->beSticky();
	}
}

#ifdef _DEBUG

	void ObjectMemory::checkPools()
	{
		const unsigned loopEnd = m_nOTSize;
		for (unsigned i=OTBase;i<loopEnd;i++)
		{
			OTE& ote = m_pOT[i];
			if (!ote.isFree())
			{
				OTEFlags::Spaces space = ote.heapSpace();
				if (space == OTEFlags::PoolSpace)
				{
					unsigned size = ote.sizeOf();
					if (size > MaxSizeOfPoolObject)
					{
						if (size <= MaxSmallObjectSize)
							HARDASSERT(__sbh_find_block(ote.m_location))
						else
						{
							tracelock lock(TRACESTREAM);
							TRACESTREAM << "Found large object (size = " << size 
								<< ") in space " << (int)space 
								<< ": " << &ote << endl;
						}
					}
					else
						if (size != 0)
							HARDASSERT(spacePoolForSize(size).isMyChunk(ote.m_location))
				}
			}
		}
		for (int j=0;j<NumPools;j++)
			HARDASSERT(m_pools[j].isValid());
	}

	int ObjectMemory::CountFreeOTEs()
	{
		OTE*	p = m_pFreePointerList;
		int		count = 0;
		OTE*	offEnd= m_pOT + m_nOTSize;
		while (p < offEnd)
		{
			count++;
			p = reinterpret_cast<OTE*>(p->m_location);
		}
		return count;
	}

	void ObjectMemory::checkStackRefs()
	{
		int zeroCountNotInZct = 0;
		Process* pProcess = Interpreter::m_registers.m_pActiveProcess;
		Oop* sp = Interpreter::m_registers.m_stackPointer;
		for (Oop* pOop = pProcess->m_stack;pOop <= sp;pOop++)
		{
			Oop oop = *pOop;
			if (!isIntegerObject(oop))
			{
				OTE* ote = reinterpret_cast<OTE*>(oop);
				if (ote->m_flags.m_count == 0 && !IsInZct(ote))
				{
					TRACESTREAM << "WARNING: Zero count Oop not in Zct: " << ote << endl;
					zeroCountNotInZct++;
				}
			}
		}
		HARDASSERT(zeroCountNotInZct == 0);
	}

	void ObjectMemory::checkReferences()
	{
		// If this assertion fires, then something has written off the end
		// of an object, and corrupted the heap. Possibilities to consider are:
		//	1)	Modification of a class (e.g. addition/removal of instance
		//		variables). The Behavior code in Snailtalk is still
		//		not very safe, and frequently fails to modify information
		//		about the 'shape' of classes in a running system. The solution
		//		is probably to reboot.
		//	2)	An object passed to some external API as an 'lpvoid' has the
		//		wrong size, or some error caused writing off either of its ends
		//	3)	Stack overflow (probably due to a recursive loop going too deep),
		//		though it this occurs it will likely cause a failure somewhat
		//		earlier (mostly, it seems, in either activateNewMethod 
		//		or returnValueTo) in BYTEASM.ASM).
		//
		#ifdef PRIVATE_HEAP
			if (Interpreter::executionTrace > 3)
				HARDASSERT(::HeapValidate(m_hHeap, 0, 0));
		#endif

//		HARDASSERT(_CrtCheckMemory());
		HARDASSERT(__sbh_heap_check() >= 0);
		//checkPools();

		HARDASSERT(m_nFreeOTEs == CountFreeOTEs());

		Interpreter::GrabAsyncProtect();

		// Now adjust for the current active process, depending on whether the ZCT has been reconciled or not
		if (!IsReconcilingZct())
		{
			checkStackRefs();
			Interpreter::IncStackRefs();
		}
	
		int errors=0;
		BYTE* currentRefs = new BYTE[m_nOTSize];
		{
			const unsigned loopEnd = m_nOTSize;
			for (unsigned i=OTBase; i < loopEnd; i++)
			{
				// Count and free bit should both be zero, or both non-zero
				/*if (m_pOT[i].m_flags.m_free ^ (m_pOT[i].m_flags.m_count == 0))
				{
					TRACESTREAM << "WARNING: ";
					Oop oop = pointerFromIndex(i);
					Interpreter::printObject(oop, TRACESTREAM);
					TRACESTREAM << " (Oop " << oop << "/" << i << ") has refs " << 
							m_pOT[i].m_flags.m_count << endl;
					//errors++;
				}*/
				OTE* ote = &m_pOT[i];
				if (!ote->isFree() && ote->heapSpace() == OTEFlags::PoolSpace)
					HARDASSERT(ote->sizeOf() <= MaxSmallObjectSize);
				currentRefs[i] = ote->m_flags.m_count;
				ote->m_flags.m_count = 0;
			}
		}

		// Recalc the references
		const OTE* pEnd = m_pOT+m_nOTSize;
		int nFree = 0;
		for (OTE* ote=m_pOT; ote < pEnd; ote++)
		{
			if (!ote->isFree())
				addRefsFrom(ote);
			else
				nFree++;
		}

		POTE poteFree = m_pFreePointerList;
		int cFreeList = 0;
		while (poteFree < pEnd)
		{
			++cFreeList;
			poteFree = reinterpret_cast<OTE*>(poteFree->m_location);
		}

		//TRACESTREAM << nFree << " free slots found in OT, " << cFreeList << " on the free list (" << nFree-cFreeList << ")" <<endl;

		Interpreter::ReincrementVMReferences();

		int refCountTooSmall = 0;
		const unsigned loopEnd = m_nOTSize;
		for (unsigned i=OTBase; i < loopEnd; i++)
		{
			OTE* ote = &m_pOT[i];
			if (currentRefs[i] < OTE::MAXCOUNT)
			{
				if (currentRefs[i] != ote->m_flags.m_count)
				{
					bool tooSmall = currentRefs[i] < ote->m_flags.m_count;

					tracelock lock(TRACESTREAM);

					if (tooSmall)
					{
						refCountTooSmall++;
						TRACESTREAM << "ERROR: ";
					}
					else
						TRACESTREAM << "WARNING: ";
					
					TRACESTREAM << ote << " (Oop " << LPVOID(ote) << "/" << i << ") had refs " << dec << (int)currentRefs[i] 
							<< " should be " << int(ote->m_flags.m_count) << endl;
					errors++;

					if (tooSmall)
					{
						if (!Interpreter::m_bAsyncGCDisabled)
						{
							TRACESTREAM << " Referenced From:" << endl;
							ArrayOTE* oteRefs = ObjectMemory::referencesTo(reinterpret_cast<Oop>(ote));
							Array* refs = oteRefs->m_location;
							for (unsigned i=0;i<oteRefs->pointersSize();i++)
								TRACESTREAM << "  " << reinterpret_cast<OTE*>(refs->m_elements[i]) << endl;
							deallocate(reinterpret_cast<OTE*>(oteRefs));
						}
					}
					
					if (Interpreter::m_bAsyncGCDisabled)
					{
						// If compiler is running then async GCs are disabled, and we should leave the ref. counts
						// that are too high unaffected
						ote->m_flags.m_count = currentRefs[i];
					}
				} else if (currentRefs[i] == 0 && !ote->isFree() && !IsInZct(ote))
				{
					// Shouldn't be zero count objects around that are not in the Zct
					TRACESTREAM << ote << " (Oop " << LPVOID(ote) << "/" << i << ") had zero refs" << endl;
					errors++;

					if (!Interpreter::m_bAsyncGCDisabled && !IsReconcilingZct())
					{
						BOOL save = Interpreter::executionTrace;
						Interpreter::executionTrace = 1;
						recursiveFree(ote);
						Interpreter::executionTrace = save;
					}
				}
			}
			else
			{
				// Never modify the ref. count of a sticky object - let the GC collect these
				ote->beSticky();
			}
		}

		// Now remove refs from the current active process that we added before checking refs
		if (!IsReconcilingZct())
		{
			// We have to be careful not to cause more entries to be placed in the Zct, so we need to inline this
			// operation and just count down the refs and not act when they drop to zero
			Process* pProcess = Interpreter::m_registers.m_pActiveProcess;
			Oop* sp = Interpreter::m_registers.m_stackPointer;
			for (Oop* pOop = pProcess->m_stack;pOop <= sp;pOop++)
				ObjectMemory::decRefs(*pOop);

			checkStackRefs();
		}

		Interpreter::RelinquishAsyncProtect();

		HARDASSERT(Interpreter::m_bAsyncGCDisabled || !refCountTooSmall);
		delete[] currentRefs;
		if (errors)
		{
			HARDASSERT(Interpreter::m_bAsyncGCDisabled || ignoreRefCountErrors || ((errors - refCountTooSmall) == 0));
			// If we don't do this, the proc. may have wrong size for GC

			Interpreter::resizeActiveProcess();
		}
	}

	void ObjectMemory::addRefsFrom(OTE* ote)
	{
		HARDASSERT(ote >= m_pOT);
		// We must also inc. ref. count on the class here
		ote->m_oteClass->countUp();

		if (ote->isPointers())
		{
			PointersOTE* otePointers = reinterpret_cast<PointersOTE*>(ote);
			VariantObject* varObj = otePointers->m_location;
			const MWORD lastPointer = otePointers->pointersSize();
			for (MWORD i = 0; i < lastPointer; i++)
			{
				Oop fieldPointer = varObj->m_fields[i];
				// The reason we don't use an ASSERT here is that, ASSERT throws
				// up a message box which causes a callback into Smalltalk to process
				// the window messages, which is not permissible during the execution
				// of a GC
				if (!isIntegerObject(fieldPointer))
				{
					OTE* fieldOTE = reinterpret_cast<OTE*>(fieldPointer);
					if (fieldOTE < m_pOT 
							|| fieldOTE > m_pOT+m_nOTSize-1 
							|| fieldOTE->isFree())
					{
						HARDASSERT(FALSE);					// Fires if fieldPointer bad
						varObj->m_fields[i] = Oop(_Pointers.Nil);		// Repair the damage
					}
				}
				countUp(fieldPointer);
			}
		}
	}
#endif	// Debug
