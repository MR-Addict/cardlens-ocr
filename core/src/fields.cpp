#include "internal.h"
#include "trace.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <opencv2/imgproc.hpp>
#include <regex>
namespace cardlens_ocr {
using nlohmann::json;
namespace {
std::string compact(std::string s) {
    s.erase(std::remove_if(
                s.begin(), s.end(),
                [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }),
            s.end());
    return s;
}
json polygon(const std::vector<cv::Point2f> &points) {
    json j = json::array();
    for (auto p : points)
        j.push_back({p.x, p.y});
    return j;
}
const std::vector<std::string> labels = {
    "公民身份号码", "车辆识别代号", "初次领证日期", "有效期限", "出生日期", "品牌型号",
    "发动机号码",   "发动机号",     "注册日期",     "发证日期", "使用性质", "车辆类型",
    "号牌号码",     "准驾车型",     "签发机关",     "有效期",   "证号",     "姓名",
    "性别",         "民族",         "国籍",         "出生",     "住址",     "所有人"};
std::vector<std::string> chars(const std::string &s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        size_t n = c < 128 ? 1 : (c < 224 ? 2 : (c < 240 ? 3 : 4));
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}
size_t fuzzy_prefix(const std::string &text, const std::string &label) {
    auto a = chars(text), b = chars(label);
    if (b.size() < 2 || b.size() > 4 || a.empty())
        return std::string::npos;
    for (size_t length = std::min(a.size(), b.size()); length >= 1; --length) {
        if (length + 1 < b.size())
            break;
        std::vector<int> prev(b.size() + 1), next(b.size() + 1);
        for (size_t j = 0; j <= b.size(); j++)
            prev[j] = j;
        int exact = 0;
        for (size_t i = 1; i <= length; i++) {
            next[0] = i;
            for (size_t j = 1; j <= b.size(); j++)
                next[j] =
                    std::min({prev[j] + 1, next[j - 1] + 1, prev[j - 1] + (a[i - 1] != b[j - 1])});
            prev = next;
            if (i <= b.size() && a[i - 1] == b[i - 1])
                ++exact;
        }
        if (prev.back() == 1 && exact >= 1) {
            size_t bytes = 0;
            for (size_t i = 0; i < length; i++)
                bytes += a[i].size();
            return bytes;
        }
        if (length == 1)
            break;
    }
    return std::string::npos;
}
bool label_fragment(const std::string &text) {
    for (auto &label : labels)
        if (text.size() <= 6 && label.find(text) != std::string::npos)
            return true;
    return false;
}
std::string take_value(std::string s) {
    while (!s.empty() && (s[0] == ':' || s[0] == ' '))
        s.erase(0, 1);
    auto end = s.size();
    for (const auto &label : labels) {
        auto pos = s.find(label);
        if (pos != std::string::npos)
            end = std::min(end, pos);
    }
    return s.substr(0, end);
}
bool address_caption_prefix(const std::string &value, size_t &start) {
    auto c = chars(value);
    size_t offset = 0;
    int precedingChinese = 0;
    for (size_t i = 0; i + 1 < c.size(); ++i) {
        auto chinese = [](const std::string &v) {
            return v.size() == 3 && (unsigned char)v[0] >= 0xe4 && (unsigned char)v[0] <= 0xe9;
        };
        if (chinese(c[i]) && chinese(c[i + 1])) {
            auto prefix = value.substr(0, offset);
            if (precedingChinese <= 1 && std::regex_search(prefix, std::regex("[A-Za-z]{3,}"))) {
                start = offset;
                return true;
            }
            return false;
        }
        precedingChinese += chinese(c[i]);
        offset += c[i].size();
    }
    return false;
}
bool id_valid(const std::string &s) {
    if (s.size() != 18)
        return false;
    int weights[] = {7, 9, 10, 5, 8, 4, 2, 1, 6, 3, 7, 9, 10, 5, 8, 4, 2};
    int sum = 0;
    for (int i = 0; i < 17; i++) {
        if (s[i] < '0' || s[i] > '9')
            return false;
        sum += (s[i] - '0') * weights[i];
    }
    return (s[17] == 'x' ? 'X' : s[17]) == std::string("10X98765432")[sum % 11];
}
} // namespace
bool dates_valid(const std::string &text) {
    std::regex pattern("([12][0-9]{3})(?:-|/|\\.|年)([0-9]{1,2})(?:-|/|\\.|月)([0-9]{1,2})");
    for (auto i = std::sregex_iterator(text.begin(), text.end(), pattern);
         i != std::sregex_iterator(); ++i) {
        int year = std::stoi((*i)[1]), month = std::stoi((*i)[2]), day = std::stoi((*i)[3]);
        int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month < 1 || month > 12 || day < 1)
            return false;
        int max =
            days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        if (day > max)
            return false;
    }
    return true;
}
bool field_valid(const std::string &key, const std::string &value) {
    static const std::string date =
        "[12][0-9]{3}(?:-|/|\\.|年)[0-9]{1,2}(?:-|/|\\.|月)[0-9]{1,2}(?:日)?";
    if (key == "birthDate" || key == "firstIssueDate" || key == "registerDate" ||
        key == "issueDate")
        return std::regex_match(value, std::regex(date)) && dates_valid(value);
    if (key == "validity")
        return std::regex_match(value, std::regex(date + "(?:至|-|—|–)(?:" + date + "|长期)")) &&
               dates_valid(value);
    if (key == "sex")
        return value == "男" || value == "女";
    if (key == "idNumber" || key == "licenseNumber")
        return id_valid(value);
    if (key == "vin")
        return std::regex_match(value, std::regex("[A-HJ-NPR-Z0-9]{17}"));
    if (key == "vehicleClass")
        return std::regex_match(value, std::regex("(?:A[1-3]|B[12]|C[1-6]|[DEFMNP])+"));
    if (key == "nationality") {
        auto slash = value.find('/');
        if (slash != std::string::npos)
            return field_valid("name", value.substr(0, slash)) &&
                   std::regex_match(value.substr(slash + 1), std::regex("[A-Z]{2,3}"));
    }
    if (key == "ethnicity") {
        // Reference vocabulary: https://www.neac.gov.cn/seac/ztzl/zgmzjs/index.shtml
        static const std::vector<std::string> names = {
            "汉",   "蒙古",     "回",   "藏",     "维吾尔", "苗",    "彝",       "壮",     "布依",
            "朝鲜", "满",       "侗",   "瑶",     "白",     "土家",  "哈尼",     "哈萨克", "傣",
            "黎",   "傈僳",     "佤",   "畲",     "高山",   "拉祜",  "水",       "东乡",   "纳西",
            "景颇", "柯尔克孜", "土",   "达斡尔", "仫佬",   "羌",    "布朗",     "撒拉",   "毛南",
            "仡佬", "锡伯",     "阿昌", "普米",   "塔吉克", "怒",    "乌孜别克", "俄罗斯", "鄂温克",
            "德昂", "保安",     "裕固", "京",     "塔塔尔", "独龙",  "鄂伦春",   "赫哲",   "门巴",
            "珞巴", "基诺",     "其他", "未识别", "入籍",   "穿青人"};
        for (const auto &name : names)
            if (value == name || value == name + "族")
                return true;
        return false;
    }
    if (key == "name" || key == "nationality" || key == "ethnicity" || key == "authority") {
        auto c = chars(value);
        auto minimum = key == "ethnicity" ? 1u : 2u;
        return c.size() >= minimum && c.size() <= 40 &&
               std::all_of(c.begin(), c.end(),
                           [](const auto &v) { return v.size() == 3 || v == "·"; });
    }
    if (key == "address") {
        auto c = chars(value);
        return std::count_if(c.begin(), c.end(), [](const auto &v) {
                   return v.size() == 3 && (unsigned char)v[0] >= 0xe4 &&
                          (unsigned char)v[0] <= 0xe9;
               }) >= 2;
    }
    return !value.empty();
}
std::string clean_field(const std::string &key, std::string value) {
    value = compact(value);
    if ((key == "idNumber" || key == "licenseNumber") && value.size() == 18 && value.back() == 'x')
        value.back() = 'X';
    if (key == "address") {
        size_t start = 0;
        if (address_caption_prefix(value, start))
            value = value.substr(start);
    }
    if (key == "birthDate" || key == "firstIssueDate" || key == "registerDate" ||
        key == "issueDate" || key == "validity")
        value = std::regex_replace(
            value, std::regex("(^|[^0-9])([12][0-9]{3})([0-9]{2})-([0-9]{2})(?![0-9])"),
            "$1$2-$3-$4");
    if (key == "sex") {
        auto male = value.find("男"), female = value.find("女");
        if ((male != std::string::npos) != (female != std::string::npos))
            return male != std::string::npos ? "男" : "女";
        return "";
    }
    if (key == "nationality" || key == "ethnicity" || key == "name" || key == "address" ||
        key == "authority") {
        value = std::regex_replace(value, std::regex("^[A-Za-z.:]+"), "");
        if (key == "address" && value.rfind("址", 0) == 0) {
            value = value.substr(std::string("址").size());
            value = std::regex_replace(value, std::regex("^[A-Za-z.:]+"), "");
        }
    }
    if (key == "vehicleClass") {
        value = std::regex_replace(value, std::regex("^(Class|Clnss)"), "");
        for (auto &c : value)
            if (c >= 'a' && c <= 'z')
                c -= ('a' - 'A');
        value = std::regex_replace(value, std::regex("([A-F])-([1-6])"), "$1$2");
    }
    if (key == "birthDate" || key == "firstIssueDate" || key == "registerDate" ||
        key == "issueDate") {
        // A trailing Latin OCR artifact may be discarded, but an extra digit must
        // not silently turn an incomplete or malformed date into a valid one.
        static const std::regex date(
            "[12][0-9]{3}(?:-|/|\\.|年)[0-9]{1,2}(?:-|/|\\.|月)[0-9]{1,2}(?:日)?");
        std::smatch m;
        if (std::regex_search(value, m, date)) {
            auto suffix = value.substr(m.position() + m.length());
            if (suffix.find_first_of("0123456789") == std::string::npos)
                return m.str();
        }
    }
    return value;
}
json parse_document(const std::vector<Line> &lines, const std::string &type,
                    const std::string &side, int width, int height, const json &overrides) {
    json result = {{"documentType", type},   {"side", side},
                   {"status", "partial"},    {"width", width},
                   {"height", height},       {"fields", json::object()},
                   {"lines", json::array()}, {"warnings", json::array()}};
    for (auto &line : lines)
        result["lines"].push_back({{"text", line.text},
                                   {"confidence", line.confidence},
                                   {"polygon", polygon(line.polygon)}});
    std::string pageText;
    for (const auto &line : lines)
        pageText += line.text;
    bool idPair = pageText.find("居民身份证") != std::string::npos &&
                  pageText.find("身份号码") != std::string::npos;
    bool mixedTypes = pageText.find("驾驶证") != std::string::npos &&
                      pageText.find("行驶证") != std::string::npos;
    bool driverSupplement = pageText.find("驾驶证副页") != std::string::npos;
    bool driverMain = pageText.find("初次领证") != std::string::npos ||
                      pageText.find("准驾车型") != std::string::npos;
    bool vehicleSupplement = pageText.find("核定载") != std::string::npos &&
                             pageText.find("总质量") != std::string::npos;
    bool vehicleMain = pageText.find("车辆识别代号") != std::string::npos ||
                       pageText.find("品牌型号") != std::string::npos;
    if (idPair || mixedTypes || (driverSupplement && driverMain) ||
        (vehicleSupplement && vehicleMain)) {
        result["status"] = "failed";
        result["warnings"].push_back("MULTIPLE_DOCUMENTS");
        return result;
    }
    if ((type == "driver_license" && driverSupplement && !driverMain) ||
        (type == "vehicle_license" && vehicleSupplement && !vehicleMain)) {
        result["status"] = "failed";
        result["warnings"].push_back("UNSUPPORTED_PAGE");
        return result;
    }
    auto put = [&](const std::string &key, std::string value, const Line &source) {
        value = clean_field(key, value);
        if (value.empty())
            return;
        result["fields"][key] = {{"value", value},
                                 {"rawText", source.text},
                                 {"confidence", source.confidence},
                                 {"polygon", polygon(source.polygon)}};
        if (trace_enabled())
            trace_event({{"stage", "field_selected"},
                         {"field", key},
                         {"value", value},
                         {"rawText", source.text},
                         {"polygon", polygon(source.polygon)}});
    };
    auto anchor = [&](const std::string &key, const std::vector<std::string> &keys) {
        // Exact label matches always outrank approximate matches anywhere on the page.
        for (int fuzzy = 0; fuzzy < 2; ++fuzzy)
            for (auto &line : lines) {
                auto text = compact(line.text);
                for (auto &label : keys) {
                    auto pos = text.find(label);
                    if (label == "号码" && (pos != 0 || key != "plateNumber"))
                        continue;
                    if ((pos == std::string::npos) != (fuzzy != 0))
                        continue;
                    size_t consumed = fuzzy ? fuzzy_prefix(text, label) : pos + label.size();
                    if (consumed == std::string::npos)
                        continue;
                    auto value = clean_field(key, take_value(text.substr(consumed)));
                    if (label_fragment(value) || ((key == "address" || key == "name" ||
                                                   key == "nationality" || key == "vehicleClass") &&
                                                  !field_valid(key, value)))
                        value.clear();
                    if (label == "号码" &&
                        !std::regex_match(
                            value, std::regex("[\xE4-\xE9][\x80-\xBF]{2}[A-Z][A-Z0-9]{5,6}")))
                        continue;
                    if (!value.empty()) {
                        put(key, value, line);
                        return;
                    }
                    auto rect = cv::boundingRect(line.polygon);
                    const Line *best = nullptr;
                    std::string bestValue;
                    float distance = 1e9;
                    for (auto &other : lines) {
                        if (&other == &line)
                            continue;
                        auto r = cv::boundingRect(other.polygon);
                        float dy = std::abs((r.y + r.height * .5f) - (rect.y + rect.height * .5f));
                        if (r.x < rect.x - rect.height * .4f ||
                            dy > std::max(r.height, rect.height) * .9f ||
                            r.x > rect.x + rect.width + rect.height * 8)
                            continue;
                        // A value may overlap its own label, but a distinct label blocks
                        // searching across the next field in the same row.
                        bool blocked = false;
                        for (auto &barrier : lines) {
                            auto br = cv::boundingRect(barrier.polygon);
                            if (br.x <= rect.x + rect.width * .5f || br.x >= r.x ||
                                std::abs(br.y - rect.y) > rect.height * .6f)
                                continue;
                            for (auto &l : labels)
                                if (compact(barrier.text).find(l) != std::string::npos)
                                    blocked = true;
                        }
                        if (blocked)
                            continue;
                        auto candidate = compact(other.text);
                        if (label_fragment(candidate))
                            continue;
                        if (r.x < rect.x + rect.width) {
                            auto labelChars = chars(label);
                            for (size_t count = labelChars.size(); count > 0; --count) {
                                std::string suffix;
                                for (size_t i = labelChars.size() - count; i < labelChars.size();
                                     ++i)
                                    suffix += labelChars[i];
                                if (candidate.rfind(suffix, 0) == 0) {
                                    candidate = candidate.substr(suffix.size());
                                    break;
                                }
                            }
                        }
                        candidate = clean_field(key, take_value(candidate));
                        if (candidate.empty())
                            continue;
                        if ((key == "sex" || key == "nationality" || key == "ethnicity" ||
                             key == "vehicleClass" || key == "address" || key == "name") &&
                            !field_valid(key, candidate)) {
                            if (trace_enabled())
                                trace_event({{"stage", "field_rejected"},
                                             {"field", key},
                                             {"rawText", other.text},
                                             {"value", candidate},
                                             {"reason", "invalid_field_format"},
                                             {"polygon", polygon(other.polygon)}});
                            continue;
                        }
                        if (std::regex_match(candidate, std::regex("[A-Za-z.]+")))
                            continue;
                        float d = std::max(0, r.x - rect.x - rect.width) + dy * 2;
                        if (key == "address" && (candidate.find("省") != std::string::npos ||
                                                 candidate.find("市") != std::string::npos ||
                                                 candidate.find("县") != std::string::npos ||
                                                 candidate.find("区") != std::string::npos))
                            d -= rect.height * 2;
                        if (d < distance) {
                            distance = d;
                            best = &other;
                            bestValue = candidate;
                        }
                    }
                    if (best) {
                        put(key, bestValue, *best);
                        return;
                    }
                }
            }
    };
    std::vector<std::string> required;
    if (type == "id_card" && side == "emblem") {
        anchor("authority", {"签发机关"});
        anchor("validity", {"有效期限", "有效期"});
        required = {"authority", "validity"};
    } else if (type == "id_card") {
        anchor("name", {"姓名"});
        anchor("sex", {"性别"});
        anchor("ethnicity", {"民族"});
        anchor("birthDate", {"出生日期", "出生"});
        anchor("address", {"住址"});
        for (auto &line : lines) {
            std::smatch match;
            auto text = compact(line.text);
            if (std::regex_search(text, match, std::regex("[0-9]{17}[0-9Xx]"))) {
                put("idNumber", match.str(), line);
                break;
            }
        }
        if (!result["fields"].contains("name") && result["fields"].contains("sex") &&
            result["fields"].contains("idNumber")) {
            const auto &sex = result["fields"]["sex"];
            std::vector<cv::Point2f> points;
            for (auto &p : sex["polygon"])
                points.emplace_back(p[0].get<float>(), p[1].get<float>());
            auto r = cv::boundingRect(points);
            const Line *candidate = nullptr;
            float distance = 1e9;
            for (auto &line : lines) {
                auto n = cv::boundingRect(line.polygon);
                auto text = compact(line.text);
                bool label = false;
                for (auto &l : labels)
                    if (text.find(l) != std::string::npos)
                        label = true;
                auto codepoints = chars(text);
                bool ascii =
                    std::all_of(text.begin(), text.end(), [](unsigned char c) { return c < 128; });
                if (label || ascii || codepoints.size() < 2 || codepoints.size() > 20 ||
                    n.y >= r.y - r.height * .5 || n.y < r.y - r.height * 4 ||
                    n.x < r.x - r.height || n.x > r.x + r.width + r.height * 2)
                    continue;
                float d = (r.y - n.y) + std::abs(n.x - r.x) * .3f;
                if (d < distance) {
                    distance = d;
                    candidate = &line;
                }
            }
            if (candidate)
                put("name", candidate->text, *candidate);
        }
        required = {"name", "sex", "ethnicity", "birthDate", "address", "idNumber"};
    } else if (type == "driver_license") {
        anchor("name", {"姓名"});
        anchor("sex", {"性别"});
        anchor("nationality", {"国籍"});
        anchor("address", {"住址"});
        anchor("birthDate", {"出生日期"});
        anchor("firstIssueDate", {"初次领证日期"});
        anchor("vehicleClass", {"准驾车型"});
        anchor("validity", {"有效期限", "有效期"});
        anchor("licenseNumber", {"证号"});
        if (!result["fields"].contains("licenseNumber"))
            for (auto &line : lines) {
                std::smatch m;
                auto t = compact(line.text);
                if (std::regex_search(t, m, std::regex("[0-9]{17}[0-9Xx]"))) {
                    put("licenseNumber", m.str(), line);
                    break;
                }
            }
        required = {"name",           "sex",          "nationality", "address",      "birthDate",
                    "firstIssueDate", "vehicleClass", "validity",    "licenseNumber"};
    } else {
        anchor("plateNumber", {"号牌号码", "号牌码", "号码"});
        anchor("vehicleType", {"车辆类型"});
        anchor("owner", {"所有人"});
        anchor("address", {"住址"});
        anchor("useCharacter", {"使用性质"});
        anchor("model", {"品牌型号"});
        anchor("vin", {"车辆识别代号"});
        anchor("engineNumber", {"发动机号码", "发动机号"});
        anchor("registerDate", {"注册日期"});
        anchor("issueDate", {"发证日期"});
        required = {"plateNumber", "vehicleType", "owner",        "address",      "useCharacter",
                    "model",       "vin",         "engineNumber", "registerDate", "issueDate"};
    }
    const std::map<std::string, std::vector<std::string>> dateLabels = {
        {"birthDate", {"出生日期", "出生"}},
        {"firstIssueDate", {"初次领证日期"}},
        {"registerDate", {"注册日期"}},
        {"issueDate", {"发证日期"}},
        {"validity", {"有效期限", "有效期"}}};
    for (const auto &[key, names] : dateLabels) {
        if (key == "registerDate" || key == "issueDate") {
            if (type != "vehicle_license")
                continue;
        }
        if (key == "firstIssueDate" && type != "driver_license")
            continue;
        for (auto &labelLine : lines) {
            auto text = compact(labelLine.text);
            size_t end = std::string::npos;
            for (auto &label : names) {
                auto pos = text.find(label);
                if (pos != std::string::npos) {
                    end = pos + label.size();
                    break;
                }
            }
            if (end == std::string::npos)
                continue;
            auto r = cv::boundingRect(labelLine.polygon);
            int right = width;
            for (auto &other : lines) {
                auto n = cv::boundingRect(other.polygon);
                if (n.x <= r.x + r.width * .7f || std::abs(n.y - r.y) > r.height * .65f)
                    continue;
                for (auto &label : labels)
                    if (compact(other.text).find(label) != std::string::npos)
                        right = std::min(right, n.x);
            }
            std::vector<const Line *> row;
            for (auto &other : lines) {
                auto n = cv::boundingRect(other.polygon);
                if (&other != &labelLine && n.x >= right)
                    continue;
                if (n.x < r.x - r.height * .4f ||
                    std::abs(n.y + n.height * .5f - r.y - r.height * .5f) >
                        std::max(n.height, r.height) * .9f)
                    continue;
                auto t = compact(other.text);
                if (t.find_first_of("0123456789") == std::string::npos &&
                    t.find("长期") == std::string::npos)
                    continue;
                row.push_back(&other);
            }
            std::sort(row.begin(), row.end(), [](auto a, auto b) {
                return cv::boundingRect(a->polygon).x < cv::boundingRect(b->polygon).x;
            });
            std::string joined, raw;
            cv::Rect bounds;
            float confidence = 1.f;
            int previousRight = -1;
            for (auto source : row) {
                auto t = compact(source->text);
                if (source == &labelLine)
                    t = t.substr(end);
                auto start = t.find_first_of("0123456789");
                if (start == std::string::npos) {
                    if (t.find("长期") == std::string::npos)
                        continue;
                    start = t.find("长期");
                }
                // Preserve the range separator on a continuation box.
                if (start > 0 && (t.substr(0, start) == "至" || t.substr(0, start) == "-" ||
                                  t.substr(0, start) == "—"))
                    start = 0;
                t = t.substr(start);
                auto b = cv::boundingRect(source->polygon);
                size_t overlap = 0;
                if (b.x < previousRight)
                    for (size_t n = 1; n <= std::min(joined.size(), t.size()); ++n)
                        if (joined.compare(joined.size() - n, n, t, 0, n) == 0)
                            overlap = n;
                joined += t.substr(overlap);
                raw += (raw.empty() ? "" : "\n") + source->text;
                bounds = bounds.empty() ? b : bounds | b;
                previousRight = std::max(previousRight, b.x + b.width);
                confidence = std::min(confidence, source->confidence);
            }
            auto value = clean_field(key, joined);
            if (field_valid(key, value)) {
                Line source{raw,
                            confidence,
                            {{(float)bounds.x, (float)bounds.y},
                             {(float)(bounds.x + bounds.width), (float)bounds.y},
                             {(float)(bounds.x + bounds.width), (float)(bounds.y + bounds.height)},
                             {(float)bounds.x, (float)(bounds.y + bounds.height)}}};
                put(key, value, source);
                break;
            }
        }
    }
    if (type == "vehicle_license")
        for (const auto &line : lines) {
            auto text = compact(line.text);
            if (text.find("注册日期") == std::string::npos)
                continue;
            static const std::regex dateToken("[12][0-9]{3}[-/.][0-9]{1,2}[-/.][0-9]{1,2}");
            std::vector<std::pair<size_t, std::string>> dates;
            for (auto i = std::sregex_iterator(text.begin(), text.end(), dateToken);
                 i != std::sregex_iterator(); ++i)
                dates.push_back({i->position(), i->str()});
            if (dates.size() != 2 || !field_valid("registerDate", dates[0].second) ||
                !field_valid("issueDate", dates[1].second))
                continue;
            auto middle = text.substr(dates[0].first + dates[0].second.size(),
                                      dates[1].first - dates[0].first - dates[0].second.size());
            if (middle.find("发") == std::string::npos)
                continue;
            for (int i = 0; i < 2; ++i) {
                auto candidate = line;
                float start =
                    (float)chars(text.substr(0, dates[i].first)).size() / chars(text).size();
                float end =
                    (float)chars(text.substr(0, dates[i].first + dates[i].second.size())).size() /
                    chars(text).size();
                auto top = line.polygon[1] - line.polygon[0],
                     bottom = line.polygon[2] - line.polygon[3];
                candidate.polygon = {line.polygon[0] + top * start, line.polygon[0] + top * end,
                                     line.polygon[3] + bottom * end,
                                     line.polygon[3] + bottom * start};
                put(i == 0 ? "registerDate" : "issueDate", dates[i].second, candidate);
            }
        }
    // Recover a printed birth date when its label is missing. A valid ID number and
    // agreement with its date constrain this fallback; the value still comes from OCR.
    if (type == "id_card" && side == "portrait" && result["fields"].contains("idNumber")) {
        std::string id = result["fields"]["idNumber"]["value"];
        if (id_valid(id) && !result["fields"].contains("birthDate")) {
            for (auto &line : lines) {
                std::smatch m;
                auto text = compact(line.text);
                if (std::regex_search(text, m,
                                      std::regex("([12][0-9]{3})年([0-9]{1,2})月([0-9]{1,2})日")) &&
                    dates_valid(m.str()) && m[1].str() == id.substr(6, 4) &&
                    std::stoi(m[2]) == std::stoi(id.substr(10, 2)) &&
                    std::stoi(m[3]) == std::stoi(id.substr(12, 2))) {
                    put("birthDate", m.str(), line);
                    result["warnings"].push_back("birthDate:LABEL_MISSING");
                    break;
                }
            }
        }
    }
    // Printed birth date and checksum-validated number provide layout anchors
    // when labels on a worn ID card disappear. Keep extracted OCR text unchanged.
    if (type == "id_card" && side == "portrait" && result["fields"].contains("birthDate") &&
        result["fields"].contains("idNumber") && id_valid(result["fields"]["idNumber"]["value"])) {
        const Line *birth = nullptr, *number = nullptr;
        for (auto &line : lines) {
            if (line.text == result["fields"]["birthDate"]["rawText"])
                birth = &line;
            if (line.text == result["fields"]["idNumber"]["rawText"])
                number = &line;
        }
        if (birth && number) {
            auto b = cv::boundingRect(birth->polygon), n = cv::boundingRect(number->polygon);
            const Line *address = nullptr, *name = nullptr;
            for (auto &line : lines) {
                auto r = cv::boundingRect(line.polygon);
                auto text = compact(line.text);
                bool label = false;
                for (auto &l : labels)
                    if (text.find(l) != std::string::npos)
                        label = true;
                if (label || text.empty())
                    continue;
                if (r.y > b.y + b.height * .5f && r.y < n.y - b.height &&
                    std::abs(r.x - b.x) < b.height * 2.5f &&
                    text.find_first_of("0123456789") != 0 &&
                    (text.find("省") != std::string::npos || text.find("市") != std::string::npos ||
                     text.find("县") != std::string::npos) &&
                    (!address || r.y < cv::boundingRect(address->polygon).y))
                    address = &line;
                auto letters = chars(text);
                bool chinese = std::all_of(letters.begin(), letters.end(),
                                           [](const auto &c) { return c.size() == 3; });
                if (chinese && letters.size() >= 2 && letters.size() <= 6 && r.y < b.y - b.height &&
                    r.y > b.y - b.height * 5 && std::abs(r.x - b.x) < b.height * 2 &&
                    text.find("民") == std::string::npos && text.find("性") == std::string::npos &&
                    (!name || r.y < cv::boundingRect(name->polygon).y))
                    name = &line;
            }
            if (!result["fields"].contains("address") && address) {
                put("address", address->text, *address);
                result["warnings"].push_back("address:LABEL_MISSING");
            }
            if (!result["fields"].contains("name") && name) {
                auto text = compact(name->text);
                if (text.rfind("名", 0) == 0)
                    text = text.substr(std::string("名").size());
                put("name", text, *name);
                result["warnings"].push_back("name:LABEL_MISSING");
            }
        }
    }
    // Join a validity range split into separate OCR boxes on the same row.
    if (result["fields"].contains("validity")) {
        auto &field = result["fields"]["validity"];
        std::string value = field["value"];
        if (std::regex_match(value, std::regex("[12][0-9]{3}[-/.][0-9]{1,2}[-/.][0-9]{1,2}"))) {
            std::vector<cv::Point2f> pts;
            for (auto &p : field["polygon"])
                pts.emplace_back(p[0], p[1]);
            auto r = cv::boundingRect(pts);
            const Line *best = nullptr;
            for (auto &line : lines) {
                auto n = cv::boundingRect(line.polygon);
                auto text = compact(line.text);
                if (std::regex_match(
                        text,
                        std::regex("(至|-|—)([12][0-9]{3}[-/.][0-9]{1,2}[-/.][0-9]{1,2}|长期)")) &&
                    n.x >= r.x + r.width - r.height && n.x < r.x + r.width + r.height * 3 &&
                    std::abs(n.y + n.height * .5f - r.y - r.height * .5f) <
                        std::max(n.height, r.height) * .6f &&
                    (!best || n.x < cv::boundingRect(best->polygon).x))
                    best = &line;
            }
            if (best) {
                field["value"] = value + compact(best->text);
                field["rawText"] = field["rawText"].get<std::string>() + "\n" + best->text;
                field["confidence"] = std::min(field["confidence"].get<float>(), best->confidence);
                auto u = r | cv::boundingRect(best->polygon);
                field["polygon"] = {{u.x, u.y},
                                    {u.x + u.width, u.y},
                                    {u.x + u.width, u.y + u.height},
                                    {u.x, u.y + u.height}};
            }
        }
    }
    // Extend multiline addresses using alignment and vertical proximity, without crossing another
    // field label.
    if (result["fields"].contains("address")) {
        auto &field = result["fields"]["address"];
        std::string raw = field["rawText"], value = field["value"];
        for (const auto &line : lines)
            if (line.text == raw) {
                auto r = cv::boundingRect(line.polygon);
                auto combinedBounds = r;
                float contentLeft =
                    r.x + (compact(line.text).rfind("住址", 0) == 0 ? 2.f * r.height : 0.f);
                for (const auto &next : lines) {
                    auto n = cv::boundingRect(next.polygon);
                    if (n.y <= r.y + r.height / 2 || n.y > r.y + r.height * 2.8 ||
                        n.x < contentLeft - r.height * .65 || n.x > contentLeft + r.height * 2.5)
                        continue;
                    bool has = false;
                    // A label anywhere on a later row ends the address block,
                    // including stamps located to the left of the next field.
                    for (const auto &boundary : lines) {
                        auto br = cv::boundingRect(boundary.polygon);
                        auto text = compact(boundary.text);
                        if ((text == "住" || text == "址" || text == "住址") &&
                            br.y < r.y + r.height * 1.2f)
                            continue;
                        if (br.y <= r.y + r.height * .5f || n.y < br.y - r.height * .3f)
                            continue;
                        for (const auto &label : labels)
                            if (boundary.text.find(label) != std::string::npos)
                                has = true;
                    }
                    for (auto &l : labels)
                        if (next.text.find(l) != std::string::npos)
                            has = true;
                    // Values already assigned to another field are not address continuations.
                    for (auto it = result["fields"].begin(); it != result["fields"].end(); ++it)
                        if (it.key() != "address" && it.value()["rawText"] == next.text)
                            has = true;
                    // A new field label to the left marks the next row even if the
                    // value itself contains no label (for example, useCharacter).
                    for (const auto &labelLine : lines) {
                        auto lr = cv::boundingRect(labelLine.polygon);
                        auto text = compact(labelLine.text);
                        if ((text == "住" || text == "址" || text == "住址") &&
                            lr.y < r.y + r.height * 1.2f)
                            continue;
                        if (lr.x >= n.x ||
                            std::abs(lr.y - n.y) > std::max(lr.height, n.height) * .6f)
                            continue;
                        for (const auto &label : labels)
                            if (compact(labelLine.text).find(label) != std::string::npos ||
                                fuzzy_prefix(compact(labelLine.text), label) != std::string::npos)
                                has = true;
                    }
                    bool content = false;
                    for (const auto &c : chars(next.text))
                        if (c.size() == 3 && (unsigned char)c[0] >= 0xe4 &&
                            (unsigned char)c[0] <= 0xe9)
                            content = true;
                    if (!content && next.text.find_first_of("0123456789") != std::string::npos &&
                        !std::regex_search(next.text, std::regex("[A-Za-z]{3,}")))
                        content = true;
                    if (!has && content && !next.text.empty()) {
                        value += compact(next.text);
                        raw += "\n" + next.text;
                        auto unionRect = combinedBounds | n;
                        combinedBounds = unionRect;
                        field["confidence"] =
                            std::min(field["confidence"].get<float>(), next.confidence);
                        field["polygon"] = {
                            {unionRect.x, unionRect.y},
                            {unionRect.x + unionRect.width, unionRect.y},
                            {unionRect.x + unionRect.width, unionRect.y + unionRect.height},
                            {unionRect.x, unionRect.y + unionRect.height}};
                    }
                }
                break;
            }
        field["value"] = value;
        field["rawText"] = raw;
    }
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        if (field_valid(it.key(), it.value()["value"]))
            result["fields"][it.key()] = it.value();
    for (auto key : {"birthDate", "firstIssueDate", "validity", "registerDate", "issueDate"})
        if (result["fields"].contains(key)) {
            std::string value = result["fields"][key]["value"];
            std::regex date("[12][0-9]{3}(?:-|/|\\.|年)[0-9]{1,2}(?:-|/|\\.|月)[0-9]{1,2}");
            if (!dates_valid(value))
                result["warnings"].push_back(std::string(key) + ":DATE_INVALID");
            else if (!std::regex_search(value, date) && value != "长期")
                result["warnings"].push_back(std::string(key) + ":DATE_FORMAT_UNRECOGNIZED");
        }
    std::string all;
    for (auto &l : lines)
        all += l.text;
    bool matches = type == "id_card"
                       ? (side == "emblem" ? all.find("居民身份证") != std::string::npos
                                           : all.find("身份号码") != std::string::npos)
                   : type == "driver_license" ? all.find("驾驶证") != std::string::npos
                                              : all.find("行驶证") != std::string::npos;
    if (!matches) {
        bool known = all.find("驾驶证") != std::string::npos ||
                     all.find("行驶证") != std::string::npos ||
                     all.find("身份号码") != std::string::npos ||
                     all.find("居民身份证") != std::string::npos;
        result["warnings"].push_back(known ? "DOCUMENT_TYPE_MISMATCH"
                                           : "DOCUMENT_TYPE_UNCONFIRMED");
        if (known) {
            result["fields"] = json::object();
            result["status"] = "failed";
        } else
            result["status"] = result["fields"].empty() ? "failed" : "partial";
    }
    for (auto key : {"idNumber", "licenseNumber"})
        if (result["fields"].contains(key)) {
            std::string number = result["fields"][key]["value"];
            if (!id_valid(number))
                result["warnings"].push_back(std::string(key) + ":CHECKSUM_INVALID");
        }
    if (result["fields"].contains("vin")) {
        std::string vin = result["fields"]["vin"]["value"];
        if (!std::regex_match(vin, std::regex("[A-HJ-NPR-Z0-9]{17}")))
            result["warnings"].push_back("vin:FORMAT_INVALID");
    }
    if (result["fields"].contains("plateNumber")) {
        auto text = result["fields"]["plateNumber"]["value"].get<std::string>();
        if (text.find_first_of("OI") != std::string::npos)
            result["warnings"].push_back("plateNumber:AMBIGUOUS_CHARACTER");
    }
    if (result["fields"].contains("address")) {
        size_t start = 0;
        std::string addressRaw = compact(result["fields"]["address"]["rawText"]);
        auto label = addressRaw.find("住址");
        if (label != std::string::npos)
            addressRaw = addressRaw.substr(label + std::string("住址").size());
        if (address_caption_prefix(addressRaw, start))
            result["warnings"].push_back("address:CAPTION_REMOVED");
    }
    for (auto &key : required) {
        if (!result["fields"].contains(key))
            result["warnings"].push_back(key + ":MISSING");
        else {
            const auto &f = result["fields"][key];
            if (!field_valid(key, f["value"]))
                result["warnings"].push_back(key + ":FIELD_INVALID");
            if (f["confidence"].get<float>() < .85f)
                result["warnings"].push_back(key + ":LOW_CONFIDENCE");
        }
    }
    if (result["fields"].contains("name") &&
        result["fields"]["name"]["confidence"].get<float>() < .94f)
        result["warnings"].push_back("name:REVIEW_REQUIRED");
    result["status"] = result["fields"].empty()     ? "failed"
                       : result["warnings"].empty() ? "complete"
                                                    : "partial";
    return result;
}
} // namespace cardlens_ocr
