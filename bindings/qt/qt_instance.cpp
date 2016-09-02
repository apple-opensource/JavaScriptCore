/*
 * Copyright (C) 2006 Trolltech ASA
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "config.h"
#include "qt_instance.h"

#include "qt_class.h"
#include "qt_runtime.h"
#include "list.h"

#include <qmetaobject.h>
#include <qdebug.h>

namespace KJS {
namespace Bindings {

QtInstance::QtInstance(QObject* o, PassRefPtr<RootObject> rootObject)
    : Instance(rootObject),
      _class(0),
      _object(o)
{
}

QtInstance::~QtInstance() 
{
}

QtInstance::QtInstance(const QtInstance& other)
    : Instance(other.rootObject()), _class(0), _object(other._object)
{
}

QtInstance& QtInstance::operator=(const QtInstance& other)
{
    if (this == &other)
        return *this;

    _object = other._object;
    _class = 0;

    return *this;
}

Class* QtInstance::getClass() const
{
    if (!_class)
        _class = QtClass::classForObject(_object);
    return _class;
}

void QtInstance::begin()
{
    // Do nothing.
}

void QtInstance::end()
{
    // Do nothing.
}

bool QtInstance::implementsCall() const
{
    return true;
}

QVariant convertValueToQVariant(ExecState* exec, JSValue* value); 
JSValue* convertQVariantToValue(ExecState* exec, const QVariant& variant);
    
JSValue* QtInstance::invokeMethod(ExecState* exec, const MethodList& methodList, const List& args)
{
    // ### Should we support overloading methods?
    ASSERT(methodList.size() == 1);

    QtMethod* method = static_cast<QtMethod*>(methodList[0]);

    if (method->metaObject != _object->metaObject()) 
        return jsUndefined();

    QMetaMethod metaMethod = method->metaObject->method(method->index);
    QList<QByteArray> argTypes = metaMethod.parameterTypes();
    if (argTypes.count() != args.size()) 
        return jsUndefined();

    // only void methods work currently
    if (args.size() > 10) 
        return jsUndefined();

    QVariant vargs[11];
    void *qargs[11];

    int returnType = QMetaType::type(metaMethod.typeName());
    if (!returnType && qstrlen(metaMethod.typeName())) {
        qCritical("QtInstance::invokeMethod: Return type %s of method %s is not registered with QMetaType!", metaMethod.typeName(), metaMethod.signature());
        return jsUndefined();
    }
    vargs[0] = QVariant(returnType);
    qargs[0] = vargs[0].data();

    for (int i = 0; i < args.size(); ++i) {
        vargs[i+1] = convertValueToQVariant(exec, args[i]);
        QVariant::Type type = (QVariant::Type) QMetaType::type(argTypes.at(i));
        if (!type) {
            qCritical("QtInstance::invokeMethod: Method %s has argument %s which is not registered with QMetaType!", metaMethod.signature(), argTypes.at(i).constData());
            return jsUndefined();
        }
        if (!vargs[i+1].convert(type))
            return jsUndefined();

        qargs[i+1] = vargs[i+1].data();
    }
    if (_object->qt_metacall(QMetaObject::InvokeMetaMethod, method->index, qargs) >= 0) 
        return jsUndefined();


    if (vargs[0].isValid())
        return convertQVariantToValue(exec, vargs[0]);
    return jsNull();
}


JSValue* QtInstance::invokeDefaultMethod(ExecState* exec, const List& args)
{
    return jsUndefined();
}


JSValue* QtInstance::defaultValue(JSType hint) const
{
    if (hint == StringType)
        return stringValue();
    if (hint == NumberType)
        return numberValue();
    if (hint == BooleanType)
        return booleanValue();
    return valueOf();
}

JSValue* QtInstance::stringValue() const
{
    QByteArray buf;
    buf = "QObject ";
    buf.append(QByteArray::number(quintptr(_object.operator->())));
    buf.append(" (");
    buf.append(_object->metaObject()->className());
    buf.append(")");
    return jsString(buf.constData());
}

JSValue* QtInstance::numberValue() const
{
    return jsNumber(0);
}

JSValue* QtInstance::booleanValue() const
{
    // FIXME: Implement something sensible.
    return jsBoolean(false);
}

JSValue* QtInstance::valueOf() const 
{
    return stringValue();
}

}
}
