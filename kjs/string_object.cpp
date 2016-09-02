// -*- c-basic-offset: 2 -*-
/*
 *  This file is part of the KDE libraries
 *  Copyright (C) 1999-2001 Harri Porten (porten@kde.org)
 *  Copyright (C) 2004 Apple Computer, Inc.
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

#include "value.h"
#include "object.h"
#include "types.h"
#include "interpreter.h"
#include "operations.h"
#include "regexp.h"
#include "regexp_object.h"
#include "string_object.h"
#include "error_object.h"
#include <stdio.h>
#include "string_object.lut.h"

using namespace KJS;

// ------------------------------ StringInstanceImp ----------------------------

const ClassInfo StringInstanceImp::info = {"String", 0, 0, 0};

StringInstanceImp::StringInstanceImp(ObjectImp *proto)
  : ObjectImp(proto)
{
  setInternalValue(String(""));
}

StringInstanceImp::StringInstanceImp(ObjectImp *proto, const UString &string)
  : ObjectImp(proto)
{
  setInternalValue(String(string));
}

Value StringInstanceImp::get(ExecState *exec, const Identifier &propertyName) const
{
  if (propertyName == lengthPropertyName)
    return Number(internalValue().toString(exec).size());

  bool ok;
  const unsigned index = propertyName.toArrayIndex(&ok);
  if (ok) {
    const UString s = internalValue().toString(exec);
    const unsigned length = s.size();
    if (index >= length)
      return Undefined();
    const UChar c = s[index];
    return String(UString(&c, 1));
  }

  return ObjectImp::get(exec, propertyName);
}

void StringInstanceImp::put(ExecState *exec, const Identifier &propertyName, const Value &value, int attr)
{
  if (propertyName == lengthPropertyName)
    return;
  ObjectImp::put(exec, propertyName, value, attr);
}

bool StringInstanceImp::hasProperty(ExecState *exec, const Identifier &propertyName) const
{
  if (propertyName == lengthPropertyName)
    return true;

  bool ok;
  const unsigned index = propertyName.toArrayIndex(&ok);
  if (ok) {
    const unsigned length = internalValue().toString(exec).size();
    if (index < length)
      return true;
  }

  return ObjectImp::hasProperty(exec, propertyName);
}

bool StringInstanceImp::deleteProperty(ExecState *exec, const Identifier &propertyName)
{
  if (propertyName == lengthPropertyName)
    return false;
  return ObjectImp::deleteProperty(exec, propertyName);
}

// ------------------------------ StringPrototypeImp ---------------------------
const ClassInfo StringPrototypeImp::info = {"String", &StringInstanceImp::info, &stringTable, 0};
/* Source for string_object.lut.h
@begin stringTable 26
  toString		StringProtoFuncImp::ToString	DontEnum|Function	0
  valueOf		StringProtoFuncImp::ValueOf	DontEnum|Function	0
  charAt		StringProtoFuncImp::CharAt	DontEnum|Function	1
  charCodeAt		StringProtoFuncImp::CharCodeAt	DontEnum|Function	1
  concat		StringProtoFuncImp::Concat	DontEnum|Function	1
  indexOf		StringProtoFuncImp::IndexOf	DontEnum|Function	1
  lastIndexOf		StringProtoFuncImp::LastIndexOf	DontEnum|Function	1
  match			StringProtoFuncImp::Match	DontEnum|Function	1
  replace		StringProtoFuncImp::Replace	DontEnum|Function	2
  search		StringProtoFuncImp::Search	DontEnum|Function	1
  slice			StringProtoFuncImp::Slice	DontEnum|Function	2
  split			StringProtoFuncImp::Split	DontEnum|Function	2
  substr		StringProtoFuncImp::Substr	DontEnum|Function	2
  substring		StringProtoFuncImp::Substring	DontEnum|Function	2
  toLowerCase		StringProtoFuncImp::ToLowerCase	DontEnum|Function	0
  toUpperCase		StringProtoFuncImp::ToUpperCase	DontEnum|Function	0
  toLocaleLowerCase	StringProtoFuncImp::ToLocaleLowerCase DontEnum|Function	0
  toLocaleUpperCase     StringProtoFuncImp::ToLocaleUpperCase DontEnum|Function	0
#
# Under here: html extension, should only exist if KJS_PURE_ECMA is not defined
# I guess we need to generate two hashtables in the .lut.h file, and use #ifdef
# to select the right one... TODO. #####
  big			StringProtoFuncImp::Big		DontEnum|Function	0
  small			StringProtoFuncImp::Small	DontEnum|Function	0
  blink			StringProtoFuncImp::Blink	DontEnum|Function	0
  bold			StringProtoFuncImp::Bold	DontEnum|Function	0
  fixed			StringProtoFuncImp::Fixed	DontEnum|Function	0
  italics		StringProtoFuncImp::Italics	DontEnum|Function	0
  strike		StringProtoFuncImp::Strike	DontEnum|Function	0
  sub			StringProtoFuncImp::Sub		DontEnum|Function	0
  sup			StringProtoFuncImp::Sup		DontEnum|Function	0
  fontcolor		StringProtoFuncImp::Fontcolor	DontEnum|Function	1
  fontsize		StringProtoFuncImp::Fontsize	DontEnum|Function	1
  anchor		StringProtoFuncImp::Anchor	DontEnum|Function	1
  link			StringProtoFuncImp::Link	DontEnum|Function	1
@end
*/
// ECMA 15.5.4
StringPrototypeImp::StringPrototypeImp(ExecState *exec,
                                       ObjectPrototypeImp *objProto)
  : StringInstanceImp(objProto)
{
  Value protect(this);
  // The constructor will be added later, after StringObjectImp has been built
  putDirect(lengthPropertyName, NumberImp::zero(), DontDelete|ReadOnly|DontEnum);

}

