/*
 *  This file is part of the KDE libraries
 *  Copyright (C) 2003 Apple Computer, Inc.
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

#ifndef KJS_SCOPE_CHAIN_H
#define KJS_SCOPE_CHAIN_H

namespace KJS {

    class ObjectImp;
    
    class ScopeChainNode {
    public:
        ScopeChainNode(ScopeChainNode *n, ObjectImp *o)
            : next(n), object(o), refCount(1) { }

        ScopeChainNode *next;
        ObjectImp *object;
        int refCount;
    };

    class ScopeChain {
    public:
        ScopeChain() : _node(0) { }
        ~ScopeChain() { deref(); }

        ScopeChain(const ScopeChain &c) : _node(c._node)
            { if (_node) ++_node->refCount; }
        ScopeChain &operator=(const ScopeChain &);

        bool isEmpty() const { return !_node; }
        ObjectImp *top() const { return _node->object; }

	ObjectImp *bottom() const;

        void clear() { deref(); _node = 0; }
        void push(ObjectImp *);
        void push(const ScopeChain &);
        void pop();
        
        void mark();
        
    private:
        ScopeChainNode *_node;
        
        void deref() { if (_node && --_node->refCount == 0) release(); }
        void ref() const;
        
        void release();
    };

}; // namespace KJS

#endif // KJS_SCOPE_CHAIN_H
