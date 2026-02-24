/****************************************************************************
** Meta object code from reading C++ file 'PeerListWidget.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../PeerListWidget.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'PeerListWidget.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_PeerListWidget_t {
    uint offsetsAndSizes[38];
    char stringdata0[15];
    char stringdata1[19];
    char stringdata2[1];
    char stringdata3[5];
    char stringdata4[19];
    char stringdata5[9];
    char stringdata6[6];
    char stringdata7[7];
    char stringdata8[9];
    char stringdata9[9];
    char stringdata10[9];
    char stringdata11[16];
    char stringdata12[8];
    char stringdata13[5];
    char stringdata14[5];
    char stringdata15[8];
    char stringdata16[12];
    char stringdata17[15];
    char stringdata18[8];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_PeerListWidget_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_PeerListWidget_t qt_meta_stringdata_PeerListWidget = {
    {
        QT_MOC_LITERAL(0, 14),  // "PeerListWidget"
        QT_MOC_LITERAL(15, 18),  // "onPeerListReceived"
        QT_MOC_LITERAL(34, 0),  // ""
        QT_MOC_LITERAL(35, 4),  // "data"
        QT_MOC_LITERAL(40, 18),  // "onDhtStatusUpdated"
        QT_MOC_LITERAL(59, 8),  // "uint32_t"
        QT_MOC_LITERAL(68, 5),  // "nodes"
        QT_MOC_LITERAL(74, 6),  // "values"
        QT_MOC_LITERAL(81, 8),  // "uint64_t"
        QT_MOC_LITERAL(90, 8),  // "msgsSent"
        QT_MOC_LITERAL(99, 8),  // "msgsRecv"
        QT_MOC_LITERAL(108, 15),  // "onEventReceived"
        QT_MOC_LITERAL(124, 7),  // "uint8_t"
        QT_MOC_LITERAL(132, 4),  // "type"
        QT_MOC_LITERAL(137, 4),  // "opId"
        QT_MOC_LITERAL(142, 7),  // "payload"
        QT_MOC_LITERAL(150, 11),  // "onConnected"
        QT_MOC_LITERAL(162, 14),  // "onDisconnected"
        QT_MOC_LITERAL(177, 7)   // "refresh"
    },
    "PeerListWidget",
    "onPeerListReceived",
    "",
    "data",
    "onDhtStatusUpdated",
    "uint32_t",
    "nodes",
    "values",
    "uint64_t",
    "msgsSent",
    "msgsRecv",
    "onEventReceived",
    "uint8_t",
    "type",
    "opId",
    "payload",
    "onConnected",
    "onDisconnected",
    "refresh"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_PeerListWidget[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       6,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   50,    2, 0x0a,    1 /* Public */,
       4,    4,   53,    2, 0x0a,    3 /* Public */,
      11,    3,   62,    2, 0x0a,    8 /* Public */,
      16,    0,   69,    2, 0x0a,   12 /* Public */,
      17,    0,   70,    2, 0x0a,   13 /* Public */,
      18,    0,   71,    2, 0x0a,   14 /* Public */,

 // slots: parameters
    QMetaType::Void, QMetaType::QByteArray,    3,
    QMetaType::Void, 0x80000000 | 5, 0x80000000 | 5, 0x80000000 | 8, 0x80000000 | 8,    6,    7,    9,   10,
    QMetaType::Void, 0x80000000 | 12, 0x80000000 | 5, QMetaType::QByteArray,   13,   14,   15,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject PeerListWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_PeerListWidget.offsetsAndSizes,
    qt_meta_data_PeerListWidget,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_PeerListWidget_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<PeerListWidget, std::true_type>,
        // method 'onPeerListReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QByteArray, std::false_type>,
        // method 'onDhtStatusUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint64_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint64_t, std::false_type>,
        // method 'onEventReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint8_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<QByteArray, std::false_type>,
        // method 'onConnected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onDisconnected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'refresh'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void PeerListWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<PeerListWidget *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->onPeerListReceived((*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 1: _t->onDhtStatusUpdated((*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<uint64_t>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<uint64_t>>(_a[4]))); break;
        case 2: _t->onEventReceived((*reinterpret_cast< std::add_pointer_t<uint8_t>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[3]))); break;
        case 3: _t->onConnected(); break;
        case 4: _t->onDisconnected(); break;
        case 5: _t->refresh(); break;
        default: ;
        }
    }
}

const QMetaObject *PeerListWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *PeerListWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_PeerListWidget.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int PeerListWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
