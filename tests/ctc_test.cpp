#include "ctc.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
using namespace cardlens_ocr;
int failures = 0;
void check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
        ++failures;
    }
}
const std::vector<std::string> dict = {"",   "C", "1", "I", "工", "男",
                                       "女", "2", "X", "Y", "Z",  "A"};
std::vector<CtcCandidate> decode(std::vector<std::vector<float>> rows, const std::string &field) {
    std::vector<float> input;
    for (const auto &row : rows)
        input.insert(input.end(), row.begin(), row.end());
    return short_field_candidates(input.data(), rows.size(), dict.size(), dict.size(), dict, field);
}
std::vector<float> row(std::initializer_list<std::pair<int, float>> values) {
    std::vector<float> r(dict.size(), 0);
    for (auto [i, v] : values)
        r[i] = v;
    return r;
}
int main() {
    auto readings = decode({row({{1, .99f}, {0, .01f}}), row({{3, .55f}, {2, .4f}, {0, .05f}})},
                           "vehicleClass");
    auto it =
        std::find_if(readings.begin(), readings.end(), [](auto &r) { return r.value == "C1"; });
    check(it != readings.end(), "CTC lost a supported non-greedy C1 candidate");
    if (it != readings.end())
        check(std::abs(it->confidence - .695f) < 1e-5f, "Field probability was renormalized");
    auto lowerDict = dict;
    lowerDict[1] = "c";
    auto lowerA = row({{1, .99f}, {0, .01f}}), lowerB = row({{2, .8f}, {3, .2f}});
    std::vector<float> lowerInput = lowerA;
    lowerInput.insert(lowerInput.end(), lowerB.begin(), lowerB.end());
    auto lower = short_field_candidates(lowerInput.data(), 2, dict.size(), dict.size(), lowerDict,
                                        "vehicleClass");
    check(!lower.empty() && lower[0].value == "C1" && lower[0].confidence > .8f,
          "ASCII case normalization lost character confidence");
    auto fifth =
        decode({row({{0, .4f}, {3, .2f}, {8, .15f}, {9, .1f}, {10, .08f}, {5, .07f}})}, "sex");
    check(!fifth.empty() && fifth[0].value == "男", "Blank displaced the fifth nonblank character");
    auto weak =
        decode({row({{1, .9f}, {0, .1f}}), row({{3, .85f}, {2, .1f}, {0, .05f}})}, "vehicleClass");
    std::vector<ShortEvidence> evidence;
    for (auto &r : weak) {
        evidence.push_back({r, 0});
        evidence.push_back({r, 1});
    }
    check(select_short_candidate(evidence).value.empty(), "Low probability alternative accepted");
    auto blank = decode({row({{0, .999f}, {5, .001f}}), row({{0, .999f}, {5, .001f}})}, "sex");
    evidence.clear();
    for (auto &r : blank) {
        evidence.push_back({r, 0});
        evidence.push_back({r, 1});
    }
    check(select_short_candidate(evidence).value.empty(), "Blank crop hallucinated a field");
    auto repeated = decode({row({{5, 1}}), row({{5, 1}})}, "sex");
    check(repeated.size() == 1 && repeated[0].rawText == "男",
          "Adjacent repeats were not collapsed");
    auto separated = decode({row({{5, 1}}), row({{0, 1}}), row({{5, 1}})}, "sex");
    check(separated.empty(),
          "Blank-separated repeated characters collapsed or normalized into sex");
    auto absent = decode(
        {row({{1, 1}}), row({{3, .3f}, {8, .2f}, {9, .18f}, {10, .16f}, {11, .12f}, {2, .04f}})},
        "vehicleClass");
    check(std::none_of(absent.begin(), absent.end(), [](auto &r) { return r.value == "C1"; }),
          "A character outside top five was invented");
    CtcCandidate c{"cI", "C1", .7f, -1};
    check(select_short_candidate({{c, 0}, {c, 0}}).value.empty(), "Same geometry counted twice");
    check(select_short_candidate({{c, 0}, {c, 1}}).value == "C1", "Geometric consensus rejected");
    check(select_short_candidate({{c, 0}, {c, 1}, {{"C2", "C2", .7f, -1}, 2}}).value.empty(),
          "Competing candidate ignored");
    check(short_field_value("vehicleClass", "C工") == "C工",
          "Hard-coded glyph substitution introduced");
    check(short_field_value("sex", "姓名男").empty(), "Caption contamination accepted");
    check(short_field_candidates(nullptr, 2, dict.size(), dict.size(), dict, "sex").empty(),
          "Null input accepted");
    return failures ? 1 : 0;
}
