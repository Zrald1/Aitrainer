#include "mobileagentcontroller.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QtMath>

namespace {

QString compactText(QString text, int maxLength)
{
    text.remove(QRegularExpression("\\[Thinking:[^\\]]*\\]", QRegularExpression::CaseInsensitiveOption));
    text.replace(QRegularExpression("[\\x00-\\x08\\x0b\\x0c\\x0e-\\x1f]"), " ");
    text.replace(QRegularExpression("\\s+"), " ");
    text = text.trimmed();
    if (maxLength > 0 && text.length() > maxLength) {
        text = text.left(maxLength - 3).trimmed() + "...";
    }
    return text;
}

QString normalizedForMatch(QString text)
{
    text.remove(QRegularExpression("\\[Thinking:[^\\]]*\\]", QRegularExpression::CaseInsensitiveOption));
    text = text.toLower();
    text.replace(QRegularExpression("[^a-z0-9]+"), " ");
    text.replace(QRegularExpression("\\s+"), " ");
    return text.trimmed();
}

QString polishText(QString text, int maxLength = 0)
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

    if (!text.isEmpty() && text[0].isLower()) {
        text[0] = text[0].toUpper();
    }
    if (!text.endsWith('.') && !text.endsWith('!') && !text.endsWith('?')
        && text.split(' ', Qt::SkipEmptyParts).size() >= 4) {
        text += ".";
    }
    return text;
}

bool looksLikeQuestion(const QString &input)
{
    const QString text = input.trimmed().toLower();
    if (text.endsWith("?")) {
        return true;
    }

    static const QStringList starts = {
        "what ", "why ", "how ", "when ", "where ", "who ", "which ",
        "can ", "could ", "should ", "would ", "is ", "are ", "do ", "does ",
        "did ", "solve ", "calculate ", "tell me ", "explain "
    };
    for (const QString &start : starts) {
        if (text.startsWith(start)) {
            return true;
        }
    }
    return false;
}

QStringList significantTokens(const QString &text)
{
    static const QStringList stopWords = {
        "what", "why", "how", "when", "where", "who", "which", "the", "a", "an",
        "and", "or", "to", "of", "in", "on", "for", "with", "is", "are", "do",
        "does", "did", "can", "could", "should", "would", "you", "your", "me",
        "my", "this", "that", "it"
    };

    QStringList output;
    const QString normalized = normalizedForMatch(text);
    for (const QString &token : normalized.split(' ', Qt::SkipEmptyParts)) {
        if (token.length() >= 3 && !stopWords.contains(token) && !output.contains(token)) {
            output.append(token);
        }
    }
    return output;
}

int relevanceScore(const QString &query, const QString &question, const QString &lesson, const QString &answer, double strength)
{
    const QString normalizedQuery = normalizedForMatch(query);
    const QString normalizedQuestion = normalizedForMatch(question);
    if (!normalizedQuery.isEmpty() && normalizedQuery == normalizedQuestion) {
        return 200 + static_cast<int>(strength);
    }

    const QString haystack = normalizedForMatch(question + " " + lesson + " " + answer);
    int score = 0;
    for (const QString &token : significantTokens(query)) {
        if (haystack.split(' ', Qt::SkipEmptyParts).contains(token)) {
            score += 12;
        } else if (haystack.contains(token)) {
            score += 4;
        }
    }

    if (!normalizedQuestion.isEmpty()
        && (normalizedQuery.contains(normalizedQuestion) || normalizedQuestion.contains(normalizedQuery))) {
        score += 60;
    }

    return score + qMin(20, static_cast<int>(strength));
}

QString formatAnswer(const QString &thinking, const QString &answer)
{
    const QString cleanThinking = polishText(thinking, 0);
    const QString cleanAnswer = polishText(answer, 0);
    if (cleanAnswer.isEmpty()) {
        return "";
    }
    return QString("[Thinking: %1]\nAnswer: %2").arg(cleanThinking, cleanAnswer);
}

