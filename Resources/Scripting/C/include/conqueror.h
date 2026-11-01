/* Conqueror Engine C script API (header only, C11/C23; the engine library is never linked).
 * ------------------------------------------------------------------------------------------
 *   #include <conqueror.h>
 *   #include "nativebridge.h"          // generated: every engine function as a macro
 *
 *   typedef struct Spinner {
 *       CQ_OBJECT;                     // must be the first member
 *       float speed;
 *       int ticks;
 *       char label[64];
 *   } Spinner;
 *
 *   static void Spinner_Update(Spinner* self, const cq_value* args, int argc)
 *   {
 *       float dt = cq_arg_float(args, argc, 0);
 *       cq_value r = GetRotation();
 *       SetRotation(r.x, r.y + self->speed * dt, r.z);
 *   }
 *
 *   CQ_FIELDS(Spinner, CQ_FLOAT(Spinner, speed, 90), CQ_INT(Spinner, ticks, 0), CQ_STRING(Spinner, label, "spin"))
 *   CQ_METHODS(Spinner, CQ_ON(Spinner, Update))
 *   CQ_CLASS(Spinner)
 *
 * The boundary with the engine is the same plain C ABI that C++, Rust and Zig scripts use.
 */
#ifndef CONQUEROR_H
#define CONQUEROR_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define CQ_EXPORT __declspec(dllexport)
#else
#define CQ_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── Values exchanged with the engine (64 bytes, identical to the engine's CQNativeValue) ── */
enum { CQ_NULL = 0, CQ_BOOL = 1, CQ_INT = 2, CQ_FLOAT = 3, CQ_STRING = 4, CQ_VECTOR = 5, CQ_JSON = 6 };

typedef struct cq_value
{
    int32_t type;
    int32_t dim;
    int64_t i;
    double x, y, z, w;
    const char* str;      /* strings returned by the engine stay valid until the next engine call */
    int32_t strlen;
    int32_t pad;
} cq_value;

typedef struct cq_host_api
{
    int version;
    int (*find_native)(const char* name);
    int (*call_native)(int index, int argc, const cq_value* args, cq_value* result);
    void (*log)(int level, const char* message);
} cq_host_api;

extern const cq_host_api* cq__host;

static inline cq_value cq_null(void) { cq_value v; memset(&v, 0, sizeof v); return v; }
static inline cq_value cq_bool(bool b) { cq_value v = cq_null(); v.type = CQ_BOOL; v.i = b ? 1 : 0; return v; }
static inline cq_value cq_int(int64_t i) { cq_value v = cq_null(); v.type = CQ_INT; v.i = i; return v; }
static inline cq_value cq_num(double d) { cq_value v = cq_null(); v.type = CQ_FLOAT; v.x = d; return v; }
static inline cq_value cq_str(const char* s) { cq_value v = cq_null(); if (!s) return v; v.type = CQ_STRING; v.str = s; v.strlen = (int32_t)strlen(s); return v; }
static inline cq_value cq_vec2(double x, double y) { cq_value v = cq_null(); v.type = CQ_VECTOR; v.dim = 2; v.x = x; v.y = y; return v; }
static inline cq_value cq_vec3(double x, double y, double z) { cq_value v = cq_vec2(x, y); v.dim = 3; v.z = z; return v; }
static inline cq_value cq_vec4(double x, double y, double z, double w) { cq_value v = cq_vec3(x, y, z); v.dim = 4; v.w = w; return v; }
static inline cq_value cq_json(const char* json) { cq_value v = cq_str(json); v.type = CQ_JSON; return v; }

