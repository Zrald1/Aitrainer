#include "Brain.h"
#include <iostream>

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
}

std::string Brain::process(const std::string& input, double temperature, int maxWords) {
    std::vector<std::string> tokens = thalamus.route(input);
    if (tokens.empty()) return "...";

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

    // Frontal lobe thinking and response generation
    return frontal.think(seed, maxWords, temperature);
}

void Brain::rememberPermanently() {
    hippocampus.save("agent_memory.txt");
}

void Brain::showStats() {
    std::cout << "Known concepts: " << hippocampus.conceptCount() << "\n";
}