QString formatNumber(double value)
{
    if (std::abs(value - std::round(value)) < 0.000001) {
        return QString::number(static_cast<qint64>(std::llround(value)));
    }
    return QString::number(value, 'g', 12);
}

QString arithmeticAnswer(const QString &question)
{
    const QString numberPattern = "(-?\\d+(?:\\.\\d+)?)";
    QRegularExpression rx(numberPattern + "\\s*([+\\-*/xX])\\s*" + numberPattern);
    QRegularExpressionMatch match = rx.match(question);
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
    const QString op = match.captured(2).toLower();
    const double right = match.captured(3).toDouble(&rightOk);
    if (!leftOk || !rightOk) {
        return "";
    }

    double result = 0.0;
    QString symbol;
    if (op == "+" || op.contains("plus") || op.contains("add")) {
        result = left + right;
        symbol = "+";
    } else if (op == "-" || op.contains("minus")) {
        result = left - right;
        symbol = "-";
    } else if (op == "*" || op == "x" || op.contains("times") || op.contains("multiplied")) {
        result = left * right;
        symbol = "*";
    } else if (op == "/" || op.contains("divided") || op.contains("over")) {
        if (std::abs(right) < 0.000001) {
            return formatAnswer("I checked the operation and division by zero has no finite value.",
                                "Undefined");
        }
        result = left / right;
        symbol = "/";
    } else if (op.contains("less than")) {
        result = right - left;
        symbol = "-";
        return formatAnswer(QString("The phrase \"%1 less than %2\" means %2 - %1, so I calculate %2 - %1 = %3.")
                                .arg(formatNumber(left), formatNumber(right), formatNumber(result)),
                            formatNumber(result));
    } else {
        return "";
    }

    return formatAnswer(QString("I identified the math operation and calculated %1 %2 %3 = %4.")
                            .arg(formatNumber(left), symbol, formatNumber(right), formatNumber(result)),
                        formatNumber(result));
}

QString conversationalAnswer(const QString &input)
{
    const QString normalized = normalizedForMatch(input);
    if (normalized == "hello" || normalized == "hi" || normalized == "hey") {
        return formatAnswer("I identified this as a greeting.", "Hello. I am ready to chat locally.");
    }
    if (normalized == "how are you" || normalized == "how are you doing") {
        return formatAnswer("I identified this as a status question.", "I am running locally and ready to answer.");
    }
    if (normalized == "what are you" || normalized == "who are you" || normalized.contains("what kind of ai")) {
        return formatAnswer("I identified this as an identity question.",
                            "I am the imported student AI model running locally on this phone.");
    }
    if (normalized.contains("what can you do")) {
        return formatAnswer("I identified this as a capability question.",
                            "I can answer from the imported training memory and continue simple local conversation.");
    }
    return "";
}

QString statementReply(const QString &input)
{
    QString statement = compactText(input, 0);
    statement.remove(QRegularExpression("[.!?]+$"));
    const QString lower = statement.toLower();

    QString clause;
    if (lower.startsWith("i am ")) {
        clause = "you are " + statement.mid(5).trimmed();
    } else if (lower.startsWith("i'm ")) {
        clause = "you are " + statement.mid(4).trimmed();
    } else if (lower.startsWith("i have ")) {
        clause = "you have " + statement.mid(7).trimmed();
    } else if (lower.startsWith("i like ")) {
        clause = "you like " + statement.mid(7).trimmed();
    } else if (lower.startsWith("my ")) {
        clause = "your " + statement.mid(3).trimmed();
    } else {
        clause = "you said \"" + statement + "\"";
    }

    return formatAnswer("I read this as a statement and store its main idea as local chat context.",
                        "I understand that " + clause);
}

QString readablePathFromUrl(QString filePathOrUrl)
{
    filePathOrUrl = filePathOrUrl.trimmed();
    const QUrl url(filePathOrUrl);
    if (url.isValid() && url.isLocalFile()) {
        return url.toLocalFile();
    }
    if (filePathOrUrl.startsWith("file://", Qt::CaseInsensitive)) {
        return QUrl(filePathOrUrl).toLocalFile();
    }
    return filePathOrUrl;
}

}

