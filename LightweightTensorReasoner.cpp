#include "LightweightTensorReasoner.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace {

enum Feature {
    Bias = 0,
    StartsWhat,
    StartsWhy,
    StartsHow,
    StartsGive,
    StartsCreate,
    StartsTell,
    HasQuestionMark,
    HasMath,
    HasNumber,
    HasProblem,
    HasStory,
    HasAgain,
    HasCause,
    HasCompare,
    HasDefine,
    HasFormula,
    HasProcedure,
    HasGreeting,
    HasStatus,
    HasCapability,
    HasPhysics,
    HasGeometry,
    HasAge,
    HasExample,
    HasExplain,
    HasGenerateVerb,
    HasAnswerVerb,
    TokenCountSmall,
    TokenCountMedium,
    TokenCountLarge,
    HasCreative,
    HasReasoning,
    HasCorrection,
    HasPriority,
    HasUnknownCue
};

enum Intent {
    IntentGreeting = 0,
    IntentStatus,
    IntentCapability,
    IntentGeneratePractice,
    IntentGenerateStory,
    IntentMath,
    IntentDefinition,
    IntentCausal,
    IntentCompare,
    IntentProcedure
};

QString normalized(QString text)
{
    text = text.toLower();
    text.replace(QRegularExpression("[^a-z0-9?]+"), " ");
    text.replace(QRegularExpression("\\s+"), " ");
    return text.trimmed();
}

