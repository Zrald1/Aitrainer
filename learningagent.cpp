#include "learningagent.h"
#include <algorithm>
#include <utility>

LearningAgent::LearningAgent() {
    load();
    loadLora();
}

void LearningAgent::learn(const std::string& input) {
    brain.learn(input, 1.0);
}

void LearningAgent::learn(const std::string& input, double salience) {
    brain.learn(input, salience);
}

std::string LearningAgent::respond(const std::string& input, double temperature, int contextWindow, int maxWords) {
    (void)contextWindow; // TemporalLobe and FrontalLobe encapsulate this logic
    return brain.process(input, temperature, maxWords);
}

bool LearningAgent::trainLoraText(const std::string& input,
                                  int epochs,
                                  double learningRate,
                                  int rank,
                                  double alpha,
                                  double salience) {
    std::vector<std::string> tokens = brain.thalamus.route(input);
    if (tokens.size() < 2) {
        return false;
    }

    lora.configure(rank, alpha);
    lora.trainSequence(tokens, epochs, learningRate, salience);
    mergeLora(0.001);
    return true;
}

void LearningAgent::mergeLora(double minimumDelta) {
    auto deltas = lora.materializedDeltas(minimumDelta);
    for (const auto& item : deltas) {
        brain.hippocampus.formMemory(item.first.first, item.first.second, item.second);
    }
}

bool LearningAgent::saveLora(const std::string& filePath) {
    std::string path = filePath.empty() ? loraFile : filePath;
    return lora.save(path);
}

bool LearningAgent::loadLora(const std::string& filePath) {
    std::string path = filePath.empty() ? loraFile : filePath;
    return lora.load(path);
}

void LearningAgent::clearLora() {
    lora.clear();
}

int LearningAgent::getLoraPairCount() const {
    return lora.pairCount();
}

int LearningAgent::getLoraRank() const {
    return lora.rank();
}

void LearningAgent::applySynapticDecay(double decayRate, double pruneThreshold) {
    // decayRate is subtraction-based in our Hippocampus module
    // Let's map multiplicative decay to subtraction value: decayFactor = 1.0 - decayRate
    double decaySub = 1.0 - decayRate;
    if (decaySub <= 0.0) decaySub = 0.01;
    brain.hippocampus.forget(decaySub, pruneThreshold);
}

bool LearningAgent::save(const std::string& filePath) {
    std::string path = filePath.empty() ? memoryFile : filePath;
    brain.hippocampus.save(path);
    return true;
}

bool LearningAgent::load(const std::string& filePath) {
    std::string path = filePath.empty() ? memoryFile : filePath;
    brain.hippocampus.load(path);
    return true;
}

void LearningAgent::clear() {
    brain.hippocampus.longTermMemory.clear();
    brain.cerebellum.practiceCount.clear();
    brain.amygdala.emotionalValue.clear();
    clearLora();
}

int LearningAgent::getVocabularySize() const {
    return static_cast<int>(brain.hippocampus.conceptCount());
}

int LearningAgent::getTotalAssociationsCount() const {
    int total = 0;
    for (const auto& pair : brain.hippocampus.longTermMemory) {
        total += pair.second.size();
    }
    return total;
}

std::string LearningAgent::getMemoryFilePath() const {
    return memoryFile;
}

void LearningAgent::setMemoryFilePath(const std::string& path) {
    memoryFile = path;
}

std::unordered_map<std::string, int> LearningAgent::getAssociations(const std::string& word) const {
    std::vector<std::string> cleanList = brain.thalamus.route(word);
    if (cleanList.empty()) return {};
    std::string clean = cleanList.front();
    
    auto it = brain.hippocampus.longTermMemory.find(clean);
    if (it != brain.hippocampus.longTermMemory.end()) {
        std::unordered_map<std::string, int> res;
        for (const auto& s : it->second) {
            res[s.target] = static_cast<int>(s.weight * 10); // scale up for integer count representation
        }
        return res;
    }
    return {};
}

std::vector<std::pair<std::pair<std::string, std::string>, int>> LearningAgent::getTopAssociations(int limit) const {
    std::vector<std::pair<std::pair<std::string, std::string>, int>> associations;
    if (limit <= 0) {
        return associations;
    }
    associations.reserve(static_cast<size_t>(limit));

    for (const auto& pair : brain.hippocampus.longTermMemory) {
        for (const auto& s : pair.second) {
            const int weight = static_cast<int>(s.weight * 10);
            auto candidate = std::make_pair(std::make_pair(pair.first, s.target), weight);
            if (static_cast<int>(associations.size()) < limit) {
                associations.push_back(std::move(candidate));
                std::sort(associations.begin(), associations.end(), [](const auto& a, const auto& b) {
                    return a.second > b.second;
                });
            } else if (weight > associations.back().second) {
                associations.back() = std::move(candidate);
                std::sort(associations.begin(), associations.end(), [](const auto& a, const auto& b) {
                    return a.second > b.second;
                });
            }
        }
    }
    return associations;
}
