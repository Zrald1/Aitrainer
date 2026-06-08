#include "loraadapter.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <sstream>

namespace {

int clampedRank(int rank)
{
    return std::max(1, std::min(rank, 16));
}

double clipped(double value, double low, double high)
{
    return std::max(low, std::min(value, high));
}

}

LoraAdapter::LoraAdapter(int rank, double alpha)
    : m_rank(clampedRank(rank))
    , m_alpha(alpha)
{
}

void LoraAdapter::configure(int rank, double alpha)
{
    rank = clampedRank(rank);
    if (rank != m_rank) {
        clear();
    }
    m_rank = rank;
    m_alpha = alpha;
}

void LoraAdapter::clear()
{
    m_a.clear();
    m_b.clear();
    m_trainedPairs.clear();
}

int LoraAdapter::rank() const
{
    return m_rank;
}

double LoraAdapter::alpha() const
{
    return m_alpha;
}

int LoraAdapter::pairCount() const
{
    return static_cast<int>(m_trainedPairs.size());
}

std::vector<double> LoraAdapter::initializedVector(const std::string &token, int salt) const
{
    std::vector<double> values(static_cast<size_t>(m_rank));
    std::hash<std::string> hash;
    for (int i = 0; i < m_rank; ++i) {
        const size_t h = hash(token + "#" + std::to_string(salt) + "#" + std::to_string(i));
        const double unit = static_cast<double>(h % 2001) / 1000.0 - 1.0;
        values[static_cast<size_t>(i)] = unit * 0.01;
    }
    return values;
}

std::vector<double> &LoraAdapter::ensureVector(std::map<std::string, std::vector<double>> &matrix,
                                               const std::string &token,
                                               int salt)
{
    auto it = matrix.find(token);
    if (it == matrix.end() || static_cast<int>(it->second.size()) != m_rank) {
        it = matrix.insert_or_assign(token, initializedVector(token, salt)).first;
    }
    return it->second;
}

double LoraAdapter::delta(const std::string &from, const std::string &to) const
{
    auto aIt = m_a.find(from);
    auto bIt = m_b.find(to);
    if (aIt == m_a.end() || bIt == m_b.end()) {
        return 0.0;
    }

    double dot = 0.0;
    for (int i = 0; i < m_rank; ++i) {
        dot += aIt->second[static_cast<size_t>(i)] * bIt->second[static_cast<size_t>(i)];
    }
    return (m_alpha / static_cast<double>(m_rank)) * dot;
}

void LoraAdapter::trainPair(const std::string &from,
                            const std::string &to,
                            double targetDelta,
                            double learningRate)
{
    if (from.empty() || to.empty()) {
        return;
    }

    std::vector<double> &a = ensureVector(m_a, from, 11);
    std::vector<double> &b = ensureVector(m_b, to, 29);
    const std::vector<double> oldA = a;
    const double current = delta(from, to);
    const double error = clipped(targetDelta - current, -4.0, 4.0);
    const double scale = learningRate * error * (m_alpha / static_cast<double>(m_rank));

    for (int i = 0; i < m_rank; ++i) {
        const size_t idx = static_cast<size_t>(i);
        a[idx] = clipped(a[idx] + scale * b[idx], -8.0, 8.0);
        b[idx] = clipped(b[idx] + scale * oldA[idx], -8.0, 8.0);
    }

    m_trainedPairs.insert({from, to});
}

void LoraAdapter::trainSequence(const std::vector<std::string> &tokens,
                                int epochs,
                                double learningRate,
                                double salience)
{
    if (tokens.size() < 2) {
        return;
    }

    epochs = std::max(1, std::min(epochs, 64));
    learningRate = clipped(learningRate, 0.001, 1.0);
    salience = clipped(salience, 0.1, 8.0);

    for (int epoch = 0; epoch < epochs; ++epoch) {
        for (size_t i = 0; i + 1 < tokens.size(); ++i) {
            trainPair(tokens[i], tokens[i + 1], salience, learningRate);
        }
    }
}

std::vector<std::pair<std::pair<std::string, std::string>, double>> LoraAdapter::materializedDeltas(double minimumDelta) const
{
    std::vector<std::pair<std::pair<std::string, std::string>, double>> deltas;
    for (const auto &pair : m_trainedPairs) {
        const double value = delta(pair.first, pair.second);
        if (value >= minimumDelta) {
            deltas.push_back({pair, value});
        }
    }
    return deltas;
}

bool LoraAdapter::save(const std::string &filePath) const
{
    std::ofstream out(filePath);
    if (!out.is_open()) {
        return false;
    }

    out << "AITRAINER_LORA_V1 " << m_rank << " " << m_alpha << "\n";
    for (const auto &item : m_a) {
        out << "A " << item.first;
        for (double value : item.second) {
            out << " " << value;
        }
        out << "\n";
    }
    for (const auto &item : m_b) {
        out << "B " << item.first;
        for (double value : item.second) {
            out << " " << value;
        }
        out << "\n";
    }
    for (const auto &pair : m_trainedPairs) {
        out << "P " << pair.first << " " << pair.second << "\n";
    }
    return true;
}

bool LoraAdapter::load(const std::string &filePath)
{
    std::ifstream in(filePath);
    if (!in.is_open()) {
        return true;
    }

    clear();

    std::string header;
    int rank = 4;
    double alpha = 8.0;
    in >> header >> rank >> alpha;
    if (header != "AITRAINER_LORA_V1") {
        clear();
        return false;
    }
    configure(rank, alpha);

    std::string kind;
    while (in >> kind) {
        if (kind == "A" || kind == "B") {
            std::string token;
            in >> token;
            std::vector<double> values(static_cast<size_t>(m_rank), 0.0);
            for (int i = 0; i < m_rank; ++i) {
                in >> values[static_cast<size_t>(i)];
            }
            if (kind == "A") {
                m_a[token] = values;
            } else {
                m_b[token] = values;
            }
        } else if (kind == "P") {
            std::string from;
            std::string to;
            in >> from >> to;
            if (!from.empty() && !to.empty()) {
                m_trainedPairs.insert({from, to});
            }
        } else {
            std::string rest;
            std::getline(in, rest);
        }
    }

    return true;
}