static inline double cq_to_num(cq_value v)
{
    switch (v.type)
    {
        case CQ_BOOL: case CQ_INT: return (double)v.i;
        case CQ_FLOAT: case CQ_VECTOR: return v.x;
        case CQ_STRING: return v.str ? atof(v.str) : 0.0;
        default: return 0.0;
    }
}
static inline int64_t cq_to_int(cq_value v) { return (v.type == CQ_INT || v.type == CQ_BOOL) ? v.i : (int64_t)cq_to_num(v); }
static inline bool cq_to_bool(cq_value v) { return v.type == CQ_STRING ? v.strlen > 0 : (v.type == CQ_FLOAT ? v.x != 0.0 : v.i != 0); }
static inline const char* cq_to_str(cq_value v) { return (v.type == CQ_STRING || v.type == CQ_JSON) && v.str ? v.str : ""; }
static inline bool cq_is_null(cq_value v) { return v.type == CQ_NULL; }

/* Method arguments (UI events, collisions ...) */
static inline cq_value cq_arg(const cq_value* args, int argc, int i) { return i < argc ? args[i] : cq_null(); }
static inline float cq_arg_float(const cq_value* args, int argc, int i) { return (float)cq_to_num(cq_arg(args, argc, i)); }
static inline int64_t cq_arg_int(const cq_value* args, int argc, int i) { return cq_to_int(cq_arg(args, argc, i)); }
static inline const char* cq_arg_str(const cq_value* args, int argc, int i) { return cq_to_str(cq_arg(args, argc, i)); }

/* Automatic conversion of C values for engine calls: SetRotation(r.x, r.y + 1.0f, r.z) */
static inline cq_value cq__id(cq_value v) { return v; }
static inline cq_value cq__i(long long i) { return cq_int((int64_t)i); }
static inline cq_value cq__u(unsigned long long i) { return cq_int((int64_t)i); }
static inline cq_value cq__f(double d) { return cq_num(d); }
static inline cq_value cq__s(const char* s) { return cq_str(s); }
static inline cq_value cq__b(bool b) { return cq_bool(b); }
#define CQV(x) _Generic((x), \
    cq_value: cq__id, bool: cq__b, char: cq__i, signed char: cq__i, unsigned char: cq__u, short: cq__i, unsigned short: cq__u, \
    int: cq__i, unsigned int: cq__u, long: cq__i, unsigned long: cq__u, long long: cq__i, unsigned long long: cq__u, \
    float: cq__f, double: cq__f, char*: cq__s, const char*: cq__s)(x)

/* Engine call by cached index (the generated macros use this) */
static inline cq_value cq_call_index(int* cache, const char* name, int argc, const cq_value* args)
{
    cq_value result = cq_null();
    if (!cq__host) return result;
    if (*cache == -2) *cache = cq__host->find_native(name);
    if (*cache < 0) return result;
    cq__host->call_native(*cache, argc, args, &result);
    return result;
}

/* Engine call by name: cq_call("GetPosition", 0, NULL) */
static inline cq_value cq_call(const char* name, int argc, const cq_value* args)
{
    int index = -2;
    return cq_call_index(&index, name, argc, args);
}

static inline void cq__logv(int level, const char* fmt, va_list ap)
{
    char buf[2048];
    vsnprintf(buf, sizeof buf, fmt, ap);
    if (cq__host) cq__host->log(level, buf);
}
static inline void cq_log(const char* fmt, ...) { va_list ap; va_start(ap, fmt); cq__logv(0, fmt, ap); va_end(ap); }
static inline void cq_warn(const char* fmt, ...) { va_list ap; va_start(ap, fmt); cq__logv(1, fmt, ap); va_end(ap); }
static inline void cq_error(const char* fmt, ...) { va_list ap; va_start(ap, fmt); cq__logv(2, fmt, ap); va_end(ap); }

