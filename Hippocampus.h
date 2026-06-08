#ifndef HIPPOCAMPUS_H
#define HIPPOCAMPUS_H
#include "Synapse.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>

// Forms, stores, strengthens, and forgets long-term memories.
class Hippocampus {
public:
    std::unordered_map<std::string, std::vector<Synapse>> longTermMemory;

    void formMemory(const std::string& from, const std::string& to, double salience = 0.1);
    void forget(double decay = 0.01, double pruneThreshold = 0.05); // weaken unused memories and prune
    void save(const std::string& file);
    void load(const std::string& file);
    size_t conceptCount() const;
};

#endif
