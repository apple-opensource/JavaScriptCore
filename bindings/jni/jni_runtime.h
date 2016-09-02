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
#ifndef _JNI_RUNTIME_H_
#define _JNI_RUNTIME_H_

#include <CoreFoundation/CoreFoundation.h>

#include <jni_utility.h>
#include <jni_instance.h>
#include <runtime.h>
#include <ustring.h>

namespace KJS
{
class Value;

namespace Bindings
{

class JavaString
{
public:
    JavaString () {};
    
    void _commonInit (JNIEnv *e, jstring s)
    {
        int _size = e->GetStringLength (s);
        const jchar *uc = getUCharactersFromJStringInEnv (e, s);
        _ustring = UString((UChar *)uc,_size);
        releaseUCharactersForJStringInEnv (e, s, uc);
    }
    
    JavaString (JNIEnv *e, jstring s) {
        _commonInit (e, s);
    }
    
    JavaString (jstring s) {
        _commonInit (getJNIEnv(), s);
    }
    
    const char *UTF8String() const { 
        if (_utf8String.c_str() == 0)
            _utf8String = _ustring.UTF8String();
        return _utf8String.c_str();
    }
    const jchar *uchars() const { return (const jchar *)_ustring.data(); }
    int length() const { return _ustring.size(); }
    KJS::UString ustring() const { return _ustring; }
    
private:
    UString _ustring;
    mutable CString _utf8String;
};

class JavaParameter : public Parameter
{
public:
    JavaParameter () : _JNIType(invalid_type) {};
    
    JavaParameter (JNIEnv *env, jstring type);
        
    ~JavaParameter() {
    };

    JavaParameter(const JavaParameter &other) : Parameter() {
        _type = other._type;
        _JNIType = other._JNIType;
    };

    JavaParameter &operator=(const JavaParameter &other)
    {
        if (this == &other)
            return *this;
                    
        _type = other._type;
        _JNIType = other._JNIType;

        return *this;
    }
    
    virtual RuntimeType type() const { return _type.UTF8String(); }

    JNIType getJNIType() const { return _JNIType; }
    
private:
    JavaString _type;
    JNIType _JNIType;
};


class JavaConstructor : public Constructor
{
public:
    JavaConstructor() : _parameters (0), _numParameters(0) {};
    
    JavaConstructor (JNIEnv *e, jobject aConstructor);
    
    ~JavaConstructor() {
        delete [] _parameters;
    };

    void _commonCopy(const JavaConstructor &other) {
        _numParameters = other._numParameters;
        _parameters = new JavaParameter[_numParameters];
        long i;
        for (i = 0; i < _numParameters; i++) {
            _parameters[i] = other._parameters[i];
        }
    }
    
    JavaConstructor(const JavaConstructor &other) : Constructor() {
        _commonCopy (other);
    };

    JavaConstructor &operator=(const JavaConstructor &other)
    {
        if (this == &other)
            return *this;
            
        delete [] _parameters;
        
        _commonCopy (other);

        return *this;
    }

    virtual Parameter *parameterAt(long i) const { return &_parameters[i]; };
    virtual long numParameters() const { return _numParameters; };
    
private:
    JavaParameter *_parameters;
    long _numParameters;
};


class JavaField : public Field
{
public:
    JavaField() : _field(0) {};
    JavaField (JNIEnv *env, jobject aField);
    ~JavaField() {
        delete _field;
    };

    JavaField(const JavaField &other) : 
        Field(), _name(other._name), _type(other._type), _field(other._field) {};

    JavaField &operator=(const JavaField &other)
    {
        if (this == &other)
            return *this;
            
        delete _field;
        
        _name = other._name;
        _type = other._type;
        _field = other._field;

        return *this;
    }
    
    virtual KJS::Value valueFromInstance(KJS::ExecState *exec, const Instance *instance) const;
    virtual void setValueToInstance(KJS::ExecState *exec, const Instance *instance, const KJS::Value &aValue) const;
    
    virtual const char *name() const { return _name.UTF8String(); }
    virtual RuntimeType type() const { return _type.UTF8String(); }

    JNIType getJNIType() const { return _JNIType; }
    
private:
    JavaString _name;
    JavaString _type;
    JNIType _JNIType;
    JavaInstance *_field;
};


class JavaMethod : public Method
{
public:
    JavaMethod() : Method(), _signature(0), _methodID(0) {};
    
    JavaMethod (JNIEnv *env, jobject aMethod);
    
    void _commonDelete() {
        delete _signature;
        delete [] _parameters;
    };
    
    ~JavaMethod () {
        _commonDelete();
    };

    void _commonCopy(const JavaMethod &other) {
        _name = other._name;
        _returnType = other._returnType;

        _numParameters = other._numParameters;
        _parameters = new JavaParameter[_numParameters];
        long i;
        for (i = 0; i < _numParameters; i++) {
            _parameters[i] = other._parameters[i];
        }
        _signature = other._signature;
    };
    
    JavaMethod(const JavaMethod &other) : Method() {
        _commonCopy(other);
    };

    JavaMethod &operator=(const JavaMethod &other)
    {
        if (this == &other)
            return *this;
            
        _commonDelete();
        _commonCopy(other);

        return *this;
    };

    virtual const char *name() const { return _name.UTF8String(); };
    RuntimeType returnType() const { return _returnType.UTF8String(); };
    virtual Parameter *parameterAt(long i) const { return &_parameters[i]; };
    virtual long numParameters() const { return _numParameters; };
    
    const char *signature() const;
    JNIType JNIReturnType() const;

    jmethodID methodID (jobject obj) const;
	
private:
    JavaParameter *_parameters;
    long _numParameters;
    JavaString _name;
    mutable KJS::UString *_signature;
    JavaString _returnType;
    JNIType _JNIReturnType;
    mutable jmethodID _methodID;
};

class JavaArray : public Array
{
public:
    JavaArray (jobject a, const char *type);

    JavaArray (const JavaArray &other);

    JavaArray &operator=(const JavaArray &other){
        if (this == &other)
            return *this;
        
        free ((void *)_type);
        _type = strdup(other._type);

        JObjectWrapper *_oldArray = _array;
        _array = other._array;
        _array->ref();
        _oldArray->deref();
        
        return *this;
    };

    virtual void setValueAt(KJS::ExecState *exec, unsigned int index, const KJS::Value &aValue) const;
    virtual KJS::Value valueAt(KJS::ExecState *exec, unsigned int index) const;
    virtual unsigned int getLength() const;
    
    virtual ~JavaArray();

    jobject javaArray() const { return _array->_instance; }

    static KJS::Value convertJObjectToArray (KJS::ExecState *exec, jobject anObject, const char *type);

private:
    JObjectWrapper *_array;
    unsigned int _length;
    const char *_type;
};

} // namespace Bindings

} // namespace KJS

#endif
