#ifndef LIGHTWEIGHTTENSORREASONER_H
#define LIGHTWEIGHTTENSORREASONER_H

#include <QString>
#include <QStringList>
#include <array>

class LightweightTensorReasoner
{
public:
    struct Result {
        QString intent;
        QString operation;
        QString answerShape;
        QStringList focusTerms;
        float confidence = 0.0f;
        bool questionLike = false;
        bool generative = false;
        bool conversational = false;
    };

    Result analyze(const QString &input) const;
    QString visiblePlan(const Result &result, const QString &input) const;

private:
    static constexpr int FeatureCount = 36;
    static constexpr int IntentCount = 10;

    using FeatureVector = std::array<float, FeatureCount>;
    using ScoreVector = std::array<float, IntentCount>;

    FeatureVector encode(const QString &input) const;
    ScoreVector score(const FeatureVector &features) const;
    QStringList focusTerms(const QString &input, int limit = 5) const;
};

#endif
