#include "pipeline.h"
#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>
namespace cardlens_ocr {
using nlohmann::json;
bool needs_review(const std::string &warning) {
    const auto colon = warning.find(':');
    return warning.substr(colon == std::string::npos ? 0 : colon + 1) != "CAPTION_REMOVED";
}
void update_status(json &result) {
    auto &warnings = result["warnings"];
    json unique = json::array();
    for (const auto &warning : warnings)
        if (std::find(unique.begin(), unique.end(), warning) == unique.end()) unique.push_back(warning);
    warnings = std::move(unique);
    bool review = false, blocked = false;
    for (const auto &warning : warnings) {
        const auto code = warning.get<std::string>();
        review |= needs_review(code);
        blocked |= code == "MULTIPLE_DOCUMENTS" || code == "UNSUPPORTED_PAGE" || code == "DOCUMENT_TYPE_MISMATCH";
    }
    result["status"] = blocked || result["fields"].empty() ? "failed" : review ? "partial" : "complete";
}
json failed_document(const std::string &type, const std::string &side, int width, int height,
                     const std::string &warning) {
    return {{"documentType", type}, {"side", side}, {"status", "failed"},
            {"width", width}, {"height", height}, {"rotation", 0},
            {"fields", json::object()}, {"lines", json::array()}, {"warnings", {warning}}};
}
double candidate_score(const json &parsed, const std::vector<Line> &lines) {
    double score = 0;
    for (auto it = parsed["fields"].begin(); it != parsed["fields"].end(); ++it)
        score += field_valid(it.key(), it.value()["value"]) ? 10. : 2.;
    if (parsed["status"] == "failed")
        score -= 100.;
    for (auto &line : lines) {
        auto bounds = cv::boundingRect(line.polygon);
        score += line.confidence * .01;
        if (bounds.height > bounds.width * 1.5)
            score -= 5.;
    }
    bool confirmed = true;
    for (auto &warning : parsed["warnings"]) {
        auto message = warning.get<std::string>();
        if (message == "DOCUMENT_TYPE_MISMATCH") {
            score = 1000.;
            confirmed = false;
        }
        if (message == "DOCUMENT_TYPE_UNCONFIRMED") {
            score -= 20.;
            confirmed = false;
        }
        if (message == "MULTIPLE_DOCUMENTS")
            score = 1000000.;
        if (message == "UNSUPPORTED_PAGE")
            score = 900000.;
    }
    if (confirmed)
        score += 10000.;
    for (auto &field : parsed["fields"])
        score += field["confidence"].get<double>();
    return score;
}
bool confident_candidate(const json &result, const std::vector<Line> &lines) {
    if (result["status"] != "complete" || lines.empty()) return false;
    for (const auto &field : result["fields"])
        if (field["confidence"].get<float>() < .97f) return false;
    for (const auto &line : lines) {
        const auto bounds = cv::boundingRect(line.polygon);
        if (bounds.height > bounds.width * 1.5 || line.confidence < .90f) return false;
    }
    return true;
}
void assess_image(json &result, const cv::Mat &bgr) {
    bool edge = false;
    for (const auto &line : result["lines"])
        for (const auto &point : line["polygon"]) {
            float x = point[0], y = point[1];
            if (x < 2 || y < 2 || x > bgr.cols - 3 || y > bgr.rows - 3) edge = true;
        }
    if (edge) result["warnings"].push_back("BORDER_INCOMPLETE");
    cv::Mat gray, lap;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::Laplacian(gray, lap, CV_64F);
    cv::Scalar mean, deviation;
    cv::meanStdDev(lap, mean, deviation);
    if (deviation[0] * deviation[0] < 30) result["warnings"].push_back("LOW_SHARPNESS");
}
}
