/*
 *  objecthandle.h
 *
 *  This file is part of GammaRay, the Qt application inspection and
 *  manipulation tool.
 *
 *  Copyright (C) 2018 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
 *  Author: Anton Kreuzkamp <anton.kreuzkamp@kdab.com>
 *
 *  Licensees holding valid commercial KDAB GammaRay licenses may use this file in
 *  accordance with GammaRay Commercial License Agreement provided with the Software.
 *
 *  Contact info@kdab.com if any conditions of this licensing are not clear to you.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GAMMARAY_OBJECTHANDLE_H
#define GAMMARAY_OBJECTHANDLE_H

#include <core/probe.h>

#include <QObject>
#include <QMetaObject>
#include <QMetaProperty>
#include <QDebug>
#include <QMetaType>
#include <QMutex>
#include <QSemaphore>
#include <QThread>

#include <core/metaobject.h>
#include <core/metaproperty.h>

#include <utility>
#include <future>
#include <tuple>
#include <memory>

#include <iostream>

#include <list>

#include <private/qobject_p.h>
#include <private/qmetaobject_p.h>

template<typename T> class error;

template<int i> class err;

/**
 * May only be used in cases, where non-constexpr if would still produce valid code
 */
#define IF_CONSTEXPR if


enum ObjectWrapperFlag {
    NoFlags = 0,
    Getter = 1,
    NonConstGetter = 2,
    MemberVar = 4,
    DptrGetter = 8,
    DptrMember = 16,
    CustomCommand = 32,

    QProp = 128,
    OwningPointer = 256,
    NonOwningPointer = 512
};


/**
 * Defines a getter function with the name @p FieldName, which returns the data
 * stored at index @p StorageIndex in m_control->dataStorage or - in the
 * non-caching case - the live value of the property.
 *
 * This is internal for use in other macros.
 */
