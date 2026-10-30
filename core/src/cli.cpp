#include "cardlens_ocr/core.h"
#include "trace.h"
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
int main(int argc, char **argv) {
    if (argc != 5 && (argc != 7 || std::string(argv[5]) != "--trace-dir")) {
        std::cerr << "usage: cardlens-ocr MODEL_DIR IMAGE TYPE SIDE [--trace-dir DIRECTORY]\n";
        return 2;
    }
    try {
        std::unique_ptr<cardlens_ocr::TraceSession> trace;
        if (argc == 7)
            trace.reset(new cardlens_ocr::TraceSession(argv[6]));
        auto bgr = cv::imread(argv[2]);
        if (bgr.empty())
            throw std::runtime_error("INVALID_IMAGE");
        cv::Mat rgba;
        cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);
        cardlens_ocr::Ocr engine;
        engine.initialize(argv[1]);
        std::cout << engine.recognize(rgba.data, rgba.cols, rgba.rows, rgba.step, argv[3], argv[4])
                  << "\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