MobileAgentController::MobileAgentController(QObject *parent)
    : QObject(parent)
{
    m_storagePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (m_storagePath.isEmpty()) {
        m_storagePath = QDir::currentPath();
    }
    QDir().mkpath(m_storagePath);
    configureAgentFiles();
    if (!loadLocalModel() && QFile::exists(":/student_agent.ai")) {
        importAgentPackage(":/student_agent.ai");
    }
}

bool MobileAgentController::modelLoaded() const
{
    return m_modelLoaded;
}

QString MobileAgentController::statusText() const
{
    return m_statusText;
}

QString MobileAgentController::storagePath() const
{
    return m_storagePath;
}

QString MobileAgentController::filePathFor(const QString &fileName) const
{
    return QDir(m_storagePath).filePath(fileName);
}

void MobileAgentController::configureAgentFiles()
{
    m_agent.setMemoryFilePath(filePathFor("agent_memory.txt").toStdString());
    m_agent.setSentenceMemoryFilePath(filePathFor("sentence_memory.txt").toStdString());
    m_agent.setLoraFilePath(filePathFor("lora_adapter.txt").toStdString());
}

void MobileAgentController::setStatusText(const QString &text)
{
    if (m_statusText == text) {
        return;
    }
    m_statusText = text;
    emit statusTextChanged();
}

void MobileAgentController::setModelLoaded(bool loaded)
{
    if (m_modelLoaded == loaded) {
        return;
    }
    m_modelLoaded = loaded;
    emit modelLoadedChanged();
}

