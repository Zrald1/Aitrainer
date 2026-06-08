#ifndef BRAIN_H
#define BRAIN_H
#include "Hippocampus.h"
#include "Amygdala.h"
#include "Thalamus.h"
#include "TemporalLobe.h"
#include "Cerebellum.h"
#include "BasalGanglia.h"
#include "FrontalLobe.h"
#include <string>
#include <vector>

// Central system that connects all brain parts (the "brainstem").
class Brain {
public:
    Hippocampus    hippocampus;
    Amygdala       amygdala;
    Thalamus       thalamus;
    Cerebellum     cerebellum;
    BasalGanglia   basalGanglia;
    TemporalLobe   temporal;
    FrontalLobe    frontal;
    std::vector<std::string> sentenceMemory;

    Brain();
    
    // Core processes matching our active agent loop
    std::string process(const std::string& input, double temperature = 0.5, int maxWords = 150);
    void learn(const std::string& text, double salience = 0.1);
    void rememberPermanently();
    bool saveSentenceMemory(const std::string& file) const;
    bool loadSentenceMemory(const std::string& file);
    void clearSentenceMemory();
    void showStats();
};

#endif
