#ifndef LORAADAPTER_H
#define LORAADAPTER_H

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

class LoraAdapter {
private:
    int m_rank;
    double m_alpha;
    std::map<std::string, std::vector<double>> m_a;
    std::map<std::string, std::vector<double>> m_b;
    std::set<std::pair<std::string, std::string>> m_trainedPairs;

    std::vector<double> initializedVector(const std::string &token, int salt) const;
    std::vector<double> &ensureVector(std::map<std::string, std::vector<double>> &matrix,
                                      const std::string &token,
                                      int salt);

public:
    explicit LoraAdapter(int rank = 4, double alpha = 8.0);

    void configure(int rank, double alpha);
    void clear();

    int rank() const;
    double alpha() const;
    int pairCount() const;

    double delta(const std::string &from, const std::string &to) const;
    void trainPair(const std::string &from,
                   const std::string &to,
                   double targetDelta,
                   double learningRate);
    void trainSequence(const std::vector<std::string> &tokens,
                       int epochs,
                       double learningRate,
                       double salience);

    std::vector<std::pair<std::pair<std::string, std::string>, double>> materializedDeltas(double minimumDelta) const;

    bool save(const std::string &filePath) const;
    bool load(const std::string &filePath);
};

#endif
