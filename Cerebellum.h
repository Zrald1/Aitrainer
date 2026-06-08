#ifndef CEREBELLUM_H
#define CEREBELLUM_H
#include <string>
#include <unordered_map>

// Refines skills through repetition.
class Cerebellum {
public:
    std::unordered_map<std::string, int> practiceCount;

    void practice(const std::string& skill);
    double skillLevel(const std::string& skill);
};

#endif