Value StringPrototypeImp::get(ExecState *exec, const Identifier &propertyName) const
{
  return lookupGetFunction<StringProtoFuncImp, StringInstanceImp>( exec, propertyName, &stringTable, this );
}

// ------------------------------ StringProtoFuncImp ---------------------------

StringProtoFuncImp::StringProtoFuncImp(ExecState *exec, int i, int len)
  : InternalFunctionImp(
    static_cast<FunctionPrototypeImp*>(exec->lexicalInterpreter()->builtinFunctionPrototype().imp())
    ), id(i)
{
  Value protect(this);
  putDirect(lengthPropertyName, len, DontDelete|ReadOnly|DontEnum);
}

bool StringProtoFuncImp::implementsCall() const
{
  return true;
}

static inline bool regExpIsGlobal(RegExpImp *regExp, ExecState *exec)
{
    Value globalProperty = regExp->get(exec,"global");
    return globalProperty.type() != UndefinedType && globalProperty.toBoolean(exec);
}

static inline void expandSourceRanges(UString::Range * & array, int& count, int& capacity)
{
  int newCapacity;
  if (capacity == 0) {
    newCapacity = 16;
  } else {
    newCapacity = capacity * 2;
  }

  UString::Range *newArray = new UString::Range[newCapacity];
  for (int i = 0; i < count; i++) {
    newArray[i] = array[i];
  }

  delete [] array;

  capacity = newCapacity;
  array = newArray;
}

static void pushSourceRange(UString::Range * & array, int& count, int& capacity, UString::Range range)
{
  if (count + 1 > capacity)
    expandSourceRanges(array, count, capacity);

  array[count] = range;
  count++;
}

static inline void expandReplacements(UString * & array, int& count, int& capacity)
{
  int newCapacity;
  if (capacity == 0) {
    newCapacity = 16;
  } else {
    newCapacity = capacity * 2;
  }

  UString *newArray = new UString[newCapacity];
  for (int i = 0; i < count; i++) {
    newArray[i] = array[i];
  }
  
  delete [] array;

  capacity = newCapacity;
  array = newArray;
}

static void pushReplacement(UString * & array, int& count, int& capacity, UString replacement)
{
  if (count + 1 > capacity)
    expandReplacements(array, count, capacity);

  array[count] = replacement;
  count++;
}

