#pragma once
#include <stddef.h>
#if defined(_WIN32)
# if defined(CARDLENS_EXPORTS)
#  define CL_API __declspec(dllexport)
# else
#  define CL_API __declspec(dllimport)
# endif
#else
# define CL_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct cl_engine cl_engine;
// 0 = success, 1 = invalid input, 2 = runtime failure.
// Output/error strings belong to the caller and must be released with cl_free.
CL_API int cl_initialize(const char *models, cl_engine **engine, char **error);
CL_API int cl_recognize(cl_engine *engine, const unsigned char *rgba, size_t length,
                       int width, int height, size_t stride, const char *type,
                       const char *side, char **json, char **error);
// Release after all calls using this handle have completed. NULL is accepted.
CL_API void cl_release(cl_engine *engine);
CL_API void cl_free(char *text);
#ifdef __cplusplus
}
#endif