#define DEFINE_GETTER(FieldName, StorageIndex, Flags) \
decltype(fetch_##FieldName<Flags>(static_cast<value_type*>(nullptr))) FieldName() const \
{ \
    m_control->semaphore.acquire(); \
    QSemaphoreReleaser releaser { &m_control->semaphore }; \
 \
    IF_CONSTEXPR (cachingDisabled<ThisClass_t>::value) { \
        return fetch_##FieldName<Flags>(m_control->object); \
    } else { \
        return std::get< StorageIndex >(m_control->dataStorage); \
    } \
} \



/**
 * Defines a wrapper function for direct access to the property, abstracting
 * away the different kinds of properties (getter, member variable, custom
 * command).  This differs from the DEFINE_GETTER in that the fetch function
 * never caches things. Instead it's used to update the cache.
 *
 * This is internal for use in other macros.
 */
#define DEFINE_FETCH_FUNCTION_PROP(FieldName) \
template<int Flags, typename T = value_type, typename std::enable_if<(Flags & DptrGetter) != 0>::type* = nullptr> /*FIXME T must be the private class! */ \
static auto fetch_##FieldName(const value_type *object) \
-> decltype(wrap<Flags>(std::declval<T>().FieldName())) \
{ \
    return wrap<Flags>(T::get(object)->FieldName()); \
} \
template<int Flags, typename T = value_type, typename std::enable_if<(Flags & DptrMember) != 0>::type* = nullptr> /*FIXME T must be the private class! */ \
static auto fetch_##FieldName(const value_type *object) \
-> decltype(wrap<Flags>(std::declval<T>().FieldName)) \
{ \
    return wrap<Flags>(T::get(object)->FieldName); \
} \
template<int Flags, typename T = value_type, typename std::enable_if<(Flags & Getter) != 0>::type* = nullptr> \
static auto fetch_##FieldName(const T *object) \
-> decltype(wrap<Flags>(std::declval<T>().FieldName())) \
{ \
    return wrap<Flags>(object->FieldName()); \
} \
template<int Flags, typename T = value_type, typename std::enable_if<(Flags & NonConstGetter) != 0>::type* = nullptr> \
static auto fetch_##FieldName(T *object) \
-> decltype(wrap<Flags>(std::declval<T>().FieldName())) \
{ \
    return wrap<Flags>(object->FieldName()); \
} \
template<int Flags, typename T = value_type, typename std::enable_if<(Flags & MemberVar) != 0>::type* = nullptr> \
static auto fetch_##FieldName(const T *object) \
-> decltype(wrap<Flags>(std::declval<T>().FieldName)) \
{ \
    return wrap<Flags>(object->FieldName); \
} \

/**
 * Like DEFINE_FETCH_FUNCTION_PROP but with custom expression.
 *
 * This is internal for use in other macros.
 */
#define DEFINE_FETCH_FUNCTION_CUSTOM_EXPRESSION(FieldName, Expr) \
template<int Flags, typename T = value_type> \
static auto fetch_##FieldName(const T *object) \
-> decltype(wrap<Flags>(Expr)) \
{ \
    return wrap<Flags>(Expr); \
} \



template<typename T, typename ...Args>
struct tuple_append {};
template<typename T, typename ...TupleArgs_t>
struct tuple_append<std::tuple<TupleArgs_t...>, T> {
    using type = std::tuple<TupleArgs_t..., T>;
};
template<typename Tuple_t, typename T>
using tuple_append_t = typename tuple_append<Tuple_t, T>::type;


/**
 * Defines a method, which holds state using overloading and inheritance tricks
 * as described at https://woboq.com/blog/verdigris-implementation-tricks.html
 *
 * Incrementing the counter is done by using a combination of STATE_APPEND and
 * DEFINE_COUNTER. Use DEFINE_COUNTER to define a constexpr variable holding the
 * current count + 1. Use STATE_APPEND to make it the new current count.
 *
 * Use StateExpression to return arbitary content, possibly using recursion. Eg.
 * you can do
 * `std::tuple_cat(MethodName(self, __counter.prev()), std::make_tuple(...)))`
 * to recursively compose a compile-time list.
 *
 * This is meant for internal use in other macros only.
 */
#define STATE_APPEND(MethodName, Counter, AppendedType, StateExpr) \
friend auto MethodName(ThisClass_t *self, __number<Counter> __counter) \
-> tuple_append_t<decltype(MethodName(self, __counter.prev())), AppendedType> \
{ \
    return std::tuple_cat(MethodName(self, __counter.prev()), std::make_tuple(StateExpr)); \
}

/**
 * Defines an overload to __metadata, which adds \p FieldName to the metaobject
 * and recursively calls the previously defined overload of __metadata.
 * (see https://woboq.com/blog/verdigris-implementation-tricks.html).
 *
 * This is meant for internal use in other macros only.
 */
#define ADD_TO_METAOBJECT(FieldName, FieldType, Flags) \
friend void __metadata(ThisClass_t *self, __number<W_COUNTER_##FieldName>, MetaObject *mo) \
{ \
    mo->addProperty(GammaRay::MetaPropertyFactory::makeProperty(#FieldName, &ThisClass_t::FieldName)); \
    __metadata(self, __number< W_COUNTER_##FieldName - 1 >{}, mo); \
}

/**
 * Defines a constexpr variable that fetches the current value of the count
 * used by the constexpr-state code defined by STATE_APPEND and stores it
 * incremented by one.
 *
 * @p CounterName is the name of the variable to be defined
 * @p CounterMethod is the name of the method, which holds the state. This
 *                  method must return a tuple, whose size equals the
 *                  current count.
 *
 * Incrementing the counter is done by using a combination of STATE_APPEND and
 * DEFINE_COUNTER. Use DEFINE_COUNTER to define a constexpr variable holding the
 * current count + 1. Use STATE_APPEND to make it the new current count.
 *
 * This is meant for internal use in other macros only.
 */
#define DEFINE_COUNTER(CounterName, CounterMethod) \
static constexpr int CounterName = \
std::tuple_size<decltype(CounterMethod(static_cast<ThisClass_t*>(nullptr), __number<255>{}))>::value + 1; \


/**
 * Defines an overload to __connectToUpdates, which connects an update property
 * slot to the notify signal of the property \p FieldName. It then recursively
 * calls the previously defined overload of __connectToUpdates
 * (see https://woboq.com/blog/verdigris-implementation-tricks.html).
 *
 * This is meant for internal use in other macros only.
 */
#define CONNECT_TO_UPDATES(FieldName, Flags) \
friend void __connectToUpdates(ThisClass_t *self, __number<W_COUNTER_##FieldName>) \
{ \
    connectToUpdates< W_COUNTER_##FieldName - 1, Flags >(self, &ThisClass_t::fetch_##FieldName<Flags>, #FieldName); \
    __connectToUpdates(self, __number< W_COUNTER_##FieldName - 1 >{}); \
} \


/**
 * Adds a property to the object wrapper. The data will be accessible
 * through a getter in the wrapper, named as \p FieldName.
 *
 * The property can be customized by a couple of \p Flags:
 *  Getter: If this flag is set, data will be fetched using obj->FieldName()
 *  NonConstGetter: Like getter, but indicating that the getter is non-const
 *  MemberVar: Data will be fetched by accessing the member field obj->FieldName directly
 *  DptrGetter: Data will be fetched by accessing ClassPrivate::get(obj)->FieldName()
 *  DptrMember: Data will be fetched by accessing ClassPrivate::get(obj)->FieldName
 *  CustomCommand: Incompatible with this macro. Use CUSTOM_PROP instead.
 *
 *  QProp: Indicates that there exists a Qt property with this name. Setting
 *         this flag will enable reading, writing as well as automatic updating.
 *  OwningPointer: Indicates that the object owns the object which this property
 *                 points to. Setting this correctly is crucial for memory
 *                 management of the object wrapper.
 *  NonOwningPointer Indicates that this object does not own the object which
 *                   this property points to. Setting this correctly is crucial
 *                   for memory management of the object wrapper.
 *
 * It is necessary to set one of Getter/NonConstGetter/MemberVar/DptrGetter/
 * DptrMember. Further, for properties that are pointers to other wrappable
 * objects, it's necessary to set either OwningPointer or NonOwningPointer.
 *
 * Example: If you used obj->x() before to access some data, you can make that
 * available to the wrapper, by writing `PROP(x, Getter)`. Later, use wrapper.x()
 * to access it.
 */
#define PROP(FieldName, Flags) \
DEFINE_COUNTER(W_COUNTER_##FieldName, __data) \
DEFINE_FETCH_FUNCTION_PROP(FieldName) \
STATE_APPEND(__data, W_COUNTER_##FieldName, decltype(fetch_##FieldName<Flags>(static_cast<value_type*>(nullptr))), fetch_##FieldName<Flags>(self->object)) \
DEFINE_GETTER(FieldName, W_COUNTER_##FieldName - 1, Flags) \
ADD_TO_METAOBJECT(FieldName, decltype(fetch_##FieldName<Flags>(static_cast<value_type*>(nullptr))), Flags) \
CONNECT_TO_UPDATES(FieldName, Flags) \

/**
 * Adds a property to the object wrapper. The data will be accessible
 * through a getter in the wrapper, named as \p FieldName. The value of the
 * property will be given by evaluating the expression \p Expression in a
 * context where `object` is a valid C-pointer pointing to the wrapped object.
 *
 * The property can be customized by a couple of \p Flags:
 *  Getter, NonConstGetter, MemberVar, DptrGetter, DptrMember: Incompatible
 *      with this macro. Use PROP instead.
 *  CustomCommand: Optional when using this macro. Indicates that data is to be
 *                 fetched using the custom command \p Expression.
 *
 *  QProp: Indicates that there exists a Qt property with this name. Setting
 *         this flag will enable reading, writing as well as automatic updating.
 *  OwningPointer: Indicates that the object owns the object which this property
 *                 points to. Setting this correctly is crucial for memory
 *                 management of the object wrapper.
 *  NonOwningPointer Indicates that this object does not own the object which
 *                   this property points to. Setting this correctly is crucial
 *                   for memory management of the object wrapper.
 *
 * For properties that are pointers to other wrappable objects, it's necessary
 * to set either OwningPointer or NonOwningPointer.
 *
 * Example: Let Utils::getQmlId(QQuickItem*) be defined. To add a property id to
 * the wrapper of QQuickItem, use `CUSTOM_PROP(id, Utils::getQmlId(object), CustomCommand)`.
 * Later, use wrapper.id() to access it.
 */
#define CUSTOM_PROP(FieldName, Expression, Flags) \
DEFINE_COUNTER(W_COUNTER_##FieldName, __data) \
DEFINE_FETCH_FUNCTION_CUSTOM_EXPRESSION(FieldName, Expression) \
STATE_APPEND(__data, W_COUNTER_##FieldName, \
    decltype(fetch_##FieldName<Flags | CustomCommand>(static_cast<value_type*>(nullptr))), fetch_##FieldName<Flags | CustomCommand>(self->object)) \
DEFINE_GETTER(FieldName, W_COUNTER_##FieldName - 1, Flags | CustomCommand) \
ADD_TO_METAOBJECT(FieldName, decltype(fetch_##FieldName<Flags | CustomCommand>(static_cast<value_type*>(nullptr))), Flags | CustomCommand) \


#define DIRECT_ACCESS_METHOD(MethodName) \
template<typename ...Args> auto MethodName(Args &&...args) -> decltype(object->MethodName(args...)) \
{ \
    return object->MethodName(args...); \
} \


#define BLOCKING_ASYNC_METHOD(MethodName) \
template<typename ...Args> auto MethodName(Args &&...args) -> decltype(object->MethodName(args...)) \
{ \
    return call(object, &value_type::MethodName, args...).get(); \
} \

#define ASYNC_VOID_METHOD(MethodName) \
template<typename ...Args> void MethodName(Args &&...args) \
{ \
    call(object, &value_type::MethodName, args...); \
} \

/**
 * Put this macro in the va_args area of DECLARE_OBJECT_WRAPPER to disable
 * caching for this class. Disabling caching means that accessing the wrapped
 * getters will always return the live value by accessing the underlying
 * getter/member directly.
 *
 * Disabling caching is mainly meant as a porting aid.
 */
#define DISABLE_CACHING using disableCaching_t = void;

/**
 * Defines a specialization of the dummy ObjectWrapper class template for
 * \p Class. This is the main macro for enabling wrapping capabilities for a
 * given class.
 *
 * This macro has two arguments. The first one is the name of the class to be
 * wrapped. The second argument is a free-form area that can be used to put
 * arbitrary content into the wrapper class. Its mostly meant, though, to put
 * PROP and CUSTOM_PROP macros in there, which define properties, the wrapper
 * will have. Also put DISABLE_CACHING here, if desired.
 */
#define DECLARE_OBJECT_WRAPPER(Class, ...) \
template<> \
class GammaRay::ObjectWrapper<Class> : public GammaRay::ObjectWrapperBase \
{ \
private: \
    Class *object = nullptr; \
 \
public: \
    using value_type = Class; \
    using ThisClass_t = ObjectWrapper<Class>; \
    friend std::tuple<> __data(ObjectWrapper<Class> *, __number<0>) { return {}; } \
    friend void __metadata(ObjectWrapper<Class> *, __number<0>, MetaObject *) {} \
    friend void __connectToUpdates(ObjectWrapper<Class> *, __number<0>) {} \
 \
    __VA_ARGS__; \
 \
    using ControlData = ControlBlock<Class, decltype( __data(static_cast<ObjectWrapper<Class>*>(nullptr), __number<255>()) )>; \
    static MetaObject *staticMetaObject() { \
        static auto mo = createStaticMetaObject(); \
        return mo.get(); \
    } \
 \
    explicit ObjectWrapper<Class>(Class *object) \
    : object(object) \
    { \
        initialize<ObjectWrapper<Class>>(this); \
    } \
    explicit ObjectWrapper<Class>(std::shared_ptr<ControlData> controlBlock) \
        : object(controlBlock->object) \
        , m_control(std::move(controlBlock)) \
    {} \
    explicit ObjectWrapper<Class>() = default; \
 \
private: \
    static std::unique_ptr<MetaObject> createStaticMetaObject() { \
        auto mo = new MetaObjectImpl<Class>; \
        mo->setClassName(QStringLiteral(#Class)); \
        __metadata(static_cast<ObjectWrapper<Class>*>(nullptr), __number<255>(), mo); \
        return std::unique_ptr<MetaObject>{mo}; \
    } \
    friend class ObjectWrapperBase; \
    friend class ObjectWrapperTest; \
    friend class ObjectHandle<Class>; \
    std::shared_ptr<ControlData> m_control; \
}; \
Q_DECLARE_METATYPE(GammaRay::ObjectWrapper<Class>) \
Q_DECLARE_METATYPE(GammaRay::ObjectHandle<Class>) \
Q_DECLARE_METATYPE(GammaRay::WeakObjectHandle<Class>) \



namespace GammaRay {


template<int N> struct __number : public __number<N - 1> {
    static constexpr int value = N;
    static constexpr __number<N-1> prev() { return {}; }
};
// Specialize for 0 to break the recursion.
template<> struct __number<0> { static constexpr int value = 0; };

template<typename T1, typename T2> using second_t = T2;


template<typename T, typename Enable = void>
struct cachingDisabled : public std::false_type {};
template<typename T>
struct cachingDisabled<T, typename T::disableCaching_t> : public std::true_type {};

template<typename Class> class ObjectWrapper {
public:
    using isDummyWrapper_t = void;
};

class ObjectWrapperBase
{
protected:
    struct ControlBlockBase {};
    template<typename Class, typename Data_t>
    struct ControlBlock : public ControlBlockBase
    {
        Class *object;
        Data_t dataStorage;
        std::vector<QMetaObject::Connection> connections;
        QSemaphore semaphore;

        explicit ControlBlock(Class *object);
        explicit ControlBlock(Class *object, Data_t &&dataStorage);

        ~ControlBlock();
    };

    template<typename Derived_t>
    static constexpr bool cachingEnabled(Derived_t *self);

    template<typename Derived_t>
    static void initialize(Derived_t *self);

    template<int storageIndex, int Flags, typename Derived_t, typename CommandFunc_t, typename std::enable_if<!(Flags & QProp)>::type* = nullptr>
    static void connectToUpdates(Derived_t *, CommandFunc_t, const char*) {}

    template<int storageIndex, int Flags, typename Derived_t, typename CommandFunc_t, typename std::enable_if<Flags & QProp>::type* = nullptr>
    static void connectToUpdates(Derived_t *self, CommandFunc_t command, const char* propertyName);

    template<int storageIndex, typename Derived_t, typename CommandFunc_t, typename SignalFunc_t>
    static void connectToUpdates(Derived_t *self, CommandFunc_t command, SignalFunc_t signal);

    template<typename Class, typename ...Args_t>
    static GammaRay::MetaObjectImpl<Class> makeStaticMetaObject(const QString &className, const std::tuple<Args_t...> &properties);

    friend class ObjectShadowDataRepository;
};



template<typename T>
class ObjectHandle
{
public:
    using value_type = T;

    explicit ObjectHandle(T *obj);
    explicit ObjectHandle(std::shared_ptr<typename ObjectWrapper<T>::ControlData> controlBlock);
    explicit ObjectHandle() = default;

    explicit operator bool() const;
    explicit operator T*() const;

    inline const ObjectWrapper<T> *operator->() const;
    inline const ObjectWrapper<T> &operator*() const;
    inline ObjectWrapper<T> &operator*();

    inline T *object() const;

    template<typename Func, typename ...Args>
    auto call(Func &&f, Args &&...args) -> std::future<decltype(std::declval<T*>()->*f(args...))>;


    static MetaObject *staticMetaObject();

    void refresh();

private:
    ObjectWrapper<T> m_objectWrapper;
};

template<typename T>
class WeakObjectHandle
{
public:
    explicit WeakObjectHandle() = default;
    explicit WeakObjectHandle(std::weak_ptr<typename ObjectWrapper<T>::ControlData> controlBlock);
    explicit operator bool() const;

    static WeakObjectHandle nullhandle();

    inline ObjectHandle<T> lock() const;

    // TODO: Do we actually want implicit locking for WeakObjectHandle?
    inline const ObjectHandle<T> operator->() const;
    inline const ObjectWrapper<T> &operator*() const;
    inline ObjectWrapper<T> &operator*();
    inline T *object() const;

private:
    std::weak_ptr<typename ObjectWrapper<T>::ControlData> m_controlBlock;
};


class ObjectShadowDataRepository
{
public:
    static ObjectShadowDataRepository *instance();

    template<typename Class>
    static ObjectHandle<Class> handleForObject(Class *obj);

    template<typename Class>
    static WeakObjectHandle<Class> weakHandleForObject(Class *obj);

private:
    explicit ObjectShadowDataRepository() = default;
    friend class Probe;

    QHash<void*, std::weak_ptr<ObjectWrapperBase::ControlBlockBase>> m_objectToWrapperControlBlockMap;

    friend class ObjectWrapperBase;
    friend class ObjectWrapperTest;
    template<typename Class, typename Data_t>
    friend struct ObjectWrapperBase::ControlBlock;
};




// === ObjectWrapperBase ===

template<typename T>
auto checkCorrectThread(T*) -> typename std::enable_if<!std::is_base_of<QObject, T>::value, bool>::type
{
    return true;
}
template<typename T>
auto checkCorrectThread(T *obj) -> typename std::enable_if<std::is_base_of<QObject, T>::value, bool>::type
{
    return obj->thread() == QThread::currentThread();
}
template<typename T>
auto checkValidObject(T *obj) -> typename std::enable_if<!std::is_base_of<QObject, T>::value, bool>::type
{
    return obj != nullptr;
}
template<typename T>
auto checkValidObject(T *obj) -> typename std::enable_if<std::is_base_of<QObject, T>::value, bool>::type
{
    return Probe::instance()->isValidObject(obj);
}


template<typename Derived_t>
void ObjectWrapperBase::initialize(Derived_t *self)
{
    if (!checkValidObject(self->object)) {
        return;
    }
    self->m_control = std::make_shared<typename Derived_t::ControlData>(self->object);
    std::weak_ptr<ObjectWrapperBase::ControlBlockBase> controlWeakPtr { std::static_pointer_cast<ObjectWrapperBase::ControlBlockBase>(self->m_control) };
    ObjectShadowDataRepository::instance()->m_objectToWrapperControlBlockMap.insert(self->object, controlWeakPtr );

    Q_ASSERT_X(checkCorrectThread(self->object), "ObjectHandle", "ObjectHandles can only be created from the thread which the wrapped QObject belongs to.");

    QSemaphoreReleaser releaser { self->m_control->semaphore }; // releases the first (and only) resource

    IF_CONSTEXPR (!cachingDisabled<Derived_t>::value) {
        QMutexLocker locker { Probe::objectLock() };
        self->m_control->dataStorage = __data(self, __number<255>());

        __connectToUpdates(self, __number<255>{});
    }
}

template<int storageIndex, int Flags, typename Derived_t, typename CommandFunc_t, typename std::enable_if<Flags & QProp>::type* = nullptr>
void ObjectWrapperBase::connectToUpdates(Derived_t *self, CommandFunc_t fetchFunction, const char* propertyName)
{
    static_assert(std::is_base_of<QObject, typename Derived_t::value_type>::value, "members with notify signals can only be defined for QObject-derived types.");
    auto mo = self->object->metaObject();
    auto prop = mo->property(mo->indexOfProperty(propertyName));

    if (!prop.hasNotifySignal()) {
        return;
    }

    auto controlPtr = std::weak_ptr<typename Derived_t::ControlData> {self->m_control};
    auto f = [controlPtr, fetchFunction]() { // We may not capture self here, because the handle might be moved.
        std::cout << "Updating cache."<< storageIndex <<"\n";
        QMutexLocker locker { Probe::objectLock() };
        auto control = controlPtr.lock();
        control->semaphore.acquire();
        QSemaphoreReleaser releaser { control->semaphore };
        std::get< storageIndex >(control->dataStorage) = fetchFunction(control->object);
    };

    auto connection = QObjectPrivate::connect(self->object,
                                                prop.notifySignal().methodIndex(),
                                                new QtPrivate::QFunctorSlotObjectWithNoArgs<decltype(f), void>(std::move(f)),
                                                Qt::DirectConnection
    );

    self->m_control->connections.push_back(connection);
}


template<int storageIndex, typename Derived_t, typename CommandFunc_t, typename SignalFunc_t>
void ObjectWrapperBase::connectToUpdates(Derived_t *self, CommandFunc_t command, SignalFunc_t signal)
{
    static_assert(std::is_base_of<QObject, typename Derived_t::value_type>::value, "members with notify signals can only be defined for QObject-derived types.");
    auto controlPtr = std::weak_ptr<typename Derived_t::ControlData> {self->m_control};
    auto f = [controlPtr, command]() {
        std::cout << "Updating cache."<< storageIndex <<"\n";
        QMutexLocker locker { Probe::objectLock() };
        auto control = controlPtr.lock();
        control->semaphore.acquire();
        QSemaphoreReleaser releaser { control->semaphore };
        std::get< storageIndex >(control->dataStorage) = command(control->object);
    };

    auto connection = QObject::connect(self->object, signal, f);

    self->m_control->connections.push_back(connection);
}


// === ObjectHandle ===

template<typename T>
ObjectHandle<T>::ObjectHandle(T *obj)
    : m_objectWrapper(obj)
{}

template<typename T>
ObjectHandle<T>::ObjectHandle(std::shared_ptr<typename ObjectWrapper<T>::ControlData> controlBlock)
    : m_objectWrapper(std::move(controlBlock))
{}

template<typename T>
ObjectHandle<T>::operator bool() const
{
    return Probe::instance()->isValidObject(m_objectWrapper->object);
}

template<typename T>
ObjectHandle<T>::operator T*() const
{
    return m_objectWrapper->object;
}

template<typename T>
const ObjectWrapper<T> *ObjectHandle<T>::operator->() const
{
    return &m_objectWrapper;
}

template<typename T>
const ObjectWrapper<T> &ObjectHandle<T>::operator*() const
{
    return m_objectWrapper;
}

template<typename T>
ObjectWrapper<T> &ObjectHandle<T>::operator*()
{
    return m_objectWrapper;
}

template<typename T>
T *ObjectHandle<T>::object() const
{
    return m_objectWrapper.object;
}



template<typename T>
template<typename Func, typename ...Args>
auto ObjectHandle<T>::call(Func &&f, Args &&...args) -> std::future<decltype(std::declval<T*>()->*f(args...))>
{
    if (!Probe::instance()->isValidObject(m_objectWrapper->object)) {
        return {};
    }

    std::promise<decltype(m_objectWrapper->object->*f(args...))> p;
    auto future = p.get_future();
    if (m_objectWrapper->object->thread == QThread::currentThread()) {
        p.set_value(m_objectWrapper->object->*f(args...));
    } else {
        T *ptr = m_objectWrapper->object;
        QMetaObject::invokeMethod(m_objectWrapper->object, [p, ptr, f, args...]() {
            p.set_value(ptr->*f(args...));
        }, Qt::QueuedConnection);
    }
    return future;
}

template<typename T>
void ObjectHandle<T>::refresh()
{
    m_objectWrapper.m_control->dataStorage = __data(&m_objectWrapper, __number<255>());
}


template<typename T>
MetaObject *ObjectHandle<T>::staticMetaObject()
{
    return decltype(m_objectWrapper)::staticMetaObject();
}

// === WeakObjectHandle ===

template<typename T>
WeakObjectHandle<T>::WeakObjectHandle(std::weak_ptr<typename ObjectWrapper<T>::ControlData> controlBlock)
: m_controlBlock(std::move(controlBlock))
{}

template<typename T>
WeakObjectHandle<T>::operator bool() const
{
    return !m_controlBlock.expired() && Probe::instance()->isValidObject(m_controlBlock.lock()->object); // FIXME we should not need to lock this just to do a null check
}

template<typename T>
ObjectHandle<T> WeakObjectHandle<T>::lock() const
{
    return ObjectHandle<T> { m_controlBlock.lock() };
}
template<typename T>
WeakObjectHandle<T> WeakObjectHandle<T>::nullhandle()
{
    return WeakObjectHandle<T> { std::weak_ptr<typename ObjectWrapper<T>::ControlData>{} };
}

template<typename T>
const ObjectHandle<T> WeakObjectHandle<T>::operator->() const
{
    return lock();
}

template<typename T>
const ObjectWrapper<T> &WeakObjectHandle<T>::operator*() const
{
    return *lock();
}

template<typename T>
ObjectWrapper<T> &WeakObjectHandle<T>::operator*()
{
    return *lock();
}

template<typename T>
T *WeakObjectHandle<T>::object() const
{
    return m_controlBlock.lock()->object;
}

// === ObjectShadowDataRepository ===

ObjectShadowDataRepository *ObjectShadowDataRepository::instance()
{
//     return Probe::instance()->objectShadowDataRepository();
    static ObjectShadowDataRepository *self = new ObjectShadowDataRepository();
    return self;
}

template<typename Class>
ObjectHandle<Class> ObjectShadowDataRepository::handleForObject(Class *obj)
{
    auto self = instance();

    if (self->m_objectToWrapperControlBlockMap.contains(obj)) {
        auto controlBasePtr = self->m_objectToWrapperControlBlockMap.value(obj).lock();
        auto controlPtr = std::static_pointer_cast<typename ObjectWrapper<Class>::ControlData>(controlBasePtr);
        return ObjectHandle<Class> { std::move(controlPtr) };
    } else {
        return ObjectHandle<Class> { obj }; // will insert itself into the map
    }
}

template<typename Class>
WeakObjectHandle<Class> ObjectShadowDataRepository::weakHandleForObject(Class *obj)
{
    if (!obj) {
        return WeakObjectHandle<Class> {};
    }

    auto self = instance();

    Q_ASSERT_X(self->m_objectToWrapperControlBlockMap.contains(obj), "weakHandleForObject", "Obtaining a weak handle requires a (strong) handle to already exist.");

    auto controlBasePtr = self->m_objectToWrapperControlBlockMap.value(obj).lock();
    std::weak_ptr<typename ObjectWrapper<Class>::ControlData> controlPtr =
        std::static_pointer_cast<typename ObjectWrapper<Class>::ControlData>(controlBasePtr);
    return WeakObjectHandle<Class> { std::move(controlPtr) };
}

template<typename Class, typename Data_t>
ObjectWrapperBase::ControlBlock<Class, Data_t>::ControlBlock(Class *obj, Data_t &&dataStorage)
    : object(obj)
    , dataStorage(dataStorage)
{
    ObjectShadowDataRepository::instance()->m_objectToWrapperControlBlockMap.insert(object, this);
}

template<typename Class, typename Data_t>
ObjectWrapperBase::ControlBlock<Class, Data_t>::ControlBlock(Class *obj)
: object(obj)
{
}


template<typename Class, typename Data_t>
ObjectWrapperBase::ControlBlock<Class, Data_t>::~ControlBlock()
{
    for (auto &&c : connections) {
        QObject::disconnect(c);
    }
    ObjectShadowDataRepository::instance()->m_objectToWrapperControlBlockMap.remove(object);
}

}


template<int flags, typename T>
auto wrap(T &&value) -> second_t<typename ObjectWrapper<T>::isDummyWrapper_t, T>
{
    return std::forward<T>(value);
}
template<int flags, typename T>
auto wrap(T *object) -> second_t<typename ObjectWrapper<T>::value_type, typename std::enable_if<flags & NonOwningPointer, WeakObjectHandle<T>>::type>
{
    return ObjectShadowDataRepository::weakHandleForObject(object);
}
template<int flags, typename T>
auto wrap(T *object) -> second_t<typename ObjectWrapper<T>::value_type, typename std::enable_if<flags & OwningPointer, ObjectHandle<T>>::type>
{
    return ObjectShadowDataRepository::handleForObject(object);
}
template<int flags, typename T>
auto wrap(const QList<T*> &list) -> second_t<typename ObjectWrapper<T>::value_type, QList<WeakObjectHandle<T>>>
{
    QList<WeakObjectHandle<T>> handleList;
    std::transform(list.cBegin(), list.cEnd(), handleList.begin(), [](T *t) { return ObjectShadowDataRepository::weakHandleForObject(t); });
    return handleList;
}
template<int flags, typename T>
auto wrap(QVector<T*> list) -> second_t<typename ObjectWrapper<T>::value_type, typename std::enable_if<flags & NonOwningPointer, QVector<WeakObjectHandle<T>>>::type>
{
    QVector<WeakObjectHandle<T>> handleList;
    handleList.reserve(list.size());
    for (T *t : qAsConst(list)) {
        handleList.push_back(ObjectShadowDataRepository::weakHandleForObject(t));
    }
    //     std::transform(list.cbegin(), list.cend(), handleList.begin(), [](T *t) { return WeakObjectHandle<T> { t }; });
    return handleList;
}
template<int flags, typename T>
auto wrap(QVector<T*> list) -> second_t<typename ObjectWrapper<T>::value_type, typename std::enable_if<flags & OwningPointer, QVector<ObjectHandle<T>>>::type>
{
    QVector<ObjectHandle<T>> handleList;
    handleList.reserve(list.size());
    for (T *t : qAsConst(list)) {
        handleList.push_back(ObjectShadowDataRepository::handleForObject(t));
    }
    //     std::transform(list.cbegin(), list.cend(), handleList.begin(), [](T *t) { return WeakObjectHandle<T> { t }; });
    return handleList;
}



//TODO: calling setters, access through d-ptr, QObject-properties as ObjectWrappers (also object-list-properties), central data cache, meta object, updates
//TODO: Check threading


// TODO: Revise shared_ptr-model: we'd actually need GC, because the handles reference each other.
// TODO: Delete the handles when the QObject is destroyed => connect to destroyed-signal => maybe we don't actually need shared-pointers at all?

// TODO: Look at Event-monitor, lazy-properties, look at Bindings und Signal-Slot connections

#endif // GAMMARAY_OBJECTHANDLE_H
