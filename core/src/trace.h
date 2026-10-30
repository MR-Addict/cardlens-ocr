#pragma once
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <string>
namespace cardlens_ocr {
#ifdef CARDLENS_OCR_DIAGNOSTICS
class TraceSession {
  public:
    explicit TraceSession(const std::string &directory);
    ~TraceSession();
    TraceSession(const TraceSession &) = delete;
    TraceSession &operator=(const TraceSession &) = delete;
};
bool trace_enabled();
void trace_event(nlohmann::json value);
void trace_image(const cv::Mat &image, nlohmann::json value);
#else
inline bool trace_enabled() { return false; }
inline void trace_event(const nlohmann::json &) {}
inline void trace_image(const cv::Mat &, const nlohmann::json &) {}
#endif
} // namespace cardlens_ocr
