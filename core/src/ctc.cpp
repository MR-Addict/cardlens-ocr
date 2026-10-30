#include "ctc.h"
#include "internal.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
namespace cardlens_ocr {
namespace {
constexpr double negative = -std::numeric_limits<double>::infinity();
double add(double a, double b) {
    if (a == negative)
        return b;
    if (b == negative)
        return a;
    return std::max(a, b) + std::log1p(std::exp(-std::abs(a - b)));
}
double logarithm(float p) { return p > 0 && p <= 1 && std::isfinite(p) ? std::log(p) : negative; }
struct Prefix {
    double blank = negative, text = negative;
};
// Recover a best CTC alignment to score emitted characters with their original
// probabilities. Blank frames never inflate the field's character confidence.
float confidence(const float *p, int steps, int stride, const std::vector<int> &tokens,
                 const std::vector<std::string> &dict, const std::string &value) {
    int states = (int)tokens.size() * 2 + 1;
    std::vector<double> previous(states, negative), next(states, negative);
    std::vector<std::vector<int>> back(steps, std::vector<int>(states, -1));
    previous[0] = 0;
    for (int t = 0; t < steps; ++t) {
        std::fill(next.begin(), next.end(), negative);
        for (int s = 0; s < states; ++s) {
            int from = s;
            if (s > 0 && previous[s - 1] > previous[from])
                from = s - 1;
            if (s > 1 && s % 2 && tokens[s / 2] != tokens[s / 2 - 1] &&
                previous[s - 2] > previous[from])
                from = s - 2;
            next[s] = previous[from] + logarithm(p[t * stride + (s % 2 ? tokens[s / 2] : 0)]);
            back[t][s] = from;
        }
        previous.swap(next);
    }
    int state = states - 1;
    if (states > 1 && previous[states - 2] > previous[state])
        state = states - 2;
    if (!std::isfinite(previous[state]))
        return 0;
    std::vector<float> peaks(tokens.size(), 0);
    for (int t = steps - 1; t >= 0; --t) {
        if (state % 2)
            peaks[state / 2] = std::max(peaks[state / 2], p[t * stride + tokens[state / 2]]);
        state = back[t][state];
    }
    std::string raw;
    for (int token : tokens)
        raw += dict[token];
    auto folded = raw, target = value;
    for (auto &c : folded)
        if (c >= 'a' && c <= 'z')
            c -= ('a' - 'A');
    for (auto &c : target)
        if (c >= 'a' && c <= 'z')
            c -= ('a' - 'A');
    auto start = folded.find(target);
    if (start == std::string::npos)
        return 0;
    size_t offset = 0;
    float sum = 0;
    int count = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        size_t end = offset + dict[tokens[i]].size();
        if (offset >= start && end <= start + value.size()) {
            sum += peaks[i];
            ++count;
        }
        offset = end;
    }
    return count ? sum / count : 0;
}
} // namespace
std::string short_field_value(const std::string &field, std::string text) {
    const std::string label = field == "sex" ? "性别" : "准驾车型";
    auto pos = text.find(label);
    if (pos != std::string::npos)
        text = text.substr(pos + label.size());
    if (field == "sex" && text != "男" && text != "女")
        return "";
    return clean_field(field, text);
}
std::vector<CtcCandidate> short_field_candidates(const float *p, int steps, int classes, int stride,
                                                 const std::vector<std::string> &dict,
                                                 const std::string &field) {
    if (!p || steps <= 0 || classes < 2 || stride < classes || (int)dict.size() != classes ||
        (field != "sex" && field != "vehicleClass"))
        return {};
    std::map<std::vector<int>, Prefix> beam;
    beam[{}].blank = 0;
    for (int t = 0; t < steps; ++t) {
        const float *row = p + t * stride;
        std::vector<int> order(classes - 1);
        std::iota(order.begin(), order.end(), 1);
        int count = std::min(5, classes - 1);
        std::partial_sort(order.begin(), order.begin() + count, order.end(),
                          [&](int a, int b) { return row[a] == row[b] ? a < b : row[a] > row[b]; });
        order.resize(count);
        std::map<std::vector<int>, Prefix> expanded;
        for (const auto &[prefix, score] : beam) {
            double total = add(score.blank, score.text);
            auto &same = expanded[prefix];
            same.blank = add(same.blank, total + logarithm(row[0]));
            for (int token : order) {
                if (!token || !std::isfinite(logarithm(row[token])))
                    continue;
                bool repeated = !prefix.empty() && token == prefix.back();
                if (repeated)
                    same.text = add(same.text, score.text + logarithm(row[token]));
                if (prefix.size() >= 32)
                    continue;
                auto extension = prefix;
                extension.push_back(token);
                auto &dest = expanded[extension];
                dest.text =
                    add(dest.text, (repeated ? score.blank : total) + logarithm(row[token]));
            }
        }
        std::vector<std::pair<std::vector<int>, Prefix>> ranked(expanded.begin(), expanded.end());
        std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
            double x = add(a.second.blank, a.second.text), y = add(b.second.blank, b.second.text);
            return x == y ? a.first < b.first : x > y;
        });
        beam.clear();
        for (size_t i = 0; i < std::min<size_t>(8, ranked.size()); ++i)
            beam.insert(ranked[i]);
    }
    std::map<std::string, CtcCandidate> candidates;
    for (const auto &[prefix, score] : beam) {
        std::string raw;
        for (int token : prefix)
            raw += dict[token];
        auto value = short_field_value(field, raw);
        double likelihood = add(score.blank, score.text);
        if (!std::isfinite(likelihood) || !field_valid(field, value))
            continue;
        CtcCandidate reading{raw, value, confidence(p, steps, stride, prefix, dict, value),
                             likelihood};
        auto it = candidates.find(value);
        if (it == candidates.end() || likelihood > it->second.logProbability)
            candidates[value] = reading;
    }
    std::vector<CtcCandidate> result;
    for (auto &[value, reading] : candidates)
        result.push_back(reading);
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.logProbability == b.logProbability ? a.value < b.value
                                                    : a.logProbability > b.logProbability;
    });
    return result;
}
CtcCandidate select_short_candidate(const std::vector<ShortEvidence> &evidence) {
    std::map<std::string, std::map<int, CtcCandidate>> votes;
    for (const auto &e : evidence) {
        if (e.reading.confidence < .6f || e.reading.value.empty())
            continue;
        auto &v = votes[e.reading.value][e.geometry];
        if (v.value.empty() || e.reading.confidence > v.confidence)
            v = e.reading;
    }
    if (votes.size() != 1 || votes.begin()->second.size() < 2)
        return {};
    CtcCandidate result;
    float sum = 0;
    for (const auto &[geometry, reading] : votes.begin()->second) {
        sum += reading.confidence;
        if (result.value.empty() || reading.confidence > result.confidence)
            result = reading;
    }
    result.confidence = sum / votes.begin()->second.size();
    return result;
}
} // namespace cardlens_ocr
