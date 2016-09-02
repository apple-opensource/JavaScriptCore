/*
 * Copyright (C) 2003 Apple Computer, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */
#ifndef _JNI_CLASS_H_
#define _JNI_CLASS_H_

#include <CoreFoundation/CoreFoundation.h>

#include <JavaVM/jni.h>

#include <runtime.h>
#include <jni_runtime.h>

#include <runtime.h>

namespace KJS {

namespace Bindings {

class JavaClass : public Class
{
    // Use the public static factory methods to get instances of JavaClass.
    
protected:
    void _commonInit (jobject aClass);

    JavaClass (const char *name);
    
    JavaClass (jobject aClass);
    
public:
    // Return the cached JavaClass from the class of the jobject.
    static JavaClass *classForInstance (jobject anInstance);

    // Return the cached JavaClass of the specified name.
    static JavaClass *classForName (const char *name);
    
    void _commonDelete() {
        free((void *)_name);
        CFRelease (_fields);
        CFRelease (_methods);
        delete [] _constructors;
    }
    
    ~JavaClass () {
        _commonDelete();
    }

    void _commonCopy(const JavaClass &other) {
        long i;

        _name = strdup (other._name);

        _methods = CFDictionaryCreateCopy (NULL, other._methods);
        _fields = CFDictionaryCreateCopy (NULL, other._fields);
        
        _numConstructors = other._numConstructors;
        _constructors = new JavaConstructor[_numConstructors];
        for (i = 0; i < _numConstructors; i++) {
            _constructors[i] = other._constructors[i];
        }
    }
    
    JavaClass (const JavaClass &other) 
            : Class() {
        _commonCopy (other);
    };

    JavaClass &operator=(const JavaClass &other)
    {
        if (this == &other)
            return *this;
            
        _commonDelete();
        _commonCopy (other);
        
        return *this;
    }

    virtual const char *name() const { return _name; };
    
    virtual MethodList *methodsNamed(const char *name) const;
    
    virtual Field *fieldNamed(const char *name) const;
    
    virtual Constructor *constructorAt(long i) const {
        return &_constructors[i]; 
    };
    
    virtual long numConstructors() const { return _numConstructors; };
    
    void setClassName(const char *n);
    bool isNumberClass() const;
    bool isBooleanClass() const;
    bool isStringClass() const;
    
private:
    const char *_name;
    CFDictionaryRef _fields;
    CFDictionaryRef _methods;
    JavaConstructor *_constructors;
    long _numConstructors;
};

} // namespace Bindings

} // namespace KJS

#endif