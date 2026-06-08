#include "Brain.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>

namespace {

std::string trimCopy(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }

    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(start, end - start);
}

std::string lowerCopy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::string normalizeSpaces(const std::string& text) {
    std::string output;
    bool pendingSpace = false;
    for (char ch : text) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isspace(uch)) {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace && !output.empty()) {
            output += ' ';
        }
        output += ch;
        pendingSpace = false;
    }
    return trimCopy(output);
}

std::string stripTrainingPrefix(std::string sentence) {
    sentence = trimCopy(sentence);
    const std::string lower = lowerCopy(sentence);
    const std::vector<std::string> prefixes = {
        "question:", "answer:", "lesson:", "correction:", "statement:",
        "verified response:", "thinking:", "dataset text sample:",
        "lora training sample:"
    };

    for (const std::string& prefix : prefixes) {
        if (lower.rfind(prefix, 0) == 0) {
            return trimCopy(sentence.substr(prefix.size()));
        }
    }
    return sentence;
}

std::vector<std::string> splitSentenceCandidates(const std::string& text) {
    std::vector<std::string> result;
    std::string current;

    auto flush = [&]() {
        std::string sentence = stripTrainingPrefix(normalizeSpaces(current));
        current.clear();
        if (!sentence.empty()) {
            result.push_back(sentence);
        }
    };

    for (char ch : text) {
        current += ch;
        if (ch == '.' || ch == '?' || ch == '!' || ch == '\n' || ch == '\r') {
            flush();
        }
    }
    flush();

    return result;
}

int wordCount(const std::string& text) {
    std::stringstream ss(text);
    int count = 0;
    std::string word;
    while (ss >> word) {
        ++count;
    }
    return count;
}

bool hasMostlyReadableCharacters(const std::string& text) {
    if (text.empty()) {
        return false;
    }

    int readable = 0;
    int total = 0;
    for (char ch : text) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isspace(uch)) {
            continue;
        }
        ++total;
        if (std::isalnum(uch) || std::ispunct(uch)) {
            ++readable;
        }
    }

    return total == 0 || readable * 10 >= total * 8;
}

bool isUsefulSentence(const std::string& sentence) {
    const std::string clean = normalizeSpaces(sentence);
    const int words = wordCount(clean);
    if (words < 3) {
        return false;
    }
    if (!hasMostlyReadableCharacters(clean)) {
        return false;
    }

    const std::string lower = lowerCopy(clean);
    if (lower.find("http://") != std::string::npos
        || lower.find("https://") != std::string::npos
        || lower.find("{") != std::string::npos
        || lower.find("}") != std::string::npos
        || lower.find("[thinking:") != std::string::npos) {
        return false;
    }

    return true;
}

std::string polishEnglishSentence(std::string text) {
    text = normalizeSpaces(text);
    if (text.empty()) {
        return "";
    }

    while (!text.empty() && (text.front() == '"' || text.front() == '\'' || text.front() == '-')) {
        text.erase(text.begin());
        text = trimCopy(text);
    }

    if (!text.empty()) {
        text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    }

    auto replaceWholeWord = [&](const std::string& from, const std::string& to) {
        std::string lower = lowerCopy(text);
        size_t pos = 0;
        while ((pos = lower.find(from, pos)) != std::string::npos) {
            const bool leftOk = pos == 0 || !std::isalnum(static_cast<unsigned char>(lower[pos - 1]));
            const size_t right = pos + from.size();
            const bool rightOk = right >= lower.size() || !std::isalnum(static_cast<unsigned char>(lower[right]));
            if (leftOk && rightOk) {
                text.replace(pos, from.size(), to);
                lower.replace(pos, from.size(), lowerCopy(to));
                pos += to.size();
            } else {
                pos += from.size();
            }
        }
    };

    replaceWholeWord(" i ", " I ");
    replaceWholeWord(" i'm ", " I am ");
    replaceWholeWord(" ive ", " I have ");
    replaceWholeWord(" dont ", " do not ");
    replaceWholeWord(" cant ", " cannot ");

    if (!text.empty()) {
        const char last = text.back();
        if (last != '.' && last != '?' && last != '!') {
            text += '.';
        }
    }
    return text;
}

std::unordered_set<std::string> significantTokenSet(const std::vector<std::string>& tokens) {
    static const std::unordered_set<std::string> stopWords = {
        "a", "an", "and", "are", "as", "at", "be", "by", "can", "do", "does",
        "for", "from", "how", "i", "in", "is", "it", "me", "my", "of", "on",
        "or", "that", "the", "this", "to", "was", "what", "when", "where",
        "which", "who", "why", "with", "you", "your"
    };

    std::unordered_set<std::string> output;
    for (const std::string& token : tokens) {
        if (token.size() < 3 || stopWords.find(token) != stopWords.end()) {
            continue;
        }
        output.insert(token);
    }
    return output;
}

}

Brain::Brain()
    : temporal(&hippocampus),
      frontal(&temporal, &basalGanglia, &amygdala) {
    hippocampus.load("agent_memory.txt"); // Compatible default path
}

