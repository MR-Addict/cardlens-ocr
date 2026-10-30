#include "cardlens_ocr/core.h"
#include "napi/native_api.h"
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace {
struct State {
    std::mutex mutex;
    std::shared_ptr<cardlens_ocr::Ocr> engine;
    bool initializing = false;
    bool busy = false;
};
using StatePtr = std::shared_ptr<State>;
struct Work {
    napi_env env = nullptr;
    napi_async_work work = nullptr;
    napi_deferred deferred = nullptr;
    StatePtr state;
    std::shared_ptr<cardlens_ocr::Ocr> engine;
    std::string path, type, side, result, error;
    std::vector<unsigned char> pixels;
    int width = 0, height = 0;
    size_t stride = 0;
    bool initialize = false;
    bool armed = false;
    ~Work() {
        if (armed && state) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (initialize)
                state->initializing = false;
            else
                state->busy = false;
        }
    }
};
StatePtr state(napi_env env, napi_callback_info info) {
    void *p = nullptr;
    size_t argc = 0;
    napi_get_cb_info(env, info, &argc, nullptr, nullptr, &p);
    return *static_cast<StatePtr *>(p);
}
bool string_arg(napi_env env, napi_value value, std::string &out) {
    size_t length = 0;
    if (napi_get_value_string_utf8(env, value, nullptr, 0, &length) != napi_ok || length > 4096)
        return false;
    std::vector<char> data(length + 1);
    if (napi_get_value_string_utf8(env, value, data.data(), data.size(), &length) != napi_ok)
        return false;
    out.assign(data.data(), length);
    return true;
}
napi_value fail(napi_env env, const char *message) {
    napi_throw_type_error(env, nullptr, message);
    return nullptr;
}
void execute(napi_env, void *data) {
    auto *w = static_cast<Work *>(data);
    try {
        if (w->initialize) {
            auto engine = std::make_shared<cardlens_ocr::Ocr>();
            engine->initialize(w->path);
            std::lock_guard<std::mutex> lock(w->state->mutex);
            w->state->engine = std::move(engine);
        } else
            w->result = w->engine->recognize(w->pixels.data(), w->width, w->height, w->stride,
                                             w->type, w->side);
    } catch (const std::exception &e) {
        w->error = e.what();
    }
}
void complete(napi_env env, napi_status status, void *data) {
    std::unique_ptr<Work> w(static_cast<Work *>(data));

    napi_value value = nullptr;
    if (status != napi_ok || !w->error.empty()) {
        napi_value message = nullptr;
        napi_create_string_utf8(env, w->error.empty() ? "ASYNC_CANCELLED" : w->error.c_str(),
                                NAPI_AUTO_LENGTH, &message);
        napi_create_error(env, nullptr, message, &value);
        napi_reject_deferred(env, w->deferred, value);
    } else {
        if (w->initialize)
            napi_get_undefined(env, &value);
        else
            napi_create_string_utf8(env, w->result.c_str(), w->result.size(), &value);
        napi_resolve_deferred(env, w->deferred, value);
    }
    napi_delete_async_work(env, w->work);
}
napi_value queue(napi_env env, std::unique_ptr<Work> w) {
    napi_value promise = nullptr, name = nullptr;
    if (napi_create_promise(env, &w->deferred, &promise) != napi_ok)
        return fail(env, "Cannot create promise");
    napi_create_string_utf8(env, "cardlens-ocr", NAPI_AUTO_LENGTH, &name);
    if (napi_create_async_work(env, nullptr, name, execute, complete, w.get(), &w->work) != napi_ok)
        return fail(env, "Cannot create worker");
    if (napi_queue_async_work(env, w->work) != napi_ok) {
        napi_delete_async_work(env, w->work);
        return fail(env, "Cannot queue worker");
    }
    w.release();
    return promise;
}
napi_value Initialize(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc != 1)
        return fail(env, "initialize requires a model directory");
    auto w = std::make_unique<Work>();
    w->env = env;
    w->state = state(env, info);
    w->initialize = true;
    if (!string_arg(env, args[0], w->path))
        return fail(env, "Invalid model directory");
    {
        std::lock_guard<std::mutex> lock(w->state->mutex);
        if (w->state->engine || w->state->initializing)
            return fail(env, "ALREADY_INITIALIZED");
        w->state->initializing = true;
        w->armed = true;
    }
    return queue(env, std::move(w));
}
napi_value Recognize(napi_env env, napi_callback_info info) {
    size_t argc = 7;
    napi_value args[7];
    if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc != 6)
        return fail(env, "recognize requires RGBA, width, height, stride, type, side");
    auto w = std::make_unique<Work>();
    w->env = env;
    w->state = state(env, info);
    void *bytes = nullptr;
    size_t length = 0;
    int32_t stride = 0;
    if (napi_get_arraybuffer_info(env, args[0], &bytes, &length) != napi_ok ||
        napi_get_value_int32(env, args[1], &w->width) != napi_ok ||
        napi_get_value_int32(env, args[2], &w->height) != napi_ok ||
        napi_get_value_int32(env, args[3], &stride) != napi_ok ||
        !string_arg(env, args[4], w->type) || !string_arg(env, args[5], w->side))
        return fail(env, "Invalid recognize arguments");
    if (w->width < 64 || w->height < 64 || w->width > 8192 || w->height > 8192 ||
        stride < w->width * 4 || static_cast<size_t>(stride) * w->height > length ||
        static_cast<size_t>(stride) * w->height > 128000000 ||
        static_cast<size_t>(w->width) * w->height > 24000000)
        return fail(env, "INVALID_IMAGE");
    w->stride = stride;
    {
        std::lock_guard<std::mutex> lock(w->state->mutex);
        if (w->state->busy)
            return fail(env, "BUSY");
        w->engine = w->state->engine;
    }
    if (!w->engine)
        return fail(env, "NOT_INITIALIZED");
    // Own a snapshot: the caller can release or mutate its ArrayBuffer after return.
    try {
        w->pixels.assign(static_cast<unsigned char *>(bytes),
                         static_cast<unsigned char *>(bytes) +
                             static_cast<size_t>(stride) * w->height);
    } catch (const std::exception &) {
        return fail(env, "IMAGE_ALLOCATION_FAILED");
    }
    {
        std::lock_guard<std::mutex> lock(w->state->mutex);
        w->state->busy = true;
        w->armed = true;
    }
    return queue(env, std::move(w));
}
napi_value Release(napi_env env, napi_callback_info info) {
    auto s = state(env, info);
    std::lock_guard<std::mutex> lock(s->mutex);
    if (s->initializing)
        return fail(env, "INITIALIZATION_IN_PROGRESS");
    s->engine.reset();
    napi_value value = nullptr;
    napi_get_undefined(env, &value);
    return value;
}
void finalize(void *data) { delete static_cast<StatePtr *>(data); }
napi_value Init(napi_env env, napi_value exports) {
    auto holder = std::make_unique<StatePtr>(std::make_shared<State>());
    if (napi_add_env_cleanup_hook(env, finalize, holder.get()) != napi_ok)
        return fail(env, "Cannot initialize addon state");
    auto *data = holder.release();
    napi_property_descriptor properties[] = {
        {"initialize", nullptr, Initialize, nullptr, nullptr, nullptr, napi_default, data},
        {"recognize", nullptr, Recognize, nullptr, nullptr, nullptr, napi_default, data},
        {"release", nullptr, Release, nullptr, nullptr, nullptr, napi_default, data}};
    if (napi_define_properties(env, exports, 3, properties) != napi_ok)
        return fail(env, "Cannot export SDK");
    return exports;
}
napi_module module = {1, 0, nullptr, Init, "cardlens_ocr", nullptr, {0}};
__attribute__((constructor)) void Register() { napi_module_register(&module); }
} // namespace
