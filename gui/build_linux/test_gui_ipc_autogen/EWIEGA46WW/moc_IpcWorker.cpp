/****************************************************************************
** Meta object code from reading C++ file 'IpcWorker.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../IpcWorker.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'IpcWorker.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_IpcWorker_t {
    uint offsetsAndSizes[140];
    char stringdata0[10];
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
    char stringdata45[6];
    char stringdata46[5];
    char stringdata47[9];
    char stringdata48[5];
    char stringdata49[12];
    char stringdata50[7];
    char stringdata51[10];
    char stringdata52[15];
    char stringdata53[14];
    char stringdata54[10];
    char stringdata55[10];
    char stringdata56[13];
    char stringdata57[5];
    char stringdata58[11];
    char stringdata59[11];
    char stringdata60[10];
    char stringdata61[10];
    char stringdata62[11];
    char stringdata63[10];
    char stringdata64[6];
    char stringdata65[10];
    char stringdata66[13];
    char stringdata67[11];
    char stringdata68[11];
    char stringdata69[14];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_IpcWorker_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_IpcWorker_t qt_meta_stringdata_IpcWorker = {
    {
        QT_MOC_LITERAL(0, 9),  // "IpcWorker"
        QT_MOC_LITERAL(10, 9),  // "connected"
        QT_MOC_LITERAL(20, 0),  // ""
        QT_MOC_LITERAL(21, 12),  // "disconnected"
        QT_MOC_LITERAL(34, 12),  // "reconnecting"
        QT_MOC_LITERAL(47, 13),  // "statusUpdated"
        QT_MOC_LITERAL(61, 8),  // "uint32_t"
        QT_MOC_LITERAL(70, 5),  // "peers"
        QT_MOC_LITERAL(76, 6),  // "chunks"
        QT_MOC_LITERAL(83, 8),  // "uint64_t"
        QT_MOC_LITERAL(92, 7),  // "storage"
        QT_MOC_LITERAL(100, 5),  // "relay"
        QT_MOC_LITERAL(106, 8),  // "listener"
        QT_MOC_LITERAL(115, 16),  // "dhtStatusUpdated"
        QT_MOC_LITERAL(132, 5),  // "nodes"
        QT_MOC_LITERAL(138, 6),  // "values"
        QT_MOC_LITERAL(145, 8),  // "msgsSent"
        QT_MOC_LITERAL(154, 8),  // "msgsRecv"
        QT_MOC_LITERAL(163, 13),  // "eventReceived"
        QT_MOC_LITERAL(177, 7),  // "uint8_t"
        QT_MOC_LITERAL(185, 4),  // "type"
        QT_MOC_LITERAL(190, 4),  // "opId"
        QT_MOC_LITERAL(195, 7),  // "payload"
        QT_MOC_LITERAL(203, 15),  // "transferStarted"
        QT_MOC_LITERAL(219, 10),  // "transferId"
        QT_MOC_LITERAL(230, 14),  // "transferFailed"
        QT_MOC_LITERAL(245, 8),  // "errorMsg"
        QT_MOC_LITERAL(254, 20),  // "transferListReceived"
        QT_MOC_LITERAL(275, 4),  // "data"
        QT_MOC_LITERAL(280, 16),  // "fileListReceived"
        QT_MOC_LITERAL(297, 16),  // "fileStoreStarted"
        QT_MOC_LITERAL(314, 8),  // "filename"
        QT_MOC_LITERAL(323, 10),  // "fileStored"
        QT_MOC_LITERAL(334, 2),  // "ok"
        QT_MOC_LITERAL(337, 5),  // "error"
        QT_MOC_LITERAL(343, 19),  // "fileRetrieveStarted"
        QT_MOC_LITERAL(363, 13),  // "fileRetrieved"
        QT_MOC_LITERAL(377, 11),  // "fileDeleted"
        QT_MOC_LITERAL(389, 11),  // "keyExported"
        QT_MOC_LITERAL(401, 3),  // "key"
        QT_MOC_LITERAL(405, 11),  // "keyImported"
        QT_MOC_LITERAL(417, 18),  // "configListReceived"
        QT_MOC_LITERAL(436, 15),  // "configSetResult"
        QT_MOC_LITERAL(452, 15),  // "restartRequired"
        QT_MOC_LITERAL(468, 16),  // "peerListReceived"
        QT_MOC_LITERAL(485, 5),  // "start"
        QT_MOC_LITERAL(491, 4),  // "stop"
        QT_MOC_LITERAL(496, 8),  // "sendFile"
        QT_MOC_LITERAL(505, 4),  // "path"
        QT_MOC_LITERAL(510, 11),  // "receiveFile"
        QT_MOC_LITERAL(522, 6),  // "ticket"
        QT_MOC_LITERAL(529, 9),  // "outputDir"
        QT_MOC_LITERAL(539, 14),  // "cancelTransfer"
        QT_MOC_LITERAL(554, 13),  // "listTransfers"
        QT_MOC_LITERAL(568, 9),  // "listFiles"
        QT_MOC_LITERAL(578, 9),  // "storeFile"
        QT_MOC_LITERAL(588, 12),  // "retrieveFile"
        QT_MOC_LITERAL(601, 4),  // "hash"
        QT_MOC_LITERAL(606, 10),  // "outputPath"
        QT_MOC_LITERAL(617, 10),  // "deleteFile"
        QT_MOC_LITERAL(628, 9),  // "exportKey"
        QT_MOC_LITERAL(638, 9),  // "importKey"
        QT_MOC_LITERAL(648, 10),  // "listConfig"
        QT_MOC_LITERAL(659, 9),  // "setConfig"
        QT_MOC_LITERAL(669, 5),  // "value"
        QT_MOC_LITERAL(675, 9),  // "listPeers"
        QT_MOC_LITERAL(685, 12),  // "getDhtStatus"
        QT_MOC_LITERAL(698, 10),  // "tryConnect"
        QT_MOC_LITERAL(709, 10),  // "pollStatus"
        QT_MOC_LITERAL(720, 13)   // "processEvents"
    },
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
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_IpcWorker[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
      39,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      20,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,  248,    2, 0x06,    1 /* Public */,
       3,    0,  249,    2, 0x06,    2 /* Public */,
       4,    0,  250,    2, 0x06,    3 /* Public */,
       5,    5,  251,    2, 0x06,    4 /* Public */,
      13,    4,  262,    2, 0x06,   10 /* Public */,
      18,    3,  271,    2, 0x06,   15 /* Public */,
      23,    1,  278,    2, 0x06,   19 /* Public */,
      25,    1,  281,    2, 0x06,   21 /* Public */,
      27,    1,  284,    2, 0x06,   23 /* Public */,
      29,    1,  287,    2, 0x06,   25 /* Public */,
      30,    1,  290,    2, 0x06,   27 /* Public */,
      32,    2,  293,    2, 0x06,   29 /* Public */,
      35,    1,  298,    2, 0x06,   32 /* Public */,
      36,    2,  301,    2, 0x06,   34 /* Public */,
      37,    2,  306,    2, 0x06,   37 /* Public */,
      38,    3,  311,    2, 0x06,   40 /* Public */,
      40,    2,  318,    2, 0x06,   44 /* Public */,
      41,    1,  323,    2, 0x06,   47 /* Public */,
      42,    3,  326,    2, 0x06,   49 /* Public */,
      44,    1,  333,    2, 0x06,   53 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      45,    0,  336,    2, 0x0a,   55 /* Public */,
      46,    0,  337,    2, 0x0a,   56 /* Public */,
      47,    1,  338,    2, 0x0a,   57 /* Public */,
      49,    2,  341,    2, 0x0a,   59 /* Public */,
      52,    1,  346,    2, 0x0a,   62 /* Public */,
      53,    0,  349,    2, 0x0a,   64 /* Public */,
      54,    0,  350,    2, 0x0a,   65 /* Public */,
      55,    1,  351,    2, 0x0a,   66 /* Public */,
      56,    2,  354,    2, 0x0a,   68 /* Public */,
      59,    1,  359,    2, 0x0a,   71 /* Public */,
      60,    1,  362,    2, 0x0a,   73 /* Public */,
      61,    2,  365,    2, 0x0a,   75 /* Public */,
      62,    0,  370,    2, 0x0a,   78 /* Public */,
      63,    2,  371,    2, 0x0a,   79 /* Public */,
      65,    0,  376,    2, 0x0a,   82 /* Public */,
      66,    0,  377,    2, 0x0a,   83 /* Public */,
      67,    0,  378,    2, 0x08,   84 /* Private */,
      68,    0,  379,    2, 0x08,   85 /* Private */,
      69,    0,  380,    2, 0x08,   86 /* Private */,

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

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   48,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,   50,   51,
    QMetaType::Void, 0x80000000 | 6,   21,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   48,
    QMetaType::Void, QMetaType::QByteArray, QMetaType::QString,   57,   58,
    QMetaType::Void, QMetaType::QByteArray,   57,
    QMetaType::Void, QMetaType::QByteArray,   57,
    QMetaType::Void, QMetaType::QByteArray, QMetaType::QByteArray,   57,   39,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,   39,   64,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject IpcWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_IpcWorker.offsetsAndSizes,
    qt_meta_data_IpcWorker,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_IpcWorker_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<IpcWorker, std::true_type>,
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
        QtPrivate::TypeAndForceComplete<QByteArray, std::false_type>,
        // method 'start'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'stop'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'sendFile'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'receiveFile'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'cancelTransfer'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        // method 'listTransfers'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'listFiles'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'storeFile'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'retrieveFile'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QByteArray &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'deleteFile'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QByteArray &, std::false_type>,
        // method 'exportKey'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QByteArray &, std::false_type>,
        // method 'importKey'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QByteArray &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QByteArray &, std::false_type>,
        // method 'listConfig'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setConfig'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'listPeers'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'getDhtStatus'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'tryConnect'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'pollStatus'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'processEvents'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void IpcWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<IpcWorker *>(_o);
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
        case 20: _t->start(); break;
        case 21: _t->stop(); break;
        case 22: _t->sendFile((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 23: _t->receiveFile((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 24: _t->cancelTransfer((*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[1]))); break;
        case 25: _t->listTransfers(); break;
        case 26: _t->listFiles(); break;
        case 27: _t->storeFile((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 28: _t->retrieveFile((*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 29: _t->deleteFile((*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 30: _t->exportKey((*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 31: _t->importKey((*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[2]))); break;
        case 32: _t->listConfig(); break;
        case 33: _t->setConfig((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 34: _t->listPeers(); break;
        case 35: _t->getDhtStatus(); break;
        case 36: _t->tryConnect(); break;
        case 37: _t->pollStatus(); break;
        case 38: _t->processEvents(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (IpcWorker::*)();
            if (_t _q_method = &IpcWorker::connected; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)();
            if (_t _q_method = &IpcWorker::disconnected; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)();
            if (_t _q_method = &IpcWorker::reconnecting; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(uint32_t , uint32_t , uint64_t , bool , bool );
            if (_t _q_method = &IpcWorker::statusUpdated; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(uint32_t , uint32_t , uint64_t , uint64_t );
            if (_t _q_method = &IpcWorker::dhtStatusUpdated; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(uint8_t , uint32_t , QByteArray );
            if (_t _q_method = &IpcWorker::eventReceived; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(uint32_t );
            if (_t _q_method = &IpcWorker::transferStarted; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(QString );
            if (_t _q_method = &IpcWorker::transferFailed; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 7;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(QByteArray );
            if (_t _q_method = &IpcWorker::transferListReceived; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 8;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(QByteArray );
            if (_t _q_method = &IpcWorker::fileListReceived; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 9;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(QString );
            if (_t _q_method = &IpcWorker::fileStoreStarted; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 10;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(bool , QString );
            if (_t _q_method = &IpcWorker::fileStored; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 11;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(QString );
            if (_t _q_method = &IpcWorker::fileRetrieveStarted; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 12;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(bool , QString );
            if (_t _q_method = &IpcWorker::fileRetrieved; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 13;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(bool , QString );
            if (_t _q_method = &IpcWorker::fileDeleted; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 14;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(bool , QByteArray , QString );
            if (_t _q_method = &IpcWorker::keyExported; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 15;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(bool , QString );
            if (_t _q_method = &IpcWorker::keyImported; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 16;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(QByteArray );
            if (_t _q_method = &IpcWorker::configListReceived; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 17;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(bool , bool , QString );
            if (_t _q_method = &IpcWorker::configSetResult; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 18;
                return;
            }
        }
        {
            using _t = void (IpcWorker::*)(QByteArray );
            if (_t _q_method = &IpcWorker::peerListReceived; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 19;
                return;
            }
        }
    }
}

const QMetaObject *IpcWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *IpcWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_IpcWorker.stringdata0))
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
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
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
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void IpcWorker::dhtStatusUpdated(uint32_t _t1, uint32_t _t2, uint64_t _t3, uint64_t _t4)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void IpcWorker::eventReceived(uint8_t _t1, uint32_t _t2, QByteArray _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void IpcWorker::transferStarted(uint32_t _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void IpcWorker::transferFailed(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void IpcWorker::transferListReceived(QByteArray _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}

// SIGNAL 9
void IpcWorker::fileListReceived(QByteArray _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 9, _a);
}

// SIGNAL 10
void IpcWorker::fileStoreStarted(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 10, _a);
}

// SIGNAL 11
void IpcWorker::fileStored(bool _t1, QString _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 11, _a);
}

// SIGNAL 12
void IpcWorker::fileRetrieveStarted(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 12, _a);
}

// SIGNAL 13
void IpcWorker::fileRetrieved(bool _t1, QString _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 13, _a);
}

// SIGNAL 14
void IpcWorker::fileDeleted(bool _t1, QString _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 14, _a);
}

// SIGNAL 15
void IpcWorker::keyExported(bool _t1, QByteArray _t2, QString _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 15, _a);
}

// SIGNAL 16
void IpcWorker::keyImported(bool _t1, QString _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 16, _a);
}

// SIGNAL 17
void IpcWorker::configListReceived(QByteArray _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 17, _a);
}

// SIGNAL 18
void IpcWorker::configSetResult(bool _t1, bool _t2, QString _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 18, _a);
}

// SIGNAL 19
void IpcWorker::peerListReceived(QByteArray _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 19, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
