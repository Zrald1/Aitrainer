#include "TemporalLobe.h"

TemporalLobe::TemporalLobe(Hippocampus* h) : hippo(h) {}

std::string TemporalLobe::recall(const std::string& word) {
    auto it = hippo->longTermMemory.find(word);
    if (it == hippo->longTermMemory.end() || it->second.empty()) return "";

    double best = -1.0;
    std::string result;
    for (const auto& s : it->second) {
        if (s.weight > best) {
            best = s.weight;
            result = s.target;
        }
    }
    return result;
}
