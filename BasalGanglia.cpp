#include "BasalGanglia.h"
#include <cmath>
#include <ctime>

BasalGanglia::BasalGanglia() {
    rng.seed(static_cast<unsigned int>(std::time(nullptr)));
}

std::string BasalGanglia::selectAction(const std::vector<Synapse>& options,
                                       Amygdala& amyg, double temperature) {
    if (options.empty()) return "";

    // Deterministic selection (argmax) for low temperature
    if (temperature <= 0.05) {
        std::string best = options[0].target;
        double bestScore = -9999.0;
        for (const auto& opt : options) {
            double score = opt.weight + amyg.evaluate(opt.target);
            if (score > bestScore) {
                bestScore = score;
                best = opt.target;
            }
        }
        return best;
    }

    // Stochastic selection with temperature scaling
    double power = 1.0 / temperature;
    std::vector<double> weights;
    std::vector<std::string> candidates;
    
    weights.reserve(options.size());
    candidates.reserve(options.size());

    for (const auto& opt : options) {
        double score = opt.weight + amyg.evaluate(opt.target);
        if (score < 0.01) score = 0.01; // Keep weight positive
        
        double w = std::pow(score, power);
        weights.push_back(w);
        candidates.push_back(opt.target);
    }

    std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
    size_t idx = dist(rng);
    return candidates[idx];
}
