#pragma once
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <string>
#include <utility>
#include <vector>
namespace cardlens_ocr {
struct Line {
    std::string text;
    float confidence;
    std::vector<cv::Point2f> polygon;
};
bool field_valid(const std::string &key, const std::string &value);
std::string clean_field(const std::string &key, std::string value);
bool dates_valid(const std::string &text);
struct NameReading {
    std::string value;
    float confidence;
    int family;
};
std::pair<std::string, float> select_name_refinement(const std::string &original, float confidence,
                                                     const std::vector<NameReading> &readings);
nlohmann::json parse_document(const std::vector<Line> &lines, const std::string &type,
                              const std::string &side, int width, int height,
                              const nlohmann::json &overrides = nlohmann::json::object());
} // namespace cardlens_ocr
