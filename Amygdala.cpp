#include "Amygdala.h"

double Amygdala::evaluate(const std::string& concept) {
    auto it = emotionalValue.find(concept);
    return it != emotionalValue.end() ? it->second : 0.0;
}

void Amygdala::reward(const std::string& concept, double value) {
    emotionalValue[concept] += value;
}
