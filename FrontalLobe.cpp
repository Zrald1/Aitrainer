#include "FrontalLobe.h"
#include <vector>
#include <algorithm>

FrontalLobe::FrontalLobe(TemporalLobe* t, BasalGanglia* g, Amygdala* a)
    : temporal(t), ganglia(g), amygdala(a) {}

std::string FrontalLobe::think(const std::string& seed, int maxWords, double temperature) {
    std::string thought = seed;
    std::string current = seed;
    
    std::vector<std::string> path = {current};

    for (int i = 1; i < maxWords; i++) {
        // Retrieve choices directly from Hippocampus via TemporalLobe
        auto it = temporal->hippo->longTermMemory.find(current);
        if (it == temporal->hippo->longTermMemory.end() || it->second.empty()) {
            break;
        }

        // Basal Ganglia selects the next action stochastically based on weight + emotional values
        std::string next = ganglia->selectAction(it->second, *amygdala, temperature);
        if (next.empty()) {
            break;
        }

        // Biological loop prevention: if a word is repeated too many times in the active path, break
        int visitCount = 0;
        for (const auto& p : path) {
            if (p == next) visitCount++;
        }
        if (visitCount > 3) {
            break;
        }

        thought += " " + next;
        path.push_back(next);
        current = next;
    }
    return thought;
}
