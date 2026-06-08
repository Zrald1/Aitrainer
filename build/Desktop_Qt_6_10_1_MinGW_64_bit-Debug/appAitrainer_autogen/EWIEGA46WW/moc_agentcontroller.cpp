/****************************************************************************
** Meta object code from reading C++ file 'agentcontroller.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../agentcontroller.h"
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
        "huggingFaceTokenChanged",
        "teacherModelChanged",
        "simulationStatusChanged",
        "simulationLogChanged",
        "simulationDelayChanged",
        "gpuSettingsChanged",
        "gpuTrainingStatusChanged",
        "localGpuTrainingChanged",
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
        "trainCurrentAgentOnGpuServer",
        "exportAgentPackage",
        "filePath",
        "importAgentPackage",
        "agentFilesSummary",
        "loraTrainingSummary",
        "copyTextToClipboard",
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
        "huggingFaceToken",
        "teacherModel",
        "isSimulationRunning",
        "simulationTopic",
        "simulationTurns",
        "simulationCurrentTurn",
        "simulationLog",
        "simulationDelay",
        "gpuHost",
        "gpuSshPort",
        "gpuUsername",
        "gpuSshKeyPath",
        "gpuRemoteRoot",
        "gpuMaxSamples",
        "isGpuTrainingRunning",
        "gpuTrainingStatus",
        "useLocalGpuTraining",
        "isLocalGpuTrainingRunning",
        "localGpuTrainingStatus",
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
        // Signal 'huggingFaceTokenChanged'
        QtMocHelpers::SignalData<void()>(11, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'teacherModelChanged'
        QtMocHelpers::SignalData<void()>(12, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'simulationStatusChanged'
        QtMocHelpers::SignalData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'simulationLogChanged'
        QtMocHelpers::SignalData<void()>(14, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'simulationDelayChanged'
        QtMocHelpers::SignalData<void()>(15, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'gpuSettingsChanged'
        QtMocHelpers::SignalData<void()>(16, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'gpuTrainingStatusChanged'
        QtMocHelpers::SignalData<void()>(17, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'localGpuTrainingChanged'
        QtMocHelpers::SignalData<void()>(18, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'testScoresChanged'
        QtMocHelpers::SignalData<void()>(19, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'lastTestResultChanged'
        QtMocHelpers::SignalData<void()>(20, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'simulationMessageAdded'
        QtMocHelpers::SignalData<void(const QString &, const QString &)>(21, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 22 }, { QMetaType::QString, 23 },
        }}),
        // Method 'learnAndRespond'
        QtMocHelpers::MethodData<QString(const QString &)>(24, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 25 },
        }}),
        // Method 'saveMemory'
        QtMocHelpers::MethodData<bool()>(26, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'loadMemory'
        QtMocHelpers::MethodData<bool()>(27, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'clearMemory'
        QtMocHelpers::MethodData<void()>(28, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'trainLoraFromText'
        QtMocHelpers::MethodData<QString(const QString &, int)>(29, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 30 }, { QMetaType::Int, 31 },
        }}),
        // Method 'trainLoraFromText'
        QtMocHelpers::MethodData<QString(const QString &)>(29, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 30 },
        }}),
        // Method 'trainLoraFromDatasetUrl'
        QtMocHelpers::MethodData<QString(const QString &, int)>(32, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 33 }, { QMetaType::Int, 31 },
        }}),
        // Method 'trainLoraFromDatasetUrl'
        QtMocHelpers::MethodData<QString(const QString &)>(32, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 33 },
        }}),
        // Method 'trainCurrentAgentOnGpuServer'
        QtMocHelpers::MethodData<QString(const QString &, int)>(34, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 33 }, { QMetaType::Int, 31 },
        }}),
        // Method 'trainCurrentAgentOnGpuServer'
        QtMocHelpers::MethodData<QString(const QString &)>(34, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::QString, {{
            { QMetaType::QString, 33 },
        }}),
        // Method 'exportAgentPackage'
        QtMocHelpers::MethodData<QString(const QString &)>(35, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 36 },
        }}),
        // Method 'importAgentPackage'
        QtMocHelpers::MethodData<QString(const QString &)>(37, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 36 },
        }}),
        // Method 'agentFilesSummary'
        QtMocHelpers::MethodData<QString() const>(38, 2, QMC::AccessPublic, QMetaType::QString),
        // Method 'loraTrainingSummary'
        QtMocHelpers::MethodData<QString() const>(39, 2, QMC::AccessPublic, QMetaType::QString),
        // Method 'copyTextToClipboard'
        QtMocHelpers::MethodData<bool(const QString &) const>(40, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 23 },
        }}),
        // Method 'getAssociationsForWord'
        QtMocHelpers::MethodData<QVariantList(const QString &) const>(41, 2, QMC::AccessPublic, 0x80000000 | 42, {{
            { QMetaType::QString, 43 },
        }}),
        // Method 'getTopAssociations'
        QtMocHelpers::MethodData<QVariantList(int) const>(44, 2, QMC::AccessPublic, 0x80000000 | 42, {{
            { QMetaType::Int, 45 },
        }}),
        // Method 'startSimulation'
        QtMocHelpers::MethodData<void(const QString &, int)>(46, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 47 }, { QMetaType::Int, 48 },
        }}),
        // Method 'stopSimulation'
        QtMocHelpers::MethodData<void()>(49, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'clearTestScores'
        QtMocHelpers::MethodData<void()>(50, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'databasePath'
        QtMocHelpers::PropertyData<QString>(51, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'vocabularySize'
        QtMocHelpers::PropertyData<int>(52, QMetaType::Int, QMC::DefaultPropertyFlags, 1),
        // property 'totalAssociations'
        QtMocHelpers::PropertyData<int>(53, QMetaType::Int, QMC::DefaultPropertyFlags, 1),
        // property 'temperature'
        QtMocHelpers::PropertyData<double>(54, QMetaType::Double, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 2),
        // property 'learningEnabled'
        QtMocHelpers::PropertyData<bool>(55, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 3),
        // property 'contextWindow'
        QtMocHelpers::PropertyData<int>(56, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 4),
        // property 'featherlessApiKey'
        QtMocHelpers::PropertyData<QString>(57, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 6),
        // property 'huggingFaceToken'
        QtMocHelpers::PropertyData<QString>(58, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 7),
        // property 'teacherModel'
        QtMocHelpers::PropertyData<QString>(59, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 8),
        // property 'isSimulationRunning'
        QtMocHelpers::PropertyData<bool>(60, QMetaType::Bool, QMC::DefaultPropertyFlags, 9),
        // property 'simulationTopic'
        QtMocHelpers::PropertyData<QString>(61, QMetaType::QString, QMC::DefaultPropertyFlags, 9),
        // property 'simulationTurns'
        QtMocHelpers::PropertyData<int>(62, QMetaType::Int, QMC::DefaultPropertyFlags, 9),
        // property 'simulationCurrentTurn'
        QtMocHelpers::PropertyData<int>(63, QMetaType::Int, QMC::DefaultPropertyFlags, 9),
        // property 'simulationLog'
        QtMocHelpers::PropertyData<QString>(64, QMetaType::QString, QMC::DefaultPropertyFlags, 10),
        // property 'simulationDelay'
        QtMocHelpers::PropertyData<int>(65, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 11),
        // property 'gpuHost'
        QtMocHelpers::PropertyData<QString>(66, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 12),
        // property 'gpuSshPort'
        QtMocHelpers::PropertyData<int>(67, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 12),
        // property 'gpuUsername'
        QtMocHelpers::PropertyData<QString>(68, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 12),
        // property 'gpuSshKeyPath'
        QtMocHelpers::PropertyData<QString>(69, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 12),
        // property 'gpuRemoteRoot'
        QtMocHelpers::PropertyData<QString>(70, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 12),
        // property 'gpuMaxSamples'
        QtMocHelpers::PropertyData<int>(71, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 12),
        // property 'isGpuTrainingRunning'
        QtMocHelpers::PropertyData<bool>(72, QMetaType::Bool, QMC::DefaultPropertyFlags, 13),
        // property 'gpuTrainingStatus'
        QtMocHelpers::PropertyData<QString>(73, QMetaType::QString, QMC::DefaultPropertyFlags, 13),
        // property 'useLocalGpuTraining'
        QtMocHelpers::PropertyData<bool>(74, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 14),
        // property 'isLocalGpuTrainingRunning'
        QtMocHelpers::PropertyData<bool>(75, QMetaType::Bool, QMC::DefaultPropertyFlags, 14),
        // property 'localGpuTrainingStatus'
        QtMocHelpers::PropertyData<QString>(76, QMetaType::QString, QMC::DefaultPropertyFlags, 14),
        // property 'testScores'
        QtMocHelpers::PropertyData<QVariantList>(77, 0x80000000 | 42, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 15),
        // property 'lastTestResult'
        QtMocHelpers::PropertyData<QVariantMap>(78, 0x80000000 | 79, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 16),
        // property 'isTestingPhase'
        QtMocHelpers::PropertyData<bool>(80, QMetaType::Bool, QMC::DefaultPropertyFlags, 9),
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
        case 7: _t->huggingFaceTokenChanged(); break;
        case 8: _t->teacherModelChanged(); break;
        case 9: _t->simulationStatusChanged(); break;
        case 10: _t->simulationLogChanged(); break;
        case 11: _t->simulationDelayChanged(); break;
        case 12: _t->gpuSettingsChanged(); break;
        case 13: _t->gpuTrainingStatusChanged(); break;
        case 14: _t->localGpuTrainingChanged(); break;
        case 15: _t->testScoresChanged(); break;
        case 16: _t->lastTestResultChanged(); break;
        case 17: _t->simulationMessageAdded((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 18: { QString _r = _t->learnAndRespond((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 19: { bool _r = _t->saveMemory();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 20: { bool _r = _t->loadMemory();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 21: _t->clearMemory(); break;
        case 22: { QString _r = _t->trainLoraFromText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 23: { QString _r = _t->trainLoraFromText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 24: { QString _r = _t->trainLoraFromDatasetUrl((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 25: { QString _r = _t->trainLoraFromDatasetUrl((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 26: { QString _r = _t->trainCurrentAgentOnGpuServer((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 27: { QString _r = _t->trainCurrentAgentOnGpuServer((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 28: { QString _r = _t->exportAgentPackage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 29: { QString _r = _t->importAgentPackage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 30: { QString _r = _t->agentFilesSummary();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 31: { QString _r = _t->loraTrainingSummary();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 32: { bool _r = _t->copyTextToClipboard((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 33: { QVariantList _r = _t->getAssociationsForWord((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 34: { QVariantList _r = _t->getTopAssociations((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 35: _t->startSimulation((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 36: _t->stopSimulation(); break;
        case 37: _t->clearTestScores(); break;
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
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::huggingFaceTokenChanged, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::teacherModelChanged, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::simulationStatusChanged, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::simulationLogChanged, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::simulationDelayChanged, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::gpuSettingsChanged, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::gpuTrainingStatusChanged, 13))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::localGpuTrainingChanged, 14))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::testScoresChanged, 15))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)()>(_a, &AgentController::lastTestResultChanged, 16))
            return;
        if (QtMocHelpers::indexOfMethod<void (AgentController::*)(const QString & , const QString & )>(_a, &AgentController::simulationMessageAdded, 17))
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
        case 7: *reinterpret_cast<QString*>(_v) = _t->huggingFaceToken(); break;
        case 8: *reinterpret_cast<QString*>(_v) = _t->teacherModel(); break;
        case 9: *reinterpret_cast<bool*>(_v) = _t->isSimulationRunning(); break;
        case 10: *reinterpret_cast<QString*>(_v) = _t->simulationTopic(); break;
        case 11: *reinterpret_cast<int*>(_v) = _t->simulationTurns(); break;
        case 12: *reinterpret_cast<int*>(_v) = _t->simulationCurrentTurn(); break;
        case 13: *reinterpret_cast<QString*>(_v) = _t->simulationLog(); break;
        case 14: *reinterpret_cast<int*>(_v) = _t->simulationDelay(); break;
        case 15: *reinterpret_cast<QString*>(_v) = _t->gpuHost(); break;
        case 16: *reinterpret_cast<int*>(_v) = _t->gpuSshPort(); break;
        case 17: *reinterpret_cast<QString*>(_v) = _t->gpuUsername(); break;
        case 18: *reinterpret_cast<QString*>(_v) = _t->gpuSshKeyPath(); break;
        case 19: *reinterpret_cast<QString*>(_v) = _t->gpuRemoteRoot(); break;
        case 20: *reinterpret_cast<int*>(_v) = _t->gpuMaxSamples(); break;
        case 21: *reinterpret_cast<bool*>(_v) = _t->isGpuTrainingRunning(); break;
        case 22: *reinterpret_cast<QString*>(_v) = _t->gpuTrainingStatus(); break;
        case 23: *reinterpret_cast<bool*>(_v) = _t->useLocalGpuTraining(); break;
        case 24: *reinterpret_cast<bool*>(_v) = _t->isLocalGpuTrainingRunning(); break;
        case 25: *reinterpret_cast<QString*>(_v) = _t->localGpuTrainingStatus(); break;
        case 26: *reinterpret_cast<QVariantList*>(_v) = _t->testScores(); break;
        case 27: *reinterpret_cast<QVariantMap*>(_v) = _t->lastTestResult(); break;
        case 28: *reinterpret_cast<bool*>(_v) = _t->isTestingPhase(); break;
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
        case 7: _t->setHuggingFaceToken(*reinterpret_cast<QString*>(_v)); break;
        case 8: _t->setTeacherModel(*reinterpret_cast<QString*>(_v)); break;
        case 14: _t->setSimulationDelay(*reinterpret_cast<int*>(_v)); break;
        case 15: _t->setGpuHost(*reinterpret_cast<QString*>(_v)); break;
        case 16: _t->setGpuSshPort(*reinterpret_cast<int*>(_v)); break;
        case 17: _t->setGpuUsername(*reinterpret_cast<QString*>(_v)); break;
        case 18: _t->setGpuSshKeyPath(*reinterpret_cast<QString*>(_v)); break;
        case 19: _t->setGpuRemoteRoot(*reinterpret_cast<QString*>(_v)); break;
        case 20: _t->setGpuMaxSamples(*reinterpret_cast<int*>(_v)); break;
        case 23: _t->setUseLocalGpuTraining(*reinterpret_cast<bool*>(_v)); break;
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
        if (_id < 38)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 38;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 38)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 38;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 29;
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
void AgentController::huggingFaceTokenChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void AgentController::teacherModelChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}

// SIGNAL 9
void AgentController::simulationStatusChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}

// SIGNAL 10
void AgentController::simulationLogChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 10, nullptr);
}

// SIGNAL 11
void AgentController::simulationDelayChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 11, nullptr);
}

// SIGNAL 12
void AgentController::gpuSettingsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 12, nullptr);
}

// SIGNAL 13
void AgentController::gpuTrainingStatusChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 13, nullptr);
}

// SIGNAL 14
void AgentController::localGpuTrainingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 14, nullptr);
}

// SIGNAL 15
void AgentController::testScoresChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 15, nullptr);
}

// SIGNAL 16
void AgentController::lastTestResultChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 16, nullptr);
}

// SIGNAL 17
void AgentController::simulationMessageAdded(const QString & _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 17, nullptr, _t1, _t2);
}
QT_WARNING_POP
