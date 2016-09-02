/*
 * Copyright (C) 2004 Apple Computer, Inc.  All rights reserved.
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
#import <Foundation/Foundation.h>

#import <JavaScriptCore/objc_instance.h>
#import <JavaScriptCore/WebScriptObject.h>

#ifdef NDEBUG
#define OBJC_LOG(formatAndArgs...) ((void)0)
#else
#define OBJC_LOG(formatAndArgs...) { \
    fprintf (stderr, "%s:%d -- %s:  ", __FILE__, __LINE__, __FUNCTION__); \
    fprintf(stderr, formatAndArgs); \
}
#endif

#include <JavaScriptCore/objc_runtime.h>

using namespace KJS::Bindings;
using namespace KJS;

ObjcInstance::ObjcInstance (ObjectStructPtr instance) 
{
    _instance = (id)instance;
    if (_instance) {
        CFRetain(_instance);
    }
    _class = 0;
    _pool = 0;
    _beginCount = 0;
}

ObjcInstance::~ObjcInstance () 
{
    if ([_instance respondsToSelector:@selector(finalizeForWebScript)])
        [_instance finalizeForWebScript];
    if (_instance) {
        CFRelease(_instance);
    }
}

ObjcInstance::ObjcInstance (const ObjcInstance &other) : Instance() 
{
    _instance = (id)other._instance;
    if (_instance) {
        CFRetain(_instance);
    }
    _class = other._class;
    _pool = 0;
    _beginCount = 0;
}

ObjcInstance &ObjcInstance::operator=(const ObjcInstance &other)
{
    ObjectStructPtr _oldInstance = _instance;
    _instance = other._instance;
    if (_instance) {
        CFRetain(_instance);
    }
    if (_oldInstance) {
        CFRelease(_oldInstance);
    }
    
    // Classes are kept around forever.
    _class = other._class;
    
    return *this;
}

void ObjcInstance::begin()
{
    if (!_pool) {
        _pool = [[NSAutoreleasePool alloc] init];
    }
    _beginCount++;
}

void ObjcInstance::end()
{
    _beginCount--;
    assert (_beginCount >= 0);
    if (_beginCount == 0) {
#if MAC_OS_X_VERSION_MAX_ALLOWED <= MAC_OS_X_VERSION_10_3
        [_pool release];
#else
        [_pool drain];
#endif
    }
    _pool = 0;
}

Bindings::Class *ObjcInstance::getClass() const 
{
    if (_instance == 0)
        return 0;
        
    if (_class == 0) {
        _class = ObjcClass::classForIsA(_instance->isa);
    }
    return static_cast<Bindings::Class*>(_class);
}

Value ObjcInstance::invokeMethod (KJS::ExecState *exec, const MethodList &methodList, const List &args)
{
    Value resultValue;

    // Overloading methods is not allowed in ObjectiveC.  Should only be one
    // name match for a particular method.
    assert (methodList.length() == 1);

NS_DURING
    
    ObjcMethod *method = 0;
    method = static_cast<ObjcMethod*>(methodList.methodAt(0));
    NSMethodSignature *signature = method->getMethodSignature();
    NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:signature];
    [invocation setSelector:(SEL)method->name()];
    [invocation setTarget:_instance];
    unsigned i, count = args.size();
    
    if (method->isFallbackMethod()) {
        // invokeUndefinedMethodFromWebScript:withArguments: implementation must return an
        // object.
        if (strcmp ([signature methodReturnType], "@") != 0) {
            OBJC_LOG ("incorrect signature for invokeUndefinedMethodFromWebScript:withArguments:, expected object return type");
            delete method;
            return Undefined();
        }
        
        // Invoke invokeUndefinedMethodFromWebScript:withArguments:, pass JavaScript function
        // name as first (actually at 2) argument and array of args as second.
        NSString *jsName = (NSString *)method->javaScriptName();
        [invocation setArgument:&jsName atIndex:2];
        
        NSMutableArray *objcArgs = [NSMutableArray array];
        for (i = 0; i < count; i++) {
            ObjcValue value = convertValueToObjcValue (exec, args.at(i), ObjcObjectType);
            [objcArgs addObject:value.objectValue];
        }
        [invocation setArgument:&objcArgs atIndex:3];
    }
    else {
        if (count != [signature numberOfArguments] - 2){
            return Undefined();
        }
        
        for (i = 2; i < count+2; i++) {
            const char *type = [signature getArgumentTypeAtIndex:i];
            ObjcValueType objcValueType = objcValueTypeForType (type);

            // Must have a valid argument type.  This method signature should have
            // been filtered already to ensure that it has acceptable argument
            // types.
            assert (objcValueType != ObjcInvalidType && objcValueType != ObjcVoidType);
            
            ObjcValue value = convertValueToObjcValue (exec, args.at(i-2), objcValueType);
            
            switch (objcValueType) {
                case ObjcObjectType:
                    [invocation setArgument:&value.objectValue atIndex:i];
                    break;
                case ObjcCharType:
                    [invocation setArgument:&value.charValue atIndex:i];
                    break;
                case ObjcShortType:
                    [invocation setArgument:&value.shortValue atIndex:i];
                    break;
                case ObjcIntType:
                    [invocation setArgument:&value.intValue atIndex:i];
                    break;
                case ObjcLongType:
                    [invocation setArgument:&value.longValue atIndex:i];
                    break;
                case ObjcFloatType:
                    [invocation setArgument:&value.floatValue atIndex:i];
                    break;
                case ObjcDoubleType:
                    [invocation setArgument:&value.doubleValue atIndex:i];
                    break;
                default:
                    // Should never get here.  Argument types are filtered (and
                    // the assert above should have fired in the impossible case
                    // of an invalid type anyway).
                    fprintf (stderr, "%s:  invalid type (%d)\n", __PRETTY_FUNCTION__, (int)objcValueType);
                    assert (true);
            }
        }
    }
    
    // Invoke the ObjectiveC method.
    [invocation invoke];

    // Get the return value type.
    const char *type = [signature methodReturnType];
    ObjcValueType objcValueType = objcValueTypeForType (type);
    
    // Must have a valid return type.  This method signature should have
    // been filtered already to ensure that it have an acceptable return
    // type.
    assert (objcValueType != ObjcInvalidType);
    
    // Get the return value and convert it to a KJS::Value.  Length
    // of return value will never exceed the size of largest scalar
    // or a pointer.
    char buffer[1024];
    assert ([signature methodReturnLength] < 1024);
    
    if (*type == 'v') {
        resultValue = Undefined();
    }
    else {
        [invocation getReturnValue:buffer];
        resultValue = convertObjcValueToValue (exec, buffer, objcValueType);
    }

NS_HANDLER
    
    resultValue = Undefined();
    
NS_ENDHANDLER

    return resultValue;
}

Value ObjcInstance::invokeDefaultMethod (KJS::ExecState *exec, const List &args)
{
    Value resultValue;
    
NS_DURING

    if (![_instance respondsToSelector:@selector(invokeDefaultMethodWithArguments:)])
        return Undefined();
    
    NSMethodSignature *signature = [_instance methodSignatureForSelector:@selector(invokeDefaultMethodWithArguments:)];
    NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:signature];
    [invocation setSelector:@selector(invokeDefaultMethodWithArguments:)];
    [invocation setTarget:_instance];
    unsigned i, count = args.size();
    
    // invokeDefaultMethodWithArguments: implementation must return an
    // object.
    if (strcmp ([signature methodReturnType], "@") != 0) {
        OBJC_LOG ("incorrect signature for invokeDefaultMethodWithArguments:, expected object return type");
        return Undefined();
    }
    
    NSMutableArray *objcArgs = [NSMutableArray array];
    for (i = 0; i < count; i++) {
        ObjcValue value = convertValueToObjcValue (exec, args.at(i), ObjcObjectType);
        [objcArgs addObject:value.objectValue];
    }
    [invocation setArgument:&objcArgs atIndex:2];
    
    // Invoke the ObjectiveC method.
    [invocation invoke];

    // Get the return value type, should always be "@" because of
    // check above.
    const char *type = [signature methodReturnType];
    ObjcValueType objcValueType = objcValueTypeForType (type);
    
    // Get the return value and convert it to a KJS::Value.  Length
    // of return value will never exceed the size of a pointer, so we're
    // OK we 32 here.
    char buffer[32];
    [invocation getReturnValue:buffer];
    resultValue = convertObjcValueToValue (exec, buffer, objcValueType);

NS_HANDLER
    
    resultValue = Undefined();
    
NS_ENDHANDLER

    return resultValue;
}

void ObjcInstance::setValueOfField (KJS::ExecState *exec, const Field *aField, const KJS::Value &aValue) const
{
    aField->setValueToInstance (exec, this, aValue);
}

bool ObjcInstance::supportsSetValueOfUndefinedField ()
{
    id targetObject = getObject();
    
    if ([targetObject respondsToSelector:@selector(setValue:forUndefinedKey:)])
	return true;
	
    return false;
}

void ObjcInstance::setValueOfUndefinedField (KJS::ExecState *exec, const KJS::Identifier &property, const KJS::Value &aValue)
{
    id targetObject = getObject();
    
    // This check is not really necessary because NSObject implements
    // setValue:forUndefinedKey:, and unfortnately the default implementation
    // throws an exception.
    if ([targetObject respondsToSelector:@selector(setValue:forUndefinedKey:)]){
        
        NS_DURING
        
            ObjcValue objcValue = convertValueToObjcValue (exec, aValue, ObjcObjectType);
            [targetObject setValue:objcValue.objectValue forUndefinedKey:[NSString stringWithCString:property.ascii()]];
        
        NS_HANDLER
            
            // Do nothing.  Class did not override valueForUndefinedKey:.
            
        NS_ENDHANDLER
        
    }
}

Value ObjcInstance::getValueOfField (KJS::ExecState *exec, const Field *aField) const {  
    return aField->valueFromInstance (exec, this);
}

KJS::Value ObjcInstance::getValueOfUndefinedField (KJS::ExecState *exec, const KJS::Identifier &property, KJS::Type hint) const
{
    Value result = Undefined();
    
    id targetObject = getObject();
    
    // This check is not really necessary because NSObject implements
    // valueForUndefinedKey:, and unfortnately the default implementation
    // throws an exception.
    if ([targetObject respondsToSelector:@selector(valueForUndefinedKey:)]){
        id objcValue;
        
        NS_DURING
        
            objcValue = [targetObject valueForUndefinedKey:[NSString stringWithCString:property.ascii()]];
            result = convertObjcValueToValue (exec, &objcValue, ObjcObjectType);
        
        NS_HANDLER
            
            // Do nothing.  Class did not override valueForUndefinedKey:.
            
        NS_ENDHANDLER
        
    }
    
    return result;
}

KJS::Value ObjcInstance::defaultValue (KJS::Type hint) const
{
    if (hint == KJS::StringType) {
        return stringValue();
    }
    else if (hint == KJS::NumberType) {
        return numberValue();
    }
    else if (hint == KJS::BooleanType) {
        return booleanValue();
    }
    else if (hint == KJS::UnspecifiedType) {
        if ([_instance isKindOfClass:[NSString class]]) {
            return stringValue();
        }
        else if ([_instance isKindOfClass:[NSNumber class]]) {
            return numberValue();
        }
        else if ([_instance isKindOfClass:[NSNumber class]]) {
            return booleanValue();
        }
    }
    
    return valueOf();
}

KJS::Value ObjcInstance::stringValue() const
{
    return convertNSStringToString ([getObject() description]);
}

KJS::Value ObjcInstance::numberValue() const
{
    // FIXME:  Implement something sensible
    KJS::Number v(0);
    return v;
}

KJS::Value ObjcInstance::booleanValue() const
{
    // FIXME:  Implement something sensible
    KJS::Boolean v((bool)0);
    return v;
}

KJS::Value ObjcInstance::valueOf() const 
{
    return stringValue();
}
