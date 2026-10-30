#include <cardlens_ocr/core.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    auto image = cv::imread(argv[2]);
    if (image.empty()) return 1;
    cv::Mat rgba;
    cv::cvtColor(image, rgba, cv::COLOR_BGR2RGBA);
    cardlens_ocr::Ocr ocr;
    ocr.initialize(argv[1]);
    std::cout << ocr.recognize(rgba.data, rgba.cols, rgba.rows, rgba.step, "id_card", "portrait");
    ocr.release();
}