static inline UString substituteBackreferences(const UString &replacement, const UString &source, int **ovector, RegExp *reg)
{
  UString substitutedReplacement = replacement;

  bool converted;

  for (int i = 0; (i = substitutedReplacement.find(UString("$"), i)) != -1; i++) {
    if (i+1 < substitutedReplacement.size() && substitutedReplacement[i+1] == '$') {  // "$$" -> "$"
      substitutedReplacement = substitutedReplacement.substr(0,i) + "$" + substitutedReplacement.substr(i+2);
      continue;
    }
    // Assume number part is one char exactly
    unsigned long backrefIndex = substitutedReplacement.substr(i+1,1).toULong(&converted, false /* tolerate empty string */);
    if (converted && backrefIndex <= (unsigned)reg->subPatterns()) {
      int backrefStart = (*ovector)[2*backrefIndex];
      int backrefLength = (*ovector)[2*backrefIndex+1] - backrefStart;
      substitutedReplacement = substitutedReplacement.substr(0,i)
        + source.substr(backrefStart, backrefLength)
        + substitutedReplacement.substr(i+2);
      i += backrefLength - 1; // -1 offsets i++
    }
  }

  return substitutedReplacement;
}

static Value replace(ExecState *exec, const UString &source, const Value &pattern, const Value &replacement)
{
  if (pattern.type() == ObjectType && pattern.toObject(exec).inherits(&RegExpImp::info)) {
    RegExpImp* imp = static_cast<RegExpImp *>( pattern.toObject(exec).imp() );
    RegExp *reg = imp->regExp();
    bool global = regExpIsGlobal(imp, exec);

    RegExpObjectImp* regExpObj = static_cast<RegExpObjectImp*>(exec->lexicalInterpreter()->builtinRegExp().imp());

    UString replacementString = replacement.toString(exec);

    int matchIndex = 0;
    int lastIndex = 0;
    int startPosition = 0;

    UString::Range *sourceRanges = 0;
    int sourceRangeCount = 0;
    int sourceRangeCapacity = 0;
    UString *replacements = 0;
    int replacementCount = 0;
    int replacementCapacity = 0;

    // This is either a loop (if global is set) or a one-way (if not).
    do {
      int **ovector = regExpObj->registerRegexp( reg, source );
      UString matchString = reg->match(source, startPosition, &matchIndex, ovector);
      regExpObj->setSubPatterns(reg->subPatterns());
      if (matchIndex == -1)
        break;
      int matchLen = matchString.size();

      pushSourceRange(sourceRanges, sourceRangeCount, sourceRangeCapacity, UString::Range(lastIndex, matchIndex - lastIndex));

      UString substitutedReplacement = substituteBackreferences(replacementString, source, ovector, reg);
      pushReplacement(replacements, replacementCount, replacementCapacity, substitutedReplacement);

      lastIndex = matchIndex + matchLen;
      startPosition = lastIndex;

      // special case of empty match
      if (matchLen == 0) {
        startPosition++;
        if (startPosition > source.size())
          break;
      }
    } while (global);

    if (lastIndex < source.size())
      pushSourceRange(sourceRanges, sourceRangeCount, sourceRangeCapacity, UString::Range(lastIndex, source.size() - lastIndex));

    UString result = source.spliceSubstringsWithSeparators(sourceRanges, sourceRangeCount, replacements, replacementCount);

    delete [] sourceRanges;
    delete [] replacements;

    return String(result);
  } else { // First arg is a string
    UString patternString = pattern.toString(exec);
    int matchPos = source.find(patternString);
    int matchLen = patternString.size();
    // Do the replacement
    if (matchPos == -1)
      return String(source);
    else {
      return String(source.substr(0, matchPos) + replacement.toString(exec) + source.substr(matchPos + matchLen));
    }
  }
}

