#include "pipeline.h"
#include "trace.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <opencv2/imgproc.hpp>
namespace cardlens_ocr {
namespace {
std::string date_signature(const std::string &value) {
    static const std::regex date("([12][0-9]{3})(?:-|/|\\.|年)([0-9]{1,2})(?:-|/|\\.|月)([0-9]{1,2})");
    std::string signature;
    for (auto match = std::sregex_iterator(value.begin(), value.end(), date);
         match != std::sregex_iterator(); ++match)
        for (int part : {1, 2, 3}) signature += std::to_string(std::stoi((*match)[part])) + ":";
    if (value.find("长期") != std::string::npos) signature += "long-term";
    return signature;
}
bool same_reading(const std::string &key, const std::string &a, const std::string &b) {
    return date_field(key) ? date_signature(a) == date_signature(b) : a == b;
}
}
nlohmann::json refine_fields(nlohmann::json result, const cv::Mat &image,
                            const std::vector<Line> &lines, const std::string &type,
                            const std::string &side, const RefinementReader &reader) {
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
            const bool verifyDate = present && date_field(key) &&
                field_valid(key, result["fields"][key]["value"]) &&
                result["fields"][key]["confidence"].get<float>() < .98f;
            bool verifyText = verifyDate ||
                (present && (key == "authority" || key == "model") && result["status"] == "complete");
            if (present && date_field(key) &&
                result["fields"][key]["confidence"].get<float>() < field_review_threshold(key))
                reviews.push_back(key + ":LOW_CONFIDENCE");
            if (present && field_valid(key, result["fields"][key]["value"]) &&
                result["fields"][key]["confidence"].get<float>() >= (key == "name" ? .94f : .90f) &&
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
                for (auto &line : lines) {
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
                            for (auto &other : lines) {
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
                for (const auto &line : lines)
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
                box &= cv::Rect(0, 0, image.cols, image.rows);
                if (box.empty())
                    continue;
                cv::Mat roi = image(box).clone();
                if (trial >= 3) {
                    cv::Mat gray;
                    cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);
                    if (trial == 4) {
                        auto clahe = cv::createCLAHE(2., cv::Size(4, 4));
                        clahe->apply(gray, gray);
                    }
                    cv::cvtColor(gray, roi, cv::COLOR_GRAY2BGR);
                }
                auto reading = reader.read(roi, key);
                // Detect again at local resolution to separate weak labels and English
                // captions that were merged when the whole photograph was downscaled.
                if (key == "name" || shortField) {
                    std::pair<std::string, float> localBest{"", 0};
                    for (auto &local : reader.run(roi)) {
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
            if (bestVote == votes.end() || bestVote->second.count < 2) {
                if (verifyDate) reviews.push_back(key + ":REVIEW_REQUIRED");
                continue;
            }
            auto &v = bestVote->second;
            if (verifyText) {
                for (const auto &entry : votes)
                    if (!same_reading(key, entry.first, result["fields"][key]["value"])) {
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
            auto refined = reader.refine_name(image, name);
            if (!refined.first.empty()) {
                overrides["name"] = name;
                overrides["name"]["value"] = refined.first;
                overrides["name"]["confidence"] = refined.second;
                reviews.push_back("name:OCR_DISAGREEMENT");
            }
        }
    }
    if (!overrides.empty())
        result = parse_document(lines, type, side, image.cols, image.rows,
                                overrides);
    for (auto &warning : reviews)
        result["warnings"].push_back(warning);
    return result;
}
}