bool MobileAgentController::loadKnowledgeBank()
{
    m_knowledgeBank.clear();
    QFile file(filePathFor("student_knowledge.json"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) {
        return false;
    }

    for (const QJsonValue &value : doc.array()) {
        const QJsonObject obj = value.toObject();
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
    return !m_knowledgeBank.isEmpty();
}

bool MobileAgentController::loadLocalModel()
{
    configureAgentFiles();
    const bool hasMemory = QFileInfo::exists(filePathFor("agent_memory.txt"));
    const bool hasKnowledge = QFileInfo::exists(filePathFor("student_knowledge.json"));
    const bool hasLora = QFileInfo::exists(filePathFor("lora_adapter.txt"));
    const bool hasSentences = QFileInfo::exists(filePathFor("sentence_memory.txt"));

    m_agent.load();
    m_agent.loadLora(filePathFor("lora_adapter.txt").toStdString());
    loadKnowledgeBank();

    const bool loaded = hasMemory || hasKnowledge || hasLora || hasSentences || !m_knowledgeBank.isEmpty();
    setModelLoaded(loaded);
    setStatusText(loaded
        ? QString("Model ready: %1 learned Q&A items.").arg(m_knowledgeBank.size())
        : "No local .ai model imported yet.");
    return loaded;
}

QString MobileAgentController::importAgentPackage(const QString &filePathOrUrl)
{
    const QString packagePath = readablePathFromUrl(filePathOrUrl);
    QFile in(packagePath);
    if (!in.open(QIODevice::ReadOnly)) {
        setStatusText("Import failed: could not open .ai package.");
        return m_statusText;
    }

    QByteArray payload = in.readAll();
    const QByteArray magic = "AITRAINER_AI_V1\n";
    if (payload.startsWith(magic)) {
        payload = payload.mid(magic.size());
    }

    const QByteArray json = qUncompress(payload);
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject() || doc.object()["format"].toString() != "AITRAINER_AI_PACKAGE") {
        setStatusText("Import failed: invalid or corrupted .ai package.");
        return m_statusText;
    }

    static const QStringList allowedFiles = {
        "agent_memory.txt",
        "sentence_memory.txt",
        "student_knowledge.json",
        "lora_adapter.txt",
        "note.txt"
    };

    int imported = 0;
    const QJsonArray files = doc.object()["files"].toArray();
    for (const QJsonValue &value : files) {
        const QJsonObject obj = value.toObject();
        const QString name = QFileInfo(obj["name"].toString()).fileName();
        if (!allowedFiles.contains(name)) {
            continue;
        }

        const QByteArray bytes = QByteArray::fromBase64(obj["data_base64"].toString().toLatin1());
        const QString expectedHash = obj["sha256"].toString();
        const QString actualHash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        if (!expectedHash.isEmpty() && expectedHash != actualHash) {
            setStatusText(QString("Import failed: checksum mismatch for %1.").arg(name));
            return m_statusText;
        }

        QFile out(filePathFor(name));
        if (!out.open(QIODevice::WriteOnly)) {
            setStatusText(QString("Import failed: could not write %1.").arg(name));
            return m_statusText;
        }
        out.write(bytes);
        out.close();
        imported++;
    }

    if (imported == 0) {
        setStatusText("Import failed: package did not contain local student model files.");
        return m_statusText;
    }

    loadLocalModel();
    setStatusText(QString("Imported %1 model files. %2 learned Q&A items ready.")
                      .arg(imported)
                      .arg(m_knowledgeBank.size()));
    return m_statusText;
}

QString MobileAgentController::answerFromKnowledge(const QString &question) const
{
    int bestIndex = -1;
    int bestScore = 0;
    for (int i = 0; i < m_knowledgeBank.size(); ++i) {
        const LearnedKnowledge &knowledge = m_knowledgeBank[i];
        const int score = relevanceScore(question,
                                         knowledge.question,
                                         knowledge.correction.isEmpty() ? knowledge.lesson : knowledge.correction,
                                         knowledge.answer,
                                         knowledge.strength);
        if (score > bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }

    if (bestIndex < 0 || bestScore < 30) {
        return "";
    }

    const LearnedKnowledge &knowledge = m_knowledgeBank[bestIndex];
    const QString lesson = knowledge.correction.isEmpty() ? knowledge.lesson : knowledge.correction;
    QString thinking;
    if (normalizedForMatch(question) == normalizedForMatch(knowledge.question)) {
        thinking = "I found the same learned question in the imported model, so I can use its verified answer.";
    } else if (!lesson.isEmpty()) {
        thinking = "I matched the important words with a related learned lesson, then used that lesson to choose the answer.";
    } else {
        thinking = "I matched the important words with related imported knowledge and selected the closest answer.";
    }
    return formatAnswer(thinking, knowledge.answer);
}

QString MobileAgentController::sendMessage(const QString &input)
{
    const QString cleanInput = compactText(input, 0);
    if (cleanInput.isEmpty()) {
        return "";
    }

    QString response = conversationalAnswer(cleanInput);
    if (response.isEmpty() && looksLikeQuestion(cleanInput)) {
        response = arithmeticAnswer(cleanInput);
        if (response.isEmpty()) {
            response = answerFromKnowledge(cleanInput);
        }
        if (response.isEmpty()) {
            const QString memoryResponse = QString::fromStdString(m_agent.respond(cleanInput.toStdString(), 0.25, 2, 4096));
            const QString normalizedMemory = normalizedForMatch(memoryResponse);
            if (!normalizedMemory.isEmpty()
                && normalizedMemory != normalizedForMatch(cleanInput)
                && !normalizedMemory.contains("need to learn more")) {
                response = formatAnswer("I could not find a verified Q&A item, so I used the local sentence memory to form the clearest related response.",
                                        memoryResponse);
            }
        }
        if (response.isEmpty()) {
            response = formatAnswer("I checked the imported model but did not find enough related knowledge to verify an answer.",
                                    "I do not know confidently from this local model yet.");
        }

        m_agent.learn(QString("Question: %1 Verified response: %2").arg(cleanInput, response).toStdString(), 0.25);
        m_agent.save();
    } else if (response.isEmpty()) {
        response = statementReply(cleanInput);
        m_agent.learn(QString("Statement: %1").arg(cleanInput).toStdString(), 0.3);
        m_agent.save();
    }

    return response;
}

QString MobileAgentController::modelSummary() const
{
    return QString("%1 | Storage: %2").arg(m_statusText, m_storagePath);
}
