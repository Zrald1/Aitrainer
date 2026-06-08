#ifndef AGENTCONTROLLER_H
#define AGENTCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include "learningagent.h"

class QThread;
class QTimer;

class AgentController : public QObject {
    Q_OBJECT

    // Core properties
    Q_PROPERTY(QString databasePath READ databasePath WRITE setDatabasePath NOTIFY databasePathChanged)
    Q_PROPERTY(int vocabularySize READ vocabularySize NOTIFY memoryChanged)
    Q_PROPERTY(int totalAssociations READ totalAssociations NOTIFY memoryChanged)
    Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
    Q_PROPERTY(bool learningEnabled READ learningEnabled WRITE setLearningEnabled NOTIFY learningEnabledChanged)
    Q_PROPERTY(int contextWindow READ contextWindow WRITE setContextWindow NOTIFY contextWindowChanged)

    // Featherless AI integration properties
    Q_PROPERTY(QString featherlessApiKey READ featherlessApiKey WRITE setFeatherlessApiKey NOTIFY featherlessApiKeyChanged)
    Q_PROPERTY(QString huggingFaceToken READ huggingFaceToken WRITE setHuggingFaceToken NOTIFY huggingFaceTokenChanged)
    Q_PROPERTY(QString teacherModel READ teacherModel WRITE setTeacherModel NOTIFY teacherModelChanged)
    Q_PROPERTY(bool isSimulationRunning READ isSimulationRunning NOTIFY simulationStatusChanged)
    Q_PROPERTY(QString simulationTopic READ simulationTopic NOTIFY simulationStatusChanged)
    Q_PROPERTY(int simulationTurns READ simulationTurns NOTIFY simulationStatusChanged)
    Q_PROPERTY(int simulationCurrentTurn READ simulationCurrentTurn NOTIFY simulationStatusChanged)
    Q_PROPERTY(QString simulationLog READ simulationLog NOTIFY simulationLogChanged)
    Q_PROPERTY(int simulationDelay READ simulationDelay WRITE setSimulationDelay NOTIFY simulationDelayChanged)
    Q_PROPERTY(QString gpuHost READ gpuHost WRITE setGpuHost NOTIFY gpuSettingsChanged)
    Q_PROPERTY(int gpuSshPort READ gpuSshPort WRITE setGpuSshPort NOTIFY gpuSettingsChanged)
    Q_PROPERTY(QString gpuUsername READ gpuUsername WRITE setGpuUsername NOTIFY gpuSettingsChanged)
    Q_PROPERTY(QString gpuSshKeyPath READ gpuSshKeyPath WRITE setGpuSshKeyPath NOTIFY gpuSettingsChanged)
    Q_PROPERTY(QString gpuRemoteRoot READ gpuRemoteRoot WRITE setGpuRemoteRoot NOTIFY gpuSettingsChanged)
    Q_PROPERTY(int gpuMaxSamples READ gpuMaxSamples WRITE setGpuMaxSamples NOTIFY gpuSettingsChanged)
    Q_PROPERTY(bool isGpuTrainingRunning READ isGpuTrainingRunning NOTIFY gpuTrainingStatusChanged)
    Q_PROPERTY(QString gpuTrainingStatus READ gpuTrainingStatus NOTIFY gpuTrainingStatusChanged)
    Q_PROPERTY(bool useLocalGpuTraining READ useLocalGpuTraining WRITE setUseLocalGpuTraining NOTIFY localGpuTrainingChanged)
    Q_PROPERTY(bool isLocalGpuTrainingRunning READ isLocalGpuTrainingRunning NOTIFY localGpuTrainingChanged)
    Q_PROPERTY(QString localGpuTrainingStatus READ localGpuTrainingStatus NOTIFY localGpuTrainingChanged)

    // Capability Test and Learning Curve properties
    Q_PROPERTY(QVariantList testScores READ testScores NOTIFY testScoresChanged)
    Q_PROPERTY(QVariantMap lastTestResult READ lastTestResult NOTIFY lastTestResultChanged)
    Q_PROPERTY(bool isTestingPhase READ isTestingPhase NOTIFY simulationStatusChanged)

private:
    LearningAgent m_agent;
    double m_temperature;
    bool m_learningEnabled;
    int m_contextWindow;

    // Simulation settings & state variables
    QString m_apiKey;
    QString m_huggingFaceToken;
    QString m_teacherModel;
    bool m_isSimulationRunning;
    QString m_simulationTopic;
    int m_simulationTurns;
    int m_simulationCurrentTurn;
    QString m_simulationLog;

    // Capability testing variables
    QVariantList m_testScores;
    QVariantMap m_lastTestResult;
    bool m_isTestingPhase;
    QString m_testQuestion;

