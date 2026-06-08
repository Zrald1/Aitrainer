/****************************************************************************
** Meta object code from reading C++ file 'agentcontroller.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../agentcontroller.h"
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'agentcontroller.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.1. It"
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
struct qt_meta_tag_ZN15AgentControllerE_t {};
} // unnamed namespace

template <> constexpr inline auto AgentController::qt_create_metaobjectdata<qt_meta_tag_ZN15AgentControllerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AgentController",
        "databasePathChanged",
        "",
        "memoryChanged",
        "temperatureChanged",
        "learningEnabledChanged",
        "contextWindowChanged",
        "responseGenerated",
        "prompt",
        "response",
        "featherlessApiKeyChanged",
        "teacherModelChanged",
        "simulationStatusChanged",
        "simulationLogChanged",
        "simulationDelayChanged",
        "testScoresChanged",
        "lastTestResultChanged",
        "simulationMessageAdded",
        "sender",
        "text",
        "learnAndRespond",
        "input",
        "saveMemory",
        "loadMemory",
        "clearMemory",
        "trainLoraFromText",
        "trainingText",
        "epochs",
        "trainLoraFromDatasetUrl",
        "datasetUrl",
        "exportAgentPackage",
        "filePath",
        "importAgentPackage",
        "agentFilesSummary",
        "loraTrainingSummary",
        "getAssociationsForWord",
        "QVariantList",
        "word",
        "getTopAssociations",
        "limit",
        "startSimulation",
        "topic",
        "turns",
        "stopSimulation",
        "clearTestScores",
        "databasePath",
        "vocabularySize",
        "totalAssociations",
        "temperature",
        "learningEnabled",
        "contextWindow",
        "featherlessApiKey",
        "teacherModel",
        "isSimulationRunning",
        "simulationTopic",
        "simulationTurns",
        "simulationCurrentTurn",
        "simulationLog",
        "simulationDelay",
        "testScores",
        "lastTestResult",
        "QVariantMap",
        "isTestingPhase"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'databasePathChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'memoryChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'temperatureChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'learningEnabledChanged'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'contextWindowChanged'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'responseGenerated'
        QtMocHelpers::SignalData<void(const QString &, const QString &)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 8 }, { QMetaType::QString, 9 },
        }}),
        // Signal 'featherlessApiKeyChanged'
        QtMocHelpers::SignalData<void()>(10, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'teacherModelChanged'
        QtMocHelpers::SignalData<void()>(11, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'simulationStatusChanged'
        QtMocHelpers::SignalData<void()>(12, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'simulationLogChanged'
        QtMocHelpers::SignalData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'simulationDelayChanged'
        QtMocHelpers::SignalData<void()>(14, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'testScoresChanged'
        QtMocHelpers::SignalData<void()>(15, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'lastTestResultChanged'
        QtMocHelpers::SignalData<void()>(16, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'simulationMessageAdded'
        QtMocHelpers::SignalData<void(const QString &, const QString &)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 18 }, { QMetaType::QString, 19 },
        }}),
        // Method 'learnAndRespond'
        QtMocHelpers::MethodData<QString(const QString &)>(20, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 21 },
        }}),
        // Method 'saveMemory'
        QtMocHelpers::MethodData<bool()>(22, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'loadMemory'
        QtMocHelpers::MethodData<bool()>(23, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'clearMemory'
        QtMocHelpers::MethodData<void()>(24, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'trainLoraFromText'
        QtMocHelpers::MethodData<QString(const QString &, int)>(25, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 26 }, { QMetaType::Int, 27 },
        }}),
        // Method 'trainLoraFromText'
        QtMocHelpers::MethodData<QString(const QString &)>(25, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 26 },
        }}),
        // Method 'trainLoraFromDatasetUrl'
        QtMocHelpers::MethodData<QString(const QString &, int)>(28, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 29 }, { QMetaType::Int, 27 },
        }}),
        // Method 'trainLoraFromDatasetUrl'
        QtMocHelpers::MethodData<QString(const QString &)>(28, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 29 },
        }}),
        // Method 'exportAgentPackage'
        QtMocHelpers::MethodData<QString(const QString &)>(30, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'importAgentPackage'
        QtMocHelpers::MethodData<QString(const QString &)>(32, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 31 },
        }}),
        // Method 'agentFilesSummary'
        QtMocHelpers::MethodData<QString() const>(33, 2, QMC::AccessPublic, QMetaType::QString),
        // Method 'loraTrainingSummary'
        QtMocHelpers::MethodData<QString() const>(34, 2, QMC::AccessPublic, QMetaType::QString),
        // Method 'getAssociationsForWord'
        QtMocHelpers::MethodData<QVariantList(const QString &) const>(35, 2, QMC::AccessPublic, 0x80000000 | 36, {{
            { QMetaType::QString, 37 },
        }}),
        // Method 'getTopAssociations'
        QtMocHelpers::MethodData<QVariantList(int) const>(38, 2, QMC::AccessPublic, 0x80000000 | 36, {{
            { QMetaType::Int, 39 },
        }}),
        // Method 'startSimulation'
        QtMocHelpers::MethodData<void(const QString &, int)>(40, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 41 }, { QMetaType::Int, 42 },
        }}),
        // Method 'stopSimulation'
        QtMocHelpers::MethodData<void()>(43, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'clearTestScores'
        QtMocHelpers::MethodData<void()>(44, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'databasePath'
        QtMocHelpers::PropertyData<QString>(45, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'vocabularySize'
        QtMocHelpers::PropertyData<int>(46, QMetaType::Int, QMC::DefaultPropertyFlags, 1),
        // property 'totalAssociations'
        QtMocHelpers::PropertyData<int>(47, QMetaType::Int, QMC::DefaultPropertyFlags, 1),
        // property 'temperature'
        QtMocHelpers::PropertyData<double>(48, QMetaType::Double, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 2),
        // property 'learningEnabled'
        QtMocHelpers::PropertyData<bool>(49, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 3),
        // property 'contextWindow'
        QtMocHelpers::PropertyData<int>(50, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 4),
        // property 'featherlessApiKey'
        QtMocHelpers::PropertyData<QString>(51, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 6),
        // property 'teacherModel'
        QtMocHelpers::PropertyData<QString>(52, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 7),
        // property 'isSimulationRunning'
        QtMocHelpers::PropertyData<bool>(53, QMetaType::Bool, QMC::DefaultPropertyFlags, 8),
        // property 'simulationTopic'
        QtMocHelpers::PropertyData<QString>(54, QMetaType::QString, QMC::DefaultPropertyFlags, 8),
        // property 'simulationTurns'
        QtMocHelpers::PropertyData<int>(55, QMetaType::Int, QMC::DefaultPropertyFlags, 8),
        // property 'simulationCurrentTurn'
        QtMocHelpers::PropertyData<int>(56, QMetaType::Int, QMC::DefaultPropertyFlags, 8),
        // property 'simulationLog'
        QtMocHelpers::PropertyData<QString>(57, QMetaType::QString, QMC::DefaultPropertyFlags, 9),
        // property 'simulationDelay'
        QtMocHelpers::PropertyData<int>(58, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 10),
        // property 'testScores'
        QtMocHelpers::PropertyData<QVariantList>(59, 0x80000000 | 36, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 11),
        // property 'lastTestResult'
        QtMocHelpers::PropertyData<QVariantMap>(60, 0x80000000 | 61, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 12),
        // property 'isTestingPhase'
        QtMocHelpers::PropertyData<bool>(62, QMetaType::Bool, QMC::DefaultPropertyFlags, 8),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AgentController, qt_meta_tag_ZN15AgentControllerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AgentController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AgentControllerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AgentControllerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN15AgentControllerE_t>.metaTypes,
    nullptr
} };

void AgentController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AgentController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->databasePathChanged(); break;
        case 1: _t->memoryChanged(); break;
        case 2: _t->temperatureChanged(); break;
        case 3: _t->learningEnabledChanged(); break;
        case 4: _t->contextWindowChanged(); break;
        case 5: _t->responseGenerated((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 6: _t->featherlessApiKeyChanged(); break;
        case 7: _t->teacherModelChanged(); break;
        case 8: _t->simulationStatusChanged(); break;
        case 9: _t->simulationLogChanged(); break;
        case 10: _t->simulationDelayChanged(); break;
        case 11: _t->testScoresChanged(); break;
        case 12: _t->lastTestResultChanged(); break;
        case 13: _t->simulationMessageAdded((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 14: { QString _r = _t->learnAndRespond((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 15: { bool _r = _t->saveMemory();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 16: { bool _r = _t->loadMemory();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 17: _t->clearMemory(); break;
        case 18: { QString _r = _t->trainLoraFromText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 19: { QString _r = _t->trainLoraFromText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 20: { QString _r = _t->trainLoraFromDatasetUrl((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 21: { QString _r = _t->trainLoraFromDatasetUrl((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 22: { QString _r = _t->exportAgentPackage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 23: { QString _r = _t->importAgentPackage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 24: { QString _r = _t->agentFilesSummary();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 25: { QString _r = _t->loraTrainingSummary();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 26: { QVariantList _r = _t->getAssociationsForWord((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 27: { QVariantList _r = _t->getTopAssociations((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 28: _t->startSimulation((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 29: _t->stopSimulation(); break;
        case 30: _t->clearTestScores(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::databasePathChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::memoryChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::temperatureChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::learningEnabledChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::contextWindowChanged, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)(const QString & , const QString & )>(_a, &AgentController::responseGenerated, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::featherlessApiKeyChanged, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::teacherModelChanged, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::simulationStatusChanged, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::simulationLogChanged, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::simulationDelayChanged, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::testScoresChanged, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::lastTestResultChanged, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)(const QString & , const QString & )>(_a, &AgentController::simulationMessageAdded, 13))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QString*>(_v) = _t->databasePath(); break;
        case 1: *reinterpret_cast<int*>(_v) = _t->vocabularySize(); break;
        case 2: *reinterpret_cast<int*>(_v) = _t->totalAssociations(); break;
        case 3: *reinterpret_cast<double*>(_v) = _t->temperature(); break;
        case 4: *reinterpret_cast<bool*>(_v) = _t->learningEnabled(); break;
        case 5: *reinterpret_cast<int*>(_v) = _t->contextWindow(); break;
        case 6: *reinterpret_cast<QString*>(_v) = _t->featherlessApiKey(); break;
        case 7: *reinterpret_cast<QString*>(_v) = _t->teacherModel(); break;
        case 8: *reinterpret_cast<bool*>(_v) = _t->isSimulationRunning(); break;
        case 9: *reinterpret_cast<QString*>(_v) = _t->simulationTopic(); break;
        case 10: *reinterpret_cast<int*>(_v) = _t->simulationTurns(); break;
        case 11: *reinterpret_cast<int*>(_v) = _t->simulationCurrentTurn(); break;
        case 12: *reinterpret_cast<QString*>(_v) = _t->simulationLog(); break;
        case 13: *reinterpret_cast<int*>(_v) = _t->simulationDelay(); break;
        case 14: *reinterpret_cast<QVariantList*>(_v) = _t->testScores(); break;
        case 15: *reinterpret_cast<QVariantMap*>(_v) = _t->lastTestResult(); break;
        case 16: *reinterpret_cast<bool*>(_v) = _t->isTestingPhase(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setDatabasePath(*reinterpret_cast<QString*>(_v)); break;
        case 3: _t->setTemperature(*reinterpret_cast<double*>(_v)); break;
        case 4: _t->setLearningEnabled(*reinterpret_cast<bool*>(_v)); break;
        case 5: _t->setContextWindow(*reinterpret_cast<int*>(_v)); break;
        case 6: _t->setFeatherlessApiKey(*reinterpret_cast<QString*>(_v)); break;
        case 7: _t->setTeacherModel(*reinterpret_cast<QString*>(_v)); break;
        case 13: _t->setSimulationDelay(*reinterpret_cast<int*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *AgentController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AgentController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AgentControllerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AgentController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 31)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 31;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 31)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 31;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 17;
    }
    return _id;
}

// SIGNAL 0
void AgentController::databasePathChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void AgentController::memoryChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void AgentController::temperatureChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void AgentController::learningEnabledChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void AgentController::contextWindowChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void AgentController::responseGenerated(const QString & _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2);
}

// SIGNAL 6
void AgentController::featherlessApiKeyChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void AgentController::teacherModelChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void AgentController::simulationStatusChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}

// SIGNAL 9
void AgentController::simulationLogChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}

// SIGNAL 10
void AgentController::simulationDelayChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 10, nullptr);
}

// SIGNAL 11
void AgentController::testScoresChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 11, nullptr);
}

// SIGNAL 12
void AgentController::lastTestResultChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 12, nullptr);
}

// SIGNAL 13
void AgentController::simulationMessageAdded(const QString & _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 13, nullptr, _t1, _t2);
}
QT_WARNING_POP
