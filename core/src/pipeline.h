#pragma once
#include "internal.h"
#include <functional>
namespace cardlens_ocr {
struct RefinementReader {
    std::function<std::pair<std::string, float>(const cv::Mat &, const std::string &)> read;
    std::function<std::vector<Line>(const cv::Mat &)> run;
    std::function<std::pair<std::string, float>(const cv::Mat &, const nlohmann::json &)> refine_name;
};
nlohmann::json refine_fields(nlohmann::json result, const cv::Mat &image,
                            const std::vector<Line> &lines, const std::string &type,
                            const std::string &side, const RefinementReader &reader);
struct ImageCandidate { cv::Mat image; cv::Mat toSource; };
std::vector<ImageCandidate> locate_documents(const cv::Mat &bgr, int &count);
void restore_coordinates(nlohmann::json &result, int rotation, cv::Size selected,
                         const cv::Mat &mapping, int width, int height);
void assess_image(nlohmann::json &result, const cv::Mat &bgr);
nlohmann::json failed_document(const std::string &type, const std::string &side,
                               int width, int height, const std::string &warning);
double candidate_score(const nlohmann::json &result, const std::vector<Line> &lines);
bool confident_candidate(const nlohmann::json &result, const std::vector<Line> &lines);
void update_status(nlohmann::json &result);
bool needs_review(const std::string &warning);
}