// ECMA 15.5.4.2 - 15.5.4.20
Value StringProtoFuncImp::call(ExecState *exec, Object &thisObj, const List &args)
{
  Value result;

  // toString and valueOf are no generic function.
  if (id == ToString || id == ValueOf) {
    if (thisObj.isNull() || !thisObj.inherits(&StringInstanceImp::info)) {
      Object err = Error::create(exec,TypeError);
      exec->setException(err);
      return err;
    }

    return String(thisObj.internalValue().toString(exec));
  }

  UString u, u2, u3;
  int pos, p0, i;
  double dpos;
  double d = 0.0;

  UString s = thisObj.toString(exec);

  int len = s.size();
  Value a0 = args[0];
  Value a1 = args[1];

  switch (id) {
  case ToString:
  case ValueOf:
    // handled above
    break;
  case CharAt:
    // Other browsers treat an omitted parameter as 0 rather than NaN.
    // That doesn't match the ECMA standard, but is needed for site compatibility.
    dpos = a0.isA(UndefinedType) ? 0 : a0.toInteger(exec);
    if (dpos >= 0 && dpos < len) // false for NaN
      u = s.substr(static_cast<int>(dpos), 1);
    else
      u = "";
    result = String(u);
    break;
  case CharCodeAt:
    // Other browsers treat an omitted parameter as 0 rather than NaN.
    // That doesn't match the ECMA standard, but is needed for site compatibility.
    dpos = a0.isA(UndefinedType) ? 0 : a0.toInteger(exec);
    if (dpos >= 0 && dpos < len) // false for NaN
      d = s[static_cast<int>(dpos)].unicode();
    else
      d = NaN;
    result = Number(d);
    break;
  case Concat: {
    ListIterator it = args.begin();
    for ( ; it != args.end() ; ++it) {
        s += it->dispatchToString(exec);
    }
    result = String(s);
    break;
  }
  case IndexOf:
    u2 = a0.toString(exec);
    if (a1.type() == UndefinedType)
      dpos = 0;
    else {
      dpos = a1.toInteger(exec);
      if (dpos >= 0) { // false for NaN
        if (dpos > len)
          dpos = len;
      } else
        dpos = 0;
    }
    d = s.find(u2, static_cast<int>(dpos));
    result = Number(d);
    break;
  case LastIndexOf:
    u2 = a0.toString(exec);
    d = a1.toNumber(exec);
    if (a1.type() == UndefinedType || KJS::isNaN(d))
      dpos = len;
    else {
      dpos = a1.toInteger(exec);
      if (dpos >= 0) { // false for NaN
        if (dpos > len)
          dpos = len;
      } else
        dpos = 0;
    }
    d = s.rfind(u2, static_cast<int>(dpos));
    result = Number(d);
    break;
  case Match:
  case Search: {
    u = s;
    RegExp *reg, *tmpReg = 0;
    RegExpImp *imp = 0;
    if (a0.isA(ObjectType) && a0.toObject(exec).inherits(&RegExpImp::info))
    {
      imp = static_cast<RegExpImp *>( a0.toObject(exec).imp() );
      reg = imp->regExp();
    }
    else
    { /*
       *  ECMA 15.5.4.12 String.prototype.search (regexp)
       *  If regexp is not an object whose [[Class]] property is "RegExp", it is
       *  replaced with the result of the expression new RegExp(regexp).
       */
      reg = tmpReg = new RegExp(a0.toString(exec), RegExp::None);
    }
    RegExpObjectImp* regExpObj = static_cast<RegExpObjectImp*>(exec->lexicalInterpreter()->builtinRegExp().imp());
    int **ovector = regExpObj->registerRegexp(reg, u);
    UString mstr = reg->match(u, -1, &pos, ovector);
    if (id == Search) {
      result = Number(pos);
    } else {
      // Exec
      if ((reg->flags() & RegExp::Global) == 0) {
	// case without 'g' flag is handled like RegExp.prototype.exec
	if (mstr.isNull()) {
	  result = Null();
	} else {
	  regExpObj->setSubPatterns(reg->subPatterns());
	  result = regExpObj->arrayOfMatches(exec,mstr);
	}
      } else {
	// return array of matches
	List list;
	int lastIndex = 0;
	while (pos >= 0) {
          if (mstr.isNull())
            list.append(UndefinedImp::staticUndefined);
          else
	    list.append(String(mstr));
	  lastIndex = pos;
	  pos += mstr.isEmpty() ? 1 : mstr.size();
	  delete [] *ovector;
	  mstr = reg->match(u, pos, &pos, ovector);
	}
	if (imp)
	  imp->put(exec, "lastIndex", Number(lastIndex), DontDelete|DontEnum);
	if (list.isEmpty()) {
	  // if there are no matches at all, it's important to return
	  // Null instead of an empty array, because this matches
	  // other browsers and because Null is a false value.
	  result = Null(); 
	} else {
	  result = exec->lexicalInterpreter()->builtinArray().construct(exec, list);
	}
      }
    }
    delete tmpReg;
    break;
  }
  case Replace:
    result = replace(exec, s, a0, a1);
    break;
  case Slice: // http://developer.netscape.com/docs/manuals/js/client/jsref/string.htm#1194366
    {
        // The arg processing is very much like ArrayProtoFunc::Slice
        double begin = args[0].toInteger(exec);
        if (begin >= 0) { // false for NaN
          if (begin > len)
            begin = len;
        } else {
          begin += len;
          if (!(begin >= 0)) // true for NaN
            begin = 0;
        }
        double end = len;
        if (args[1].type() != UndefinedType) {
          end = args[1].toInteger(exec);
          if (end >= 0) { // false for NaN
            if (end > len)
              end = len;
          } else {
            end += len;
            if (!(end >= 0)) // true for NaN
              end = 0;
          }
        }
        //printf( "Slicing from %d to %d \n", begin, end );
        result = String(s.substr(static_cast<int>(begin), static_cast<int>(end-begin)));
        break;
    }
    case Split: {
    Object constructor = exec->lexicalInterpreter()->builtinArray();
    Object res = Object::dynamicCast(constructor.construct(exec,List::empty()));
    result = res;
    u = s;
    i = p0 = 0;
    uint32_t limit = a1.type() == UndefinedType ? 0xFFFFFFFFU : a1.toUInt32(exec);
    if (a0.type() == ObjectType && Object::dynamicCast(a0).inherits(&RegExpImp::info)) {
      Object obj0 = Object::dynamicCast(a0);
      RegExp reg(obj0.get(exec,"source").toString(exec));
      if (u.isEmpty() && !reg.match(u, 0).isNull()) {
	// empty string matched by regexp -> empty array
	res.put(exec,lengthPropertyName, Number(0));
	break;
      }
      pos = 0;
      while (static_cast<uint32_t>(i) != limit && pos < u.size()) {
	// TODO: back references
        int mpos;
        int *ovector = 0L;
	UString mstr = reg.match(u, pos, &mpos, &ovector);
        delete [] ovector; ovector = 0L;
	if (mpos < 0)
	  break;
	pos = mpos + (mstr.isEmpty() ? 1 : mstr.size());
	if (mpos != p0 || !mstr.isEmpty()) {
	  res.put(exec,i, String(u.substr(p0, mpos-p0)));
	  p0 = mpos + mstr.size();
	  i++;
	}
      }
    } else {
      u2 = a0.toString(exec);
      if (u2.isEmpty()) {
	if (u.isEmpty()) {
	  // empty separator matches empty string -> empty array
	  put(exec,lengthPropertyName, Number(0));
	  break;
	} else {
	  while (static_cast<uint32_t>(i) != limit && i < u.size()-1)
	    res.put(exec, i++, String(u.substr(p0++, 1)));
	}
      } else {
	while (static_cast<uint32_t>(i) != limit && (pos = u.find(u2, p0)) >= 0) {
	  res.put(exec, i, String(u.substr(p0, pos-p0)));
	  p0 = pos + u2.size();
	  i++;
	}
      }
    }
    // add remaining string, if any
    if (static_cast<uint32_t>(i) != limit)
      res.put(exec, i++, String(u.substr(p0)));
    res.put(exec,lengthPropertyName, Number(i));
    }
    break;
  case Substr: {
    double d = a0.toInteger(exec);
    double d2 = a1.toInteger(exec);
    if (!(d >= 0)) { // true for NaN
      d += len;
      if (!(d >= 0)) // true for NaN
        d = 0;
    }
    if (isNaN(d2))
      d2 = len - d;
    else {
      if (d2 < 0)
        d2 = 0;
      if (d2 > len - d)
        d2 = len - d;
    }
    result = String(s.substr(static_cast<int>(d), static_cast<int>(d2)));
    break;
  }
  case Substring: {
    double start = a0.toNumber(exec);
    double end = a1.toNumber(exec);
    if (KJS::isNaN(start))
      start = 0;
    if (KJS::isNaN(end))
      end = 0;
    if (start < 0)
      start = 0;
    if (end < 0)
      end = 0;
    if (start > len)
      start = len;
    if (end > len)
      end = len;
    if (a1.type() == UndefinedType)
      end = len;
    if (start > end) {
      double temp = end;
      end = start;
      start = temp;
    }
    result = String(s.substr((int)start, (int)end-(int)start));
    }
    break;
  case ToLowerCase:
  case ToLocaleLowerCase: // FIXME: To get this 100% right we need to detect Turkish and change I to lowercase i without a dot.
    u = s;
    for (i = 0; i < len; i++)
      u[i] = u[i].toLower();
    result = String(u);
    break;
  case ToUpperCase:
  case ToLocaleUpperCase: // FIXME: To get this 100% right we need to detect Turkish and change i to uppercase I with a dot.
    u = s;
    for (i = 0; i < len; i++)
      u[i] = u[i].toUpper();
    result = String(u);
    break;
#ifndef KJS_PURE_ECMA
  case Big:
    result = String("<big>" + s + "</big>");
    break;
  case Small:
    result = String("<small>" + s + "</small>");
    break;
  case Blink:
    result = String("<blink>" + s + "</blink>");
    break;
  case Bold:
    result = String("<b>" + s + "</b>");
    break;
  case Fixed:
    result = String("<tt>" + s + "</tt>");
    break;
  case Italics:
    result = String("<i>" + s + "</i>");
    break;
  case Strike:
    result = String("<strike>" + s + "</strike>");
    break;
  case Sub:
    result = String("<sub>" + s + "</sub>");
    break;
  case Sup:
    result = String("<sup>" + s + "</sup>");
    break;
  case Fontcolor:
    result = String("<font color=\"" + a0.toString(exec) + "\">" + s + "</font>");
    break;
  case Fontsize:
    result = String("<font size=\"" + a0.toString(exec) + "\">" + s + "</font>");
    break;
  case Anchor:
    result = String("<a name=\"" + a0.toString(exec) + "\">" + s + "</a>");
    break;
  case Link:
    result = String("<a href=\"" + a0.toString(exec) + "\">" + s + "</a>");
    break;
#endif
  }

  return result;
}

