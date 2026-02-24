/****************************************************************************
** Meta object code from reading C++ file 'TrayManager.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../TrayManager.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'TrayManager.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_TrayManager_t {
    uint offsetsAndSizes[26];
    char stringdata0[12];
    char stringdata1[18];
    char stringdata2[1];
    char stringdata3[21];
    char stringdata4[16];
    char stringdata5[8];
    char stringdata6[5];
    char stringdata7[9];
    char stringdata8[5];
    char stringdata9[8];
    char stringdata10[16];
    char stringdata11[34];
    char stringdata12[7];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_TrayManager_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_TrayManager_t qt_meta_stringdata_TrayManager = {
    {
        QT_MOC_LITERAL(0, 11),  // "TrayManager"
        QT_MOC_LITERAL(12, 17),  // "sendFileRequested"
        QT_MOC_LITERAL(30, 0),  // ""
        QT_MOC_LITERAL(31, 20),  // "receiveFileRequested"
        QT_MOC_LITERAL(52, 15),  // "onEventReceived"
        QT_MOC_LITERAL(68, 7),  // "uint8_t"
        QT_MOC_LITERAL(76, 4),  // "type"
        QT_MOC_LITERAL(81, 8),  // "uint32_t"
        QT_MOC_LITERAL(90, 4),  // "opId"
        QT_MOC_LITERAL(95, 7),  // "payload"
        QT_MOC_LITERAL(103, 15),  // "onTrayActivated"
        QT_MOC_LITERAL(119, 33),  // "QSystemTrayIcon::ActivationRe..."
        QT_MOC_LITERAL(153, 6)   // "reason"
    },
    "TrayManager",
    "sendFileRequested",
    "",
    "receiveFileRequested",
    "onEventReceived",
    "uint8_t",
    "type",
    "uint32_t",
    "opId",
    "payload",
    "onTrayActivated",
    "QSystemTrayIcon::ActivationReason",
    "reason"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_TrayManager[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       4,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   38,    2, 0x06,    1 /* Public */,
       3,    0,   39,    2, 0x06,    2 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       4,    3,   40,    2, 0x0a,    3 /* Public */,
      10,    1,   47,    2, 0x08,    7 /* Private */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 5, 0x80000000 | 7, QMetaType::QByteArray,    6,    8,    9,
    QMetaType::Void, 0x80000000 | 11,   12,

       0        // eod
};

Q_CONSTINIT const QMetaObject TrayManager::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_TrayManager.offsetsAndSizes,
    qt_meta_data_TrayManager,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_TrayManager_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<TrayManager, std::true_type>,
        // method 'sendFileRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'receiveFileRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onEventReceived'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint8_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<QByteArray, std::false_type>,
        // method 'onTrayActivated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QSystemTrayIcon::ActivationReason, std::false_type>
    >,
    nullptr
} };

void TrayManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<TrayManager *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->sendFileRequested(); break;
        case 1: _t->receiveFileRequested(); break;
        case 2: _t->onEventReceived((*reinterpret_cast< std::add_pointer_t<uint8_t>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[3]))); break;
        case 3: _t->onTrayActivated((*reinterpret_cast< std::add_pointer_t<QSystemTrayIcon::ActivationReason>>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (TrayManager::*)();
            if (_t _q_method = &TrayManager::sendFileRequested; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (TrayManager::*)();
            if (_t _q_method = &TrayManager::receiveFileRequested; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
    }
}

const QMetaObject *TrayManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *TrayManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_TrayManager.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int TrayManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 4)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 4)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 4;
    }
    return _id;
}

// SIGNAL 0
void TrayManager::sendFileRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void TrayManager::receiveFileRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
