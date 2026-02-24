/****************************************************************************
** Meta object code from reading C++ file 'DaemonClient.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../DaemonClient.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DaemonClient.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.4.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
namespace {
struct qt_meta_stringdata_DaemonClient_t {
    uint offsetsAndSizes[90];
    char stringdata0[13];
    char stringdata1[10];
    char stringdata2[1];
    char stringdata3[13];
    char stringdata4[13];
    char stringdata5[14];
    char stringdata6[9];
    char stringdata7[6];
    char stringdata8[7];
    char stringdata9[9];
    char stringdata10[8];
    char stringdata11[6];
    char stringdata12[9];
    char stringdata13[17];
    char stringdata14[6];
    char stringdata15[7];
    char stringdata16[9];
    char stringdata17[9];
    char stringdata18[14];
    char stringdata19[8];
    char stringdata20[5];
    char stringdata21[5];
    char stringdata22[8];
    char stringdata23[16];
    char stringdata24[11];
    char stringdata25[15];
    char stringdata26[9];
    char stringdata27[21];
    char stringdata28[5];
    char stringdata29[17];
    char stringdata30[17];
    char stringdata31[9];
    char stringdata32[11];
    char stringdata33[3];
    char stringdata34[6];
    char stringdata35[20];
    char stringdata36[14];
    char stringdata37[12];
    char stringdata38[12];
    char stringdata39[4];
    char stringdata40[12];
    char stringdata41[19];
    char stringdata42[16];
    char stringdata43[16];
    char stringdata44[17];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_DaemonClient_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_DaemonClient_t qt_meta_stringdata_DaemonClient = {
    {
        QT_MOC_LITERAL(0, 12),  // "DaemonClient"
        QT_MOC_LITERAL(13, 9),  // "connected"
        QT_MOC_LITERAL(23, 0),  // ""
        QT_MOC_LITERAL(24, 12),  // "disconnected"
        QT_MOC_LITERAL(37, 12),  // "reconnecting"
        QT_MOC_LITERAL(50, 13),  // "statusUpdated"
        QT_MOC_LITERAL(64, 8),  // "uint32_t"
        QT_MOC_LITERAL(73, 5),  // "peers"
        QT_MOC_LITERAL(79, 6),  // "chunks"
        QT_MOC_LITERAL(86, 8),  // "uint64_t"
        QT_MOC_LITERAL(95, 7),  // "storage"
        QT_MOC_LITERAL(103, 5),  // "relay"
        QT_MOC_LITERAL(109, 8),  // "listener"
        QT_MOC_LITERAL(118, 16),  // "dhtStatusUpdated"
        QT_MOC_LITERAL(135, 5),  // "nodes"
        QT_MOC_LITERAL(141, 6),  // "values"
        QT_MOC_LITERAL(148, 8),  // "msgsSent"
        QT_MOC_LITERAL(157, 8),  // "msgsRecv"
        QT_MOC_LITERAL(166, 13),  // "eventReceived"
        QT_MOC_LITERAL(180, 7),  // "uint8_t"
        QT_MOC_LITERAL(188, 4),  // "type"
        QT_MOC_LITERAL(193, 4),  // "opId"
        QT_MOC_LITERAL(198, 7),  // "payload"
        QT_MOC_LITERAL(206, 15),  // "transferStarted"
        QT_MOC_LITERAL(222, 10),  // "transferId"
        QT_MOC_LITERAL(233, 14),  // "transferFailed"
        QT_MOC_LITERAL(248, 8),  // "errorMsg"
        QT_MOC_LITERAL(257, 20),  // "transferListReceived"
        QT_MOC_LITERAL(278, 4),  // "data"
        QT_MOC_LITERAL(283, 16),  // "fileListReceived"
        QT_MOC_LITERAL(300, 16),  // "fileStoreStarted"
        QT_MOC_LITERAL(317, 8),  // "filename"
        QT_MOC_LITERAL(326, 10),  // "fileStored"
        QT_MOC_LITERAL(337, 2),  // "ok"
        QT_MOC_LITERAL(340, 5),  // "error"
        QT_MOC_LITERAL(346, 19),  // "fileRetrieveStarted"
        QT_MOC_LITERAL(366, 13),  // "fileRetrieved"
        QT_MOC_LITERAL(380, 11),  // "fileDeleted"
        QT_MOC_LITERAL(392, 11),  // "keyExported"
        QT_MOC_LITERAL(404, 3),  // "key"
        QT_MOC_LITERAL(408, 11),  // "keyImported"
        QT_MOC_LITERAL(420, 18),  // "configListReceived"
        QT_MOC_LITERAL(439, 15),  // "configSetResult"
        QT_MOC_LITERAL(455, 15),  // "restartRequired"
        QT_MOC_LITERAL(471, 16)   // "peerListReceived"
    },
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
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_DaemonClient[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
      20,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      20,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,  134,    2, 0x06,    1 /* Public */,
       3,    0,  135,    2, 0x06,    2 /* Public */,
       4,    0,  136,    2, 0x06,    3 /* Public */,
       5,    5,  137,    2, 0x06,    4 /* Public */,
      13,    4,  148,    2, 0x06,   10 /* Public */,
      18,    3,  157,    2, 0x06,   15 /* Public */,
      23,    1,  164,    2, 0x06,   19 /* Public */,
      25,    1,  167,    2, 0x06,   21 /* Public */,
      27,    1,  170,    2, 0x06,   23 /* Public */,
      29,    1,  173,    2, 0x06,   25 /* Public */,
      30,    1,  176,    2, 0x06,   27 /* Public */,
      32,    2,  179,    2, 0x06,   29 /* Public */,
      35,    1,  184,    2, 0x06,   32 /* Public */,
      36,    2,  187,    2, 0x06,   34 /* Public */,
      37,    2,  192,    2, 0x06,   37 /* Public */,
      38,    3,  197,    2, 0x06,   40 /* Public */,
      40,    2,  204,    2, 0x06,   44 /* Public */,
      41,    1,  209,    2, 0x06,   47 /* Public */,
      42,    3,  212,    2, 0x06,   49 /* Public */,
      44,    1,  219,    2, 0x06,   53 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 6, 0x80000000 | 6, 0x80000000 | 9, QMetaType::Bool, QMetaType::Bool,    7,    8,   10,   11,   12,
    QMetaType::Void, 0x80000000 | 6, 0x80000000 | 6, 0x80000000 | 9, 0x80000000 | 9,   14,   15,   16,   17,
    QMetaType::Void, 0x80000000 | 19, 0x80000000 | 6, QMetaType::QByteArray,   20,   21,   22,
    QMetaType::Void, 0x80000000 | 6,   24,
    QMetaType::Void, QMetaType::QString,   26,
    QMetaType::Void, QMetaType::QByteArray,   28,
    QMetaType::Void, QMetaType::QByteArray,   28,
    QMetaType::Void, QMetaType::QString,   31,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   33,   34,
    QMetaType::Void, QMetaType::QString,   31,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   33,   34,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   33,   34,
    QMetaType::Void, QMetaType::Bool, QMetaType::QByteArray, QMetaType::QString,   33,   39,   34,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   33,   34,
    QMetaType::Void, QMetaType::QByteArray,   28,
    QMetaType::Void, QMetaType::Bool, QMetaType::Bool, QMetaType::QString,   33,   43,   34,
    QMetaType::Void, QMetaType::QByteArray,   28,

       0        // eod
};

