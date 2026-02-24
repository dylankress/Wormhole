/****************************************************************************
** Meta object code from reading C++ file 'IpcWorker.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../IpcWorker.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'IpcWorker.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN9IpcWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto IpcWorker::qt_create_metaobjectdata<qt_meta_tag_ZN9IpcWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "IpcWorker",
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
        "peerListReceived",
        "start",
        "stop",
        "sendFile",
        "path",
        "receiveFile",
        "ticket",
        "outputDir",
        "cancelTransfer",
        "listTransfers",
        "listFiles",
        "storeFile",
        "retrieveFile",
        "hash",
        "outputPath",
        "deleteFile",
        "exportKey",
        "importKey",
        "listConfig",
        "setConfig",
        "value",
        "listPeers",
        "getDhtStatus",
        "tryConnect",
        "pollStatus",
        "processEvents"
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
        // Slot 'start'
        QtMocHelpers::SlotData<void()>(45, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'stop'
        QtMocHelpers::SlotData<void()>(46, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'sendFile'
        QtMocHelpers::SlotData<void(const QString &)>(47, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 48 },
        }}),
        // Slot 'receiveFile'
        QtMocHelpers::SlotData<void(const QString &, const QString &)>(49, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 50 }, { QMetaType::QString, 51 },
        }}),
        // Slot 'cancelTransfer'
        QtMocHelpers::SlotData<void(uint32_t)>(52, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 21 },
        }}),
        // Slot 'listTransfers'
        QtMocHelpers::SlotData<void()>(53, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'listFiles'
        QtMocHelpers::SlotData<void()>(54, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'storeFile'
        QtMocHelpers::SlotData<void(const QString &)>(55, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 48 },
        }}),
        // Slot 'retrieveFile'
        QtMocHelpers::SlotData<void(const QByteArray &, const QString &)>(56, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QByteArray, 57 }, { QMetaType::QString, 58 },
        }}),
        // Slot 'deleteFile'
        QtMocHelpers::SlotData<void(const QByteArray &)>(59, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QByteArray, 57 },
        }}),
        // Slot 'exportKey'
        QtMocHelpers::SlotData<void(const QByteArray &)>(60, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QByteArray, 57 },
        }}),
        // Slot 'importKey'
        QtMocHelpers::SlotData<void(const QByteArray &, const QByteArray &)>(61, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QByteArray, 57 }, { QMetaType::QByteArray, 39 },
        }}),
        // Slot 'listConfig'
        QtMocHelpers::SlotData<void()>(62, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setConfig'
        QtMocHelpers::SlotData<void(const QString &, const QString &)>(63, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 39 }, { QMetaType::QString, 64 },
        }}),
        // Slot 'listPeers'
        QtMocHelpers::SlotData<void()>(65, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'getDhtStatus'
        QtMocHelpers::SlotData<void()>(66, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'tryConnect'
        QtMocHelpers::SlotData<void()>(67, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'pollStatus'
        QtMocHelpers::SlotData<void()>(68, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'processEvents'
        QtMocHelpers::SlotData<void()>(69, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<IpcWorker, qt_meta_tag_ZN9IpcWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject IpcWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9IpcWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9IpcWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN9IpcWorkerE_t>.metaTypes,
    nullptr
} };

void IpcWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<IpcWorker *>(_o);
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
        case 20: _t->start(); break;
        case 21: _t->stop(); break;
        case 22: _t->sendFile((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 23: _t->receiveFile((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 24: _t->cancelTransfer((*reinterpret_cast<std::add_pointer_t<uint32_t>>(_a[1]))); break;
        case 25: _t->listTransfers(); break;
        case 26: _t->listFiles(); break;
        case 27: _t->storeFile((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 28: _t->retrieveFile((*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 29: _t->deleteFile((*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 30: _t->exportKey((*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 31: _t->importKey((*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[2]))); break;
        case 32: _t->listConfig(); break;
        case 33: _t->setConfig((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 34: _t->listPeers(); break;
        case 35: _t->getDhtStatus(); break;
        case 36: _t->tryConnect(); break;
        case 37: _t->pollStatus(); break;
        case 38: _t->processEvents(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)()>(_a, &IpcWorker::connected, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)()>(_a, &IpcWorker::disconnected, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)()>(_a, &IpcWorker::reconnecting, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(uint32_t , uint32_t , uint64_t , bool , bool )>(_a, &IpcWorker::statusUpdated, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(uint32_t , uint32_t , uint64_t , uint64_t )>(_a, &IpcWorker::dhtStatusUpdated, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(uint8_t , uint32_t , QByteArray )>(_a, &IpcWorker::eventReceived, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(uint32_t )>(_a, &IpcWorker::transferStarted, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(QString )>(_a, &IpcWorker::transferFailed, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(QByteArray )>(_a, &IpcWorker::transferListReceived, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(QByteArray )>(_a, &IpcWorker::fileListReceived, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(QString )>(_a, &IpcWorker::fileStoreStarted, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(bool , QString )>(_a, &IpcWorker::fileStored, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(QString )>(_a, &IpcWorker::fileRetrieveStarted, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(bool , QString )>(_a, &IpcWorker::fileRetrieved, 13))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(bool , QString )>(_a, &IpcWorker::fileDeleted, 14))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(bool , QByteArray , QString )>(_a, &IpcWorker::keyExported, 15))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(bool , QString )>(_a, &IpcWorker::keyImported, 16))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(QByteArray )>(_a, &IpcWorker::configListReceived, 17))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(bool , bool , QString )>(_a, &IpcWorker::configSetResult, 18))
            return;
        if (QtMocHelpers::indexOfMethod<void (IpcWorker::*)(QByteArray )>(_a, &IpcWorker::peerListReceived, 19))
            return;
    }
}

const QMetaObject *IpcWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *IpcWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9IpcWorkerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int IpcWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 39)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 39;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 39)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 39;
    }
    return _id;
}

// SIGNAL 0
void IpcWorker::connected()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void IpcWorker::disconnected()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void IpcWorker::reconnecting()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void IpcWorker::statusUpdated(uint32_t _t1, uint32_t _t2, uint64_t _t3, bool _t4, bool _t5)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2, _t3, _t4, _t5);
}

// SIGNAL 4
void IpcWorker::dhtStatusUpdated(uint32_t _t1, uint32_t _t2, uint64_t _t3, uint64_t _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 5
void IpcWorker::eventReceived(uint8_t _t1, uint32_t _t2, QByteArray _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2, _t3);
}

// SIGNAL 6
void IpcWorker::transferStarted(uint32_t _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1);
}

// SIGNAL 7
void IpcWorker::transferFailed(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}

// SIGNAL 8
void IpcWorker::transferListReceived(QByteArray _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1);
}

// SIGNAL 9
void IpcWorker::fileListReceived(QByteArray _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}

// SIGNAL 10
void IpcWorker::fileStoreStarted(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 10, nullptr, _t1);
}

// SIGNAL 11
void IpcWorker::fileStored(bool _t1, QString _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 11, nullptr, _t1, _t2);
}

// SIGNAL 12
void IpcWorker::fileRetrieveStarted(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 12, nullptr, _t1);
}

// SIGNAL 13
void IpcWorker::fileRetrieved(bool _t1, QString _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 13, nullptr, _t1, _t2);
}

// SIGNAL 14
void IpcWorker::fileDeleted(bool _t1, QString _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 14, nullptr, _t1, _t2);
}

// SIGNAL 15
void IpcWorker::keyExported(bool _t1, QByteArray _t2, QString _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 15, nullptr, _t1, _t2, _t3);
}

// SIGNAL 16
void IpcWorker::keyImported(bool _t1, QString _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 16, nullptr, _t1, _t2);
}

// SIGNAL 17
void IpcWorker::configListReceived(QByteArray _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 17, nullptr, _t1);
}

// SIGNAL 18
void IpcWorker::configSetResult(bool _t1, bool _t2, QString _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 18, nullptr, _t1, _t2, _t3);
}

// SIGNAL 19
void IpcWorker::peerListReceived(QByteArray _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 19, nullptr, _t1);
}
QT_WARNING_POP
