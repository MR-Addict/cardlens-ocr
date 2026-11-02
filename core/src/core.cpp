#include "cardlens_ocr/core.h"
#include "engine.h"
#include "pipeline.h"
#include "trace.h"
#include <algorithm>
#include <mutex>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
namespace cardlens_ocr {
Ocr::Ocr() = default;
Ocr::~Ocr() = default;
void Ocr::initialize(const std::string &directory) {
    if (impl_) throw std::runtime_error("ALREADY_INITIALIZED");
    impl_.reset(new Impl(directory));
}
void Ocr::release() { impl_.reset(); }
std::string Ocr::recognize(const unsigned char *pixels, int width, int height,
                              std::size_t stride, const std::string &type,
                              const std::string &side) {
    if (!impl_) throw std::runtime_error("NOT_INITIALIZED");
    if (!pixels || width < 64 || height < 64 || width > 8192 || height > 8192 ||
        stride < (std::size_t)width * 4 || (std::size_t)width * height > 24000000)
        throw std::invalid_argument("INVALID_IMAGE");
    if (type != "id_card" && type != "driver_license" && type != "vehicle_license")
        throw std::invalid_argument("INVALID_DOCUMENT_OPTIONS");
    if ((type == "id_card" && side != "portrait" && side != "emblem") ||
        (type != "id_card" && side != "main"))
        throw std::invalid_argument("INVALID_DOCUMENT_OPTIONS");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    cv::Mat rgba(height, width, CV_8UC4, (void *)pixels, stride), bgr;
    cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
    int cardCount = 0;
    auto images = locate_documents(bgr, cardCount);
    if (cardCount > 1)
        return failed_document(type, side, width, height, "MULTIPLE_DOCUMENTS").dump();
    nlohmann::json result;
    double best = -1;
    int rotation = 0;
    cv::Mat mapping;
    cv::Size selectedSize;
    cv::Mat selectedImage;
    std::vector<Line> selectedLines;
    bool finished = false;
    for (auto &candidate : images) {
        for (int angle : {0, 90, 180, 270}) {
            cv::Mat oriented;
            if (angle == 0)
                oriented = candidate.image;
            else
                cv::rotate(candidate.image, oriented,
                           angle == 90    ? cv::ROTATE_90_CLOCKWISE
                           : angle == 180 ? cv::ROTATE_180
                                          : cv::ROTATE_90_COUNTERCLOCKWISE);
            auto lines = impl_->run(oriented);
            auto parsed = parse_document(lines, type, side, oriented.cols, oriented.rows);
            double score = candidate_score(parsed, lines);
            if (score > best || result.is_null()) {
                best = score;
                result = std::move(parsed);
                rotation = angle;
                mapping = candidate.toSource;
                selectedSize = candidate.image.size();
                selectedImage = oriented;
                selectedLines = lines;
            }
            if (trace_enabled())
                trace_event({{"stage", "orientation"},
                             {"angle", angle},
                             {"score", score},
                             {"bestScore", best},
                             {"sourceSize", {oriented.cols, oriented.rows}}});
            const auto &warnings = result["warnings"];
            const bool multiple = std::find(warnings.begin(), warnings.end(), "MULTIPLE_DOCUMENTS") != warnings.end();
            if (!exhaustive_search() && (multiple || confident_candidate(result, selectedLines))) {
                finished = true;
                break;
            }
        }
        if (finished) break;
    }
    if (trace_enabled())
        trace_event({{"stage", "selected_fields"}, {"rotation", rotation}, {"result", result}});
    result = refine_fields(std::move(result), selectedImage, selectedLines, type, side, {
        [this](const cv::Mat &roi, const std::string &key) { return impl_->read(roi, key); },
        [this](const cv::Mat &roi) { return impl_->run(roi); },
        [this](const cv::Mat &image, const nlohmann::json &field) { return impl_->refine_name(image, field); }
    });
    restore_coordinates(result, rotation, selectedSize, mapping, width, height);
    assess_image(result, bgr);
    update_status(result);
    if (trace_enabled())
        trace_event({{"stage", "result"}, {"result", result}});
    return result.dump();
}
} // namespace cardlens_ocr
