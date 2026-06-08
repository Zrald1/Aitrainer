#ifndef BASALGANGLIA_H
#define BASALGANGLIA_H
#include "Synapse.h"
#include "Amygdala.h"
#include <string>
#include <vector>
#include <random>

// Selects which action/habit to perform stochastically.
class BasalGanglia {
private:
    std::mt19937 rng;

public:
    BasalGanglia();
    std::string selectAction(const std::vector<Synapse>& options,
                             Amygdala& amyg, double temperature = 0.5);
};

#endif
