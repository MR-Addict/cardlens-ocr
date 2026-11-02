#include "cardlens_ocr/core.h"
#include "ctc.h"
#include "internal.h"
#include "trace.h"
#include <algorithm>
#include <clipper2/clipper.h>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <net.h>
#include <numeric>
#include <opencv2/imgproc.hpp>
#include <regex>
#include <set>
#include <stdexcept>
#include "engine.h"
namespace cardlens_ocr {
namespace {
std::vector<cv::Point2f> ordered_points(std::vector<cv::Point2f> p) {
    std::sort(p.begin(), p.end(), [](auto a, auto b) { return a.x < b.x; });
    if (p[0].y > p[1].y)
        std::swap(p[0], p[1]);
    if (p[2].y > p[3].y)
        std::swap(p[2], p[3]);
    return {p[0], p[2], p[3], p[1]};
}
std::vector<cv::Point2f> ordered(cv::RotatedRect rect) {
    cv::Point2f points[4];
    rect.points(points);
    return ordered_points({points, points + 4});
}
cv::Mat crop(const cv::Mat &image, const std::vector<cv::Point2f> &p) {
    int w = std::max(2, (int)std::round(std::max(cv::norm(p[1] - p[0]), cv::norm(p[2] - p[3]))));
    int h = std::max(2, (int)std::round(std::max(cv::norm(p[3] - p[0]), cv::norm(p[2] - p[1]))));
    if (w > 8192 || h > 8192)
        throw std::runtime_error("Invalid detected text size");
    std::vector<cv::Point2f> dest = {
        {0, 0}, {(float)w - 1, 0}, {(float)w - 1, (float)h - 1}, {0, (float)h - 1}};
    cv::Mat output;
    cv::warpPerspective(image, output, cv::getPerspectiveTransform(p, dest), cv::Size(w, h),
                        cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    if (h > w * 1.5)
        cv::rotate(output, output, cv::ROTATE_90_CLOCKWISE);
    return output;
}
} // namespace
std::pair<std::string, float> select_name_refinement(const std::string &original, float confidence,
                                                     const std::vector<NameReading> &readings) {
    std::map<std::string, std::map<int, float>> votes;
    for (const auto &reading : readings)
        if (reading.confidence >= .94f && reading.value.size() == original.size() &&
            field_valid("name", reading.value))
            votes[reading.value][reading.family] =
                std::max(votes[reading.value][reading.family], reading.confidence);
    std::pair<std::string, float> selected{"", 0};
    int eligible = 0;
    for (const auto &[value, families] : votes) {
        if (families.size() < 3)
            continue;
        float score = 0;
        for (const auto &[family, best] : families)
            score += best;
        score /= families.size();
        ++eligible;
        if (value != original && score >= confidence + .03f)
            selected = {value, score};
    }
    return eligible == 1 ? selected : std::pair<std::string, float>{"", 0};
}
Ocr::Impl::Impl(const std::string &directory) {
    for (auto *net : {&det, &rec}) {
        #ifdef __EMSCRIPTEN__
        net->opt.num_threads = 1;
#else
        net->opt.num_threads = 2;
#endif
        net->opt.use_vulkan_compute = false;
        net->opt.use_fp16_packed = false;
        net->opt.use_fp16_storage = false;
        net->opt.use_fp16_arithmetic = false;
    }
    if (det.load_param((directory + "/det.param").c_str()) ||
        det.load_model((directory + "/det.bin").c_str()) ||
        rec.load_param((directory + "/rec.param").c_str()) ||
        rec.load_model((directory + "/rec.bin").c_str()))
        throw std::runtime_error("MODEL_LOAD_FAILED");
    dictionary.push_back(""); // CTC blank
    std::ifstream file(directory + "/dictionary.txt");
    std::string s;
    if (!file)
        throw std::runtime_error("DICTIONARY_MISSING");
    while (std::getline(file, s)) {
        if (!s.empty() && s.back() == '\r')
            s.pop_back();
        dictionary.push_back(s);
    }
    dictionary.push_back(" ");
    if (dictionary.size() < 100)
        throw std::runtime_error("DICTIONARY_INVALID");
}
std::pair<std::string, float> Ocr::Impl::read(const cv::Mat &roi, const std::string &field,
                                   float aspect,
                                   std::vector<CtcCandidate> *candidates){
    int rw = std::clamp((int)std::ceil(48.f * roi.cols / roi.rows * aspect), 8, 2048);
    cv::Mat resized;
    cv::resize(roi, resized, cv::Size(rw, 48), 0, 0, cv::INTER_CUBIC);
    ncnn::Mat rin = ncnn::Mat::from_pixels(resized.data, ncnn::Mat::PIXEL_BGR, rw, 48);
    const float rm[] = {127.5f, 127.5f, 127.5f},
                rn[] = {1.f / 127.5f, 1.f / 127.5f, 1.f / 127.5f};
    rin.substract_mean_normalize(rm, rn);
    auto rex = rec.create_extractor();
    ncnn::Mat rout;
    if (rex.input("in0", rin) || rex.extract("out0", rout))
        throw std::runtime_error("RECOGNITION_FAILED");
    if (rout.w != (int)dictionary.size())
        throw std::runtime_error("DICTIONARY_SHAPE_MISMATCH");
    std::string text;
    float score = 0;
    struct Token {
        size_t begin, end;
        float score;
    };
    std::vector<Token> tokens;
    int count = 0, last = 0;
    for (int t = 0; t < rout.h; t++) {
        const float *row = rout.row(t);
        int token = (int)(std::max_element(row, row + rout.w) - row);
        if (token > 0 && token != last) {
            auto begin = text.size();
            text += dictionary[token];
            tokens.push_back({begin, text.size(), row[token]});
            score += row[token];
            ++count;
        }
        last = token;
    }
    if (!field.empty()) {
        auto value = clean_field(field, text);
        auto begin = text.find(value);
        if (!value.empty() && begin != std::string::npos) {
            score = 0;
            count = 0;
            for (auto &token : tokens)
                if (token.begin >= begin && token.end <= begin + value.size()) {
                    score += token.score;
                    ++count;
                }
        }
    }
    float confidence = count ? score / count : 0;
    if (trace_enabled()) {
        nlohmann::json frames = nlohmann::json::array();
        for (int t = 0; t < rout.h; ++t) {
            const float *row = rout.row(t);
            std::vector<int> order(rout.w - 1);
            std::iota(order.begin(), order.end(), 1);
            std::partial_sort(order.begin(), order.begin() + 5, order.end(),
                              [&](int a, int b) { return row[a] > row[b]; });
            nlohmann::json top = nlohmann::json::array();
            for (int i = 0; i < 5; ++i)
                top.push_back(
                    {{"token", dictionary[order[i]]}, {"probability", row[order[i]]}});
            frames.push_back({{"blank", row[0]}, {"top", top}});
        }
        nlohmann::json alternatives = nlohmann::json::array();
        if (candidates)
            for (const auto &c : *candidates)
                alternatives.push_back({{"rawText", c.rawText},
                                        {"value", c.value},
                                        {"confidence", c.confidence},
                                        {"logProbability", c.logProbability}});
        trace_image(roi, {{"stage", "recognition"},
                          {"field", field},
                          {"text", text},
                          {"confidence", confidence},
                          {"frames", frames},
                          {"candidates", alternatives}});
    }
    return {text, confidence};
}
std::pair<std::string, float> Ocr::Impl::refine_name(const cv::Mat &image, const nlohmann::json &field){
    std::vector<cv::Point2f> polygon;
    for (const auto &p : field["polygon"])
        polygon.emplace_back(p[0], p[1]);
    std::vector<NameReading> readings;
    // Preserve the text's perspective. Rectangular padding alone changes both
    // its slant and glyph proportions, and can repeat the same wrong reading.
    for (float margin : {.1f, .25f}) {
        auto p = polygon;
        auto dy = polygon[3] - polygon[0];
        p[0] -= dy * margin;
        p[1] -= dy * margin;
        p[2] += dy * margin;
        p[3] += dy * margin;
        auto original = crop(image, p);
        for (int family = 0; family < 4; ++family) {
            cv::Mat roi;
            if (!family)
                roi = original;
            else {
                cv::Mat gray;
                cv::cvtColor(original, gray, cv::COLOR_BGR2GRAY);
                if (family == 2)
                    cv::GaussianBlur(gray, gray, cv::Size(3, 3), .7);
                if (family == 3)
                    cv::medianBlur(gray, gray, 3);
                cv::cvtColor(gray, roi, cv::COLOR_GRAY2BGR);
            }
            for (float aspect : {.8f, 1.f, 1.2f}) {
                auto reading = read(roi, "name", aspect);
                // Use value-only readings so a crisp label or English caption
                // cannot inflate the score of an uncertain name.
                if (reading.first == clean_field("name", reading.first))
                    readings.push_back({reading.first, reading.second, family});
            }
        }
    }
    return select_name_refinement(field["value"], field["confidence"], readings);
}
std::vector<Line> Ocr::Impl::run(const cv::Mat &bgr){
    const float scale = std::min(1.f, 960.f / std::max(bgr.cols, bgr.rows));
    int w = std::max(32, (int)std::round(bgr.cols * scale));
    int h = std::max(32, (int)std::round(bgr.rows * scale));
    ncnn::Mat input =
        ncnn::Mat::from_pixels_resize(bgr.data, ncnn::Mat::PIXEL_BGR, bgr.cols, bgr.rows, w, h);
    int pw = (w + 31) / 32 * 32, ph = (h + 31) / 32 * 32;
    ncnn::Mat padded;
    ncnn::copy_make_border(input, padded, 0, ph - h, 0, pw - w, ncnn::BORDER_CONSTANT, 0.f);
    const float mean[] = {123.675f, 116.28f, 103.53f},
                norm[] = {1.f / 58.395f, 1.f / 57.12f, 1.f / 57.375f};
    padded.substract_mean_normalize(mean, norm);
    ncnn::Extractor ex = det.create_extractor();
    ncnn::Mat output;
    if (ex.input("in0", padded) || ex.extract("out0", output))
        throw std::runtime_error("DETECTION_FAILED");
    if (output.w != pw || output.h != ph)
        throw std::runtime_error("DETECTION_SHAPE_MISMATCH");
    cv::Mat probabilities(ph, pw, CV_32F, (void *)output.channel(0));
    cv::Mat bitmap;
    cv::threshold(probabilities, bitmap, 0.3, 255, cv::THRESH_BINARY);
    bitmap.convertTo(bitmap, CV_8U);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bitmap, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    std::vector<Line> lines;
    for (const auto &contour : contours) {
        auto rejected = [&](const char *reason) {
            if (trace_enabled()) {
                auto b = cv::boundingRect(contour);
                trace_event({{"stage", "detection_filter"},
                             {"reason", reason},
                             {"bounds", {b.x, b.y, b.width, b.height}},
                             {"detectorSize", {pw, ph}},
                             {"sourceSize", {bgr.cols, bgr.rows}}});
            }
        };
        if (contour.size() < 3 || std::abs(cv::contourArea(contour)) < 4) {
            rejected("small_contour");
            continue;
        }
        cv::Rect bounds = cv::boundingRect(contour) & cv::Rect(0, 0, pw, ph);
        cv::Mat mask(bounds.size(), CV_8U, cv::Scalar(0));
        std::vector<cv::Point> local;
        for (auto p : contour)
            local.push_back(p - bounds.tl());
        cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{local}, cv::Scalar(255));
        if (cv::mean(probabilities(bounds), mask)[0] < 0.5) {
            rejected("low_box_probability");
            continue;
        }
        auto rect = cv::minAreaRect(contour);
        if (std::min(rect.size.width, rect.size.height) < 3) {
            rejected("small_box");
            continue;
        }
        double perimeter = cv::arcLength(contour, true);
        if (perimeter <= 0)
            continue;
        Clipper2Lib::PathD path;
        for (auto p : contour)
            path.emplace_back(p.x, p.y);
        auto expanded = Clipper2Lib::InflatePaths(
            Clipper2Lib::PathsD{path}, std::abs(cv::contourArea(contour)) * 2.0 / perimeter,
            Clipper2Lib::JoinType::Round, Clipper2Lib::EndType::Polygon);
        if (expanded.size() != 1 || expanded[0].size() < 3)
            continue;
        std::vector<cv::Point2f> points;
        for (auto p : expanded[0])
            points.emplace_back(p.x, p.y);
        auto polygon = ordered(cv::minAreaRect(points));
        for (auto &p : polygon) {
            p.x = std::clamp(p.x, 0.f, (float)w - 1) * bgr.cols / w;
            p.y = std::clamp(p.y, 0.f, (float)h - 1) * bgr.rows / h;
        }
        cv::Mat roi = crop(bgr, polygon);
        auto reading = read(roi);
        if (trace_enabled()) {
            nlohmann::json points = nlohmann::json::array();
            for (auto p : polygon)
                points.push_back({p.x, p.y});
            trace_event({{"stage", "detected_text_initial"},
                         {"polygon", points},
                         {"text", reading.first},
                         {"confidence", reading.second},
                         {"retained", !reading.first.empty() && reading.second >= .45f}});
        }
        if (!dates_valid(reading.first)) {
            static const std::regex datePattern(
                "[12][0-9]{3}(?:-|/|\\.|年)[0-9]{1,2}(?:-|/|\\.|月)[0-9]{1,2}");
            auto countDates = [&](const std::string &text) {
                return std::distance(
                    std::sregex_iterator(text.begin(), text.end(), datePattern),
                    std::sregex_iterator());
            };
            auto originalDates = countDates(reading.first);
            auto acceptable = [&](const std::pair<std::string, float> &candidate) {
                return candidate.second >= .6f && dates_valid(candidate.first) &&
                       countDates(candidate.first) >= originalDates;
            };
            // Retry with extra context: tight text boxes can clip digit strokes.
            for (float margin : {.1f, .25f, .4f}) {
                if (dates_valid(reading.first))
                    break;
                auto wider = polygon;
                auto dx = polygon[1] - polygon[0], dy = polygon[3] - polygon[0];
                float pad = cv::norm(dy) * margin;
                dx *= pad / std::max(1.f, (float)cv::norm(dx));
                dy *= margin;
                wider[0] -= dx + dy;
                wider[1] += dx - dy;
                wider[2] += dx + dy;
                wider[3] += dy - dx;
                auto context = read(crop(bgr, wider));
                if (acceptable(context) && context.first.size() >= reading.first.size())
                    reading = context;
            }
            if (!dates_valid(reading.first)) {
                cv::Mat gray, enhanced;
                cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);
                auto clahe = cv::createCLAHE(2.0, cv::Size(4, 4));
                clahe->apply(gray, enhanced);
                cv::cvtColor(enhanced, enhanced, cv::COLOR_GRAY2BGR);
                auto retry = read(enhanced);
                if (!dates_valid(reading.first) && acceptable(retry))
                    reading = retry;
                if (!dates_valid(reading.first)) {
                    cv::threshold(gray, enhanced, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
                    cv::cvtColor(enhanced, enhanced, cv::COLOR_GRAY2BGR);
                    retry = read(enhanced);
                    if (!dates_valid(reading.first) && acceptable(retry))
                        reading = retry;
                }
            }
        }
        if (trace_enabled()) {
            nlohmann::json points = nlohmann::json::array();
            for (auto p : polygon)
                points.push_back({p.x, p.y});
            trace_event({{"stage", "text_filter"},
                         {"polygon", points},
                         {"text", reading.first},
                         {"confidence", reading.second},
                         {"reason", reading.first.empty()   ? "empty"
                                    : reading.second < .45f ? "low_text_confidence"
                                                            : "retained"}});
        }
        if (!reading.first.empty() && reading.second >= 0.45f)
            lines.push_back({reading.first, reading.second, polygon});
        if (lines.size() > 512)
            throw std::runtime_error("TOO_MANY_TEXT_LINES");
    }
    std::sort(lines.begin(), lines.end(), [](const Line &a, const Line &b) {
        auto ra = cv::boundingRect(a.polygon), rb = cv::boundingRect(b.polygon);
        if (ra.y == rb.y)
            return ra.x < rb.x;
        return ra.y < rb.y;
    });
    return lines;
}
}
