// -*- c-basic-offset: 2 -*-
/*
 *  This file is part of the KDE libraries
 *  Copyright (C) 1999-2000 Harri Porten (porten@kde.org)
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

#ifndef ARRAY_INSTANCE_H
#define ARRAY_INSTANCE_H

#include "object.h"

namespace KJS {

  class ArrayInstanceImp : public ObjectImp {
  public:
    ArrayInstanceImp(ObjectImp *proto, unsigned initialLength);
    ArrayInstanceImp(ObjectImp *proto, const List &initialValues);
    ~ArrayInstanceImp();

    virtual Value get(ExecState *exec, const Identifier &propertyName) const;
    virtual Value get(ExecState *exec, unsigned propertyName) const;
    virtual void put(ExecState *exec, const Identifier &propertyName, const Value &value, int attr = None);
    virtual void put(ExecState *exec, unsigned propertyName, const Value &value, int attr = None);
    virtual bool hasOwnProperty(ExecState *exec, const Identifier &propertyName) const;
    virtual bool hasOwnProperty(ExecState *exec, unsigned propertyName) const;
    virtual bool deleteProperty(ExecState *exec, const Identifier &propertyName);
    virtual bool deleteProperty(ExecState *exec, unsigned propertyName);
    virtual ReferenceList propList(ExecState *exec, bool recursive);

    virtual void mark();

    virtual const ClassInfo *classInfo() const { return &info; }
    static const ClassInfo info;
    
    unsigned getLength() const { return length; }
    
    void sort(ExecState *exec);
    void sort(ExecState *exec, Object &compareFunction);
    
  private:
    void setLength(unsigned newLength, ExecState *exec);
    
    unsigned pushUndefinedObjectsToEnd(ExecState *exec);
    
    void resizeStorage(unsigned);

    unsigned length;
    unsigned storageLength;
    unsigned capacity;
    ValueImp **storage;
  };

}; // namespace KJS

#endif
