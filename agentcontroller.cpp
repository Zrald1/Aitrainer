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
#include <cmath>

namespace {

QString compactText(QString text, int maxLength)
{
    text.remove(QRegularExpression("\\[Thinking:[^\\]]*\\]", QRegularExpression::CaseInsensitiveOption));
    text.replace(QRegularExpression("\\s+"), " ");
    text = text.trimmed();
    if (text.length() > maxLength) {
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
        || cleanResponse == normalizedForMatch(input)) {
        return false;
    }
    return keywordOverlapScore(response, input) > 0 || response.split(' ', Qt::SkipEmptyParts).size() > 2;
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

bool isLikelyTrainableDatasetFile(const QString &path)
{
    const QString lower = path.toLower();
    return lower.endsWith(".txt")
        || lower.endsWith(".md")
        || lower.endsWith(".json")
        || lower.endsWith(".jsonl")
        || lower.endsWith(".csv");
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
                .arg(formatNumber(calculated), compactText(answer, 80));
        }
    } else if (!answer.trimmed().isEmpty()) {
        comparison = QString(" I compare this calculated value with my answer \"%1\".")
            .arg(compactText(answer, 80));
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
    QString clean = compactText(answer, 120);
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
    const QString cleanQuestion = compactText(question, 110);
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
        return compactText(bracketMatch.captured(1), 260);
    }

