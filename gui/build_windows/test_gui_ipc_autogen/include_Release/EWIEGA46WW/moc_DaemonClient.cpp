/****************************************************************************
** Meta object code from reading C++ file 'DaemonClient.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../DaemonClient.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DaemonClient.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN12DaemonClientE_t {};
} // unnamed namespace

template <> constexpr inline auto DaemonClient::qt_create_metaobjectdata<qt_meta_tag_ZN12DaemonClientE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "DaemonClient",
        "connected",
        "",
        "disconnected",
        "reconnecting",
        "statusUpdated",
        "uint32_t",
        "peers",
        "chunks",
        "uint64_t",
        "storage",
        "relay",
        "listener",
        "dhtStatusUpdated",
        "nodes",
        "values",
        "msgsSent",
        "msgsRecv",
        "eventReceived",
        "uint8_t",
        "type",
        "opId",
        "payload",
        "transferStarted",
        "transferId",
        "transferFailed",
        "errorMsg",
        "transferListReceived",
        "data",
        "fileListReceived",
        "fileStoreStarted",
        "filename",
        "fileStored",
        "ok",
        "error",
        "fileRetrieveStarted",
        "fileRetrieved",
        "fileDeleted",
        "keyExported",
        "key",
        "keyImported",
        "configListReceived",
        "configSetResult",
        "restartRequired",
        "peerListReceived"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'connected'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'disconnected'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'reconnecting'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'statusUpdated'
        QtMocHelpers::SignalData<void(uint32_t, uint32_t, uint64_t, bool, bool)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 7 }, { 0x80000000 | 6, 8 }, { 0x80000000 | 9, 10 }, { QMetaType::Bool, 11 },
            { QMetaType::Bool, 12 },
        }}),
        // Signal 'dhtStatusUpdated'
        QtMocHelpers::SignalData<void(uint32_t, uint32_t, uint64_t, uint64_t)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 14 }, { 0x80000000 | 6, 15 }, { 0x80000000 | 9, 16 }, { 0x80000000 | 9, 17 },
        }}),
        // Signal 'eventReceived'
        QtMocHelpers::SignalData<void(uint8_t, uint32_t, QByteArray)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 19, 20 }, { 0x80000000 | 6, 21 }, { QMetaType::QByteArray, 22 },
        }}),
        // Signal 'transferStarted'
        QtMocHelpers::SignalData<void(uint32_t)>(23, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 24 },
        }}),
        // Signal 'transferFailed'
        QtMocHelpers::SignalData<void(QString)>(25, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 26 },
        }}),
        // Signal 'transferListReceived'
        QtMocHelpers::SignalData<void(QByteArray)>(27, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QByteArray, 28 },
        }}),
        // Signal 'fileListReceived'
        QtMocHelpers::SignalData<void(QByteArray)>(29, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QByteArray, 28 },
        }}),
        // Signal 'fileStoreStarted'
        QtMocHelpers::SignalData<void(QString)>(30, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 31 },
        }}),
        // Signal 'fileStored'
        QtMocHelpers::SignalData<void(bool, QString)>(32, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 33 }, { QMetaType::QString, 34 },
        }}),
        // Signal 'fileRetrieveStarted'
        QtMocHelpers::SignalData<void(QString)>(35, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 31 },
        }}),
        // Signal 'fileRetrieved'
        QtMocHelpers::SignalData<void(bool, QString)>(36, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 33 }, { QMetaType::QString, 34 },
        }}),
        // Signal 'fileDeleted'
        QtMocHelpers::SignalData<void(bool, QString)>(37, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 33 }, { QMetaType::QString, 34 },
        }}),
        // Signal 'keyExported'
        QtMocHelpers::SignalData<void(bool, QByteArray, QString)>(38, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 33 }, { QMetaType::QByteArray, 39 }, { QMetaType::QString, 34 },
        }}),
        // Signal 'keyImported'
        QtMocHelpers::SignalData<void(bool, QString)>(40, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 33 }, { QMetaType::QString, 34 },
        }}),
        // Signal 'configListReceived'
        QtMocHelpers::SignalData<void(QByteArray)>(41, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QByteArray, 28 },
        }}),
        // Signal 'configSetResult'
        QtMocHelpers::SignalData<void(bool, bool, QString)>(42, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 33 }, { QMetaType::Bool, 43 }, { QMetaType::QString, 34 },
        }}),
        // Signal 'peerListReceived'
        QtMocHelpers::SignalData<void(QByteArray)>(44, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QByteArray, 28 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<DaemonClient, qt_meta_tag_ZN12DaemonClientE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject DaemonClient::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12DaemonClientE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12DaemonClientE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN12DaemonClientE_t>.metaTypes,
    nullptr
} };

void DaemonClient::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DaemonClient *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->connected(); break;
        case 1: _t->disconnected(); break;
        case 2: _t->reconnecting(); break;
        case 3: _t->statusUpdated((*reinterpret_cast<std::add_pointer_t<uint32_t>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<uint32_t>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<uint64_t>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[5]))); break;
        case 4: _t->dhtStatusUpdated((*reinterpret_cast<std::add_pointer_t<uint32_t>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<uint32_t>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<uint64_t>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<uint64_t>>(_a[4]))); break;
        case 5: _t->eventReceived((*reinterpret_cast<std::add_pointer_t<uint8_t>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<uint32_t>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[3]))); break;
        case 6: _t->transferStarted((*reinterpret_cast<std::add_pointer_t<uint32_t>>(_a[1]))); break;
        case 7: _t->transferFailed((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 8: _t->transferListReceived((*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 9: _t->fileListReceived((*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 10: _t->fileStoreStarted((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 11: _t->fileStored((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 12: _t->fileRetrieveStarted((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 13: _t->fileRetrieved((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 14: _t->fileDeleted((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 15: _t->keyExported((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 16: _t->keyImported((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 17: _t->configListReceived((*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 18: _t->configSetResult((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 19: _t->peerListReceived((*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)()>(_a, &DaemonClient::connected, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)()>(_a, &DaemonClient::disconnected, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)()>(_a, &DaemonClient::reconnecting, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(uint32_t , uint32_t , uint64_t , bool , bool )>(_a, &DaemonClient::statusUpdated, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(uint32_t , uint32_t , uint64_t , uint64_t )>(_a, &DaemonClient::dhtStatusUpdated, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(uint8_t , uint32_t , QByteArray )>(_a, &DaemonClient::eventReceived, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(uint32_t )>(_a, &DaemonClient::transferStarted, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(QString )>(_a, &DaemonClient::transferFailed, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(QByteArray )>(_a, &DaemonClient::transferListReceived, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(QByteArray )>(_a, &DaemonClient::fileListReceived, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(QString )>(_a, &DaemonClient::fileStoreStarted, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(bool , QString )>(_a, &DaemonClient::fileStored, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(QString )>(_a, &DaemonClient::fileRetrieveStarted, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(bool , QString )>(_a, &DaemonClient::fileRetrieved, 13))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(bool , QString )>(_a, &DaemonClient::fileDeleted, 14))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(bool , QByteArray , QString )>(_a, &DaemonClient::keyExported, 15))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(bool , QString )>(_a, &DaemonClient::keyImported, 16))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(QByteArray )>(_a, &DaemonClient::configListReceived, 17))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(bool , bool , QString )>(_a, &DaemonClient::configSetResult, 18))
            return;
        if (QtMocHelpers::indexOfMethod<void (DaemonClient::*)(QByteArray )>(_a, &DaemonClient::peerListReceived, 19))
            return;
    }
}

const QMetaObject *DaemonClient::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DaemonClient::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12DaemonClientE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int DaemonClient::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 20)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 20;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 20)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 20;
    }
    return _id;
}

// SIGNAL 0
void DaemonClient::connected()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void DaemonClient::disconnected()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void DaemonClient::reconnecting()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void DaemonClient::statusUpdated(uint32_t _t1, uint32_t _t2, uint64_t _t3, bool _t4, bool _t5)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2, _t3, _t4, _t5);
}

// SIGNAL 4
void DaemonClient::dhtStatusUpdated(uint32_t _t1, uint32_t _t2, uint64_t _t3, uint64_t _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 5
void DaemonClient::eventReceived(uint8_t _t1, uint32_t _t2, QByteArray _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2, _t3);
}

// SIGNAL 6
void DaemonClient::transferStarted(uint32_t _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1);
}

// SIGNAL 7
void DaemonClient::transferFailed(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}

// SIGNAL 8
void DaemonClient::transferListReceived(QByteArray _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1);
}

// SIGNAL 9
void DaemonClient::fileListReceived(QByteArray _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}

// SIGNAL 10
void DaemonClient::fileStoreStarted(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 10, nullptr, _t1);
}

// SIGNAL 11
void DaemonClient::fileStored(bool _t1, QString _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 11, nullptr, _t1, _t2);
}

// SIGNAL 12
void DaemonClient::fileRetrieveStarted(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 12, nullptr, _t1);
}

// SIGNAL 13
void DaemonClient::fileRetrieved(bool _t1, QString _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 13, nullptr, _t1, _t2);
}

// SIGNAL 14
void DaemonClient::fileDeleted(bool _t1, QString _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 14, nullptr, _t1, _t2);
}

// SIGNAL 15
void DaemonClient::keyExported(bool _t1, QByteArray _t2, QString _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 15, nullptr, _t1, _t2, _t3);
}

// SIGNAL 16
void DaemonClient::keyImported(bool _t1, QString _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 16, nullptr, _t1, _t2);
}

// SIGNAL 17
void DaemonClient::configListReceived(QByteArray _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 17, nullptr, _t1);
}

// SIGNAL 18
void DaemonClient::configSetResult(bool _t1, bool _t2, QString _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 18, nullptr, _t1, _t2, _t3);
}

// SIGNAL 19
void DaemonClient::peerListReceived(QByteArray _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 19, nullptr, _t1);
}
QT_WARNING_POP
