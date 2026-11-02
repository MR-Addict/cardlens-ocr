#include "pipeline.h"
#include "trace.h"
#include <algorithm>
#include <opencv2/imgproc.hpp>
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
}
std::vector<ImageCandidate> locate_documents(const cv::Mat &bgr, int &cardCount) {
    const int width = bgr.cols, height = bgr.rows;
    std::vector<ImageCandidate> images = {{bgr, cv::Mat::eye(3, 3, CV_64F)}};
    cv::Mat grayCard, edges;
    cv::cvtColor(bgr, grayCard, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(grayCard, grayCard, cv::Size(5, 5), 0);
    cv::Canny(grayCard, edges, 40, 120);
    cv::morphologyEx(edges, edges, cv::MORPH_CLOSE,
                     cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5)));
    std::vector<std::vector<cv::Point>> cards;
    cv::findContours(edges, cards, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    double bestArea = 0;
    cardCount = 0;
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
    return images;
}
void restore_coordinates(nlohmann::json &result, int rotation, cv::Size selected,
                         const cv::Mat &mapping, int width, int height) {
    cv::Mat turn = cv::Mat::eye(3, 3, CV_64F);
    if (rotation == 90)
        turn = (cv::Mat_<double>(3, 3) << 0, 1, 0, -1, 0, selected.height - 1, 0, 0, 1);
    else if (rotation == 180)
        turn = (cv::Mat_<double>(3, 3) << -1, 0, selected.width - 1, 0, -1, selected.height - 1, 0, 0, 1);
    else if (rotation == 270)
        turn = (cv::Mat_<double>(3, 3) << 0, -1, selected.width - 1, 1, 0, 0, 0, 0, 1);
    cv::Mat toSource = mapping * turn;
    for (auto collection : {"lines", "fields"})
        for (auto &item : result[collection]) {
            std::vector<cv::Point2f> points;
            for (const auto &point : item["polygon"])
                points.emplace_back(point[0], point[1]);
            if (!points.empty()) cv::perspectiveTransform(points, points, toSource);
            item["polygon"] = nlohmann::json::array();
            for (auto point : points)
                item["polygon"].push_back({std::clamp(point.x, 0.f, float(width - 1)),
                                           std::clamp(point.y, 0.f, float(height - 1))});
        }
    result["width"] = width; result["height"] = height; result["rotation"] = rotation;
}
}