void Brain::learn(const std::string& text, double salience) {
    // 1. Thalamus routes input
    std::vector<std::string> tokens = thalamus.route(text);
    if (tokens.empty()) return;

    // 2. Hippocampus forms memories; Cerebellum practices
    for (size_t i = 0; i + 1 < tokens.size(); i++) {
        hippocampus.formMemory(tokens[i], tokens[i + 1], salience);
        cerebellum.practice(tokens[i]);
    }

    // 3. Amygdala rewards learning
    for (const auto& t : tokens) {
        amygdala.reward(t, salience * 0.1);
    }

    const std::vector<std::string> candidates = splitSentenceCandidates(text);
    for (const std::string& candidate : candidates) {
        const std::string polished = polishEnglishSentence(candidate);
        if (!isUsefulSentence(polished)) {
            continue;
        }

        const std::string normalizedCandidate = lowerCopy(polished);
        bool exists = false;
        for (const std::string& existing : sentenceMemory) {
            if (lowerCopy(existing) == normalizedCandidate) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            sentenceMemory.push_back(polished);
        }
    }

    const size_t maxSentences = 2000;
    if (sentenceMemory.size() > maxSentences) {
        const auto eraseCount = static_cast<std::vector<std::string>::difference_type>(sentenceMemory.size() - maxSentences);
        sentenceMemory.erase(sentenceMemory.begin(),
                             sentenceMemory.begin() + eraseCount);
    }
}

std::string Brain::process(const std::string& input, double temperature, int maxWords) {
    std::vector<std::string> tokens = thalamus.route(input);
    if (tokens.empty()) return "...";
    if (maxWords <= 0) {
        maxWords = 4096;
    }

    // Robust concept seed selection: find the first known token (from back to front)
    std::string seed = "";
    for (int i = static_cast<int>(tokens.size()) - 1; i >= 0; --i) {
        if (hippocampus.longTermMemory.find(tokens[i]) != hippocampus.longTermMemory.end()) {
            seed = tokens[i];
            break;
        }
    }

    // Fallback if no token was matched in memory
    if (seed.empty()) {
        seed = tokens.back();
    }

    if (hippocampus.longTermMemory.empty()) {
        return "i need to learn more words first. tell me something!";
    }

    const std::unordered_set<std::string> queryTokens = significantTokenSet(tokens);
    if (!sentenceMemory.empty() && !queryTokens.empty()) {
        int bestScore = 0;
        std::string bestSentence;
        const std::string normalizedInput = lowerCopy(normalizeSpaces(input));

        for (const std::string& sentence : sentenceMemory) {
            const std::string normalizedSentence = lowerCopy(normalizeSpaces(sentence));
            if (normalizedSentence == normalizedInput) {
                continue;
            }

            const std::vector<std::string> sentenceTokens = thalamus.route(sentence);
            const std::unordered_set<std::string> sentenceTokenSet = significantTokenSet(sentenceTokens);
            int score = 0;
            for (const std::string& token : queryTokens) {
                if (sentenceTokenSet.find(token) != sentenceTokenSet.end()) {
                    score += 3;
                } else if (normalizedSentence.find(token) != std::string::npos) {
                    score += 1;
                }
            }

            if (normalizedSentence.find(" is ") != std::string::npos
                || normalizedSentence.find(" are ") != std::string::npos
                || normalizedSentence.find(" means ") != std::string::npos) {
                score += 1;
            }

            if (score > bestScore) {
                bestScore = score;
                bestSentence = sentence;
            }
        }

        if (bestScore >= 3 && !bestSentence.empty()) {
            (void)temperature;
            (void)maxWords;
            return polishEnglishSentence(bestSentence);
        }
    }

    // Frontal lobe thinking and response generation
    const std::string rawThought = frontal.think(seed, maxWords, temperature);
    if (wordCount(rawThought) < 3) {
        return "i do not have enough learned language yet to form a clear sentence.";
    }
    return polishEnglishSentence(rawThought);
}

void Brain::rememberPermanently() {
    hippocampus.save("agent_memory.txt");
}

bool Brain::saveSentenceMemory(const std::string& file) const {
    std::ofstream out(file);
    if (!out.is_open()) {
        return false;
    }

    for (const std::string& sentence : sentenceMemory) {
        out << sentence << "\n";
    }
    return true;
}

bool Brain::loadSentenceMemory(const std::string& file) {
    std::ifstream in(file);
    if (!in.is_open()) {
        return true;
    }

    sentenceMemory.clear();
    std::string line;
    while (std::getline(in, line)) {
        const std::string sentence = polishEnglishSentence(line);
        if (isUsefulSentence(sentence)) {
            sentenceMemory.push_back(sentence);
        }
    }
    return true;
}

void Brain::clearSentenceMemory() {
    sentenceMemory.clear();
}

void Brain::showStats() {
    std::cout << "Known concepts: " << hippocampus.conceptCount() << "\n";
    std::cout << "Learned sentences: " << sentenceMemory.size() << "\n";
}
