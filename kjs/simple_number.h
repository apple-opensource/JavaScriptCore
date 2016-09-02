// -*- c-basic-offset: 2 -*-
/*
 *  This file is part of the KDE libraries
 *  Copyright (C) 2003 Apple Computer, Inc
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA 02111-1307, USA.
 *
 */

#ifndef _KJS_SIMPLE_NUMBER_H_
#define _KJS_SIMPLE_NUMBER_H_

#include <limits.h>
#include <math.h>

namespace KJS {
    class ValueImp;

    class SimpleNumber {
    public:
	enum { tag = 1, shift = 2, mask = (1 << shift) - 1, sign = 1 << 31, max = (1 << (31 - shift)) - 1, min = -max - 1 };

	static inline bool is(const ValueImp *imp) { return ((int)imp & mask) == tag; }
	static inline int value(const ValueImp *imp) { return ((int)imp >> shift) | (((int)imp & sign) ? ~max : 0); }

	static inline bool fits(int i) { return i <= max && i >= min; }
	static inline bool fits(unsigned i) { return i <= (unsigned)max; }
	static inline bool fits(long i) { return i <= max && i >= min; }
	static inline bool fits(unsigned long i) { return i <= (unsigned)max; }
	static inline bool fits(double d) { return d <= max && d >= min && d == (double)(int)d; }
	static inline ValueImp *make(int i) { return (ValueImp *)((i << shift) | tag); }
    };
}

#endif
