#include "cardlens_ocr/core.h"
#include "internal.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
using namespace cardlens_ocr;
using nlohmann::json;
int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    int failures = 0;
    auto check = [&](bool value, const char *name) {
        if (!value) {
            std::cerr << name << "\n";
            ++failures;
        }
    };
    auto rejects = [&](auto function, const char *name) {
        bool rejected = false;
        try {
            function();
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected, name);
    };
    rejects([] { Ocr bad; bad.initialize("/nonexistent/cardlens-ocr-models"); }, "missing models accepted");
    check(dates_valid("2024-02-29"), "leap date rejected");
    check(!dates_valid("2023-02-29"), "invalid leap date accepted");
    check(!dates_valid("2025-10-61"), "invalid day accepted");
    std::vector<Line> synthetic = {
        {"居民身份证", .99f, {{0, 0}, {300, 0}, {300, 30}, {0, 30}}},
        {"签发机关测试机关", .99f, {{0, 50}, {300, 50}, {300, 80}, {0, 80}}},
        {"有效期限2025.01.01-2035.01.01", .99f, {{0, 100}, {500, 100}, {500, 130}, {0, 130}}}};
    auto fields = parse_document(synthetic, "id_card", "emblem", 600, 400);
    check(fields["fields"]["authority"]["value"] == "测试机关", "anchor parsing failed");
    check(fields["fields"]["validity"]["rawText"] == synthetic[2].text, "raw text lost");
    auto line = [](std::string text, float x, float y, float w = 200) {
        return Line{text, .99f, {{x, y}, {x + w, y}, {x + w, y + 25}, {x, y + 25}}};
    };
    auto vehicle = parse_document({line("机动车行驶证", 0, 0), line("住址", 0, 50, 50),
                                   line("某省某市某村", 70, 50), line("使用性质", 0, 85, 65),
                                   line("非营运", 70, 85), line("注册日期2020-01-02D", 0, 150),
                                   line("号牌号码某AOO123", 0, 200)},
                                  "vehicle_license", "main", 600, 400);
    check(vehicle["fields"]["address"]["value"] == "某省某市某村",
          "address swallowed another field");
    check(vehicle["fields"]["registerDate"]["value"] == "2020-01-02", "date suffix retained");
    check(vehicle["fields"]["registerDate"]["rawText"] == "注册日期2020-01-02D",
          "date raw text changed");
    check(std::find(vehicle["warnings"].begin(), vehicle["warnings"].end(),
                    "plateNumber:AMBIGUOUS_CHARACTER") != vehicle["warnings"].end(),
          "plate ambiguity unreported");
    auto range =
        parse_document({line("机动车驾驶证", 0, 0), line("有效期限", 0, 50, 80),
                        line("2020-01-02", 100, 50, 140), line("至2030-01-02", 250, 50, 160)},
                       "driver_license", "main", 600, 400);
    check(range["fields"]["validity"]["value"] == "2020-01-02至2030-01-02",
          "split validity truncated");
    check(range["fields"]["validity"]["polygon"][1][0].get<int>() >= 410,
          "range geometry truncated");
    auto wornLines = std::vector<Line>{
        line("张某某", 90, 10),           line("1995年5月13日", 90, 100),
        line("某省某市某区", 90, 140),    line("某路12号", 90, 175),
        line("公民身份号码", 0, 240, 80), line("440305199505132561", 100, 240, 280)};
    auto worn = parse_document(wornLines, "id_card", "portrait", 600, 400);
    check(worn["fields"]["birthDate"]["value"] == "1995年5月13日", "unlabelled birth date lost");
    check(worn["fields"]["name"]["value"] == "张某某", "unlabelled name lost");
    check(worn["fields"]["address"]["value"] == "某省某市某区某路12号", "unlabelled address lost");
    wornLines[1].text = "1996年5月13日";
    auto mismatch = parse_document(wornLines, "id_card", "portrait", 600, 400);
    check(!mismatch["fields"].contains("birthDate"), "conflicting birth date accepted by fallback");
    wornLines[1].text = "1995年5月13日";
    wornLines.back().text = "440305199505132560";
    auto badNumber = parse_document(wornLines, "id_card", "portrait", 600, 400);
    check(!badNumber["fields"].contains("name"), "invalid number enabled layout fallback");
    check(!field_valid("birthDate", "1992"), "bare year treated as a birth date");
    check(!field_valid("birthDate", "2023-02-29"), "invalid calendar date accepted");
    check(!field_valid("validity", "2020-01-02"), "truncated validity accepted");
    check(!field_valid("sex", "Nationality中国"), "English annotation accepted as sex");
    check(!field_valid("address", "-ess"), "English caption accepted as an address");
    check(clean_field("firstIssueDate", "2015-05-1.8.") == "2015-05-1.8.",
          "malformed date silently truncated");
    auto splitBirth =
        parse_document({line("公民身份号码", 0, 200, 100), line("出生1989", 0, 50, 200),
                        line("9年6月01日", 180, 50, 210)},
                       "id_card", "portrait", 600, 400);
    check(splitBirth["fields"]["birthDate"]["value"] == "1989年6月01日",
          "overlapping birth date not joined");
    auto splitRange = parse_document({line("机动车驾驶证", 0, 0), line("有效期限2015", 0, 100, 200),
                                      line("-12-31至2021-12-31", 190, 100, 300)},
                                     "driver_license", "main", 600, 400);
    check(splitRange["fields"]["validity"]["value"] == "2015-12-31至2021-12-31",
          "year-only range not joined");
    auto mixed = parse_document({line("机动车驾驶证", 0, 0), line("性别", 0, 50, 60),
                                 line("女Nationality", 70, 50, 150), line("国籍", 250, 50, 60),
                                 line("Nationality中国", 310, 50, 200)},
                                "driver_license", "main", 600, 400);
    check(mixed["fields"]["sex"]["value"] == "女", "English suffix leaked into sex");
    check(mixed["fields"]["nationality"]["value"] == "中国",
          "English prefix leaked into nationality");
    auto missingSex = parse_document({line("机动车驾驶证", 0, 0), line("性别", 0, 50, 60),
                                      line("国籍", 250, 50, 60), line("中国", 320, 50, 100)},
                                     "driver_license", "main", 600, 400);
    check(!missingSex["fields"].contains("sex"), "search crossed the next field label");
    auto shortAddress = parse_document(
        {line("机动车行驶证", 0, 0), line("住址某省某市某", 0, 50, 250), line("镇", 50, 85, 25),
         line("使用性质", 0, 125, 100), line("非营运", 110, 125, 100)},
        "vehicle_license", "main", 600, 400);
    check(shortAddress["fields"]["address"]["value"] == "某省某市某镇",
          "single-character continuation lost");
    auto completeIdLines =
        std::vector<Line>{line("居民身份证", 0, 0), line("签发机关某市公安局", 0, 50),
                          line("有效期限2020.01.01-2030.01.01", 0, 100, 500)};
    auto completeId = parse_document(completeIdLines, "id_card", "emblem", 600, 400);
    check(completeId["status"] == "complete", "valid complete document rejected");
    completeIdLines.back().text = "有效期限2020";
    auto truncatedId = parse_document(completeIdLines, "id_card", "emblem", 600, 400);
    check(truncatedId["status"] == "partial", "year-only date produced complete status");
    completeIdLines.back().text = "有效期限2020.02.30-2030.01.01";
    auto invalidId = parse_document(completeIdLines, "id_card", "emblem", 600, 400);
    check(invalidId["status"] == "partial", "invalid calendar date produced complete status");
    completeIdLines.back().text = "有效期限2020.01.01-2030.01.01";
    completeIdLines[1].confidence = .6f;
    check(parse_document(completeIdLines, "id_card", "emblem", 600, 400)["status"] == "partial",
          "low confidence produced complete status");
    auto multi = parse_document({line("居民身份证", 0, 0), line("公民身份号码", 0, 200)}, "id_card",
                                "portrait", 600, 400);
    check(multi["status"] == "failed" && multi["fields"].empty(),
          "two ID faces accepted as one document");
    auto supplement = parse_document({line("机动车驾驶证副页", 0, 0), line("姓名某某", 0, 50)},
                                     "driver_license", "main", 600, 400);
    check(supplement["status"] == "failed", "supplementary page accepted as main page");
    check(clean_field("address", "焦ares某省某市甲路8号") == "某省某市甲路8号",
          "mixed caption prefix retained");
    check(clean_field("address", "某省某市A栋21A") == "某省某市A栋21A",
          "legitimate address letters removed");
    check(clean_field("vehicleClass", "c-1") == "C1", "class formatting not normalized");
    check(clean_field("vehicleClass", "CI") == "CI", "class letter guessed as digit");
    check(field_valid("nationality", "中国/CHN"), "bilingual nationality rejected");
    check(field_valid("ethnicity", "蒙古") && field_valid("ethnicity", "柯尔克孜"),
          "compound ethnicity rejected");
    check(!field_valid("ethnicity", "我汉"), "corrupted ethnicity accepted");
    std::vector<NameReading> nameReadings = {
        {"张丰", .98f, 0}, {"张丰", .99f, 1}, {"张丰", .98f, 2}};
    check(select_name_refinement("张三", .90f, nameReadings).first == "张丰",
          "independent name readings not recovered");
    check(select_name_refinement("张三", .98f, nameReadings).first.empty(),
          "small score change replaced a confident name");
    check(select_name_refinement("张三", .90f,
                                 {{"张丰", .99f, 0}, {"张丰", .99f, 0}, {"张丰", .99f, 0}})
              .first.empty(),
          "repeated resize counted as independent name evidence");
    auto conflictingNames = nameReadings;
    for (int family = 0; family < 3; ++family)
        conflictingNames.push_back({"张三", .99f, family});
    check(select_name_refinement("张三", .90f, conflictingNames).first.empty(),
          "conflicting name consensus replaced original");
    check(select_name_refinement("张三", .90f,
                                 {{"张三丰", .99f, 0}, {"张三丰", .99f, 1}, {"张三丰", .99f, 2}})
              .first.empty(),
          "name refinement added a character");
    check(clean_field("idNumber", "44030519950513256x") == "44030519950513256X",
          "ID case not normalized");
    check(clean_field("validity", "202412-25至2030-12-25") == "2024-12-25至2030-12-25",
          "compact year/month not formatted");
    check(!field_valid("validity", clean_field("validity", "202413-25至2030-12-25")),
          "invalid compact date corrected silently");
    auto alphanumericAddress =
        parse_document({line("机动车行驶证", 0, 0), line("住址某省某市甲", 0, 50, 250),
                        line("村115栋21A", 50, 85, 200)},
                       "vehicle_license", "main", 600, 400);
    check(alphanumericAddress["fields"]["address"]["value"] == "某省某市甲村115栋21A",
          "alphanumeric address continuation lost");
    auto noisyAddress =
        parse_document({line("机动车驾驶证", 0, 0), line("住址焦ares某省某市甲路8号", 0, 50, 400)},
                       "driver_license", "main", 600, 400);
    check(noisyAddress["fields"]["address"]["value"] == "某省某市甲路8号",
          "caption noise leaked into address");
    check(noisyAddress["fields"]["address"]["rawText"] == "住址焦ares某省某市甲路8号",
          "address raw text changed");
    check(std::find(noisyAddress["warnings"].begin(), noisyAddress["warnings"].end(),
                    "address:CAPTION_REMOVED") != noisyAddress["warnings"].end(),
          "caption cleanup omitted review warning");
    auto offsetLabel =
        parse_document({line("机动车行驶证", 0, 0), line("某省某市甲路", 60, 70, 200),
                        line("住", 0, 85, 25), line("村115栋21A", 60, 100, 200)},
                       "vehicle_license", "main", 600, 400);
    check(offsetLabel["fields"]["address"]["value"] == "某省某市甲路村115栋21A",
          "offset label selected the second address row");
    auto fusedDates = parse_document(
        {line("机动车行驶证", 0, 0), line("注册日期2020-01-02发2021-02-03", 0, 50, 500)},
        "vehicle_license", "main", 600, 400);
    check(fusedDates["fields"]["registerDate"]["value"] == "2020-01-02",
          "fused registration date not separated");
    check(fusedDates["fields"]["issueDate"]["value"] == "2021-02-03",
          "fused issue date not separated");
    check(fusedDates["fields"]["issueDate"]["rawText"] == "注册日期2020-01-02发2021-02-03",
          "fused date evidence lost");
    auto unlabelledSecondDate = parse_document(
        {line("机动车行驶证", 0, 0), line("注册日期2020-01-02至2021-02-03", 0, 50, 500)},
        "vehicle_license", "main", 600, 400);
    check(!unlabelledSecondDate["fields"].contains("issueDate"),
          "unlabelled second date guessed as issue date");
    auto shortPlate = parse_document({line("机动车行驶证", 0, 0), line("号码京A12345", 0, 50),
                                      line("发动机号码7654321", 0, 100)},
                                     "vehicle_license", "main", 600, 400);
    check(shortPlate["fields"]["plateNumber"]["value"] == "京A12345",
          "fragmented plate label not recovered");
    auto engineOnly =
        parse_document({line("机动车行驶证", 0, 0), line("发动机号码7654321", 0, 100)},
                       "vehicle_license", "main", 600, 400);
    check(!engineOnly["fields"].contains("plateNumber"), "engine number mistaken for plate");
    cv::Mat bgr = cv::imread(argv[2]), rgba;
    cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);
    for (int life = 0; life < 2; life++) {
        Ocr engine; engine.initialize(argv[1]);
        rejects([&] { engine.recognize(nullptr, 800, 800, 3200, "id_card", "portrait"); },
                "null pixels accepted");
        rejects([&] { engine.recognize(rgba.data, 10, 10, 40, "id_card", "portrait"); },
                "undersized image accepted");
        rejects(
            [&] {
                engine.recognize(rgba.data, rgba.cols, rgba.rows, rgba.step, "unknown", "portrait");
            },
            "unknown type accepted");
        rejects(
            [&] {
                engine.recognize(rgba.data, rgba.cols, rgba.rows, rgba.step, "driver_license",
                                 "emblem");
            },
            "unsupported side accepted");
        auto a =
            engine.recognize(rgba.data, rgba.cols, rgba.rows, rgba.step, "id_card", "portrait");
        auto b =
            engine.recognize(rgba.data, rgba.cols, rgba.rows, rgba.step, "id_card", "portrait");
        check(a == b, "repeated requests differ");
        check(json::parse(a)["fields"].contains("idNumber"),
              "real image recognition missing number");
        engine.release();
        engine.release();
        rejects([&] { engine.recognize(rgba.data, rgba.cols, rgba.rows, rgba.step,
                                      "id_card", "portrait"); }, "released engine accepted input");
        engine.initialize(argv[1]);
    }
    return failures ? 1 : 0;
}