    // Curriculum state machine
    enum TrainingStage { TeachingStage, TestingStage };
    TrainingStage m_trainingStage;

    struct CurriculumItem {
        QString question;
        QString lesson;
        QString answer;
        bool answeredCorrectly;
    };
    QList<CurriculumItem> m_curriculumQuestions;
    int m_curriculumQuestionIndex;
    bool m_isReTest;

    struct LearnedKnowledge {
        QString question;
        QString lesson;
        QString answer;
        QString correction;
        QString source;
        double strength;
    };
    QList<LearnedKnowledge> m_knowledgeBank;
    QString m_knowledgeFile;
    QString m_loraFile;

    // Networking & history tracking
    QNetworkAccessManager m_networkManager;
    QNetworkReply *m_currentReply;
    QNetworkReply *m_datasetReply;
    QProcess *m_gpuProcess;
    QThread *m_datasetParseThread;
    QTimer *m_datasetParseLogTimer;
    QList<QVariantMap> m_conversationHistory;

    int m_simulationDelay;
    QStringList m_pendingDatasetChunks;
    int m_datasetTrainingNextChunkIndex;
    QString m_datasetTrainingSource;
    QString m_datasetRepoId;
    int m_datasetTrainingEpochs;
    int m_datasetTrainingTotalChunks;
    int m_datasetTrainingOriginalBytes;
    bool m_datasetRequestIsMetadata;
    bool m_datasetRequestIsViewerSplits;
    bool m_datasetRequestIsViewerRows;
    int m_teacherRetryCount;
    QStringList m_rejectedTeacherQuestions;
    QString m_datasetViewerConfig;
    QString m_datasetViewerSplit;
    int m_datasetViewerOffset;
    int m_datasetViewerTotalRows;
    int m_datasetViewerPageSize;
    int m_datasetParseLogTicks;
    QString m_gpuHost;
    int m_gpuSshPort;
    QString m_gpuUsername;
    QString m_gpuSshKeyPath;
    QString m_gpuRemoteRoot;
    int m_gpuMaxSamples;
    bool m_isGpuTrainingRunning;
    QString m_gpuTrainingStatus;
    QString m_gpuLocalRunDir;
    QString m_gpuLocalInputPackage;
    QString m_gpuLocalScriptPath;
    QString m_gpuLocalOutputPackage;
    QString m_gpuRemoteRunDir;
    QString m_gpuDatasetSource;
    int m_gpuTrainingEpochs;
    bool m_useLocalGpuTraining;
    bool m_isLocalGpuTrainingRunning;
    QString m_localGpuTrainingStatus;

    enum class GpuTrainingStage {
        None,
        CreateRemoteDir,
        UploadPackage,
        UploadScript,
        RunRemoteTraining,
        DownloadPackage,
        CleanupRemote
    };
    GpuTrainingStage m_gpuTrainingStage;

    void appendToSimulationLog(const QString &text);
    void triggerNextSimulationTurn();
    void handleTeacherQuestionResponse();
    void requestEvaluationAndCorrection(const QString &teaching, const QString &question, const QString &answer);
    void handleEvaluationResponse();
    void handleDatasetTrainingDownload();
    void requestDatasetViewerSplits(const QString &repoId);
    void requestDatasetViewerRows(int offset);
    void applyHuggingFaceAuth(QNetworkRequest &request) const;
    void connectDatasetDownloadProgress(QNetworkReply *reply, const QString &label);
    void startDatasetFileParsing(const QString &filePath);
    void startDatasetBytesParsing(const QByteArray &datasetBytes, const QString &source, int originalBytes);
    void finishDatasetParsing(QStringList samples, int originalBytes, const QString &source);
    void failDatasetParsing(const QString &message);
    void cleanupDatasetParseThread();
    void setLocalGpuTrainingStatus(const QString &status);
    void saveLocalGpuSettings() const;
    void processNextDatasetTrainingChunk();
    QString gpuRemoteScript() const;
    QString gpuRemoteTarget(const QString &remotePath) const;
    QStringList gpuSshArguments(const QString &remoteCommand) const;
    QStringList gpuScpUploadArguments(const QString &localPath, const QString &remotePath) const;
    QStringList gpuScpDownloadArguments(const QString &remotePath, const QString &localPath) const;
    void startGpuProcess(GpuTrainingStage stage, const QString &program, const QStringList &arguments, const QString &status);
    void handleGpuProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleGpuProcessOutput();
    void failGpuTraining(const QString &message);
    void finishGpuTraining();
    void setGpuTrainingStatus(const QString &status);
    void saveGpuSettings() const;
    QString selectThinkingSeed(const QString &question, const QString &answer) const;
    QStringList recentKnownQuestions(int limit) const;
    bool isQuestionAlreadyLearned(const QString &question, int *score = nullptr) const;
    bool loadKnowledgeBank();
    bool saveKnowledgeBank() const;
    void upsertKnowledge(const QString &question,
                         const QString &lesson,
                         const QString &answer,
                         const QString &correction,
                         const QString &source,
                         double strength);
    int findBestKnowledgeIndex(const QString &query, int *score = nullptr) const;
    QString relatedKnowledgeContext(const QString &query, int limit = 3) const;

public:
    explicit AgentController(QObject *parent = nullptr);
    ~AgentController();

