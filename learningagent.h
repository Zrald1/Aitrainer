#ifndef LEARNINGAGENT_H
#define LEARNINGAGENT_H

#include "Brain.h"
#include "loraadapter.h"
#include <string>
#include <unordered_map>
#include <vector>

class LearningAgent {
private:
    Brain brain;
    LoraAdapter lora;
    std::string memoryFile = "agent_memory.txt";
    std::string loraFile = "lora_adapter.txt";

public:
    LearningAgent();

    // Hebbian learning methods
    void learn(const std::string& input);
    void learn(const std::string& input, double salience);

    // Stochastic response generation
    std::string respond(const std::string& input, double temperature = 0.5, int contextWindow = 2, int maxWords = 150);

    // Pure C++ LoRA-like adapter training
    bool trainLoraText(const std::string& input,
                       int epochs = 4,
                       double learningRate = 0.05,
                       int rank = 4,
                       double alpha = 8.0,
                       double salience = 1.0);
    void mergeLora(double minimumDelta = 0.01);
    bool saveLora(const std::string& filePath = "");
    bool loadLora(const std::string& filePath = "");
    void clearLora();
    int getLoraPairCount() const;
    int getLoraRank() const;

    // Synaptic Pruning / Decay
    void applySynapticDecay(double decayRate = 0.99, double pruneThreshold = 0.05);

    // Persistence
    bool save(const std::string& filePath = "");
    bool load(const std::string& filePath = "");
    void clear();

    // Setters/Getters for UI Stats
    int getVocabularySize() const;
    int getTotalAssociationsCount() const;
    std::string getMemoryFilePath() const;
    void setMemoryFilePath(const std::string& path);

    // Inspector functions for UI
    std::unordered_map<std::string, int> getAssociations(const std::string& word) const;
    std::vector<std::pair<std::pair<std::string, std::string>, int>> getTopAssociations(int limit) const;
};

#endif // LEARNINGAGENT_H
