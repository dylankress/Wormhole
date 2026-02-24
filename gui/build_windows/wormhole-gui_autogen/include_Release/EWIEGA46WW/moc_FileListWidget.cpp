/****************************************************************************
** Meta object code from reading C++ file 'FileListWidget.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../FileListWidget.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'FileListWidget.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN14FileListWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto FileListWidget::qt_create_metaobjectdata<qt_meta_tag_ZN14FileListWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "FileListWidget",
        "onFileListReceived",
        "",
        "data",
        "onEventReceived",
        "uint8_t",
        "type",
        "uint32_t",
        "opId",
        "payload",
        "refresh",
        "onStoreFile",
        "onRetrieveFile",
        "onDeleteFile",
        "onExportKey",
        "onImportKey",
        "onContextMenu",
        "QPoint",
        "pos",
        "onFileStored",
        "ok",
        "error",
        "onFileRetrieved",
        "onFileDeleted",
        "onKeyExported",
        "key",
        "onSearchChanged",
        "text",
        "onFileStoreStarted",
        "filename",
        "onFileRetrieveStarted",
        "updateEmptyState"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onFileListReceived'
        QtMocHelpers::SlotData<void(QByteArray)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QByteArray, 3 },
        }}),
        // Slot 'onEventReceived'
        QtMocHelpers::SlotData<void(uint8_t, uint32_t, QByteArray)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { 0x80000000 | 7, 8 }, { QMetaType::QByteArray, 9 },
        }}),
        // Slot 'refresh'
        QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onStoreFile'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRetrieveFile'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDeleteFile'
        QtMocHelpers::SlotData<void()>(13, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onExportKey'
        QtMocHelpers::SlotData<void()>(14, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onImportKey'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(16, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 17, 18 },
        }}),
        // Slot 'onFileStored'
        QtMocHelpers::SlotData<void(bool, QString)>(19, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 20 }, { QMetaType::QString, 21 },
        }}),
        // Slot 'onFileRetrieved'
        QtMocHelpers::SlotData<void(bool, QString)>(22, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 20 }, { QMetaType::QString, 21 },
        }}),
        // Slot 'onFileDeleted'
        QtMocHelpers::SlotData<void(bool, QString)>(23, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 20 }, { QMetaType::QString, 21 },
        }}),
        // Slot 'onKeyExported'
        QtMocHelpers::SlotData<void(bool, QByteArray, QString)>(24, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 20 }, { QMetaType::QByteArray, 25 }, { QMetaType::QString, 21 },
        }}),
        // Slot 'onSearchChanged'
        QtMocHelpers::SlotData<void(const QString &)>(26, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 27 },
        }}),
        // Slot 'onFileStoreStarted'
        QtMocHelpers::SlotData<void(const QString &)>(28, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 29 },
        }}),
        // Slot 'onFileRetrieveStarted'
        QtMocHelpers::SlotData<void(const QString &)>(30, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 29 },
        }}),
        // Slot 'updateEmptyState'
        QtMocHelpers::SlotData<void()>(31, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<FileListWidget, qt_meta_tag_ZN14FileListWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject FileListWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14FileListWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14FileListWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14FileListWidgetE_t>.metaTypes,
    nullptr
} };

void FileListWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<FileListWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onFileListReceived((*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 1: _t->onEventReceived((*reinterpret_cast<std::add_pointer_t<uint8_t>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<uint32_t>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[3]))); break;
        case 2: _t->refresh(); break;
        case 3: _t->onStoreFile(); break;
        case 4: _t->onRetrieveFile(); break;
        case 5: _t->onDeleteFile(); break;
        case 6: _t->onExportKey(); break;
        case 7: _t->onImportKey(); break;
        case 8: _t->onContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 9: _t->onFileStored((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 10: _t->onFileRetrieved((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 11: _t->onFileDeleted((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 12: _t->onKeyExported((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 13: _t->onSearchChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 14: _t->onFileStoreStarted((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 15: _t->onFileRetrieveStarted((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 16: _t->updateEmptyState(); break;
        default: ;
        }
    }
}

const QMetaObject *FileListWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *FileListWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14FileListWidgetE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int FileListWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 17)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 17;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 17)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 17;
    }
    return _id;
}
QT_WARNING_POP