/* ── Argument list helpers for the generated variadic macros (0..16 arguments) ── */
#define CQ__ARGC(...) CQ__ARGC_(__VA_OPT__(__VA_ARGS__,) 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define CQ__ARGC_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N
#define CQ__CAT(a, b) CQ__CAT_(a, b)
#define CQ__CAT_(a, b) a##b
#define CQ__MAP(...) CQ__CAT(CQ__MAP, CQ__ARGC(__VA_ARGS__))(__VA_ARGS__)
#define CQ__MAP0()
#define CQ__MAP1(a) , CQV(a)
#define CQ__MAP2(a, ...) , CQV(a) CQ__MAP1(__VA_ARGS__)
#define CQ__MAP3(a, ...) , CQV(a) CQ__MAP2(__VA_ARGS__)
#define CQ__MAP4(a, ...) , CQV(a) CQ__MAP3(__VA_ARGS__)
#define CQ__MAP5(a, ...) , CQV(a) CQ__MAP4(__VA_ARGS__)
#define CQ__MAP6(a, ...) , CQV(a) CQ__MAP5(__VA_ARGS__)
#define CQ__MAP7(a, ...) , CQV(a) CQ__MAP6(__VA_ARGS__)
#define CQ__MAP8(a, ...) , CQV(a) CQ__MAP7(__VA_ARGS__)
#define CQ__MAP9(a, ...) , CQV(a) CQ__MAP8(__VA_ARGS__)
#define CQ__MAP10(a, ...) , CQV(a) CQ__MAP9(__VA_ARGS__)
#define CQ__MAP11(a, ...) , CQV(a) CQ__MAP10(__VA_ARGS__)
#define CQ__MAP12(a, ...) , CQV(a) CQ__MAP11(__VA_ARGS__)
#define CQ__MAP13(a, ...) , CQV(a) CQ__MAP12(__VA_ARGS__)
#define CQ__MAP14(a, ...) , CQV(a) CQ__MAP13(__VA_ARGS__)
#define CQ__MAP15(a, ...) , CQV(a) CQ__MAP14(__VA_ARGS__)
#define CQ__MAP16(a, ...) , CQV(a) CQ__MAP15(__VA_ARGS__)
/* One leading null keeps the array non-empty for zero-argument calls */
#define CQ__CALL(cache, name, ...) cq_call_index(cache, name, CQ__ARGC(__VA_ARGS__), ((cq_value[]){ cq_null() CQ__MAP(__VA_ARGS__) }) + 1)

/* ── Script classes ── */
typedef struct cq_object
{
    uint32_t entity;
} cq_object;
#define CQ_OBJECT cq_object cq_base

typedef enum cq_field_type { CQ_FIELD_BOOL, CQ_FIELD_INT, CQ_FIELD_FLOAT, CQ_FIELD_DOUBLE, CQ_FIELD_STRING } cq_field_type;

typedef struct cq_field_desc
{
    const char* name;
    cq_field_type type;
    size_t offset;
    size_t size;
    double num_default;
    const char* str_default;
    const char* attributes;
} cq_field_desc;

typedef void (*cq_method_fn)(void* self, const cq_value* args, int argc);

typedef struct cq_method_desc
{
    const char* name;
    cq_method_fn fn;
} cq_method_desc;

typedef struct cq_class_desc
{
    const char* name;
    size_t size;
    const cq_field_desc* fields;
    int field_count;
    const cq_method_desc* methods;
    int method_count;
} cq_class_desc;

#define CQ__MEMBER_SIZE(T, f) sizeof(((T*)0)->f)
#define CQ_BOOL_FIELD(T, f, d) { #f, CQ_FIELD_BOOL, offsetof(T, f), CQ__MEMBER_SIZE(T, f), (double)(d), NULL, NULL }
#define CQ_INT(T, f, d) { #f, CQ_FIELD_INT, offsetof(T, f), CQ__MEMBER_SIZE(T, f), (double)(d), NULL, NULL }
#define CQ_FLOAT(T, f, d) { #f, CQ_FIELD_FLOAT, offsetof(T, f), CQ__MEMBER_SIZE(T, f), (double)(d), NULL, NULL }
#define CQ_DOUBLE(T, f, d) { #f, CQ_FIELD_DOUBLE, offsetof(T, f), CQ__MEMBER_SIZE(T, f), (double)(d), NULL, NULL }
#define CQ_STRING(T, f, d) { #f, CQ_FIELD_STRING, offsetof(T, f), CQ__MEMBER_SIZE(T, f), 0.0, (d), NULL }
/* Slider in the Inspector */
#define CQ_RANGE(T, f, d, lo, hi) { #f, CQ_FIELD_FLOAT, offsetof(T, f), CQ__MEMBER_SIZE(T, f), (double)(d), NULL, "Range(" #lo ", " #hi ")" }