// ------------------------------ StringObjectImp ------------------------------

StringObjectImp::StringObjectImp(ExecState *exec,
                                 FunctionPrototypeImp *funcProto,
                                 StringPrototypeImp *stringProto)
  : InternalFunctionImp(funcProto)
{
  Value protect(this);
  // ECMA 15.5.3.1 String.prototype
  putDirect(prototypePropertyName, stringProto, DontEnum|DontDelete|ReadOnly);

  static Identifier fromCharCode("fromCharCode");
  putDirect(fromCharCode, new StringObjectFuncImp(exec,funcProto), DontEnum);

  // no. of arguments for constructor
  putDirect(lengthPropertyName, NumberImp::one(), ReadOnly|DontDelete|DontEnum);
}


bool StringObjectImp::implementsConstruct() const
{
  return true;
}

// ECMA 15.5.2
Object StringObjectImp::construct(ExecState *exec, const List &args)
{
  ObjectImp *proto = exec->lexicalInterpreter()->builtinStringPrototype().imp();
  if (args.size() == 0)
    return Object(new StringInstanceImp(proto));
  return Object(new StringInstanceImp(proto, args.begin()->dispatchToString(exec)));
}

bool StringObjectImp::implementsCall() const
{
  return true;
}

// ECMA 15.5.1
Value StringObjectImp::call(ExecState *exec, Object &/*thisObj*/, const List &args)
{
  if (args.isEmpty())
    return String("");
  else {
    Value v = args[0];
    return String(v.toString(exec));
  }
}

// ------------------------------ StringObjectFuncImp --------------------------

// ECMA 15.5.3.2 fromCharCode()
StringObjectFuncImp::StringObjectFuncImp(ExecState *exec, FunctionPrototypeImp *funcProto)
  : InternalFunctionImp(funcProto)
{
  Value protect(this);
  putDirect(lengthPropertyName, NumberImp::one(), DontDelete|ReadOnly|DontEnum);
}

bool StringObjectFuncImp::implementsCall() const
{
  return true;
}

Value StringObjectFuncImp::call(ExecState *exec, Object &/*thisObj*/, const List &args)
{
  UString s;
  if (args.size()) {
    UChar *buf = new UChar[args.size()];
    UChar *p = buf;
    ListIterator it = args.begin();
    while (it != args.end()) {
      unsigned short u = it->toUInt16(exec);
      *p++ = UChar(u);
      it++;
    }
    s = UString(buf, args.size(), false);
  } else
    s = "";

  return String(s);
}
