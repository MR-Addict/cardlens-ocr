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
struct Ocr::Impl {
    ncnn::Net det, rec;
    std::vector<std::string> dictionary;
    std::mutex mutex;
    explicit Impl(const std::string &directory) {
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
    std::pair<std::string, float> read(const cv::Mat &roi, const std::string &field = "",
                                       float aspect = 1.f,
                                       std::vector<CtcCandidate> *candidates = nullptr) {
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
    std::pair<std::string, float> refine_name(const cv::Mat &image, const nlohmann::json &field) {
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
    std::vector<Line> run(const cv::Mat &bgr) {
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
};
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
        throw std::invalid_argument("INVALID_DOCUMENT_TYPE");
    if ((type == "id_card" && side != "portrait" && side != "emblem") ||
        (type != "id_card" && side != "main"))
        throw std::invalid_argument("UNSUPPORTED_SIDE");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    cv::Mat rgba(height, width, CV_8UC4, (void *)pixels, stride), bgr;
    cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
    std::vector<std::pair<cv::Mat, cv::Mat>> images = {{bgr, cv::Mat::eye(3, 3, CV_64F)}};
    cv::Mat grayCard, edges;
    cv::cvtColor(bgr, grayCard, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(grayCard, grayCard, cv::Size(5, 5), 0);
    cv::Canny(grayCard, edges, 40, 120);
    cv::morphologyEx(edges, edges, cv::MORPH_CLOSE,
                     cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5)));
    std::vector<std::vector<cv::Point>> cards;
    cv::findContours(edges, cards, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    double bestArea = 0;
    int cardCount = 0;
    std::vector<cv::Point2f> corners;
    for (auto &contour : cards) {
        double area = std::abs(cv::contourArea(contour));
        if (area < width * height * .15 || area > width * height * .95)
            continue;
        std::vector<cv::Point> approx;
        cv::approxPolyDP(contour, approx, cv::arcLength(contour, true) * .025, true);
        if (approx.size() != 4 || !cv::isContourConvex(approx))
            continue;
        auto r = cv::minAreaRect(approx);
        double ratio =
            std::max(r.size.width, r.size.height) / std::min(r.size.width, r.size.height);
        if (ratio < 1.25 || ratio > 2.1)
            continue;
        ++cardCount;
        if (area < bestArea)
            continue;
        std::vector<cv::Point2f> p;
        for (auto point : approx)
            p.emplace_back(point);
        corners = ordered_points(p);
        bestArea = area;
    }
    if (trace_enabled())
        trace_event({{"stage", "document_localization"},
                     {"cardCount", cardCount},
                     {"sourceSize", {width, height}}});
    if (cardCount > 1)
        return nlohmann::json({{"documentType", type},
                               {"side", side},
                               {"status", "failed"},
                               {"width", width},
                               {"height", height},
                               {"rotation", 0},
                               {"fields", nlohmann::json::object()},
                               {"lines", nlohmann::json::array()},
                               {"warnings", {"MULTIPLE_DOCUMENTS"}}})
            .dump();
    if (!corners.empty()) {
        int cw = std::max(64, (int)std::max(cv::norm(corners[1] - corners[0]),
                                            cv::norm(corners[2] - corners[3])));
        int ch = std::max(64, (int)std::max(cv::norm(corners[3] - corners[0]),
                                            cv::norm(corners[2] - corners[1])));
        std::vector<cv::Point2f> destination = {
            {0, 0}, {(float)cw - 1, 0}, {(float)cw - 1, (float)ch - 1}, {0, (float)ch - 1}};
        cv::Mat transform = cv::getPerspectiveTransform(corners, destination), card;
        cv::warpPerspective(bgr, card, transform, cv::Size(cw, ch), cv::INTER_CUBIC,
                            cv::BORDER_REPLICATE);
        images.push_back({card, transform.inv()});
    }
    nlohmann::json result;
    double best = -1;
    int rotation = 0;
    cv::Mat mapping;
    cv::Size selectedSize;
    cv::Mat selectedImage;
    std::vector<Line> selectedLines;
    for (auto &candidate : images)
        for (int angle : {0, 90, 180, 270}) {
            cv::Mat oriented;
            if (angle == 0)
                oriented = candidate.first;
            else
                cv::rotate(candidate.first, oriented,
                           angle == 90    ? cv::ROTATE_90_CLOCKWISE
                           : angle == 180 ? cv::ROTATE_180
                                          : cv::ROTATE_90_COUNTERCLOCKWISE);
            auto lines = impl_->run(oriented);
            auto parsed = parse_document(lines, type, side, oriented.cols, oriented.rows);
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
            if (score > best || result.is_null()) {
                best = score;
                result = std::move(parsed);
                rotation = angle;
                mapping = candidate.second;
                selectedSize = candidate.first.size();
                selectedImage = oriented;
                selectedLines = lines;
            }
            if (trace_enabled())
                trace_event({{"stage", "orientation"},
                             {"angle", angle},
                             {"score", score},
                             {"bestScore", best},
                             {"sourceSize", {oriented.cols, oriented.rows}}});
        }
    if (trace_enabled())
        trace_event({{"stage", "selected_fields"}, {"rotation", rotation}, {"result", result}});
    // Retry only the selected orientation, keeping the original OCR lines for audit.
    const std::map<std::string, std::vector<std::string>> retryLabels = {
        {"name", {"姓名"}},
        {"sex", {"性别"}},
        {"ethnicity", {"民族", "民"}},
        {"nationality", {"国籍"}},
        {"birthDate", {"出生日期", "出生"}},
        {"firstIssueDate", {"初次领证日期"}},
        {"registerDate", {"注册日期"}},
        {"issueDate", {"发证日期"}},
        {"validity", {"有效期限", "有效期"}},
        {"vehicleClass", {"准驾车型"}},
        {"address", {"住址"}},
        {"authority", {"签发机关"}},
        {"model", {"品牌型号"}}};
    nlohmann::json overrides = nlohmann::json::object();
    std::vector<std::string> reviews;
    if (result["status"] != "failed")
        for (const auto &[key, labels] : retryLabels) {
            if (key == "nationality" || key == "firstIssueDate" || key == "vehicleClass") {
                if (type != "driver_license")
                    continue;
            }
            if (key == "registerDate" || key == "issueDate") {
                if (type != "vehicle_license")
                    continue;
            }
            if (key == "ethnicity" && (type != "id_card" || side != "portrait"))
                continue;
            if (key == "name" || key == "sex" || key == "birthDate" || key == "address") {
                if (type == "id_card" && side == "emblem")
                    continue;
                if (type == "vehicle_license" && key != "address")
                    continue;
            }
            if (key == "authority" && (type != "id_card" || side != "emblem"))
                continue;
            if (key == "model" && type != "vehicle_license")
                continue;
            bool present = result["fields"].contains(key);
            bool verifyText =
                present && (key == "authority" || key == "model") && result["status"] == "complete";
            if (present && field_valid(key, result["fields"][key]["value"]) &&
                (key != "name" || result["fields"][key]["confidence"].get<float>() >= .94f) &&
                !verifyText)
                continue;
            cv::Rect bounds;
            bool found = false;
            if ((key == "name" || verifyText) && present) {
                std::vector<cv::Point2f> pts;
                for (auto &p : result["fields"][key]["polygon"])
                    pts.emplace_back(p[0], p[1]);
                bounds = cv::boundingRect(pts);
                found = true;
            } else
                for (auto &line : selectedLines) {
                    auto t = line.text;
                    for (auto &label : labels) {
                        if (key == "ethnicity" && label == "民" && t.rfind("民", 0) != 0 &&
                            t.find("男") == std::string::npos && t.find("女") == std::string::npos)
                            continue;
                        auto at = t.find(label);
                        if (at == std::string::npos)
                            continue;
                        auto r = cv::boundingRect(line.polygon);
                        bounds = r;
                        found = true;
                        bool inlineValue = t.size() > at + label.size();
                        if (!inlineValue) {
                            bounds.x = r.x + r.width;
                            bounds.width = (key == "sex"            ? 2
                                            : key == "nationality"  ? 4
                                            : key == "vehicleClass" ? 5
                                                                    : 14) *
                                           r.height;
                            for (auto &other : selectedLines) {
                                auto n = cv::boundingRect(other.polygon);
                                if (n.x < bounds.x || std::abs(n.y - r.y) > r.height * .6f)
                                    continue;
                                for (auto &entry : retryLabels)
                                    for (auto &l : entry.second)
                                        if (other.text.find(l) != std::string::npos)
                                            bounds.width = std::min(bounds.width, n.x - bounds.x);
                            }
                        }
                        break;
                    }
                    if (found)
                        break;
                }
            if (!found && key == "ethnicity" && result["fields"].contains("sex")) {
                std::string raw = result["fields"]["sex"]["rawText"];
                for (const auto &line : selectedLines)
                    if (line.text == raw) {
                        auto at = raw.find("男");
                        if (at == std::string::npos)
                            at = raw.find("女");
                        if (at == std::string::npos || at + 3 >= raw.size())
                            continue;
                        auto count = [](const std::string &text) {
                            return std::count_if(text.begin(), text.end(), [](unsigned char c) {
                                return (c & 0xc0) != 0x80;
                            });
                        };
                        auto r = cv::boundingRect(line.polygon);
                        int offset =
                            std::round(r.width * (float)count(raw.substr(0, at + 3)) / count(raw));
                        bounds =
                            cv::Rect(r.x + offset, r.y, std::max(4, r.width - offset), r.height);
                        found = true;
                        break;
                    }
            }
            if (!found || bounds.width < 4)
                continue;
            struct Vote {
                int count = 0;
                float score = 0;
                std::string raw;
                cv::Rect box;
            };
            std::map<std::string, Vote> votes;
            for (int trial = 0; trial < 5; ++trial) {
                float margin = trial == 0 ? .15f : trial == 1 ? .3f : trial == 2 ? .5f : .25f;
                int pad = std::max(2, (int)(bounds.height * margin));
                bool shortField = key == "sex" || key == "nationality" || key == "vehicleClass" ||
                                  key == "ethnicity";
                int leftPad = shortField ? 0 : pad;
                cv::Rect box(bounds.x - leftPad, bounds.y - pad, bounds.width + pad + leftPad,
                             bounds.height + 2 * pad);
                box &= cv::Rect(0, 0, selectedImage.cols, selectedImage.rows);
                if (box.empty())
                    continue;
                cv::Mat roi = selectedImage(box).clone();
                if (trial >= 3) {
                    cv::Mat gray;
                    cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);
                    if (trial == 4) {
                        auto clahe = cv::createCLAHE(2., cv::Size(4, 4));
                        clahe->apply(gray, gray);
                    }
                    cv::cvtColor(gray, roi, cv::COLOR_GRAY2BGR);
                }
                auto reading = impl_->read(roi, key);
                // Detect again at local resolution to separate weak labels and English
                // captions that were merged when the whole photograph was downscaled.
                if (key == "name" || shortField) {
                    std::pair<std::string, float> localBest{"", 0};
                    for (auto &local : impl_->run(roi)) {
                        auto value = local.text;
                        for (auto &label : labels) {
                            auto at = value.find(label);
                            if (at != std::string::npos) {
                                value = value.substr(at + label.size());
                                break;
                            }
                        }
                        if (key == "name" && value.rfind("名", 0) == 0)
                            value = value.substr(std::string("名").size());
                        value = clean_field(key, value);
                        if (field_valid(key, value) && local.confidence > localBest.second)
                            localBest = {local.text, local.confidence};
                    }
                    if (!localBest.first.empty() &&
                        (!field_valid(key, clean_field(key, reading.first)) ||
                         localBest.second > reading.second))
                        reading = localBest;
                }
                auto text = reading.first;
                for (auto &label : labels) {
                    auto at = text.find(label);
                    if (at != std::string::npos) {
                        text = text.substr(at + label.size());
                        break;
                    }
                }
                if (key == "name" && text.rfind("名", 0) == 0)
                    text = text.substr(std::string("名").size());
                auto value = clean_field(key, text);
                if (!field_valid(key, value) || reading.second < (shortField ? .45f : .6f))
                    continue;
                auto &v = votes[value];
                v.count++;
                v.score += reading.second;
                v.raw = reading.first;
                v.box = box;
            }
            auto bestVote = votes.end();
            for (auto it = votes.begin(); it != votes.end(); ++it)
                if (bestVote == votes.end() || it->second.count > bestVote->second.count ||
                    (it->second.count == bestVote->second.count &&
                     it->second.score > bestVote->second.score))
                    bestVote = it;
            if (bestVote == votes.end() || bestVote->second.count < 2)
                continue;
            auto &v = bestVote->second;
            if (verifyText) {
                for (const auto &entry : votes)
                    if (entry.first != result["fields"][key]["value"]) {
                        reviews.push_back(key + ":OCR_DISAGREEMENT");
                        break;
                    }
                // A consistency check requests review; it does not overwrite an
                // otherwise valid reading with a conflicting alternative.
                continue;
            }
            auto b = v.box;
            nlohmann::json polygon = {{b.x, b.y},
                                      {b.x + b.width, b.y},
                                      {b.x + b.width, b.y + b.height},
                                      {b.x, b.y + b.height}};
            overrides[key] = {{"value", bestVote->first},
                              {"rawText", v.raw},
                              {"confidence", v.score / v.count},
                              {"polygon", polygon}};
            if (votes.size() > 1 ||
                (key == "name" && present && result["fields"][key]["value"] != bestVote->first))
                reviews.push_back(key + ":OCR_DISAGREEMENT");
        }
    if (result["status"] != "failed" && result["fields"].contains("name")) {
        const auto &name = result["fields"]["name"];
        if (name["confidence"].get<float>() < .94f && field_valid("name", name["value"]) &&
            (!overrides.contains("name") || overrides["name"]["value"] == name["value"])) {
            auto refined = impl_->refine_name(selectedImage, name);
            if (!refined.first.empty()) {
                overrides["name"] = name;
                overrides["name"]["value"] = refined.first;
                overrides["name"]["confidence"] = refined.second;
                reviews.push_back("name:OCR_DISAGREEMENT");
            }
        }
    }
    if (!overrides.empty())
        result = parse_document(selectedLines, type, side, selectedImage.cols, selectedImage.rows,
                                overrides);
    for (auto &warning : reviews)
        result["warnings"].push_back(warning);
    auto map_polygon = [&](nlohmann::json &polygon) {
        std::vector<cv::Point2f> p;
        for (auto &item : polygon) {
            float x = item[0], y = item[1], u = x, v = y;
            if (rotation == 90) {
                u = y;
                v = selectedSize.height - 1 - x;
            }
            if (rotation == 180) {
                u = selectedSize.width - 1 - x;
                v = selectedSize.height - 1 - y;
            }
            if (rotation == 270) {
                u = selectedSize.width - 1 - y;
                v = x;
            }
            p.emplace_back(u, v);
        }
        if (!p.empty())
            cv::perspectiveTransform(p, p, mapping);
        polygon = nlohmann::json::array();
        for (auto pt : p)
            polygon.push_back({std::clamp(pt.x, 0.f, (float)width - 1),
                               std::clamp(pt.y, 0.f, (float)height - 1)});
    };
    for (auto &line : result["lines"])
        map_polygon(line["polygon"]);
    for (auto &field : result["fields"])
        map_polygon(field["polygon"]);
    result["width"] = width;
    result["height"] = height;
    result["rotation"] = rotation;
    bool edge = false;
    for (auto &line : result["lines"])
        for (auto &point : line["polygon"]) {
            float x = point[0], y = point[1];
            if (x < 2 || y < 2 || x > width - 3 || y > height - 3)
                edge = true;
        }
    if (edge)
        result["warnings"].push_back("BORDER_INCOMPLETE");
    result["warnings"] = result.value("warnings", nlohmann::json::array());
    cv::Mat gray, lap;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::Laplacian(gray, lap, CV_64F);
    cv::Scalar mean, stddev;
    cv::meanStdDev(lap, mean, stddev);
    if (stddev[0] * stddev[0] < 30)
        result["warnings"].push_back("LOW_SHARPNESS");
    if (result["status"] == "complete" && !result["warnings"].empty())
        result["status"] = "partial";
    if (trace_enabled())
        trace_event({{"stage", "result"}, {"result", result}});
    return result.dump();
}
} // namespace cardlens_ocr
