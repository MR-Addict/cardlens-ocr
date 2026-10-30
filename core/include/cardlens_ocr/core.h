#pragma once
#include <cstddef>
#include <memory>
#include <string>

namespace cardlens_ocr {
class Ocr {
  public:
    Ocr();
    ~Ocr();
    Ocr(const Ocr &) = delete;
    Ocr &operator=(const Ocr &) = delete;
    void initialize(const std::string &model_directory);
    // RGBA pixels; stride is in bytes. Returns owned UTF-8 JSON.
    std::string recognize(const unsigned char *rgba, int width, int height, std::size_t stride,
                          const std::string &type, const std::string &side);
    void release();
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
