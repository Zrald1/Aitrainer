#ifndef SYNAPSE_H
#define SYNAPSE_H
#include <string>

// Basic connection unit between concepts.
// Real brain: synapses strengthen with use (Hebbian learning).
struct Synapse {
    std::string target;
    double weight;
};

#endif
