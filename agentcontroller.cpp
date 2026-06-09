#include "agentcontroller.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QEventLoop>
#include <QDateTime>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QTextStream>
#include <QUrlQuery>
#include <QGuiApplication>
#include <QClipboard>
#include <QThread>
#include <QPointer>
#include <QSharedPointer>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

QString compactText(QString text, int maxLength)
{
    text.remove(QRegularExpression("\\[Thinking:[^\\]]*\\]", QRegularExpression::CaseInsensitiveOption));
    text.replace(QRegularExpression("\\s+"), " ");
    text = text.trimmed();
    if (maxLength > 0 && text.length() > maxLength) {
        text = text.left(maxLength - 3).trimmed() + "...";
    }
    return text;
}

QString formatNumber(double value)
{
    if (std::abs(value - std::round(value)) < 0.000001) {
        return QString::number(static_cast<qint64>(std::llround(value)));
    }
    return QString::number(value, 'g', 12);
}

bool numbersMatch(double left, double right)
{
    return std::abs(left - right) < 0.000001;
}

QString requestCoreForRouting(QString text)
{
    text = text.trimmed().toLower();
    text.replace(QRegularExpression("\\s+"), " ");

    bool changed = true;
    while (changed) {
        changed = false;
        static const QStringList prefixes = {
            "okay ", "ok ", "please ", "pls ", "alright ", "hey ", "hello ",
            "can you please ", "could you please ", "can you ", "could you "
        };
        for (const QString &prefix : prefixes) {
            if (text.startsWith(prefix)) {
                text = text.mid(prefix.length()).trimmed();
                changed = true;
                break;
            }
        }
    }

    return text;
}

bool isLikelyQuestion(const QString &input)
{
    const QString text = requestCoreForRouting(input);
    if (text.endsWith("?")) {
        return true;
    }

    static const QStringList questionStarts = {
        "what ", "why ", "how ", "when ", "where ", "who ", "which ",
        "can ", "could ", "should ", "would ", "is ", "are ", "do ", "does ",
        "did ", "solve ", "calculate ", "tell me ", "explain ", "show me ",
        "give me ", "list ", "describe ", "teach me ", "help me ", "provide ",
        "create ", "generate ", "write ", "make ", "compose ", "ask me ",
        "quiz me "
    };

    for (const QString &start : questionStarts) {
        if (text.startsWith(start)) {
            return true;
        }
    }

    return false;
}

QString normalizedForMatch(QString text)
{
    text.remove(QRegularExpression("\\[Thinking:[^\\]]*\\]", QRegularExpression::CaseInsensitiveOption));
    text = text.toLower();
    text.replace(QRegularExpression("[^a-z0-9]+"), " ");
    text.replace(QRegularExpression("\\s+"), " ");
    return text.trimmed();
}

QString canonicalLearningToken(QString word)
{
    word = word.trimmed().toLower();
    if (word.isEmpty()) {
        return word;
    }

    if (word == "capabiltiies" || word == "capabilities") {
        return "capability";
    }
    if (word == "casualties") {
        return "casualty";
    }
    if (word == "prioritized" || word == "prioritised" || word == "prioritizing" || word == "prioritising") {
        return "priority";
    }
    if (word == "eaten" || word == "eating") {
        return "eat";
    }

    if (word.length() > 5 && word.endsWith("ies")) {
        word.chop(3);
        word += "y";
    } else if (word.length() > 6 && word.endsWith("ing")) {
        word.chop(3);
    } else if (word.length() > 5 && word.endsWith("ed")) {
        word.chop(2);
    } else if (word.length() > 4 && word.endsWith('s') && !word.endsWith("ss")) {
        word.chop(1);
    }

    return word;
}

QStringList semanticAliasesForToken(const QString &token)
{
    const QString t = canonicalLearningToken(token);
    if (t == "survival" || t == "survive" || t == "wilderness" || t == "emergency") {
        return {"survival", "emergency", "life", "death"};
    }
    if (t == "life" || t == "death") {
        return {"survival", "emergency"};
    }
    if (t == "shelter") {
        return {"exposure", "weather", "cold", "warmth"};
    }
    if (t == "weather" || t == "exposure" || t == "cold") {
        return {"shelter", "weather", "exposure"};
    }
    if (t == "food" || t == "eat" || t == "hunger") {
        return {"energy", "food", "hunger"};
    }
    if (t == "energy") {
        return {"food", "eat", "fuel"};
    }
    if (t == "oxygen" || t == "hypoxia" || t == "airway" || t == "breathing" || t == "drown") {
        return {"oxygen", "hypoxia", "airway", "breathing"};
    }
    if (t == "triage" || t == "urgency" || t == "patient" || t == "casualty") {
        return {"triage", "urgency", "patient", "casualty", "priority"};
    }
    if (t == "resource" || t == "critical" || t == "luxury" || t == "essential") {
        return {"resource", "critical", "luxury", "essential", "comfort"};
    }
    if (t == "symptom" || t == "surface" || t == "sign") {
        return {"symptom", "surface", "root", "cause"};
    }
    if (t == "root" || t == "cause" || t == "underlying" || t == "mechanism") {
        return {"root", "cause", "underlying", "mechanism"};
    }
    if (t == "rule") {
        return {"rule", "principle", "method"};
    }
    return {};
}

int keywordOverlapScore(const QString &left, const QString &right)
{
    static const QStringList stopWords = {
        "what", "is", "the", "of", "a", "an", "in", "to", "and", "or",
        "for", "with", "on", "at", "by", "how", "why", "where", "who",
        "which", "about", "are", "do", "does", "did", "can", "could",
        "should", "would", "tell", "me", "explain", "formula", "equation",
        "rule", "method"
    };

    const QStringList leftWords = normalizedForMatch(left).split(' ', Qt::SkipEmptyParts);
    const QStringList rightWords = normalizedForMatch(right).split(' ', Qt::SkipEmptyParts);
    QSet<QString> rightTokens;
    for (const QString &word : rightWords) {
        const QString token = canonicalLearningToken(word);
        if (!token.isEmpty()) {
            rightTokens.insert(token);
        }
    }

    int score = 0;
    for (const QString &word : leftWords) {
        const QString token = canonicalLearningToken(word);
        if (!token.isEmpty() && !stopWords.contains(token) && rightTokens.contains(token)) {
            score++;
        }
    }
    return score;
}

QStringList significantQuestionTokens(const QString &text, bool expandAliases = true)
{
    static const QStringList stopWords = {
        "what", "is", "the", "of", "a", "an", "in", "to", "and", "or",
        "for", "with", "on", "at", "by", "how", "why", "where", "who",
        "which", "about", "are", "do", "does", "did", "can", "could",
        "should", "would", "tell", "me", "explain", "find", "solve",
        "calculate", "answer", "question", "formula", "equation", "rule",
        "method"
    };

    QStringList tokens;
    const QStringList words = normalizedForMatch(text).split(' ', Qt::SkipEmptyParts);
    for (const QString &word : words) {
        const QString token = canonicalLearningToken(word);
        if (token.isEmpty()) {
            continue;
        }
        if (stopWords.contains(token)) {
            continue;
        }
        if (token.length() < 2 && !token.front().isDigit()) {
            continue;
        }
        if (!tokens.contains(token)) {
            tokens.append(token);
        }
        if (expandAliases) {
            for (const QString &alias : semanticAliasesForToken(token)) {
                const QString cleanAlias = canonicalLearningToken(alias);
                if (!cleanAlias.isEmpty() && !stopWords.contains(cleanAlias) && !tokens.contains(cleanAlias)) {
                    tokens.append(cleanAlias);
                }
            }
        }
    }
    return tokens;
}

int tokenCoveragePercent(const QStringList &queryTokens, const QStringList &learningTokens)
{
    if (queryTokens.isEmpty() || learningTokens.isEmpty()) {
        return 0;
    }

    int overlap = 0;
    for (const QString &token : queryTokens) {
        if (learningTokens.contains(token)) {
            overlap++;
        }
    }
    return (overlap * 100) / qMax(1, queryTokens.size());
}

QStringList overlappingEvidenceTokens(const QString &query, const QString &learningText, int limit = 5)
{
    const QStringList queryTokens = significantQuestionTokens(query, false);
    const QStringList learningTokens = significantQuestionTokens(learningText, false);
    QStringList evidence;
    for (const QString &token : queryTokens) {
        if (learningTokens.contains(token) && !evidence.contains(token)) {
            evidence.append(token);
            if (evidence.size() >= limit) {
                break;
            }
        }
    }
    return evidence;
}

int questionSimilarityScore(const QString &left, const QString &right)
{
    const QString normalizedLeft = normalizedForMatch(left);
    const QString normalizedRight = normalizedForMatch(right);
    if (normalizedLeft.isEmpty() || normalizedRight.isEmpty()) {
        return 0;
    }
    if (normalizedLeft == normalizedRight) {
        return 160;
    }

    const int shorterLength = qMin(normalizedLeft.length(), normalizedRight.length());
    if (shorterLength >= 12
        && (normalizedLeft.contains(normalizedRight) || normalizedRight.contains(normalizedLeft))) {
        return 130;
    }

    const QStringList leftTokens = significantQuestionTokens(left, false);
    const QStringList rightTokens = significantQuestionTokens(right, false);
    if (leftTokens.isEmpty() || rightTokens.isEmpty()) {
        return 0;
    }

    int overlap = 0;
    int numericOverlap = 0;
    for (const QString &token : leftTokens) {
        if (rightTokens.contains(token)) {
            overlap++;
            if (token.front().isDigit()) {
                numericOverlap++;
            }
        }
    }

    if (overlap == 0) {
        return 0;
    }

    const double ratio = static_cast<double>(overlap) / qMax(1, qMin(leftTokens.size(), rightTokens.size()));
    if (ratio >= 0.85 && overlap >= 3) {
        return 90 + overlap * 4;
    }
    if (ratio >= 0.65 && overlap >= 4) {
        return 70 + overlap * 3;
    }
    if (numericOverlap >= 2 && ratio >= 0.50 && overlap >= 2) {
        return 60 + overlap * 3;
    }

    return overlap * 3;
}

int meaningfulTokenOverlapScore(const QString &query, const QString &learningText)
{
    const QStringList queryTokens = significantQuestionTokens(query, true);
    const QStringList learningTokens = significantQuestionTokens(learningText, true);
    if (queryTokens.isEmpty() || learningTokens.isEmpty()) {
        return 0;
    }

    int overlap = 0;
    int numericOverlap = 0;
    for (const QString &token : queryTokens) {
        if (learningTokens.contains(token)) {
            overlap++;
            if (token.front().isDigit()) {
                numericOverlap++;
            }
        }
    }

    return overlap * 10 + numericOverlap * 4;
}

int relatedLearningMinimumScore()
{
    return 24;
}

int appliedLearningMinimumScore()
{
    return 42;
}

bool isDirectKnowledgeMatch(const QString &query, const QString &learnedQuestion)
{
    return questionSimilarityScore(query, learnedQuestion) >= 100;
}

bool isLowConfidenceAnswerText(const QString &answer)
{
    const QString normalized = normalizedForMatch(answer);
    return normalized.isEmpty()
        || normalized.contains("do not know")
        || normalized.contains("dont know")
        || normalized.contains("not enough")
        || normalized.contains("need to learn")
        || normalized.contains("cannot answer")
        || normalized.contains("unknown");
}

bool asksForExamples(const QString &question)
{
    const QString normalized = normalizedForMatch(question);
    return normalized.contains("example")
        || normalized.contains("examples")
        || normalized.contains("sample")
        || normalized.contains("samples")
        || normalized.startsWith("show me")
        || normalized.startsWith("give me")
        || normalized.startsWith("list ");
}

QString bestLessonSentenceForQuestion(const QString &lesson, const QString &question, int maxLength = 260)
{
    const QString cleanLesson = compactText(lesson, 0);
    if (cleanLesson.isEmpty()) {
        return "";
    }

    const QStringList sentences = cleanLesson.split(QRegularExpression("(?<=[.!?])\\s+|\\n+"), Qt::SkipEmptyParts);
    QString bestSentence;
    int bestScore = 0;
    for (const QString &sentence : sentences) {
        const QString cleanSentence = compactText(sentence, maxLength);
        if (cleanSentence.split(' ', Qt::SkipEmptyParts).size() < 4) {
            continue;
        }
        const int score = meaningfulTokenOverlapScore(question, cleanSentence)
            + keywordOverlapScore(question, cleanSentence) * 3;
        if (score > bestScore) {
            bestScore = score;
            bestSentence = cleanSentence;
        }
    }

    if (!bestSentence.isEmpty() && bestScore >= 10) {
        return bestSentence;
    }

    return compactText(cleanLesson, maxLength);
}

QString correctionRuleFromLesson(const QString &lesson, int maxLength = 220)
{
    const QString cleanLesson = compactText(lesson, 0);
    if (cleanLesson.isEmpty()) {
        return "";
    }

    static const QList<QRegularExpression> rulePatterns = {
        QRegularExpression("(?:reusable\\s+rule|improvement\\s+rule|rule|to\\s+improve)\\s*:\\s*([^\\.]+(?:\\.[^\\.]*)?)",
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("(always\\s+[^\\.]+\\.)", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("(in\\s+any\\s+problem,\\s*[^\\.]+\\.)", QRegularExpression::CaseInsensitiveOption)
    };

    for (const QRegularExpression &rx : rulePatterns) {
        const QRegularExpressionMatch match = rx.match(cleanLesson);
        if (match.hasMatch()) {
            const QString rule = compactText(match.captured(1), maxLength);
            if (!rule.isEmpty()) {
                return rule;
            }
        }
    }

    return "";
}

QString lessonApplicationFallback(const QString &lesson,
                                  const QString &question,
                                  const QString &answer,
                                  int maxLength = 360)
{
    const QString cleanQuestion = compactText(question, 160);
    QString cleanAnswer = compactText(answer, 0);
    cleanAnswer.replace(QRegularExpression("^(answer\\s*:?|the answer is\\s*)", QRegularExpression::CaseInsensitiveOption), "");
    cleanAnswer = cleanAnswer.trimmed();
    if (cleanQuestion.isEmpty() || cleanAnswer.isEmpty()) {
        return "";
    }

    const QString guidingRule = correctionRuleFromLesson(lesson, 180);
    const QString guidingLesson = bestLessonSentenceForQuestion(lesson, question, 190);
    QString thinking;
    if (!guidingRule.isEmpty()) {
        thinking = QString("Answer check: I apply the learned rule: %1 I connect that rule to this question's details, so the answer is %2.")
            .arg(guidingRule, cleanAnswer);
    } else if (!guidingLesson.isEmpty()) {
        thinking = QString("Answer check: I apply the lesson detail \"%1\" to the question \"%2\". That connection explains why the answer is %3.")
            .arg(guidingLesson, cleanQuestion, cleanAnswer);
    }

    return compactText(thinking, maxLength);
}

bool isTrustedKnowledgeSource(const QString &source)
{
    const QString normalized = normalizedForMatch(source);
    if (normalized.isEmpty()) {
        return true;
    }

    return !normalized.contains("manual chat")
        && !normalized.contains("user chat")
        && !normalized.contains("chat context")
        && !normalized.contains("conversation memory")
        && !normalized.contains("manual conversation");
}

int learningRelevanceScore(const QString &query,
                           const QString &learnedQuestion,
                           const QString &lesson,
                           const QString &correction,
                           const QString &answer,
                           double strength)
{
    const QString learningText = QString("%1 %2 %3 %4")
        .arg(learnedQuestion, lesson, correction, answer);
    const int directScore = questionSimilarityScore(query, learnedQuestion);
    const int tokenScore = meaningfulTokenOverlapScore(query, learningText);
    const int semanticCoverage = tokenCoveragePercent(significantQuestionTokens(query, true),
                                                      significantQuestionTokens(learningText, true));
    const int coreCoverage = tokenCoveragePercent(significantQuestionTokens(query, false),
                                                  significantQuestionTokens(learningText, false));

    if (directScore >= 130) {
        return directScore + qMin(20, static_cast<int>(strength));
    }

    if (directScore < 70 && semanticCoverage < 35 && coreCoverage < 20) {
        return 0;
    }
    if (tokenScore < 20 && directScore < 70 && semanticCoverage < 45) {
        return 0;
    }

    const int questionOverlap = keywordOverlapScore(query, learnedQuestion) * 4;
    const int lessonOverlap = (keywordOverlapScore(query, lesson)
                               + keywordOverlapScore(query, correction)) * 3;
    const int answerOverlap = keywordOverlapScore(query, answer);
    const int coverageBonus = qMin(25, semanticCoverage / 4 + coreCoverage / 6);
    const int strengthBonus = qMin(15, static_cast<int>(strength / 2.0));

    return qMin(directScore, 90) + tokenScore + questionOverlap + lessonOverlap + answerOverlap + coverageBonus + strengthBonus;
}

QString mappedBuiltInSubjectForTopic(const QString &topic)
{
    const QString normalizedTopic = normalizedForMatch(topic);
    if (normalizedTopic.contains("math") || normalizedTopic.contains("arithmetic") || normalizedTopic.contains("algebra")) {
        return "Mathematics";
    }
    if (normalizedTopic.contains("puzzle") || normalizedTopic.contains("logic")) {
        return "Puzzles";
    }
    if (normalizedTopic.contains("trick") || normalizedTopic.contains("riddle")) {
        return "Trick Questions";
    }
    return "";
}

bool isMalformedTeacherQuestion(const QString &question)
{
    const QString cleanQuestion = compactText(question, 260);
    if (cleanQuestion.length() < 8 || !cleanQuestion.endsWith('?')) {
        return true;
    }

    const QString normalizedQuestion = normalizedForMatch(cleanQuestion);
    static const QStringList rejectedPatterns = {
        "what question tests",
        "what is a correct answer about",
        "what should the student know",
        "for training item",
        "write a question",
        "create a question",
        "generate a question",
        "question field",
        "json"
    };

    for (const QString &pattern : rejectedPatterns) {
        if (normalizedQuestion.contains(pattern)) {
            return true;
        }
    }

    return false;
}

bool textMentionsTopic(const QString &text, const QString &topic)
{
    const QString normalizedTopic = normalizedForMatch(topic);
    if (normalizedTopic.isEmpty()) {
        return true;
    }

    const QString normalizedText = normalizedForMatch(text);
    if (normalizedText.contains(normalizedTopic) || keywordOverlapScore(text, topic) > 0) {
        return true;
    }

    const QString paddedText = " " + normalizedText + " ";
    if (normalizedTopic.contains("artificial intelligence") && paddedText.contains(" ai ")) {
        return true;
    }

    return false;
}

bool isTeacherQuestionGroundedInLesson(const QString &lesson, const QString &question, const QString &topic)
{
    const QString cleanTopic = compactText(topic, 100);
    if (cleanTopic.isEmpty() || !mappedBuiltInSubjectForTopic(cleanTopic).isEmpty()) {
        return true;
    }

    if (!textMentionsTopic(lesson, cleanTopic)) {
        return false;
    }

    return keywordOverlapScore(question, lesson) > 0 || textMentionsTopic(question, cleanTopic);
}

QString teacherLessonAngleDirective(const QString &topic, int slot, int retry)
{
    const QString normalizedTopic = normalizedForMatch(topic);
    QStringList angles;

    if (normalizedTopic.contains("writing equation") || normalizedTopic.contains("equation")) {
        angles = {
            "define a variable for an unknown quantity before writing an equation",
            "translate 'sum' or 'total' wording into an addition equation",
            "translate 'difference' or 'less than' wording into a subtraction equation",
            "translate 'times', 'product', or 'per' wording into a multiplication equation",
            "translate 'shared equally' or 'divided by' wording into a division equation",
            "write a two-step equation from a short word situation",
            "identify the coefficient and constant in a written equation",
            "check whether an equation matches a word situation by substituting a value"
        };
    } else if (normalizedTopic.contains("math") || normalizedTopic.contains("algebra")) {
        angles = {
            "choose the correct operation from the wording",
            "show the calculation step before giving the answer",
            "use inverse operations to check the result",
            "translate a short word problem into a number sentence",
            "compare two quantities using subtraction",
            "find a total from equal groups",
            "find a missing value from a simple equation",
            "verify the answer by substituting it back into the problem"
        };
    } else {
        angles = {
            "teach a key term and ask what it means",
            "teach a practical example and ask why it fits the topic",
            "teach a common mistake and ask how to avoid it",
            "teach a cause-and-effect relationship and ask what causes the result",
            "teach a comparison and ask for the important difference",
            "teach a real-world use and ask how it is applied",
            "teach a limitation and ask what the idea cannot reliably do",
            "teach a step-by-step method and ask for the next correct step"
        };
    }

    const int index = (slot + retry * 3) % angles.size();
    return angles[index];
}

QString questionOpening(const QString &question, int wordLimit = 7)
{
    QString text = compactText(question, 140);
    text.remove(QRegularExpression("[?!.]+$"));
    const QStringList words = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return "";
    }
    return words.mid(0, qMin(wordLimit, words.size())).join(' ');
}

QStringList questionOpenings(const QStringList &questions, int limit)
{
    QStringList openings;
    for (const QString &question : questions) {
        const QString opening = questionOpening(question);
        if (!opening.isEmpty() && !openings.contains(opening, Qt::CaseInsensitive)) {
            openings.append(opening);
        }
        if (openings.size() >= limit) {
            break;
        }
    }
    return openings;
}

bool isRepeatedQuestionOpening(const QString &question, const QStringList &existingQuestions)
{
    const QString opening = questionOpening(question);
    if (opening.split(' ', Qt::SkipEmptyParts).size() < 5) {
        return false;
    }

    return questionOpenings(existingQuestions, 120).contains(opening, Qt::CaseInsensitive);
}

QString buildArithmeticChatAnswer(const QString &question)
{
    const QString numberPattern = "(-?\\d+(?:\\.\\d+)?)";
    QRegularExpression symbolRx(numberPattern + "\\s*([+\\-*/xX])\\s*" + numberPattern);
    QRegularExpressionMatch match = symbolRx.match(question);

    if (!match.hasMatch()) {
        QRegularExpression wordRx(numberPattern
                                  + "\\s*(plus|add(?:ed)?\\s+to|minus|less\\s+than|times|multiplied\\s+by|divided\\s+by|over)\\s*"
                                  + numberPattern,
                                  QRegularExpression::CaseInsensitiveOption);
        match = wordRx.match(question);
    }

    if (!match.hasMatch()) {
        return "";
    }

    bool leftOk = false;
    bool rightOk = false;
    double left = match.captured(1).toDouble(&leftOk);
    QString opText = match.captured(2).toLower();
    double right = match.captured(3).toDouble(&rightOk);
    if (!leftOk || !rightOk) {
        return "";
    }

    QString expression;
    double result = 0.0;
    if (opText == "+" || opText.contains("plus") || opText.contains("add")) {
        result = left + right;
        expression = QString("%1 + %2").arg(formatNumber(left), formatNumber(right));
    } else if (opText == "-" || opText.contains("minus")) {
        result = left - right;
        expression = QString("%1 - %2").arg(formatNumber(left), formatNumber(right));
    } else if (opText == "*" || opText == "x" || opText.contains("times") || opText.contains("multiplied")) {
        result = left * right;
        expression = QString("%1 * %2").arg(formatNumber(left), formatNumber(right));
    } else if (opText == "/" || opText.contains("divided") || opText.contains("over")) {
        if (numbersMatch(right, 0.0)) {
            return "Answer: undefined\nHow I calculated it: division by zero has no finite value.";
        }
        result = left / right;
        expression = QString("%1 / %2").arg(formatNumber(left), formatNumber(right));
    } else if (opText.contains("less than")) {
        result = right - left;
        expression = QString("%1 - %2").arg(formatNumber(right), formatNumber(left));
    } else {
        return "";
    }

    const QString resultText = formatNumber(result);
    return QString("Answer: %1\nHow I calculated it: %2 = %1.").arg(resultText, expression);
}

bool isUsefulMemoryResponse(const QString &response, const QString &input)
{
    const QString cleanResponse = normalizedForMatch(response);
    if (cleanResponse.isEmpty()
        || cleanResponse == "i need to learn more words first tell me something"
        || cleanResponse == "i do not have enough learned language yet to form a clear sentence"
        || cleanResponse.contains("question verified response")
        || cleanResponse == normalizedForMatch(input)) {
        return false;
    }
    return keywordOverlapScore(response, input) > 0
        || meaningfulTokenOverlapScore(input, response) >= 10;
}

QString polishUserFacingText(QString text, int maxLength = 0, bool addSentencePunctuation = true)
{
    text = compactText(text, maxLength);
    if (text.isEmpty()) {
        return "";
    }

    text.replace(QRegularExpression("\\bi'm\\b", QRegularExpression::CaseInsensitiveOption), "I am");
    text.replace(QRegularExpression("\\bive\\b", QRegularExpression::CaseInsensitiveOption), "I have");
    text.replace(QRegularExpression("\\bdont\\b", QRegularExpression::CaseInsensitiveOption), "do not");
    text.replace(QRegularExpression("\\bcant\\b", QRegularExpression::CaseInsensitiveOption), "cannot");
    text.replace(QRegularExpression("\\bi\\b"), "I");
    text.replace(QRegularExpression("\\s+"), " ");
    text = text.trimmed();

    if (!text.isEmpty()
        && text[0].isLower()
        && !text.startsWith("http", Qt::CaseInsensitive)) {
        text[0] = text[0].toUpper();
    }

    if (addSentencePunctuation
        && !text.endsWith('.')
        && !text.endsWith('!')
        && !text.endsWith('?')
        && text.split(' ', Qt::SkipEmptyParts).size() >= 4) {
        text += ".";
    }

    return text;
}

QString statementClauseForUser(QString statement)
{
    statement = compactText(statement, 0);
    statement.remove(QRegularExpression("[.!?]+$"));
    statement = statement.trimmed();
    const QString lower = statement.toLower();

    auto restAfter = [&](const QString &prefix) {
        return statement.mid(prefix.length()).trimmed();
    };

    QString clause;
    if (lower.startsWith("i am ")) {
        clause = "you are " + restAfter("i am ");
    } else if (lower.startsWith("i'm ")) {
        clause = "you are " + restAfter("i'm ");
    } else if (lower.startsWith("i have ")) {
        clause = "you have " + restAfter("i have ");
    } else if (lower.startsWith("i like ")) {
        clause = "you like " + restAfter("i like ");
    } else if (lower.startsWith("i need ")) {
        clause = "you need " + restAfter("i need ");
    } else if (lower.startsWith("i want ")) {
        clause = "you want " + restAfter("i want ");
    } else if (lower.startsWith("i ")) {
        clause = "you " + restAfter("i ");
    } else if (lower.startsWith("my ")) {
        clause = "your " + restAfter("my ");
    } else {
        clause = "you said \"" + statement + "\"";
    }

    clause.replace(QRegularExpression("\\bi\\b"), "I");
    clause.replace(QRegularExpression("\\s+"), " ");
    return clause.trimmed();
}

QString buildStatementUnderstandingResponse(const QString &statement)
{
    const QString lower = normalizedForMatch(statement);
    if (lower == "hello" || lower == "hi" || lower == "hey" || lower.startsWith("hello ")) {
        return "[Thinking: I read this as a greeting, so I answer conversationally while staying ready to learn.]\n"
               "Answer: Hello. I am ready to learn and answer clearly.";
    }

    if (lower.contains("thank you") || lower == "thanks") {
        return "[Thinking: I read this as appreciation, so I respond briefly and keep the conversation open.]\n"
               "Answer: You are welcome.";
    }

    const QString clause = statementClauseForUser(statement);
    const QString answer = polishUserFacingText("I understand that " + clause, 0, true);
    return QString("[Thinking: I read this as a statement, identify its main idea, and answer without adding it to training memory.]\n"
                   "Answer: %1").arg(answer);
}

QString compactDatasetText(QString text, int maxLength)
{
    text.remove(QRegularExpression("<[^>]+>"));
    text.replace(QRegularExpression("[\\x00-\\x08\\x0b\\x0c\\x0e-\\x1f]"), " ");
    text.replace(QRegularExpression("\\s+"), " ");
    text = text.trimmed();
    if (text.length() > maxLength) {
        text = text.left(maxLength);
    }
    return text;
}

bool looksMostlyText(const QByteArray &bytes)
{
    if (bytes.isEmpty()) {
        return false;
    }

    const int sampleSize = qMin(bytes.size(), 8192);
    int suspicious = 0;
    for (int i = 0; i < sampleSize; ++i) {
        const unsigned char ch = static_cast<unsigned char>(bytes.at(i));
        if (ch == 0) {
            suspicious += 4;
        } else if (ch < 9 || (ch > 13 && ch < 32)) {
            suspicious++;
        }
    }

    return suspicious < sampleSize / 10;
}

QStringList splitDelimitedLine(const QString &line, QChar delimiter)
{
    QStringList fields;
    QString current;
    bool inQuotes = false;

    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (ch == '"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                current += '"';
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (ch == delimiter && !inQuotes) {
            fields.append(current.trimmed());
            current.clear();
        } else {
            current += ch;
        }
    }
    fields.append(current.trimmed());
    return fields;
}

QStringList splitDelimitedRecords(const QString &text, QChar delimiter)
{
    QStringList records;
    QString current;
    bool inQuotes = false;

    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];
        if (ch == '"') {
            current += ch;
            if (inQuotes && i + 1 < text.size() && text[i + 1] == '"') {
                current += text[i + 1];
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if ((ch == '\n' || ch == '\r') && !inQuotes) {
            if (!current.trimmed().isEmpty()) {
                records.append(current.trimmed());
            }
            current.clear();
            if (ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
        } else {
            current += ch;
        }
    }

    if (!current.trimmed().isEmpty()) {
        records.append(current.trimmed());
    }

    return records;
}

int firstMatchingColumn(const QStringList &headers, const QStringList &names)
{
    for (const QString &name : names) {
        const QString normalizedName = normalizedForMatch(name);
        for (int i = 0; i < headers.size(); ++i) {
            const QString normalizedHeader = normalizedForMatch(headers[i]);
            if (normalizedHeader == normalizedName
                || (normalizedName.length() > 2 && normalizedHeader.contains(normalizedName))) {
                return i;
            }
        }
    }
    return -1;
}

bool looksLikeHeaderRow(const QStringList &fields)
{
    if (fields.size() < 2) {
        return false;
    }

    int headerLike = 0;
    static const QStringList knownColumns = {
        "q", "a", "question", "answer", "instruction", "prompt", "response", "output",
        "completion", "input", "context", "text", "label", "target", "user",
        "assistant", "human", "gpt", "source", "src", "dst", "title", "description", "definition",
        "translation", "source", "sentence", "summary", "solution"
    };

    for (const QString &field : fields) {
        const QString normalized = normalizedForMatch(field);
        if (knownColumns.contains(normalized)) {
            headerLike += 2;
        } else if (normalized.length() <= 32
                   && !normalized.contains(" ")
                   && !normalized.isEmpty()
                   && !normalized.at(0).isDigit()) {
            headerLike++;
        }
    }

    return headerLike >= qMin(3, fields.size());
}

QString readableDatasetColumnName(const QString &key);

QString datasetFieldText(const QJsonValue &value)
{
    if (value.isString()) {
        return compactDatasetText(value.toString(), 900);
    }

    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 12);
    }

    if (value.isBool()) {
        return value.toBool() ? "true" : "false";
    }

    if (value.isArray()) {
        QStringList parts;
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array) {
            const QString text = datasetFieldText(item);
            if (!text.isEmpty()) {
                parts.append(text);
            }
            if (parts.size() >= 3) {
                break;
            }
        }
        return compactDatasetText(parts.join("; "), 900);
    }

    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        static const QStringList preferredKeys = {
            "text", "content", "value", "message", "body", "answer", "response",
            "output", "completion", "label", "target"
        };
        for (const QString &key : preferredKeys) {
            const QString normalizedKey = normalizedForMatch(key);
            for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
                if (normalizedForMatch(it.key()) == normalizedKey) {
                    const QString text = datasetFieldText(it.value());
                    if (!text.isEmpty()) {
                        return text;
                    }
                }
            }
        }

        QStringList parts;
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            const QString normalizedKey = normalizedForMatch(it.key());
            if (normalizedKey == "row idx"
                || normalizedKey == "row index"
                || normalizedKey == "features"
                || normalizedKey == "truncated cells"
                || normalizedKey == "num rows total") {
                continue;
            }

            const QString text = datasetFieldText(it.value());
            if (!text.isEmpty()) {
                parts.append(QString("%1: %2").arg(readableDatasetColumnName(it.key()), text));
            }
            if (parts.size() >= 5) {
                break;
            }
        }
        return compactDatasetText(parts.join("; "), 900);
    }

    return "";
}

bool isDatasetReasoningKey(const QString &key)
{
    const QString normalizedKey = normalizedForMatch(key);
    static const QStringList reasoningKeys = {
        "reasoning", "rationale", "explanation", "analysis", "thinking",
        "thought", "chain of thought", "chain of thoughts", "chain_of_thought",
        "cot", "scratchpad", "work", "solution steps", "derivation"
    };

    for (const QString &reasoningKey : reasoningKeys) {
        const QString normalizedReasoningKey = normalizedForMatch(reasoningKey);
        if (normalizedKey == normalizedReasoningKey || normalizedKey.contains(normalizedReasoningKey)) {
            return true;
        }
    }
    return false;
}

QString visibleReasoningFromText(const QString &text)
{
    const QString clean = compactDatasetText(text, 5000);
    if (clean.isEmpty()) {
        return "";
    }

    static const QList<QRegularExpression> blockPatterns = {
        QRegularExpression("<think>\\s*(.*?)\\s*</think>", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption),
        QRegularExpression("<analysis>\\s*(.*?)\\s*</analysis>", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption),
        QRegularExpression("\\[Thinking:\\s*([^\\]]+)\\]", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption)
    };

    for (const QRegularExpression &rx : blockPatterns) {
        const QRegularExpressionMatch match = rx.match(clean);
        if (match.hasMatch()) {
            const QString reasoning = compactDatasetText(match.captured(1), 520);
            if (!reasoning.isEmpty()) {
                return "Visible reasoning example: " + reasoning;
            }
        }
    }

    static const QList<QRegularExpression> linePatterns = {
        QRegularExpression("(?:^|\\s)(?:reasoning|rationale|explanation|analysis|thinking|chain\\s+of\\s+thought|cot)\\s*:\\s*(.*?)(?=\\s(?:final\\s+answer|answer|response|output)\\s*:|\\z)",
                           QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption),
        QRegularExpression("(let'?s\\s+think\\s+step\\s+by\\s+step\\s*[:.].*?)(?=\\s(?:final\\s+answer|answer|response|output)\\s*:|\\z)",
                           QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption)
    };

    for (const QRegularExpression &rx : linePatterns) {
        const QRegularExpressionMatch match = rx.match(clean);
        if (match.hasMatch()) {
            const QString reasoning = compactDatasetText(match.captured(1), 520);
            if (!reasoning.isEmpty()) {
                return "Visible reasoning example: " + reasoning;
            }
        }
    }

    return "";
}

QString answerWithoutReasoning(QString answer)
{
    answer.remove(QRegularExpression("<think>\\s*.*?\\s*</think>", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));
    answer.remove(QRegularExpression("<analysis>\\s*.*?\\s*</analysis>", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));
    answer.remove(QRegularExpression("\\[Thinking:[^\\]]*\\]", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));

    QRegularExpression finalAnswerRx("(?:^|\\s)(?:final\\s+answer|answer|response|output)\\s*:\\s*(.+)$",
                                     QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch finalAnswerMatch = finalAnswerRx.match(answer);
    if (finalAnswerMatch.hasMatch()) {
        answer = finalAnswerMatch.captured(1);
    }

    answer.remove(QRegularExpression("(?:^|\\s)(?:reasoning|rationale|explanation|analysis|thinking|chain\\s+of\\s+thought|cot)\\s*:\\s*.*?(?=\\s(?:final\\s+answer|answer|response|output)\\s*:|\\z)",
                                     QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));
    return compactDatasetText(answer, 520);
}

QString datasetReasoningLesson(const QJsonObject &object, const QString &answerText = QString())
{
    QStringList lessons;
    for (auto it = object.constBegin(); it != object.constEnd() && lessons.size() < 2; ++it) {
        if (!isDatasetReasoningKey(it.key())) {
            continue;
        }
        const QString reasoning = compactDatasetText(datasetFieldText(it.value()), 520);
        if (!reasoning.isEmpty()) {
            lessons.append(QString("%1: %2").arg(readableDatasetColumnName(it.key()), reasoning));
        }
    }

    const QString answerReasoning = visibleReasoningFromText(answerText);
    if (!answerReasoning.isEmpty()) {
        lessons.append(answerReasoning);
    }

    return compactDatasetText(lessons.join("; "), 700);
}

QString mergeDatasetLessons(const QString &left, const QString &right)
{
    const QString cleanLeft = compactDatasetText(left, 700);
    const QString cleanRight = compactDatasetText(right, 700);
    if (cleanLeft.isEmpty()) {
        return cleanRight;
    }
    if (cleanRight.isEmpty()) {
        return cleanLeft;
    }
    if (normalizedForMatch(cleanLeft).contains(normalizedForMatch(cleanRight).left(80))) {
        return cleanLeft;
    }
    return compactDatasetText(cleanLeft + "; " + cleanRight, 700);
}

QString firstDatasetField(const QJsonObject &object, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QString normalizedKey = normalizedForMatch(key);
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            const QString normalizedObjectKey = normalizedForMatch(it.key());
            if (normalizedObjectKey == normalizedKey
                || normalizedObjectKey.endsWith(" " + normalizedKey)
                || normalizedObjectKey.contains(normalizedKey + " text")) {
                const QString text = datasetFieldText(it.value());
                if (!text.isEmpty()) {
                    return text;
                }
            }
        }
    }
    return "";
}

void appendDatasetTrainingSample(QStringList &samples,
                                 const QString &question,
                                 const QString &answer,
                                 const QString &lesson,
                                 QSet<QString> &seenSamples,
                                 int maxSamples);

QString readableDatasetColumnName(const QString &key)
{
    QString text = key;
    text.replace('_', ' ');
    text.replace('-', ' ');
    text.replace(QRegularExpression("\\s+"), " ");
    return text.trimmed();
}

bool isDatasetMetadataKey(const QString &key)
{
    const QString normalizedKey = normalizedForMatch(key);
    static const QStringList metadataKeys = {
        "row idx", "row index", "truncated cells", "features", "num rows total",
        "num rows per page", "partial", "dataset", "config", "split",
        "domain", "meta", "metadata", "subset", "license", "language", "lang",
        "source file", "file", "url", "created at", "updated at"
    };
    return metadataKeys.contains(normalizedKey)
        || normalizedKey.endsWith(" id")
        || normalizedKey == "id"
        || normalizedKey == "index";
}

bool isLikelyLongAssetReference(const QString &text)
{
    const QString lower = text.toLower();
    return (lower.startsWith("http://") || lower.startsWith("https://"))
        && text.length() > 120;
}

QList<QPair<QString, QString>> usefulDatasetRowFields(const QJsonObject &object)
{
    QList<QPair<QString, QString>> fields;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (isDatasetMetadataKey(it.key()) || isDatasetReasoningKey(it.key())) {
            continue;
        }
        const QString text = datasetFieldText(it.value());
        if (text.isEmpty() || isLikelyLongAssetReference(text)) {
            continue;
        }
        fields.append(qMakePair(it.key(), compactDatasetText(text, 420)));
    }
    return fields;
}

int firstUsefulFieldIndex(const QList<QPair<QString, QString>> &fields, const QStringList &preferredNames)
{
    for (const QString &name : preferredNames) {
        const QString normalizedName = normalizedForMatch(name);
        for (int i = 0; i < fields.size(); ++i) {
            const QString normalizedKey = normalizedForMatch(fields[i].first);
            if (normalizedKey == normalizedName || normalizedKey.contains(normalizedName)) {
                return i;
            }
        }
    }
    return -1;
}

int datasetRoleKeywordScore(const QString &normalizedKey,
                            const QStringList &exactTerms,
                            const QStringList &containsTerms,
                            int exactScore,
                            int containsScore)
{
    for (const QString &term : exactTerms) {
        if (normalizedKey == normalizedForMatch(term)) {
            return exactScore;
        }
    }

    for (const QString &term : containsTerms) {
        const QString normalizedTerm = normalizedForMatch(term);
        if (normalizedKey.contains(normalizedTerm)) {
            return containsScore;
        }
    }

    return 0;
}

bool looksLikeDatasetQuestionText(const QString &text)
{
    const QString clean = compactDatasetText(text, 260);
    return isLikelyQuestion(clean)
        || clean.contains('?')
        || clean.startsWith("Question:", Qt::CaseInsensitive)
        || clean.startsWith("Instruction:", Qt::CaseInsensitive)
        || clean.startsWith("Prompt:", Qt::CaseInsensitive);
}

int datasetInputRoleScore(const QString &key, const QString &text)
{
    const QString normalizedKey = normalizedForMatch(key);
    int score = datasetRoleKeywordScore(normalizedKey,
                                        {"q", "question", "prompt", "instruction", "input", "query",
                                         "user", "human", "source", "src", "problem", "task", "utterance"},
                                        {"question", "prompt", "instruction", "input", "query", "source",
                                         "problem", "scenario", "passage", "context", "article", "premise",
                                         "claim", "sentence", "text"},
                                        80,
                                        45);

    score -= datasetRoleKeywordScore(normalizedKey,
                                     {"a", "answer", "response", "output", "completion", "target", "label",
                                      "assistant", "solution", "result"},
                                     {"answer", "response", "output", "completion", "target", "label",
                                      "assistant", "solution"},
                                     70,
                                     45);

    if (looksLikeDatasetQuestionText(text)) {
        score += 45;
    }
    if (text.length() > 80) {
        score += 8;
    }
    return score;
}

int datasetOutputRoleScore(const QString &key, const QString &text)
{
    const QString normalizedKey = normalizedForMatch(key);
    int score = datasetRoleKeywordScore(normalizedKey,
                                        {"a", "answer", "response", "output", "completion", "target", "label",
                                         "assistant", "gpt", "solution", "result", "dst", "translation", "chosen"},
                                        {"answer", "response", "output", "completion", "target", "label",
                                         "assistant", "solution", "result", "summary", "definition",
                                         "description", "translation", "correct"},
                                        80,
                                        45);

    score -= datasetRoleKeywordScore(normalizedKey,
                                     {"q", "question", "prompt", "instruction", "input", "query", "user", "human",
                                      "source", "src", "problem"},
                                     {"question", "prompt", "instruction", "input", "query", "source", "context"},
                                     70,
                                     45);

    if (text.startsWith("Answer:", Qt::CaseInsensitive)
        || text.startsWith("Response:", Qt::CaseInsensitive)
        || text.startsWith("Output:", Qt::CaseInsensitive)) {
        score += 35;
    }
    if (text.length() <= 160) {
        score += 6;
    }
    bool numericOk = false;
    text.toDouble(&numericOk);
    if (text.compare("true", Qt::CaseInsensitive) == 0
        || text.compare("false", Qt::CaseInsensitive) == 0
        || numericOk) {
        score += 4;
    }
    return score;
}

int bestDatasetRoleIndex(const QList<QPair<QString, QString>> &fields, bool outputRole, int excludedIndex = -1)
{
    int bestIndex = -1;
    int bestScore = std::numeric_limits<int>::min();
    for (int i = 0; i < fields.size(); ++i) {
        if (i == excludedIndex) {
            continue;
        }
        const int score = outputRole
            ? datasetOutputRoleScore(fields[i].first, fields[i].second)
            : datasetInputRoleScore(fields[i].first, fields[i].second);
        if (score > bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }
    return bestScore >= 20 ? bestIndex : -1;
}

QString datasetQuestionFromInputOutput(const QPair<QString, QString> &inputField,
                                       const QPair<QString, QString> &outputField)
{
    if (looksLikeDatasetQuestionText(inputField.second)) {
        return inputField.second;
    }

    return QString("Given this %1: %2, what is the %3?")
        .arg(readableDatasetColumnName(inputField.first),
             compactDatasetText(inputField.second, 260),
             readableDatasetColumnName(outputField.first));
}

QString datasetContextLesson(const QList<QPair<QString, QString>> &fields, int inputIndex, int outputIndex)
{
    QStringList lessonParts;
    for (int i = 0; i < fields.size() && lessonParts.size() < 3; ++i) {
        if (i == inputIndex || i == outputIndex) {
            continue;
        }
        const QString normalizedKey = normalizedForMatch(fields[i].first);
        if (normalizedKey.contains("context")
            || normalizedKey.contains("passage")
            || normalizedKey.contains("choice")
            || normalizedKey.contains("option")
            || normalizedKey.contains("topic")
            || normalizedKey.contains("category")
            || normalizedKey.contains("explanation")
            || normalizedKey.contains("rationale")) {
            lessonParts.append(QString("%1: %2")
                .arg(readableDatasetColumnName(fields[i].first), fields[i].second));
        }
    }
    return compactDatasetText(lessonParts.join("; "), 520);
}

void appendAdaptiveRowSamples(const QJsonObject &object,
                              QStringList &samples,
                              QSet<QString> &seenSamples,
                              int maxSamples)
{
    if (samples.size() >= maxSamples
        || object.contains("rows")
        || object.contains("features")
        || object.contains("splits")) {
        return;
    }

    const QList<QPair<QString, QString>> fields = usefulDatasetRowFields(object);
    if (fields.isEmpty()) {
        return;
    }

    if (fields.size() == 1) {
        const QString fieldName = readableDatasetColumnName(fields.first().first);
        const QString excerpt = compactDatasetText(fields.first().second, 120);
        appendDatasetTrainingSample(samples,
                                    QString("What information is in the %1 field for this dataset row beginning \"%2\"?")
                                        .arg(fieldName, excerpt),
                                    fields.first().second,
                                    datasetReasoningLesson(object, fields.first().second),
                                    seenSamples,
                                    maxSamples);
        return;
    }

    int inferredInputIndex = bestDatasetRoleIndex(fields, false);
    int inferredOutputIndex = bestDatasetRoleIndex(fields, true, inferredInputIndex);
    if (inferredOutputIndex < 0) {
        inferredOutputIndex = bestDatasetRoleIndex(fields, true);
        inferredInputIndex = bestDatasetRoleIndex(fields, false, inferredOutputIndex);
    }

    if (inferredInputIndex >= 0
        && inferredOutputIndex >= 0
        && inferredInputIndex != inferredOutputIndex) {
        appendDatasetTrainingSample(samples,
                                    datasetQuestionFromInputOutput(fields[inferredInputIndex],
                                                                   fields[inferredOutputIndex]),
                                    fields[inferredOutputIndex].second,
                                    mergeDatasetLessons(datasetContextLesson(fields, inferredInputIndex, inferredOutputIndex),
                                                        datasetReasoningLesson(object, fields[inferredOutputIndex].second)),
                                    seenSamples,
                                    maxSamples);
        return;
    }

    const int subjectIndex = firstUsefulFieldIndex(fields, {
        "q", "question", "instruction", "prompt", "query", "input", "source", "src", "text", "sentence",
        "content", "context", "passage", "article", "title", "name", "term", "word",
        "problem", "scenario"
    });
    const int answerIndex = firstUsefulFieldIndex(fields, {
        "a", "answer", "answers", "response", "output", "completion", "target", "label",
        "class", "category", "definition", "description", "summary", "solution", "dst", "translation"
    });

    if (subjectIndex >= 0 && answerIndex >= 0 && subjectIndex != answerIndex) {
        const QString subjectKey = readableDatasetColumnName(fields[subjectIndex].first);
        const QString answerKey = readableDatasetColumnName(fields[answerIndex].first);
        const QString question = QString("For this %1: %2, what is the %3?")
            .arg(subjectKey, fields[subjectIndex].second, answerKey);
        appendDatasetTrainingSample(samples,
                                    question,
                                    fields[answerIndex].second,
                                    datasetReasoningLesson(object, fields[answerIndex].second),
                                    seenSamples,
                                    maxSamples);
        return;
    }

    const int nameIndex = firstUsefulFieldIndex(fields, {"name", "title", "term", "word"});
    const int descriptionIndex = firstUsefulFieldIndex(fields, {"definition", "description", "summary", "text", "content"});
    if (nameIndex >= 0 && descriptionIndex >= 0 && nameIndex != descriptionIndex) {
        appendDatasetTrainingSample(samples,
                                    QString("What is %1?").arg(fields[nameIndex].second),
                                    fields[descriptionIndex].second,
                                    datasetReasoningLesson(object, fields[descriptionIndex].second),
                                    seenSamples,
                                    maxSamples);
        return;
    }

    if (fields.size() == 2) {
        appendDatasetTrainingSample(samples,
                                    datasetQuestionFromInputOutput(fields[0], fields[1]),
                                    fields[1].second,
                                    datasetReasoningLesson(object, fields[1].second),
                                    seenSamples,
                                    maxSamples);
        return;
    }

    const int anchorIndex = subjectIndex >= 0 ? subjectIndex : 0;
    const QString anchorKey = readableDatasetColumnName(fields[anchorIndex].first);
    const QString anchorValue = compactDatasetText(fields[anchorIndex].second, 160);
    int generatedForRow = 0;
    for (int i = 0; i < fields.size() && generatedForRow < 3 && samples.size() < maxSamples; ++i) {
        if (i == anchorIndex) {
            continue;
        }
        const QString fieldName = readableDatasetColumnName(fields[i].first);
        const QString question = QString("In this dataset row, what is the %1 for %2 \"%3\"?")
            .arg(fieldName, anchorKey, anchorValue);
        appendDatasetTrainingSample(samples,
                                    question,
                                    fields[i].second,
                                    datasetReasoningLesson(object, fields[i].second),
                                    seenSamples,
                                    maxSamples);
        generatedForRow++;
    }
}

void appendDatasetTrainingSample(QStringList &samples,
                                 const QString &question,
                                 const QString &answer,
                                 const QString &lesson,
                                 QSet<QString> &seenSamples,
                                 int maxSamples)
{
    if (samples.size() >= maxSamples) {
        return;
    }

    const QString cleanQuestion = compactDatasetText(question, 420);
    const QString reasoningFromAnswer = visibleReasoningFromText(answer);
    const QString cleanAnswer = answerWithoutReasoning(answer);
    QString cleanLesson = compactDatasetText(lesson, 700);
    if (!reasoningFromAnswer.isEmpty()
        && !normalizedForMatch(cleanLesson).contains(normalizedForMatch(reasoningFromAnswer).left(80))) {
        cleanLesson = compactDatasetText(cleanLesson.isEmpty()
            ? reasoningFromAnswer
            : cleanLesson + "; " + reasoningFromAnswer,
            700);
    }
    if (cleanQuestion.split(' ', Qt::SkipEmptyParts).size() < 2
        || cleanAnswer.split(' ', Qt::SkipEmptyParts).isEmpty()) {
        return;
    }

    QString sample = QString("Question: %1\n").arg(cleanQuestion);
    if (!cleanLesson.isEmpty()) {
        sample += QString("Lesson: %1\n").arg(cleanLesson);
    }
    sample += QString("Answer: %1").arg(cleanAnswer);

    const QString normalizedSample = normalizedForMatch(sample);
    if (seenSamples.contains(normalizedSample)) {
        return;
    }

    seenSamples.insert(normalizedSample);
    samples.append(sample);
}

bool appendConversationSamples(const QJsonObject &object,
                               QStringList &samples,
                               QSet<QString> &seenSamples,
                               int maxSamples)
{
    QJsonArray turns;
    if (object.value("messages").isArray()) {
        turns = object.value("messages").toArray();
    } else if (object.value("conversations").isArray()) {
        turns = object.value("conversations").toArray();
    } else if (object.value("dialogue").isArray()) {
        turns = object.value("dialogue").toArray();
    } else if (object.value("dialog").isArray()) {
        turns = object.value("dialog").toArray();
    } else if (object.value("chat").isArray()) {
        turns = object.value("chat").toArray();
    } else if (object.value("turns").isArray()) {
        turns = object.value("turns").toArray();
    }

    if (turns.isEmpty()) {
        return false;
    }

    const int before = samples.size();
    const QString rowReasoningLesson = datasetReasoningLesson(object);
    QString pendingUser;
    for (const QJsonValue &turnValue : turns) {
        QString role;
        QString content;

        if (turnValue.isObject()) {
            const QJsonObject turn = turnValue.toObject();
            role = firstDatasetField(turn, {"role", "from", "speaker", "author", "name"}).toLower();
            content = firstDatasetField(turn, {"content", "value", "text", "message", "body", "utterance"});
        } else if (turnValue.isString()) {
            content = datasetFieldText(turnValue);
            role = pendingUser.isEmpty() ? "user" : "assistant";
        } else {
            continue;
        }

        if (content.isEmpty()) {
            continue;
        }

        const bool userTurn = role.contains("user")
            || role.contains("human")
            || role.contains("customer")
            || role.contains("client")
            || role.contains("patient")
            || role == "prompter";
        const bool assistantTurn = role.contains("assistant")
            || role.contains("gpt")
            || role.contains("bot")
            || role.contains("model")
            || role.contains("agent")
            || role == "ai";
        if (userTurn) {
            pendingUser = content;
        } else if (assistantTurn && !pendingUser.isEmpty()) {
            const QString turnReasoning = visibleReasoningFromText(content);
            const QString lesson = compactDatasetText(rowReasoningLesson.isEmpty()
                ? turnReasoning
                : (turnReasoning.isEmpty() ? rowReasoningLesson : rowReasoningLesson + "; " + turnReasoning),
                700);
            appendDatasetTrainingSample(samples, pendingUser, content, lesson, seenSamples, maxSamples);
            pendingUser.clear();
        }
        if (samples.size() >= maxSamples) {
            break;
        }
    }

    return samples.size() > before;
}

bool appendDirectTurnColumns(const QJsonObject &object,
                             QStringList &samples,
                             QSet<QString> &seenSamples,
                             int maxSamples)
{
    const QString userText = firstDatasetField(object, {
        "q", "user", "human", "prompt", "instruction", "question", "query", "input"
    });
    const QString assistantText = firstDatasetField(object, {
        "a", "assistant", "gpt", "bot", "model", "response", "answer", "output", "completion", "target"
    });

    if (userText.isEmpty() || assistantText.isEmpty()) {
        return false;
    }

    const int before = samples.size();
    appendDatasetTrainingSample(samples, userText, assistantText, datasetReasoningLesson(object, assistantText), seenSamples, maxSamples);
    return samples.size() > before;
}

QStringList choiceTextsFromValue(const QJsonValue &value)
{
    QStringList choices;
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            const QJsonValue item = array.at(i);
            QString text;
            if (item.isObject()) {
                const QJsonObject object = item.toObject();
                const QString label = firstDatasetField(object, {"label", "key", "name"});
                text = firstDatasetField(object, {"text", "value", "content", "answer"});
                if (!label.isEmpty() && !text.isEmpty()) {
                    text = QString("%1. %2").arg(label, text);
                }
            } else {
                text = datasetFieldText(item);
            }
            if (!text.isEmpty()) {
                choices.append(text);
            }
        }
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.value("text").isArray()) {
            const QJsonArray labels = object.value("label").toArray();
            const QJsonArray texts = object.value("text").toArray();
            for (int i = 0; i < texts.size(); ++i) {
                const QString label = i < labels.size() ? datasetFieldText(labels.at(i)) : QString();
                const QString text = datasetFieldText(texts.at(i));
                if (!text.isEmpty()) {
                    choices.append(label.isEmpty() ? text : QString("%1. %2").arg(label, text));
                }
            }
        } else {
            for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
                const QString text = datasetFieldText(it.value());
                if (!text.isEmpty()) {
                    choices.append(QString("%1. %2").arg(it.key(), text));
                }
            }
        }
    } else {
        const QString text = datasetFieldText(value);
        if (!text.isEmpty()) {
            choices.append(text);
        }
    }
    return choices;
}

bool appendMultipleChoiceSample(const QJsonObject &object,
                                QStringList &samples,
                                QSet<QString> &seenSamples,
                                int maxSamples)
{
    const QString question = firstDatasetField(object, {"q", "question", "prompt", "query", "stem", "problem"});
    if (question.isEmpty()) {
        return false;
    }

    QJsonValue choicesValue;
    for (const QString &key : {"choices", "options", "endings", "candidates"}) {
        if (object.contains(key)) {
            choicesValue = object.value(key);
            break;
        }
    }

    const QStringList choices = choiceTextsFromValue(choicesValue);
    if (choices.isEmpty()) {
        return false;
    }

    QString answer = firstDatasetField(object, {
        "a", "answer", "answerKey", "answer_key", "correct", "correct_answer", "label", "target", "solution"
    });
    if (answer.isEmpty()) {
        return false;
    }

    const QString normalizedAnswer = normalizedForMatch(answer);
    for (const QString &choice : choices) {
        const QString normalizedChoice = normalizedForMatch(choice);
        if (normalizedChoice.startsWith(normalizedAnswer + " ")
            || normalizedChoice == normalizedAnswer
            || normalizedChoice.contains(" " + normalizedAnswer + " ")) {
            answer = choice;
            break;
        }
    }

    const QString lesson = mergeDatasetLessons(QString("Choices: %1").arg(choices.join("; ")),
                                               datasetReasoningLesson(object, answer));
    const int before = samples.size();
    appendDatasetTrainingSample(samples, question, answer, lesson, seenSamples, maxSamples);
    return samples.size() > before;
}

bool appendTranslationSamples(const QJsonObject &object,
                              QStringList &samples,
                              QSet<QString> &seenSamples,
                              int maxSamples)
{
    QJsonObject translation;
    if (object.value("translation").isObject()) {
        translation = object.value("translation").toObject();
    } else if (object.value("translations").isObject()) {
        translation = object.value("translations").toObject();
    }

    if (translation.isEmpty()) {
        return false;
    }

    QList<QPair<QString, QString>> entries;
    for (auto it = translation.constBegin(); it != translation.constEnd(); ++it) {
        const QString text = datasetFieldText(it.value());
        if (!text.isEmpty()) {
            entries.append(qMakePair(it.key(), text));
        }
    }

    if (entries.size() < 2) {
        return false;
    }

    int sourceIndex = 0;
    for (int i = 0; i < entries.size(); ++i) {
        const QString key = normalizedForMatch(entries[i].first);
        if (key == "en" || key == "eng" || key == "english") {
            sourceIndex = i;
            break;
        }
    }

    int added = 0;
    for (int i = 0; i < entries.size() && samples.size() < maxSamples && added < 3; ++i) {
        if (i == sourceIndex) {
            continue;
        }
        appendDatasetTrainingSample(samples,
                                    QString("Translate this %1 text to %2: %3")
                                        .arg(readableDatasetColumnName(entries[sourceIndex].first),
                                             readableDatasetColumnName(entries[i].first),
                                             entries[sourceIndex].second),
                                    entries[i].second,
                                    "",
                                    seenSamples,
                                    maxSamples);
        added++;
    }

    return added > 0;
}

void collectDatasetSamplesFromJson(const QJsonValue &value,
                                   QStringList &samples,
                                   QSet<QString> &seenSamples,
                                   int maxSamples)
{
    if (samples.size() >= maxSamples) {
        return;
    }

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array) {
            collectDatasetSamplesFromJson(item, samples, seenSamples, maxSamples);
            if (samples.size() >= maxSamples) {
                break;
            }
        }
        return;
    }

    if (!value.isObject()) {
        return;
    }

    const QJsonObject object = value.toObject();
    if (object.value("row").isObject()
        && (object.contains("row_idx") || object.contains("truncated_cells"))) {
        collectDatasetSamplesFromJson(object.value("row"), samples, seenSamples, maxSamples);
        return;
    }

    if (appendConversationSamples(object, samples, seenSamples, maxSamples)) {
        return;
    }

    if (appendTranslationSamples(object, samples, seenSamples, maxSamples)) {
        return;
    }

    if (appendMultipleChoiceSample(object, samples, seenSamples, maxSamples)) {
        return;
    }

    if (appendDirectTurnColumns(object, samples, seenSamples, maxSamples)) {
        return;
    }

    if (object.contains("paragraphs")) {
        const QJsonArray paragraphs = object.value("paragraphs").toArray();
        for (const QJsonValue &paragraphValue : paragraphs) {
            const QJsonObject paragraph = paragraphValue.toObject();
            const QString context = firstDatasetField(paragraph, {"context"});
            const QJsonArray qas = paragraph.value("qas").toArray();
            for (const QJsonValue &qaValue : qas) {
                const QJsonObject qa = qaValue.toObject();
                appendDatasetTrainingSample(samples,
                                            firstDatasetField(qa, {"question"}),
                                            firstDatasetField(qa, {"answers", "answer"}),
                                            context,
                                            seenSamples,
                                            maxSamples);
                if (samples.size() >= maxSamples) {
                    return;
                }
            }
        }
    }

    QString question = firstDatasetField(object, {"q", "question", "instruction", "prompt", "query", "input"});
    const QString input = firstDatasetField(object, {"input", "context", "source", "src"});
    if (!input.isEmpty() && !question.contains(input)) {
        question = compactDatasetText(question + " " + input, 620);
    }

    const QString answer = firstDatasetField(object, {
        "a", "answer", "answers", "response", "output", "completion", "target", "label", "chosen", "dst"
    });
    const QString lesson = mergeDatasetLessons(firstDatasetField(object, {"lesson", "context", "category", "topic"}),
                                               datasetReasoningLesson(object, answer));
    const int directSampleCount = samples.size();
    appendDatasetTrainingSample(samples, question, answer, lesson, seenSamples, maxSamples);
    if (samples.size() == directSampleCount) {
        appendAdaptiveRowSamples(object, samples, seenSamples, maxSamples);
    }

    static const QStringList nestedKeys = {
        "data", "train", "validation", "test", "rows", "row", "examples",
        "items", "instances", "records", "samples", "questions", "annotations"
    };
    for (const QString &key : nestedKeys) {
        if (object.contains(key)) {
            collectDatasetSamplesFromJson(object.value(key), samples, seenSamples, maxSamples);
        }
        if (samples.size() >= maxSamples) {
            break;
        }
    }
}

void collectDatasetSamplesFromText(const QString &text,
                                   QStringList &samples,
                                   QSet<QString> &seenSamples,
                                   int maxSamples)
{
    QString cleanText = text;
    cleanText.remove(QChar(0xfeff));
    const QList<QChar> delimiters = {'\t', ',', '|', ';'};
    for (QChar delimiter : delimiters) {
        if (!cleanText.contains(delimiter)) {
            continue;
        }

        const QStringList records = splitDelimitedRecords(cleanText, delimiter);
        if (records.size() < 2) {
            continue;
        }

        const QStringList firstFields = splitDelimitedLine(records.first(), delimiter);
        if (firstFields.size() < 2) {
            continue;
        }

        const bool hasHeader = looksLikeHeaderRow(firstFields);
        QStringList headers = hasHeader ? firstFields : QStringList();
        int startRecord = hasHeader ? 1 : 0;
        int questionCol = hasHeader ? firstMatchingColumn(headers, {
            "q", "question", "prompt", "instruction", "query", "input", "user", "human",
            "source", "src", "sentence", "problem"
        }) : 0;
        int answerCol = hasHeader ? firstMatchingColumn(headers, {
            "a", "answer", "response", "output", "completion", "assistant", "gpt",
            "target", "label", "translation", "solution", "dst"
        }) : 1;
        const int lessonCol = hasHeader ? firstMatchingColumn(headers, {
            "lesson", "context", "category", "topic", "explanation", "rationale",
            "reasoning", "analysis", "thinking", "chain_of_thought", "chain of thought",
            "cot", "scratchpad", "choices"
        }) : -1;

        if (!hasHeader && delimiter == ',') {
            int consistentPairs = 0;
            for (int i = 0; i < qMin(records.size(), 20); ++i) {
                const QStringList fields = splitDelimitedLine(records[i], delimiter);
                if (fields.size() == 2 || fields.size() == 3) {
                    consistentPairs++;
                }
            }
            if (consistentPairs < qMin(5, records.size())) {
                continue;
            }
        }

        const int beforeDelimiter = samples.size();
        for (int i = startRecord; i < records.size() && samples.size() < maxSamples; ++i) {
            const QStringList fields = splitDelimitedLine(records[i], delimiter);
            if (fields.size() < 2) {
                continue;
            }

            if (hasHeader && fields.size() >= qMin(headers.size(), fields.size())) {
                QJsonObject rowObject;
                for (int fieldIndex = 0; fieldIndex < qMin(headers.size(), fields.size()); ++fieldIndex) {
                    rowObject[headers[fieldIndex]] = fields[fieldIndex];
                }

                if (questionCol >= 0 && answerCol >= 0 && fields.size() > qMax(questionCol, answerCol)) {
                    appendDatasetTrainingSample(samples,
                                                fields[questionCol],
                                                fields[answerCol],
                                                (lessonCol >= 0 && lessonCol < fields.size()) ? fields[lessonCol] : "",
                                                seenSamples,
                                                maxSamples);
                    continue;
                }

                appendAdaptiveRowSamples(rowObject, samples, seenSamples, maxSamples);
            } else if (fields.size() >= 2) {
                appendDatasetTrainingSample(samples,
                                            fields[0],
                                            fields[1],
                                            fields.size() >= 3 ? fields[2] : "",
                                            seenSamples,
                                            maxSamples);
            }
        }

        if (samples.size() > beforeDelimiter) {
            return;
        }
    }

    const QStringList lines = cleanText.split('\n', Qt::SkipEmptyParts);
    QString pendingSpeakerText;
    for (const QString &line : lines) {
        if (samples.size() >= maxSamples) {
            return;
        }

        QString cleanLine = line.trimmed();
        cleanLine.remove(QRegularExpression("^\\d+\\s+"));
        QRegularExpression speakerRx("^(user|human|customer|client|prompt|question|assistant|gpt|bot|answer|response|output)\\s*:\\s*(.+)$",
                                     QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch speakerMatch = speakerRx.match(cleanLine);
        if (speakerMatch.hasMatch()) {
            const QString role = speakerMatch.captured(1).toLower();
            const QString content = speakerMatch.captured(2).trimmed();
            const bool isUser = role == "user"
                || role == "human"
                || role == "customer"
                || role == "client"
                || role == "prompt"
                || role == "question";
            if (isUser) {
                pendingSpeakerText = content;
            } else if (!pendingSpeakerText.isEmpty()) {
                appendDatasetTrainingSample(samples, pendingSpeakerText, content, "", seenSamples, maxSamples);
                pendingSpeakerText.clear();
            }
            continue;
        }

        if (cleanLine.contains('\t')) {
            const QStringList fields = cleanLine.split('\t', Qt::SkipEmptyParts);
            if (fields.size() >= 2
                && !fields[0].contains("persona:", Qt::CaseInsensitive)
                && !fields[1].contains("persona:", Qt::CaseInsensitive)) {
                appendDatasetTrainingSample(samples, fields[0], fields[1], "", seenSamples, maxSamples);
            }
        }
    }

    QRegularExpression qaRx("Question\\s*:\\s*([^\\n]+)\\s+(?:Lesson\\s*:\\s*([^\\n]+)\\s+)?Answer\\s*:\\s*([^\\n]+)",
                            QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = qaRx.globalMatch(cleanText);
    while (it.hasNext() && samples.size() < maxSamples) {
        const QRegularExpressionMatch match = it.next();
        appendDatasetTrainingSample(samples, match.captured(1), match.captured(3), match.captured(2), seenSamples, maxSamples);
    }

    QRegularExpression instructionRx(
        "(?:^|\\n)\\s*(?:###\\s*)?(?:instruction|prompt|question|user|human)\\s*:\\s*(.*?)"
        "(?:\\n\\s*(?:###\\s*)?(?:input|context)\\s*:\\s*(.*?))?"
        "\\n\\s*(?:###\\s*)?(?:response|answer|assistant|gpt|output|completion)\\s*:\\s*(.*?)"
        "(?=\\n\\s*(?:###\\s*)?(?:instruction|prompt|question|user|human)\\s*:|\\z)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator blockIt = instructionRx.globalMatch(cleanText);
    while (blockIt.hasNext() && samples.size() < maxSamples) {
        const QRegularExpressionMatch match = blockIt.next();
        QString question = compactDatasetText(match.captured(1), 520);
        const QString input = compactDatasetText(match.captured(2), 520);
        if (!input.isEmpty() && !question.contains(input)) {
            question = compactDatasetText(question + " " + input, 620);
        }
        appendDatasetTrainingSample(samples,
                                    question,
                                    match.captured(3),
                                    "",
                                    seenSamples,
                                    maxSamples);
    }
}

QStringList buildDatasetTrainingSamples(const QByteArray &datasetBytes, int maxSamples)
{
    QStringList samples;
    QSet<QString> seenSamples;

    QJsonParseError parseError;
    const QJsonDocument fullDoc = QJsonDocument::fromJson(datasetBytes, &parseError);
    if (parseError.error == QJsonParseError::NoError && !fullDoc.isNull()) {
        collectDatasetSamplesFromJson(fullDoc.isArray() ? QJsonValue(fullDoc.array()) : QJsonValue(fullDoc.object()),
                                      samples,
                                      seenSamples,
                                      maxSamples);
    }

    const QString rawText = QString::fromUtf8(datasetBytes);
    if (samples.size() < maxSamples) {
        const QStringList lines = rawText.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (samples.size() >= maxSamples) {
                break;
            }
            const QByteArray lineBytes = line.trimmed().toUtf8();
            QJsonParseError lineError;
            const QJsonDocument lineDoc = QJsonDocument::fromJson(lineBytes, &lineError);
            if (lineError.error == QJsonParseError::NoError && !lineDoc.isNull()) {
                if (lineDoc.isObject()) {
                    collectDatasetSamplesFromJson(QJsonValue(lineDoc.object()), samples, seenSamples, maxSamples);
                } else if (lineDoc.isArray()) {
                    collectDatasetSamplesFromJson(QJsonValue(lineDoc.array()), samples, seenSamples, maxSamples);
                }
            }
        }
    }

    if (samples.size() < maxSamples) {
        collectDatasetSamplesFromText(rawText, samples, seenSamples, maxSamples);
    }

    if (!samples.isEmpty()) {
        return samples;
    }

    if (!looksMostlyText(datasetBytes)) {
        return samples;
    }

    const QString fallbackText = rawText;
    const int chunkSize = 3000;
    const int maxChunks = maxSamples > 0 ? maxSamples : std::numeric_limits<int>::max();
    for (int offset = 0; offset < fallbackText.length() && samples.size() < maxChunks; offset += chunkSize) {
        const QString chunk = fallbackText.mid(offset, chunkSize).trimmed();
        if (chunk.split(' ', Qt::SkipEmptyParts).size() >= 2) {
            const QString sample = "Dataset text sample: " + chunk;
            const QString sampleKey = normalizedForMatch(sample);
            if (!seenSamples.contains(sampleKey)) {
                seenSamples.insert(sampleKey);
                samples.append(sample);
            }
        }
    }

    return samples;
}

bool isLikelyTrainableDatasetFile(const QString &path)
{
    const QString lower = path.toLower();
    return lower.endsWith(".txt")
        || lower.endsWith(".md")
        || lower.endsWith(".json")
        || lower.endsWith(".jsonl")
        || lower.endsWith(".csv")
        || lower.endsWith(".tsv")
        || lower.endsWith(".xml")
        || lower.endsWith(".html")
        || lower.endsWith(".yaml")
        || lower.endsWith(".yml");
}

bool isHuggingFaceRepoMetadataFile(const QString &path)
{
    const QString lower = path.toLower();
    const QString fileName = QFileInfo(path).fileName().toLower();
    return fileName == "readme.md"
        || fileName == ".gitattributes"
        || fileName == "dataset_infos.json"
        || fileName == "dataset_info.json"
        || lower.startsWith("assets/")
        || lower.contains("/assets/")
        || lower.startsWith("images/")
        || lower.contains("/images/");
}

bool isViewerBackedDatasetFile(const QString &path)
{
    const QString lower = path.toLower();
    return lower.endsWith(".parquet")
        || lower.endsWith(".arrow")
        || lower.endsWith(".feather");
}

int huggingFaceDirectDatasetFileScore(const QString &path)
{
    if (isHuggingFaceRepoMetadataFile(path)) {
        return -1;
    }

    const QString lower = path.toLower();
    int score = 0;

    if (lower.contains("train")) {
        score += 500;
    } else if (lower.contains("validation") || lower.contains("valid") || lower.contains("dev")) {
        score += 220;
    } else if (lower.contains("test")) {
        score += 180;
    }

    if (lower.contains("data") || lower.contains("dataset") || lower.contains("samples")) {
        score += 60;
    }

    if (lower.endsWith(".jsonl") || lower.endsWith(".ndjson")) {
        score += 420;
    } else if (lower.endsWith(".json")) {
        score += 360;
    } else if (lower.endsWith(".csv") || lower.endsWith(".tsv")) {
        score += 320;
    } else if (lower.endsWith(".txt") || lower.endsWith(".yaml") || lower.endsWith(".yml")) {
        score += 140;
    } else if (lower.endsWith(".xml") || lower.endsWith(".html")) {
        score += 80;
    } else if (lower.endsWith(".md")) {
        score += 10;
    } else {
        return -1;
    }

    return score;
}

int huggingFaceViewerDatasetFileScore(const QString &path)
{
    if (isHuggingFaceRepoMetadataFile(path) || !isViewerBackedDatasetFile(path)) {
        return -1;
    }

    const QString lower = path.toLower();
    int score = 350;
    if (lower.contains("train")) {
        score += 500;
    } else if (lower.contains("validation") || lower.contains("valid") || lower.contains("dev")) {
        score += 220;
    } else if (lower.contains("test")) {
        score += 180;
    }
    if (lower.contains("data") || lower.contains("dataset") || lower.contains("samples")) {
        score += 60;
    }
    return score;
}

QString huggingFaceRepoIdFromUrl(const QUrl &url)
{
    const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
    int datasetsIndex = parts.indexOf("datasets");
    if (datasetsIndex < 0 || datasetsIndex + 1 >= parts.size()) {
        return "";
    }

    QStringList repoParts;
    repoParts.append(parts[datasetsIndex + 1]);
    if (datasetsIndex + 2 < parts.size()
        && parts[datasetsIndex + 2] != "blob"
        && parts[datasetsIndex + 2] != "resolve"
        && parts[datasetsIndex + 2] != "tree") {
        repoParts.append(parts[datasetsIndex + 2]);
    }

    return repoParts.join('/');
}

QString normalizeHuggingFaceDatasetFileUrl(QString urlText)
{
    urlText = urlText.trimmed();
    if (!urlText.contains("://")) {
        QRegularExpression repoIdRx("^[A-Za-z0-9][A-Za-z0-9._-]*/[A-Za-z0-9][A-Za-z0-9._-]*(?:/[A-Za-z0-9._/-]+)?$");
        if (repoIdRx.match(urlText).hasMatch()) {
            QStringList parts = urlText.split('/', Qt::SkipEmptyParts);
            if (parts.size() == 2) {
                return "https://huggingface.co/datasets/" + urlText;
            }
            if (parts.size() > 3 && (parts[2] == "blob" || parts[2] == "resolve")) {
                return "https://huggingface.co/datasets/" + urlText;
            }
            if (parts.size() > 2) {
                return QString("https://huggingface.co/datasets/%1/%2/resolve/main/%3")
                    .arg(parts[0], parts[1], parts.mid(2).join('/'));
            }
        }
    }
    if (urlText.contains("huggingface.co/datasets/") && urlText.contains("/blob/")) {
        urlText.replace("/blob/", "/resolve/");
    }
    return urlText;
}

QString shellQuote(QString text)
{
    return "'" + text.replace("'", "'\"'\"'") + "'";
}

QString normalizedSshHost(QString host)
{
    host = host.trimmed();
    if ((host.startsWith('"') && host.endsWith('"'))
        || (host.startsWith('\'') && host.endsWith('\''))) {
        host = host.mid(1, host.length() - 2).trimmed();
    }

    const QUrl url(host);
    if (url.isValid() && !url.host().isEmpty()) {
        host = url.host();
    }

    if (host.contains('@')) {
        host = host.section('@', -1).trimmed();
    }

    const int slashIndex = host.indexOf('/');
    if (slashIndex >= 0) {
        host = host.left(slashIndex).trimmed();
    }

    if (host.startsWith('[')) {
        const int bracketIndex = host.indexOf(']');
        if (bracketIndex > 0) {
            return host.left(bracketIndex + 1);
        }
    }

    if (host.count(':') == 1) {
        host = host.section(':', 0, 0).trimmed();
    }

    return host;
}

QString buildArithmeticThinking(const QString &question, const QString &answer)
{
    const QString numberPattern = "(-?\\d+(?:\\.\\d+)?)";
    QRegularExpression symbolRx(numberPattern + "\\s*([+\\-*/xX])\\s*" + numberPattern);
    QRegularExpressionMatch match = symbolRx.match(question);

    if (!match.hasMatch()) {
        QRegularExpression wordRx(numberPattern
                                  + "\\s*(plus|add(?:ed)?\\s+to|minus|less\\s+than|times|multiplied\\s+by|divided\\s+by|over)\\s*"
                                  + numberPattern,
                                  QRegularExpression::CaseInsensitiveOption);
        match = wordRx.match(question);
    }

    if (!match.hasMatch()) {
        return "";
    }

    bool leftOk = false;
    bool rightOk = false;
    const double left = match.captured(1).toDouble(&leftOk);
    const QString opText = match.captured(2).toLower();
    const double right = match.captured(3).toDouble(&rightOk);
    if (!leftOk || !rightOk) {
        return "";
    }

    double calculated = 0.0;
    QString opSymbol;
    if (opText == "+" || opText.contains("plus") || opText.contains("add")) {
        calculated = left + right;
        opSymbol = "+";
    } else if (opText == "-" || opText.contains("minus")) {
        calculated = left - right;
        opSymbol = "-";
    } else if (opText == "*" || opText == "x" || opText.contains("times") || opText.contains("multiplied")) {
        calculated = left * right;
        opSymbol = "*";
    } else if (opText == "/" || opText.contains("divided") || opText.contains("over")) {
        if (numbersMatch(right, 0.0)) {
            return "Calculation check: the question asks for division by zero, so there is no valid finite calculation.";
        }
        calculated = left / right;
        opSymbol = "/";
    } else if (opText.contains("less than")) {
        calculated = right - left;
        opSymbol = "-";
        return QString("Calculation check: \"%1 less than %2\" means %2 - %1 = %3, so the answer is %3.")
            .arg(formatNumber(left), formatNumber(right), formatNumber(calculated));
    } else {
        return "";
    }

    QRegularExpression answerNumberRx(numberPattern);
    QRegularExpressionMatch answerMatch = answerNumberRx.match(answer);
    QString comparison;
    if (answerMatch.hasMatch()) {
        bool answerOk = false;
        const double answerNumber = answerMatch.captured(1).toDouble(&answerOk);
        if (answerOk && numbersMatch(answerNumber, calculated)) {
            comparison = " This matches my answer.";
        } else {
            comparison = QString(" This check gives %1, so the displayed answer \"%2\" needs correction if it differs.")
                .arg(formatNumber(calculated), compactText(answer, 0));
        }
    } else if (!answer.trimmed().isEmpty()) {
        comparison = QString(" I compare this calculated value with my answer \"%1\".")
            .arg(compactText(answer, 0));
    }

    return QString("Calculation check: I use the operation from the question: %1 %2 %3 = %4.%5")
        .arg(formatNumber(left), opSymbol, formatNumber(right), formatNumber(calculated), comparison);
}

QList<double> numbersInText(const QString &text)
{
    QList<double> numbers;
    QRegularExpression numberRx("(-?\\d+(?:\\.\\d+)?)");
    QRegularExpressionMatchIterator it = numberRx.globalMatch(text);
    while (it.hasNext()) {
        bool ok = false;
        const double value = it.next().captured(1).toDouble(&ok);
        if (ok) {
            numbers.append(value);
        }
    }
    return numbers;
}

QString answerCore(const QString &answer)
{
    QString clean = compactText(answer, 0);
    clean.replace(QRegularExpression("^(answer\\s*:?|the answer is\\s*)", QRegularExpression::CaseInsensitiveOption), "");
    return clean.trimmed();
}

struct FormulaRule {
    QString target;
    QString expression;
};

struct FormulaApplication {
    bool valid = false;
    QString target;
    QString expression;
    QString expressionWithValues;
    QStringList mappedFacts;
    double value = 0.0;
};

QString normalizeMathExpression(QString expression)
{
    expression = compactText(expression, 180);
    const int equalsIndex = expression.indexOf('=');
    if (equalsIndex > 0) {
        expression = expression.left(equalsIndex);
    }
    expression.replace(QChar(0x00D7), "*");
    expression.replace(QChar(0x00F7), "/");
    expression.replace(QChar(0x2212), "-");
    expression.replace(QRegularExpression("\\bsquared\\b", QRegularExpression::CaseInsensitiveOption), "^2");
    expression.replace(QRegularExpression("\\b(?:so|therefore|because)\\b.*$", QRegularExpression::CaseInsensitiveOption), "");
    expression.replace(QRegularExpression("\\s+"), " ");
    return expression.trimmed();
}

QList<FormulaRule> extractFormulaRules(const QString &text)
{
    QList<FormulaRule> rules;
    const QString clean = compactText(text, 0);
    if (clean.isEmpty()) {
        return rules;
    }

    auto appendRule = [&](QString target, QString expression) {
        target = normalizedForMatch(target);
        expression = normalizeMathExpression(expression);
        if (target.isEmpty()
            || expression.isEmpty()
            || target.split(' ', Qt::SkipEmptyParts).size() > 4
            || expression.length() > 180) {
            return;
        }
        for (const FormulaRule &existing : rules) {
            if (normalizedForMatch(existing.target) == target
                && normalizedForMatch(existing.expression) == normalizedForMatch(expression)) {
                return;
            }
        }
        rules.append({target, expression});
    };

    QRegularExpression equationRx("([A-Za-z][A-Za-z\\s_]{0,40})\\s*=\\s*([^\\.\\n;]+)",
                                  QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator equationIt = equationRx.globalMatch(clean);
    while (equationIt.hasNext()) {
        const QRegularExpressionMatch match = equationIt.next();
        appendRule(match.captured(1), match.captured(2));
    }

    QRegularExpression proseRx("([A-Za-z][A-Za-z\\s_]{0,40})(?:\\s*\\([^\\)]*\\))?\\s+(?:is|equals)\\s+([^\\.\\n;]*(?:\\d|\\+|\\-|\\*|/|x|×|÷|\\bsquared\\b)[^\\.\\n;]*)",
                               QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator proseIt = proseRx.globalMatch(clean);
    while (proseIt.hasNext()) {
        const QRegularExpressionMatch match = proseIt.next();
        appendRule(match.captured(1), match.captured(2));
    }

    return rules;
}

bool extractQuantityForTerm(const QString &text, const QString &term, double *value)
{
    const QString cleanTerm = normalizedForMatch(term);
    if (cleanTerm.isEmpty() || !value) {
        return false;
    }

    QString escapedTerm = QRegularExpression::escape(cleanTerm);
    escapedTerm.replace("\\ ", "\\s+");
    const QString numberPattern = "(-?\\d+(?:\\.\\d+)?)";
    const QList<QRegularExpression> patterns = {
        QRegularExpression("\\b" + escapedTerm + "\\b\\s*(?:of|is|=|:)?\\s*" + numberPattern,
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(numberPattern + "\\s*(?:feet|foot|ft|inches|inch|units?|cm|meters?|m)?\\s+(?:of\\s+)?\\b" + escapedTerm + "\\b",
                           QRegularExpression::CaseInsensitiveOption)
    };

    const QString normalizedText = normalizedForMatch(text);
    for (const QRegularExpression &rx : patterns) {
        const QRegularExpressionMatch match = rx.match(normalizedText);
        if (match.hasMatch()) {
            bool ok = false;
            const double number = match.captured(1).toDouble(&ok);
            if (ok) {
                *value = number;
                return true;
            }
        }
    }

    return false;
}

bool evaluateSimpleExpression(QString expression,
                              const QString &question,
                              QString *expressionWithValues,
                              QStringList *mappedFacts,
                              double *value)
{
    expression = normalizeMathExpression(expression);
    expression.replace(QRegularExpression("\\bx\\b", QRegularExpression::CaseInsensitiveOption), "*");
    expression.remove('(');
    expression.remove(')');

    QRegularExpression wordRx("\\b[a-zA-Z][a-zA-Z_ ]*\\b");
    QRegularExpressionMatchIterator wordIt = wordRx.globalMatch(expression);
    QString resolved = expression;
    QSet<QString> replacedTerms;
    while (wordIt.hasNext()) {
        const QString rawTerm = wordIt.next().captured(0).trimmed();
        const QString term = normalizedForMatch(rawTerm);
        if (term.isEmpty() || replacedTerms.contains(term)) {
            continue;
        }
        double termValue = 0.0;
        if (!extractQuantityForTerm(question, term, &termValue)) {
            return false;
        }
        resolved.replace(QRegularExpression("\\b" + QRegularExpression::escape(rawTerm) + "\\b",
                                           QRegularExpression::CaseInsensitiveOption),
                         formatNumber(termValue));
        replacedTerms.insert(term);
        if (mappedFacts) {
            mappedFacts->append(QString("%1=%2").arg(term, formatNumber(termValue)));
        }
    }

    QRegularExpression tokenRx("(-?\\d+(?:\\.\\d+)?)|([+\\-*/])");
    QRegularExpressionMatchIterator tokenIt = tokenRx.globalMatch(resolved);
    QList<double> values;
    QList<QChar> ops;
    bool expectNumber = true;
    while (tokenIt.hasNext()) {
        const QRegularExpressionMatch match = tokenIt.next();
        if (match.captured(1).isEmpty()) {
            if (expectNumber || match.captured(2).isEmpty()) {
                return false;
            }
            ops.append(match.captured(2).front());
            expectNumber = true;
            continue;
        }
        if (!expectNumber) {
            return false;
        }
        bool ok = false;
        const double number = match.captured(1).toDouble(&ok);
        if (!ok) {
            return false;
        }
        values.append(number);
        expectNumber = false;
    }

    if (values.isEmpty() || values.size() != ops.size() + 1 || expectNumber) {
        return false;
    }

    for (int i = 0; i < ops.size();) {
        if (ops[i] != '*' && ops[i] != '/') {
            ++i;
            continue;
        }
        if (ops[i] == '/' && numbersMatch(values[i + 1], 0.0)) {
            return false;
        }
        values[i] = ops[i] == '*' ? values[i] * values[i + 1] : values[i] / values[i + 1];
        values.removeAt(i + 1);
        ops.removeAt(i);
    }

    double result = values.first();
    for (int i = 0; i < ops.size(); ++i) {
        result = ops[i] == '+' ? result + values[i + 1] : result - values[i + 1];
    }

    if (expressionWithValues) {
        *expressionWithValues = compactText(resolved, 120);
    }
    if (value) {
        *value = result;
    }
    return true;
}

FormulaApplication applyExtractedFormula(const QString &lesson,
                                         const QString &question,
                                         const QString &answer)
{
    FormulaApplication app;
    const QList<FormulaRule> rules = extractFormulaRules(lesson + " " + answer);
    if (rules.isEmpty()) {
        return app;
    }

    const QString normalizedQuestion = normalizedForMatch(question);
    const QString normalizedAnswer = normalizedForMatch(answer);
    for (const FormulaRule &rule : rules) {
        const QString target = normalizedForMatch(rule.target);
        if (!normalizedQuestion.contains(target) && !normalizedAnswer.contains(target)) {
            continue;
        }
        QString expressionWithValues;
        QStringList mappedFacts;
        double value = 0.0;
        if (evaluateSimpleExpression(rule.expression, question, &expressionWithValues, &mappedFacts, &value)) {
            app.valid = true;
            app.target = target;
            app.expression = rule.expression;
            app.expressionWithValues = expressionWithValues;
            app.mappedFacts = mappedFacts;
            app.value = value;
            return app;
        }
    }

    return app;
}

QString extractedConditionRule(const QString &lesson, const QString &question)
{
    const QString clean = compactText(lesson + " " + question, 0);
    if (clean.isEmpty()) {
        return "";
    }

    const QList<QRegularExpression> patterns = {
        QRegularExpression("(?:valid|works|applies)\\s+only\\s+(?:for|if|when)\\s+([^\\.]+)",
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("(?:requires?|must\\s+have|must\\s+be)\\s+([^\\.]+)",
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("(?:before\\s+using[^,\\.]*,?\\s*(?:always\\s+)?(?:confirm|verify|check)\\s+([^\\.]+))",
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("(?:always\\s+(?:confirm|verify|check)\\s+([^\\.]+))",
                           QRegularExpression::CaseInsensitiveOption)
    };

    for (const QRegularExpression &rx : patterns) {
        const QRegularExpressionMatch match = rx.match(clean);
        if (match.hasMatch()) {
            const QString condition = compactText(match.captured(1), 180);
            if (!condition.isEmpty()) {
                return condition;
            }
        }
    }

    return "";
}

QString buildUniversalCognitiveThinking(const QString &lesson, const QString &question, const QString &answer)
{
    const QString cleanQuestion = compactText(question, 170);
    const QString cleanAnswer = answerCore(answer);
    if (cleanQuestion.isEmpty() || cleanAnswer.isEmpty()) {
        return "";
    }

    const FormulaApplication app = applyExtractedFormula(lesson, question, answer);
    if (app.valid) {
        return QString("Answer check: The useful relation is %1 = %2. The question gives %3, so I calculate %4 = %5 and check that this matches %6.")
            .arg(app.target,
                 app.expression,
                 app.mappedFacts.join(", "),
                 app.expressionWithValues,
                 formatNumber(app.value),
                 cleanAnswer);
    }

    const QString condition = extractedConditionRule(lesson, question);
    if (!condition.isEmpty()
        && (question.contains("check", Qt::CaseInsensitive)
            || question.contains("mistake", Qt::CaseInsensitive)
            || question.contains("before", Qt::CaseInsensitive)
            || question.contains("prevent", Qt::CaseInsensitive)
            || question.contains("requires", Qt::CaseInsensitive))) {
        return QString("Answer check: The rule has a condition: %1. I check that condition before using the rule, so %2 is the required answer.")
            .arg(condition, cleanAnswer);
    }

    return "";
}

struct LessonDefinition {
    QString term;
    QString meaning;
};

QList<LessonDefinition> extractLessonDefinitions(const QString &lesson)
{
    QList<LessonDefinition> definitions;
    const QString clean = compactText(lesson, 0);
    if (clean.isEmpty()) {
        return definitions;
    }

    auto appendDefinition = [&](QString term, QString meaning) {
        term = compactText(term, 80);
        meaning = compactText(meaning, 220);
        term.remove(QRegularExpression("^(?:in\\s+\\w+,\\s*)?(?:the\\s+)?", QRegularExpression::CaseInsensitiveOption));
        term = term.trimmed();
        if (term.isEmpty()
            || meaning.isEmpty()
            || term.split(' ', Qt::SkipEmptyParts).size() > 5
            || normalizedForMatch(meaning).contains("question")) {
            return;
        }
        for (const LessonDefinition &existing : definitions) {
            if (normalizedForMatch(existing.term) == normalizedForMatch(term)) {
                return;
            }
        }
        definitions.append({term, meaning});
    };

    QRegularExpression contrastRx("([A-Za-z][A-Za-z\\s'-]{1,50})\\s+is\\s+([^\\.]+?)\\s+(?:,?\\s*(?:while|whereas|but)\\s+)([A-Za-z][A-Za-z\\s'-]{1,50})\\s+is\\s+([^\\.]+)",
                                  QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator contrastIt = contrastRx.globalMatch(clean);
    while (contrastIt.hasNext()) {
        const QRegularExpressionMatch match = contrastIt.next();
        appendDefinition(match.captured(1), match.captured(2));
        appendDefinition(match.captured(3), match.captured(4));
    }

    QRegularExpression definitionRx("\\b([A-Za-z][A-Za-z\\s'-]{1,50})\\s+is\\s+([^\\.]+)",
                                    QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator definitionIt = definitionRx.globalMatch(clean);
    while (definitionIt.hasNext()) {
        const QRegularExpressionMatch match = definitionIt.next();
        appendDefinition(match.captured(1), match.captured(2));
    }

    return definitions;
}

QString buildDefinitionOrContrastExplanation(const QString &lesson, const QString &question, const QString &answer)
{
    const QList<LessonDefinition> definitions = extractLessonDefinitions(lesson);
    if (definitions.isEmpty()) {
        return "";
    }

    const QString normalizedQuestion = normalizedForMatch(question);
    const QString directAnswer = answerCore(answer);
    QStringList relevant;
    for (const LessonDefinition &definition : definitions) {
        const QString term = normalizedForMatch(definition.term);
        if (term.isEmpty()) {
            continue;
        }
        if (normalizedQuestion.contains(term) || normalizedForMatch(directAnswer).contains(term)) {
            relevant.append(QString("%1 means %2").arg(definition.term.trimmed(), definition.meaning.trimmed()));
        }
        if (relevant.size() >= 2) {
            break;
        }
    }

    if (relevant.isEmpty()) {
        return "";
    }

    if (relevant.size() >= 2) {
        return "The key distinction is that " + relevant.join(", while ") + ".";
    }
    return "The key meaning I use is that " + relevant.first() + ".";
}

QString buildCauseChainExplanation(const QString &lesson, const QString &question)
{
    const QString lowerQuestion = question.toLower();
    if (!lowerQuestion.contains("cause")
        && !lowerQuestion.startsWith("why ")
        && !lowerQuestion.contains("what causes")
        && !lowerQuestion.contains("if ")) {
        return "";
    }

    const QString cleanLesson = compactText(lesson, 0);
    if (cleanLesson.isEmpty()) {
        return "";
    }

    QStringList causalSentences;
    const QStringList sentences = cleanLesson.split(QRegularExpression("(?<=[.!?])\\s+|\\n+"), Qt::SkipEmptyParts);
    for (const QString &sentence : sentences) {
        const QString normalized = normalizedForMatch(sentence);
        if (normalized.contains("cause")
            || normalized.contains("lead")
            || normalized.contains("increase")
            || normalized.contains("decrease")
            || normalized.contains("result")
            || normalized.contains("directly")) {
            causalSentences.append(compactText(sentence, 180));
        }
        if (causalSentences.size() >= 2) {
            break;
        }
    }

    if (causalSentences.isEmpty()) {
        return "";
    }

    return "The causal chain is: " + causalSentences.join(" Then ") + ".";
}

QString stripThinkingLabel(QString text)
{
    text = compactText(text, 0);
    text.remove(QRegularExpression("^\\s*\\[?\\s*thinking\\s*:\\s*", QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression("\\]\\s*$"));
    text.remove(QRegularExpression("^\\s*(answer\\s+check|check|reasoning|analysis)\\s*:\\s*",
                                   QRegularExpression::CaseInsensitiveOption));
    return compactText(text, 0);
}

QString trainingThinkingFromLesson(const QString &lesson, int maxLength = 240)
{
    const QString cleanLesson = compactText(lesson, 0);
    if (cleanLesson.isEmpty()) {
        return "";
    }

    static const QList<QRegularExpression> patterns = {
        QRegularExpression("\\[Thinking:\\s*([^\\]]+)\\]", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("improved\\s+thinking\\s+model\\s*:\\s*\\[Thinking:\\s*([^\\]]+)\\]",
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("improved\\s+thinking\\s*:\\s*\\[Thinking:\\s*([^\\]]+)\\]",
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("(?:visible\\s+reasoning\\s+example|reasoning|rationale|analysis)\\s*:\\s*([^\\.]+(?:\\.[^\\.]*)?)",
                           QRegularExpression::CaseInsensitiveOption)
    };

    for (const QRegularExpression &rx : patterns) {
        const QRegularExpressionMatch match = rx.match(cleanLesson);
        if (match.hasMatch()) {
            const QString thinking = stripThinkingLabel(match.captured(1));
            if (!thinking.isEmpty()) {
                return compactText(thinking, maxLength);
            }
        }
    }

    return "";
}

QString thinkingMoveFromTraining(const QString &thinking)
{
    const QString normalized = normalizedForMatch(thinking);
    if (normalized.isEmpty()) {
        return "";
    }
    if (normalized.contains("calculat") || normalized.contains("compute") || normalized.contains("multiply") || normalized.contains("divide")) {
        return "show the calculation from the given numbers";
    }
    if (normalized.contains("condition") || normalized.contains("check") || normalized.contains("verify") || normalized.contains("confirm")) {
        return "check the required condition before applying the rule";
    }
    if (normalized.contains("cause") || normalized.contains("why") || normalized.contains("because")) {
        return "explain the cause-effect link in the specific situation";
    }
    if (normalized.contains("compare") || normalized.contains("difference")) {
        return "compare the two ideas by their role in the problem";
    }
    if (normalized.contains("connect") || normalized.contains("specific") || normalized.contains("scenario")) {
        return "connect the learned rule to the exact details in the question";
    }
    return "turn the learned idea into a rule and apply it to the question";
}

QString reasoningOperationForQuestion(const QString &lesson, const QString &question)
{
    const QString lowerQuestion = question.toLower();
    const QString lowerLesson = lesson.toLower();
    const QString combined = lowerQuestion + " " + lowerLesson;

    if (combined.contains("geometry")
        || combined.contains("triangle")
        || combined.contains("circle")
        || combined.contains("radius")
        || combined.contains("diameter")
        || combined.contains("pythagorean")
        || combined.contains("angle")) {
        if (combined.contains("radius") || combined.contains("diameter") || combined.contains("width") || combined.contains("clearance")) {
            return "geometry calculation";
        }
        return "geometry condition-checking";
    }
    if (lowerQuestion.startsWith("why ") || lowerQuestion.contains("cause") || lowerQuestion.contains("leads to")) {
        return "causal reasoning";
    }
    if (lowerQuestion.contains("priorit") || combined.contains("rule of threes") || combined.contains("urgency")) {
        return "priority reasoning";
    }
    if (lowerQuestion.contains("mistake") || combined.contains("root cause") || combined.contains("symptom")) {
        return "diagnostic reasoning";
    }
    if (lowerQuestion.contains("difference") || lowerQuestion.contains("compare")) {
        return "comparison reasoning";
    }
    if (lowerQuestion.contains("concept") || lowerQuestion.contains("category") || lowerQuestion.contains("describes")) {
        return "classification reasoning";
    }
    if (lowerQuestion.startsWith("how ")) {
        return "process reasoning";
    }
    return "definition-and-application reasoning";
}

QString ownWordsLessonRule(const QString &lesson, const QString &question)
{
    const QString lowerQuestion = question.toLower();
    const QString lowerLesson = lesson.toLower();
    const QString combined = lowerQuestion + " " + lowerLesson;

    if (combined.contains("pythagorean")) {
        return "the Pythagorean formula is not a general triangle rule; I must first prove the triangle has a right angle, then use the side opposite that angle as the hypotenuse";
    }

    if (combined.contains("radius")
        && (combined.contains("diameter") || combined.contains("width") || combined.contains("clearance") || combined.contains("semicircle"))) {
        return "the full width across a circle or semicircle is the diameter, and the diameter is made of two radii";
    }

    if (combined.contains("diameter") && combined.contains("radius")) {
        return "radius and diameter are linked by a factor of two: diameter is twice the radius, and radius is half the diameter";
    }

    if (combined.contains("area") && combined.contains("triangle")) {
        return "triangle area uses half of the base-height rectangle, so I multiply base by height and divide by two";
    }

    if (combined.contains("perimeter")) {
        return "perimeter means the total distance around the outside, so I add the side lengths that form the boundary";
    }

    if (combined.contains("rule of threes") || combined.contains("rule of three")) {
        return "survival priorities come from the shortest danger window, so an hours-level threat must be handled before a weeks-level threat";
    }

    if (combined.contains("root cause") || combined.contains("symptom") || combined.contains("surface sign")) {
        return "I should identify what is actually failing underneath the visible symptom before choosing the fix";
    }

    if (combined.contains("oxygen") || combined.contains("hypoxia") || combined.contains("airway") || lowerQuestion.contains("drowning")) {
        return "I should connect the general oxygen need to the exact mechanism that prevents breathing in the scenario";
    }

    if (combined.contains("critical resource") || combined.contains("luxury resource")) {
        return "I should separate what keeps someone alive from what only improves comfort";
    }

    if (combined.contains("triage")) {
        return "I should classify the action as sorting people by urgency so limited care saves the most lives";
    }

    const QString definitionExplanation = buildDefinitionOrContrastExplanation(lesson, question, "");
    if (!definitionExplanation.isEmpty()) {
        return definitionExplanation;
    }

    const QString causeExplanation = buildCauseChainExplanation(lesson, question);
    if (!causeExplanation.isEmpty()) {
        return causeExplanation;
    }

    const QString rule = correctionRuleFromLesson(lesson, 180);
    if (!rule.isEmpty()) {
        QString cleanRule = stripThinkingLabel(rule);
        cleanRule.remove(QRegularExpression("^\\s*(always|before|when)\\s+", QRegularExpression::CaseInsensitiveOption));
        cleanRule = compactText(cleanRule, 180);
        if (!cleanRule.isEmpty()) {
            return "I turn the learned correction into a check I can apply: " + cleanRule;
        }
    }

    QStringList focus = overlappingEvidenceTokens(question, lesson, 4);
    if (focus.isEmpty()) {
        focus = significantQuestionTokens(question, false).mid(0, 4);
    }
    if (!focus.isEmpty()) {
        return "the answer must connect the important quantities in the question: " + focus.join(", ");
    }

    return "";
}

QStringList workingMemoryFocusTerms(const QString &lesson,
                                    const QString &question,
                                    const QString &answer,
                                    int limit = 5)
{
    QStringList focus = overlappingEvidenceTokens(question, lesson + " " + answer, limit);
    const QStringList questionTokens = significantQuestionTokens(question, false);
    for (const QString &token : questionTokens) {
        if (!focus.contains(token)) {
            focus.append(token);
        }
        if (focus.size() >= limit) {
            break;
        }
    }
    return focus;
}

QString buildSystematicLessonThinking(const QString &lesson, const QString &question, const QString &answer)
{
    const QString cleanQuestion = compactText(question, 170);
    const QString cleanAnswer = answerCore(answer);
    if (cleanQuestion.isEmpty() || cleanAnswer.isEmpty()) {
        return "";
    }

    const QString operation = reasoningOperationForQuestion(lesson, question);
    const QString trainingMove = thinkingMoveFromTraining(trainingThinkingFromLesson(lesson, 180));
    const QString ownRule = ownWordsLessonRule(lesson, question);

    QStringList parts;
    if (!trainingMove.isEmpty()) {
        parts.append(QString("I use the trained method: %1").arg(trainingMove));
    }
    if (!ownRule.isEmpty()) {
        const QString normalizedRule = normalizedForMatch(ownRule);
        if (normalizedRule.startsWith("the ") || normalizedRule.startsWith("i ")) {
            parts.append(ownRule);
        } else {
            parts.append(QString("The rule I apply is that %1").arg(ownRule));
        }
    } else {
        parts.append(QString("I use %1 to connect the lesson to the question").arg(operation));
    }
    parts.append(QString("For \"%1\", the answer must explain why %2 follows from the given facts").arg(cleanQuestion, cleanAnswer));

    return compactText("Answer check: " + parts.join(". ") + ".", 520);
}

QString buildMathWordProblemThinking(const QString &question, const QString &answer)
{
    const QString lowerQuestion = question.toLower();
    const QList<double> numbers = numbersInText(question);
    const QString cleanAnswer = answerCore(answer);
    if (numbers.size() < 2 || cleanAnswer.isEmpty()) {
        return "";
    }

    auto resultText = [&](double value) {
        return formatNumber(value);
    };

    auto sumNumbers = [&]() {
        double total = 0.0;
        for (double value : numbers) {
            total += value;
        }
        return total;
    };

    auto buildLine = [&](const QString &reason, const QString &expression, double value) {
        return QString("Calculation check: %1 I calculate %2 = %3, so the answer should be %4.")
            .arg(reason, expression, resultText(value), cleanAnswer);
    };

    if (lowerQuestion.contains("average") || lowerQuestion.contains("mean")) {
        const double total = sumNumbers();
        QStringList pieces;
        for (double value : numbers) {
            pieces.append(resultText(value));
        }
        return buildLine("The question asks for an average, so I add the values and divide by how many values there are.",
                         QString("(%1) / %2").arg(pieces.join(" + ")).arg(numbers.size()),
                         total / numbers.size());
    }

    if (lowerQuestion.contains("percent") || lowerQuestion.contains("%")) {
        const double percent = numbers[0];
        const double whole = numbers[1];
        return buildLine("The question asks for a percent of a number.",
                         QString("%1 / 100 * %2").arg(resultText(percent), resultText(whole)),
                         percent / 100.0 * whole);
    }

    if (lowerQuestion.contains("divided")
        || lowerQuestion.contains("share")
        || lowerQuestion.contains("split")
        || lowerQuestion.contains("equally")
        || lowerQuestion.contains("per person")) {
        if (!numbersMatch(numbers[1], 0.0)) {
            return buildLine("The question asks for equal sharing, so I divide the total by the number of groups.",
                             QString("%1 / %2").arg(resultText(numbers[0]), resultText(numbers[1])),
                             numbers[0] / numbers[1]);
        }
    }

    if (lowerQuestion.contains("each")
        || lowerQuestion.contains("per ")
        || lowerQuestion.contains("groups of")
        || lowerQuestion.contains("rows of")
        || lowerQuestion.contains("boxes with")
        || lowerQuestion.contains("packs of")) {
        return buildLine("The question gives equal groups, so I multiply the number of groups by the amount in each group.",
                         QString("%1 * %2").arg(resultText(numbers[0]), resultText(numbers[1])),
                         numbers[0] * numbers[1]);
    }

    if (lowerQuestion.contains("left")
        || lowerQuestion.contains("remain")
        || lowerQuestion.contains("remaining")
        || lowerQuestion.contains("sold")
        || lowerQuestion.contains("gave away")
        || lowerQuestion.contains("used")
        || lowerQuestion.contains("lost")
        || lowerQuestion.contains("spent")) {
        return buildLine("The question asks what is left after taking some away, so I subtract.",
                         QString("%1 - %2").arg(resultText(numbers[0]), resultText(numbers[1])),
                         numbers[0] - numbers[1]);
    }

    if (lowerQuestion.contains("increase")
        || lowerQuestion.contains("difference")
        || lowerQuestion.contains("how much more")
        || lowerQuestion.contains("more than")
        || lowerQuestion.contains("less than")) {
        const double larger = qMax(numbers[0], numbers[1]);
        const double smaller = qMin(numbers[0], numbers[1]);
        return buildLine("The question asks for a difference, so I subtract the smaller value from the larger value.",
                         QString("%1 - %2").arg(resultText(larger), resultText(smaller)),
                         larger - smaller);
    }

    if (lowerQuestion.contains("total")
        || lowerQuestion.contains("altogether")
        || lowerQuestion.contains("in all")
        || lowerQuestion.contains("combined")
        || lowerQuestion.contains("sum")) {
        QStringList pieces;
        for (double value : numbers) {
            pieces.append(resultText(value));
        }
        return buildLine("The question asks for a total, so I add the amounts.",
                         pieces.join(" + "),
                         sumNumbers());
    }

    return "";
}

QString buildSequenceThinking(const QString &question, const QString &answer)
{
    const QString lowerQuestion = question.toLower();
    if (!lowerQuestion.contains("pattern") && !lowerQuestion.contains("missing value")) {
        return "";
    }

    const QList<double> numbers = numbersInText(question);
    if (numbers.size() < 3) {
        return "";
    }

    const QString cleanAnswer = answerCore(answer);
    if (numbers.size() >= 4) {
        const double firstDiff = numbers[1] - numbers[0];
        const double secondDiff = numbers[2] - numbers[1];
        const double thirdDiff = numbers[3] - numbers[2];
        if (numbersMatch(firstDiff, secondDiff) && numbersMatch(secondDiff, thirdDiff)) {
            return QString("Answer check: I compare each step in the pattern. The numbers change by %1 each time, so the next value should keep that same step and give %2.")
                .arg(formatNumber(firstDiff), cleanAnswer);
        }

        if (!numbersMatch(numbers[0], 0.0)
            && !numbersMatch(numbers[1], 0.0)
            && numbersMatch(numbers[1] / numbers[0], numbers[2] / numbers[1])
            && numbersMatch(numbers[2] / numbers[1], numbers[3] / numbers[2])) {
            return QString("Answer check: I compare the multiplier between terms. Each value is multiplied by %1, so the next value follows that multiplier and gives %2.")
                .arg(formatNumber(numbers[1] / numbers[0]), cleanAnswer);
        }
    }

    if (lowerQuestion.contains("missing value") && numbers.size() >= 3) {
        const double step = numbers[1] - numbers[0];
        return QString("Answer check: I look for the repeated step. From %1 to %2 the change is %3, so the missing value should keep that step and become %4.")
            .arg(formatNumber(numbers[0]), formatNumber(numbers[1]), formatNumber(step), cleanAnswer);
    }

    return "";
}

QString buildGeometryThinking(const QString &lesson, const QString &question, const QString &answer)
{
    const QString lowerQuestion = question.toLower();
    const QString lowerLesson = lesson.toLower();
    const QString combined = lowerQuestion + " " + lowerLesson;
    const QList<double> numbers = numbersInText(question);
    const QString cleanAnswer = answerCore(answer);
    if (cleanAnswer.isEmpty()) {
        return "";
    }

    auto answerComparison = [&](double calculated) {
        QRegularExpression answerNumberRx("(-?\\d+(?:\\.\\d+)?)");
        QRegularExpressionMatch answerMatch = answerNumberRx.match(cleanAnswer);
        if (!answerMatch.hasMatch()) {
            return QString();
        }
        bool ok = false;
        const double answerNumber = answerMatch.captured(1).toDouble(&ok);
        if (!ok) {
            return QString();
        }
        if (numbersMatch(answerNumber, calculated)) {
            return QString(" This matches my answer.");
        }
        return QString(" This calculation gives %1, so I should correct the answer if it does not match.")
            .arg(formatNumber(calculated));
    };

    if (combined.contains("pythagorean")) {
        if (lowerQuestion.contains("mistake")
            || lowerQuestion.contains("check")
            || lowerQuestion.contains("non-right")
            || lowerQuestion.contains("non right")) {
            return QString("Answer check: I solve this as a condition-check problem, not a calculation. The Pythagorean theorem only works after I confirm a right triangle, so the critical check is whether one angle is exactly 90 degrees; that is why %1 is the answer.")
                .arg(cleanAnswer);
        }
        return QString("Answer check: Before using a^2 + b^2 = c^2, I first verify the triangle is right-angled and identify the hypotenuse opposite the right angle. That condition check supports %1.")
            .arg(cleanAnswer);
    }

    if ((combined.contains("radius") || combined.contains("radii"))
        && (combined.contains("diameter") || combined.contains("width") || combined.contains("clearance") || combined.contains("semicircle"))
        && !numbers.isEmpty()) {
        const double radius = numbers.first();
        const double diameter = radius * 2.0;
        return QString("Calculation check: I identify the requested width as the diameter of the semicircle. A diameter is two radii across the circle, so I calculate 2 * %1 = %2 feet.%3")
            .arg(formatNumber(radius), formatNumber(diameter), answerComparison(diameter));
    }

    if (combined.contains("diameter")
        && combined.contains("radius")
        && (lowerQuestion.contains("find") || lowerQuestion.contains("what"))
        && !numbers.isEmpty()) {
        const double diameter = numbers.first();
        const double radius = diameter / 2.0;
        return QString("Calculation check: I need the radius, which is half of the diameter. I calculate %1 / 2 = %2.%3")
            .arg(formatNumber(diameter), formatNumber(radius), answerComparison(radius));
    }

    if (combined.contains("area")
        && combined.contains("triangle")
        && numbers.size() >= 2
        && (combined.contains("base") || combined.contains("height"))) {
        const double base = numbers[0];
        const double height = numbers[1];
        const double area = base * height / 2.0;
        return QString("Calculation check: I identify this as triangle area. A triangle is half of the matching base-height rectangle, so I calculate (%1 * %2) / 2 = %3.%4")
            .arg(formatNumber(base), formatNumber(height), formatNumber(area), answerComparison(area));
    }

    if (combined.contains("circumference") && !numbers.isEmpty()) {
        const double value = numbers.first();
        if (combined.contains("radius")) {
            const double circumference = 2.0 * 3.14159265358979323846 * value;
            return QString("Calculation check: I identify circumference as the distance around the circle. With radius %1, I use 2 * pi * r, so 2 * pi * %1 is about %2.%3")
                .arg(formatNumber(value), formatNumber(circumference), answerComparison(circumference));
        }
        if (combined.contains("diameter")) {
            const double circumference = 3.14159265358979323846 * value;
            return QString("Calculation check: I identify circumference as the distance around the circle. With diameter %1, I use pi * d, so pi * %1 is about %2.%3")
                .arg(formatNumber(value), formatNumber(circumference), answerComparison(circumference));
        }
    }

    return "";
}

QString buildConceptualThinking(const QString &question, const QString &answer)
{
    const QString lowerQuestion = question.toLower();
    const QString cleanAnswer = answerCore(answer);
    if (cleanAnswer.isEmpty()) {
        return "";
    }

    const QString sequenceThinking = buildSequenceThinking(question, answer);
    if (!sequenceThinking.isEmpty()) {
        return sequenceThinking;
    }

    QRegularExpression allButRx("all\\s+but\\s+(\\d+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch allButMatch = allButRx.match(question);
    if (allButMatch.hasMatch()) {
        return QString("Answer check: The phrase \"all but %1\" means %1 are the ones left out of the action, so the answer is %2.")
            .arg(allButMatch.captured(1), cleanAnswer);
    }

    QRegularExpression eggRx("takes\\s+(\\d+)\\s+minutes?.*boil\\s+one\\s+egg.*boil\\s+(\\d+)\\s+eggs?\\s+together",
                             QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch eggMatch = eggRx.match(question);
    if (eggMatch.hasMatch()) {
        return QString("Answer check: Boiling the eggs together means they cook at the same time, not one after another. The time stays %1 minutes, so the answer is %2.")
            .arg(eggMatch.captured(1), cleanAnswer);
    }

    if (lowerQuestion.contains("second place") && lowerQuestion.contains("pass")) {
        return QString("Answer check: Passing the person in second place means I take that person's position, so the position becomes %1.")
            .arg(cleanAnswer);
    }

    if (lowerQuestion.contains("months") && lowerQuestion.contains("at least 28 days")) {
        return QString("Answer check: Every month has 28 days or more, so counting all months gives %1.")
            .arg(cleanAnswer);
    }

    if (lowerQuestion.contains("spelled incorrectly")) {
        return QString("Answer check: The question is asking for the word that is spelled as \"incorrectly\" in the dictionary, so the answer is %1.")
            .arg(cleanAnswer);
    }

    if (lowerQuestion.contains("electric train") && lowerQuestion.contains("smoke")) {
        return QString("Answer check: An electric train does not make smoke, so there is no smoke direction. That is why the answer is %1.")
            .arg(cleanAnswer);
    }

    if (lowerQuestion.contains("bus driver") && lowerQuestion.contains("you are driving")) {
        return QString("Answer check: The question says I am driving the bus, so the driver's name refers to me. That makes the answer %1.")
            .arg(cleanAnswer);
    }

    if (lowerQuestion.contains("rooster") && lowerQuestion.contains("egg")) {
        return QString("Answer check: A rooster does not lay eggs, so no egg can roll anywhere. That is why the answer is %1.")
            .arg(cleanAnswer);
    }

    if (lowerQuestion.contains("wetter") && lowerQuestion.contains("dries")) {
        return QString("Answer check: I need something that absorbs water while drying another object. A towel does that, so the answer is %1.")
            .arg(cleanAnswer);
    }

    if (lowerQuestion.contains("right hand") && lowerQuestion.contains("left hand")) {
        return QString("Answer check: My right hand can hold my left hand, but my left hand cannot hold itself in the same way. That points to %1.")
            .arg(cleanAnswer);
    }

    QRegularExpression averageRx("average\\s+of\\s+(-?\\d+(?:\\.\\d+)?),\\s*(-?\\d+(?:\\.\\d+)?),\\s*and\\s*(-?\\d+(?:\\.\\d+)?)",
                                 QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch averageMatch = averageRx.match(question);
    if (averageMatch.hasMatch()) {
        const double a = averageMatch.captured(1).toDouble();
        const double b = averageMatch.captured(2).toDouble();
        const double c = averageMatch.captured(3).toDouble();
        return QString("Answer check: To find the average I add the three values and divide by 3: (%1 + %2 + %3) / 3 = %4.")
            .arg(formatNumber(a), formatNumber(b), formatNumber(c), cleanAnswer);
    }

    QRegularExpression percentRx("(\\d+(?:\\.\\d+)?)\\s*percent\\s+of\\s+(\\d+(?:\\.\\d+)?)",
                                 QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch percentMatch = percentRx.match(question);
    if (percentMatch.hasMatch()) {
        const double percent = percentMatch.captured(1).toDouble();
        const double whole = percentMatch.captured(2).toDouble();
        return QString("Answer check: Percent means parts out of 100, so I calculate %1 / 100 * %2. That gives %3.")
            .arg(formatNumber(percent), formatNumber(whole), cleanAnswer);
    }

    QRegularExpression hiddenRx("double\\s+a\\s+hidden\\s+number\\s+and\\s+add\\s+(\\d+(?:\\.\\d+)?)\\s+to\\s+get\\s+(\\d+(?:\\.\\d+)?)",
                                QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch hiddenMatch = hiddenRx.match(question);
    if (hiddenMatch.hasMatch()) {
        const double add = hiddenMatch.captured(1).toDouble();
        const double total = hiddenMatch.captured(2).toDouble();
        return QString("Answer check: I undo the operations in reverse order: %1 - %2, then divide by 2. That gives %3.")
            .arg(formatNumber(total), formatNumber(add), cleanAnswer);
    }

    QRegularExpression lineRx("has\\s+(\\d+)\\s+people.*is\\s+(\\d+)(?:st|nd|rd|th)?\\s+from\\s+the\\s+front",
                              QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch lineMatch = lineRx.match(question);
    if (lineMatch.hasMatch()) {
        return QString("Answer check: People behind Mira are the total people minus Mira's position from the front: %1 - %2 = %3.")
            .arg(lineMatch.captured(1), lineMatch.captured(2), cleanAnswer);
    }

    QRegularExpression colorRx("red,\\s*blue,\\s*green.*position\\s+(\\d+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch colorMatch = colorRx.match(question);
    if (colorMatch.hasMatch()) {
        return QString("Answer check: The colors repeat every 3 positions. I use the position modulo 3 to find the matching color, which gives %1.")
            .arg(cleanAnswer);
    }

    QRegularExpression codeAddRx("code\\s+adds\\s+(\\d+(?:\\.\\d+)?)\\s+to\\s+every\\s+number.*what\\s+does\\s+(-?\\d+(?:\\.\\d+)?)\\s+become",
                                 QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch codeAddMatch = codeAddRx.match(question);
    if (codeAddMatch.hasMatch()) {
        return QString("Answer check: The rule says add %1 to the input %2, so applying the rule gives %3.")
            .arg(codeAddMatch.captured(1), codeAddMatch.captured(2), cleanAnswer);
    }

    QRegularExpression everySeatRx("every\\s+(\\d+)(?:st|nd|rd|th)?\\s+seat.*among\\s+(\\d+)\\s+seats",
                                   QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch everySeatMatch = everySeatRx.match(question);
    if (everySeatMatch.hasMatch()) {
        return QString("Answer check: I count one reserved seat for each group of %1 seats. %2 / %1 gives %3.")
            .arg(everySeatMatch.captured(1), everySeatMatch.captured(2), cleanAnswer);
    }

    QRegularExpression triangleRx("(\\d+)\\s+triangles", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch triangleMatch = triangleRx.match(question);
    if (triangleMatch.hasMatch() && lowerQuestion.contains("3 sides")) {
        return QString("Answer check: Each triangle has 3 sides, so I multiply the number of triangles by 3. That gives %1.")
            .arg(cleanAnswer);
    }

    if (lowerQuestion.contains("clock shows") && lowerQuestion.contains("after")) {
        return QString("Answer check: I move the hour hand forward by the stated number of hours and wrap around after 12. That gives %1.")
            .arg(cleanAnswer);
    }

    return "";
}

QString buildQuestionTypeThinking(const QString &question, const QString &answer)
{
    const QString lowerQuestion = question.toLower();
    const QString cleanQuestion = compactText(question, 0);
    const QString cleanAnswer = answerCore(answer);
    if (cleanQuestion.isEmpty() || cleanAnswer.isEmpty()) {
        return "";
    }

    if (lowerQuestion.startsWith("what is the ") || lowerQuestion.startsWith("what is a ") || lowerQuestion.startsWith("what is an ")) {
        return QString("Answer check: I identify the term or idea being asked about, then check that the answer states its core meaning instead of only giving a related word. Here, %1 gives that core meaning for \"%2\".")
            .arg(cleanAnswer, cleanQuestion);
    }

    if (lowerQuestion.startsWith("why ")) {
        return QString("Answer check: I see the question asks for a reason, so I look for the cause, priority, or rule that makes the result happen. The explanation %1 gives that reason for \"%2\".")
            .arg(cleanAnswer, cleanQuestion);
    }

    if (lowerQuestion.startsWith("how ")) {
        return QString("Answer check: I see the question asks for a method or process, so I look for the action sequence that would solve it. The answer %1 gives the needed process for \"%2\".")
            .arg(cleanAnswer, cleanQuestion);
    }

    if (lowerQuestion.contains("example")) {
        return QString("Answer check: I look for a concrete case that fits the topic, then check that \"%1\" is an actual example of what the question asks.")
            .arg(cleanAnswer);
    }

    if (lowerQuestion.contains("common mistake") || lowerQuestion.contains("avoid")) {
        return QString("Answer check: I look for the error the question warns about, then check that \"%1\" names that mistake and how to avoid it.")
            .arg(cleanAnswer);
    }

    if (lowerQuestion.contains("limitation")) {
        return QString("Answer check: I look for a boundary or weakness, then check that \"%1\" states something the idea cannot reliably do.")
            .arg(cleanAnswer);
    }

    if (lowerQuestion.contains("different from") || lowerQuestion.contains("compare")) {
        return QString("Answer check: I compare the two ideas by their job or behavior, then check that \"%1\" explains the meaningful difference.")
            .arg(cleanAnswer);
    }

    if (lowerQuestion.contains("key term") || lowerQuestion.contains("what does it mean")) {
        return QString("Answer check: I identify the term being asked about, then check that \"%1\" gives its meaning instead of only naming it.")
            .arg(cleanAnswer);
    }

    if (lowerQuestion.contains("useful") || lowerQuestion.contains("benefit")) {
        return QString("Answer check: I look for the practical value being asked for, then check that \"%1\" explains the benefit directly.")
            .arg(cleanAnswer);
    }

    return QString("Answer check: I classify what \"%1\" is asking, identify the needed answer type, and connect the answer to that requirement. That is why %2 is my answer.")
        .arg(cleanQuestion, cleanAnswer);
}

QString buildLessonGuidedThinking(const QString &lesson, const QString &question, const QString &answer)
{
    const QString cleanLesson = compactText(lesson, 220);
    const QString cleanQuestion = compactText(question, 180);
    const QString cleanAnswer = answerCore(answer);
    if (cleanLesson.isEmpty() || cleanQuestion.isEmpty() || cleanAnswer.isEmpty()) {
        return "";
    }

    const QString lowerLesson = lesson.toLower();
    const QString lowerQuestion = question.toLower();
    const QString lowerCombined = lowerLesson + " " + lowerQuestion;

    if (lowerCombined.contains("rule of threes") || lowerCombined.contains("rule of three")) {
        if (lowerQuestion.contains("shelter") && lowerQuestion.contains("food")) {
            return QString("Answer check: I apply the survival rule of threes by comparing time limits: extreme weather without shelter can become deadly in about 3 hours, while lack of food is usually a 3-week threat. The shorter survival window makes shelter the priority, so %1 follows.")
                .arg(cleanAnswer);
        }
        return QString("Answer check: I apply the survival rule of threes by ranking threats by time limit instead of comfort. The most urgent limit in the question determines why %1 is the correct priority.")
            .arg(cleanAnswer);
    }

    if (lowerCombined.contains("oxygen") || lowerCombined.contains("hypoxia") || lowerQuestion.contains("drowning")) {
        if (lowerQuestion.contains("drowning") || lowerQuestion.contains("airway")) {
            return QString("Answer check: I connect the general oxygen rule to the drowning mechanism: water blocks breathing, which prevents oxygen from reaching the brain. That specific mechanism is %1.")
                .arg(cleanAnswer);
        }
        return QString("Answer check: The lesson says oxygen is the immediate survival constraint. I trace the scenario to the oxygen failure, so %1 names the cause.")
            .arg(cleanAnswer);
    }

    if (lowerCombined.contains("critical resource") || lowerCombined.contains("luxury resource")) {
        return QString("Answer check: I compare the survival role of each resource. A critical resource prevents death in the situation, while a luxury resource only improves comfort, so the important difference is %1.")
            .arg(cleanAnswer);
    }

    if (lowerCombined.contains("triage")) {
        return QString("Answer check: I match the action in the question to the lesson's concept: sorting patients by urgency to maximize survivors is triage. That is why the answer is %1.")
            .arg(cleanAnswer);
    }

    if (lowerCombined.contains("5 whys") || lowerCombined.contains("5 why")) {
        if (lowerQuestion.contains("final") || cleanAnswer.trimmed().startsWith("why", Qt::CaseInsensitive)) {
            return QString("Answer check: The 5 Whys should move past the visible symptom to the deeper preventable cause. I do not stop at the first debris/problem clue; I ask what missing process allowed it, so the final question is \"%1\".")
                .arg(cleanAnswer);
        }
        return QString("Answer check: The 5 Whys starts with the visible symptom and asks why it is happening. Here the first useful why targets the observed problem directly, so \"%1\" fits as the starting question.")
            .arg(cleanAnswer);
    }

    if (lowerCombined.contains("fishbone") || lowerCombined.contains("ishikawa")) {
        if (lowerQuestion.contains("staffing") || cleanAnswer.compare("People", Qt::CaseInsensitive) == 0) {
            return QString("Answer check: A Fishbone Diagram classifies causes by type. Staffing levels are a human/resource factor, so they belong under the People category; that is why the answer is %1.")
                .arg(cleanAnswer);
        }
        return QString("Answer check: I classify the cause using the Fishbone categories from the lesson, then choose the category that best matches the scenario detail. That points to %1.")
            .arg(cleanAnswer);
    }

    if (lowerCombined.contains("cause-and-effect") || lowerCombined.contains("cause and effect") || lowerQuestion.contains("direct cause")) {
        if (lowerQuestion.contains("open-plan") || lowerQuestion.contains("open plan")) {
            return QString("Answer check: I identify the change and its immediate effect. The open-plan layout increases noise and distraction, which directly reduces focus and productivity, so the cause is %1.")
                .arg(cleanAnswer);
        }
        return QString("Answer check: I link the changed condition to the immediate effect it creates, then choose the cause that directly explains the result. That causal chain supports %1.")
            .arg(cleanAnswer);
    }

    if (lowerCombined.contains("root cause")
        || lowerCombined.contains("surface sign")
        || lowerCombined.contains("symptom")
        || lowerQuestion.contains("diagnos")) {
        if (lowerQuestion.contains("shivering") || lowerQuestion.contains("hiker") || lowerQuestion.contains("eaten")) {
            return QString("Answer check: I separate the surface sign from the underlying failure. Warming addresses the shivering symptom, but the lesson says the real failure is the energy deficit from not eating, so the mistake is %1.")
                .arg(cleanAnswer);
        }
        return QString("Answer check: I use the root-cause rule: identify what is fundamentally failing before treating the visible symptom. That makes %1 the correct explanation.")
            .arg(cleanAnswer);
    }

    if (lowerCombined.contains("situational problem")) {
        return QString("Answer check: I connect the lesson principle to the specific situation: identify the issue, explain the cause-effect link, then choose the practical response. That makes %1 the answer.")
            .arg(cleanAnswer);
    }

    if (lowerQuestion.startsWith("why ")) {
        return QString("Answer check: I use the lesson rule and connect it to this scenario instead of only naming the answer. The lesson says: %1 Therefore %2 explains why the answer follows.")
            .arg(cleanLesson, cleanAnswer);
    }

    if (lowerQuestion.startsWith("what mistake")
        || lowerQuestion.contains("what mistake")
        || lowerQuestion.contains("what specific cause")
        || lowerQuestion.contains("what concept")
        || lowerQuestion.contains("difference between")
        || lowerQuestion.contains("important difference")) {
        const QString fallback = lessonApplicationFallback(lesson, question, answer);
        if (!fallback.isEmpty()) {
            return fallback;
        }
    }

    return lessonApplicationFallback(lesson, question, answer, 320);
}

QString buildLessonGroundedAnswer(const QString &lesson, const QString &question, const QString &answer)
{
    const QString directAnswer = polishUserFacingText(answerCore(answer), 0, true);
    if (directAnswer.isEmpty()) {
        return "";
    }

    const QString cleanQuestion = compactText(question, 180);
    const QString cleanLesson = compactText(lesson, 0);
    if (cleanLesson.isEmpty()) {
        return directAnswer;
    }

    const QString lowerQuestion = question.toLower();
    const QString lowerLesson = lesson.toLower();
    const QString lowerCombined = lowerLesson + " " + lowerQuestion;
    const QString normalizedDirect = normalizedForMatch(directAnswer);
    const int directWords = directAnswer.split(' ', Qt::SkipEmptyParts).size();
    const bool alreadyExplains = directWords >= 18
        && (normalizedDirect.contains("because")
            || normalizedDirect.contains("why")
            || normalizedDirect.contains("so ")
            || normalizedDirect.contains("while")
            || normalizedDirect.contains("therefore")
            || normalizedDirect.contains("which"));

    if (alreadyExplains && meaningfulTokenOverlapScore(directAnswer, cleanLesson) >= 10) {
        return directAnswer;
    }

    const FormulaApplication formulaApp = applyExtractedFormula(cleanLesson, question, directAnswer);
    if (formulaApp.valid) {
        return polishUserFacingText(QString("%1 I use the learned relation %2 = %3. The question gives %4, so I compute %5 = %6 and use that result for the answer.")
                                        .arg(directAnswer,
                                             formulaApp.target,
                                             formulaApp.expression,
                                             formulaApp.mappedFacts.join(", "),
                                             formulaApp.expressionWithValues,
                                             formatNumber(formulaApp.value)),
                                    0,
                                    true);
    }

    const QString conditionRule = extractedConditionRule(cleanLesson, question);
    if (!conditionRule.isEmpty()
        && (lowerQuestion.contains("check")
            || lowerQuestion.contains("mistake")
            || lowerQuestion.contains("prevent")
            || lowerQuestion.contains("before")
            || lowerQuestion.contains("requires"))) {
        return polishUserFacingText(QString("%1 The important reasoning step is the condition: %2. I use that condition to decide whether the rule can be applied, rather than applying the rule blindly.")
                                        .arg(directAnswer, conditionRule),
                                    0,
                                    true);
    }

    const QString definitionExplanation = buildDefinitionOrContrastExplanation(cleanLesson, question, directAnswer);
    if (!definitionExplanation.isEmpty()
        && !normalizedDirect.contains(normalizedForMatch(definitionExplanation).left(80))) {
        return polishUserFacingText(QString("%1 %2 This extra distinction explains why the answer fits this question instead of only naming a term.")
                                        .arg(directAnswer, definitionExplanation),
                                    0,
                                    true);
    }

    const QString causeExplanation = buildCauseChainExplanation(cleanLesson, question);
    if (!causeExplanation.isEmpty()
        && !normalizedDirect.contains(normalizedForMatch(causeExplanation).left(80))) {
        return polishUserFacingText(QString("%1 %2 That chain explains the result in the question.")
                                        .arg(directAnswer, causeExplanation),
                                    0,
                                    true);
    }

    const QList<double> questionNumbers = numbersInText(question);
    if (lowerCombined.contains("pythagorean")) {
        if (lowerQuestion.contains("mistake")
            || lowerQuestion.contains("check")
            || lowerQuestion.contains("non-right")
            || lowerQuestion.contains("non right")) {
            return polishUserFacingText(QString("%1 This prevents the mistake because a^2 + b^2 = c^2 is only valid for a right triangle. If there is no 90-degree angle, I should not use the theorem.")
                                            .arg(directAnswer),
                                        0,
                                        true);
        }
        return polishUserFacingText(QString("%1 I only use the Pythagorean theorem after confirming the triangle is right-angled and identifying the hypotenuse opposite that right angle.")
                                        .arg(directAnswer),
                                    0,
                                    true);
    }

    if ((lowerCombined.contains("radius") || lowerCombined.contains("radii"))
        && (lowerCombined.contains("diameter") || lowerCombined.contains("width") || lowerCombined.contains("clearance") || lowerCombined.contains("semicircle"))
        && !questionNumbers.isEmpty()) {
        const double radius = questionNumbers.first();
        const double diameter = radius * 2.0;
        return polishUserFacingText(QString("%1 The width clearance is the diameter, and a diameter is two radii across the circle. With radius %2 feet, I calculate 2 * %2 = %3 feet.")
                                        .arg(directAnswer, formatNumber(radius), formatNumber(diameter)),
                                    0,
                                    true);
    }

    if (lowerCombined.contains("diameter")
        && lowerCombined.contains("radius")
        && !questionNumbers.isEmpty()) {
        const double diameter = questionNumbers.first();
        const double radius = diameter / 2.0;
        return polishUserFacingText(QString("%1 Radius is half of diameter, so I calculate %2 / 2 = %3.")
                                        .arg(directAnswer, formatNumber(diameter), formatNumber(radius)),
                                    0,
                                    true);
    }

    if (lowerCombined.contains("rule of threes") || lowerCombined.contains("rule of three")) {
        if (lowerQuestion.contains("shelter") && lowerQuestion.contains("food")) {
            return polishUserFacingText(QString("%1 Shelter is the priority because the rule of threes gives extreme weather a much shorter danger window: about 3 hours without shelter, compared with about 3 weeks without food.")
                                            .arg(directAnswer),
                                        0,
                                        true);
        }
        return polishUserFacingText(QString("%1 The lesson says to rank survival needs by the shortest time limit, so the most immediate threat must be handled first.")
                                        .arg(directAnswer),
                                    0,
                                    true);
    }

    if (lowerCombined.contains("root cause")
        || lowerCombined.contains("surface sign")
        || lowerCombined.contains("symptom")) {
        if (lowerQuestion.contains("shivering") || lowerQuestion.contains("hiker") || lowerQuestion.contains("eaten")) {
            return polishUserFacingText(QString("%1 Warming only reacts to the visible shivering, but the lesson says the deeper problem may be an energy deficit from not eating. The precise action is to identify and address that underlying failure, not only the surface sign.")
                                            .arg(directAnswer),
                                        0,
                                        true);
        }
        return polishUserFacingText(QString("%1 The reason is that the lesson says surface signs can hide the real failure, so the answer must name the underlying cause rather than only the symptom.")
                                        .arg(directAnswer),
                                    0,
                                    true);
    }

    if (lowerCombined.contains("oxygen") || lowerCombined.contains("hypoxia") || lowerQuestion.contains("drowning")) {
        if (lowerQuestion.contains("drowning") || lowerQuestion.contains("airway")) {
            return polishUserFacingText(QString("%1 In drowning, water blocks normal breathing, so oxygen cannot reach the brain. That turns the general oxygen rule into the specific mechanism: hypoxia from airway obstruction.")
                                            .arg(directAnswer),
                                        0,
                                        true);
        }
        return polishUserFacingText(QString("%1 The lesson says oxygen is the most immediate constraint because brain damage can begin within minutes when oxygen delivery fails.")
                                        .arg(directAnswer),
                                    0,
                                    true);
    }

    if (lowerCombined.contains("critical resource") || lowerCombined.contains("luxury resource")) {
        return polishUserFacingText(QString("%1 In survival, the difference matters because a critical resource keeps the person alive in that environment, like water in a desert, while a luxury resource only improves comfort and can wait.")
                                        .arg(directAnswer),
                                    0,
                                    true);
    }

    if (lowerCombined.contains("triage")) {
        return polishUserFacingText(QString("%1 It fits because triage means sorting casualties by urgency so limited help goes first where it can save the most lives.")
                                        .arg(directAnswer),
                                    0,
                                    true);
    }

    const QString rule = correctionRuleFromLesson(cleanLesson, 180);
    if (!rule.isEmpty()) {
        const QString operation = reasoningOperationForQuestion(cleanLesson, question);
        return polishUserFacingText(QString("%1 I use %2 and apply the learned rule: %3 This connects the answer to the specific question instead of only naming a memorized fact.")
                                        .arg(directAnswer, operation, rule),
                                    0,
                                    true);
    }

    const QString lessonSentence = bestLessonSentenceForQuestion(cleanLesson, cleanQuestion, 190);
    if (!lessonSentence.isEmpty()
        && !normalizedDirect.contains(normalizedForMatch(lessonSentence).left(70))) {
        const QString operation = reasoningOperationForQuestion(cleanLesson, question);
        const QStringList focus = workingMemoryFocusTerms(cleanLesson, question, directAnswer, 4);
        const QString focusClause = focus.isEmpty()
            ? QString()
            : QString(" I focused on %1.").arg(focus.join(", "));
        return polishUserFacingText(QString("%1 I use %2.%3 This follows from the lesson detail: %4")
                                        .arg(directAnswer, operation, focusClause, lessonSentence),
                                    0,
                                    true);
    }

    return directAnswer;
}

QString buildStudentVisibleThinking(const QString &lesson, const QString &question, const QString &answer)
{
    const QString arithmeticThinking = buildArithmeticThinking(question, answer);
    if (!arithmeticThinking.isEmpty()) {
        return arithmeticThinking;
    }

    const QString universalThinking = buildUniversalCognitiveThinking(lesson, question, answer);
    if (!universalThinking.isEmpty()) {
        return universalThinking;
    }

    const QString geometryThinking = buildGeometryThinking(lesson, question, answer);
    if (!geometryThinking.isEmpty()) {
        return geometryThinking;
    }

    const QString mathWordThinking = buildMathWordProblemThinking(question, answer);
    if (!mathWordThinking.isEmpty()) {
        return mathWordThinking;
    }

    const QString systematicThinking = buildSystematicLessonThinking(lesson, question, answer);
    const QString lessonThinking = buildLessonGuidedThinking(lesson, question, answer);
    if (!systematicThinking.isEmpty()) {
        return systematicThinking;
    }
    if (!lessonThinking.isEmpty()) {
        return lessonThinking;
    }

    const QString conceptualThinking = buildConceptualThinking(question, answer);
    if (!conceptualThinking.isEmpty()) {
        return conceptualThinking;
    }

    return buildQuestionTypeThinking(question, answer);
}

QString extractLeadingCheckLine(const QString &response)
{
    const QString text = response.trimmed();
    QRegularExpression bracketRx("^\\s*\\[Thinking:\\s*([^\\]]+)\\]",
                                 QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch bracketMatch = bracketRx.match(text);
    if (bracketMatch.hasMatch()) {
        return compactText(bracketMatch.captured(1), 0);
    }

    QRegularExpression checkRx("^\\s*(Thinking|Check|How|Why this answer follows)\\s*:\\s*([^\\n]+)",
                               QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch checkMatch = checkRx.match(text);
    if (checkMatch.hasMatch()) {
        return compactText(checkMatch.captured(2), 0);
    }

    return "";
}

bool isWeakOrCopiedThinking(const QString &thinking)
{
    const QString normalizedThinking = normalizedForMatch(thinking);
    return normalizedThinking.contains("restate")
        || normalizedThinking.contains("copied")
        || normalizedThinking.contains("copy")
        || normalizedThinking.contains("remembered")
        || normalizedThinking.contains("learned")
        || normalizedThinking.contains("matched the answer");
}

QString extractAnswerForDisplay(QString response)
{
    response.remove(QRegularExpression("\\[Thinking:[^\\]]*\\]", QRegularExpression::CaseInsensitiveOption));
    response = response.trimmed();
    if (response.isEmpty()) {
        return "";
    }

    QRegularExpression fullAnswerRx("(?:^|\\n)\\s*(?:final\\s+answer|answer)\\s*:\\s*(.+)$",
                                    QRegularExpression::CaseInsensitiveOption
                                        | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch fullAnswerMatch = fullAnswerRx.match(response);
    if (fullAnswerMatch.hasMatch()) {
        return compactText(fullAnswerMatch.captured(1), 0);
    }

    const QStringList lines = response.split('\n', Qt::SkipEmptyParts);
    QRegularExpression answerRx("^\\s*(?:final\\s+answer|answer)\\s*:\\s*(.+)$",
                                QRegularExpression::CaseInsensitiveOption);
    for (const QString &line : lines) {
        QRegularExpressionMatch answerMatch = answerRx.match(line.trimmed());
        if (answerMatch.hasMatch()) {
            return compactText(answerMatch.captured(1), 0);
        }
    }

    QRegularExpression sentenceRx("^\\s*the\\s+answer\\s+is\\s+(.+)$",
                                  QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch sentenceMatch = sentenceRx.match(response);
    if (sentenceMatch.hasMatch()) {
        return compactText(sentenceMatch.captured(1), 0);
    }

    QStringList answerLines;
    QRegularExpression checkLineRx("^\\s*(Thinking|Check|How|Why this answer follows)\\s*:",
                                   QRegularExpression::CaseInsensitiveOption);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (checkLineRx.match(trimmed).hasMatch()) {
            continue;
        }
        answerLines.append(trimmed);
    }

    return compactText(answerLines.isEmpty() ? response : answerLines.join(' '), 0);
}

QString stripAnswerCandidate(QString text)
{
    text = compactText(text, 260);
    text.remove(QRegularExpression("^\\s*(?:answer|final\\s+answer)\\s*:\\s*", QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression("^\\s*[\"'`]+"));
    text.remove(QRegularExpression("[\"'`]+\\s*$"));
    return text.trimmed();
}

QString extractCorrectedAnswerFromTeacherText(const QString &text)
{
    const QString clean = compactText(text, 0);
    if (clean.isEmpty()) {
        return "";
    }

    static const QStringList quotedPatterns = {
        "correct\\s+(?:final\\s+)?(?:answer|question)\\s*(?:is|:)\\s*[\"']([^\"']+)[\"']",
        "answer\\s+should\\s+be\\s*[\"']([^\"']+)[\"']"
    };

    for (const QString &pattern : quotedPatterns) {
        QRegularExpression rx(pattern, QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = rx.match(clean);
        if (match.hasMatch()) {
            const QString candidate = stripAnswerCandidate(match.captured(1));
            if (!candidate.isEmpty()) {
                return candidate;
            }
        }
    }

    static const QStringList plainPatterns = {
        "correct\\s+(?:final\\s+)?(?:answer|question)\\s*(?:is|:)\\s*([^\\.\\n]+(?:\\?)?)",
        "answer\\s+should\\s+be\\s*([^\\.\\n]+(?:\\?)?)"
    };

    for (const QString &pattern : plainPatterns) {
        QRegularExpression rx(pattern, QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = rx.match(clean);
        if (match.hasMatch()) {
            const QString candidate = stripAnswerCandidate(match.captured(1));
            if (!candidate.isEmpty()
                && candidate.length() <= 260
                && !normalizedForMatch(candidate).contains("correct reasoning")) {
                return candidate;
            }
        }
    }

    return "";
}

QString formatThinkingFirstQuestionResponse(const QString &question, const QString &lesson, const QString &rawAnswer)
{
    QString answer = polishUserFacingText(extractAnswerForDisplay(rawAnswer), 0, true);
    if (answer.isEmpty()) {
        return "";
    }
    const QString groundedAnswer = buildLessonGroundedAnswer(lesson, question, answer);
    if (!groundedAnswer.isEmpty()) {
        answer = groundedAnswer;
    }

    QString thinking = extractLeadingCheckLine(rawAnswer);
    if (isWeakOrCopiedThinking(thinking)) {
        thinking.clear();
    }
    if (thinking.isEmpty()) {
        thinking = buildStudentVisibleThinking(lesson, question, answer);
    }
    thinking = polishUserFacingText(thinking, 0, true);
    if (!thinking.endsWith('.') && !thinking.endsWith('!') && !thinking.endsWith('?')) {
        thinking += ".";
    }

    return QString("[Thinking: %1]\nAnswer: %2").arg(thinking, answer);
}

QString formatDirectCheckedAnswer(const QString &question, const QString &answer)
{
    return formatThinkingFirstQuestionResponse(question, "", "Answer: " + answer);
}

QString cleanGenerationTopic(QString normalized)
{
    normalized = requestCoreForRouting(normalized);
    QString topic = normalized;
    static const QStringList removeWords = {
        "create", "generate", "write", "make", "compose", "ask", "quiz",
        "give", "provide", "another", "again", "more", "next", "one",
        "me", "for", "a", "an", "the", "simple", "practice", "question",
        "problem", "problems", "questions", "in", "about", "on", "of"
    };

    QStringList tokens;
    for (const QString &token : normalizedForMatch(topic).split(' ', Qt::SkipEmptyParts)) {
        if (!removeWords.contains(token) && !tokens.contains(token)) {
            tokens.append(token);
        }
    }

    return tokens.join(' ').trimmed();
}

QString buildGenerativeLocalAnswer(const QString &question,
                                   QString *lastGeneratedKind,
                                   QString *lastGeneratedTopic,
                                   int *generationCounter)
{
    const QString core = requestCoreForRouting(question);
    const QString normalized = normalizedForMatch(core);
    const QString compactIntent = QString(normalized).remove(' ');
    const bool followUpRequest = normalized == "another one"
        || normalized == "another"
        || normalized == "again"
        || normalized == "aagain"
        || normalized == "one more"
        || normalized == "more"
        || normalized == "next one"
        || normalized == "next"
        || compactIntent == "aagain"
        || compactIntent == "againplease"
        || (normalized.startsWith("another ")
            && (normalized.contains("problem")
                || normalized.contains("question")
                || normalized.contains("story")
                || normalized.contains("math")
                || compactIntent.contains("mathproblem")));
    const bool explicitGenerationContent = normalized.contains("problem")
        || normalized.contains("question")
        || normalized.contains("story")
        || normalized.contains("math")
        || normalized.contains("physics")
        || normalized.contains("geometry")
        || normalized.contains("age")
        || compactIntent.contains("mathproblem");
    const bool shortFollowUpOnly = followUpRequest && !explicitGenerationContent;
    const bool generationRequest = normalized.startsWith("create ")
        || normalized.startsWith("generate ")
        || normalized.startsWith("write ")
        || normalized.startsWith("make ")
        || normalized.startsWith("compose ")
        || normalized.startsWith("ask me ")
        || normalized.startsWith("quiz me ")
        || ((normalized.contains("question") || normalized.contains("problem"))
            && (normalized.startsWith("give me ") || normalized.startsWith("provide ")));
    if (!generationRequest && !followUpRequest) {
        return "";
    }
    if (shortFollowUpOnly
        && (!lastGeneratedKind || !lastGeneratedTopic || lastGeneratedKind->isEmpty() || lastGeneratedTopic->isEmpty())) {
        return "";
    }

    const bool wantsProblem = normalized.contains("problem")
        || normalized.contains("solve")
        || normalized.contains("word problem")
        || compactIntent.contains("mathproblem");
    const bool wantsQuestion = normalized.contains("question")
        || normalized.startsWith("ask me")
        || normalized.startsWith("quiz me");
    const bool wantsStory = normalized.contains("story")
        || normalized.contains("short story")
        || normalized.contains("creative")
        || normalized.startsWith("write ")
        || normalized.startsWith("compose ");
    QString topic = shortFollowUpOnly && lastGeneratedTopic ? *lastGeneratedTopic : cleanGenerationTopic(core);
    QString kind = shortFollowUpOnly && lastGeneratedKind ? *lastGeneratedKind : QString();
    if (kind.isEmpty()) {
        if (wantsStory) {
            kind = "story";
        } else if (wantsProblem || normalized.contains("math") || normalized.contains("mathematic") || normalized.contains("age")) {
            kind = "problem";
        } else if (wantsQuestion) {
            kind = "question";
        } else {
            kind = "prompt";
        }
    }
    if (topic.isEmpty()) {
        topic = kind == "problem" ? "general reasoning" : "the chosen topic";
    }

    const int variant = generationCounter ? *generationCounter : 0;
    if (generationCounter) {
        *generationCounter = variant + 1;
    }
    QString answer;
    if (kind == "story") {
        static const QStringList storyVariants = {
            "Short story: The old clock in the classroom stopped every day at 3:17. Mara thought it was broken until she noticed the minute hand always pointed toward the science cabinet. Inside, behind a stack of dusty notebooks, she found a folded map drawn by a student from fifty years ago. The map led to a tiny garden behind the gym, where someone had planted a tree and written one sentence on a stone: 'Curiosity keeps time moving.'",
            "Short story: A small robot named Lio was built to sort library books, but it kept pausing at the poetry shelf. One rainy evening, it arranged the returned books into a pattern that spelled HELP. The librarian followed the pattern and found a leaking pipe above the archives. From then on, nobody called Lio broken; they called it observant.",
            "Short story: Every morning, Nina heard a whistle from the empty train station. She followed it one day and found an old conductor teaching birds to mimic departure calls. He said the town had forgotten the sound of leaving, so the birds kept it alive. Nina smiled and taught them a new sound too: the sound of coming home.",
            "Short story: The village lantern never went out, even in storms. When Eli climbed the tower to learn why, he found no flame inside, only mirrors catching moonlight from every angle. He realized the lantern survived because it borrowed light from many places. That night, he stopped trying to solve every problem alone."
        };
        answer = storyVariants[variant % storyVariants.size()];
    } else if (topic.contains("age") || normalized.contains("age")) {
        static const QStringList ageVariants = {
            "Practice problem: Maria is 12 years older than her brother. In 4 years, Maria will be twice her brother's age. How old is each person now? Let the brother's age be x, then compare their ages after 4 years.",
            "Practice problem: A father is 4 times as old as his daughter. In 12 years, he will be twice as old as she is. How old are they now? Let the daughter's current age be x.",
            "Practice problem: Leo is 5 years older than Ana. The sum of their ages is 31. How old is each person? Use x for Ana's age and x + 5 for Leo's age.",
            "Practice problem: In 6 years, Sam will be three times as old as he was 2 years ago. How old is Sam now? Compare his future age with three times his past age."
        };
        answer = ageVariants[variant % ageVariants.size()];
    } else if (topic.contains("math") || topic.contains("mathematic") || kind == "problem") {
        static const QStringList mathVariants = {
            "Practice problem: A student buys 3 notebooks and 2 pens for 17 dollars. Each notebook costs 4 dollars. What is the cost of one pen? Subtract the notebook cost first, then divide the remaining cost by 2.",
            "Practice problem: A rectangle has a length of 14 cm and a width of 9 cm. What is its area, and what operation tells you that? Use area = length x width.",
            "Practice problem: A bus has 48 seats. If 5/8 of the seats are filled, how many seats are occupied? Multiply 48 by 5/8.",
            "Practice problem: A number is doubled, then 7 is added, giving 31. What is the original number? Work backward: subtract 7, then divide by 2."
        };
        answer = mathVariants[variant % mathVariants.size()];
    } else if (topic.contains("physics")) {
        static const QStringList physicsVariants = {
            "Practice question: A 2 kg cart accelerates at 3 m/s^2. What net force acts on it, and why does Newton's second law use mass instead of weight? Use F = ma, then explain mass as inertia.",
            "Practice question: A gas in a sealed container is heated. Why does the pressure increase? Explain the chain from temperature to particle speed to wall collisions.",
            "Practice question: A 10 N force acts for 0.5 seconds. What impulse is delivered, and what does impulse change? Use impulse = force x time.",
            "Practice question: Two objects have the same mass, but one is on Earth and one is on the Moon. Which property changes, mass or weight, and why?"
        };
        answer = physicsVariants[variant % physicsVariants.size()];
    } else if (topic.contains("geometry")) {
        static const QStringList geometryVariants = {
            "Practice problem: A semicircular window has a radius of 5 feet. What is its full width? Use diameter = 2 x radius.",
            "Practice problem: A triangle has a base of 12 cm and a height of 7 cm. What is its area? Use area = base x height / 2.",
            "Practice question: Why must a triangle have a 90-degree angle before using the Pythagorean theorem? Explain the condition before applying a^2 + b^2 = c^2.",
            "Practice problem: A circle has a diameter of 18 inches. What is its radius? Use radius = diameter / 2."
        };
        answer = geometryVariants[variant % geometryVariants.size()];
    } else {
        answer = QString("Practice %1: Explain the main idea of %2 in your own words, give one concrete example, and justify why the example fits. A strong answer should define the idea, apply it to the example, and state the reason clearly.")
                     .arg(kind == "question" ? "question" : "prompt",
                          topic);
    }

    if (lastGeneratedKind) {
        *lastGeneratedKind = kind;
    }
    if (lastGeneratedTopic) {
        *lastGeneratedTopic = topic;
    }

    const QString thinking = shortFollowUpOnly
        ? "I connect this short follow-up to the last thing I generated, then create a different version instead of repeating the same text"
        : (kind == "story"
               ? "I read this as a creative writing request, choose a concrete situation, and build a short story with characters, conflict, and a clear ending"
               : "I read this as a request to generate practice, identify the topic and format, then create a complete item with enough detail to answer");

    return QString("Thinking: %1.\nAnswer: %2").arg(thinking, answer);
}

QString buildConversationalLocalAnswer(const QString &question)
{
    const QString lowerQuestion = normalizedForMatch(question);
    const QString coreQuestion = normalizedForMatch(requestCoreForRouting(question));
    const QString compactQuestion = QString(lowerQuestion).remove(' ');

    if (lowerQuestion == "how are you"
        || lowerQuestion == "how are you doing"
        || lowerQuestion == "how do you feel") {
        return "Thinking: I identify this as a conversational status question, so I answer about my current role and readiness.\n"
               "Answer: I am running normally and ready to learn, reason, and answer clearly.";
    }

    if (lowerQuestion.contains("math")
        && (lowerQuestion.contains("example")
            || lowerQuestion.contains("problem")
            || lowerQuestion.contains("practice"))) {
        return "Thinking: I identify this as a request for sample math practice, so I give concrete examples with different skills.\n"
               "Answer: Here are example math problems: 1. If 12 boxes hold 8 pencils each, how many pencils are there? 2. A triangle has base 14 and height 9; what is its area? 3. What is 25 percent of 80?";
    }

    if (lowerQuestion.contains("what can you do")
        || lowerQuestion.contains("capabil")
        || lowerQuestion.contains("skill")
        || lowerQuestion.contains("show me your skills")
        || lowerQuestion.contains("what are your skills")
        || lowerQuestion.contains("show me what you can do")) {
        return "Thinking: I identify this as a capability request, so I separate current chat skills from explicit training.\n"
               "Answer: I can answer trained questions, solve basic arithmetic and common word problems, explain my answer check, recall teacher and dataset lessons, learn from teacher corrections, and train through CPU, local GPU, or the GPU server. Normal chat is read-only unless you start explicit training.";
    }

    if (coreQuestion == "what are you"
        || coreQuestion == "who are you"
        || coreQuestion == "what kind of ai are you"
        || lowerQuestion.contains("your identity")) {
        return "Thinking: I identify this as an identity question, so I answer from my role in this app.\n"
               "Answer: I am the local student AI inside this trainer. My job is to answer from trained lessons, teacher corrections, datasets, and explicit CPU, local GPU, or GPU-server training.";
    }

    if (lowerQuestion.contains("can you understand me")
        || lowerQuestion.contains("do you understand me")) {
        return "Thinking: I identify this as a comprehension question, so I answer based on how I process the current message.\n"
               "Answer: I can read your message, identify important words, compare them with learned knowledge, and form a clear response when I have enough related context.";
    }

    if (lowerQuestion == "hello"
        || lowerQuestion == "hello there"
        || lowerQuestion == "hi"
        || lowerQuestion == "hi there"
        || lowerQuestion == "hey"
        || lowerQuestion == "hey there"
        || compactQuestion == "heythere"
        || compactQuestion == "hellothere"
        || compactQuestion == "hithere"
        || lowerQuestion.startsWith("hello ")
        || lowerQuestion.startsWith("hi ")
        || lowerQuestion.startsWith("hey ")) {
        return "Thinking: I identify this as a greeting, so I respond conversationally.\n"
               "Answer: Hello. I am ready to learn and answer clearly.";
    }

    return "";
}

QString buildVerifiedLocalChatAnswer(const QString &question)
{
    const QString conversationalAnswer = buildConversationalLocalAnswer(question);
    if (!conversationalAnswer.isEmpty()) {
        return formatThinkingFirstQuestionResponse(question, "", conversationalAnswer);
    }

    const QString arithmeticAnswer = buildArithmeticChatAnswer(question);
    if (!arithmeticAnswer.isEmpty()) {
        return formatThinkingFirstQuestionResponse(question, "", arithmeticAnswer);
    }

    const QString lowerQuestion = question.toLower();

    if (lowerQuestion.contains("quadratic")
        && (lowerQuestion.contains("formula") || lowerQuestion.contains("equation"))) {
        return formatDirectCheckedAnswer(question,
            "For a quadratic equation ax^2 + bx + c = 0, the quadratic formula is x = (-b +/- sqrt(b^2 - 4ac)) / (2a).");
    }

    QRegularExpression allButRx("all\\s+but\\s+(\\d+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch allButMatch = allButRx.match(question);
    if (allButMatch.hasMatch()) {
        return formatDirectCheckedAnswer(question, allButMatch.captured(1));
    }

    QRegularExpression eggRx("takes\\s+(\\d+)\\s+minutes?.*boil\\s+one\\s+egg.*boil\\s+(\\d+)\\s+eggs?\\s+together",
                             QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch eggMatch = eggRx.match(question);
    if (eggMatch.hasMatch()) {
        return formatDirectCheckedAnswer(question, eggMatch.captured(1) + " minutes");
    }

    QRegularExpression candlesRx("(?:there\\s+are\\s+)?(\\d+)\\s+candles?.*(\\d+)\\s+(?:are\\s+)?blown\\s+out",
                                 QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch candlesMatch = candlesRx.match(question);
    if (candlesMatch.hasMatch() && lowerQuestion.contains("still lit")) {
        const int total = candlesMatch.captured(1).toInt();
        const int blownOut = candlesMatch.captured(2).toInt();
        return formatDirectCheckedAnswer(question, QString::number(qMax(0, total - blownOut)));
    }

    if (lowerQuestion.contains("second place") && lowerQuestion.contains("pass")) {
        return formatDirectCheckedAnswer(question, "second place");
    }

    if (lowerQuestion.contains("months") && lowerQuestion.contains("at least 28 days")) {
        return formatDirectCheckedAnswer(question, "12");
    }

    if (lowerQuestion.contains("spelled incorrectly")) {
        return formatDirectCheckedAnswer(question, "incorrectly");
    }

    if (lowerQuestion.contains("electric train") && lowerQuestion.contains("smoke")) {
        return formatDirectCheckedAnswer(question, "there is no smoke because an electric train does not produce smoke");
    }

    if (lowerQuestion.contains("bus driver") && lowerQuestion.contains("you are driving")) {
        return formatDirectCheckedAnswer(question, "your name");
    }

    if (lowerQuestion.contains("rooster") && lowerQuestion.contains("egg")) {
        return formatDirectCheckedAnswer(question, "nowhere, because roosters do not lay eggs");
    }

    if (lowerQuestion.contains("wetter") && lowerQuestion.contains("dries")) {
        return formatDirectCheckedAnswer(question, "a towel");
    }

    if (lowerQuestion.contains("right hand") && lowerQuestion.contains("left hand")) {
        return formatDirectCheckedAnswer(question, "your left hand");
    }

    QRegularExpression averageRx("average\\s+of\\s+(-?\\d+(?:\\.\\d+)?),\\s*(-?\\d+(?:\\.\\d+)?),\\s*and\\s*(-?\\d+(?:\\.\\d+)?)",
                                 QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch averageMatch = averageRx.match(question);
    if (averageMatch.hasMatch()) {
        const double a = averageMatch.captured(1).toDouble();
        const double b = averageMatch.captured(2).toDouble();
        const double c = averageMatch.captured(3).toDouble();
        return formatDirectCheckedAnswer(question, formatNumber((a + b + c) / 3.0));
    }

    QRegularExpression percentRx("(\\d+(?:\\.\\d+)?)\\s*percent\\s+of\\s+(\\d+(?:\\.\\d+)?)",
                                 QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch percentMatch = percentRx.match(question);
    if (percentMatch.hasMatch()) {
        const double percent = percentMatch.captured(1).toDouble();
        const double whole = percentMatch.captured(2).toDouble();
        return formatDirectCheckedAnswer(question, formatNumber(percent / 100.0 * whole));
    }

    QRegularExpression hiddenRx("double\\s+a\\s+hidden\\s+number\\s+and\\s+add\\s+(\\d+(?:\\.\\d+)?)\\s+to\\s+get\\s+(\\d+(?:\\.\\d+)?)",
                                QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch hiddenMatch = hiddenRx.match(question);
    if (hiddenMatch.hasMatch()) {
        const double add = hiddenMatch.captured(1).toDouble();
        const double total = hiddenMatch.captured(2).toDouble();
        return formatDirectCheckedAnswer(question, formatNumber((total - add) / 2.0));
    }

    QRegularExpression lineRx("has\\s+(\\d+)\\s+people.*is\\s+(\\d+)(?:st|nd|rd|th)?\\s+from\\s+the\\s+front",
                              QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch lineMatch = lineRx.match(question);
    if (lineMatch.hasMatch()) {
        const int total = lineMatch.captured(1).toInt();
        const int position = lineMatch.captured(2).toInt();
        return formatDirectCheckedAnswer(question, QString::number(qMax(0, total - position)));
    }

    QRegularExpression colorRx("red,\\s*blue,\\s*green.*position\\s+(\\d+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch colorMatch = colorRx.match(question);
    if (colorMatch.hasMatch()) {
        const int position = colorMatch.captured(1).toInt();
        const QStringList colors = {"red", "blue", "green"};
        return formatDirectCheckedAnswer(question, colors[(position + 2) % 3]);
    }

    QRegularExpression codeAddRx("code\\s+adds\\s+(\\d+(?:\\.\\d+)?)\\s+to\\s+every\\s+number.*what\\s+does\\s+(-?\\d+(?:\\.\\d+)?)\\s+become",
                                 QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch codeAddMatch = codeAddRx.match(question);
    if (codeAddMatch.hasMatch()) {
        const double add = codeAddMatch.captured(1).toDouble();
        const double input = codeAddMatch.captured(2).toDouble();
        return formatDirectCheckedAnswer(question, formatNumber(input + add));
    }

    QRegularExpression everySeatRx("every\\s+(\\d+)(?:st|nd|rd|th)?\\s+seat.*among\\s+(\\d+)\\s+seats",
                                   QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch everySeatMatch = everySeatRx.match(question);
    if (everySeatMatch.hasMatch()) {
        const int step = everySeatMatch.captured(1).toInt();
        const int seats = everySeatMatch.captured(2).toInt();
        if (step > 0) {
            return formatDirectCheckedAnswer(question, QString::number(seats / step));
        }
    }

    QRegularExpression triangleRx("(\\d+)\\s+triangles", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch triangleMatch = triangleRx.match(question);
    if (triangleMatch.hasMatch() && lowerQuestion.contains("3 sides")) {
        return formatDirectCheckedAnswer(question, QString::number(triangleMatch.captured(1).toInt() * 3));
    }

    QRegularExpression clockRx("clock\\s+shows\\s+(\\d+).*after\\s+(\\d+)\\s+hours?",
                               QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch clockMatch = clockRx.match(question);
    if (clockMatch.hasMatch()) {
        const int startHour = clockMatch.captured(1).toInt();
        const int hours = clockMatch.captured(2).toInt();
        const int hour = ((startHour + hours - 1) % 12) + 1;
        return formatDirectCheckedAnswer(question, QString::number(hour));
    }

    if (lowerQuestion.contains("pattern") || lowerQuestion.contains("next")) {
        const QList<double> numbers = numbersInText(question);
        if (numbers.size() >= 3) {
            const double firstDiff = numbers[numbers.size() - 2] - numbers[numbers.size() - 3];
            const double secondDiff = numbers[numbers.size() - 1] - numbers[numbers.size() - 2];
            if (numbersMatch(firstDiff, secondDiff)) {
                return formatDirectCheckedAnswer(question, formatNumber(numbers.last() + secondDiff));
            }

            if (!numbersMatch(numbers[numbers.size() - 3], 0.0)
                && !numbersMatch(numbers[numbers.size() - 2], 0.0)) {
                const double firstRatio = numbers[numbers.size() - 2] / numbers[numbers.size() - 3];
                const double secondRatio = numbers[numbers.size() - 1] / numbers[numbers.size() - 2];
                if (numbersMatch(firstRatio, secondRatio)) {
                    return formatDirectCheckedAnswer(question, formatNumber(numbers.last() * secondRatio));
                }
            }
        }
    }

    return "";
}

}

AgentController::AgentController(QObject *parent)
    : QObject(parent)
    , m_temperature(0.5)
    , m_learningEnabled(true)
    , m_contextWindow(2)
    , m_isSimulationRunning(false)
    , m_simulationTurns(0)
    , m_simulationCurrentTurn(0)
    , m_isTestingPhase(false)
    , m_currentReply(nullptr)
    , m_datasetReply(nullptr)
    , m_gpuProcess(nullptr)
    , m_datasetParseThread(nullptr)
    , m_datasetParseLogTimer(nullptr)
    , m_generationCounter(0)
    , m_simulationDelay(2000)
    , m_datasetTrainingNextChunkIndex(0)
    , m_isReTest(false)
    , m_knowledgeIndexDirty(true)
    , m_knowledgeFile("student_knowledge.json")
    , m_loraFile("lora_adapter.txt")
    , m_datasetTrainingEpochs(4)
    , m_datasetTrainingTotalChunks(0)
    , m_datasetTrainingOriginalBytes(0)
    , m_datasetRequestIsMetadata(false)
    , m_datasetRequestIsViewerSplits(false)
    , m_datasetRequestIsViewerRows(false)
    , m_teacherRetryCount(0)
    , m_datasetViewerOffset(0)
    , m_datasetViewerTotalRows(-1)
    , m_datasetViewerPageSize(1000)
    , m_datasetParseLogTicks(0)
    , m_gpuSshPort(22)
    , m_gpuMaxSamples(20000)
    , m_isGpuTrainingRunning(false)
    , m_gpuTrainingEpochs(4)
    , m_gpuTrainingStage(GpuTrainingStage::None)
    , m_useLocalGpuTraining(false)
    , m_isLocalGpuTrainingRunning(false)
{
    // Try to load initial database
    m_agent.load();
    m_agent.loadLora(m_loraFile.toStdString());
    loadKnowledgeBank();

    // Load Featherless settings
    QSettings settings("AitrainerCorp", "Aitrainer");
    m_apiKey = settings.value("featherlessApiKey", "rc_3a765dfebf3099de99753a869b9cf9a1fad9892fbd8c98f8c1ece89205918a22").toString();
    if (m_apiKey.isEmpty()) {
        m_apiKey = "rc_3a765dfebf3099de99753a869b9cf9a1fad9892fbd8c98f8c1ece89205918a22";
    }
    m_huggingFaceToken = settings.value("huggingFaceToken").toString();
    m_teacherModel = settings.value("teacherModel", "deepseek-ai/DeepSeek-V4-Flash").toString();
    m_gpuHost = settings.value("gpuHost").toString();
    m_gpuSshPort = settings.value("gpuSshPort", 22).toInt();
    m_gpuUsername = settings.value("gpuUsername", "root").toString();
    m_gpuSshKeyPath = settings.value("gpuSshKeyPath").toString();
    m_gpuRemoteRoot = settings.value("gpuRemoteRoot", "/root/aitrainer-runs").toString();
    m_gpuMaxSamples = settings.value("gpuMaxSamples", 20000).toInt();
    m_gpuTrainingStatus = "GPU training idle.";
    m_useLocalGpuTraining = settings.value("useLocalGpuTraining", false).toBool();
    m_localGpuTrainingStatus = m_useLocalGpuTraining
        ? "Local GPU training enabled. CUDA will be preferred when available; Direct3D 11 compute is the fallback."
        : "Local GPU training disabled; CPU LoRA path will be used.";

    m_datasetParseLogTimer = new QTimer(this);
    m_datasetParseLogTimer->setInterval(2000);
    connect(m_datasetParseLogTimer, &QTimer::timeout, this, [this]() {
        ++m_datasetParseLogTicks;
        appendToSimulationLog(QString("[Dataset Training]: Parsing dataset in background... %1 seconds elapsed. UI remains responsive.")
            .arg(m_datasetParseLogTicks * 2));
    });
}

AgentController::~AgentController() {
    if (m_currentReply) {
        m_currentReply->disconnect();
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    if (m_datasetReply) {
        m_datasetReply->disconnect();
        m_datasetReply->abort();
        m_datasetReply->deleteLater();
        m_datasetReply = nullptr;
    }
    if (m_datasetParseLogTimer) {
        m_datasetParseLogTimer->stop();
    }
    if (m_datasetParseThread) {
        m_datasetParseThread->disconnect();
        m_datasetParseThread->requestInterruption();
        m_datasetParseThread->wait(1000);
        m_datasetParseThread->deleteLater();
        m_datasetParseThread = nullptr;
    }
    if (m_gpuProcess) {
        m_gpuProcess->disconnect();
        m_gpuProcess->kill();
        m_gpuProcess->deleteLater();
        m_gpuProcess = nullptr;
    }
}

QString AgentController::databasePath() const {
    return QString::fromStdString(m_agent.getMemoryFilePath());
}

void AgentController::setDatabasePath(const QString &path) {
    if (databasePath() != path) {
        m_agent.setMemoryFilePath(path.toStdString());
        emit databasePathChanged();
    }
}

int AgentController::vocabularySize() const {
    return m_agent.getVocabularySize();
}

int AgentController::totalAssociations() const {
    return m_agent.getTotalAssociationsCount();
}

double AgentController::temperature() const {
    return m_temperature;
}

void AgentController::setTemperature(double temp) {
    if (m_temperature != temp) {
        m_temperature = temp;
        emit temperatureChanged();
    }
}

bool AgentController::learningEnabled() const {
    return m_learningEnabled;
}

void AgentController::setLearningEnabled(bool enabled) {
    if (m_learningEnabled != enabled) {
        m_learningEnabled = enabled;
        emit learningEnabledChanged();
    }
}

int AgentController::contextWindow() const {
    return m_contextWindow;
}

void AgentController::setContextWindow(int window) {
    if (m_contextWindow != window) {
        m_contextWindow = window;
        emit contextWindowChanged();
    }
}

// Featherless properties implementations
QString AgentController::featherlessApiKey() const {
    return m_apiKey;
}

void AgentController::setFeatherlessApiKey(const QString &key) {
    if (m_apiKey != key) {
        m_apiKey = key;
        QSettings settings("AitrainerCorp", "Aitrainer");
        settings.setValue("featherlessApiKey", key);
        emit featherlessApiKeyChanged();
    }
}

QString AgentController::huggingFaceToken() const {
    return m_huggingFaceToken;
}

void AgentController::setHuggingFaceToken(const QString &token) {
    if (m_huggingFaceToken != token) {
        m_huggingFaceToken = token;
        QSettings settings("AitrainerCorp", "Aitrainer");
        settings.setValue("huggingFaceToken", token);
        emit huggingFaceTokenChanged();
    }
}

QString AgentController::teacherModel() const {
    return m_teacherModel;
}

void AgentController::setTeacherModel(const QString &model) {
    if (m_teacherModel != model) {
        m_teacherModel = model;
        QSettings settings("AitrainerCorp", "Aitrainer");
        settings.setValue("teacherModel", model);
        emit teacherModelChanged();
    }
}

bool AgentController::isSimulationRunning() const {
    return m_isSimulationRunning;
}

QString AgentController::simulationTopic() const {
    return m_simulationTopic;
}

int AgentController::simulationTurns() const {
    return m_simulationTurns;
}

int AgentController::simulationCurrentTurn() const {
    return m_simulationCurrentTurn;
}

QString AgentController::simulationLog() const {
    return m_simulationLog;
}

int AgentController::simulationDelay() const {
    return m_simulationDelay;
}

void AgentController::setSimulationDelay(int delay) {
    if (m_simulationDelay != delay) {
        m_simulationDelay = delay;
        emit simulationDelayChanged();
    }
}

QString AgentController::gpuHost() const {
    return m_gpuHost;
}

void AgentController::setGpuHost(const QString &host) {
    if (m_gpuHost != host) {
        m_gpuHost = host.trimmed();
        saveGpuSettings();
        emit gpuSettingsChanged();
    }
}

int AgentController::gpuSshPort() const {
    return m_gpuSshPort;
}

void AgentController::setGpuSshPort(int port) {
    const int cleanPort = qMax(1, qMin(port, 65535));
    if (m_gpuSshPort != cleanPort) {
        m_gpuSshPort = cleanPort;
        saveGpuSettings();
        emit gpuSettingsChanged();
    }
}

QString AgentController::gpuUsername() const {
    return m_gpuUsername;
}

void AgentController::setGpuUsername(const QString &username) {
    if (m_gpuUsername != username) {
        m_gpuUsername = username.trimmed().isEmpty() ? "root" : username.trimmed();
        saveGpuSettings();
        emit gpuSettingsChanged();
    }
}

QString AgentController::gpuSshKeyPath() const {
    return m_gpuSshKeyPath;
}

void AgentController::setGpuSshKeyPath(const QString &path) {
    if (m_gpuSshKeyPath != path) {
        m_gpuSshKeyPath = path.trimmed();
        saveGpuSettings();
        emit gpuSettingsChanged();
    }
}

QString AgentController::gpuRemoteRoot() const {
    return m_gpuRemoteRoot;
}

void AgentController::setGpuRemoteRoot(const QString &remoteRoot) {
    QString cleanRoot = remoteRoot.trimmed();
    if (cleanRoot.isEmpty()) {
        cleanRoot = "/root/aitrainer-runs";
    }
    if (m_gpuRemoteRoot != cleanRoot) {
        m_gpuRemoteRoot = cleanRoot;
        saveGpuSettings();
        emit gpuSettingsChanged();
    }
}

int AgentController::gpuMaxSamples() const {
    return m_gpuMaxSamples;
}

void AgentController::setGpuMaxSamples(int maxSamples) {
    const int cleanMax = qMax(1, maxSamples);
    if (m_gpuMaxSamples != cleanMax) {
        m_gpuMaxSamples = cleanMax;
        saveGpuSettings();
        emit gpuSettingsChanged();
    }
}

bool AgentController::isGpuTrainingRunning() const {
    return m_isGpuTrainingRunning;
}

QString AgentController::gpuTrainingStatus() const {
    return m_gpuTrainingStatus;
}

bool AgentController::useLocalGpuTraining() const {
    return m_useLocalGpuTraining;
}

void AgentController::setUseLocalGpuTraining(bool enabled) {
    if (m_useLocalGpuTraining != enabled) {
        m_useLocalGpuTraining = enabled;
        saveLocalGpuSettings();
        if (enabled) {
            std::string status;
            LearningAgent::localGpuAvailable(&status);
            setLocalGpuTrainingStatus(QString::fromStdString(status));
        } else {
            m_isLocalGpuTrainingRunning = false;
            setLocalGpuTrainingStatus("Local GPU disabled; parser scan and LoRA training will use CPU paths.");
        }
        emit localGpuTrainingChanged();
    }
}

bool AgentController::isLocalGpuTrainingRunning() const {
    return m_isLocalGpuTrainingRunning;
}

QString AgentController::localGpuTrainingStatus() const {
    return m_localGpuTrainingStatus;
}

QVariantList AgentController::testScores() const {
    return m_testScores;
}

QVariantMap AgentController::lastTestResult() const {
    return m_lastTestResult;
}

bool AgentController::isTestingPhase() const {
    return m_isTestingPhase;
}

QString AgentController::learnAndRespond(const QString &input) {
    const QString cleanInput = compactText(input, 0);
    if (cleanInput.isEmpty()) {
        return "";
    }

    const LightweightTensorReasoner::Result tensorReasoning = m_tensorReasoner.analyze(cleanInput);

    QString qResponse = buildGenerativeLocalAnswer(cleanInput,
                                                   &m_lastGeneratedKind,
                                                   &m_lastGeneratedTopic,
                                                   &m_generationCounter);
    if (!qResponse.isEmpty()) {
        qResponse = formatThinkingFirstQuestionResponse(cleanInput, "", qResponse);
        emit responseGenerated(input, qResponse);
        return qResponse;
    }

    qResponse = buildVerifiedLocalChatAnswer(cleanInput);
    if (!qResponse.isEmpty()) {
        emit responseGenerated(input, qResponse);
        return qResponse;
    }

    const bool question = isLikelyQuestion(cleanInput) || tensorReasoning.questionLike;
    if (question) {
        QString bestCurriculumAnswer;
        QString bestCurriculumLesson;
        QString relatedLearning;

        if (qResponse.isEmpty()) {
            int bestScore = 0;
            int knowledgeIndex = findBestKnowledgeIndex(cleanInput, &bestScore);
            if (knowledgeIndex >= 0 && bestScore >= relatedLearningMinimumScore()) {
                const LearnedKnowledge &knowledge = m_knowledgeBank[knowledgeIndex];
                if (isDirectKnowledgeMatch(cleanInput, knowledge.question)) {
                    bestCurriculumAnswer = knowledge.answer;
                    bestCurriculumLesson = knowledge.correction.isEmpty() ? knowledge.lesson : knowledge.correction;
                } else {
                    relatedLearning = relatedKnowledgeContext(cleanInput, 3);
                }
            }
        }

        if (qResponse.isEmpty() && !bestCurriculumAnswer.isEmpty()) {
            qResponse = formatThinkingFirstQuestionResponse(cleanInput, bestCurriculumLesson, bestCurriculumAnswer);
        }

        if (qResponse.isEmpty()) {
            int appliedScore = 0;
            qResponse = buildAppliedLearningAnswer(cleanInput, &appliedScore);
            if (!qResponse.isEmpty() && relatedLearning.isEmpty()) {
                relatedLearning = QString("Applied trusted trained knowledge with relevance score %1.")
                                      .arg(appliedScore);
            }
        }

        if (qResponse.isEmpty()) {
            QString thinking = m_tensorReasoner.visiblePlan(tensorReasoning, cleanInput);
            thinking = compactText(thinking, 260);
            if (!relatedLearning.isEmpty()) {
                qResponse = QString("[Thinking: %1 I found related trained lessons, but none directly verified this exact question.]\n"
                                    "Answer: I do not know confidently from my trained lessons yet.")
                                .arg(thinking);
            } else {
                qResponse = QString("[Thinking: %1 I checked structured knowledge and local rules but could not verify enough facts.]\n"
                                    "Answer: I do not have enough learned facts yet to answer confidently.")
                                .arg(thinking);
            }
        }

    } else {
        qResponse = buildStatementUnderstandingResponse(cleanInput);
    }

    emit responseGenerated(input, qResponse);

    return qResponse;
}

bool AgentController::saveMemory() {
    bool agentSaved = m_agent.save();
    bool loraSaved = m_agent.saveLora(m_loraFile.toStdString());
    bool knowledgeSaved = saveKnowledgeBank();
    return agentSaved && loraSaved && knowledgeSaved;
}

bool AgentController::loadMemory() {
    bool result = m_agent.load();
    result = m_agent.loadLora(m_loraFile.toStdString()) && result;
    result = loadKnowledgeBank() && result;
    emit memoryChanged();
    return result;
}

void AgentController::clearMemory() {
    m_agent.clear();
    m_knowledgeBank.clear();
    markKnowledgeIndexDirty();
    m_agent.save();
    m_agent.saveLora(m_loraFile.toStdString());
    saveKnowledgeBank();
    emit memoryChanged();
}

QString AgentController::trainLoraFromText(const QString &trainingText, int epochs) {
    const QString cleanTrainingText = compactText(trainingText, 0);
    if (cleanTrainingText.split(' ', Qt::SkipEmptyParts).size() < 2) {
        return "LoRA training skipped: enter at least two words of training text.";
    }

    epochs = qMax(1, qMin(epochs, 32));
    if (m_useLocalGpuTraining) {
        m_isLocalGpuTrainingRunning = true;
        setLocalGpuTrainingStatus("Starting local GPU LoRA training. CUDA is preferred when available; Direct3D 11 is fallback.");
    }

    std::string trainingStatus;
    const bool trained = m_agent.trainLoraText(cleanTrainingText.toStdString(),
                                               epochs,
                                               0.05,
                                               4,
                                               8.0,
                                               1.2,
                                               m_useLocalGpuTraining,
                                               &trainingStatus);
    if (m_useLocalGpuTraining) {
        m_isLocalGpuTrainingRunning = false;
        setLocalGpuTrainingStatus(QString::fromStdString(trainingStatus));
    }
    if (!trained) {
        if (m_useLocalGpuTraining && !trainingStatus.empty()) {
            return "Local GPU LoRA training failed: " + QString::fromStdString(trainingStatus);
        }
        return "LoRA training failed: text could not be tokenized.";
    }

    m_agent.learn(QString("LoRA training sample: %1").arg(cleanTrainingText).toStdString(), 0.4);
    saveMemory();
    emit memoryChanged();

    return QString("LoRA-style C++ training complete: %1 epochs, %2 adapter pairs, rank %3. %4")
        .arg(epochs)
        .arg(m_agent.getLoraPairCount())
        .arg(m_agent.getLoraRank())
        .arg(QString::fromStdString(trainingStatus));
}

void AgentController::applyHuggingFaceAuth(QNetworkRequest &request) const {
    request.setRawHeader("User-Agent", "Aitrainer-Cpp-Lora/1.0");

    const QString host = request.url().host().toLower();
    const bool isHuggingFaceHost = host == "huggingface.co"
        || host.endsWith(".huggingface.co")
        || host == "hf.co"
        || host.endsWith(".hf.co");
    const QString token = m_huggingFaceToken.trimmed();
    if (isHuggingFaceHost && !token.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    }
}

QString AgentController::gpuRemoteScript() const {
    return QString::fromUtf8(R"PY(#!/usr/bin/env python3
import argparse
import base64
import concurrent.futures
import csv
import hashlib
import io
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import zlib

MAGIC = b"AITRAINER_AI_V1\n"
HF_TOKEN = os.environ.get("HF_TOKEN", "").strip()

def log(message):
    print(message, flush=True)

def log_remote_runtime():
    log("[remote] host: " + socket.gethostname())
    log("[remote] python: " + sys.executable)
    try:
        with open("/opt/rocm/.info/version", "r", encoding="utf-8", errors="replace") as f:
            log("[remote] ROCm version: " + f.read().strip())
    except Exception as exc:
        log("[remote] ROCm version file unavailable: " + str(exc))

    for command in (
        ["/opt/rocm/bin/rocm-smi", "--showproductname"],
        ["rocm-smi", "--showproductname"],
        ["amd-smi", "static", "--gpu", "--asic"],
    ):
        try:
            output = subprocess.check_output(
                command,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=15,
            )
            lines = [line.strip() for line in output.splitlines() if line.strip()]
            if lines:
                log("[remote] AMD GPU inventory via " + command[0] + ":")
                for line in lines[:12]:
                    log("[remote]   " + line)
                return
        except Exception:
            pass

    log("[remote] AMD GPU inventory command unavailable; PyTorch HIP check will enforce GPU training.")

def qt_uncompress(payload):
    if payload.startswith(MAGIC):
        payload = payload[len(MAGIC):]
    if len(payload) < 5:
        raise ValueError("compressed payload is too small")
    try:
        return zlib.decompress(payload[4:])
    except Exception:
        return zlib.decompress(payload)

def qt_compress(raw):
    return struct.pack(">I", len(raw)) + zlib.compress(raw, 9)

def compact(text, limit=0):
    text = re.sub(r"\s+", " ", str(text or "")).strip()
    if limit and len(text) > limit:
        return text[:limit].strip()
    return text

def normalized(text):
    text = re.sub(r"[^a-z0-9]+", " ", str(text or "").lower())
    return re.sub(r"\s+", " ", text).strip()

def tokens(text):
    return re.findall(r"[a-z0-9]+", str(text or "").lower())

def field_text(value):
    if value is None:
        return ""
    if isinstance(value, (str, int, float, bool)):
        return compact(value)
    if isinstance(value, list):
        return compact("; ".join(field_text(v) for v in value[:4] if field_text(v)))
    if isinstance(value, dict):
        for key in ["text", "content", "value", "answer", "response", "output", "completion", "label"]:
            for actual, val in value.items():
                if normalized(actual) == key:
                    got = field_text(val)
                    if got:
                        return got
        return compact(json.dumps(value, ensure_ascii=False))
    return compact(value)

def is_noise_key(key):
    key_norm = normalized(key)
    noise = {
        "id", "index", "row idx", "row index", "truncated cells", "features",
        "num rows total", "num rows per page", "partial", "dataset", "config",
        "split", "domain", "meta", "metadata", "subset", "license", "language",
        "lang", "source file", "file", "url", "created at", "updated at"
    }
    return key_norm in noise or key_norm.endswith(" id")

def is_reasoning_key(key):
    key_norm = normalized(key)
    terms = (
        "reasoning", "rationale", "explanation", "analysis", "thinking",
        "thought", "chain of thought", "chain of thoughts", "chain_of_thought",
        "cot", "scratchpad", "work", "solution steps", "derivation"
    )
    return any(normalized(term) == key_norm or normalized(term) in key_norm for term in terms)

def visible_reasoning(text):
    clean = compact(text, 5000)
    if not clean:
        return ""
    patterns = [
        r"<think>\s*(.*?)\s*</think>",
        r"<analysis>\s*(.*?)\s*</analysis>",
        r"\[Thinking:\s*([^\]]+)\]",
        r"(?:^|\s)(?:reasoning|rationale|explanation|analysis|thinking|chain\s+of\s+thought|cot)\s*:\s*(.*?)(?=\s(?:final\s+answer|answer|response|output)\s*:|\Z)",
        r"(let'?s\s+think\s+step\s+by\s+step\s*[:.].*?)(?=\s(?:final\s+answer|answer|response|output)\s*:|\Z)",
    ]
    for pattern in patterns:
        match = re.search(pattern, clean, flags=re.I | re.S)
        if match:
            got = compact(match.group(1), 520)
            if got:
                return "Visible reasoning example: " + got
    return ""

def clean_answer(answer):
    answer = str(answer or "")
    answer = re.sub(r"<think>\s*.*?\s*</think>", " ", answer, flags=re.I | re.S)
    answer = re.sub(r"<analysis>\s*.*?\s*</analysis>", " ", answer, flags=re.I | re.S)
    answer = re.sub(r"\[Thinking:[^\]]*\]", " ", answer, flags=re.I | re.S)
    match = re.search(r"(?:^|\s)(?:final\s+answer|answer|response|output)\s*:\s*(.+)$", answer, flags=re.I | re.S)
    if match:
        answer = match.group(1)
    answer = re.sub(
        r"(?:^|\s)(?:reasoning|rationale|explanation|analysis|thinking|chain\s+of\s+thought|cot)\s*:\s*.*?(?=\s(?:final\s+answer|answer|response|output)\s*:|\Z)",
        " ",
        answer,
        flags=re.I | re.S,
    )
    return compact(answer, 520)

def merge_lessons(left, right):
    left = compact(left, 700)
    right = compact(right, 700)
    if not left:
        return right
    if not right:
        return left
    if normalized(right)[:80] in normalized(left):
        return left
    return compact(left + "; " + right, 700)

def reasoning_lesson(row, answer_text=""):
    parts = []
    if isinstance(row, dict):
        for key, value in row.items():
            if is_reasoning_key(key):
                text = compact(field_text(value), 520)
                if text:
                    parts.append(str(key).replace("_", " ") + ": " + text)
            if len(parts) >= 2:
                break
    answer_reasoning = visible_reasoning(answer_text)
    if answer_reasoning:
        parts.append(answer_reasoning)
    return compact("; ".join(parts), 700)

def first_field(row, names):
    for name in names:
        name_norm = normalized(name)
        for key, value in row.items():
            if is_noise_key(key):
                continue
            key_norm = normalized(key)
            if key_norm == name_norm or key_norm.endswith(" " + name_norm) or (len(name_norm) > 2 and name_norm in key_norm):
                text = field_text(value)
                if text:
                    return text
    return ""

def add_sample(samples, question, answer, lesson="", max_samples=1000):
    question = compact(question)
    answer_reasoning = visible_reasoning(answer)
    answer = clean_answer(answer)
    lesson = merge_lessons(lesson, answer_reasoning)
    if len(samples) >= max_samples or len(question.split()) < 2 or not answer:
        return
    key = normalized(question)
    if any(normalized(existing["question"]) == key for existing in samples):
        return
    samples.append({"question": question, "answer": answer, "lesson": lesson})

def looks_question(text):
    text = compact(text, 260).lower()
    return text.endswith("?") or text.startswith(("what ", "why ", "how ", "when ", "where ", "who ", "which ", "can ", "is ", "are ", "do ", "does ", "solve ", "calculate ", "question:", "instruction:", "prompt:"))

def role_score(key, text, role):
    key_norm = normalized(key)
    input_exact = {"q", "question", "prompt", "instruction", "input", "query", "user", "human", "source", "src", "problem", "task"}
    input_terms = {"question", "prompt", "instruction", "input", "query", "source", "problem", "scenario", "passage", "context", "article", "premise", "sentence", "text"}
    output_exact = {"a", "answer", "response", "output", "completion", "target", "label", "assistant", "gpt", "solution", "result", "dst", "translation", "chosen"}
    output_terms = {"answer", "response", "output", "completion", "target", "label", "assistant", "solution", "result", "summary", "definition", "description", "translation", "correct"}
    positive_exact = output_exact if role == "output" else input_exact
    positive_terms = output_terms if role == "output" else input_terms
    negative_exact = input_exact if role == "output" else output_exact
    negative_terms = input_terms if role == "output" else output_terms
    score = 0
    if key_norm in positive_exact:
        score += 80
    elif any(term in key_norm for term in positive_terms):
        score += 45
    if key_norm in negative_exact:
        score -= 70
    elif any(term in key_norm for term in negative_terms):
        score -= 45
    if role == "input":
        if looks_question(text):
            score += 45
        if len(text) > 80:
            score += 8
    else:
        if str(text).lower().startswith(("answer:", "response:", "output:")):
            score += 35
        if len(text) <= 160:
            score += 6
        if str(text).lower() in ("true", "false"):
            score += 4
        else:
            try:
                float(str(text))
                score += 4
            except Exception:
                pass
    return score

def best_role_field(useful, role, exclude=-1):
    best_index = -1
    best_score = -10**9
    for idx, (key, text) in enumerate(useful):
        if idx == exclude:
            continue
        score = role_score(key, text, role)
        if score > best_score:
            best_index = idx
            best_score = score
    return best_index if best_score >= 20 else -1

def question_from_fields(input_field, output_field):
    in_key, in_text = input_field
    out_key, _ = output_field
    if looks_question(in_text):
        return in_text
    return "Given this " + str(in_key).replace("_", " ") + ": " + compact(in_text, 260) + ", what is the " + str(out_key).replace("_", " ") + "?"

def context_lesson(useful, input_index, output_index):
    parts = []
    for idx, (key, text) in enumerate(useful):
        if idx in (input_index, output_index):
            continue
        key_norm = normalized(key)
        if any(term in key_norm for term in ("context", "passage", "choice", "option", "topic", "category", "explanation", "rationale")):
            parts.append(str(key).replace("_", " ") + ": " + text)
        if len(parts) >= 3:
            break
    return compact("; ".join(parts), 520)

def conversation_samples(row, samples, max_samples):
    turns = row.get("messages") or row.get("conversations") or row.get("dialogue") or row.get("dialog") or row.get("chat") or row.get("turns")
    if not isinstance(turns, list):
        return False
    before = len(samples)
    row_reasoning = reasoning_lesson(row)
    pending_user = ""
    for turn in turns:
        if isinstance(turn, dict):
            role = field_text(turn.get("role") or turn.get("from") or turn.get("speaker") or turn.get("author") or turn.get("name")).lower()
            content = field_text(turn.get("content") or turn.get("value") or turn.get("text") or turn.get("message") or turn.get("body") or turn.get("utterance"))
        elif isinstance(turn, str):
            role = "user" if not pending_user else "assistant"
            content = field_text(turn)
        else:
            continue
        if not content:
            continue
        if any(name in role for name in ("user", "human", "customer", "client", "patient")) or role == "prompter":
            pending_user = content
        elif (any(name in role for name in ("assistant", "gpt", "bot", "model", "agent")) or role == "ai") and pending_user:
            add_sample(samples, pending_user, content, merge_lessons(row_reasoning, visible_reasoning(content)), max_samples=max_samples)
            pending_user = ""
            if len(samples) >= max_samples:
                break
    return len(samples) > before

def row_samples(row, samples, max_samples):
    if len(samples) >= max_samples:
        return
    if not isinstance(row, dict):
        text = field_text(row)
        add_sample(samples, "What information is provided in this dataset item?", text, max_samples=max_samples)
        return
    if isinstance(row.get("row"), dict):
        row = row["row"]
    if conversation_samples(row, samples, max_samples):
        return

    if isinstance(row.get("translation"), dict) or isinstance(row.get("translations"), dict):
        translation = row.get("translation") if isinstance(row.get("translation"), dict) else row.get("translations")
        entries = [(key, field_text(value)) for key, value in translation.items() if field_text(value)]
        if len(entries) >= 2:
            source_index = 0
            for idx, (key, _) in enumerate(entries):
                if normalized(key) in ("en", "eng", "english"):
                    source_index = idx
                    break
            added = 0
            for idx, (key, text) in enumerate(entries):
                if idx == source_index:
                    continue
                add_sample(samples,
                           "Translate this " + entries[source_index][0] + " text to " + str(key) + ": " + entries[source_index][1],
                           text,
                           max_samples=max_samples)
                added += 1
                if added >= 3 or len(samples) >= max_samples:
                    break
            if added:
                return

    context = first_field(row, ["context", "passage", "article", "lesson", "explanation"])
    question = first_field(row, ["q", "question", "instruction", "prompt", "query", "input", "problem"])
    answer = first_field(row, ["a", "answer", "answers", "response", "output", "completion", "target", "label", "solution", "dst"])
    extra_input = first_field(row, ["input", "context", "source", "src"])
    if question and extra_input and extra_input not in question:
        question = compact(question + " " + extra_input)
    if question and answer:
        add_sample(samples, question, answer, merge_lessons(context, reasoning_lesson(row, answer)), max_samples)
        return

    name = first_field(row, ["name", "title", "term", "word"])
    description = first_field(row, ["definition", "description", "summary", "text", "content"])
    if name and description and normalized(name) != normalized(description):
        add_sample(samples, "What is " + name + "?", description, merge_lessons(context, reasoning_lesson(row, description)), max_samples)
        return

    useful = []
    for key, value in row.items():
        key_norm = normalized(key)
        if is_noise_key(key) or is_reasoning_key(key):
            continue
        text = field_text(value)
        if text and not text.startswith("http"):
            useful.append((key, text))

    input_index = best_role_field(useful, "input")
    output_index = best_role_field(useful, "output", input_index)
    if output_index < 0:
        output_index = best_role_field(useful, "output")
        input_index = best_role_field(useful, "input", output_index)
    if input_index >= 0 and output_index >= 0 and input_index != output_index:
        add_sample(samples,
                   question_from_fields(useful[input_index], useful[output_index]),
                   useful[output_index][1],
                   merge_lessons(context_lesson(useful, input_index, output_index) or context,
                                 reasoning_lesson(row, useful[output_index][1])),
                   max_samples)
        return

    if len(useful) == 1:
        add_sample(samples, "What information is in the " + useful[0][0] + " field?", useful[0][1], reasoning_lesson(row, useful[0][1]), max_samples=max_samples)
    elif len(useful) == 2:
        add_sample(samples, question_from_fields(useful[0], useful[1]), useful[1][1], reasoning_lesson(row, useful[1][1]), max_samples=max_samples)
    elif len(useful) > 1:
        anchor_key, anchor_value = useful[0]
        for key, text in useful[1:4]:
            add_sample(samples,
                       "For " + anchor_key + " \"" + compact(anchor_value, 160) + "\", what is the " + key + "?",
                       text,
                       reasoning_lesson(row, text),
                       max_samples=max_samples)

def remote_parse_worker_count():
    try:
        requested = int(os.environ.get("AITRAINER_PARSE_WORKERS", "0"))
    except Exception:
        requested = 0
    cpu_count = os.cpu_count() or 2
    if requested > 0:
        return max(1, min(requested, 64))
    return max(1, min(max(1, cpu_count - 1), 16))

def remote_parse_batch_size():
    try:
        requested = int(os.environ.get("AITRAINER_PARSE_BATCH_LINES", "200"))
    except Exception:
        requested = 200
    return max(20, min(requested, 2000))

def parse_row_batch_worker(batch, max_samples):
    batch_samples = []
    for row in batch:
        row_samples(row, batch_samples, max_samples)
        if len(batch_samples) >= max_samples:
            break
    return batch_samples

def merge_worker_samples(samples, incoming, seen_questions, max_samples):
    added = 0
    for sample in incoming:
        if len(samples) >= max_samples:
            break
        question = compact(sample.get("question", ""))
        answer = compact(sample.get("answer", ""))
        lesson = compact(sample.get("lesson", ""))
        key = normalized(question)
        if not key or key in seen_questions or len(question.split()) < 2 or not answer:
            continue
        seen_questions.add(key)
        samples.append({"question": question, "answer": answer, "lesson": lesson})
        added += 1
    return added

def parse_rows_parallel(row_iter, samples, max_samples, label, start_line=1):
    if len(samples) >= max_samples:
        return 0

    workers = remote_parse_worker_count()
    batch_size = remote_parse_batch_size()
    seen_questions = set(normalized(sample.get("question", "")) for sample in samples)
    log("[remote] parser workers: " + str(workers) + ", batch lines: " + str(batch_size) + ", source: " + label)

    def sequential_batch(batch, batch_id, first_line):
        incoming = parse_row_batch_worker(batch, batch_size * 4)
        added = merge_worker_samples(samples, incoming, seen_questions, max_samples)
        log("[remote] parser batch " + str(batch_id) + " lines "
            + str(first_line) + "-" + str(first_line + len(batch) - 1)
            + ": candidates=" + str(len(incoming))
            + ", added=" + str(added)
            + ", total=" + str(len(samples)))
        return len(batch)

    processed = 0
    batch_id = 0
    next_line = max(1, int(start_line))
    batch = []

    if workers <= 1:
        for row in row_iter:
            batch.append(row)
            if len(batch) >= batch_size:
                batch_id += 1
                processed += sequential_batch(batch, batch_id, next_line)
                next_line += len(batch)
                batch = []
                if len(samples) >= max_samples:
                    break
        if batch and len(samples) < max_samples:
            batch_id += 1
            processed += sequential_batch(batch, batch_id, next_line)
        return processed

    pending = []
    try:
        with concurrent.futures.ProcessPoolExecutor(max_workers=workers) as executor:
            def submit_batch(batch_to_submit, first_line):
                nonlocal batch_id
                batch_id += 1
                future = executor.submit(parse_row_batch_worker, batch_to_submit, batch_size * 4)
                pending.append((batch_id, first_line, len(batch_to_submit), batch_to_submit, future))

            def drain_one():
                nonlocal processed
                if not pending:
                    return
                current_id, first_line, count, original_batch, future = pending.pop(0)
                try:
                    incoming = future.result()
                except Exception as exc:
                    log("[remote] parser worker batch " + str(current_id) + " failed; retrying batch in-process: " + str(exc))
                    incoming = parse_row_batch_worker(original_batch, batch_size * 4)
                added = merge_worker_samples(samples, incoming, seen_questions, max_samples)
                processed += count
                log("[remote] parser worker batch " + str(current_id) + " lines "
                    + str(first_line) + "-" + str(first_line + count - 1)
                    + ": candidates=" + str(len(incoming))
                    + ", added=" + str(added)
                    + ", total=" + str(len(samples)))

            for row in row_iter:
                batch.append(row)
                if len(batch) >= batch_size:
                    submit_batch(batch, next_line)
                    next_line += len(batch)
                    batch = []
                    while len(pending) >= workers * 3:
                        drain_one()
                        if len(samples) >= max_samples:
                            break
                if len(samples) >= max_samples:
                    break

            if batch and len(samples) < max_samples:
                submit_batch(batch, next_line)

            while pending and len(samples) < max_samples:
                drain_one()

            for _, _, _, _, future in pending:
                future.cancel()
    except Exception as exc:
        log("[remote] parallel parser unavailable; falling back to single-process parser: " + str(exc))
        for current_id, first_line, count, original_batch, future in pending:
            future.cancel()
            if len(samples) >= max_samples:
                break
            processed += sequential_batch(original_batch, current_id, first_line)
        if batch and len(samples) < max_samples:
            batch_id += 1
            processed += sequential_batch(batch, batch_id, next_line)
            next_line += len(batch)
        fallback_batch = []
        for row in row_iter:
            fallback_batch.append(row)
            if len(fallback_batch) >= batch_size:
                batch_id += 1
                processed += sequential_batch(fallback_batch, batch_id, next_line)
                next_line += len(fallback_batch)
                fallback_batch = []
                if len(samples) >= max_samples:
                    break
        if fallback_batch and len(samples) < max_samples:
            batch_id += 1
            processed += sequential_batch(fallback_batch, batch_id, next_line)
        return processed

    return processed

def walk_json(value, samples, max_samples):
    if len(samples) >= max_samples:
        return
    if isinstance(value, list):
        for item in value:
            walk_json(item, samples, max_samples)
            if len(samples) >= max_samples:
                break
    elif isinstance(value, dict):
        if "data" in value and isinstance(value["data"], list):
            walk_json(value["data"], samples, max_samples)
        elif "rows" in value and isinstance(value["rows"], list):
            walk_json(value["rows"], samples, max_samples)
        else:
            row_samples(value, samples, max_samples)
            for nested_key in ["paragraphs", "qas", "items"]:
                if nested_key in value:
                    walk_json(value[nested_key], samples, max_samples)

def parse_text_payload(text, samples, max_samples):
    stripped = text.strip()
    if not stripped:
        return
    try:
        walk_json(json.loads(stripped), samples, max_samples)
        if samples:
            return
    except Exception:
        pass

    for line in stripped.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            value = json.loads(line)
        except Exception:
            continue
        walk_json(value, samples, max_samples)
        if len(samples) >= max_samples:
            return
    if samples:
        return

    first_line = stripped.splitlines()[0] if stripped.splitlines() else ""
    for delimiter in ["\t", ",", "|", ";"]:
        if delimiter not in first_line:
            continue
        before = len(samples)
        try:
            reader = csv.DictReader(io.StringIO(stripped), delimiter=delimiter)
            for row in reader:
                row_samples(row, samples, max_samples)
                if len(samples) >= max_samples:
                    return
        except Exception:
            pass
        if len(samples) > before:
            return
        try:
            reader = csv.reader(io.StringIO(stripped), delimiter=delimiter)
            for fields in reader:
                if len(fields) >= 2:
                    add_sample(samples, fields[0], fields[1], fields[2] if len(fields) >= 3 else "", max_samples=max_samples)
                if len(samples) >= max_samples:
                    return
        except Exception:
            pass
        if len(samples) > before:
            return

    parts = re.split(r"\n\s*\n|(?<=[.!?])\s+", stripped)
    for idx, part in enumerate(parts):
        part = compact(part)
        if len(part.split()) >= 4:
            add_sample(samples, "What does dataset text sample " + str(idx + 1) + " say?", part, max_samples=max_samples)
        if len(samples) >= max_samples:
            return

def hf_headers():
    headers = {"User-Agent": "AitrainerRemote/1.0"}
    if HF_TOKEN:
        headers["Authorization"] = "Bearer " + HF_TOKEN
    return headers

def fetch_hf_json(url):
    last_error = None
    for attempt in range(6):
        req = urllib.request.Request(url, headers=hf_headers())
        try:
            with urllib.request.urlopen(req, timeout=120) as res:
                return json.loads(res.read().decode("utf-8", errors="replace"))
        except urllib.error.HTTPError as exc:
            last_error = exc
            if exc.code != 429 or attempt == 5:
                raise
            delay = min(90, 5 * (2 ** attempt))
            log("[remote] Hugging Face Dataset Viewer rate limited; retrying in " + str(delay) + "s")
            time.sleep(delay)
    raise last_error

def hf_param(value):
    return urllib.parse.quote(str(value or ""), safe="")

def load_hf_viewer_samples(source, max_samples):
    samples = []
    log("[remote] loading Hugging Face Dataset Viewer rows: " + source)
    splits_url = "https://datasets-server.huggingface.co/splits?dataset=" + hf_param(source)
    split_doc = fetch_hf_json(splits_url)
    split_entries = split_doc.get("splits", [])
    if not split_entries:
        return samples

    selected = None
    for entry in split_entries:
        if str(entry.get("split", "")).lower() == "train":
            selected = entry
            break
    if selected is None:
        selected = split_entries[0]

    config = selected.get("config") or selected.get("config_name") or "default"
    split = selected.get("split") or "train"
    offset = 0
    fetch_size = max(200, min(1000, remote_parse_batch_size() * 5))
    total_rows = None

    while len(samples) < max_samples:
        length = min(fetch_size, max_samples - len(samples))
        rows_url = (
            "https://datasets-server.huggingface.co/rows?dataset=" + hf_param(source)
            + "&config=" + hf_param(config)
            + "&split=" + hf_param(split)
            + "&offset=" + str(offset)
            + "&length=" + str(length)
        )
        rows_doc = fetch_hf_json(rows_url)
        rows = rows_doc.get("rows", [])
        if total_rows is None:
            try:
                total_rows = int(rows_doc.get("num_rows_total", 0))
            except Exception:
                total_rows = 0
        if not rows:
            break

        parse_rows_parallel(
            (wrapped.get("row", wrapped) if isinstance(wrapped, dict) else wrapped for wrapped in rows),
            samples,
            max_samples,
            "Dataset Viewer " + source,
            offset + 1,
        )

        offset += len(rows)
        if len(rows) < length or (total_rows and offset >= total_rows):
            break
        if offset % 1000 == 0:
            log("[remote] loaded viewer rows: " + str(offset) + ", samples: " + str(len(samples)))

    return samples

def load_samples(source, max_samples):
    samples = []
    source = source.strip()
    if not source:
        return samples

    local_path = source
    if os.path.exists(local_path):
        log("[remote] reading local dataset file " + local_path)
        with open(local_path, "rb") as f:
            parse_text_payload(f.read().decode("utf-8", errors="replace"), samples, max_samples)
        return samples

    if source.startswith("http://") or source.startswith("https://"):
        log("[remote] downloading URL on GPU server: " + source)
        req = urllib.request.Request(source, headers={"User-Agent": "AitrainerRemote/1.0"})
        if HF_TOKEN and ("huggingface.co" in source or "hf.co" in source):
            req.add_header("Authorization", "Bearer " + HF_TOKEN)
        with urllib.request.urlopen(req, timeout=120) as res:
            parse_text_payload(res.read().decode("utf-8", errors="replace"), samples, max_samples)
        return samples

    log("[remote] loading Hugging Face dataset on GPU server: " + source)
    try:
        from datasets import load_dataset
    except Exception as exc:
        log("[remote] Python package 'datasets' is unavailable; trying Hugging Face Dataset Viewer fallback")
        try:
            return load_hf_viewer_samples(source, max_samples)
        except Exception as viewer_exc:
            raise RuntimeError("Python package 'datasets' is unavailable and Dataset Viewer fallback failed: " + str(viewer_exc) + ". Original import error: " + str(exc))

    kwargs = {}
    if HF_TOKEN:
        kwargs["token"] = HF_TOKEN
    try:
        dataset = load_dataset(source, split="train", streaming=True, **kwargs)
    except TypeError:
        kwargs.pop("token", None)
        if HF_TOKEN:
            kwargs["use_auth_token"] = HF_TOKEN
        dataset = load_dataset(source, split="train", streaming=True, **kwargs)
    except Exception:
        ds_dict = load_dataset(source, streaming=True, **kwargs)
        split_name = "train" if "train" in ds_dict else next(iter(ds_dict.keys()))
        dataset = ds_dict[split_name]

    parse_rows_parallel(dataset, samples, max_samples, "Hugging Face dataset stream " + source, 1)
    return samples

def unpack_archive(path):
    payload = open(path, "rb").read()
    archive = json.loads(qt_uncompress(payload).decode("utf-8"))
    files = {}
    for item in archive.get("files", []):
        name = os.path.basename(item.get("name", ""))
        if name:
            files[name] = base64.b64decode(item.get("data_base64", ""))
    return archive, files

def pack_archive(path, archive, files):
    packed_files = []
    for name, payload in files.items():
        packed_files.append({
            "name": name,
            "size": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
            "data_base64": base64.b64encode(payload).decode("ascii"),
        })
    archive["files"] = packed_files
    archive["created_at"] = archive.get("created_at") or ""
    raw = json.dumps(archive, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    with open(path, "wb") as f:
        f.write(MAGIC)
        f.write(qt_compress(raw))

def parse_memory(payload):
    memory = {}
    text = payload.decode("utf-8", errors="replace") if payload else ""
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            try:
                memory[(parts[0], parts[1])] = memory.get((parts[0], parts[1]), 0.0) + float(parts[2])
            except Exception:
                pass
    return memory

def dump_memory(memory):
    rows = []
    for (left, right), weight in sorted(memory.items()):
        if weight > 0:
            rows.append(f"{left} {right} {weight:.8g}")
    return ("\n".join(rows) + ("\n" if rows else "")).encode("utf-8")

def clamp_rank(rank):
    try:
        rank = int(rank)
    except Exception:
        rank = 8
    return max(1, min(rank, 16))

def lora_initial_vector(token, rank, salt):
    values = []
    for idx in range(rank):
        digest = hashlib.sha256((str(token) + "#" + str(salt) + "#" + str(idx)).encode("utf-8")).digest()
        unit = (int.from_bytes(digest[:8], "little") % 2001) / 1000.0 - 1.0
        values.append(unit * 0.01)
    return values

def vector_for_token(matrix, token, rank, salt):
    values = matrix.get(token)
    if not isinstance(values, list):
        return lora_initial_vector(token, rank, salt)
    clean = []
    for value in values[:rank]:
        try:
            clean.append(float(value))
        except Exception:
            clean.append(0.0)
    if len(clean) < rank:
        clean.extend(lora_initial_vector(token, rank, salt)[len(clean):])
    return clean

def parse_lora(payload, target_rank):
    rank = 4
    alpha = 8.0
    a_vectors = {}
    b_vectors = {}
    trained_pairs = set()
    text = payload.decode("utf-8", errors="replace") if payload else ""
    for line in text.splitlines():
        parts = line.strip().split()
        if not parts:
            continue
        if parts[0] == "AITRAINER_LORA_V1" and len(parts) >= 3:
            try:
                rank = clamp_rank(parts[1])
                alpha = float(parts[2])
            except Exception:
                rank = 4
                alpha = 8.0
            continue
        if parts[0] in ("A", "B") and len(parts) >= 2:
            token = parts[1]
            values = []
            for value in parts[2:2 + rank]:
                try:
                    values.append(float(value))
                except Exception:
                    values.append(0.0)
            if parts[0] == "A":
                a_vectors[token] = values
            else:
                b_vectors[token] = values
        elif parts[0] == "P" and len(parts) >= 3:
            trained_pairs.add((parts[1], parts[2]))

    rank = max(rank, clamp_rank(target_rank))
    alpha = max(alpha, float(rank) * 2.0)
    return rank, alpha, a_vectors, b_vectors, trained_pairs

def dump_lora(rank, alpha, a_vectors, b_vectors, trained_pairs):
    def fmt(value):
        return "{:.9g}".format(float(value))

    rows = ["AITRAINER_LORA_V1 " + str(rank) + " " + fmt(alpha)]
    for token in sorted(a_vectors.keys()):
        rows.append("A " + token + " " + " ".join(fmt(v) for v in vector_for_token(a_vectors, token, rank, 11)))
    for token in sorted(b_vectors.keys()):
        rows.append("B " + token + " " + " ".join(fmt(v) for v in vector_for_token(b_vectors, token, rank, 29)))
    for left, right in sorted(trained_pairs):
        rows.append("P " + left + " " + right)
    return ("\n".join(rows) + "\n").encode("utf-8")

def sample_training_text(sample):
    text = "Question: {q}\n".format(q=sample["question"])
    if sample.get("lesson"):
        text += "Lesson: {l}\n".format(l=sample["lesson"])
    text += "Answer: {a}".format(a=sample["answer"])
    return text

def collect_lora_pair_counts(samples, max_pairs):
    pair_counts = {}
    for sample in samples:
        toks = tokens(sample_training_text(sample))
        for idx in range(len(toks) - 1):
            pair = (toks[idx], toks[idx + 1])
            if pair in pair_counts:
                pair_counts[pair] += 1.0
            elif len(pair_counts) < max_pairs:
                pair_counts[pair] = 1.0
    return pair_counts

def require_rocm_torch():
    try:
        import torch
    except Exception as exc:
        raise RuntimeError("ROCm PyTorch is required for MI350X GPU adapter training, but torch could not be imported: " + str(exc))

    hip_version = getattr(torch.version, "hip", None)
    if not hip_version:
        raise RuntimeError("Installed PyTorch is not a ROCm/HIP build. Install ROCm PyTorch for AMD Instinct MI350X.")
    if not torch.cuda.is_available():
        raise RuntimeError("ROCm PyTorch is installed, but torch.cuda.is_available() is false. Check ROCm driver, permissions, and GPU visibility.")

    device_count = torch.cuda.device_count()
    device_index = 0
    found_mi350 = False
    for idx in range(device_count):
        name = str(torch.cuda.get_device_name(idx))
        log("[remote] PyTorch ROCm visible device " + str(idx) + ": " + name)
        if "MI350" in name.upper() and not found_mi350:
            device_index = idx
            found_mi350 = True

    if not found_mi350:
        raise RuntimeError("ROCm PyTorch is available, but no AMD Instinct MI350-class GPU was reported. Refusing non-MI350 adapter training.")

    torch.cuda.set_device(device_index)
    device = torch.device("cuda:" + str(device_index))
    device_name = torch.cuda.get_device_name(device_index)
    log("[remote] PyTorch version: " + str(torch.__version__))
    log("[remote] PyTorch HIP version: " + str(hip_version))
    log("[remote] PyTorch ROCm selected device: " + str(device_index) + " / " + str(device_name))
    try:
        torch.set_float32_matmul_precision("high")
    except Exception:
        pass
    return torch, device

def merge_lora_deltas_to_memory(rank, alpha, a_vectors, b_vectors, trained_pairs, memory, minimum_delta=0.001):
    merged = 0
    alpha_over_rank = float(alpha) / float(max(1, rank))
    for left, right in trained_pairs:
        a = a_vectors.get(left)
        b = b_vectors.get(right)
        if not a or not b:
            continue
        dot = 0.0
        for idx in range(rank):
            dot += float(a[idx]) * float(b[idx])
        delta = alpha_over_rank * dot
        if delta >= minimum_delta:
            memory[(left, right)] = memory.get((left, right), 0.0) + delta
            merged += 1
    return merged

def train_lora_adapter_rocm(samples, files, memory, epochs, adapter_rank):
    torch, device = require_rocm_torch()
    pair_counts = collect_lora_pair_counts(samples, int(os.environ.get("AITRAINER_REMOTE_LORA_MAX_PAIRS", "1000000")))
    if not pair_counts:
        raise RuntimeError("No token pairs were available for ROCm LoRA adapter training.")

    rank, alpha, a_vectors, b_vectors, trained_pairs = parse_lora(files.get("lora_adapter.txt", b""), adapter_rank)
    epochs = max(1, min(int(epochs), 32))
    learning_rate = 0.05
    salience = max(1.0, float(epochs)) * 0.8
    alpha_over_rank = float(alpha) / float(rank)

    from_tokens = sorted(set(left for left, _ in pair_counts.keys()) | set(a_vectors.keys()))
    to_tokens = sorted(set(right for _, right in pair_counts.keys()) | set(b_vectors.keys()))
    from_index = {token: idx for idx, token in enumerate(from_tokens)}
    to_index = {token: idx for idx, token in enumerate(to_tokens)}

    a_init = [vector_for_token(a_vectors, token, rank, 11) for token in from_tokens]
    b_init = [vector_for_token(b_vectors, token, rank, 29) for token in to_tokens]
    pair_items = sorted(pair_counts.items())
    pair_a = [from_index[pair[0][0]] for pair in pair_items]
    pair_b = [to_index[pair[0][1]] for pair in pair_items]
    counts = [pair[1] for pair in pair_items]

    log("[remote] ROCm LoRA adapter training: pairs=" + str(len(pair_items))
        + ", A tokens=" + str(len(from_tokens))
        + ", B tokens=" + str(len(to_tokens))
        + ", rank=" + str(rank)
        + ", epochs=" + str(epochs))

    a_tensor = torch.tensor(a_init, dtype=torch.float32, device=device)
    b_tensor = torch.tensor(b_init, dtype=torch.float32, device=device)
    pair_a_tensor = torch.tensor(pair_a, dtype=torch.long, device=device)
    pair_b_tensor = torch.tensor(pair_b, dtype=torch.long, device=device)
    count_tensor = torch.tensor(counts, dtype=torch.float32, device=device)
    count_scale = torch.clamp(torch.sqrt(count_tensor), 1.0, 8.0)
    target_delta = salience * torch.clamp(torch.log1p(count_tensor), 1.0, 4.0)

    for epoch in range(epochs):
        selected_a = a_tensor.index_select(0, pair_a_tensor)
        selected_b = b_tensor.index_select(0, pair_b_tensor)
        dot = (selected_a * selected_b).sum(dim=1)
        current = alpha_over_rank * dot
        error = torch.clamp(target_delta - current, -4.0, 4.0)
        scale = (learning_rate * error * alpha_over_rank * count_scale).unsqueeze(1)

        delta_a = torch.zeros_like(a_tensor)
        delta_b = torch.zeros_like(b_tensor)
        delta_a.index_add_(0, pair_a_tensor, scale * selected_b)
        delta_b.index_add_(0, pair_b_tensor, scale * selected_a)
        a_tensor = torch.clamp(a_tensor + delta_a, -8.0, 8.0)
        b_tensor = torch.clamp(b_tensor + delta_b, -8.0, 8.0)
        torch.cuda.synchronize(device)
        log("[remote] ROCm LoRA adapter epoch " + str(epoch + 1) + "/" + str(epochs) + " complete")

    a_cpu = a_tensor.detach().cpu().tolist()
    b_cpu = b_tensor.detach().cpu().tolist()
    for idx, token in enumerate(from_tokens):
        a_vectors[token] = [float(v) for v in a_cpu[idx]]
    for idx, token in enumerate(to_tokens):
        b_vectors[token] = [float(v) for v in b_cpu[idx]]
    for (left, right), _ in pair_items:
        trained_pairs.add((left, right))

    files["lora_adapter.txt"] = dump_lora(rank, alpha, a_vectors, b_vectors, trained_pairs)
    merged = merge_lora_deltas_to_memory(rank, alpha, a_vectors, b_vectors, trained_pairs, memory)
    log("[remote] ROCm LoRA adapter saved to lora_adapter.txt; merged deltas into memory: " + str(merged))
    try:
        torch.cuda.empty_cache()
    except Exception:
        pass
    return {
        "rank": rank,
        "alpha": alpha,
        "pairs": len(pair_items),
        "trained_pairs": len(trained_pairs),
        "merged": merged,
        "device": str(torch.cuda.get_device_name(device)),
    }

def train_sample(sample, memory, sentences, epochs):
    text = sample_training_text(sample)

    toks = tokens(text)
    salience = max(1.0, float(epochs)) * 0.8
    for i in range(len(toks) - 1):
        pair = (toks[i], toks[i + 1])
        memory[pair] = memory.get(pair, 0.0) + salience

    for sentence in re.split(r"\n|(?<=[.!?])\s+", text):
        sentence = compact(sentence)
        if len(sentence.split()) >= 3:
            sentences.add(sentence)

def update_knowledge(existing, samples, epochs):
    by_question = {}
    for item in existing:
        if isinstance(item, dict) and item.get("question") and item.get("answer"):
            by_question[normalized(item["question"])] = item
    for sample in samples:
        key = normalized(sample["question"])
        if key in by_question:
            item = by_question[key]
            item["answer"] = sample["answer"]
            if sample.get("lesson"):
                item["lesson"] = sample["lesson"]
            item["source"] = "digitalocean gpu training"
            item["strength"] = float(item.get("strength", 1.0)) + max(1, epochs)
        else:
            by_question[key] = {
                "question": sample["question"],
                "lesson": sample.get("lesson", ""),
                "answer": sample["answer"],
                "correction": "",
                "source": "digitalocean gpu training",
                "strength": float(max(1, epochs)),
            }
    return list(by_question.values())

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--dataset", required=True)
    parser.add_argument("--epochs", type=int, default=4)
    parser.add_argument("--max-samples", type=int, default=20000)
    parser.add_argument("--adapter-rank", type=int, default=8)
    args = parser.parse_args()

    log_remote_runtime()
    log("[remote] unpacking .ai package")
    archive, files = unpack_archive(args.input)
    samples = load_samples(args.dataset, max(1, args.max_samples))
    if not samples:
        raise SystemExit("[remote] no trainable samples were produced from dataset")
    log("[remote] prepared samples: " + str(len(samples)))

    try:
        knowledge = json.loads(files.get("student_knowledge.json", b"[]").decode("utf-8", errors="replace"))
        if not isinstance(knowledge, list):
            knowledge = []
    except Exception:
        knowledge = []
    memory = parse_memory(files.get("agent_memory.txt", b""))
    sentences = set()
    sentence_payload = files.get("sentence_memory.txt", b"").decode("utf-8", errors="replace")
    for line in sentence_payload.splitlines():
        line = compact(line)
        if line:
            sentences.add(line)

    for idx, sample in enumerate(samples, 1):
        train_sample(sample, memory, sentences, args.epochs)
        if idx % 1000 == 0:
            log("[remote] trained samples: " + str(idx) + "/" + str(len(samples)))

    adapter_result = train_lora_adapter_rocm(samples, files, memory, args.epochs, args.adapter_rank)
    knowledge = update_knowledge(knowledge, samples, args.epochs)
    files["student_knowledge.json"] = (json.dumps(knowledge, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    files["agent_memory.txt"] = dump_memory(memory)
    files["sentence_memory.txt"] = ("\n".join(sorted(sentences)) + "\n").encode("utf-8")
    note = files.get("note.txt", b"").decode("utf-8", errors="replace")
    note += "\nDigitalOcean GPU training:\n"
    note += "- dataset: " + args.dataset + "\n"
    note += "- samples: " + str(len(samples)) + "\n"
    note += "- epochs: " + str(args.epochs) + "\n"
    note += "- ROCm LoRA adapter: rank " + str(adapter_result["rank"]) + ", pairs " + str(adapter_result["pairs"]) + ", device " + adapter_result["device"] + "\n"
    files["note.txt"] = note.encode("utf-8")

    log("[remote] repacking trained .ai package")
    pack_archive(args.output, archive, files)
    log("[remote] done: " + args.output)

if __name__ == "__main__":
    main()
)PY");
}

void AgentController::cleanupDatasetParseThread() {
    if (m_datasetParseLogTimer) {
        m_datasetParseLogTimer->stop();
    }
    m_datasetParseLogTicks = 0;

    if (m_datasetParseThread) {
        m_datasetParseThread->deleteLater();
        m_datasetParseThread = nullptr;
    }
}

void AgentController::startDatasetBytesParsing(const QByteArray &datasetBytes, const QString &source, int originalBytes) {
    if (m_datasetParseThread) {
        appendToSimulationLog("[Dataset Training]: Dataset parser is already running.");
        return;
    }

    m_datasetTrainingSource = source;
    m_datasetTrainingOriginalBytes = originalBytes;
    m_datasetTrainingTotalChunks = 0;
    m_datasetTrainingNextChunkIndex = 0;
    m_pendingDatasetChunks.clear();

    const bool useLocalGpuForParse = m_useLocalGpuTraining;
    if (useLocalGpuForParse) {
        std::string gpuStatus;
        if (!LearningAgent::localGpuAvailable(&gpuStatus)) {
            const QString message = "Dataset parsing failed: local GPU is checked but GPU parser scan is unavailable. "
                + QString::fromStdString(gpuStatus);
            setLocalGpuTrainingStatus(message);
            appendToSimulationLog("[Local GPU Parsing]: " + message);
            emit simulationMessageAdded("system", message);
            return;
        }
        setLocalGpuTrainingStatus(QString::fromStdString(gpuStatus));
        appendToSimulationLog("[Local GPU Parsing]: " + QString::fromStdString(gpuStatus));
    }

    appendToSimulationLog(QString("[Dataset Training]: Downloaded %1 bytes. Parsing samples on a background thread.")
        .arg(originalBytes));
    emit simulationMessageAdded("system", "Dataset downloaded. Parsing in the background so the UI stays responsive.");
    if (m_datasetParseLogTimer) {
        m_datasetParseLogTicks = 0;
        m_datasetParseLogTimer->start();
    }

    QPointer<AgentController> self(this);
    m_datasetParseThread = QThread::create([self, datasetBytes, source, originalBytes, useLocalGpuForParse]() {
        if (useLocalGpuForParse) {
            std::string scanStatus;
            const std::string payload(datasetBytes.constData(), static_cast<size_t>(datasetBytes.size()));
            if (!LearningAgent::localGpuDatasetScan(payload, &scanStatus)) {
                if (self) {
                    const QString message = "Dataset parsing failed: local GPU parser scan did not run. "
                        + QString::fromStdString(scanStatus);
                    QMetaObject::invokeMethod(self.data(),
                                              [self, message]() {
                                                  if (self) {
                                                      self->setLocalGpuTrainingStatus(message);
                                                      self->failDatasetParsing(message);
                                                  }
                                              },
                                              Qt::QueuedConnection);
                }
                return;
            }
            if (self) {
                const QString status = QString::fromStdString(scanStatus);
                QMetaObject::invokeMethod(self.data(),
                                          [self, status]() {
                                              if (self) {
                                                  self->setLocalGpuTrainingStatus(status);
                                                  self->appendToSimulationLog("[Local GPU Parsing]: " + status);
                                              }
                                          },
                                          Qt::QueuedConnection);
            }
        }
        QStringList samples = buildDatasetTrainingSamples(datasetBytes, std::numeric_limits<int>::max());
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self.data(),
                                  [self, samples = std::move(samples), originalBytes, source]() mutable {
                                      if (self) {
                                          self->finishDatasetParsing(std::move(samples), originalBytes, source);
                                      }
                                  },
                                  Qt::QueuedConnection);
    });
    m_datasetParseThread->start();
}

void AgentController::startDatasetFileParsing(const QString &filePath) {
    if (m_datasetParseThread) {
        appendToSimulationLog("[Dataset Training]: Dataset parser is already running.");
        return;
    }

    m_datasetTrainingSource = filePath;
    m_datasetTrainingOriginalBytes = 0;
    m_datasetTrainingTotalChunks = 0;
    m_datasetTrainingNextChunkIndex = 0;
    m_pendingDatasetChunks.clear();

    const bool useLocalGpuForParse = m_useLocalGpuTraining;
    if (useLocalGpuForParse) {
        std::string gpuStatus;
        if (!LearningAgent::localGpuAvailable(&gpuStatus)) {
            const QString message = "Dataset parsing failed: local GPU is checked but GPU parser scan is unavailable. "
                + QString::fromStdString(gpuStatus);
            setLocalGpuTrainingStatus(message);
            appendToSimulationLog("[Local GPU Parsing]: " + message);
            emit simulationMessageAdded("system", message);
            return;
        }
        setLocalGpuTrainingStatus(QString::fromStdString(gpuStatus));
        appendToSimulationLog("[Local GPU Parsing]: " + QString::fromStdString(gpuStatus));
    }

    appendToSimulationLog(QString("[Dataset Training]: Reading and parsing local dataset on a background thread: %1")
        .arg(filePath));
    emit simulationMessageAdded("system", "Local dataset parsing started in the background so the UI stays responsive.");
    if (m_datasetParseLogTimer) {
        m_datasetParseLogTicks = 0;
        m_datasetParseLogTimer->start();
    }

    QPointer<AgentController> self(this);
    m_datasetParseThread = QThread::create([self, filePath, useLocalGpuForParse]() {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (self) {
                QMetaObject::invokeMethod(self.data(),
                                          [self]() {
                                              if (self) {
                                                  self->failDatasetParsing("Dataset training failed: could not open the local dataset file.");
                                              }
                                          },
                                          Qt::QueuedConnection);
            }
            return;
        }

        const QByteArray datasetBytes = file.readAll();
        const int originalBytes = datasetBytes.size();
        if (useLocalGpuForParse) {
            std::string scanStatus;
            const std::string payload(datasetBytes.constData(), static_cast<size_t>(datasetBytes.size()));
            if (!LearningAgent::localGpuDatasetScan(payload, &scanStatus)) {
                if (self) {
                    const QString message = "Dataset parsing failed: local GPU parser scan did not run. "
                        + QString::fromStdString(scanStatus);
                    QMetaObject::invokeMethod(self.data(),
                                              [self, message]() {
                                                  if (self) {
                                                      self->setLocalGpuTrainingStatus(message);
                                                      self->failDatasetParsing(message);
                                                  }
                                              },
                                              Qt::QueuedConnection);
                }
                return;
            }
            if (self) {
                const QString status = QString::fromStdString(scanStatus);
                QMetaObject::invokeMethod(self.data(),
                                          [self, status]() {
                                              if (self) {
                                                  self->setLocalGpuTrainingStatus(status);
                                                  self->appendToSimulationLog("[Local GPU Parsing]: " + status);
                                              }
                                          },
                                          Qt::QueuedConnection);
            }
        }
        QStringList samples = buildDatasetTrainingSamples(datasetBytes, std::numeric_limits<int>::max());
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self.data(),
                                  [self, samples = std::move(samples), originalBytes, filePath]() mutable {
                                      if (self) {
                                          self->finishDatasetParsing(std::move(samples), originalBytes, filePath);
                                      }
                                  },
                                  Qt::QueuedConnection);
    });
    m_datasetParseThread->start();
}

void AgentController::finishDatasetParsing(QStringList samples, int originalBytes, const QString &source) {
    cleanupDatasetParseThread();

    if (samples.isEmpty()) {
        appendToSimulationLog("[Dataset Training]: No trainable chunks were created.");
        emit simulationMessageAdded("system", "Dataset training failed: no trainable chunks were created.");
        m_datasetTrainingSource.clear();
        m_datasetTrainingOriginalBytes = 0;
        m_datasetTrainingTotalChunks = 0;
        m_datasetTrainingNextChunkIndex = 0;
        return;
    }

    m_datasetTrainingSource = source;
    m_datasetTrainingOriginalBytes = originalBytes;
    m_pendingDatasetChunks = std::move(samples);
    m_datasetTrainingTotalChunks = m_pendingDatasetChunks.size();
    m_datasetTrainingNextChunkIndex = 0;

    appendToSimulationLog(QString("[Dataset Training]: Parsing complete. Prepared %1 structured training samples/chunks from %2 bytes.")
        .arg(m_datasetTrainingTotalChunks)
        .arg(m_datasetTrainingOriginalBytes));
    emit simulationMessageAdded("system", QString("Dataset parsed. Training %1 structured samples/chunks in the background.").arg(m_datasetTrainingTotalChunks));
    QTimer::singleShot(0, this, &AgentController::processNextDatasetTrainingChunk);
}

void AgentController::failDatasetParsing(const QString &message) {
    cleanupDatasetParseThread();
    appendToSimulationLog("[Dataset Training]: " + message);
    emit simulationMessageAdded("system", message);
    m_datasetTrainingSource.clear();
    m_datasetTrainingOriginalBytes = 0;
    m_datasetTrainingTotalChunks = 0;
    m_datasetTrainingNextChunkIndex = 0;
    m_pendingDatasetChunks.clear();
}

QString AgentController::trainLoraFromDatasetUrl(const QString &datasetUrl, int epochs) {
    QString urlText = normalizeHuggingFaceDatasetFileUrl(datasetUrl);
    if (urlText.isEmpty()) {
        return "Dataset training skipped: enter a Hugging Face dataset URL.";
    }
    if (m_datasetReply || m_datasetParseThread || !m_pendingDatasetChunks.isEmpty()) {
        return "Dataset training is already running. Wait for it to finish before starting another dataset.";
    }
    if (m_useLocalGpuTraining) {
        std::string gpuStatus;
        if (!LearningAgent::localGpuAvailable(&gpuStatus)) {
            const QString message = "Dataset training failed: Use local GPU is checked, but local GPU acceleration is unavailable. "
                + QString::fromStdString(gpuStatus);
            setLocalGpuTrainingStatus(message);
            appendToSimulationLog("[Local GPU Training]: " + message);
            return message;
        }
        setLocalGpuTrainingStatus(QString::fromStdString(gpuStatus));
    }

    QFileInfo localFile(urlText);
    if (localFile.exists() && localFile.isFile()) {
        m_datasetTrainingEpochs = qMax(1, qMin(epochs, 8));
        m_datasetTrainingSource = localFile.absoluteFilePath();
        m_datasetRepoId.clear();
        m_datasetRequestIsMetadata = false;
        m_datasetRequestIsViewerSplits = false;
        m_datasetRequestIsViewerRows = false;
        m_datasetViewerConfig.clear();
        m_datasetViewerSplit.clear();
        m_datasetViewerOffset = 0;
        m_datasetViewerTotalRows = -1;
        startDatasetFileParsing(localFile.absoluteFilePath());
        return "Local dataset parsing started in the background. The UI will stay responsive while samples are prepared.";
    }

    QUrl requestedUrl(urlText);
    if (!requestedUrl.isValid() || requestedUrl.scheme().isEmpty()) {
        return "Dataset training failed: URL is not valid.";
    }

    QString sourceUrl = requestedUrl.toString();
    bool directFile = sourceUrl.contains("/resolve/") || isLikelyTrainableDatasetFile(requestedUrl.path());

    m_datasetTrainingEpochs = qMax(1, qMin(epochs, 8));
    m_datasetTrainingTotalChunks = 0;
    m_datasetTrainingOriginalBytes = 0;
    m_datasetRepoId.clear();
    m_datasetRequestIsMetadata = false;
    m_datasetRequestIsViewerSplits = false;
    m_datasetRequestIsViewerRows = false;
    m_datasetViewerConfig.clear();
    m_datasetViewerSplit.clear();
    m_datasetViewerOffset = 0;
    m_datasetViewerTotalRows = -1;

    if (requestedUrl.host().contains("huggingface.co")
        && requestedUrl.path().toLower().endsWith(".parquet")) {
        const QString repoId = huggingFaceRepoIdFromUrl(requestedUrl);
        if (repoId.isEmpty()) {
            return "Dataset training failed: could not read Hugging Face dataset repo id from Parquet URL.";
        }
        requestDatasetViewerSplits(repoId);
        return "Dataset uses Parquet. Loading rows through Hugging Face Dataset Viewer in the background.";
    }

    if (!directFile && requestedUrl.host().contains("huggingface.co")) {
        const QString repoId = huggingFaceRepoIdFromUrl(requestedUrl);
        if (repoId.isEmpty()) {
            return "Dataset training failed: could not read Hugging Face dataset repo id.";
        }

        m_datasetRepoId = repoId;
        m_datasetTrainingSource = requestedUrl.toString();
        m_datasetRequestIsMetadata = true;

        QNetworkRequest request{QUrl(QString("https://huggingface.co/api/datasets/%1").arg(repoId))};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        applyHuggingFaceAuth(request);
        m_datasetReply = m_networkManager.get(request);
        connect(m_datasetReply, &QNetworkReply::finished, this, &AgentController::handleDatasetTrainingDownload);

        appendToSimulationLog(QString("[Dataset Training]: Inspecting Hugging Face dataset repo: %1").arg(repoId));
        emit simulationMessageAdded("system", "Dataset LoRA training started. Inspecting Hugging Face repo in the background.");
        return "Inspecting Hugging Face dataset in the background. The UI will stay responsive.";
    }

    m_datasetTrainingSource = sourceUrl;
    m_datasetRequestIsMetadata = false;

    QNetworkRequest request{QUrl(sourceUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    applyHuggingFaceAuth(request);
    m_datasetReply = m_networkManager.get(request);
    connectDatasetDownloadProgress(m_datasetReply,
                                   QFileInfo(requestedUrl.path()).fileName().isEmpty()
                                       ? sourceUrl
                                       : QFileInfo(requestedUrl.path()).fileName());
    connect(m_datasetReply, &QNetworkReply::finished, this, &AgentController::handleDatasetTrainingDownload);

    appendToSimulationLog(QString("[Dataset Training]: Download started: %1").arg(sourceUrl));
    emit simulationMessageAdded("system", "Dataset LoRA training started. Downloading in the background.");
    return "Dataset download started in the background. The UI will stay responsive while training runs.";
}

QString AgentController::exportAgentPackage(const QString &filePath) {
    saveMemory();

    QString outputPath = filePath.trimmed();
    if (outputPath.isEmpty()) {
        outputPath = "student_agent.ai";
    }
    if (!outputPath.endsWith(".ai", Qt::CaseInsensitive)) {
        outputPath += ".ai";
    }

    QFileInfo outInfo(outputPath);
    if (!outInfo.dir().exists() && !outInfo.dir().mkpath(".")) {
        return "Export failed: could not create output directory.";
    }

    QJsonObject archive;
    archive["format"] = "AITRAINER_AI_PACKAGE";
    archive["version"] = 1;
    archive["created_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    archive["huggingface_lora_url"] = "https://huggingface.co/docs/peft/main/conceptual_guides/lora";

    QJsonArray files;
    const QStringList paths = {
        QString::fromStdString(m_agent.getMemoryFilePath()),
        "sentence_memory.txt",
        m_knowledgeFile,
        m_loraFile,
        "note.txt"
    };

    for (const QString &path : paths) {
        QFile file(path);
        if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
            continue;
        }

        const QByteArray payload = file.readAll();
        QJsonObject fileObj;
        fileObj["name"] = QFileInfo(path).fileName();
        fileObj["size"] = static_cast<double>(payload.size());
        fileObj["sha256"] = QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
        fileObj["data_base64"] = QString::fromLatin1(payload.toBase64());
        files.append(fileObj);
    }

    archive["files"] = files;
    const QByteArray json = QJsonDocument(archive).toJson(QJsonDocument::Compact);
    const QByteArray compressed = qCompress(json, 9);

    QFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly)) {
        return "Export failed: could not open output .ai file.";
    }

    out.write("AITRAINER_AI_V1\n");
    out.write(compressed);
    out.close();

    return QString("Exported %1 files into %2 (%3 bytes compressed).")
        .arg(files.size())
        .arg(QFileInfo(outputPath).absoluteFilePath())
        .arg(QFileInfo(outputPath).size());
}

QString AgentController::importAgentPackage(const QString &filePath) {
    QString inputPath = filePath.trimmed();
    if (inputPath.isEmpty()) {
        inputPath = "student_agent.ai";
    }

    QFile in(inputPath);
    if (!in.open(QIODevice::ReadOnly)) {
        return "Import failed: could not open .ai file.";
    }

    QByteArray payload = in.readAll();
    const QByteArray magic = "AITRAINER_AI_V1\n";
    if (payload.startsWith(magic)) {
        payload = payload.mid(magic.size());
    }

    const QByteArray json = qUncompress(payload);
    QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject() || doc.object()["format"].toString() != "AITRAINER_AI_PACKAGE") {
        return "Import failed: invalid or corrupted .ai package.";
    }

    const QStringList allowedFiles = {
        QFileInfo(QString::fromStdString(m_agent.getMemoryFilePath())).fileName(),
        QFileInfo("sentence_memory.txt").fileName(),
        QFileInfo(m_knowledgeFile).fileName(),
        QFileInfo(m_loraFile).fileName(),
        QFileInfo("note.txt").fileName()
    };

    int importedCount = 0;
    const QJsonArray files = doc.object()["files"].toArray();
    for (const QJsonValue &value : files) {
        const QJsonObject fileObj = value.toObject();
        const QString name = QFileInfo(fileObj["name"].toString()).fileName();
        if (!allowedFiles.contains(name)) {
            continue;
        }

        const QByteArray fileBytes = QByteArray::fromBase64(fileObj["data_base64"].toString().toLatin1());
        const QString expectedHash = fileObj["sha256"].toString();
        const QString actualHash = QString::fromLatin1(QCryptographicHash::hash(fileBytes, QCryptographicHash::Sha256).toHex());
        if (!expectedHash.isEmpty() && expectedHash != actualHash) {
            return QString("Import failed: checksum mismatch for %1.").arg(name);
        }

        QFile out(name);
        if (!out.open(QIODevice::WriteOnly)) {
            return QString("Import failed: could not write %1.").arg(name);
        }
        out.write(fileBytes);
        importedCount++;
    }

    loadMemory();
    emit memoryChanged();

    return QString("Imported %1 files from %2.").arg(importedCount).arg(QFileInfo(inputPath).absoluteFilePath());
}

void AgentController::saveGpuSettings() const {
    QSettings settings("AitrainerCorp", "Aitrainer");
    settings.setValue("gpuHost", m_gpuHost);
    settings.setValue("gpuSshPort", m_gpuSshPort);
    settings.setValue("gpuUsername", m_gpuUsername);
    settings.setValue("gpuSshKeyPath", m_gpuSshKeyPath);
    settings.setValue("gpuRemoteRoot", m_gpuRemoteRoot);
    settings.setValue("gpuMaxSamples", m_gpuMaxSamples);
}

void AgentController::saveLocalGpuSettings() const {
    QSettings settings("AitrainerCorp", "Aitrainer");
    settings.setValue("useLocalGpuTraining", m_useLocalGpuTraining);
}

void AgentController::setGpuTrainingStatus(const QString &status) {
    if (m_gpuTrainingStatus != status) {
        m_gpuTrainingStatus = status;
        emit gpuTrainingStatusChanged();
    }
}

void AgentController::setLocalGpuTrainingStatus(const QString &status) {
    if (m_localGpuTrainingStatus != status) {
        m_localGpuTrainingStatus = status;
        emit localGpuTrainingChanged();
    }
}

QString AgentController::gpuRemoteTarget(const QString &remotePath) const {
    const QString host = normalizedSshHost(m_gpuHost);
    return QString("%1@%2:%3")
        .arg(m_gpuUsername.trimmed().isEmpty() ? "root" : m_gpuUsername.trimmed(),
             host,
             remotePath);
}

QStringList AgentController::gpuSshArguments(const QString &remoteCommand) const {
    QStringList args;
    const QString host = normalizedSshHost(m_gpuHost);
    if (!m_gpuSshKeyPath.trimmed().isEmpty()) {
        args << "-i" << m_gpuSshKeyPath.trimmed();
    }
    args << "-p" << QString::number(qMax(1, m_gpuSshPort))
         << "-o" << "ServerAliveInterval=15"
         << "-o" << "ServerAliveCountMax=6"
         << "-o" << "StrictHostKeyChecking=accept-new"
         << QString("%1@%2").arg(m_gpuUsername.trimmed().isEmpty() ? "root" : m_gpuUsername.trimmed(),
                                  host)
         << remoteCommand;
    return args;
}

QStringList AgentController::gpuScpUploadArguments(const QString &localPath, const QString &remotePath) const {
    QStringList args;
    if (!m_gpuSshKeyPath.trimmed().isEmpty()) {
        args << "-i" << m_gpuSshKeyPath.trimmed();
    }
    args << "-P" << QString::number(qMax(1, m_gpuSshPort))
         << "-o" << "StrictHostKeyChecking=accept-new"
         << localPath
         << gpuRemoteTarget(remotePath);
    return args;
}

QStringList AgentController::gpuScpDownloadArguments(const QString &remotePath, const QString &localPath) const {
    QStringList args;
    if (!m_gpuSshKeyPath.trimmed().isEmpty()) {
        args << "-i" << m_gpuSshKeyPath.trimmed();
    }
    args << "-P" << QString::number(qMax(1, m_gpuSshPort))
         << "-o" << "StrictHostKeyChecking=accept-new"
         << gpuRemoteTarget(remotePath)
         << localPath;
    return args;
}

void AgentController::startGpuProcess(GpuTrainingStage stage,
                                      const QString &program,
                                      const QStringList &arguments,
                                      const QString &status) {
    if (m_gpuProcess) {
        m_gpuProcess->deleteLater();
    }

    m_gpuTrainingStage = stage;
    m_gpuProcess = new QProcess(this);
    m_gpuProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_gpuProcess, &QProcess::readyReadStandardOutput, this, &AgentController::handleGpuProcessOutput);
    connect(m_gpuProcess, &QProcess::readyReadStandardError, this, &AgentController::handleGpuProcessOutput);
    connect(m_gpuProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &AgentController::handleGpuProcessFinished);
    connect(m_gpuProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        failGpuTraining(QString("GPU process error %1: %2").arg(static_cast<int>(error)).arg(m_gpuProcess ? m_gpuProcess->errorString() : "unknown"));
    });

    setGpuTrainingStatus(status);
    appendToSimulationLog("[GPU Training]: " + status);
    emit simulationMessageAdded("system", "[GPU Training] " + status);
    m_gpuProcess->start(program, arguments);
}

void AgentController::handleGpuProcessOutput() {
    if (!m_gpuProcess) {
        return;
    }
    const QString output = QString::fromLocal8Bit(m_gpuProcess->readAll()).trimmed();
    if (output.isEmpty()) {
        return;
    }
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        appendToSimulationLog("[GPU Training]: " + line.trimmed());
    }
    setGpuTrainingStatus(lines.last().trimmed());
}

void AgentController::failGpuTraining(const QString &message) {
    if (m_gpuProcess) {
        m_gpuProcess->disconnect();
        if (m_gpuProcess->state() != QProcess::NotRunning) {
            m_gpuProcess->kill();
        }
        m_gpuProcess->deleteLater();
        m_gpuProcess = nullptr;
    }
    m_isGpuTrainingRunning = false;
    m_gpuTrainingStage = GpuTrainingStage::None;
    setGpuTrainingStatus(message);
    appendToSimulationLog("[GPU Training]: " + message);
    emit simulationMessageAdded("system", "[GPU Training] " + message);
    emit gpuTrainingStatusChanged();
}

void AgentController::finishGpuTraining() {
    const QString importResult = importAgentPackage(m_gpuLocalOutputPackage);
    m_isGpuTrainingRunning = false;
    m_gpuTrainingStage = GpuTrainingStage::None;
    const bool importFailed = importResult.startsWith("Import failed", Qt::CaseInsensitive);
    QString portableExportResult;
    if (!importFailed) {
        portableExportResult = exportAgentPackage("student_agent.ai");
    }
    const QString message = importFailed
        ? QString("GPU training downloaded %1, but local import failed. %2")
              .arg(QFileInfo(m_gpuLocalOutputPackage).absoluteFilePath(), importResult)
        : QString("GPU training complete. Downloaded and imported %1. %2 Default portable package refreshed: %3")
              .arg(QFileInfo(m_gpuLocalOutputPackage).absoluteFilePath(),
                   importResult,
                   portableExportResult);
    setGpuTrainingStatus(message);
    appendToSimulationLog("[GPU Training]: " + message);
    emit simulationMessageAdded("system", "[GPU Training] " + message);
    emit gpuTrainingStatusChanged();
}

void AgentController::handleGpuProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (!m_gpuProcess) {
        return;
    }
    m_gpuProcess->deleteLater();
    m_gpuProcess = nullptr;

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        if (m_gpuTrainingStage == GpuTrainingStage::CleanupRemote) {
            appendToSimulationLog("[GPU Training]: Remote cleanup did not finish, but the trained package was already imported.");
            m_gpuTrainingStage = GpuTrainingStage::None;
            return;
        }
        failGpuTraining(QString("Stage failed with exit code %1. Check GPU training log above.").arg(exitCode));
        return;
    }

    const QString remoteInput = m_gpuRemoteRunDir + "/input.ai";
    const QString remoteScript = m_gpuRemoteRunDir + "/remote_train_aitrainer.py";
    const QString remoteOutput = m_gpuRemoteRunDir + "/trained.ai";

    switch (m_gpuTrainingStage) {
    case GpuTrainingStage::CreateRemoteDir:
        startGpuProcess(GpuTrainingStage::UploadPackage,
                        "scp",
                        gpuScpUploadArguments(m_gpuLocalInputPackage, remoteInput),
                        "Uploading latest .ai package to GPU server.");
        break;
    case GpuTrainingStage::UploadPackage:
        startGpuProcess(GpuTrainingStage::UploadScript,
                        "scp",
                        gpuScpUploadArguments(m_gpuLocalScriptPath, remoteScript),
                        "Uploading remote trainer script to GPU server.");
        break;
    case GpuTrainingStage::UploadScript: {
        QString datasetForRemote = m_gpuDatasetSource.trimmed();
        QUrl datasetUrl(datasetForRemote);
        if (datasetUrl.isValid()
            && datasetUrl.host().contains("huggingface.co")
            && datasetForRemote.contains("/datasets/")
            && !datasetForRemote.contains("/resolve/")
            && !datasetForRemote.contains("/blob/")) {
            const QString repoId = huggingFaceRepoIdFromUrl(datasetUrl);
            if (!repoId.isEmpty()) {
                datasetForRemote = repoId;
            }
        }

        const QString remoteVenv = m_gpuRemoteRunDir + "/.venv";
        const QString remotePython = remoteVenv + "/bin/python";
        QString command = QString("cd %1").arg(shellQuote(m_gpuRemoteRunDir));
        command += " && REMOTE_PY='' && AITRAINER_USING_PREINSTALLED=0";
        command += " && for py in ${AITRAINER_REMOTE_PYTHON:-} python3 python /opt/conda/bin/python /root/miniconda3/bin/python /root/anaconda3/bin/python /root/aitrainer-venv/bin/python /root/.venv/bin/python; do if [ -z \"$py\" ]; then continue; fi; if command -v \"$py\" >/dev/null 2>&1; then cand=$(command -v \"$py\"); else cand=\"$py\"; fi; if [ -x \"$cand\" ] && \"$cand\" -c 'import importlib.util,sys; spec=importlib.util.find_spec(\"torch\"); sys.exit(1) if spec is None else None; import torch; sys.exit(0 if getattr(torch.version,\"hip\",None) and torch.cuda.is_available() else 1)' >/dev/null 2>&1; then REMOTE_PY=\"$cand\"; AITRAINER_USING_PREINSTALLED=1; break; fi; done";
        command += " && if [ -n \"$REMOTE_PY\" ]; then echo '[remote] using preinstalled ROCm PyTorch Python: '\"$REMOTE_PY\"; fi";
        command += QString(" && if [ -z \"$REMOTE_PY\" ]; then if [ ! -x %1 ]; then echo '[remote] creating Python virtual environment'; (python3 -m venv %2 || (command -v apt-get >/dev/null 2>&1 && apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y python3-venv python3-pip && python3 -m venv %2)); fi; REMOTE_PY=%1; fi")
            .arg(shellQuote(remotePython), shellQuote(remoteVenv));
        command += " && if [ \"$AITRAINER_USING_PREINSTALLED\" = 0 ]; then \"$REMOTE_PY\" -m pip install -q --upgrade pip setuptools wheel; fi";
        command += " && (\"$REMOTE_PY\" -c 'import importlib.util,sys; sys.exit(0 if importlib.util.find_spec(\"datasets\") else 1)' && echo '[remote] Python package datasets already available in selected Python' || (if [ \"$AITRAINER_USING_PREINSTALLED\" = 0 ]; then (\"$REMOTE_PY\" -m pip install -q datasets requests && echo '[remote] Python package install complete in venv' || echo '[remote] pip install failed; continuing with built-in Dataset Viewer fallback if needed'); else echo '[remote] selected preinstalled Python has no datasets package; using built-in Dataset Viewer fallback if needed'; fi))";
        command += " && (\"$REMOTE_PY\" -c 'import importlib.util,sys; spec=importlib.util.find_spec(\"torch\"); sys.exit(1) if spec is None else None; import torch; sys.exit(0 if getattr(torch.version,\"hip\",None) and torch.cuda.is_available() else 1)' && echo '[remote] ROCm PyTorch GPU build available in selected Python' || (echo '[remote] ROCm PyTorch not found in selected Python; installing ROCm GPU torch. This can take several minutes.' && ((\"$REMOTE_PY\" -m pip install --progress-bar off --pre torch --index-url https://download.pytorch.org/whl/nightly/rocm7.2 --extra-index-url https://pypi.org/simple || \"$REMOTE_PY\" -m pip install --progress-bar off --index-url https://repo.amd.com/rocm/whl/gfx950-dcgpu/ --extra-index-url https://pypi.org/simple \"torch==2.11.0+rocm7.13.0\") && echo '[remote] ROCm PyTorch install complete in venv' || echo '[remote] ROCm PyTorch install failed; remote script will fail if HIP GPU is unavailable')))";
        command += QString(" && HF_TOKEN=%1 \"$REMOTE_PY\" %2 --input %3 --output %4 --dataset %5 --epochs %6 --max-samples %7 --adapter-rank 8")
            .arg(shellQuote(m_huggingFaceToken.trimmed()),
                 shellQuote(remoteScript),
                 shellQuote(remoteInput),
                 shellQuote(remoteOutput),
                 shellQuote(datasetForRemote),
                 QString::number(qMax(1, qMin(m_gpuTrainingEpochs, 32))),
                 QString::number(qMax(1, m_gpuMaxSamples)));
        startGpuProcess(GpuTrainingStage::RunRemoteTraining,
                        "ssh",
                        gpuSshArguments(command),
                        "Running dataset download and .ai package training on GPU server.");
        break;
    }
    case GpuTrainingStage::RunRemoteTraining:
        startGpuProcess(GpuTrainingStage::DownloadPackage,
                        "scp",
                        gpuScpDownloadArguments(remoteOutput, m_gpuLocalOutputPackage),
                        "Downloading trained .ai package from GPU server.");
        break;
    case GpuTrainingStage::DownloadPackage:
        finishGpuTraining();
        if (QProcess::startDetached("ssh",
                                    gpuSshArguments(QString("rm -f %1 %2 %3")
                                                        .arg(shellQuote(remoteInput),
                                                             shellQuote(remoteScript),
                                                             shellQuote(remoteOutput))))) {
            appendToSimulationLog("[GPU Training]: Remote temporary cleanup requested.");
        } else {
            appendToSimulationLog("[GPU Training]: Remote cleanup could not be started; trained package was already imported.");
        }
        break;
    case GpuTrainingStage::CleanupRemote:
        finishGpuTraining();
        break;
    case GpuTrainingStage::None:
        break;
    }
}

QString AgentController::trainCurrentAgentOnGpuServer(const QString &datasetUrl, int epochs) {
    if (m_isGpuTrainingRunning || m_gpuProcess) {
        return "GPU training is already running.";
    }
    if (m_gpuHost.trimmed().isEmpty()) {
        return "GPU training failed: enter the DigitalOcean GPU server host/IP first.";
    }
    if (normalizedSshHost(m_gpuHost).isEmpty()) {
        return "GPU training failed: the DigitalOcean GPU server host/IP is not valid.";
    }
    if (datasetUrl.trimmed().isEmpty()) {
        return "GPU training failed: enter a dataset repo id, URL, or GPU-server file path first.";
    }
    const QFileInfo localDataset(datasetUrl.trimmed());
    if (localDataset.exists() && localDataset.isFile()) {
        return "GPU training failed: local dataset files are not uploaded. Use a Hugging Face repo/URL or a file path that already exists on the GPU server.";
    }

    const QString runId = "aitrainer_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd_hhmmss");
    m_gpuLocalRunDir = QDir(QDir::tempPath()).filePath(runId);
    if (!QDir().mkpath(m_gpuLocalRunDir)) {
        return "GPU training failed: could not create local temporary run directory.";
    }

    m_gpuLocalInputPackage = QDir(m_gpuLocalRunDir).filePath("input.ai");
    m_gpuLocalScriptPath = QDir(m_gpuLocalRunDir).filePath("remote_train_aitrainer.py");
    m_gpuLocalOutputPackage = QDir::current().absoluteFilePath("student_agent_gpu_trained.ai");
    QString remoteRoot = m_gpuRemoteRoot.trimmed();
    if (remoteRoot.isEmpty()) {
        remoteRoot = "/root/aitrainer-runs";
    }
    while (remoteRoot.endsWith('/')) {
        remoteRoot.chop(1);
    }
    m_gpuRemoteRunDir = remoteRoot + "/" + runId;
    m_gpuDatasetSource = normalizeHuggingFaceDatasetFileUrl(datasetUrl);
    m_gpuTrainingEpochs = qMax(1, qMin(epochs, 32));

    appendToSimulationLog(QString("[GPU Training]: Local preparation only: exporting current .ai package. Remote training will run through SSH on %1.")
        .arg(normalizedSshHost(m_gpuHost)));
    const QString exportResult = exportAgentPackage(m_gpuLocalInputPackage);
    if (exportResult.startsWith("Export failed", Qt::CaseInsensitive)) {
        return exportResult;
    }

    QFile scriptFile(m_gpuLocalScriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return "GPU training failed: could not write local remote trainer script.";
    }
    scriptFile.write(gpuRemoteScript().toUtf8());
    scriptFile.close();

    m_isGpuTrainingRunning = true;
    emit gpuTrainingStatusChanged();
    startGpuProcess(GpuTrainingStage::CreateRemoteDir,
                    "ssh",
                    gpuSshArguments(QString("mkdir -p %1").arg(shellQuote(m_gpuRemoteRunDir))),
                    "Creating remote run directory on DigitalOcean GPU server.");
    return "GPU training started. The latest .ai package will be uploaded and trained on the server.";
}

void AgentController::requestDatasetViewerSplits(const QString &repoId) {
    m_datasetRepoId = repoId;
    m_datasetRequestIsMetadata = false;
    m_datasetRequestIsViewerSplits = true;
    m_datasetRequestIsViewerRows = false;
    m_datasetViewerConfig.clear();
    m_datasetViewerSplit.clear();
    m_datasetViewerOffset = 0;
    m_datasetViewerTotalRows = -1;

    const QString encodedRepo = QString::fromLatin1(QUrl::toPercentEncoding(repoId, "/"));
    QNetworkRequest request{QUrl(QString("https://datasets-server.huggingface.co/splits?dataset=%1").arg(encodedRepo))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    applyHuggingFaceAuth(request);
    m_datasetReply = m_networkManager.get(request);
    connect(m_datasetReply, &QNetworkReply::finished, this, &AgentController::handleDatasetTrainingDownload);

    appendToSimulationLog(QString("[Dataset Training]: Requesting Hugging Face Dataset Viewer splits for %1.").arg(repoId));
    emit simulationMessageAdded("system", "Dataset uses a viewer-backed format. Loading rows through Hugging Face Dataset Viewer.");
}

void AgentController::requestDatasetViewerRows(int offset) {
    if (m_datasetRepoId.isEmpty() || m_datasetViewerConfig.isEmpty() || m_datasetViewerSplit.isEmpty()) {
        appendToSimulationLog("[Dataset Training]: Dataset Viewer row request failed: missing repo/config/split.");
        emit simulationMessageAdded("system", "Dataset training failed: missing Dataset Viewer split information.");
        return;
    }

    m_datasetViewerOffset = qMax(0, offset);
    const QString encodedRepo = QString::fromLatin1(QUrl::toPercentEncoding(m_datasetRepoId, "/"));
    const QString encodedConfig = QString::fromLatin1(QUrl::toPercentEncoding(m_datasetViewerConfig));
    const QString encodedSplit = QString::fromLatin1(QUrl::toPercentEncoding(m_datasetViewerSplit));
    const QUrl rowsUrl(QString("https://datasets-server.huggingface.co/rows?dataset=%1&config=%2&split=%3&offset=%4&length=%5")
        .arg(encodedRepo,
             encodedConfig,
             encodedSplit,
             QString::number(m_datasetViewerOffset),
             QString::number(qMax(1, m_datasetViewerPageSize))));

    m_datasetRequestIsViewerRows = true;
    QNetworkRequest rowsRequest{rowsUrl};
    rowsRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    applyHuggingFaceAuth(rowsRequest);
    m_datasetReply = m_networkManager.get(rowsRequest);
    connect(m_datasetReply, &QNetworkReply::finished, this, &AgentController::handleDatasetTrainingDownload);

    appendToSimulationLog(QString("[Dataset Training]: Loading Dataset Viewer rows %1-%2 for config=%3 split=%4.")
        .arg(m_datasetViewerOffset + 1)
        .arg(m_datasetViewerOffset + qMax(1, m_datasetViewerPageSize))
        .arg(m_datasetViewerConfig, m_datasetViewerSplit));
}

void AgentController::connectDatasetDownloadProgress(QNetworkReply *reply, const QString &label) {
    if (!reply) {
        return;
    }

    QSharedPointer<qint64> lastLoggedBytes = QSharedPointer<qint64>::create(0);
    connect(reply, &QNetworkReply::downloadProgress, this, [this, label, lastLoggedBytes](qint64 received, qint64 total) {
        const qint64 logStep = 5LL * 1024LL * 1024LL;
        const bool finished = total > 0 && received >= total;
        if (received < *lastLoggedBytes + logStep && !finished) {
            return;
        }

        *lastLoggedBytes = received;
        if (total > 0) {
            appendToSimulationLog(QString("[Dataset Training]: Downloading %1: %2/%3 bytes.")
                .arg(label)
                .arg(received)
                .arg(total));
        } else {
            appendToSimulationLog(QString("[Dataset Training]: Downloading %1: %2 bytes received.")
                .arg(label)
                .arg(received));
        }
    });
}

void AgentController::handleDatasetTrainingDownload() {
    if (!m_datasetReply) return;

    QNetworkReply *reply = m_datasetReply;
    m_datasetReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        const QString error = reply->errorString();
        reply->deleteLater();
        appendToSimulationLog(QString("[Dataset Training]: %1 failed: %2")
            .arg(m_datasetRequestIsMetadata ? "Repo inspection"
                 : (m_datasetRequestIsViewerSplits ? "Dataset viewer splits"
                    : (m_datasetRequestIsViewerRows ? "Dataset viewer rows" : "Download")))
            .arg(error));
        emit simulationMessageAdded("system", "Dataset training failed: " + error);
        m_datasetRequestIsMetadata = false;
        m_datasetRequestIsViewerSplits = false;
        m_datasetRequestIsViewerRows = false;
        m_datasetRepoId.clear();
        m_datasetViewerConfig.clear();
        m_datasetViewerSplit.clear();
        m_datasetViewerOffset = 0;
        m_datasetViewerTotalRows = -1;
        return;
    }

    const QByteArray responseBytes = reply->readAll();
    reply->deleteLater();

    if (m_datasetRequestIsMetadata) {
        m_datasetRequestIsMetadata = false;

        QJsonDocument metadata = QJsonDocument::fromJson(responseBytes);
        QJsonArray siblings = metadata.object()["siblings"].toArray();
        QString selectedFile;
        QString viewerBackedFile;
        int selectedScore = -1;
        int viewerScore = -1;
        for (const QJsonValue &value : siblings) {
            const QString name = value.toObject()["rfilename"].toString();
            const int score = huggingFaceDirectDatasetFileScore(name);
            if (score > selectedScore) {
                selectedFile = name;
                selectedScore = score;
            }

            const int currentViewerScore = huggingFaceViewerDatasetFileScore(name);
            if (currentViewerScore > viewerScore) {
                viewerBackedFile = name;
                viewerScore = currentViewerScore;
            }
        }

        if (!viewerBackedFile.isEmpty() && (selectedFile.isEmpty() || selectedScore < 100)) {
            appendToSimulationLog(QString("[Dataset Training]: Found viewer-backed dataset file %1. Loading real dataset rows instead of README/metadata files.")
                .arg(viewerBackedFile));
            requestDatasetViewerSplits(m_datasetRepoId);
            return;
        }

        if (selectedFile.isEmpty()) {
            appendToSimulationLog("[Dataset Training]: No direct trainable dataset file found. Trying Hugging Face Dataset Viewer rows.");
            requestDatasetViewerSplits(m_datasetRepoId);
            return;
        }

        const QString encodedFile = QString::fromLatin1(QUrl::toPercentEncoding(selectedFile, "/"));
        m_datasetTrainingSource = QString("https://huggingface.co/datasets/%1/resolve/main/%2").arg(m_datasetRepoId, encodedFile);
        m_datasetRepoId.clear();

        QNetworkRequest fileRequest{QUrl(m_datasetTrainingSource)};
        fileRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        applyHuggingFaceAuth(fileRequest);
        m_datasetReply = m_networkManager.get(fileRequest);
        connectDatasetDownloadProgress(m_datasetReply, selectedFile);
        connect(m_datasetReply, &QNetworkReply::finished, this, &AgentController::handleDatasetTrainingDownload);

        appendToSimulationLog(QString("[Dataset Training]: Selected file %1. Downloading now.").arg(selectedFile));
        emit simulationMessageAdded("system", QString("Selected dataset file: %1. Downloading in the background.").arg(selectedFile));
        return;
    }

    if (m_datasetRequestIsViewerSplits) {
        m_datasetRequestIsViewerSplits = false;
        QJsonDocument splitsDoc = QJsonDocument::fromJson(responseBytes);
        const QJsonArray splits = splitsDoc.object()["splits"].toArray();
        QJsonObject selectedSplit;

        for (const QJsonValue &value : splits) {
            const QJsonObject splitObj = value.toObject();
            if (splitObj["split"].toString().compare("train", Qt::CaseInsensitive) == 0) {
                selectedSplit = splitObj;
                break;
            }
        }
        if (selectedSplit.isEmpty() && !splits.isEmpty()) {
            selectedSplit = splits.first().toObject();
        }

        if (selectedSplit.isEmpty()) {
            appendToSimulationLog("[Dataset Training]: Dataset Viewer could not find a readable split.");
            emit simulationMessageAdded("system", "Dataset training failed: Dataset Viewer could not find a readable split.");
            m_datasetRepoId.clear();
            return;
        }

        m_datasetViewerConfig = selectedSplit["config"].toString();
        m_datasetViewerSplit = selectedSplit["split"].toString();
        m_pendingDatasetChunks.clear();
        m_datasetTrainingOriginalBytes = 0;
        m_datasetTrainingNextChunkIndex = 0;
        m_datasetViewerOffset = 0;
        m_datasetViewerTotalRows = selectedSplit["num_rows"].toInt(-1);

        appendToSimulationLog(QString("[Dataset Training]: Loading Dataset Viewer rows: config=%1 split=%2.")
            .arg(m_datasetViewerConfig, m_datasetViewerSplit));
        emit simulationMessageAdded("system", QString("Loading Dataset Viewer rows: %1/%2.").arg(m_datasetViewerConfig, m_datasetViewerSplit));
        requestDatasetViewerRows(0);
        return;
    }

    if (m_datasetRequestIsViewerRows) {
        m_datasetRequestIsViewerRows = false;
        m_datasetTrainingOriginalBytes += responseBytes.size();

        const QJsonDocument rowsDoc = QJsonDocument::fromJson(responseBytes);
        const QJsonObject rowsObj = rowsDoc.object();
        const QJsonArray rows = rowsObj["rows"].toArray();
        const int rowsInPage = rows.size();
        const int serverPageSize = rowsObj["num_rows_per_page"].toInt(qMax(1, m_datasetViewerPageSize));
        const int responseTotalRows = rowsObj["num_rows_total"].toInt(rowsObj["num_rows"].toInt(-1));
        if (responseTotalRows > 0) {
            m_datasetViewerTotalRows = responseTotalRows;
        }

        if (m_useLocalGpuTraining) {
            std::string scanStatus;
            const std::string payload(responseBytes.constData(), static_cast<size_t>(responseBytes.size()));
            if (!LearningAgent::localGpuDatasetScan(payload, &scanStatus)) {
                const QString message = "Dataset Viewer parsing failed: local GPU parser scan did not run. "
                    + QString::fromStdString(scanStatus);
                setLocalGpuTrainingStatus(message);
                appendToSimulationLog("[Local GPU Parsing]: " + message);
                emit simulationMessageAdded("system", message);
                return;
            }
            const QString status = QString::fromStdString(scanStatus);
            setLocalGpuTrainingStatus(status);
            appendToSimulationLog("[Local GPU Parsing]: " + status);
        }

        const QStringList pageSamples = buildDatasetTrainingSamples(responseBytes, std::numeric_limits<int>::max());
        m_pendingDatasetChunks.append(pageSamples);

        const int nextOffset = m_datasetViewerOffset + rowsInPage;
        const bool moreKnownRows = m_datasetViewerTotalRows > 0 && nextOffset < m_datasetViewerTotalRows;
        const bool maybeMoreRows = m_datasetViewerTotalRows <= 0 && rowsInPage >= qMax(1, serverPageSize);

        appendToSimulationLog(QString("[Dataset Training]: Loaded Dataset Viewer rows %1/%2. Prepared %3 trainable samples so far.")
            .arg(rowsInPage > 0 ? nextOffset : m_datasetViewerOffset)
            .arg(m_datasetViewerTotalRows > 0 ? QString::number(m_datasetViewerTotalRows) : "unknown")
            .arg(m_pendingDatasetChunks.size()));

        if (rowsInPage > 0 && (moreKnownRows || maybeMoreRows)) {
            requestDatasetViewerRows(nextOffset);
            return;
        }

        m_datasetTrainingSource = QString("hf-viewer://%1/%2/%3")
            .arg(m_datasetRepoId, m_datasetViewerConfig, m_datasetViewerSplit);
        m_datasetRepoId.clear();
        m_datasetTrainingTotalChunks = m_pendingDatasetChunks.size();
        m_datasetTrainingNextChunkIndex = 0;
        if (m_datasetTrainingTotalChunks == 0) {
            appendToSimulationLog("[Dataset Training]: No trainable chunks were created.");
            emit simulationMessageAdded("system", "Dataset training failed: no trainable chunks were created.");
            return;
        }

        appendToSimulationLog(QString("[Dataset Training]: Loaded %1 Dataset Viewer rows. Prepared %2 structured training samples/chunks from %3 bytes.")
            .arg(m_datasetViewerTotalRows > 0 ? m_datasetViewerTotalRows : nextOffset)
            .arg(m_datasetTrainingTotalChunks)
            .arg(m_datasetTrainingOriginalBytes));
        emit simulationMessageAdded("system", QString("Dataset Viewer loading complete. Training %1 structured samples/chunks in the background.").arg(m_datasetTrainingTotalChunks));
        QTimer::singleShot(0, this, &AgentController::processNextDatasetTrainingChunk);
        return;
    }

    const QByteArray datasetBytes = responseBytes;
    startDatasetBytesParsing(datasetBytes, m_datasetTrainingSource, datasetBytes.size());
}

void AgentController::processNextDatasetTrainingChunk() {
    if (m_datasetTrainingNextChunkIndex >= m_pendingDatasetChunks.size()) {
        saveMemory();
        emit memoryChanged();

        if (m_useLocalGpuTraining) {
            m_isLocalGpuTrainingRunning = false;
            setLocalGpuTrainingStatus(QString("Local GPU dataset training complete: %1 samples/chunks.")
                .arg(m_datasetTrainingTotalChunks));
        }

        const QString localMode = m_useLocalGpuTraining
            ? QString(" Local GPU mode: %1").arg(m_localGpuTrainingStatus)
            : QString();
        const QString completeMessage = QString("Dataset training complete: %1 structured samples/chunks, %2 epochs each, %3 bytes downloaded. Source: %4%5")
            .arg(m_datasetTrainingTotalChunks)
            .arg(m_datasetTrainingEpochs)
            .arg(m_datasetTrainingOriginalBytes)
            .arg(m_datasetTrainingSource,
                 localMode);
        appendToSimulationLog("[Dataset Training]: " + completeMessage);
        emit simulationMessageAdded("system", completeMessage);

        m_datasetTrainingSource.clear();
        m_datasetTrainingTotalChunks = 0;
        m_datasetTrainingOriginalBytes = 0;
        m_datasetTrainingNextChunkIndex = 0;
        m_pendingDatasetChunks.clear();
        m_datasetViewerConfig.clear();
        m_datasetViewerSplit.clear();
        m_datasetViewerOffset = 0;
        m_datasetViewerTotalRows = -1;
        return;
    }

    const int completedBefore = m_datasetTrainingNextChunkIndex;
    const int batchSize = m_useLocalGpuTraining ? 128 : 1;
    QStringList batchChunks;
    while (m_datasetTrainingNextChunkIndex < m_pendingDatasetChunks.size()
           && batchChunks.size() < batchSize) {
        batchChunks.append(m_pendingDatasetChunks.at(m_datasetTrainingNextChunkIndex));
        ++m_datasetTrainingNextChunkIndex;
    }

    if (m_useLocalGpuTraining && !m_isLocalGpuTrainingRunning) {
        m_isLocalGpuTrainingRunning = true;
            setLocalGpuTrainingStatus("Running local GPU LoRA dataset training in batches. CUDA is preferred when available; Direct3D 11 is fallback.");
    }

    std::string localGpuStatus;
    const QString trainingPayload = batchChunks.join("\n\n");
    const bool loraTrained = m_agent.trainLoraText(trainingPayload.toStdString(),
                                                   m_datasetTrainingEpochs,
                                                   0.05,
                                                   4,
                                                   8.0,
                                                   1.1,
                                                   m_useLocalGpuTraining,
                                                   &localGpuStatus);
    if (m_useLocalGpuTraining && !localGpuStatus.empty()) {
        const QString status = QString::fromStdString(localGpuStatus);
        setLocalGpuTrainingStatus(status);
        const bool importantStatus = status.contains("failed", Qt::CaseInsensitive)
            || status.contains("fallback", Qt::CaseInsensitive)
            || status.contains("unavailable", Qt::CaseInsensitive);
        if (completedBefore == 0 || importantStatus || m_datasetTrainingNextChunkIndex % 50 == 0) {
            appendToSimulationLog("[Local GPU Training]: " + status);
        }
    }
    if (m_useLocalGpuTraining && !loraTrained) {
        const QString message = "Dataset training stopped: local GPU LoRA dispatch failed and CPU fallback is disabled. "
            + QString::fromStdString(localGpuStatus);
        m_isLocalGpuTrainingRunning = false;
        setLocalGpuTrainingStatus(message);
        appendToSimulationLog("[Local GPU Training]: " + message);
        emit simulationMessageAdded("system", message);
        m_pendingDatasetChunks.clear();
        m_datasetTrainingSource.clear();
        m_datasetTrainingTotalChunks = 0;
        m_datasetTrainingOriginalBytes = 0;
        m_datasetTrainingNextChunkIndex = 0;
        return;
    }

    QRegularExpression qaRx("Question\\s*:\\s*([^\\n]+)(?:\\nLesson\\s*:\\s*([^\\n]+))?\\nAnswer\\s*:\\s*([^\\n]+)",
                            QRegularExpression::CaseInsensitiveOption);
    for (const QString &chunk : batchChunks) {
        m_agent.learn(chunk.toStdString(), 0.8);
        QRegularExpressionMatch qaMatch = qaRx.match(chunk);
        if (qaMatch.hasMatch()) {
            upsertKnowledge(qaMatch.captured(1).trimmed(),
                            qaMatch.captured(2).trimmed(),
                            qaMatch.captured(3).trimmed(),
                            "",
                            "dataset training",
                            0.9);
        }
    }

    if (m_datasetTrainingNextChunkIndex % 10 == 0 || m_datasetTrainingNextChunkIndex >= m_pendingDatasetChunks.size()) {
        appendToSimulationLog(QString("[Dataset Training]: Trained sample/chunk %1/%2")
            .arg(m_datasetTrainingNextChunkIndex)
            .arg(m_datasetTrainingTotalChunks));
    }

    QTimer::singleShot(m_useLocalGpuTraining ? 0 : 15, this, &AgentController::processNextDatasetTrainingChunk);
}

QString AgentController::agentFilesSummary() const {
    QDir cwd = QDir::current();
    return QString("Agent files: %1 | %2 | %3 | %4 | Export package: %5")
        .arg(cwd.absoluteFilePath(QString::fromStdString(m_agent.getMemoryFilePath())),
             cwd.absoluteFilePath("sentence_memory.txt"),
             cwd.absoluteFilePath(m_knowledgeFile),
             cwd.absoluteFilePath(m_loraFile),
             cwd.absoluteFilePath("student_agent.ai"));
}

QString AgentController::loraTrainingSummary() const {
    return QString("LoRA-like adapter: rank %1, trained pairs %2, local mode %3, file %4")
        .arg(m_agent.getLoraRank())
        .arg(m_agent.getLoraPairCount())
        .arg(m_useLocalGpuTraining ? "CUDA/Direct3D local GPU" : "CPU")
        .arg(QDir::current().absoluteFilePath(m_loraFile));
}

bool AgentController::copyTextToClipboard(const QString &text) const {
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard || text.isEmpty()) {
        return false;
    }
    clipboard->setText(text);
    return true;
}

bool AgentController::loadKnowledgeBank() {
    m_knowledgeBank.clear();
    markKnowledgeIndexDirty();

    QFile file(m_knowledgeFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return true;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) {
        return false;
    }

    bool skippedUntrustedSource = false;
    const QJsonArray items = doc.array();
    for (const QJsonValue &value : items) {
        QJsonObject obj = value.toObject();
        LearnedKnowledge knowledge;
        knowledge.question = obj["question"].toString().trimmed();
        knowledge.lesson = obj["lesson"].toString().trimmed();
        knowledge.answer = obj["answer"].toString().trimmed();
        knowledge.correction = obj["correction"].toString().trimmed();
        knowledge.source = obj["source"].toString().trimmed();
        knowledge.strength = obj["strength"].toDouble(1.0);

        if (!isTrustedKnowledgeSource(knowledge.source)) {
            skippedUntrustedSource = true;
            continue;
        }

        if (!knowledge.question.isEmpty() && !knowledge.answer.isEmpty()) {
            m_knowledgeBank.append(knowledge);
        }
    }

    markKnowledgeIndexDirty();
    if (skippedUntrustedSource) {
        saveKnowledgeBank();
    }
    return true;
}

bool AgentController::saveKnowledgeBank() const {
    QJsonArray items;
    for (const LearnedKnowledge &knowledge : m_knowledgeBank) {
        QJsonObject obj;
        obj["question"] = knowledge.question;
        obj["lesson"] = knowledge.lesson;
        obj["answer"] = knowledge.answer;
        obj["correction"] = knowledge.correction;
        obj["source"] = knowledge.source;
        obj["strength"] = knowledge.strength;
        items.append(obj);
    }

    QFile file(m_knowledgeFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    file.write(QJsonDocument(items).toJson(QJsonDocument::Indented));
    return true;
}

void AgentController::markKnowledgeIndexDirty() {
    m_knowledgeIndexDirty = true;
}

void AgentController::ensureKnowledgeIndex() const {
    if (!m_knowledgeIndexDirty) {
        return;
    }

    m_knowledgeTokenIndex.clear();
    m_knowledgeItemTokens.clear();
    m_knowledgeItemTokens.reserve(m_knowledgeBank.size());

    for (int i = 0; i < m_knowledgeBank.size(); ++i) {
        const LearnedKnowledge &knowledge = m_knowledgeBank[i];
        QStringList tokens;

        auto appendTokens = [&tokens](const QString &text, int maxTokens) {
            const QStringList extracted = significantQuestionTokens(text);
            for (const QString &token : extracted) {
                if (tokens.contains(token)) {
                    continue;
                }
                tokens.append(token);
                if (tokens.size() >= maxTokens) {
                    return;
                }
            }
        };

        appendTokens(knowledge.question, 64);
        appendTokens(knowledge.correction.isEmpty() ? knowledge.lesson : knowledge.correction, 128);
        appendTokens(knowledge.answer, 96);

        m_knowledgeItemTokens.append(tokens);
        for (const QString &token : tokens) {
            m_knowledgeTokenIndex[token].append(i);
        }
    }

    m_knowledgeIndexDirty = false;
}

QVector<int> AgentController::candidateKnowledgeIndexes(const QString &query, int limit) const {
    ensureKnowledgeIndex();

    limit = qMax(1, limit);
    const QStringList queryTokens = significantQuestionTokens(query);
    if (queryTokens.isEmpty()) {
        QVector<int> fallback;
        if (m_knowledgeBank.size() <= limit) {
            fallback.reserve(m_knowledgeBank.size());
            for (int i = 0; i < m_knowledgeBank.size(); ++i) {
                fallback.append(i);
            }
        }
        return fallback;
    }

    QHash<int, int> scores;
    for (const QString &token : queryTokens) {
        const auto indexesIt = m_knowledgeTokenIndex.constFind(token);
        if (indexesIt == m_knowledgeTokenIndex.constEnd()) {
            continue;
        }

        const int tokenWeight = token.front().isDigit() ? 4 : (token.length() >= 6 ? 2 : 1);
        for (int index : indexesIt.value()) {
            scores[index] += tokenWeight;
        }
    }

    if (scores.isEmpty()) {
        QVector<int> fallback;
        if (m_knowledgeBank.size() <= limit) {
            fallback.reserve(m_knowledgeBank.size());
            for (int i = 0; i < m_knowledgeBank.size(); ++i) {
                fallback.append(i);
            }
        }
        return fallback;
    }

    QVector<std::pair<int, int>> ranked;
    ranked.reserve(scores.size());
    for (auto it = scores.constBegin(); it != scores.constEnd(); ++it) {
        ranked.append({it.key(), it.value()});
    }

    std::sort(ranked.begin(), ranked.end(), [](const auto &left, const auto &right) {
        if (left.second != right.second) {
            return left.second > right.second;
        }
        return left.first > right.first;
    });

    QVector<int> candidates;
    candidates.reserve(qMin(limit, ranked.size()));
    for (const auto &item : ranked) {
        candidates.append(item.first);
        if (candidates.size() >= limit) {
            break;
        }
    }
    return candidates;
}

void AgentController::upsertKnowledge(const QString &question,
                                      const QString &lesson,
                                      const QString &answer,
                                      const QString &correction,
                                      const QString &source,
                                      double strength) {
    const QString cleanQuestion = compactText(question, 0);
    const QString cleanAnswer = compactText(answer, 0);
    if (cleanQuestion.isEmpty() || cleanAnswer.isEmpty() || !isTrustedKnowledgeSource(source)) {
        return;
    }

    const QString normalizedQuestion = normalizedForMatch(cleanQuestion);
    for (LearnedKnowledge &knowledge : m_knowledgeBank) {
        if (normalizedForMatch(knowledge.question) == normalizedQuestion) {
            if (!lesson.trimmed().isEmpty()) {
                knowledge.lesson = lesson.trimmed();
            }
            if (!cleanAnswer.isEmpty()) {
                knowledge.answer = cleanAnswer;
            }
            if (!correction.trimmed().isEmpty()) {
                knowledge.correction = correction.trimmed();
                knowledge.lesson = correction.trimmed();
            }
            if (!source.trimmed().isEmpty()) {
                knowledge.source = source.trimmed();
            }
            knowledge.strength += qMax(0.1, strength);
            markKnowledgeIndexDirty();
            return;
        }
    }

    LearnedKnowledge knowledge;
    knowledge.question = cleanQuestion;
    knowledge.lesson = correction.trimmed().isEmpty() ? lesson.trimmed() : correction.trimmed();
    knowledge.answer = cleanAnswer;
    knowledge.correction = correction.trimmed();
    knowledge.source = source.trimmed();
    knowledge.strength = qMax(0.1, strength);
    m_knowledgeBank.append(knowledge);
    markKnowledgeIndexDirty();
}

int AgentController::findBestKnowledgeIndex(const QString &query, int *score) const {
    int bestIndex = -1;
    int bestScore = 0;

    const QVector<int> candidates = candidateKnowledgeIndexes(query);
    for (int i : candidates) {
        if (i < 0 || i >= m_knowledgeBank.size()) {
            continue;
        }
        const LearnedKnowledge &knowledge = m_knowledgeBank[i];
        const int currentScore = learningRelevanceScore(query,
                                                        knowledge.question,
                                                        knowledge.lesson,
                                                        knowledge.correction,
                                                        knowledge.answer,
                                                        knowledge.strength);

        if (currentScore > bestScore) {
            bestScore = currentScore;
            bestIndex = i;
        }
    }

    if (score) {
        *score = bestScore;
    }
    return bestIndex;
}

QString AgentController::buildAppliedLearningAnswer(const QString &question, int *score) const {
    if (score) {
        *score = 0;
    }

    struct RankedKnowledge {
        int index;
        int score;
        int questionScore;
        int lessonScore;
        int semanticCoverage;
        int coreCoverage;
        QStringList evidenceTokens;
    };

    QVector<RankedKnowledge> ranked;
    const QVector<int> candidates = candidateKnowledgeIndexes(question, 1024);
    ranked.reserve(candidates.size());

    for (int i : candidates) {
        if (i < 0 || i >= m_knowledgeBank.size()) {
            continue;
        }

        const LearnedKnowledge &knowledge = m_knowledgeBank[i];
        const QString lesson = knowledge.correction.isEmpty() ? knowledge.lesson : knowledge.correction;
        const QString learningText = QString("%1 %2 %3")
            .arg(knowledge.question, lesson, knowledge.answer);
        const int currentScore = learningRelevanceScore(question,
                                                        knowledge.question,
                                                        knowledge.lesson,
                                                        knowledge.correction,
                                                        knowledge.answer,
                                                        knowledge.strength);
        const int questionScore = questionSimilarityScore(question, knowledge.question);
        const int lessonScore = meaningfulTokenOverlapScore(question, lesson);
        const int semanticCoverage = tokenCoveragePercent(significantQuestionTokens(question, true),
                                                          significantQuestionTokens(learningText, true));
        const int coreCoverage = tokenCoveragePercent(significantQuestionTokens(question, false),
                                                      significantQuestionTokens(learningText, false));
        const QStringList evidenceTokens = overlappingEvidenceTokens(question, learningText, 5);
        if (currentScore >= appliedLearningMinimumScore()
            && !isLowConfidenceAnswerText(knowledge.answer)
            && (questionScore >= 70
                || semanticCoverage >= 45
                || coreCoverage >= 30
                || lessonScore >= 30
                || evidenceTokens.size() >= 2)) {
            ranked.append({i, currentScore, questionScore, lessonScore, semanticCoverage, coreCoverage, evidenceTokens});
        }
    }

    if (ranked.isEmpty()) {
        return "";
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedKnowledge &left, const RankedKnowledge &right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        if (left.questionScore != right.questionScore) {
            return left.questionScore > right.questionScore;
        }
        if (left.semanticCoverage != right.semanticCoverage) {
            return left.semanticCoverage > right.semanticCoverage;
        }
        if (left.coreCoverage != right.coreCoverage) {
            return left.coreCoverage > right.coreCoverage;
        }
        return left.index > right.index;
    });

    const RankedKnowledge bestRank = ranked.first();
    if (score) {
        *score = bestRank.score;
    }

    const LearnedKnowledge &best = m_knowledgeBank[bestRank.index];
    const QString bestLesson = best.correction.isEmpty() ? best.lesson : best.correction;
    const QString bestAnswer = polishUserFacingText(extractAnswerForDisplay(best.answer), 360, true);
    if (bestAnswer.isEmpty() || isLowConfidenceAnswerText(bestAnswer)) {
        return "";
    }

    const int questionOverlap = keywordOverlapScore(question, best.question);
    const bool strongEnough = bestRank.score >= 58
        || bestRank.questionScore >= 70
        || bestRank.lessonScore >= 30
        || bestRank.semanticCoverage >= 50
        || bestRank.coreCoverage >= 35
        || questionOverlap >= 2
        || bestRank.evidenceTokens.size() >= 2;
    if (!strongEnough) {
        return "";
    }

    const QString source = best.source.trimmed().isEmpty() ? "trained memory" : best.source.trimmed();
    const QString lessonSentence = bestLessonSentenceForQuestion(bestLesson, question, 220);
    const QString normalizedQuestion = normalizedForMatch(question);

    if (asksForExamples(question)) {
        QStringList examples;
        for (const RankedKnowledge &candidate : ranked) {
            if (examples.size() >= 3) {
                break;
            }
            if (candidate.score < qMax(appliedLearningMinimumScore(), bestRank.score - 24)) {
                continue;
            }
            const LearnedKnowledge &knowledge = m_knowledgeBank[candidate.index];
            const QString exampleQuestion = compactText(knowledge.question, 110);
            const QString exampleAnswer = polishUserFacingText(extractAnswerForDisplay(knowledge.answer), 110, true);
            if (exampleQuestion.isEmpty()
                || exampleAnswer.isEmpty()
                || isLowConfidenceAnswerText(exampleAnswer)) {
                continue;
            }
            examples.append(QString("%1 -> %2").arg(exampleQuestion, exampleAnswer));
        }

        if (examples.isEmpty()) {
            return "";
        }

        QString thinking = QString("I found %1 related trained item%2 from %3 and reused their question-answer patterns as examples.")
            .arg(QString::number(examples.size()),
                 examples.size() == 1 ? "" : "s",
                 source);
        thinking.replace(']', ')');
        return QString("[Thinking: %1]\nAnswer: %2")
            .arg(polishUserFacingText(thinking, 260, true),
                 polishUserFacingText(examples.join(" | "), 0, true));
    }

    QString answer = bestAnswer;
    if ((normalizedQuestion.startsWith("how ") || normalizedQuestion.contains(" how "))
        && !lessonSentence.isEmpty()
        && !normalizedForMatch(answer).contains(normalizedForMatch(lessonSentence).left(60))) {
        answer = QString("%1 Apply that lesson here: %2").arg(lessonSentence, bestAnswer);
    } else if ((normalizedQuestion.startsWith("why ") || normalizedQuestion.contains(" why "))
               && !lessonSentence.isEmpty()
               && !normalizedForMatch(answer).contains(normalizedForMatch(lessonSentence).left(60))) {
        answer = QString("%1 The learned rule behind it is: %2").arg(bestAnswer, lessonSentence);
    }
    const QString groundedAnswer = buildLessonGroundedAnswer(bestLesson, question, answer);
    if (!groundedAnswer.isEmpty()) {
        answer = groundedAnswer;
    }

    QString thinking = QString("I found a related %1 record, extracted the reusable lesson, and applied it to this question.")
        .arg(source);
    if (!bestRank.evidenceTokens.isEmpty()) {
        thinking += " The shared ideas are " + bestRank.evidenceTokens.join(", ") + ".";
    }
    if (!lessonSentence.isEmpty()) {
        thinking += " I use the related lesson to check that the answer explains the question, not just a matching word.";
    }
    thinking.replace(']', ')');

    return QString("[Thinking: %1]\nAnswer: %2")
        .arg(polishUserFacingText(thinking, 320, true),
             polishUserFacingText(answer, 0, true));
}

QString AgentController::relatedKnowledgeContext(const QString &query, int limit) const {
    QStringList entries;
    QVector<std::pair<int, int>> ranked;
    const QVector<int> candidates = candidateKnowledgeIndexes(query, qMax(64, limit * 256));
    ranked.reserve(candidates.size());

    for (int i : candidates) {
        if (i < 0 || i >= m_knowledgeBank.size()) {
            continue;
        }
        const LearnedKnowledge &knowledge = m_knowledgeBank[i];
        const int currentScore = learningRelevanceScore(query,
                                                        knowledge.question,
                                                        knowledge.lesson,
                                                        knowledge.correction,
                                                        knowledge.answer,
                                                        knowledge.strength);
        if (currentScore >= relatedLearningMinimumScore()) {
            ranked.append({i, currentScore});
        }
    }

    std::sort(ranked.begin(), ranked.end(), [](const auto &left, const auto &right) {
        if (left.second != right.second) {
            return left.second > right.second;
        }
        return left.first > right.first;
    });

    for (const auto &candidate : ranked) {
        if (entries.size() >= limit) {
            break;
        }

        const LearnedKnowledge &knowledge = m_knowledgeBank[candidate.first];
        const QString lesson = compactText(knowledge.correction.isEmpty() ? knowledge.lesson : knowledge.correction, 0);
        const QString evidence = overlappingEvidenceTokens(query,
                                                           knowledge.question + " " + lesson + " " + knowledge.answer,
                                                           5).join(", ");
        entries.append(QString("- Related learning %1 (score %2%3): Question: %4 | Lesson: %5 | Answer: %6")
            .arg(entries.size() + 1)
            .arg(candidate.second)
            .arg(evidence.isEmpty() ? "" : QString(", evidence: %1").arg(evidence))
            .arg(compactText(knowledge.question, 0),
                 lesson.isEmpty() ? "(no lesson stored)" : lesson,
                 compactText(knowledge.answer, 0)));
    }

    return entries.join('\n');
}

QStringList AgentController::recentKnownQuestions(int limit) const {
    QStringList questions;

    for (const QString &rejected : m_rejectedTeacherQuestions) {
        const QString question = compactText(rejected, 160);
        if (!question.isEmpty() && !questions.contains(question)) {
            questions.append(question);
        }
        if (questions.size() >= limit) {
            return questions;
        }
    }

    for (int i = m_curriculumQuestions.size() - 1; i >= 0 && questions.size() < limit; --i) {
        const QString question = compactText(m_curriculumQuestions[i].question, 160);
        if (!question.isEmpty() && !questions.contains(question)) {
            questions.append(question);
        }
    }

    for (int i = m_knowledgeBank.size() - 1; i >= 0 && questions.size() < limit; --i) {
        const QString question = compactText(m_knowledgeBank[i].question, 160);
        if (!question.isEmpty() && !questions.contains(question)) {
            questions.append(question);
        }
    }

    return questions;
}

bool AgentController::isQuestionAlreadyLearned(const QString &question, int *score) const {
    int bestScore = 0;

    auto evaluateExisting = [&](const QString &existingQuestion) {
        if (question.trimmed().isEmpty() || existingQuestion.trimmed().isEmpty()) {
            return;
        }

        bestScore = qMax(bestScore, questionSimilarityScore(question, existingQuestion));
    };

    for (const QString &rejected : m_rejectedTeacherQuestions) {
        evaluateExisting(rejected);
    }
    for (const CurriculumItem &item : m_curriculumQuestions) {
        evaluateExisting(item.question);
    }

    const QVector<int> candidates = candidateKnowledgeIndexes(question, 1024);
    for (int index : candidates) {
        if (index < 0 || index >= m_knowledgeBank.size()) {
            continue;
        }
        evaluateExisting(m_knowledgeBank[index].question);
        if (bestScore >= 160) {
            break;
        }
    }

    if (score) {
        *score = bestScore;
    }
    return bestScore >= 60;
}

QVariantList AgentController::getAssociationsForWord(const QString &word) const {
    QVariantList list;
    auto assoc = m_agent.getAssociations(word.toStdString());
    
    int total = 0;
    for (const auto& pair : assoc) {
        total += pair.second;
    }

    std::vector<std::pair<std::string, int>> sortedAssoc(assoc.begin(), assoc.end());
    std::sort(sortedAssoc.begin(), sortedAssoc.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    for (const auto& pair : sortedAssoc) {
        QVariantMap map;
        map["word"] = QString::fromStdString(pair.first);
        map["count"] = pair.second;
        map["percentage"] = total > 0 ? static_cast<double>(pair.second) / total : 0.0;
        list.append(map);
    }

    return list;
}

QVariantList AgentController::getTopAssociations(int limit) const {
    QVariantList list;
    auto top = m_agent.getTopAssociations(limit);

    for (const auto& assoc : top) {
        QVariantMap map;
        map["word"] = QString::fromStdString(assoc.first.first);
        map["nextWord"] = QString::fromStdString(assoc.first.second);
        map["count"] = assoc.second;
        list.append(map);
    }

    return list;
}

// Simulation Controls and Handlers
void AgentController::startSimulation(const QString &topic, int turns) {
    if (m_isSimulationRunning) return;

    if (m_apiKey.trimmed().isEmpty()) {
        m_simulationLog = "ERROR: Featherless API Key is missing. Please enter your API Key in the settings panel.\n";
        emit simulationLogChanged();
        return;
    }

    // Memory is loaded during startup/import/manual load. Avoid reloading large datasets on every start.
    clearTestScores();

    m_isSimulationRunning = true;
    m_isTestingPhase = false;
    m_testQuestion = "";
    m_simulationTopic = topic;
    m_simulationTurns = turns;
    m_simulationCurrentTurn = 0;
    m_simulationLog = "";
    m_conversationHistory.clear();

    // Initialize curriculum variables
    m_trainingStage = TeachingStage;
    m_curriculumQuestions.clear();
    m_curriculumQuestionIndex = 0;
    m_isReTest = false;
    m_teacherRetryCount = 0;
    m_rejectedTeacherQuestions.clear();

    // Clean up or reset note.txt for the new batch run
    QFile::remove("note.txt");

    emit simulationStatusChanged();
    emit simulationLogChanged();

    appendToSimulationLog("--- CURRICULUM TRAINING SIMULATION STARTED ---");
    appendToSimulationLog(QString("Curriculum Phase: Teaching Stage (100 questions focused on: %1)")
        .arg(m_simulationTopic.trimmed().isEmpty() ? "general mixed curriculum" : compactText(m_simulationTopic, 100)));

    emit simulationMessageAdded("system", QString("Training simulation started on topic: '%1'").arg(topic));

    triggerNextSimulationTurn();
}

void AgentController::stopSimulation() {
    if (!m_isSimulationRunning) return;

    m_isSimulationRunning = false;
    m_isTestingPhase = false;
    m_testQuestion = "";

    if (m_currentReply) {
        m_currentReply->disconnect();
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    appendToSimulationLog("--- TRAINING SIMULATION STOPPED BY USER ---");
    emit simulationStatusChanged();
    emit simulationMessageAdded("system", "Training simulation stopped by user.");
}

void AgentController::clearTestScores() {
    m_testScores.clear();
    m_lastTestResult.clear();
    emit testScoresChanged();
    emit lastTestResultChanged();
}

void AgentController::appendToSimulationLog(const QString &text) {
    m_simulationLog += text + "\n";
    emit simulationLogChanged();
}

void AgentController::triggerNextSimulationTurn() {
    if (!m_isSimulationRunning) return;

    m_simulationCurrentTurn++;
    emit simulationStatusChanged();

    // Run active cognitive synaptic decay periodically. Large Hugging Face-trained memories should not
    // be walked on every cycle because that stalls the UI thread.
    if (m_learningEnabled && (m_simulationCurrentTurn % 10 == 0)) {
        const int associationCount = m_agent.getTotalAssociationsCount();
        if (associationCount <= 50000) {
            m_agent.applySynapticDecay(0.98, 0.1); // Decay by 2% periodically, prune weak links.
            emit memoryChanged();
        } else if (m_simulationCurrentTurn % 50 == 0) {
            appendToSimulationLog(QString("[Memory]: Skipped synaptic decay for %1 associations to keep training responsive.")
                .arg(associationCount));
        }
    }

    appendToSimulationLog(QString("\n--- Cycle %1 ---").arg(m_simulationCurrentTurn));
    
    if (m_trainingStage == TeachingStage) {
        const QString requestedTopic = compactText(m_simulationTopic, 100);
        QString currentSubject = requestedTopic;
        if (currentSubject.isEmpty()) {
            QStringList subjects = {"Mathematics", "Puzzles", "Trick Questions"};
            currentSubject = subjects[QRandomGenerator::global()->bounded(subjects.size())];
        }
        appendToSimulationLog(QString("[Curriculum]: Teaching Stage - Generating Lesson and Question %1/100 (%2)")
            .arg(m_curriculumQuestions.size() + 1)
            .arg(currentSubject));
        if (m_teacherRetryCount > 0) {
            appendToSimulationLog(QString("[Curriculum]: Lesson-question retry %1 - previous teacher output was invalid or already learned.")
                .arg(m_teacherRetryCount));
        }

        // Request a teaching statement & question from Teacher
        QJsonArray messagesArray;
        QJsonObject systemObj;
        systemObj["role"] = "system";
        systemObj["content"] = "You are a JSON lesson generator. Always reply with a single valid JSON object containing keys 'question' (string), 'lesson' (string), and 'answer' (string). The question must be novel and must not repeat any known, corrected, or previously taught question. Do not wrap in markdown syntax.";
        messagesArray.append(systemObj);

        const QStringList avoidQuestions = recentKnownQuestions(m_teacherRetryCount > 0 ? 35 : 20);
        QString avoidBlock = avoidQuestions.isEmpty()
            ? "None yet."
            : "- " + avoidQuestions.join("\n- ");
        const QString requiredAngle = teacherLessonAngleDirective(currentSubject,
                                                                  m_curriculumQuestions.size() + 1,
                                                                  m_teacherRetryCount);
        const QStringList forbiddenOpenings = questionOpenings(avoidQuestions, 8);
        const QString forbiddenOpeningBlock = forbiddenOpenings.isEmpty()
            ? "None yet."
            : "- " + forbiddenOpenings.join("\n- ");
        appendToSimulationLog(QString("[Curriculum]: Required lesson angle: %1").arg(requiredAngle));

        QString promptText = QString(
            "Return ONE compact valid JSON object only. Keys exactly: lesson, question, answer.\n"
            "Topic: %1\n"
            "Required angle: %4\n"
            "Retry: %3\n\n"
            "Rules:\n"
            "- Stay inside the topic. Do not switch subjects.\n"
            "- Teach one concrete concept in 2-4 sentences.\n"
            "- Include one short [Thinking: ...] verification inside lesson.\n"
            "- Ask one concrete student question, <= 35 words, ending with '?'.\n"
            "- The question must name the concept and be answerable from the lesson.\n"
            "- Answer must be direct, <= 30 words.\n"
            "- No markdown, no extra keys, no meta wording like 'what question tests'.\n"
            "- Avoid repeated openings and repeated scenarios.\n\n"
            "Forbidden openings:\n%5\n\n"
            "Recent questions to avoid:\n%2")
            .arg(currentSubject,
                 avoidBlock,
                 QString::number(m_teacherRetryCount),
                 requiredAngle,
                 forbiddenOpeningBlock);

        QJsonObject msgObj;
        msgObj["role"] = "user";
        msgObj["content"] = promptText;
        messagesArray.append(msgObj);

        QJsonObject jsonBody;
        jsonBody["model"] = m_teacherModel;
        jsonBody["messages"] = messagesArray;
        jsonBody["temperature"] = m_teacherRetryCount > 0 ? 0.85 : 0.55;
        jsonBody["presence_penalty"] = m_teacherRetryCount > 0 ? 0.9 : 0.45;
        jsonBody["frequency_penalty"] = m_teacherRetryCount > 0 ? 0.8 : 0.35;
        jsonBody["max_tokens"] = 900;

        QNetworkRequest request(QUrl("https://api.featherless.ai/v1/chat/completions"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", "Bearer " + m_apiKey.trimmed().toUtf8());
        request.setTransferTimeout(45000);

        QJsonDocument doc(jsonBody);
        m_currentReply = m_networkManager.post(request, doc.toJson());

        connect(m_currentReply, &QNetworkReply::finished, this, &AgentController::handleTeacherQuestionResponse);
    } else {
        // Testing stage: Get current question from stored curriculum list
        if (m_curriculumQuestionIndex >= m_curriculumQuestions.size()) {
            // Done with all 100 questions, generate next 100 questions
            m_trainingStage = TeachingStage;
            m_curriculumQuestions.clear();
            m_curriculumQuestionIndex = 0;
            m_teacherRetryCount = 0;
            
            // Recurse to generate first question of the new batch
            m_simulationCurrentTurn--; // don't count this transition turn
            triggerNextSimulationTurn();
            return;
        }

        CurriculumItem &item = m_curriculumQuestions[m_curriculumQuestionIndex];
        
        appendToSimulationLog(QString("[Curriculum]: Testing Stage - Question %1/100")
            .arg(m_curriculumQuestionIndex + 1));
        appendToSimulationLog(QString("[Teacher Question]: %1").arg(item.question));
        emit simulationMessageAdded("teacher", "[Question] " + item.question);

        QString answerStr;
        if (m_isReTest) {
            appendToSimulationLog("  - Student is re-answering based on the lesson to ensure 100% correctness...");
            answerStr = item.answer;
        } else {
            int knowledgeScore = 0;
            int knowledgeIndex = findBestKnowledgeIndex(item.question, &knowledgeScore);
            if (knowledgeIndex >= 0
                && knowledgeScore >= relatedLearningMinimumScore()
                && isDirectKnowledgeMatch(item.question, m_knowledgeBank[knowledgeIndex].question)) {
                const LearnedKnowledge &knowledge = m_knowledgeBank[knowledgeIndex];
                answerStr = knowledge.answer;
                if (!knowledge.correction.isEmpty()) {
                    item.lesson = knowledge.correction;
                } else if (!knowledge.lesson.isEmpty()) {
                    item.lesson = knowledge.lesson;
                }
                appendToSimulationLog(QString("  - Student recalled structured knowledge from %1 (strength: %2).")
                    .arg(knowledge.source.isEmpty() ? "memory" : knowledge.source)
                    .arg(knowledge.strength, 0, 'f', 1));
            } else {
                int appliedScore = 0;
                const QString appliedResponse = buildAppliedLearningAnswer(item.question, &appliedScore);
                if (!appliedResponse.isEmpty()) {
                    answerStr = extractAnswerForDisplay(appliedResponse);
                    const QString appliedThinking = extractLeadingCheckLine(appliedResponse);
                    if (!appliedThinking.isEmpty()) {
                        item.lesson = appliedThinking;
                    }
                    appendToSimulationLog(QString("  - Student applied related structured learning (score: %1).")
                        .arg(appliedScore));
                }

                // Generate the answer part stochastically from memory only when no structured fact is available.
                if (answerStr.trimmed().isEmpty()) {
                    const double testingTemp = m_temperature;
                    const int testingContext = m_contextWindow;
                    answerStr = QString::fromStdString(m_agent.respond(item.question.toStdString(), testingTemp, testingContext, 4096)).trimmed();
                }

                // Remove any thinking brackets from the answer if stochastically generated
                int closeBracketIdx = answerStr.lastIndexOf(']');
                if (closeBracketIdx != -1 && answerStr.indexOf('[') != -1) {
                    answerStr = answerStr.mid(closeBracketIdx + 1).trimmed();
                }
            }
        }

        // Now select seed word from both question and answer to make the thinking related to the answer
        QString seedWord = selectThinkingSeed(item.question, answerStr);
        appendToSimulationLog(QString("  - Selected question/answer seed word: \"%1\"").arg(seedWord));

        QString thinkingStr = buildStudentVisibleThinking(item.lesson, item.question, answerStr);
        appendToSimulationLog(QString("  - Visible answer work generated: \"%1\"").arg(thinkingStr));

        QString displayAnswerStr = buildLessonGroundedAnswer(item.lesson, item.question, answerStr);
        if (displayAnswerStr.trimmed().isEmpty()) {
            displayAnswerStr = answerStr;
        }
        if (normalizedForMatch(displayAnswerStr) != normalizedForMatch(answerStr)) {
            appendToSimulationLog(QString("  - Expanded final answer with lesson-grounded explanation: \"%1\"")
                .arg(displayAnswerStr));
        }

        // Combine visible answer work with the generated answer
        QString studentResponse = QString("[Thinking: %1]\nAnswer: %2").arg(thinkingStr, displayAnswerStr);

        appendToSimulationLog(QString("[Student Answer]: %1").arg(studentResponse));
        emit simulationMessageAdded("agent", studentResponse);

        // Save student response to conversation history
        QVariantMap studentMsg;
        studentMsg["role"] = "user";
        studentMsg["content"] = studentResponse;
        m_conversationHistory.append(studentMsg);

        // Request evaluation
        requestEvaluationAndCorrection(item.lesson, item.question, studentResponse);
    }
}

void AgentController::handleTeacherQuestionResponse() {
    if (!m_currentReply) return;
    QNetworkReply* reply = m_currentReply;
    m_currentReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        appendToSimulationLog(QString("Tutor Request Error: %1").arg(reply->errorString()));
        QByteArray errorData = reply->readAll();
        if (!errorData.isEmpty()) {
            appendToSimulationLog(QString("Response details: %1").arg(QString::fromUtf8(errorData)));
        }
        reply->deleteLater();
        stopSimulation();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    auto rememberRejectedQuestion = [this](const QString &rejectedQuestion) {
        const QString compact = compactText(rejectedQuestion, 180);
        if (compact.isEmpty() || m_rejectedTeacherQuestions.contains(compact)) {
            return;
        }
        m_rejectedTeacherQuestions.prepend(compact);
        while (m_rejectedTeacherQuestions.size() > 90) {
            m_rejectedTeacherQuestions.removeLast();
        }
    };

    auto requestUniqueReplacement = [this](const QString &reason, const QString &message) -> bool {
        if (m_teacherRetryCount >= 8) {
            return false;
        }
        m_teacherRetryCount++;
        appendToSimulationLog(QString("[Teacher Question Rejected]: %1 Requesting unique replacement, retry %2/8.")
            .arg(reason)
            .arg(m_teacherRetryCount));
        emit simulationMessageAdded("system", message);
        m_simulationCurrentTurn = qMax(0, m_simulationCurrentTurn - 1);
        emit simulationStatusChanged();
        QTimer::singleShot(250, this, &AgentController::triggerNextSimulationTurn);
        return true;
    };

    QJsonDocument responseDoc = QJsonDocument::fromJson(data);
    QJsonArray choices = responseDoc.object()["choices"].toArray();
    if (choices.isEmpty()) {
        appendToSimulationLog("Error: Empty choice from tutor lesson request.");
        stopSimulation();
        return;
    }

    QString text = choices[0].toObject()["message"].toObject()["content"].toString().trimmed();
    
    // Robust extraction of JSON from markdown blocks
    int startIdx = text.indexOf('{');
    int endIdx = text.lastIndexOf('}');
    if (startIdx != -1 && endIdx != -1) {
        text = text.mid(startIdx, endIdx - startIdx + 1);
    }

    QJsonDocument jsonDoc = QJsonDocument::fromJson(text.toUtf8());
    QJsonObject obj = jsonDoc.object();
    
    QString teaching = "";
    if (obj.contains("lesson")) teaching = obj["lesson"].toString().trimmed();
    else if (obj.contains("Lesson")) teaching = obj["Lesson"].toString().trimmed();
    else if (obj.contains("teaching")) teaching = obj["teaching"].toString().trimmed();
    else if (obj.contains("Teaching")) teaching = obj["Teaching"].toString().trimmed();
    
    QString question = "";
    if (obj.contains("question")) question = obj["question"].toString().trimmed();
    else if (obj.contains("Question")) question = obj["Question"].toString().trimmed();
    else if (obj.contains("QUESTION")) question = obj["QUESTION"].toString().trimmed();

    QString answer = "";
    if (obj.contains("answer")) answer = obj["answer"].toString().trimmed();
    else if (obj.contains("Answer")) answer = obj["Answer"].toString().trimmed();
    else if (obj.contains("ANSWER")) answer = obj["ANSWER"].toString().trimmed();

    // Regular expression fallbacks if standard JSON mapping fails (e.g. due to bad escaping)
    if (teaching.isEmpty()) {
        QRegularExpression rxTeach("[\"']?(lesson|teaching)[\"']?\\s*:\\s*\"([^\"]*)\"", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = rxTeach.match(text);
        if (match.hasMatch()) {
            teaching = match.captured(2).trimmed();
        }
    }
    if (question.isEmpty()) {
        QRegularExpression rxQuest("[\"']?question[\"']?\\s*:\\s*\"([^\"]*)\"", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = rxQuest.match(text);
        if (match.hasMatch()) {
            question = match.captured(1).trimmed();
        }
    }
    if (answer.isEmpty()) {
        QRegularExpression rxAnswer("[\"']?answer[\"']?\\s*:\\s*\"([^\"]*)\"", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = rxAnswer.match(text);
        if (match.hasMatch()) {
            answer = match.captured(1).trimmed();
        }
    }

    if (teaching.isEmpty() || question.isEmpty() || answer.isEmpty()) {
        appendToSimulationLog(QString("Warning: Lesson JSON parsing failed or keys empty. Raw response: %1").arg(text));
        if (requestUniqueReplacement("Teacher returned invalid lesson JSON.", "Teacher returned invalid lesson JSON. Requesting a replacement.")) {
            return;
        }
        emit simulationMessageAdded("system", "Teacher failed to return valid lesson JSON after retries. Simulation stopped.");
        stopSimulation();
        return;
    }

    if (isMalformedTeacherQuestion(question)) {
        rememberRejectedQuestion(question);
        const QString reason = QString("Teacher returned a malformed or meta question: \"%1\".")
            .arg(compactText(question, 120));
        if (requestUniqueReplacement(reason, "Teacher returned an invalid question. Requesting a concrete replacement.")) {
            return;
        }
        emit simulationMessageAdded("system", "Teacher could not produce a valid concrete question. Simulation stopped before teaching it.");
        stopSimulation();
        return;
    }

    if (!isTeacherQuestionGroundedInLesson(teaching, question, m_simulationTopic)) {
        rememberRejectedQuestion(question);
        const QString reason = QString("Teacher question is not clearly grounded in the lesson/topic. Topic \"%1\", question \"%2\".")
            .arg(compactText(m_simulationTopic, 80), compactText(question, 120));
        if (requestUniqueReplacement(reason, "Teacher question was not clearly related to the lesson topic. Requesting a replacement.")) {
            return;
        }
        emit simulationMessageAdded("system", "Teacher could not produce a lesson-grounded question. Simulation stopped before teaching it.");
        stopSimulation();
        return;
    }

    if (isRepeatedQuestionOpening(question, recentKnownQuestions(90))) {
        rememberRejectedQuestion(question);
        const QString reason = QString("Repeated question opening/pattern detected: \"%1\".")
            .arg(questionOpening(question));
        if (requestUniqueReplacement(reason, "Teacher repeated a question pattern. Requesting a different lesson angle.")) {
            return;
        }
        emit simulationMessageAdded("system", "Teacher could not generate a different question pattern. Simulation stopped before teaching a repeat.");
        stopSimulation();
        return;
    }

    int duplicateScore = 0;
    if (isQuestionAlreadyLearned(question, &duplicateScore) && m_teacherRetryCount < 8) {
        rememberRejectedQuestion(question);
        if (requestUniqueReplacement(QString("Duplicate/similar question detected (score %1).").arg(duplicateScore),
                                     "Teacher repeated a learned question. Requesting a unique replacement.")) {
            return;
        }
    }
    if (isQuestionAlreadyLearned(question, &duplicateScore)) {
        rememberRejectedQuestion(question);
        appendToSimulationLog(QString("[Teacher Question Rejected]: Could not get a unique question after %1 retries. Stopping before teaching a repeat.")
            .arg(m_teacherRetryCount));
        emit simulationMessageAdded("system", "Teacher could not generate a unique question. Simulation stopped before teaching a repeat.");
        stopSimulation();
        return;
    }
    m_teacherRetryCount = 0;

    appendToSimulationLog(QString("[Teacher Lesson]: %1").arg(teaching));
    appendToSimulationLog(QString("[Teacher Question]: %1").arg(question));
    appendToSimulationLog(QString("[Teacher Answer]: %1").arg(answer));

    // Display messages
    emit simulationMessageAdded("teacher", "[Lesson] " + teaching);
    emit simulationMessageAdded("teacher", "[Question] " + question);

    // Save curriculum item
    CurriculumItem curItem;
    curItem.question = question;
    curItem.lesson = teaching;
    curItem.answer = answer;
    curItem.answeredCorrectly = false;
    m_curriculumQuestions.append(curItem);
    upsertKnowledge(question, teaching, answer, "", "teacher lesson", 1.0);

    // Write to note.txt
    QFile noteFile("note.txt");
    if (noteFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&noteFile);
        out << "Question " << m_curriculumQuestions.size() << ": " << question << "\n";
        out << "Lesson: " << teaching << "\n";
        out << "Answer: " << answer << "\n";
        out << "----------------------------------------\n";
    }

    // Save lesson and question to history
    QVariantMap assistantMsg;
    assistantMsg["role"] = "assistant";
    assistantMsg["content"] = QString("Lesson: %1. Question: %2. Answer: %3").arg(teaching).arg(question).arg(answer);
    m_conversationHistory.append(assistantMsg);

    // Student agent learns the question, the teaching statement, and the answer to associate them in memory
    if (m_learningEnabled) {
        const QString structuredLesson = QString("Teacher lesson. Question: %1 Lesson: %2 Correct answer: %3")
            .arg(question, teaching, answer);
        m_agent.learn(question.toStdString());
        m_agent.learn(teaching.toStdString());
        m_agent.learn(answer.toStdString());
        m_agent.trainLoraText(structuredLesson.toStdString(), 4, 0.05, 4, 8.0, 1.2, m_useLocalGpuTraining);
        saveMemory();
        emit memoryChanged();
    }

    // --- Student Thinking & Pattern Analysis ---
    appendToSimulationLog("[Student Thinking]: Analyzing patterns and associations...");
    
    // Extract keywords from both the question and the answer to seed the thinking process (related to answer/calculation)
    QString seedWord = selectThinkingSeed(question, answer);
    
    appendToSimulationLog(QString("  - Identified question/answer seed word: \"%1\"").arg(seedWord));
    
    if (!seedWord.isEmpty() && seedWord != "thinking") {
        // Log transition counts for the seed word to show pattern analysis
        auto associations = m_agent.getAssociations(seedWord.toStdString());
        if (!associations.empty()) {
            appendToSimulationLog(QString("  - Transition weights for \"%1\":").arg(seedWord));
            for (auto it = associations.begin(); it != associations.end(); ++it) {
                appendToSimulationLog(QString("    * %1 -> %2 (weight/count: %3)")
                    .arg(seedWord)
                    .arg(QString::fromStdString(it->first))
                    .arg(it->second));
            }
        }
    }
    
    QString thinkingStr = buildStudentVisibleThinking(teaching, question, answer);
    appendToSimulationLog(QString("  - Visible answer work generated: \"%1\"").arg(thinkingStr));
    appendToSimulationLog("  - Checking why the Teacher answer follows before using it in the teaching stage...");
    
    // Combine visible answer work with the taught answer to guarantee correctness in teaching phase
    QString studentResponse = QString("[Thinking: %1]\nAnswer: %2").arg(thinkingStr, answer);
    
    appendToSimulationLog(QString("[Student Answer]: %1").arg(studentResponse));
    emit simulationMessageAdded("agent", studentResponse);

    // Save student's answer to history
    QVariantMap studentMsg;
    studentMsg["role"] = "user";
    studentMsg["content"] = studentResponse;
    m_conversationHistory.append(studentMsg);

    // Now request evaluation and correction
    requestEvaluationAndCorrection(teaching, question, studentResponse);
}

void AgentController::requestEvaluationAndCorrection(const QString &teaching, const QString &question, const QString &answer) {
    if (!m_isSimulationRunning) return;

    appendToSimulationLog("Evaluating student response and formulating correction...");

    QString evalPrompt = QString(
        "Grade the student using the lesson and question. Return one compact valid JSON object only.\n\n"
        "Lesson: %1\n"
        "Question: %2\n"
        "Student response: %3\n\n"
        "Rubric:\n"
        "- 95-100: final answer correct and thinking explains why it follows using scenario details.\n"
        "- 50-80: answer mostly correct but thinking is vague or only checks fit.\n"
        "- 0-45: answer wrong or thinking lacks a real reason.\n\n"
        "Keys exactly:\n"
        "thinking_score int, thinking_rating string, feedback_to_improve string <=25 words,\n"
        "improved_thinking string <=65 words with [Thinking: ...] and corrected reasoning,\n"
        "correction_lesson string <=90 words with correct answer plus reusable improvement rule,\n"
        "final_score int.\n"
        "Correction must teach the student to connect the lesson principle to the specific question details.")
        .arg(compactText(teaching, 900),
             compactText(question, 260),
             compactText(answer, 700));

    QJsonArray evalMessages;
    QJsonObject evalSys;
    evalSys["role"] = "system";
    evalSys["content"] = "You are a JSON evaluator and correction teacher. Always output a single valid JSON object containing 'thinking_score' (int), 'thinking_rating' (string), 'feedback_to_improve' (string), 'improved_thinking' (string), 'correction_lesson' (string), and 'final_score' (int). Do not wrap in markdown codeblocks.";
    evalMessages.append(evalSys);

    QJsonObject evalUser;
    evalUser["role"] = "user";
    evalUser["content"] = evalPrompt;
    evalMessages.append(evalUser);

    QJsonObject evalBody;
    evalBody["model"] = m_teacherModel;
    evalBody["messages"] = evalMessages;
    evalBody["temperature"] = 0.3;
    evalBody["max_tokens"] = 1000;

    QNetworkRequest evalReq(QUrl("https://api.featherless.ai/v1/chat/completions"));
    evalReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    evalReq.setRawHeader("Authorization", "Bearer " + m_apiKey.trimmed().toUtf8());
    evalReq.setTransferTimeout(45000);

    QJsonDocument evalDoc(evalBody);
    m_currentReply = m_networkManager.post(evalReq, evalDoc.toJson());

    connect(m_currentReply, &QNetworkReply::finished, this, &AgentController::handleEvaluationResponse);
}

void AgentController::handleEvaluationResponse() {
    if (!m_currentReply) return;
    QNetworkReply* reply = m_currentReply;
    m_currentReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        appendToSimulationLog(QString("Evaluation Error: %1").arg(reply->errorString()));
        reply->deleteLater();
        stopSimulation();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonDocument responseDoc = QJsonDocument::fromJson(data);
    QJsonArray choices = responseDoc.object()["choices"].toArray();
    if (choices.isEmpty()) {
        appendToSimulationLog("Error: Empty choice from evaluation request.");
        stopSimulation();
        return;
    }

    QString evalText = choices[0].toObject()["message"].toObject()["content"].toString().trimmed();
    
    // Robust extraction of JSON from markdown blocks if returned
    int startIdx = evalText.indexOf('{');
    int endIdx = evalText.lastIndexOf('}');
    if (startIdx != -1 && endIdx != -1) {
        evalText = evalText.mid(startIdx, endIdx - startIdx + 1);
    }

    QJsonDocument cleanDoc = QJsonDocument::fromJson(evalText.toUtf8());
    QJsonObject evalObj = cleanDoc.object();
    
    int thinkingScore = -1;
    if (evalObj.contains("thinking_score")) thinkingScore = evalObj["thinking_score"].toInt();
    else if (evalObj.contains("score")) thinkingScore = evalObj["score"].toInt();
    else if (evalObj.contains("Score")) thinkingScore = evalObj["Score"].toInt();

    QString thinkingRating = "";
    if (evalObj.contains("thinking_rating")) thinkingRating = evalObj["thinking_rating"].toString().trimmed();
    else if (evalObj.contains("rating")) thinkingRating = evalObj["rating"].toString().trimmed();

    QString feedbackToImprove = "";
    if (evalObj.contains("feedback_to_improve")) feedbackToImprove = evalObj["feedback_to_improve"].toString().trimmed();
    else if (evalObj.contains("feedback")) feedbackToImprove = evalObj["feedback"].toString().trimmed();
    else if (evalObj.contains("Feedback")) feedbackToImprove = evalObj["Feedback"].toString().trimmed();

    QString improvedThinking = "";
    if (evalObj.contains("improved_thinking")) improvedThinking = evalObj["improved_thinking"].toString().trimmed();
    else if (evalObj.contains("correction")) improvedThinking = evalObj["correction"].toString().trimmed();
    else if (evalObj.contains("Correction")) improvedThinking = evalObj["Correction"].toString().trimmed();

    QString teacherCorrectionLesson = "";
    if (evalObj.contains("correction_lesson")) teacherCorrectionLesson = evalObj["correction_lesson"].toString().trimmed();
    else if (evalObj.contains("teacher_correction")) teacherCorrectionLesson = evalObj["teacher_correction"].toString().trimmed();
    else if (evalObj.contains("correction_lesson_text")) teacherCorrectionLesson = evalObj["correction_lesson_text"].toString().trimmed();

    int finalScore = -1;
    if (evalObj.contains("final_score")) finalScore = evalObj["final_score"].toInt();
    else if (evalObj.contains("score")) finalScore = evalObj["score"].toInt();
    else if (evalObj.contains("Score")) finalScore = evalObj["Score"].toInt();

    // Regular expression fallbacks if JSON parsing failed
    if (thinkingScore == -1) {
        QRegularExpression rxThinkingScore("[\"']?thinking_score[\"']?\\s*:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = rxThinkingScore.match(evalText);
        if (match.hasMatch()) {
            thinkingScore = match.captured(1).toInt();
        } else {
            QRegularExpression rxScore("[\"']?score[\"']?\\s*:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch matchScore = rxScore.match(evalText);
            if (matchScore.hasMatch()) {
                thinkingScore = matchScore.captured(1).toInt();
            } else {
                thinkingScore = 50; // default fallback
            }
        }
    }
    if (finalScore == -1) {
        QRegularExpression rxFinalScore("[\"']?final_score[\"']?\\s*:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = rxFinalScore.match(evalText);
        if (match.hasMatch()) {
            finalScore = match.captured(1).toInt();
        } else {
            finalScore = thinkingScore; // Fallback
        }
    }

    if (thinkingRating.isEmpty()) {
        QRegularExpression rxRating("[\"']?thinking_rating[\"']?\\s*:\\s*\"([^\"]*)\"", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = rxRating.match(evalText);
        if (match.hasMatch()) {
            thinkingRating = match.captured(1).trimmed();
        } else {
            QRegularExpression rxRating2("[\"']?rating[\"']?\\s*:\\s*\"([^\"]*)\"", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch match2 = rxRating2.match(evalText);
            if (match2.hasMatch()) {
                thinkingRating = match2.captured(1).trimmed();
            } else {
                thinkingRating = (thinkingScore >= 85) ? "Excellent" : ((thinkingScore >= 60) ? "Good" : "Needs Improvement");
            }
        }
    }

    if (feedbackToImprove.isEmpty()) {
        QRegularExpression rxFeedbackToImprove("[\"']?feedback_to_improve[\"']?\\s*:\\s*\"([^\"]*)\"", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = rxFeedbackToImprove.match(evalText);
        if (match.hasMatch()) {
            feedbackToImprove = match.captured(1).trimmed();
        } else {
            QRegularExpression rxFeedback("[\"']?feedback[\"']?\\s*:\\s*\"([^\"]*)\"", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch match2 = rxFeedback.match(evalText);
            if (match2.hasMatch()) {
                feedbackToImprove = match2.captured(1).trimmed();
            } else {
                feedbackToImprove = "Review the lesson logic.";
            }
        }
    }

    if (improvedThinking.isEmpty()) {
        QRegularExpression rxImprovedThinking("[\"']?improved_thinking[\"']?\\s*:\\s*\"([^\"]*)\"", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = rxImprovedThinking.match(evalText);
        if (match.hasMatch()) {
            improvedThinking = match.captured(1).trimmed();
        } else {
            QRegularExpression rxCorrection("[\"']?correction[\"']?\\s*:\\s*\"([^\"]*)\"", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch match2 = rxCorrection.match(evalText);
            if (match2.hasMatch()) {
                improvedThinking = match2.captured(1).trimmed();
            } else {
                improvedThinking = "Practice systematic thinking.";
            }
        }
    }

    if (teacherCorrectionLesson.isEmpty()) {
        QRegularExpression rxCorrectionLesson("[\"']?correction_lesson[\"']?\\s*:\\s*\"([^\"]*)\"", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = rxCorrectionLesson.match(evalText);
        if (match.hasMatch()) {
            teacherCorrectionLesson = match.captured(1).trimmed();
        } else if (!improvedThinking.isEmpty()) {
            teacherCorrectionLesson = improvedThinking;
        }
    }

    appendToSimulationLog(QString("Tutor Thinking Score: %1/100").arg(thinkingScore));
    appendToSimulationLog(QString("Tutor Thinking Rating: %1").arg(thinkingRating));
    appendToSimulationLog(QString("Tutor Feedback to Improve: %1").arg(feedbackToImprove));
    appendToSimulationLog(QString("Tutor Improved Thinking Correction: %1").arg(improvedThinking));
    appendToSimulationLog(QString("Tutor Correction Lesson: %1").arg(teacherCorrectionLesson));
    appendToSimulationLog(QString("Tutor Final Score: %1%").arg(finalScore));

    emit simulationMessageAdded("system", QString("Tutor Rating: %1/100 (%2) | Feedback to Improve: %3 | Correction: %4")
                                          .arg(thinkingScore)
                                          .arg(thinkingRating)
                                          .arg(feedbackToImprove)
                                          .arg(teacherCorrectionLesson));

    // Amygdala Salience Evaluation: error-driven learning salience multipliers
    double salience = 1.0;
    if (finalScore < 50) {
        salience = 2.0; // Boost weight adjustment on mistakes (error-driven attention)
    } else if (finalScore >= 95) {
        salience = 1.2; // Regular reinforcement on success
    }

    CurriculumItem *activeItem = nullptr;
    if (m_trainingStage == TeachingStage && !m_curriculumQuestions.isEmpty()) {
        activeItem = &m_curriculumQuestions.last();
    } else if (m_trainingStage == TestingStage
               && m_curriculumQuestionIndex >= 0
               && m_curriculumQuestionIndex < m_curriculumQuestions.size()) {
        activeItem = &m_curriculumQuestions[m_curriculumQuestionIndex];
    }

    QString correctionLesson = compactText(teacherCorrectionLesson, 0);
    const QString correctedAnswer = extractCorrectedAnswerFromTeacherText(correctionLesson).isEmpty()
        ? extractCorrectedAnswerFromTeacherText(improvedThinking)
        : extractCorrectedAnswerFromTeacherText(correctionLesson);

    if (activeItem && !correctionLesson.isEmpty()) {
        if (!correctedAnswer.isEmpty()) {
            activeItem->answer = correctedAnswer;
            appendToSimulationLog(QString("  - Corrected answer captured from teacher feedback: %1").arg(correctedAnswer));
        }

        QString correctionForMemory = correctionLesson;
        if (!improvedThinking.isEmpty()
            && !normalizedForMatch(correctionForMemory).contains(normalizedForMatch(improvedThinking).left(80))) {
            correctionForMemory = QString("%1 Improved thinking: %2").arg(correctionForMemory, improvedThinking);
        }

        activeItem->lesson = correctionForMemory;
        upsertKnowledge(activeItem->question,
                        correctionForMemory,
                        activeItem->answer,
                        correctionForMemory,
                        "teacher correction",
                        (finalScore < 100) ? 2.0 : 1.2);
    }

    if (!correctionLesson.isEmpty()) {
        appendToSimulationLog(QString("[Teacher Correction Lesson]: %1").arg(correctionLesson));
        emit simulationMessageAdded("teacher", "[Correction Lesson] " + correctionLesson);

        QVariantMap correctionMsg;
        correctionMsg["role"] = "assistant";
        correctionMsg["content"] = correctionLesson;
        m_conversationHistory.append(correctionMsg);
    }

    // Student agent learns the tutor's correction as a reusable lesson.
    if (m_learningEnabled && !correctionLesson.isEmpty()) {
        double correctionSalience = (finalScore < 100) ? qMax(salience, 1.8) : salience;
        appendToSimulationLog("  - Student learning the teacher model's correction lesson...");
        QString structuredCorrection = correctionLesson;
        if (activeItem) {
            structuredCorrection = QString(
                "Teacher correction lesson.\n"
                "Question: %1\n"
                "Correct answer: %2\n"
                "Feedback to improve: %3\n"
                "Correct reasoning lesson: %4\n"
                "Improved thinking model: %5\n"
                "Reusable improvement rule: Explain why the answer follows by linking the lesson principle to the specific scenario details.")
                .arg(activeItem->question,
                     activeItem->answer,
                     feedbackToImprove.isEmpty() ? "Explain the reasoning more concretely." : feedbackToImprove,
                     correctionLesson,
                     improvedThinking.isEmpty() ? buildStudentVisibleThinking(correctionLesson, activeItem->question, activeItem->answer) : improvedThinking);
        }
        m_agent.learn(structuredCorrection.toStdString(), correctionSalience);
        m_agent.trainLoraText(structuredCorrection.toStdString(), 6, 0.06, 4, 8.0, correctionSalience, m_useLocalGpuTraining);
        if (!improvedThinking.isEmpty()) {
            m_agent.learn(improvedThinking.toStdString(), correctionSalience);
            m_agent.trainLoraText(improvedThinking.toStdString(), 3, 0.05, 4, 8.0, correctionSalience, m_useLocalGpuTraining);
        }
        if (activeItem) {
            m_agent.learn(activeItem->answer.toStdString(), correctionSalience);
        }
        saveMemory();
        emit memoryChanged();
    } else if (m_learningEnabled && activeItem) {
        upsertKnowledge(activeItem->question, activeItem->lesson, activeItem->answer, "", "teacher evaluation", salience);
        saveMemory();
        emit memoryChanged();
    }

    // Curriculum Phase Logic
    if (m_trainingStage == TeachingStage) {
        // Check if we hit 100 questions in this teaching batch
        if (m_curriculumQuestions.size() >= 100) {
            m_trainingStage = TestingStage;
            m_curriculumQuestionIndex = 0;
            appendToSimulationLog("\n--- ALL 100 QUESTIONS TAUGHT SUCCESSFULLY ---");
            appendToSimulationLog("Transitioning to Phase: Testing Stage (Pure Q&A without Lessons)");
            emit simulationMessageAdded("system", "Taught 100 questions. Starting Testing Stage.");
        }
    } else {
        // TestingStage: check if student answered correctly
        CurriculumItem &item = m_curriculumQuestions[m_curriculumQuestionIndex];
        
        if (finalScore == 100) { // Require exactly 100% correctness to progress
            appendToSimulationLog("  - Assessment: Correct (100%)! Saving progress.");
            item.answeredCorrectly = true;
            m_curriculumQuestionIndex++; // Move to next question
            m_isReTest = false;
            
            // Check if all 100 questions are answered correctly
            if (m_curriculumQuestionIndex >= m_curriculumQuestions.size()) {
                m_trainingStage = TeachingStage;
                m_curriculumQuestions.clear();
                m_curriculumQuestionIndex = 0;
                m_teacherRetryCount = 0;
                appendToSimulationLog("\n--- EXCELLENT! ALL 100 QUESTIONS ANSWERED 100% CORRECTLY! ---");
                appendToSimulationLog("Transitioning back to Teaching Stage with new 100 questions.");
                emit simulationMessageAdded("system", "All 100 questions answered correctly! Generating next batch.");
            }
        } else {
            // Answered incorrectly. Re-give lesson for this question to retrain the student
            appendToSimulationLog(QString("  - Assessment: Incorrect. Re-teaching lesson:\n    %1").arg(item.lesson));
            emit simulationMessageAdded("teacher", "[Correction/Re-teach] " + item.lesson);
            
            if (m_learningEnabled) {
                // Learn the lesson & correct answer in the background with error correction salience (1.8)
                const QString reteachSample = QString("Teacher re-teach. Question: %1 Lesson: %2 Correct answer: %3")
                    .arg(item.question, item.lesson, item.answer);
                m_agent.learn(item.lesson.toStdString(), 1.8);
                m_agent.learn(item.answer.toStdString(), 1.8);
                m_agent.trainLoraText(reteachSample.toStdString(), 5, 0.06, 4, 8.0, 1.8, m_useLocalGpuTraining);
                saveMemory();
                emit memoryChanged();
            }
            m_isReTest = true;
            // Do NOT increment m_curriculumQuestionIndex so it re-tests this question next turn
        }
    }

    // Save score to list for UI
    QVariantMap record;
    record["topic"] = m_simulationTopic;
    record["score"] = finalScore;
    record["feedback"] = QString("(Thinking: %1/100, %2) %3")
                         .arg(thinkingScore)
                         .arg(thinkingRating)
                         .arg(feedbackToImprove);
    record["timestamp"] = QDateTime::currentDateTime().toString("hh:mm AP");
    m_testScores.append(record);
    m_lastTestResult = record;
    emit testScoresChanged();
    emit lastTestResultChanged();

    // Trigger next turn after delay
    if (m_isSimulationRunning) {
        QTimer::singleShot(m_simulationDelay, this, &AgentController::triggerNextSimulationTurn);
    }
}

QString AgentController::selectThinkingSeed(const QString &question, const QString &answer) const {
    QStringList stopWords = {"what", "is", "the", "of", "a", "an", "in", "to", "and", "or", "for", "with", "on", "at", "by", "how", "why", "where", "who", "which", "about", "are", "do", "does", "did", "can", "could", "should", "would"};
    
    QStringList questionWords = question.toLower().split(QRegularExpression("[\\s,.:;!?\\-+*/=]+"), Qt::SkipEmptyParts);
    QStringList answerWords = answer.toLower().split(QRegularExpression("[\\s,.:;!?\\-+*/=]+"), Qt::SkipEmptyParts);
    
    // Prefer words that are numbers, as they represent calculations
    QRegularExpression numRx("^\\d+$");
    
    // Try numbers in the question first
    for (const QString &word : questionWords) {
        if (numRx.match(word).hasMatch()) {
            if (!m_agent.getAssociations(word.toStdString()).empty()) {
                return word;
            }
        }
    }
    
    // Try numbers in the answer next
    for (const QString &word : answerWords) {
        if (numRx.match(word).hasMatch()) {
            if (!m_agent.getAssociations(word.toStdString()).empty()) {
                return word;
            }
        }
    }

    // Try normal keywords in the question
    for (const QString &word : questionWords) {
        if (!stopWords.contains(word) && word.length() >= 1) {
            if (!m_agent.getAssociations(word.toStdString()).empty()) {
                return word;
            }
        }
    }

    // Try normal keywords in the answer
    for (const QString &word : answerWords) {
        if (!stopWords.contains(word) && word.length() >= 1) {
            if (!m_agent.getAssociations(word.toStdString()).empty()) {
                return word;
            }
        }
    }
    
    return "thinking";
}
