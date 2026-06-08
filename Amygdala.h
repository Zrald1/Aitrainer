#ifndef AMYGDALA_H
#define AMYGDALA_H
#include <string>
#include <unordered_map>

// Assigns emotional weight / reward to concepts.
// Important things get remembered more strongly.
class Amygdala {
public:
    std::unordered_map<std::string, double> emotionalValue;

    double evaluate(const std::string& concept);
    void reward(const std::string& concept, double value);
};

#endif
