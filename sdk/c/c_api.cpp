#include "cardlens_ocr/c_api.h"
#include "cardlens_ocr/core.h"
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>

struct cl_engine { cardlens_ocr::Ocr ocr; std::mutex mutex; };
namespace {
char *copy(const char *text) {
    auto size = std::strlen(text) + 1;
    auto *result = static_cast<char *>(std::malloc(size));
    if (result) std::memcpy(result, text, size);
    return result;
}
int fail(char **error, const char *message, int code) {
    if (error) *error = copy(message);
    return code;
}
}
int cl_initialize(const char *models, cl_engine **engine, char **error) {
    if (error) *error = nullptr;
    if (engine) *engine = nullptr;
    if (!models || !engine) return fail(error, "INVALID_ARGUMENT", 1);
    try {
        auto value = std::make_unique<cl_engine>();
        value->ocr.initialize(models);
        *engine = value.release();
        return 0;
    } catch (const std::exception &e) { return fail(error, e.what(), 2); }
    catch (...) { return fail(error, "INITIALIZATION_FAILED", 2); }
}
int cl_recognize(cl_engine *engine, const unsigned char *rgba, size_t length,
                 int width, int height, size_t stride, const char *type,
                 const char *side, char **json, char **error) {
    if (error) *error = nullptr;
    if (json) *json = nullptr;
    if (!engine) return fail(error, "NOT_INITIALIZED", 1);
    if (!rgba || !json || !type || !side || width < 64 || height < 64 || width > 8192 ||
        height > 8192 || stride < static_cast<size_t>(width) * 4 ||
        stride > 128000000u / static_cast<size_t>(height) ||
        stride * static_cast<size_t>(height) > length ||
        static_cast<size_t>(width) * height > 24000000u)
        return fail(error, "INVALID_IMAGE", 1);
    try {
        std::lock_guard<std::mutex> lock(engine->mutex);
        auto result = engine->ocr.recognize(rgba, width, height, stride, type, side);
        *json = copy(result.c_str());
        if (!*json) return fail(error, "ALLOCATION_FAILED", 2);
        return 0;
    } catch (const std::invalid_argument &e) { return fail(error, e.what(), 1); }
    catch (const std::exception &e) { return fail(error, e.what(), 2); }
    catch (...) { return fail(error, "RECOGNITION_FAILED", 2); }
}
void cl_release(cl_engine *engine) { delete engine; }
void cl_free(char *text) { std::free(text); }
