#include "Hippocampus.h"

void Hippocampus::formMemory(const std::string& from, const std::string& to, double salience) {
    auto& synapses = longTermMemory[from];
    for (auto& s : synapses) {
        if (s.target == to) {
            s.weight += salience;
            return;
        } // reinforce
    }
    synapses.push_back({to, salience}); // new connection
}

// Biological brains weaken memories that aren't reinforced (forgetting curve)
void Hippocampus::forget(double decay, double pruneThreshold) {
    for (auto& node : longTermMemory) {
        auto& synapses = node.second;
        auto it = synapses.begin();
        while (it != synapses.end()) {
            it->weight -= decay;
            if (it->weight < pruneThreshold) {
                it = synapses.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void Hippocampus::save(const std::string& file) {
    std::ofstream out(file);
    if (!out.is_open()) return;
    for (auto& n : longTermMemory) {
        for (auto& s : n.second) {
            if (s.weight > 0) {
                out << n.first << " " << s.target << " " << s.weight << "\n";
            }
        }
    }
}

void Hippocampus::load(const std::string& file) {
    std::ifstream in(file);
    if (!in.is_open()) return;
    longTermMemory.clear();
    std::string a, b; double w;
    while (in >> a >> b >> w) {
        longTermMemory[a].push_back({b, w});
    }
}

size_t Hippocampus::conceptCount() const {
    return longTermMemory.size();
}