    // Property Accessors
    QString databasePath() const;
    void setDatabasePath(const QString &path);

    int vocabularySize() const;
    int totalAssociations() const;

    double temperature() const;
    void setTemperature(double temp);

    bool learningEnabled() const;
    void setLearningEnabled(bool enabled);

    int contextWindow() const;
    void setContextWindow(int window);

    // Featherless properties
    QString featherlessApiKey() const;
    void setFeatherlessApiKey(const QString &key);

    QString huggingFaceToken() const;
    void setHuggingFaceToken(const QString &token);

    QString teacherModel() const;
    void setTeacherModel(const QString &model);

    bool isSimulationRunning() const;
    QString simulationTopic() const;
    int simulationTurns() const;
    int simulationCurrentTurn() const;
    QString simulationLog() const;
    int simulationDelay() const;
    void setSimulationDelay(int delay);
    QString gpuHost() const;
    void setGpuHost(const QString &host);
    int gpuSshPort() const;
    void setGpuSshPort(int port);
    QString gpuUsername() const;
    void setGpuUsername(const QString &username);
    QString gpuSshKeyPath() const;
    void setGpuSshKeyPath(const QString &path);
    QString gpuRemoteRoot() const;
    void setGpuRemoteRoot(const QString &remoteRoot);
    int gpuMaxSamples() const;
    void setGpuMaxSamples(int maxSamples);
    bool isGpuTrainingRunning() const;
    QString gpuTrainingStatus() const;
    bool useLocalGpuTraining() const;
    void setUseLocalGpuTraining(bool enabled);
    bool isLocalGpuTrainingRunning() const;
    QString localGpuTrainingStatus() const;

    // Capability Testing property accessors
    QVariantList testScores() const;
    QVariantMap lastTestResult() const;
    bool isTestingPhase() const;

    // Q_INVOKABLE Methods for UI Interaction
    Q_INVOKABLE QString learnAndRespond(const QString &input);
    Q_INVOKABLE bool saveMemory();
    Q_INVOKABLE bool loadMemory();
    Q_INVOKABLE void clearMemory();
    Q_INVOKABLE QString trainLoraFromText(const QString &trainingText, int epochs = 4);
    Q_INVOKABLE QString trainLoraFromDatasetUrl(const QString &datasetUrl, int epochs = 4);
    Q_INVOKABLE QString trainCurrentAgentOnGpuServer(const QString &datasetUrl, int epochs = 4);
    Q_INVOKABLE QString exportAgentPackage(const QString &filePath);
    Q_INVOKABLE QString importAgentPackage(const QString &filePath);
    Q_INVOKABLE QString agentFilesSummary() const;
    Q_INVOKABLE QString loraTrainingSummary() const;
    Q_INVOKABLE bool copyTextToClipboard(const QString &text) const;

    // Inspector methods for UI visualisation
    Q_INVOKABLE QVariantList getAssociationsForWord(const QString &word) const;
    Q_INVOKABLE QVariantList getTopAssociations(int limit) const;

    // Simulation controls
    Q_INVOKABLE void startSimulation(const QString &topic, int turns);
    Q_INVOKABLE void stopSimulation();
    Q_INVOKABLE void clearTestScores();

signals:
    void databasePathChanged();
    void memoryChanged();
    void temperatureChanged();
    void learningEnabledChanged();
    void contextWindowChanged();
    void responseGenerated(const QString &prompt, const QString &response);

    // Featherless signals
    void featherlessApiKeyChanged();
    void huggingFaceTokenChanged();
    void teacherModelChanged();
    void simulationStatusChanged();
    void simulationLogChanged();
    void simulationDelayChanged();
    void gpuSettingsChanged();
    void gpuTrainingStatusChanged();
    void localGpuTrainingChanged();

    // Testing signals
    void testScoresChanged();
    void lastTestResultChanged();
    void simulationMessageAdded(const QString &sender, const QString &text);
};

#endif // AGENTCONTROLLER_H
