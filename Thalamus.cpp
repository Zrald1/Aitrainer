#include "Thalamus.h"
#include <sstream>
#include <cctype>

std::vector<std::string> Thalamus::route(const std::string& input) const {
    std::vector<std::string> tokens;
    std::stringstream ss(input);
    std::string word;
    while (ss >> word) {
        // strip punctuation
        std::string clean;
        for (char c : word) {
            if (std::isalnum((unsigned char)c)) {
                clean += std::tolower((unsigned char)c);
            }
        }
        if (!clean.empty()) {
            tokens.push_back(clean);
        }
    }
    return tokens;
}
