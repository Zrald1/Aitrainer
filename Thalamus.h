#ifndef THALAMUS_H
#define THALAMUS_H
#include <string>
#include <vector>

// Input router: cleans, tokenizes, and dispatches signals.
class Thalamus {
public:
    std::vector<std::string> route(const std::string& input) const;
};

#endif
