#pragma once
#include <string>
#include <vector>
namespace cardlens_ocr {
struct CtcCandidate {
    std::string rawText;
    std::string value;
    float confidence = 0;
    double logProbability = 0;
};
// Scores always refer to the original, unmasked posterior distribution.
std::vector<CtcCandidate> short_field_candidates(const float *probabilities, int steps, int classes,
                                                 int rowStride,
                                                 const std::vector<std::string> &dictionary,
                                                 const std::string &field);
struct ShortEvidence {
    CtcCandidate reading;
    int geometry;
};
CtcCandidate select_short_candidate(const std::vector<ShortEvidence> &evidence);
std::string short_field_value(const std::string &field, std::string text);
} // namespace cardlens_ocr