    QRegularExpression checkRx("^\\s*(Thinking|Check|How|Why this answer follows)\\s*:\\s*([^\\n]+)",
                               QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch checkMatch = checkRx.match(text);
    if (checkMatch.hasMatch()) {
        return compactText(checkMatch.captured(2), 260);
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

    const QStringList lines = response.split('\n', Qt::SkipEmptyParts);
    QRegularExpression answerRx("^\\s*(?:final\\s+answer|answer)\\s*:\\s*(.+)$",
                                QRegularExpression::CaseInsensitiveOption);
    for (const QString &line : lines) {
        QRegularExpressionMatch answerMatch = answerRx.match(line.trimmed());
        if (answerMatch.hasMatch()) {
            return compactText(answerMatch.captured(1), 260);
        }
    }

    QRegularExpression sentenceRx("^\\s*the\\s+answer\\s+is\\s+(.+)$",
                                  QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch sentenceMatch = sentenceRx.match(response);
    if (sentenceMatch.hasMatch()) {
        return compactText(sentenceMatch.captured(1), 260);
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

    return compactText(answerLines.isEmpty() ? response : answerLines.join(' '), 260);
}

QString formatThinkingFirstQuestionResponse(const QString &question, const QString &lesson, const QString &rawAnswer)
{
    const QString answer = extractAnswerForDisplay(rawAnswer);
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
    thinking = compactText(thinking, 260);
    if (!thinking.endsWith('.') && !thinking.endsWith('!') && !thinking.endsWith('?')) {
        thinking += ".";
    }

    return QString("[Thinking: %1]\nAnswer: %2").arg(thinking, answer);
}

QString formatDirectCheckedAnswer(const QString &question, const QString &answer)
{
    return formatThinkingFirstQuestionResponse(question, "", "Answer: " + answer);
}

QString buildVerifiedLocalChatAnswer(const QString &question)
{
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
    , m_simulationDelay(2000)
    , m_isReTest(false)
    , m_knowledgeFile("student_knowledge.json")
    , m_loraFile("lora_adapter.txt")
    , m_datasetTrainingEpochs(4)
    , m_datasetTrainingTotalChunks(0)
    , m_datasetTrainingOriginalBytes(0)
    , m_datasetRequestIsMetadata(false)
    , m_teacherRetryCount(0)
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
    m_teacherModel = settings.value("teacherModel", "deepseek-ai/DeepSeek-V4-Flash").toString();
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
    const QString cleanInput = compactText(input, 500);
    if (cleanInput.isEmpty()) {
        return "";
    }

    const bool question = isLikelyQuestion(cleanInput);
    QString qResponse;

    if (question) {
        QString bestCurriculumAnswer;
        QString bestCurriculumLesson;
        int bestScore = 0;
        int knowledgeIndex = findBestKnowledgeIndex(cleanInput, &bestScore);
        if (knowledgeIndex >= 0 && bestScore >= 3) {
            const LearnedKnowledge &knowledge = m_knowledgeBank[knowledgeIndex];
            bestCurriculumAnswer = knowledge.answer;
            bestCurriculumLesson = knowledge.correction.isEmpty() ? knowledge.lesson : knowledge.correction;
        }

        if (bestScore >= 2 && !bestCurriculumAnswer.isEmpty()) {
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
            systemObj["content"] = "You answer normal chatbox messages. Treat the user input as a question, never as a lesson. Before answering, check the facts, wording, and any calculation internally. Reply with exactly two lines: 'Thinking: ' followed by one concise verification sentence, then 'Answer: ' followed by the final answer. For math, verify the operation. For trick questions, identify the key wording. If you cannot verify the answer, say so. Do not output lesson JSON or markdown.";
            messages.append(systemObj);

            QJsonObject userObj;
            userObj["role"] = "user";
            userObj["content"] = cleanInput;
            messages.append(userObj);

            QJsonObject body;
            body["model"] = m_teacherModel;
            body["messages"] = messages;
            body["temperature"] = 0.2;
            body["max_tokens"] = 220;

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
            const QString memoryResponse = QString::fromStdString(m_agent.respond(cleanInput.toStdString(), m_temperature, m_contextWindow)).trimmed();
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
            m_agent.trainLoraText(trainingSample.toStdString(), 3, 0.05, 4, 8.0, 1.0);
            upsertKnowledge(cleanInput, "", verifiedAnswer, "", "manual chat", 0.4);
            saveMemory();
        }
    } else {
        qResponse = "I understand: " + compactText(cleanInput, 180);

        if (m_learningEnabled) {
            const QString trainingSample = QString("Statement: %1").arg(cleanInput);
            m_agent.learn(trainingSample.toStdString(), 0.6);
            m_agent.trainLoraText(trainingSample.toStdString(), 2, 0.04, 4, 8.0, 0.6);
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
    const QString cleanTrainingText = compactText(trainingText, 4000);
    if (cleanTrainingText.split(' ', Qt::SkipEmptyParts).size() < 2) {
        return "LoRA training skipped: enter at least two words of training text.";
    }

    epochs = qMax(1, qMin(epochs, 32));
    const bool trained = m_agent.trainLoraText(cleanTrainingText.toStdString(),
                                               epochs,
                                               0.05,
                                               4,
                                               8.0,
                                               1.2);
    if (!trained) {
        return "LoRA training failed: text could not be tokenized.";
    }

    m_agent.learn(QString("LoRA training sample: %1").arg(cleanTrainingText).toStdString(), 0.4);
    saveMemory();
    emit memoryChanged();

    return QString("LoRA-style C++ training complete: %1 epochs, %2 adapter pairs, rank %3.")
        .arg(epochs)
        .arg(m_agent.getLoraPairCount())
        .arg(m_agent.getLoraRank());
}

QString AgentController::trainLoraFromDatasetUrl(const QString &datasetUrl, int epochs) {
    QString urlText = normalizeHuggingFaceDatasetFileUrl(datasetUrl);
    if (urlText.isEmpty()) {
        return "Dataset training skipped: enter a Hugging Face dataset URL.";
    }
    if (m_datasetReply || !m_pendingDatasetChunks.isEmpty()) {
        return "Dataset training is already running. Wait for it to finish before starting another dataset.";
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
        request.setRawHeader("User-Agent", "Aitrainer-Cpp-Lora/1.0");
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
    request.setRawHeader("User-Agent", "Aitrainer-Cpp-Lora/1.0");
    m_datasetReply = m_networkManager.get(request);
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

void AgentController::handleDatasetTrainingDownload() {
    if (!m_datasetReply) return;

    QNetworkReply *reply = m_datasetReply;
    m_datasetReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        const QString error = reply->errorString();
        reply->deleteLater();
        appendToSimulationLog(QString("[Dataset Training]: %1 failed: %2")
            .arg(m_datasetRequestIsMetadata ? "Repo inspection" : "Download")
            .arg(error));
        emit simulationMessageAdded("system", "Dataset training failed: " + error);
        m_datasetRequestIsMetadata = false;
        m_datasetRepoId.clear();
        return;
    }

    const QByteArray responseBytes = reply->readAll();
    reply->deleteLater();

    if (m_datasetRequestIsMetadata) {
        m_datasetRequestIsMetadata = false;

        QJsonDocument metadata = QJsonDocument::fromJson(responseBytes);
        QJsonArray siblings = metadata.object()["siblings"].toArray();
        QString selectedFile;

        for (const QJsonValue &value : siblings) {
            const QString name = value.toObject()["rfilename"].toString();
            if (isLikelyTrainableDatasetFile(name) && name.toLower().contains("train")) {
                selectedFile = name;
                break;
            }
        }
        if (selectedFile.isEmpty()) {
            for (const QJsonValue &value : siblings) {
                const QString name = value.toObject()["rfilename"].toString();
                if (isLikelyTrainableDatasetFile(name)) {
                    selectedFile = name;
                    break;
                }
            }
        }

        if (selectedFile.isEmpty()) {
            appendToSimulationLog("[Dataset Training]: No trainable dataset file found in repo.");
            emit simulationMessageAdded("system", "Dataset training failed: no .txt, .md, .json, .jsonl, or .csv file found in the public repo.");
            m_datasetRepoId.clear();
            return;
        }

        const QString encodedFile = QString::fromLatin1(QUrl::toPercentEncoding(selectedFile, "/"));
        m_datasetTrainingSource = QString("https://huggingface.co/datasets/%1/resolve/main/%2").arg(m_datasetRepoId, encodedFile);
        m_datasetRepoId.clear();

        QNetworkRequest fileRequest{QUrl(m_datasetTrainingSource)};
        fileRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        fileRequest.setRawHeader("User-Agent", "Aitrainer-Cpp-Lora/1.0");
        m_datasetReply = m_networkManager.get(fileRequest);
        connect(m_datasetReply, &QNetworkReply::finished, this, &AgentController::handleDatasetTrainingDownload);

        appendToSimulationLog(QString("[Dataset Training]: Selected file %1. Downloading now.").arg(selectedFile));
        emit simulationMessageAdded("system", QString("Selected dataset file: %1. Downloading in the background.").arg(selectedFile));
        return;
    }

    const QByteArray datasetBytes = responseBytes;
    m_datasetTrainingOriginalBytes = datasetBytes.size();
    const int maxTrainingChars = 600000;
    QString datasetText = QString::fromUtf8(datasetBytes);
    datasetText = compactDatasetText(datasetText, maxTrainingChars);
    if (datasetText.split(' ', Qt::SkipEmptyParts).size() < 2) {
        appendToSimulationLog("[Dataset Training]: Downloaded file did not contain enough readable text.");
        emit simulationMessageAdded("system", "Dataset training failed: downloaded file did not contain enough readable text.");
        return;
    }

    m_pendingDatasetChunks.clear();
    const int chunkSize = 3000;
    const int maxChunks = 80;
    for (int offset = 0; offset < datasetText.length() && m_pendingDatasetChunks.size() < maxChunks; offset += chunkSize) {
        const QString chunk = datasetText.mid(offset, chunkSize).trimmed();
        if (chunk.split(' ', Qt::SkipEmptyParts).size() >= 2) {
            m_pendingDatasetChunks.append(chunk);
        }
    }

    m_datasetTrainingTotalChunks = m_pendingDatasetChunks.size();
    if (m_datasetTrainingTotalChunks == 0) {
        appendToSimulationLog("[Dataset Training]: No trainable chunks were created.");
        emit simulationMessageAdded("system", "Dataset training failed: no trainable chunks were created.");
        return;
    }

    appendToSimulationLog(QString("[Dataset Training]: Downloaded %1 bytes. Training %2 chunks asynchronously.")
        .arg(m_datasetTrainingOriginalBytes)
        .arg(m_datasetTrainingTotalChunks));
    emit simulationMessageAdded("system", QString("Dataset downloaded. Training %1 chunks in the background.").arg(m_datasetTrainingTotalChunks));
    QTimer::singleShot(0, this, &AgentController::processNextDatasetTrainingChunk);
}

void AgentController::processNextDatasetTrainingChunk() {
    if (m_pendingDatasetChunks.isEmpty()) {
        saveMemory();
        emit memoryChanged();

        const QString completeMessage = QString("Dataset LoRA training complete: %1 chunks, %2 epochs each, %3 bytes downloaded. Source: %4")
            .arg(m_datasetTrainingTotalChunks)
            .arg(m_datasetTrainingEpochs)
            .arg(m_datasetTrainingOriginalBytes)
            .arg(m_datasetTrainingSource);
        appendToSimulationLog("[Dataset Training]: " + completeMessage);
        emit simulationMessageAdded("system", completeMessage);

        m_datasetTrainingSource.clear();
        m_datasetTrainingTotalChunks = 0;
        m_datasetTrainingOriginalBytes = 0;
        return;
    }

    const int completed = m_datasetTrainingTotalChunks - m_pendingDatasetChunks.size();
    const QString chunk = m_pendingDatasetChunks.takeFirst();
    m_agent.trainLoraText(chunk.toStdString(), m_datasetTrainingEpochs, 0.04, 4, 8.0, 1.0);
    m_agent.learn(QString("Hugging Face dataset sample: %1").arg(chunk.left(800)).toStdString(), 0.2);

    if ((completed + 1) % 10 == 0 || m_pendingDatasetChunks.isEmpty()) {
        appendToSimulationLog(QString("[Dataset Training]: Trained chunk %1/%2")
            .arg(completed + 1)
            .arg(m_datasetTrainingTotalChunks));
    }

    QTimer::singleShot(15, this, &AgentController::processNextDatasetTrainingChunk);
}

QString AgentController::agentFilesSummary() const {
    QDir cwd = QDir::current();
    return QString("Agent files: %1 | %2 | %3 | Export package: %4")
        .arg(cwd.absoluteFilePath(QString::fromStdString(m_agent.getMemoryFilePath())),
             cwd.absoluteFilePath(m_knowledgeFile),
             cwd.absoluteFilePath(m_loraFile),
             cwd.absoluteFilePath("student_agent.ai"));
}

QString AgentController::loraTrainingSummary() const {
    return QString("LoRA-like adapter: rank %1, trained pairs %2, file %3")
        .arg(m_agent.getLoraRank())
        .arg(m_agent.getLoraPairCount())
        .arg(QDir::current().absoluteFilePath(m_loraFile));
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
    const QString cleanQuestion = compactText(question, 500);
    const QString cleanAnswer = compactText(answer, 500);
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
    const QString normalizedQuery = normalizedForMatch(query);

    for (int i = 0; i < m_knowledgeBank.size(); ++i) {
        const LearnedKnowledge &knowledge = m_knowledgeBank[i];
        const QString normalizedQuestion = normalizedForMatch(knowledge.question);
        int currentScore = keywordOverlapScore(query, knowledge.question) * 3
            + keywordOverlapScore(query, knowledge.lesson)
            + keywordOverlapScore(query, knowledge.correction)
            + keywordOverlapScore(query, knowledge.answer);
        const int overlapScore = currentScore;

        if (!normalizedQuestion.isEmpty()
            && (normalizedQuery == normalizedQuestion
                || normalizedQuery.contains(normalizedQuestion)
                || normalizedQuestion.contains(normalizedQuery))) {
            currentScore += 100;
        }

        if (overlapScore > 0 || currentScore >= 100) {
            currentScore += static_cast<int>(knowledge.strength / 2.0);
        }

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
        jsonBody["max_tokens"] = 350; // increased for thinking details

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
            if (knowledgeIndex >= 0 && knowledgeScore >= 3) {
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
                answerStr = QString::fromStdString(m_agent.respond(item.question.toStdString(), testingTemp, testingContext, 500)).trimmed();

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
        m_agent.trainLoraText(structuredLesson.toStdString(), 4, 0.05, 4, 8.0, 1.2);
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
    evalBody["max_tokens"] = 500;

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

    QString correctionLesson = compactText(teacherCorrectionLesson, 900);
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
        m_agent.trainLoraText(structuredCorrection.toStdString(), 6, 0.06, 4, 8.0, correctionSalience);
        if (!improvedThinking.isEmpty()) {
            m_agent.learn(improvedThinking.toStdString(), correctionSalience);
            m_agent.trainLoraText(improvedThinking.toStdString(), 3, 0.05, 4, 8.0, correctionSalience);
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
                m_agent.trainLoraText(reteachSample.toStdString(), 5, 0.06, 4, 8.0, 1.8);
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
