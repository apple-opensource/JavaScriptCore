/*
 *  Copyright (C) 2003 Apple Computer, Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <config.h>

// This file exists because JavaScriptCore needs to define the NaN and Inf globals in a way
// that does not use a static initializer so we don't have a framework initialization routine.

// The trick is to define the NaN and Inf globals with a different type than the declaration.
// This trick works because the mangled name of the globals does not include the type, although
// I'm not sure that's guaranteed. There could be alignment issues with this, since arrays of
// characters don't necessarily need the same alignment doubles do, but for now it seems to work.
// It would be good to figure out a 100% clean way that still avoids code that runs at init time.

namespace KJS {

#ifdef WORDS_BIGENDIAN
  extern const unsigned char NaN[sizeof(double)] = { 0x7f, 0xf8, 0, 0, 0, 0, 0, 0 };
  extern const unsigned char Inf[sizeof(double)] = { 0x7f, 0xf0, 0, 0, 0, 0, 0, 0 };
#else
  extern const unsigned char NaN[sizeof(double)] = { 0, 0, 0, 0, 0, 0, 0xf8, 0x7f };
  extern const unsigned char Inf[sizeof(double)] = { 0, 0, 0, 0, 0, 0, 0xf0, 0x7f };
#endif

};
