#ifndef TEMPORALLOBE_H
#define TEMPORALLOBE_H
#include "Hippocampus.h"
#include <string>

// Language & memory recall.
class TemporalLobe {
public:
    Hippocampus* hippo;
    TemporalLobe(Hippocampus* h);

    std::string recall(const std::string& word);
};

#endif
