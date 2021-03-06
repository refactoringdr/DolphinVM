/******************************************************************************

	File: STMagnitude.h

	Description:

	VM representation of Smalltalk abstract magnitude classes.

	N.B. Some of the classes here defined are well known to the VM, and must not
	be modified in the image. Note also that these classes may also have
	a representation in the assembler modules (so see istasm.inc)

******************************************************************************/

#ifndef _IST_STMAGNITUDE_H_
#define _IST_STMAGNITUDE_H_

#include "STObject.h"

class Magnitude //: public Object
{
public:
	enum { FixedSize = 0 };
};

class ArithmeticValue  : public Magnitude
{
};

class Number : public ArithmeticValue
{
};

#endif