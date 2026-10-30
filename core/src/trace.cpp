#include "trace.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <opencv2/imgcodecs.hpp>
#include <stdexcept>
namespace cardlens_ocr {
namespace {
struct Sink {
    std::filesystem::path directory;
    std::ofstream stream;
    size_t sequence = 0;
};
thread_local std::unique_ptr<Sink> sink;
} // namespace
TraceSession::TraceSession(const std::string &directory) {
    if (sink)
        throw std::runtime_error("Diagnostic session already active");
    auto output = std::make_unique<Sink>();
    output->directory = directory;
    std::filesystem::create_directories(output->directory);
    output->stream.open(output->directory / "events.jsonl");
    if (!output->stream)
        throw std::runtime_error("Cannot open diagnostic directory");
    sink = std::move(output);
}
TraceSession::~TraceSession() { sink.reset(); }
bool trace_enabled() { return bool(sink); }
void trace_event(nlohmann::json value) {
    if (!sink)
        return;
    value["sequence"] = sink->sequence++;
    sink->stream << value.dump() << '\n';
    if (!sink->stream)
        throw std::runtime_error("Cannot write diagnostic event");
}
void trace_image(const cv::Mat &image, nlohmann::json value) {
    if (!sink)
        return;
    std::string name = "crop-" + std::to_string(sink->sequence) + ".png";
    if (!cv::imwrite((sink->directory / name).string(), image))
        throw std::runtime_error("Cannot write diagnostic crop");
    value["image"] = name;
    trace_event(std::move(value));
}
} // namespace cardlens_ocr
