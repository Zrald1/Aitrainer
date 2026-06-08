#ifndef FRONTALLOBE_H
#define FRONTALLOBE_H
#include "TemporalLobe.h"
#include "BasalGanglia.h"
#include "Amygdala.h"
#include <string>

// Executive: plans, reasons, and builds thoughts.
class FrontalLobe {
public:
    TemporalLobe* temporal;
    BasalGanglia* ganglia;
    Amygdala* amygdala;

    FrontalLobe(TemporalLobe* t, BasalGanglia* g, Amygdala* a);
    
    // Frontal Lobe thinking with variable length and temperature-scaled selection
    std::string think(const std::string& seed, int maxWords = 150, double temperature = 0.5);
};

#endif
