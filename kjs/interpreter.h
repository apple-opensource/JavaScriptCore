/*
 *  This file is part of the KDE libraries
 *  Copyright (C) 1999-2001 Harri Porten (porten@kde.org)
 *  Copyright (C) 2001 Peter Kelly (pmk@post.com)
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

#ifndef _KJS_INTERPRETER_H_
#define _KJS_INTERPRETER_H_

#include "value.h"
#include "object.h"
#include "types.h"

#if APPLE_CHANGES

#include "runtime.h"

#endif

namespace KJS {

  class ContextImp;
  class InterpreterImp;

  /**
   * Represents an execution context, as specified by section 10 of the ECMA
   * spec.
   *
   * An execution context contains information about the current state of the
   * script - the scope for variable lookup, the value of "this", etc. A new
   * execution context is entered whenever global code is executed (e.g. with
   * @ref Interpreter::evaluate()), a function is called (see @ref
   * Object::call()), or the builtin "eval" function is executed.
   *
   * Most inheritable functions in the KJS api take a @ref ExecState pointer as
   * their first parameter. This can be used to obtain a handle to the current
   * execution context.
   *
   * Note: Context objects are wrapper classes/smart pointers for the internal
   * KJS ContextImp type. When one context variable is assigned to another, it
   * is still referencing the same internal object.
   */
  class Context {
  public:
    Context(ContextImp *i) : rep(i) { }

    ContextImp *imp() const { return rep; }

    /**
     * Returns the scope chain for this execution context. This is used for
     * variable lookup, with the list being searched from start to end until a
     * variable is found.
     *
     * @return The execution context's scope chain
     */
    const ScopeChain &scopeChain() const;

    /**
     * Returns the variable object for the execution context. This contains a
     * property for each variable declared in the execution context.
     *
     * @return The execution context's variable object
     */
    Object variableObject() const;

    /**
     * Returns the "this" value for the execution context. This is the value
     * returned when a script references the special variable "this". It should
     * always be an Object, unless application-specific code has passed in a
     * different type.
     *
     * The object that is used as the "this" value depends on the type of
     * execution context - for global contexts, the global object is used. For
     * function objewcts, the value is given by the caller (e.g. in the case of
     * obj.func(), obj would be the "this" value). For code executed by the
     * built-in "eval" function, the this value is the same as the calling
     * context.
     *
     * @return The execution context's "this" value
     */
    Object thisValue() const;

    /**
     * Returns the context from which the current context was invoked. For
     * global code this will be a null context (i.e. one for which @ref
     * isNull() returns true). You should check @ref isNull() on the returned
     * value before calling any of it's methods.
     *
     * @return The calling execution context
     */
    const Context callingContext() const;

  private:
    ContextImp *rep;
  };

  class SavedBuiltinsInternal;

  class SavedBuiltins {
    friend class InterpreterImp;
  public:
    SavedBuiltins();
    ~SavedBuiltins();
  private:
    SavedBuiltinsInternal *_internal;
  };

  /**
   * Interpreter objects can be used to evaluate ECMAScript code. Each
   * interpreter has a global object which is used for the purposes of code
   * evaluation, and also provides access to built-in properties such as
   * " Object" and "Number".
   */
  class Interpreter {
  public:
    /**
     * Creates a new interpreter. The supplied object will be used as the global
     * object for all scripts executed with this interpreter. During
     * constuction, all the standard properties such as "Object" and "Number"
     * will be added to the global object.
     *
     * Note: You should not use the same global object for multiple
     * interpreters.
     *
     * This is due do the fact that the built-in properties are set in the
     * constructor, and if these objects have been modified from another
     * interpreter (e.g. a script modifying String.prototype), the changes will
     * be overridden.
     *
     * @param global The object to use as the global object for this interpreter
     */
    Interpreter(const Object &global);
    /**
     * Creates a new interpreter. A global object will be created and
     * initialized with the standard global properties.
     */
    Interpreter();
    virtual ~Interpreter();

    /**
     * Returns the object that is used as the global object during all script
     * execution performed by this interpreter
     */
    Object &globalObject() const;

    void initGlobalObject();

    static void lock();
    static void unlock();
    static int lockCount();

    /**
     * Returns the execution state object which can be used to execute
     * scripts using this interpreter at a the "global" level, i.e. one
     * with a execution context that has the global object as the "this"
     * value, and who's scope chain contains only the global object.
     *
     * Note: this pointer remains constant for the life of the interpreter
     * and should not be manually deleted.
     *
     * @return The interpreter global execution state object
     */
    ExecState *globalExec();

    /**
     * Parses the supplied ECMAScript code and checks for syntax errors.
     *
     * @param code The code to check
     * @return true if there were no syntax errors in the code, otherwise false
     */
    bool checkSyntax(const UString &code);

    /**
     * Evaluates the supplied ECMAScript code.
     *
     * Since this method returns a Completion, you should check the type of
     * completion to detect an error or before attempting to access the returned
     * value. For example, if an error occurs during script execution and is not
     * caught by the script, the completion type will be Throw.
     *
     * If the supplied code is invalid, a SyntaxError will be thrown.
     *
     * @param code The code to evaluate
     * @param thisV The value to pass in as the "this" value for the script
     * execution. This should either be Null() or an Object.
     * @return A completion object representing the result of the execution.
     */
    Completion evaluate(const UString &sourceURL, int startingLineNumber, const UString &code, const Value &thisV = Value());

	// Overload of evaluate to keep JavaScriptGlue both source and binary compatible.
	Completion evaluate(const UString &code, const Value &thisV = Value(), const UString &sourceFilename = UString());

    /**
     * @internal
     *
     * Returns the implementation object associated with this interpreter.
     * Only useful for internal KJS operations.
     */
    InterpreterImp *imp() const { return rep; }

    /**
     * Returns the builtin "Object" object. This is the object that was set
     * as a property of the global object during construction; if the property
     * is replaced by script code, this method will still return the original
     * object.
     *
     * @return The builtin "Object" object
     */
    Object builtinObject() const;

    /**
     * Returns the builtin "Function" object.
     */
    Object builtinFunction() const;

    /**
     * Returns the builtin "Array" object.
     */
    Object builtinArray() const;

    /**
     * Returns the builtin "Boolean" object.
     */
    Object builtinBoolean() const;

    /**
     * Returns the builtin "String" object.
     */
    Object builtinString() const;

    /**
     * Returns the builtin "Number" object.
     */
    Object builtinNumber() const;

    /**
     * Returns the builtin "Date" object.
     */
    Object builtinDate() const;

    /**
     * Returns the builtin "RegExp" object.
     */
    Object builtinRegExp() const;

    /**
     * Returns the builtin "Error" object.
     */
    Object builtinError() const;

    /**
     * Returns the builtin "Object.prototype" object.
     */
    Object builtinObjectPrototype() const;

    /**
     * Returns the builtin "Function.prototype" object.
     */
    Object builtinFunctionPrototype() const;

    /**
     * Returns the builtin "Array.prototype" object.
     */
    Object builtinArrayPrototype() const;

    /**
     * Returns the builtin "Boolean.prototype" object.
     */
    Object builtinBooleanPrototype() const;

    /**
     * Returns the builtin "String.prototype" object.
     */
    Object builtinStringPrototype() const;

    /**
     * Returns the builtin "Number.prototype" object.
     */
    Object builtinNumberPrototype() const;

    /**
     * Returns the builtin "Date.prototype" object.
     */
    Object builtinDatePrototype() const;

    /**
     * Returns the builtin "RegExp.prototype" object.
     */
    Object builtinRegExpPrototype() const;

    /**
     * Returns the builtin "Error.prototype" object.
     */
    Object builtinErrorPrototype() const;

    /**
     * The initial value of "Error" global property
     */
    Object builtinEvalError() const;
    Object builtinRangeError() const;
    Object builtinReferenceError() const;
    Object builtinSyntaxError() const;
    Object builtinTypeError() const;
    Object builtinURIError() const;

    Object builtinEvalErrorPrototype() const;
    Object builtinRangeErrorPrototype() const;
    Object builtinReferenceErrorPrototype() const;
    Object builtinSyntaxErrorPrototype() const;
    Object builtinTypeErrorPrototype() const;
    Object builtinURIErrorPrototype() const;

    enum CompatMode { NativeMode, IECompat, NetscapeCompat };
    /**
     * Call this to enable a compatibility mode with another browser.
     * (by default konqueror is in "native mode").
     * Currently, in KJS, this only changes the behaviour of Date::getYear()
     * which returns the full year under IE.
     */
    void setCompatMode(CompatMode mode);
    CompatMode compatMode() const;

    /**
     * Called by InterpreterImp during the mark phase of the garbage collector
     * Default implementation does nothing, this exist for classes that reimplement Interpreter.
     */
    virtual void mark() {}

    /**
     * Provides a way to distinguish derived classes.
     * Only useful if you reimplement Interpreter and if different kind of
     * interpreters are created in the same process.
     * The base class returns 0, the ECMA-bindings interpreter returns 1.
     */
    virtual int rtti() { return 0; }

#ifdef KJS_DEBUG_MEM
    /**
     * @internal
     */
    static void finalCheck();
#endif

#if APPLE_CHANGES
    static bool shouldPrintExceptions();
    static void setShouldPrintExceptions(bool);
#endif

    void saveBuiltins (SavedBuiltins &) const;
    void restoreBuiltins (const SavedBuiltins &);

#if APPLE_CHANGES
    /**
     * Determine if the value is a global object (for any interpreter).  This may
     * be difficult to determine for multiple uses of JSC in a process that are
     * logically independent of each other.  In the case of WebCore, this method
     * is used to determine if an object is the Window object so we can perform
     * security checks.
     */
    virtual bool isGlobalObject(const Value &v) { return false; }
    
    /** 
     * Find the interpreter for a particular global object.  This should really
     * be a static method, but we can't do that is C++.  Again, as with isGlobalObject()
     * implementation really need to know about all instances of Interpreter
     * created in an application to correctly implement this method.  The only
     * override of this method is currently in WebCore.
     */
    virtual Interpreter *interpreterForGlobalObject (const ValueImp *imp) { return 0; }
    
    /**
     * Determine if the it is 'safe' to execute code in the target interpreter from an
     * object that originated in this interpreter.  This check is used to enforce WebCore
     * cross frame security rules.  In particular, attempts to access 'bound' objects are
     * not allowed unless isSafeScript returns true.
     */
    virtual bool isSafeScript (const Interpreter *target) { return true; }
    
    virtual void *createLanguageInstanceForValue (ExecState *exec, Bindings::Instance::BindingLanguage language, const Object &value, const Bindings::RootObject *origin, const Bindings::RootObject *current);
#endif
    
  private:
    InterpreterImp *rep;

    /**
     * This constructor is not implemented, in order to prevent
     * copy-construction of Interpreter objects. You should always pass around
     * pointers to an interpreter instance instead.
     */
    Interpreter(const Interpreter&);

    /**
     * This constructor is not implemented, in order to prevent assignment of
     * Interpreter objects. You should always pass around pointers to an
     * interpreter instance instead.
     */
    Interpreter operator=(const Interpreter&);
  protected:
    virtual void virtual_hook( int id, void* data );
  };

  /**
   * Represents the current state of script execution. This object allows you
   * obtain a handle the interpreter that is currently executing the script,
   * and also the current execution state context.
   */
  class ExecState {
    friend class InterpreterImp;
    friend class FunctionImp;
#if APPLE_CHANGES
    friend class RuntimeMethodImp;
#endif

    friend class GlobalFuncImp;
  public:
    /**
     * Returns the interpreter associated with this execution state
     *
     * @return The interpreter executing the script
     */
    Interpreter *dynamicInterpreter() const { return _interpreter; }

    // for compatibility
    Interpreter *interpreter() const { return dynamicInterpreter(); }

    /**
     * Returns the interpreter associated with the current scope's
     * global object
     *
     * @return The interpreter currently in scope
     */
    Interpreter *lexicalInterpreter() const;

    /**
     * Returns the execution context associated with this execution state
     *
     * @return The current execution state context
     */
    Context context() const { return _context; }

    void setException(const Value &e) { _exception = e; }
    void clearException() { _exception = Value(); }
    Value exception() const { return _exception; }
    bool hadException() const { return !_exception.isNull(); }

  private:
    ExecState(Interpreter *interp, ContextImp *con)
        : _interpreter(interp), _context(con) { }
    Interpreter *_interpreter;
    ContextImp *_context;
    Value _exception;
  };

    class InterpreterLock
    {
    public:
        InterpreterLock() { Interpreter::lock(); }
        ~InterpreterLock() { Interpreter::unlock(); }
    private:
        InterpreterLock(const InterpreterLock &);
        InterpreterLock &operator =(const InterpreterLock &);
    };

} // namespace

#endif // _KJS_INTERPRETER_H_
