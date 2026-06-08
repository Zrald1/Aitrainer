#ifndef MOBILEAGENTCONTROLLER_H
#define MOBILEAGENTCONTROLLER_H

#include "learningagent.h"

#include <QObject>
#include <QString>
#include <QVector>

class MobileAgentController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool modelLoaded READ modelLoaded NOTIFY modelLoadedChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)

public:
    explicit MobileAgentController(QObject *parent = nullptr);

    bool modelLoaded() const;
    QString statusText() const;
    QString storagePath() const;

    Q_INVOKABLE QString importAgentPackage(const QString &filePathOrUrl);
    Q_INVOKABLE QString sendMessage(const QString &input);
    Q_INVOKABLE QString modelSummary() const;

signals:
    void modelLoadedChanged();
    void statusTextChanged();

private:
    struct LearnedKnowledge {
        QString question;
        QString lesson;
        QString answer;
        QString correction;
        QString source;
        double strength = 1.0;
    };

    LearningAgent m_agent;
    QVector<LearnedKnowledge> m_knowledgeBank;
    QString m_storagePath;
    QString m_statusText;
    bool m_modelLoaded = false;

    QString filePathFor(const QString &fileName) const;
    void configureAgentFiles();
    bool loadLocalModel();
    bool loadKnowledgeBank();
    void setStatusText(const QString &text);
    void setModelLoaded(bool loaded);
    QString answerFromKnowledge(const QString &question) const;
};

#endif // MOBILEAGENTCONTROLLER_H
