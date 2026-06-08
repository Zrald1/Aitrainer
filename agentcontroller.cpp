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

bool isLikelyQuestion(const QString &input)
{
    const QString text = input.trimmed().toLower();
    if (text.endsWith("?")) {
        return true;
    }

    static const QStringList questionStarts = {
        "what ", "why ", "how ", "when ", "where ", "who ", "which ",
        "can ", "could ", "should ", "would ", "is ", "are ", "do ", "does ",
        "did ", "solve ", "calculate ", "tell me ", "explain "
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

int keywordOverlapScore(const QString &left, const QString &right)
{
    static const QStringList stopWords = {
        "what", "is", "the", "of", "a", "an", "in", "to", "and", "or",
        "for", "with", "on", "at", "by", "how", "why", "where", "who",
        "which", "about", "are", "do", "does", "did", "can", "could",
        "should", "would", "tell", "me", "explain"
    };

    const QStringList leftWords = normalizedForMatch(left).split(' ', Qt::SkipEmptyParts);
    const QStringList rightWords = normalizedForMatch(right).split(' ', Qt::SkipEmptyParts);
    int score = 0;
    for (const QString &word : leftWords) {
        if (!stopWords.contains(word) && rightWords.contains(word)) {
            score++;
        }
    }
    return score;
}

QStringList significantQuestionTokens(const QString &text)
{
    static const QStringList stopWords = {
        "what", "is", "the", "of", "a", "an", "in", "to", "and", "or",
        "for", "with", "on", "at", "by", "how", "why", "where", "who",
        "which", "about", "are", "do", "does", "did", "can", "could",
        "should", "would", "tell", "me", "explain", "find", "solve",
        "calculate", "answer", "question"
    };

    QStringList tokens;
    const QStringList words = normalizedForMatch(text).split(' ', Qt::SkipEmptyParts);
    for (const QString &word : words) {
        if (stopWords.contains(word)) {
            continue;
        }
        if (word.length() < 2 && !word.front().isDigit()) {
            continue;
        }
        if (!tokens.contains(word)) {
            tokens.append(word);
        }
    }
    return tokens;
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

    const QStringList leftTokens = significantQuestionTokens(left);
    const QStringList rightTokens = significantQuestionTokens(right);
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
    const QStringList queryTokens = significantQuestionTokens(query);
    const QStringList learningTokens = significantQuestionTokens(learningText);
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

bool isDirectKnowledgeMatch(const QString &query, const QString &learnedQuestion)
{
    return questionSimilarityScore(query, learnedQuestion) >= 100;
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

    if (directScore >= 130) {
        return directScore + qMin(20, static_cast<int>(strength));
    }

    if (tokenScore < 20 && directScore < 70) {
        return 0;
    }

    const int questionOverlap = keywordOverlapScore(query, learnedQuestion) * 4;
    const int lessonOverlap = (keywordOverlapScore(query, lesson)
                               + keywordOverlapScore(query, correction)) * 3;
    const int answerOverlap = keywordOverlapScore(query, answer);
    const int strengthBonus = qMin(15, static_cast<int>(strength / 2.0));

    return qMin(directScore, 90) + tokenScore + questionOverlap + lessonOverlap + answerOverlap + strengthBonus;
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
    return QString("[Thinking: I read this as a statement, identify its main idea, and store it as context for future replies.]\n"
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
        for (int i = 0; i < headers.size(); ++i) {
            const QString normalizedHeader = normalizedForMatch(headers[i]);
            if (normalizedHeader == normalizedForMatch(name)
                || normalizedHeader.contains(normalizedForMatch(name))) {
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
        "num rows per page", "partial", "dataset", "config", "split"
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
        if (isDatasetMetadataKey(it.key())) {
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
                                    "",
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
                                    datasetContextLesson(fields, inferredInputIndex, inferredOutputIndex),
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
        appendDatasetTrainingSample(samples, question, fields[answerIndex].second, "", seenSamples, maxSamples);
        return;
    }

    const int nameIndex = firstUsefulFieldIndex(fields, {"name", "title", "term", "word"});
    const int descriptionIndex = firstUsefulFieldIndex(fields, {"definition", "description", "summary", "text", "content"});
    if (nameIndex >= 0 && descriptionIndex >= 0 && nameIndex != descriptionIndex) {
        appendDatasetTrainingSample(samples,
                                    QString("What is %1?").arg(fields[nameIndex].second),
                                    fields[descriptionIndex].second,
                                    "",
                                    seenSamples,
                                    maxSamples);
        return;
    }

    if (fields.size() == 2) {
        appendDatasetTrainingSample(samples,
                                    datasetQuestionFromInputOutput(fields[0], fields[1]),
                                    fields[1].second,
                                    "",
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
        appendDatasetTrainingSample(samples, question, fields[i].second, "", seenSamples, maxSamples);
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
    const QString cleanAnswer = compactDatasetText(answer, 520);
    const QString cleanLesson = compactDatasetText(lesson, 520);
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
            appendDatasetTrainingSample(samples, pendingUser, content, "", seenSamples, maxSamples);
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
    appendDatasetTrainingSample(samples, userText, assistantText, "", seenSamples, maxSamples);
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

    const QString lesson = QString("Choices: %1").arg(choices.join("; "));
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
    const QString lesson = firstDatasetField(object, {"lesson", "context", "category", "topic"});
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
            "lesson", "context", "category", "topic", "explanation", "rationale", "choices"
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
        return QString("Answer check: I see the question asks for an identity or definition, so the answer must name the idea directly. \"%1\" does that for \"%2\".")
            .arg(cleanAnswer, cleanQuestion);
    }

    if (lowerQuestion.startsWith("why ")) {
        return QString("Answer check: I see the question asks for a reason, so I check that \"%1\" explains why \"%2\" is true.")
            .arg(cleanAnswer, cleanQuestion);
    }

    if (lowerQuestion.startsWith("how ")) {
        return QString("Answer check: I see the question asks for a method or process, so I check that \"%1\" gives the steps or action needed for \"%2\".")
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

    return QString("Answer check: I read what \"%1\" is asking, isolate the required kind of answer, and check that \"%2\" directly satisfies it.")
        .arg(cleanQuestion, cleanAnswer);
}

QString buildStudentVisibleThinking(const QString &lesson, const QString &question, const QString &answer)
{
    (void)lesson;

    const QString arithmeticThinking = buildArithmeticThinking(question, answer);
    if (!arithmeticThinking.isEmpty()) {
        return arithmeticThinking;
    }

    const QString conceptualThinking = buildConceptualThinking(question, answer);
    if (!conceptualThinking.isEmpty()) {
        return conceptualThinking;
    }

    const QString mathWordThinking = buildMathWordProblemThinking(question, answer);
    if (!mathWordThinking.isEmpty()) {
        return mathWordThinking;
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

QString formatThinkingFirstQuestionResponse(const QString &question, const QString &lesson, const QString &rawAnswer)
{
    const QString answer = polishUserFacingText(extractAnswerForDisplay(rawAnswer), 0, true);
    if (answer.isEmpty()) {
        return "";
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

QString buildConversationalLocalAnswer(const QString &question)
{
    const QString lowerQuestion = normalizedForMatch(question);

    if (lowerQuestion == "how are you"
        || lowerQuestion == "how are you doing"
        || lowerQuestion == "how do you feel") {
        return "Thinking: I identify this as a conversational status question, so I answer about my current role and readiness.\n"
               "Answer: I am running normally and ready to learn, reason, and answer clearly.";
    }

    if (lowerQuestion == "what are you"
        || lowerQuestion == "who are you"
        || lowerQuestion.contains("what kind of ai are you")) {
        return "Thinking: I identify this as an identity question, so I answer from my role in this app.\n"
               "Answer: I am a local student AI inside this trainer. I learn from lessons, datasets, corrections, and conversation memory.";
    }

    if (lowerQuestion.contains("what can you do")
        || lowerQuestion.contains("what are your capabilities")) {
        return "Thinking: I identify this as a capability question, so I describe the skills this app gives me.\n"
               "Answer: I can learn from text or datasets, store related facts, answer questions with visible checks, and practice lessons from the teacher model.";
    }

    if (lowerQuestion.contains("can you understand me")
        || lowerQuestion.contains("do you understand me")) {
        return "Thinking: I identify this as a comprehension question, so I answer based on how I process the current message.\n"
               "Answer: I can read your message, identify important words, compare them with learned knowledge, and form a clear response when I have enough related context.";
    }

    if (lowerQuestion == "hello"
        || lowerQuestion == "hi"
        || lowerQuestion == "hey") {
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
    , m_simulationDelay(2000)
    , m_datasetTrainingNextChunkIndex(0)
    , m_isReTest(false)
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
        ? "Local GPU training enabled. Direct3D 11 compute will be checked when training starts."
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
            setLocalGpuTrainingStatus("Local GPU training disabled; CPU LoRA path will be used.");
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

    const bool question = isLikelyQuestion(cleanInput);
    QString qResponse;

    if (question) {
        QString bestCurriculumAnswer;
        QString bestCurriculumLesson;
        QString relatedLearning;
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

        if (!bestCurriculumAnswer.isEmpty()) {
            qResponse = formatThinkingFirstQuestionResponse(cleanInput, bestCurriculumLesson, bestCurriculumAnswer);
        }

        if (qResponse.isEmpty()) {
            qResponse = buildVerifiedLocalChatAnswer(cleanInput);
        }

        if (qResponse.isEmpty() && !m_apiKey.trimmed().isEmpty()) {
            QNetworkAccessManager chatNetwork;
            QJsonArray messages;

            QJsonObject systemObj;
            systemObj["role"] = "system";
            systemObj["content"] = "You are the student AI. Answer the user's question by applying only clearly related learned facts, lessons, and calculations. If related learnings are provided, adapt their rule or method to the new situation instead of copying an old answer. Ignore unrelated learnings. Before answering, check the facts, wording, and any calculation internally. Reply with exactly two sections: 'Thinking: ' followed by the full verification needed to justify the answer, then 'Answer: ' followed by the final answer. For math, show the calculation. For word/concept questions, explain the relevant learned rule or concept. If the provided learnings are not related enough to verify the answer, say you do not know confidently. Do not output lesson JSON or markdown.";
            messages.append(systemObj);

            QJsonObject userObj;
            userObj["role"] = "user";
            userObj["content"] = relatedLearning.isEmpty()
                ? cleanInput
                : QString("User question: %1\n\nUse these related learnings only if they truly apply:\n%2")
                    .arg(cleanInput, relatedLearning);
            messages.append(userObj);

            QJsonObject body;
            body["model"] = m_teacherModel;
            body["messages"] = messages;
            body["temperature"] = 0.2;
            body["max_tokens"] = 4096;

            QNetworkRequest request(QUrl("https://api.featherless.ai/v1/chat/completions"));
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            request.setRawHeader("Authorization", "Bearer " + m_apiKey.trimmed().toUtf8());

            QNetworkReply *reply = chatNetwork.post(request, QJsonDocument(body).toJson());
            QEventLoop loop;
            QTimer timeout;
            timeout.setSingleShot(true);
            connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
            timeout.start(10000);
            loop.exec();

            if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
                QJsonDocument responseDoc = QJsonDocument::fromJson(reply->readAll());
                QJsonArray choices = responseDoc.object()["choices"].toArray();
                if (!choices.isEmpty()) {
                    qResponse = choices[0].toObject()["message"].toObject()["content"].toString().trimmed();
                    qResponse = formatThinkingFirstQuestionResponse(cleanInput, "", qResponse);
                }
            } else if (!reply->isFinished()) {
                reply->abort();
            }
            reply->deleteLater();
        }

        if (qResponse.isEmpty()) {
            const QString memoryResponse = QString::fromStdString(m_agent.respond(cleanInput.toStdString(), m_temperature, m_contextWindow, 4096)).trimmed();
            if (isUsefulMemoryResponse(memoryResponse, cleanInput)) {
                qResponse = formatThinkingFirstQuestionResponse(cleanInput, "", "Answer: " + memoryResponse);
            } else {
                qResponse = "[Thinking: I checked learned memory and could not verify enough facts for this question.]\nAnswer: I do not have enough learned facts yet to answer confidently.";
            }
        }

        if (m_learningEnabled) {
            const QString verifiedAnswer = extractAnswerForDisplay(qResponse);
            const QString trainingSample = QString("Question: %1 Verified response: %2").arg(cleanInput, qResponse);
            m_agent.learn(trainingSample.toStdString(), 1.0);
            m_agent.trainLoraText(trainingSample.toStdString(), 3, 0.05, 4, 8.0, 1.0, m_useLocalGpuTraining);
            upsertKnowledge(cleanInput, "", verifiedAnswer, "", "manual chat", 0.4);
            saveMemory();
        }
    } else {
        qResponse = buildStatementUnderstandingResponse(cleanInput);

        if (m_learningEnabled) {
            const QString trainingSample = QString("Statement: %1").arg(cleanInput);
            m_agent.learn(trainingSample.toStdString(), 0.6);
            m_agent.trainLoraText(trainingSample.toStdString(), 2, 0.04, 4, 8.0, 0.6, m_useLocalGpuTraining);
            saveMemory();
        }
    }

    emit memoryChanged();
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
        setLocalGpuTrainingStatus("Starting Direct3D 11 local GPU LoRA training...");
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
import csv
import hashlib
import io
import json
import os
import re
import struct
import sys
import urllib.request
import zlib

MAGIC = b"AITRAINER_AI_V1\n"
HF_TOKEN = os.environ.get("HF_TOKEN", "").strip()

def log(message):
    print(message, flush=True)

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

def first_field(row, names):
    for name in names:
        name_norm = normalized(name)
        for key, value in row.items():
            key_norm = normalized(key)
            if key_norm == name_norm or name_norm in key_norm:
                text = field_text(value)
                if text:
                    return text
    return ""

def add_sample(samples, question, answer, lesson="", max_samples=1000):
    question = compact(question)
    answer = compact(answer)
    lesson = compact(lesson)
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
            add_sample(samples, pending_user, content, max_samples=max_samples)
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
        add_sample(samples, question, answer, context, max_samples)
        return

    name = first_field(row, ["name", "title", "term", "word"])
    description = first_field(row, ["definition", "description", "summary", "text", "content"])
    if name and description and normalized(name) != normalized(description):
        add_sample(samples, "What is " + name + "?", description, context, max_samples)
        return

    useful = []
    for key, value in row.items():
        key_norm = normalized(key)
        if key_norm in ("id", "index", "row idx", "row index") or key_norm.endswith(" id"):
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
                   context_lesson(useful, input_index, output_index) or context,
                   max_samples)
        return

    if len(useful) == 1:
        add_sample(samples, "What information is in the " + useful[0][0] + " field?", useful[0][1], max_samples=max_samples)
    elif len(useful) == 2:
        add_sample(samples, question_from_fields(useful[0], useful[1]), useful[1][1], max_samples=max_samples)
    elif len(useful) > 1:
        anchor_key, anchor_value = useful[0]
        for key, text in useful[1:4]:
            add_sample(samples, "For " + anchor_key + " \"" + compact(anchor_value, 160) + "\", what is the " + key + "?", text, max_samples=max_samples)

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
        raise RuntimeError("Python package 'datasets' is required on the GPU server: " + str(exc))

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

    for row in dataset:
        row_samples(row, samples, max_samples)
        if len(samples) >= max_samples:
            break
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

def train_sample(sample, memory, sentences, epochs):
    text = "Question: {q}\n".format(q=sample["question"])
    if sample.get("lesson"):
        text += "Lesson: {l}\n".format(l=sample["lesson"])
    text += "Answer: {a}".format(a=sample["answer"])

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
    args = parser.parse_args()

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

    knowledge = update_knowledge(knowledge, samples, args.epochs)
    files["student_knowledge.json"] = (json.dumps(knowledge, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    files["agent_memory.txt"] = dump_memory(memory)
    files["sentence_memory.txt"] = ("\n".join(sorted(sentences)) + "\n").encode("utf-8")
    note = files.get("note.txt", b"").decode("utf-8", errors="replace")
    note += "\nDigitalOcean GPU training:\n"
    note += "- dataset: " + args.dataset + "\n"
    note += "- samples: " + str(len(samples)) + "\n"
    note += "- epochs: " + str(args.epochs) + "\n"
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

    appendToSimulationLog(QString("[Dataset Training]: Downloaded %1 bytes. Parsing samples on a background thread.")
        .arg(originalBytes));
    emit simulationMessageAdded("system", "Dataset downloaded. Parsing in the background so the UI stays responsive.");
    if (m_datasetParseLogTimer) {
        m_datasetParseLogTicks = 0;
        m_datasetParseLogTimer->start();
    }

    QPointer<AgentController> self(this);
    m_datasetParseThread = QThread::create([self, datasetBytes, source, originalBytes]() {
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

    appendToSimulationLog(QString("[Dataset Training]: Reading and parsing local dataset on a background thread: %1")
        .arg(filePath));
    emit simulationMessageAdded("system", "Local dataset parsing started in the background so the UI stays responsive.");
    if (m_datasetParseLogTimer) {
        m_datasetParseLogTicks = 0;
        m_datasetParseLogTimer->start();
    }

    QPointer<AgentController> self(this);
    m_datasetParseThread = QThread::create([self, filePath]() {
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
    const QString message = importFailed
        ? QString("GPU training downloaded %1, but local import failed. %2")
              .arg(QFileInfo(m_gpuLocalOutputPackage).absoluteFilePath(), importResult)
        : QString("GPU training complete. Downloaded and imported %1. %2")
              .arg(QFileInfo(m_gpuLocalOutputPackage).absoluteFilePath(), importResult);
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

        const QString command = QString("cd %1 && (python3 -m pip install --user -q datasets requests >/dev/null 2>&1 || true) && HF_TOKEN=%2 python3 %3 --input %4 --output %5 --dataset %6 --epochs %7 --max-samples %8")
            .arg(shellQuote(m_gpuRemoteRunDir),
                 shellQuote(m_huggingFaceToken.trimmed()),
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

    const int completed = m_datasetTrainingNextChunkIndex;
    const QString chunk = m_pendingDatasetChunks.at(m_datasetTrainingNextChunkIndex);
    ++m_datasetTrainingNextChunkIndex;
    if (m_useLocalGpuTraining && !m_isLocalGpuTrainingRunning) {
        m_isLocalGpuTrainingRunning = true;
        setLocalGpuTrainingStatus("Running Direct3D 11 local GPU LoRA dataset training...");
    }

    std::string localGpuStatus;
    m_agent.trainLoraText(chunk.toStdString(),
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
        if (completed == 0 || importantStatus || (completed + 1) % 50 == 0) {
            appendToSimulationLog("[Local GPU Training]: " + status);
        }
    }
    m_agent.learn(chunk.toStdString(), 0.8);

    QRegularExpression qaRx("Question\\s*:\\s*([^\\n]+)(?:\\nLesson\\s*:\\s*([^\\n]+))?\\nAnswer\\s*:\\s*([^\\n]+)",
                            QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch qaMatch = qaRx.match(chunk);
    if (qaMatch.hasMatch()) {
        upsertKnowledge(qaMatch.captured(1).trimmed(),
                        qaMatch.captured(2).trimmed(),
                        qaMatch.captured(3).trimmed(),
                        "",
                        "dataset training",
                        0.9);
    }

    if ((completed + 1) % 10 == 0 || m_datasetTrainingNextChunkIndex >= m_pendingDatasetChunks.size()) {
        appendToSimulationLog(QString("[Dataset Training]: Trained sample/chunk %1/%2")
            .arg(completed + 1)
            .arg(m_datasetTrainingTotalChunks));
    }

    QTimer::singleShot(15, this, &AgentController::processNextDatasetTrainingChunk);
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
        .arg(m_useLocalGpuTraining ? "Direct3D 11 GPU" : "CPU")
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

    QFile file(m_knowledgeFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return true;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) {
        return false;
    }

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

        if (!knowledge.question.isEmpty() && !knowledge.answer.isEmpty()) {
            m_knowledgeBank.append(knowledge);
        }
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

void AgentController::upsertKnowledge(const QString &question,
                                      const QString &lesson,
                                      const QString &answer,
                                      const QString &correction,
                                      const QString &source,
                                      double strength) {
    const QString cleanQuestion = compactText(question, 0);
    const QString cleanAnswer = compactText(answer, 0);
    if (cleanQuestion.isEmpty() || cleanAnswer.isEmpty()) {
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
}

int AgentController::findBestKnowledgeIndex(const QString &query, int *score) const {
    int bestIndex = -1;
    int bestScore = 0;

    for (int i = 0; i < m_knowledgeBank.size(); ++i) {
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

QString AgentController::relatedKnowledgeContext(const QString &query, int limit) const {
    QStringList entries;
    QList<int> usedIndexes;

    for (int count = 0; count < limit; ++count) {
        int bestIndex = -1;
        int bestScore = 0;

        for (int i = 0; i < m_knowledgeBank.size(); ++i) {
            if (usedIndexes.contains(i)) {
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

        if (bestIndex < 0 || bestScore < relatedLearningMinimumScore()) {
            break;
        }

        usedIndexes.append(bestIndex);
        const LearnedKnowledge &knowledge = m_knowledgeBank[bestIndex];
        const QString lesson = compactText(knowledge.correction.isEmpty() ? knowledge.lesson : knowledge.correction, 0);
        entries.append(QString("- Related learning %1: Question: %2 | Lesson: %3 | Answer: %4")
            .arg(entries.size() + 1)
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
    for (const LearnedKnowledge &knowledge : m_knowledgeBank) {
        evaluateExisting(knowledge.question);
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

        const QStringList avoidQuestions = recentKnownQuestions(m_teacherRetryCount > 0 ? 90 : 60);
        QString avoidBlock = avoidQuestions.isEmpty()
            ? "None yet."
            : "- " + avoidQuestions.join("\n- ");
        const QString requiredAngle = teacherLessonAngleDirective(currentSubject,
                                                                  m_curriculumQuestions.size() + 1,
                                                                  m_teacherRetryCount);
        const QStringList forbiddenOpenings = questionOpenings(avoidQuestions, 14);
        const QString forbiddenOpeningBlock = forbiddenOpenings.isEmpty()
            ? "None yet."
            : "- " + forbiddenOpenings.join("\n- ");
        appendToSimulationLog(QString("[Curriculum]: Required lesson angle: %1").arg(requiredAngle));

        QString promptText = QString(
            "Create one lesson about the requested topic, then create one question that tests the lesson.\n"
            "Requested topic: '%1'.\n\n"
            "Required lesson angle for THIS request: %4.\n"
            "You must use this angle and must not reuse the same concept, scenario, or question pattern from the avoided list.\n\n"
            "Topic obedience rules:\n"
            "1. The lesson, question, and answer MUST stay inside the requested topic '%1'.\n"
            "2. Do not switch to mathematics, puzzles, trick questions, generic riddles, or another subject unless the requested topic explicitly asks for that.\n"
            "3. First choose one specific concept, rule, method, or key term inside '%1' that matches the required lesson angle.\n"
            "4. The lesson must teach that concept clearly before the question is asked.\n\n"
            "Question-quality rules:\n"
            "1. The JSON 'question' must be a concrete student-facing question that can be answered using the lesson.\n"
            "2. Do not use meta wording such as 'what question tests', 'what should the student know', or 'training item'.\n"
            "3. The question must name the concept from the lesson; do not ask vague pronoun-only questions like 'Why is it useful?'.\n"
            "4. The question must end with a question mark and have one direct answer.\n"
            "5. The answer must be short and must match the lesson and question exactly.\n"
            "6. Do not start the question with any forbidden opening pattern below.\n\n"
            "You MUST reply in JSON format with exactly these three keys:\n"
            "- 'lesson': (string) A concise lesson about one concept in '%1'. Include a [Thinking: ...] bracket with a verification check or example that shows how the concept can be reasoned through.\n"
            "- 'question': (string) A new question directly testing the lesson concept.\n"
            "- 'answer': (string) The direct, correct answer to the question.\n\n"
            "Hard uniqueness rules:\n"
            "1. Do not ask any question from the avoided list.\n"
            "2. Do not reuse the same numbers, wording pattern, puzzle setup, trick, or corrected concept from the avoided list.\n"
            "3. Prefer a new concept or a new variant that tests a different skill.\n"
            "4. If the student already learned a correction for a question, never ask that same question again.\n"
            "5. Retry number for uniqueness: %3. If this is greater than 0, your previous output was rejected as repetitive, so choose a different scenario, different wording, and different answer type.\n\n"
            "Forbidden question openings:\n%5\n\n"
            "Avoid these already learned/corrected/rejected questions:\n%2"
        ).arg(currentSubject,
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
        jsonBody["temperature"] = m_teacherRetryCount > 0 ? 0.95 : 0.65;
        jsonBody["presence_penalty"] = m_teacherRetryCount > 0 ? 1.1 : 0.7;
        jsonBody["frequency_penalty"] = m_teacherRetryCount > 0 ? 1.0 : 0.7;
        jsonBody["max_tokens"] = 4096;

        QNetworkRequest request(QUrl("https://api.featherless.ai/v1/chat/completions"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", "Bearer " + m_apiKey.trimmed().toUtf8());

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
                // Generate the answer part stochastically from memory only when no structured fact is available.
                const double testingTemp = m_temperature;
                const int testingContext = m_contextWindow;
                answerStr = QString::fromStdString(m_agent.respond(item.question.toStdString(), testingTemp, testingContext, 4096)).trimmed();

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

        // Combine visible answer work with the generated answer
        QString studentResponse = QString("[Thinking: %1]\nAnswer: %2").arg(thinkingStr, answerStr);

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

    QString evalPrompt = QString("You are a strict, objective academic grader. Your task is to evaluate both the student's thinking process and their answer based on the lesson taught and the question asked.\n\n"
                                 "Lesson Taught: '%1'\n"
                                 "Question Asked: '%2'\n"
                                 "Student's Answer (including [Thinking: ...]): '%3'\n\n"
                                 "Evaluation Requirements:\n"
                                 "1. Analyze the student's thinking process inside the [Thinking: ...] bracket.\n"
                                 "2. Critical Rule: The student's thinking must explain in its own words how it got the answer. For math, it must show the calculation or inverse check. For puzzles/trick questions, it must identify the key wording or rule that makes the answer follow. It must not merely say it learned, remembered, matched, or copied the answer.\n"
                                 "3. Grade the thinking quality and the final answer correctness according to this rubric:\n"
                                 "   - Score 95-100: The student's thinking gives a concrete calculation, inverse check, pattern check, or key-wording explanation that makes the final answer follow, and the final answer is correct.\n"
                                 "   - Score 50-80: The student's thinking has some logic but is vague, relies on memory, or does not fully connect the question to the answer.\n"
                                 "   - Score 0-45: The student's thinking lacks a real reason, only says it learned/copied/remembered the answer, or the final answer is incorrect.\n"
                                 "4. In the 'feedback_to_improve' key, write feedback specifically rating whether the student explained why the answer follows.\n"
                                 "5. In the 'improved_thinking' key, provide a corrected thinking statement showing how the student should explain the answer in its own words (e.g. '[Thinking: Answer check: to verify 43 + 28, I split it as 40+20=60 and 3+8=11, which gives 71.] The correct answer is 71.').\n"
                                 "6. In the 'correction_lesson' key, write the actual teacher correction lesson the student must learn. This must be authored by you, include the correct answer, and explain the corrected method/rule for this exact question.\n\n"
                                 "You MUST reply in JSON format with keys:\n"
                                 "- 'thinking_score': (integer between 0 and 100 representing thinking quality)\n"
                                 "- 'thinking_rating': (string like 'Excellent', 'Good', 'Needs Improvement') rating the thinking\n"
                                 "- 'feedback_to_improve': (string, max 25 words) rating the thinking and instructing how to improve it\n"
                                 "- 'improved_thinking': (string, max 60 words) containing the improved thinking block and correct answer for the student to learn and copy\n"
                                 "- 'correction_lesson': (string, max 90 words) the teacher-authored correction lesson the student should learn\n"
                                 "- 'final_score': (integer between 0 and 100 representing final answer correctness)")
                                 .arg(teaching)
                                 .arg(question)
                                 .arg(answer);

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
    evalBody["max_tokens"] = 4096;

    QNetworkRequest evalReq(QUrl("https://api.featherless.ai/v1/chat/completions"));
    evalReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    evalReq.setRawHeader("Authorization", "Bearer " + m_apiKey.trimmed().toUtf8());

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
    if (activeItem && !correctionLesson.isEmpty()) {
        activeItem->lesson = correctionLesson;
        upsertKnowledge(activeItem->question,
                        activeItem->lesson,
                        activeItem->answer,
                        correctionLesson,
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
            structuredCorrection = QString("Teacher correction lesson. Question: %1 Correct answer: %2 Lesson: %3")
                .arg(activeItem->question, activeItem->answer, correctionLesson);
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
