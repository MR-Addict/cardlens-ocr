#pragma once
#include "cardlens_ocr/core.h"
#include "internal.h"
#include "ctc.h"
#include <mutex>
#include <net.h>
namespace cardlens_ocr {
struct Ocr::Impl {
    ncnn::Net det, rec;
    std::vector<std::string> dictionary;
    std::mutex mutex;
    explicit Impl(const std::string &directory);
    std::pair<std::string, float> read(const cv::Mat &roi, const std::string &field = "",
                                       float aspect = 1.f,
                                       std::vector<CtcCandidate> *candidates = nullptr);
    std::pair<std::string, float> refine_name(const cv::Mat &image, const nlohmann::json &field);
    std::vector<Line> run(const cv::Mat &bgr);
};
}
