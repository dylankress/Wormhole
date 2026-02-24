/****************************************************************************
** Meta object code from reading C++ file 'TransferWidget.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../TransferWidget.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'TransferWidget.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_TransferWidget_t {
    uint offsetsAndSizes[34];
    char stringdata0[15];
    char stringdata1[18];
    char stringdata2[1];
    char stringdata3[9];
    char stringdata4[11];
    char stringdata5[16];
    char stringdata6[8];
    char stringdata7[5];
    char stringdata8[5];
    char stringdata9[8];
    char stringdata10[17];
    char stringdata11[11];
    char stringdata12[16];
    char stringdata13[10];
    char stringdata14[17];
    char stringdata15[13];
    char stringdata16[17];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_TransferWidget_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_TransferWidget_t qt_meta_stringdata_TransferWidget = {
    {
        QT_MOC_LITERAL(0, 14),  // "TransferWidget"
        QT_MOC_LITERAL(15, 17),  // "onTransferStarted"
        QT_MOC_LITERAL(33, 0),  // ""
        QT_MOC_LITERAL(34, 8),  // "uint32_t"
        QT_MOC_LITERAL(43, 10),  // "transferId"
        QT_MOC_LITERAL(54, 15),  // "onEventReceived"
        QT_MOC_LITERAL(70, 7),  // "uint8_t"
        QT_MOC_LITERAL(78, 4),  // "type"
        QT_MOC_LITERAL(83, 4),  // "opId"
        QT_MOC_LITERAL(88, 7),  // "payload"
        QT_MOC_LITERAL(96, 16),  // "refreshTransfers"
        QT_MOC_LITERAL(113, 10),  // "onSendFile"
        QT_MOC_LITERAL(124, 15),  // "onSendDirectory"
        QT_MOC_LITERAL(140, 9),  // "onReceive"
        QT_MOC_LITERAL(150, 16),  // "onCancelTransfer"
        QT_MOC_LITERAL(167, 12),  // "onCopyTicket"
        QT_MOC_LITERAL(180, 16)   // "onClearCompleted"
    },
    "TransferWidget",
    "onTransferStarted",
    "",
    "uint32_t",
    "transferId",
    "onEventReceived",
    "uint8_t",
    "type",
    "opId",
    "payload",
    "refreshTransfers",
    "onSendFile",
    "onSendDirectory",
    "onReceive",
    "onCancelTransfer",
    "onCopyTicket",
    "onClearCompleted"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_TransferWidget[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       9,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   68,    2, 0x0a,    1 /* Public */,
       5,    3,   71,    2, 0x0a,    3 /* Public */,
      10,    0,   78,    2, 0x0a,    7 /* Public */,
      11,    0,   79,    2, 0x08,    8 /* Private */,
      12,    0,   80,    2, 0x08,    9 /* Private */,
      13,    0,   81,    2, 0x08,   10 /* Private */,
      14,    0,   82,    2, 0x08,   11 /* Private */,
      15,    0,   83,    2, 0x08,   12 /* Private */,
      16,    0,   84,    2, 0x08,   13 /* Private */,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 6, 0x80000000 | 3, QMetaType::QByteArray,    7,    8,    9,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject TransferWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_TransferWidget.offsetsAndSizes,
    qt_meta_data_TransferWidget,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_TransferWidget_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<TransferWidget, std::true_type>,
        // method 'onTransferStarted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        // method 'onEventReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint8_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<QByteArray, std::false_type>,
        // method 'refreshTransfers'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onSendFile'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onSendDirectory'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onReceive'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onCancelTransfer'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onCopyTicket'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onClearCompleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void TransferWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<TransferWidget *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->onTransferStarted((*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[1]))); break;
        case 1: _t->onEventReceived((*reinterpret_cast< std::add_pointer_t<uint8_t>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[3]))); break;
        case 2: _t->refreshTransfers(); break;
        case 3: _t->onSendFile(); break;
        case 4: _t->onSendDirectory(); break;
        case 5: _t->onReceive(); break;
        case 6: _t->onCancelTransfer(); break;
        case 7: _t->onCopyTicket(); break;
        case 8: _t->onClearCompleted(); break;
        default: ;
        }
    }
}

const QMetaObject *TransferWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *TransferWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_TransferWidget.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int TransferWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 9;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