bool startsWithAny(const QString &text, const QStringList &prefixes)
{
    for (const QString &prefix : prefixes) {
        if (text.startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

bool containsAny(const QString &text, const QStringList &words)
{
    for (const QString &word : words) {
        if (text.contains(word)) {
            return true;
        }
    }
    return false;
}

float sigmoid(float value)
{
    return 1.0f / (1.0f + std::exp(-value));
}

QString intentName(int intent)
{
    switch (intent) {
    case IntentGreeting: return "greeting";
    case IntentStatus: return "status";
    case IntentCapability: return "capability";
    case IntentGeneratePractice: return "practice-generation";
    case IntentGenerateStory: return "story-generation";
    case IntentMath: return "mathematical reasoning";
    case IntentDefinition: return "definition";
    case IntentCausal: return "cause-effect reasoning";
    case IntentCompare: return "comparison";
    case IntentProcedure: return "procedure";
    default: return "general reasoning";
    }
}

QString operationName(int intent)
{
    switch (intent) {
    case IntentGreeting: return "respond conversationally";
    case IntentStatus: return "answer current status";
    case IntentCapability: return "summarize available skills";
    case IntentGeneratePractice: return "generate a new practice item";
    case IntentGenerateStory: return "compose a concrete short story";
    case IntentMath: return "identify quantities and operations";
    case IntentDefinition: return "state the core meaning and apply it";
    case IntentCausal: return "trace cause to effect";
    case IntentCompare: return "separate the roles of two ideas";
    case IntentProcedure: return "give an ordered method";
    default: return "retrieve relevant learning and verify fit";
    }
}

QString answerShapeName(int intent)
{
    switch (intent) {
    case IntentGeneratePractice: return "new prompt plus enough details to solve it";
    case IntentGenerateStory: return "short narrative with a character, conflict, and resolution";
    case IntentMath: return "calculation, rule, and final result";
    case IntentDefinition: return "definition plus one sentence explaining why it fits";
    case IntentCausal: return "because-chain from trigger to result";
    case IntentCompare: return "clear difference between the compared ideas";
    case IntentProcedure: return "step-by-step answer";
    default: return "direct answer checked against the request";
    }
}

} // namespace

LightweightTensorReasoner::FeatureVector LightweightTensorReasoner::encode(const QString &input) const
{
    FeatureVector features = {};
    const QString text = normalized(input);
    const QString compact = QString(text).remove(' ');
    const QStringList tokens = text.split(' ', Qt::SkipEmptyParts);

    features[Bias] = 1.0f;
    features[StartsWhat] = text.startsWith("what ");
    features[StartsWhy] = text.startsWith("why ");
    features[StartsHow] = text.startsWith("how ");
    features[StartsGive] = startsWithAny(text, {"give ", "give me ", "provide "});
    features[StartsCreate] = startsWithAny(text, {"create ", "generate ", "write ", "make ", "compose ", "ask me ", "quiz me "});
    features[StartsTell] = text.startsWith("tell me ");
    features[HasQuestionMark] = input.contains('?');
    features[HasMath] = containsAny(text, {"math", "mathematic", "algebra", "arithmetic", "calculate", "solve"});
    features[HasNumber] = text.contains(QRegularExpression("\\d"));
    features[HasProblem] = containsAny(text, {"problem", "problems", "question", "questions", "quiz"});
    features[HasStory] = containsAny(text, {"story", "stories", "narrative", "tale"});
    features[HasAgain] = containsAny(text, {"again", "another", "more", "next"}) || compact == "aagain";
    features[HasCause] = containsAny(text, {"why", "cause", "causes", "because", "effect", "leads", "result", "happen"});
    features[HasCompare] = containsAny(text, {"difference", "compare", "versus", "vs", "better", "advantage", "best"});
    features[HasDefine] = containsAny(text, {"what is", "what are", "define", "meaning", "means", "term"});
    features[HasFormula] = containsAny(text, {"formula", "equation", "theorem", "rule"});
    features[HasProcedure] = containsAny(text, {"how", "steps", "method", "process", "procedure", "way to"});
    features[HasGreeting] = compact == "hi" || compact == "hello" || compact == "hey" || compact == "heythere" || compact == "hellothere";
    features[HasStatus] = containsAny(text, {"how are you", "how do you feel", "are you running"});
    features[HasCapability] = containsAny(text, {"capability", "capabilities", "capabiltiies", "skill", "skills", "what can you do"});
    features[HasPhysics] = containsAny(text, {"physics", "force", "mass", "weight", "motion", "energy", "pressure"});
    features[HasGeometry] = containsAny(text, {"geometry", "triangle", "circle", "radius", "diameter", "angle"});
    features[HasAge] = containsAny(text, {"age", "older", "younger"});
    features[HasExample] = containsAny(text, {"example", "sample"});
    features[HasExplain] = containsAny(text, {"explain", "why", "reason"});
    features[HasGenerateVerb] = containsAny(text, {"create", "generate", "write", "make", "compose", "give me", "another"});
    features[HasAnswerVerb] = containsAny(text, {"answer", "tell", "show", "explain"});
    features[TokenCountSmall] = tokens.size() <= 3;
    features[TokenCountMedium] = tokens.size() > 3 && tokens.size() <= 12;
    features[TokenCountLarge] = tokens.size() > 12;
    features[HasCreative] = containsAny(text, {"creative", "story", "paragraph", "essay"});
    features[HasReasoning] = containsAny(text, {"reason", "reasoning", "think", "thinking", "understand"});
    features[HasCorrection] = containsAny(text, {"correction", "correct", "teacher", "lesson", "learned"});
    features[HasPriority] = containsAny(text, {"priority", "prioritize", "urgent", "important"});
    features[HasUnknownCue] = containsAny(text, {"unknown", "not sure", "confused"});

    return features;
}

LightweightTensorReasoner::ScoreVector LightweightTensorReasoner::score(const FeatureVector &f) const
{
    ScoreVector s = {};

    s[IntentGreeting] = 2.9f * f[HasGreeting] - 0.7f * f[HasProblem];
    s[IntentStatus] = 2.8f * f[HasStatus] + 0.5f * f[StartsHow] - 0.5f * f[HasProcedure];
    s[IntentCapability] = 2.7f * f[HasCapability] + 0.7f * f[StartsWhat] + 0.4f * f[StartsTell];

    s[IntentGeneratePractice] =
        1.4f * f[StartsGive] + 1.5f * f[StartsCreate] + 1.4f * f[HasGenerateVerb]
        + 1.2f * f[HasProblem] + 0.9f * f[HasMath] + 0.7f * f[HasAge]
        + 0.5f * f[HasAgain] - 1.6f * f[HasStory];

    s[IntentGenerateStory] =
        1.5f * f[StartsCreate] + 1.5f * f[HasGenerateVerb] + 2.4f * f[HasStory]
        + 0.8f * f[HasCreative] + 0.4f * f[HasQuestionMark];

    s[IntentMath] =
        1.9f * f[HasMath] + 1.2f * f[HasNumber] + 0.8f * f[HasFormula]
        + 0.7f * f[HasGeometry] + 0.6f * f[HasAge] + 0.5f * f[HasProblem];

    s[IntentDefinition] =
        1.5f * f[StartsWhat] + 1.4f * f[HasDefine] + 0.7f * f[HasFormula]
        + 0.3f * f[HasQuestionMark] - 0.5f * f[HasCause];

    s[IntentCausal] =
        1.8f * f[StartsWhy] + 1.8f * f[HasCause] + 0.5f * f[HasPhysics]
        + 0.4f * f[HasCorrection] + 0.4f * f[HasPriority];

    s[IntentCompare] =
        2.2f * f[HasCompare] + 0.6f * f[HasQuestionMark] + 0.5f * f[TokenCountLarge];

    s[IntentProcedure] =
        1.7f * f[StartsHow] + 1.6f * f[HasProcedure] + 0.7f * f[HasExplain]
        + 0.4f * f[HasReasoning] - 0.4f * f[HasStatus];

    return s;
}

QStringList LightweightTensorReasoner::focusTerms(const QString &input, int limit) const
{
    static const QStringList stopWords = {
        "what", "why", "how", "when", "where", "who", "which", "the", "a", "an",
        "is", "are", "do", "does", "did", "to", "for", "of", "and", "or", "me",
        "you", "your", "give", "create", "generate", "tell", "please", "another"
    };

    QStringList focus;
    for (QString token : normalized(input).split(' ', Qt::SkipEmptyParts)) {
        token.remove('?');
        if (token.size() < 2 || stopWords.contains(token) || focus.contains(token)) {
            continue;
        }
        focus.append(token);
        if (focus.size() >= limit) {
            break;
        }
    }
    return focus;
}

LightweightTensorReasoner::Result LightweightTensorReasoner::analyze(const QString &input) const
{
    const FeatureVector features = encode(input);
    const ScoreVector scores = score(features);

    int best = 0;
    int second = 0;
    for (int i = 1; i < IntentCount; ++i) {
        if (scores[i] > scores[best]) {
            second = best;
            best = i;
        } else if (i != best && scores[i] > scores[second]) {
            second = i;
        }
    }

    const bool explicitPrompt = features[HasQuestionMark] > 0.0f
        || features[StartsWhat] > 0.0f
        || features[StartsWhy] > 0.0f
        || features[StartsHow] > 0.0f
        || features[StartsGive] > 0.0f
        || features[StartsCreate] > 0.0f
        || features[StartsTell] > 0.0f
        || features[HasGreeting] > 0.0f
        || features[HasStatus] > 0.0f
        || features[HasCapability] > 0.0f;

    if (scores[best] < 0.65f && !explicitPrompt) {
        Result result;
        result.intent = "statement";
        result.operation = "understand the main idea without treating it as training";
        result.answerShape = "brief acknowledgement";
        result.focusTerms = focusTerms(input);
        result.confidence = 0.55f;
        result.questionLike = false;
        result.generative = false;
        result.conversational = false;
        return result;
    }

    const float margin = scores[best] - scores[second];
    Result result;
    result.intent = intentName(best);
    result.operation = operationName(best);
    result.answerShape = answerShapeName(best);
    result.focusTerms = focusTerms(input);
    result.confidence = std::max(0.05f, std::min(0.99f, sigmoid(scores[best] + margin * 0.35f)));
    result.generative = best == IntentGeneratePractice || best == IntentGenerateStory;
    result.conversational = best == IntentGreeting || best == IntentStatus || best == IntentCapability;
    const bool impliedRequest = (features[HasProblem] > 0.0f && features[HasGenerateVerb] > 0.0f)
        || (features[HasAgain] > 0.0f && features[HasProblem] > 0.0f)
        || features[HasAnswerVerb] > 0.0f
        || features[HasExplain] > 0.0f;

    result.questionLike = result.generative
        || result.conversational
        || explicitPrompt
        || impliedRequest;

    return result;
}

QString LightweightTensorReasoner::visiblePlan(const Result &result, const QString &input) const
{
    QString focus = result.focusTerms.isEmpty()
        ? "the request wording"
        : result.focusTerms.join(", ");
    focus.replace(']', ')');

    QString cleanInput = input;
    cleanInput.replace(QRegularExpression("\\s+"), " ");
    cleanInput = cleanInput.trimmed();
    if (cleanInput.size() > 120) {
        cleanInput = cleanInput.left(117).trimmed() + "...";
    }
    cleanInput.replace(']', ')');

    return QString("I classify \"%1\" as %2, keep %3 in working memory, and use the plan: %4. The expected answer shape is %5")
        .arg(cleanInput,
             result.intent,
             focus,
             result.operation,
             result.answerShape);
}