Q_CONSTINIT const QMetaObject DaemonClient::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_DaemonClient.offsetsAndSizes,
    qt_meta_data_DaemonClient,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_DaemonClient_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<DaemonClient, std::true_type>,
        // method 'connected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'disconnected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'reconnecting'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'statusUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint64_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'dhtStatusUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint64_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint64_t, std::false_type>,
        // method 'eventReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint8_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<QByteArray, std::false_type>,
        // method 'transferStarted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        // method 'transferFailed'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'transferListReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QByteArray, std::false_type>,
        // method 'fileListReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QByteArray, std::false_type>,
        // method 'fileStoreStarted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'fileStored'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'fileRetrieveStarted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'fileRetrieved'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'fileDeleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'keyExported'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<QByteArray, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'keyImported'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'configListReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QByteArray, std::false_type>,
        // method 'configSetResult'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'peerListReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QByteArray, std::false_type>
    >,
    nullptr
} };

void DaemonClient::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<DaemonClient *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->connected(); break;
        case 1: _t->disconnected(); break;
        case 2: _t->reconnecting(); break;
        case 3: _t->statusUpdated((*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<uint64_t>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[4])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[5]))); break;
        case 4: _t->dhtStatusUpdated((*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<uint64_t>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<uint64_t>>(_a[4]))); break;
        case 5: _t->eventReceived((*reinterpret_cast< std::add_pointer_t<uint8_t>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[3]))); break;
        case 6: _t->transferStarted((*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[1]))); break;
        case 7: _t->transferFailed((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 8: _t->transferListReceived((*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 9: _t->fileListReceived((*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 10: _t->fileStoreStarted((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 11: _t->fileStored((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 12: _t->fileRetrieveStarted((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 13: _t->fileRetrieved((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 14: _t->fileDeleted((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 15: _t->keyExported((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[3]))); break;
        case 16: _t->keyImported((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 17: _t->configListReceived((*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 18: _t->configSetResult((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[3]))); break;
        case 19: _t->peerListReceived((*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (DaemonClient::*)();
            if (_t _q_method = &DaemonClient::connected; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)();
            if (_t _q_method = &DaemonClient::disconnected; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)();
            if (_t _q_method = &DaemonClient::reconnecting; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(uint32_t , uint32_t , uint64_t , bool , bool );
            if (_t _q_method = &DaemonClient::statusUpdated; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(uint32_t , uint32_t , uint64_t , uint64_t );
            if (_t _q_method = &DaemonClient::dhtStatusUpdated; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(uint8_t , uint32_t , QByteArray );
            if (_t _q_method = &DaemonClient::eventReceived; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(uint32_t );
            if (_t _q_method = &DaemonClient::transferStarted; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(QString );
            if (_t _q_method = &DaemonClient::transferFailed; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 7;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(QByteArray );
            if (_t _q_method = &DaemonClient::transferListReceived; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 8;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(QByteArray );
            if (_t _q_method = &DaemonClient::fileListReceived; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 9;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(QString );
            if (_t _q_method = &DaemonClient::fileStoreStarted; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 10;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(bool , QString );
            if (_t _q_method = &DaemonClient::fileStored; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 11;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(QString );
            if (_t _q_method = &DaemonClient::fileRetrieveStarted; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 12;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(bool , QString );
            if (_t _q_method = &DaemonClient::fileRetrieved; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 13;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(bool , QString );
            if (_t _q_method = &DaemonClient::fileDeleted; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 14;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(bool , QByteArray , QString );
            if (_t _q_method = &DaemonClient::keyExported; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 15;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(bool , QString );
            if (_t _q_method = &DaemonClient::keyImported; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 16;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(QByteArray );
            if (_t _q_method = &DaemonClient::configListReceived; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 17;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(bool , bool , QString );
            if (_t _q_method = &DaemonClient::configSetResult; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 18;
                return;
            }
        }
        {
            using _t = void (DaemonClient::*)(QByteArray );
            if (_t _q_method = &DaemonClient::peerListReceived; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 19;
                return;
            }
        }
    }
}

const QMetaObject *DaemonClient::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DaemonClient::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_DaemonClient.stringdata0))
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
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
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
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void DaemonClient::dhtStatusUpdated(uint32_t _t1, uint32_t _t2, uint64_t _t3, uint64_t _t4)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void DaemonClient::eventReceived(uint8_t _t1, uint32_t _t2, QByteArray _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void DaemonClient::transferStarted(uint32_t _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void DaemonClient::transferFailed(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void DaemonClient::transferListReceived(QByteArray _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}

// SIGNAL 9
void DaemonClient::fileListReceived(QByteArray _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 9, _a);
}

// SIGNAL 10
void DaemonClient::fileStoreStarted(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 10, _a);
}

// SIGNAL 11
void DaemonClient::fileStored(bool _t1, QString _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 11, _a);
}

// SIGNAL 12
void DaemonClient::fileRetrieveStarted(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 12, _a);
}

// SIGNAL 13
void DaemonClient::fileRetrieved(bool _t1, QString _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 13, _a);
}

// SIGNAL 14
void DaemonClient::fileDeleted(bool _t1, QString _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 14, _a);
}

// SIGNAL 15
void DaemonClient::keyExported(bool _t1, QByteArray _t2, QString _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 15, _a);
}

// SIGNAL 16
void DaemonClient::keyImported(bool _t1, QString _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 16, _a);
}

// SIGNAL 17
void DaemonClient::configListReceived(QByteArray _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 17, _a);
}

// SIGNAL 18
void DaemonClient::configSetResult(bool _t1, bool _t2, QString _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 18, _a);
}

// SIGNAL 19
void DaemonClient::peerListReceived(QByteArray _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 19, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