#define CQ_FIELDS(T, ...) static const cq_field_desc cq__fields_##T[] = { __VA_ARGS__ };
#define CQ_NO_FIELDS(T) static const cq_field_desc cq__fields_##T[] = { { NULL, CQ_FIELD_INT, 0, 0, 0.0, NULL, NULL } };
/* Engine callback / UI event handler: void T_Name(T* self, const cq_value* args, int argc) */
#define CQ_ON(T, m) { #m, (cq_method_fn)T##_##m }
#define CQ_METHODS(T, ...) static const cq_method_desc cq__methods_##T[] = { __VA_ARGS__ };
#define CQ_NO_METHODS(T) static const cq_method_desc cq__methods_##T[] = { { NULL, NULL } };
#define CQ_CLASS(T) \
    CQ_EXPORT const cq_class_desc cq_class_##T = { #T, sizeof(T), cq__fields_##T, \
        (int)(sizeof(cq__fields_##T) / sizeof(cq__fields_##T[0])), cq__methods_##T, \
        (int)(sizeof(cq__methods_##T) / sizeof(cq__methods_##T[0])) };

#ifdef __cplusplus
}
#endif

/* ── Plugin entry points: compiled once, in the file the engine generates ── */
#ifdef CQ_PLUGIN_IMPLEMENTATION
const cq_host_api* cq__host = NULL;
extern const cq_class_desc* const cq__classes[];
extern const int cq__class_count;

static char cq__error[1024];
static char* cq__text = NULL;
static size_t cq__text_len = 0, cq__text_cap = 0;

typedef struct cq__instance
{
    const cq_class_desc* cls;
    void* obj;
} cq__instance;

static void cq__append(const char* s)
{
    size_t n = strlen(s);
    if (cq__text_len + n + 1 > cq__text_cap)
    {
        size_t cap = cq__text_cap ? cq__text_cap * 2 : 1024;
        while (cap < cq__text_len + n + 1) cap *= 2;
        cq__text = (char*)realloc(cq__text, cap);
        cq__text_cap = cap;
    }
    memcpy(cq__text + cq__text_len, s, n + 1);
    cq__text_len += n;
}

static void cq__append_json_string(const char* s)
{
    cq__append("\"");
    for (; s && *s; ++s)
    {
        char b[8];
        if (*s == '"' || *s == '\\') { b[0] = '\\'; b[1] = *s; b[2] = 0; }
        else if ((unsigned char)*s < 0x20) snprintf(b, sizeof b, "\\u%04x", (unsigned char)*s);
        else { b[0] = *s; b[1] = 0; }
        cq__append(b);
    }
    cq__append("\"");
}

static bool cq__field_valid(const cq_field_desc* f) { return f->name != NULL; }

static const char* cq__type_name(cq_field_type t)
{
    switch (t)
    {
        case CQ_FIELD_BOOL: return "bool";
        case CQ_FIELD_INT: return "int";
        case CQ_FIELD_FLOAT: return "float";
        case CQ_FIELD_DOUBLE: return "double";
        default: return "string";
    }
}

static void cq__write_number(void* p, const cq_field_desc* f, double d)
{
    switch (f->type)
    {
        case CQ_FIELD_BOOL: if (f->size == sizeof(bool)) *(bool*)p = d != 0.0; else *(int*)p = d != 0.0; break;
        case CQ_FIELD_INT:
            if (f->size == 8) *(int64_t*)p = (int64_t)d;
            else if (f->size == 2) *(int16_t*)p = (int16_t)d;
            else if (f->size == 1) *(int8_t*)p = (int8_t)d;
            else *(int32_t*)p = (int32_t)d;
            break;
        case CQ_FIELD_FLOAT: if (f->size == 8) *(double*)p = d; else *(float*)p = (float)d; break;
        case CQ_FIELD_DOUBLE: *(double*)p = d; break;
        default: break;
    }
}

static double cq__read_number(const void* p, const cq_field_desc* f)
{
    switch (f->type)
    {
        case CQ_FIELD_BOOL: return f->size == sizeof(bool) ? (double)*(const bool*)p : (double)(*(const int*)p != 0);
        case CQ_FIELD_INT:
            if (f->size == 8) return (double)*(const int64_t*)p;
            if (f->size == 2) return (double)*(const int16_t*)p;
            if (f->size == 1) return (double)*(const int8_t*)p;
            return (double)*(const int32_t*)p;
        case CQ_FIELD_FLOAT: return f->size == 8 ? *(const double*)p : (double)*(const float*)p;
        case CQ_FIELD_DOUBLE: return *(const double*)p;
        default: return 0.0;
    }
}

static void cq__write_string(char* dst, size_t size, const char* src, size_t len)
{
    if (!size) return;
    if (len >= size) len = size - 1;
    memcpy(dst, src ? src : "", len);
    dst[len] = 0;
}

static const cq_class_desc* cq__find_class(const char* name)
{
    for (int i = 0; i < cq__class_count; ++i)
        if (name && strcmp(cq__classes[i]->name, name) == 0)
            return cq__classes[i];
    return NULL;
}

CQ_EXPORT int cq_plugin_version(void) { return 1; }
CQ_EXPORT void cq_plugin_init(const cq_host_api* api) { cq__host = api; }

CQ_EXPORT const char* cq_plugin_describe(void)
{
    cq__text_len = 0;
    cq__append("[");
    for (int c = 0; c < cq__class_count; ++c)
    {
        const cq_class_desc* cls = cq__classes[c];
        if (c) cq__append(",");
        cq__append("{\"name\":");
        cq__append_json_string(cls->name);
        cq__append(",\"fields\":[");
        int written = 0;
        for (int i = 0; i < cls->field_count; ++i)
        {
            const cq_field_desc* f = &cls->fields[i];
            if (!cq__field_valid(f)) continue;
            if (written++) cq__append(",");
            cq__append("{\"name\":");
            cq__append_json_string(f->name);
            cq__append(",\"type\":\"");
            cq__append(cq__type_name(f->type));
            cq__append("\",\"default\":");
            if (f->type == CQ_FIELD_STRING) cq__append_json_string(f->str_default ? f->str_default : "");
            else if (f->type == CQ_FIELD_BOOL) cq__append(f->num_default != 0.0 ? "true" : "false");
            else
            {
                char b[64];
                if (f->type == CQ_FIELD_INT) snprintf(b, sizeof b, "%lld", (long long)f->num_default);
                else snprintf(b, sizeof b, "%.17g", f->num_default);
                cq__append(b);
            }
            cq__append(",\"attributes\":[");
            if (f->attributes) cq__append_json_string(f->attributes);
            cq__append("]}");
        }
        cq__append("]}");
    }
    cq__append("]");
    return cq__text;
}

CQ_EXPORT void* cq_create(const char* className, uint32_t entity)
{
    const cq_class_desc* cls = cq__find_class(className);
    if (!cls)
    {
        snprintf(cq__error, sizeof cq__error, "C script class '%s' not found (did you add CQ_CLASS(%s)?)", className ? className : "", className ? className : "");
        return NULL;
    }
    void* obj = calloc(1, cls->size < sizeof(cq_object) ? sizeof(cq_object) : cls->size);
    if (!obj) { snprintf(cq__error, sizeof cq__error, "out of memory"); return NULL; }
    ((cq_object*)obj)->entity = entity;
    for (int i = 0; i < cls->field_count; ++i)
    {
        const cq_field_desc* f = &cls->fields[i];
        if (!cq__field_valid(f)) continue;
        char* p = (char*)obj + f->offset;
        if (f->type == CQ_FIELD_STRING) cq__write_string(p, f->size, f->str_default, f->str_default ? strlen(f->str_default) : 0);
        else cq__write_number(p, f, f->num_default);
    }
    cq__instance* inst = (cq__instance*)malloc(sizeof(cq__instance));
    inst->cls = cls;
    inst->obj = obj;
    return inst;
}

CQ_EXPORT void cq_destroy(void* handle)
{
    cq__instance* inst = (cq__instance*)handle;
    if (!inst) return;
    free(inst->obj);
    free(inst);
}

static cq_method_fn cq__find_method(const cq__instance* inst, const char* name)
{
    for (int i = 0; i < inst->cls->method_count; ++i)
        if (inst->cls->methods[i].name && strcmp(inst->cls->methods[i].name, name) == 0)
            return inst->cls->methods[i].fn;
    return NULL;
}

CQ_EXPORT int cq_has_method(void* handle, const char* name)
{
    cq__instance* inst = (cq__instance*)handle;
    return inst && name && cq__find_method(inst, name) ? 1 : 0;
}

CQ_EXPORT int cq_invoke(void* handle, const char* name, const cq_value* args, int argc)
{
    cq__instance* inst = (cq__instance*)handle;
    if (!inst || !name) return 1;
    cq_method_fn fn = cq__find_method(inst, name);
    if (fn) fn(inst->obj, args, argc);
    return 1;
}

CQ_EXPORT int cq_set_field(void* handle, const char* name, const cq_value* value)
{
    cq__instance* inst = (cq__instance*)handle;
    if (!inst || !name || !value) return 0;
    for (int i = 0; i < inst->cls->field_count; ++i)
    {
        const cq_field_desc* f = &inst->cls->fields[i];
        if (!cq__field_valid(f) || strcmp(f->name, name) != 0) continue;
        char* p = (char*)inst->obj + f->offset;
        if (f->type == CQ_FIELD_STRING)
        {
            if (value->type == CQ_STRING) cq__write_string(p, f->size, value->str, (size_t)value->strlen);
            else { char b[64]; snprintf(b, sizeof b, "%g", cq_to_num(*value)); cq__write_string(p, f->size, b, strlen(b)); }
        }
        else
            cq__write_number(p, f, cq_to_num(*value));
        return 1;
    }
    return 0;
}

CQ_EXPORT int cq_get_field(void* handle, const char* name, cq_value* out, const char** typeName)
{
    cq__instance* inst = (cq__instance*)handle;
    if (!inst || !name || !out) return 0;
    for (int i = 0; i < inst->cls->field_count; ++i)
    {
        const cq_field_desc* f = &inst->cls->fields[i];
        if (!cq__field_valid(f) || strcmp(f->name, name) != 0) continue;
        const char* p = (const char*)inst->obj + f->offset;
        switch (f->type)
        {
            case CQ_FIELD_STRING: *out = cq_str(p); break;
            case CQ_FIELD_BOOL: *out = cq_bool(cq__read_number(p, f) != 0.0); break;
            case CQ_FIELD_INT: *out = cq_int((int64_t)cq__read_number(p, f)); break;
            default: *out = cq_num(cq__read_number(p, f)); break;
        }
        if (typeName) *typeName = cq__type_name(f->type);
        return 1;
    }
    return 0;
}

CQ_EXPORT const char* cq_last_error(void) { return cq__error; }
#endif /* CQ_PLUGIN_IMPLEMENTATION */

#endif /* CONQUEROR_H */
