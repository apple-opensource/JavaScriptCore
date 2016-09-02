/*
    Copyright (C) 2004 Apple Computer, Inc. All rights reserved.    
*/
#ifndef _WEB_SCRIPT_OBJECT_PRIVATE_H_
#define _WEB_SCRIPT_OBJECT_PRIVATE_H_

#import <JavaScriptCore/WebScriptObject.h>

#include <JavaScriptCore/internal.h>
#include <JavaScriptCore/list.h>
#include <JavaScriptCore/object.h>
#include <JavaScriptCore/runtime_root.h>
#include <JavaScriptCore/value.h>

@interface WebScriptObject (Private)
+ (id)_convertValueToObjcValue:(KJS::Value)value originExecutionContext:(const KJS::Bindings::RootObject *)originExecutionContext executionContext:(const KJS::Bindings::RootObject *)executionContext;
- _init;
- _initWithObjectImp:(KJS::ObjectImp *)imp originExecutionContext:(const KJS::Bindings::RootObject *)originExecutionContext executionContext:(const KJS::Bindings::RootObject *)executionContext ;
- (void)_initializeWithObjectImp:(KJS::ObjectImp *)imp originExecutionContext:(const KJS::Bindings::RootObject *)originExecutionContext executionContext:(const KJS::Bindings::RootObject *)executionContext ;
- (void)_initializeScriptDOMNodeImp;
- (KJS::ObjectImp *)_imp;
- (void)_setExecutionContext:(const KJS::Bindings::RootObject *)context;
- (const KJS::Bindings::RootObject *)_executionContext;
- (void)_setOriginExecutionContext:(const KJS::Bindings::RootObject *)originExecutionContext;
- (const KJS::Bindings::RootObject *)_originExecutionContext;
@end

@interface WebScriptObjectPrivate : NSObject
{
@public
    KJS::ObjectImp *imp;
    const KJS::Bindings::RootObject *executionContext;
    const KJS::Bindings::RootObject *originExecutionContext;
    BOOL isCreatedByDOMWrapper;
}
@end


#endif
