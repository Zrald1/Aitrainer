#include "Cerebellum.h"

void Cerebellum::practice(const std::string& skill) {
    practiceCount[skill]++;
}

double Cerebellum::skillLevel(const std::string& skill) {
    auto it = practiceCount.find(skill);
    return it != practiceCount.end() ? it->second * 0.05 : 0.0;
}
