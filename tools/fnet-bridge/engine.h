// Wire buffer and deep-copy engine. Included after gen_common.inc.
// Struct layouts are identical on arm64 and x86_64 (LP64, pack 8), so a struct is
// shipped as its raw bytes followed by the data behind each of its pointers.
#pragma once
#include <cstdio>
#include <mutex>

namespace fb {

struct Buf {
    std::vector<uint8_t> d;
    size_t pos = 0;
    bool bad = false;

    void raw(const void *p, size_t n) {
        const uint8_t *b = (const uint8_t *)p;
        d.insert(d.end(), b, b + n);
    }
    void u8(uint8_t v) { d.push_back(v); }
    void u32(uint32_t v) { raw(&v, 4); }
    void u64(uint64_t v) { raw(&v, 8); }
    void i64(int64_t v) { raw(&v, 8); }
    void str(const char *s) {
        if (!s) { u8(0); return; }
        u8(1);
        size_t n = strlen(s);
        u32((uint32_t)n);
        raw(s, n);
    }
    bool get(void *p, size_t n) {
        if (pos + n > d.size()) { bad = true; memset(p, 0, n); return false; }
        memcpy(p, d.data() + pos, n);
        pos += n;
        return true;
    }
    uint8_t r8() { uint8_t v = 0; get(&v, 1); return v; }
    uint32_t r32() { uint32_t v = 0; get(&v, 4); return v; }
    uint64_t r64() { uint64_t v = 0; get(&v, 8); return v; }
    int64_t ri64() { int64_t v = 0; get(&v, 8); return v; }
    char *rstr() {
        if (!r8()) return nullptr;
        uint32_t n = r32();
        if (bad || pos + n > d.size()) { bad = true; return nullptr; }
        char *p = (char *)malloc((size_t)n + 1);
        memcpy(p, d.data() + pos, n);
        p[n] = 0;
        pos += n;
        return p;
    }
};

// Hooks: the shim registers callbacks while encoding, the helper builds trampolines
// while decoding.
inline uint64_t (*hook_enc_cb)(int kind, void *fn, void *data) = nullptr;
inline void (*hook_dec_cb)(int kind, uint64_t id, void **fn, void **data) = nullptr;

void encode_struct(int ty, const void *p, Buf &b);
void encode_fields(int ty, const uint8_t *base, Buf &b);
void decode_fields(int ty, uint8_t *base, Buf &b);
void free_fields(int ty, uint8_t *base);

inline int32_t rd_i32(const uint8_t *base, uint32_t off) {
    int32_t v;
    memcpy(&v, base + off, 4);
    return v;
}
inline int rd_cnt(const uint8_t *base, const Field &f) {
    int32_t v = rd_i32(base, f.cnt_off);
    return v < 0 ? 0 : v;
}
template <class T> inline T rd_ptr(const uint8_t *base, uint32_t off) {
    T v;
    memcpy(&v, base + off, sizeof(T));
    return v;
}
template <class T> inline void wr_ptr(uint8_t *base, uint32_t off, T v) {
    memcpy(base + off, &v, sizeof(T));
}

// ---- void* payloads whose type depends on a discriminator
inline void enc_vd(int map, int tv, const void *p, Buf &b) {
    if (tv < 0 || tv >= VD_N[map]) return;
    const VdEntry &e = VD[map][tv];
    if (e.vk == 1) b.str((const char *)p);
    else if (e.vk == 2) {
        if (!p) b.u8(0);
        else { b.u8(1); encode_struct(e.type, p, b); }
    }
}
inline void *dec_vd(int map, int tv, Buf &b);
inline void free_vd(int map, int tv, void *p);

// ---- top level helpers
inline void enc_struct_ptr(Buf &b, int ty, const void *p) {
    if (!p) { b.u8(0); return; }
    b.u8(1);
    encode_struct(ty, p, b);
}
inline void *dec_struct_alloc(int ty, Buf &b) {
    uint8_t *p = (uint8_t *)calloc(1, TYPES[ty].size);
    b.get(p, TYPES[ty].size);
    decode_fields(ty, p, b);
    return p;
}
inline void *dec_struct_ptr(Buf &b, int ty) {
    if (!b.r8()) return nullptr;
    return dec_struct_alloc(ty, b);
}
inline void free_struct_ptr(int ty, void *p) {
    if (!p) return;
    free_fields(ty, (uint8_t *)p);
    free(p);
}
inline void enc_struct_array(Buf &b, int ty, const void *p, int n) {
    if (!p || n <= 0) { b.u8(0); b.i64(0); return; }
    b.u8(1);
    b.i64(n);
    for (int i = 0; i < n; i++) encode_struct(ty, (const uint8_t *)p + (size_t)i * TYPES[ty].size, b);
}
inline void *dec_struct_array(Buf &b, int ty, int64_t *n) {
    bool present = b.r8();
    *n = b.ri64();
    if (!present || *n <= 0 || b.bad) { *n = 0; return nullptr; }
    uint32_t sz = TYPES[ty].size;
    uint8_t *a = (uint8_t *)calloc((size_t)*n, sz);
    for (int64_t i = 0; i < *n && !b.bad; i++) {
        b.get(a + i * sz, sz);
        decode_fields(ty, a + i * sz, b);
    }
    return a;
}
inline void free_struct_array(int ty, void *a, int n) {
    if (!a) return;
    uint32_t sz = TYPES[ty].size;
    for (int i = 0; i < n; i++) free_fields(ty, (uint8_t *)a + (size_t)i * sz);
    free(a);
}
inline void enc_strarr(Buf &b, const char **a, int n) {
    if (!a || n <= 0) { b.u8(0); b.i64(0); return; }
    b.u8(1);
    b.i64(n);
    for (int i = 0; i < n; i++) b.str(a[i]);
}
inline char **dec_strarr(Buf &b, int64_t *n) {
    bool present = b.r8();
    *n = b.ri64();
    if (!present || *n <= 0 || b.bad) { *n = 0; return nullptr; }
    char **a = (char **)calloc((size_t)*n, sizeof(char *));
    for (int64_t i = 0; i < *n && !b.bad; i++) a[i] = b.rstr();
    return a;
}
inline void free_strarr(char **a, int n) {
    if (!a) return;
    for (int i = 0; i < n; i++) free(a[i]);
    free(a);
}

// ---- implementation
inline void encode_struct(int ty, const void *p, Buf &b) {
    b.raw(p, TYPES[ty].size);
    encode_fields(ty, (const uint8_t *)p, b);
}

inline void encode_fields(int ty, const uint8_t *base, Buf &b) {
    const TypeDesc &t = TYPES[ty];
    for (int i = 0; i < t.nf; i++) {
        const Field &f = t.f[i];
        switch (f.kind) {
        case FK_STR:
            b.str(rd_ptr<const char *>(base, f.off));
            break;
        case FK_STRARR: {
            const char **a = rd_ptr<const char **>(base, f.off);
            int n = rd_cnt(base, f);
            if (!a) { b.u8(0); break; }
            b.u8(1);
            for (int k = 0; k < n; k++) b.str(a[k]);
            break;
        }
        case FK_DBLARR: case FK_INTARR: case FK_BYTES: {
            const void *a = rd_ptr<const void *>(base, f.off);
            int n = rd_cnt(base, f);
            if (!a) { b.u8(0); break; }
            b.u8(1);
            size_t es = f.kind == FK_DBLARR ? 8 : f.kind == FK_INTARR ? 4 : 1;
            b.raw(a, es * (size_t)n);
            break;
        }
        case FK_ARR: {
            const uint8_t *a = rd_ptr<const uint8_t *>(base, f.off);
            int n = rd_cnt(base, f);
            if (!a) { b.u8(0); break; }
            b.u8(1);
            for (int k = 0; k < n; k++) encode_struct(f.type, a + (size_t)k * f.elem, b);
            break;
        }
        case FK_INLINE:
            encode_fields(f.type, base + f.off, b);
            break;
        case FK_VOIDDATA:
            enc_vd(f.aux, rd_i32(base, f.aux_off), rd_ptr<const void *>(base, f.off), b);
            break;
        case FK_VOIDARR: {
            const void **a = rd_ptr<const void **>(base, f.off);
            int n = rd_cnt(base, f);
            if (!a) { b.u8(0); break; }
            b.u8(1);
            int tv = rd_i32(base, f.aux_off);
            for (int k = 0; k < n; k++) enc_vd(f.aux, tv, a[k], b);
            break;
        }
        case FK_CB: {
            void *fn = rd_ptr<void *>(base, f.off);
            void *dt = rd_ptr<void *>(base, f.aux_off);
            b.u64(fn && hook_enc_cb ? hook_enc_cb(f.aux, fn, dt) : 0);
            break;
        }
        }
    }
}

inline void *dec_vd(int map, int tv, Buf &b) {
    if (tv < 0 || tv >= VD_N[map]) return nullptr;
    const VdEntry &e = VD[map][tv];
    if (e.vk == 1) return b.rstr();
    if (e.vk == 2) return dec_struct_ptr(b, e.type);
    return nullptr;
}
inline void free_vd(int map, int tv, void *p) {
    if (!p || tv < 0 || tv >= VD_N[map]) return;
    const VdEntry &e = VD[map][tv];
    if (e.vk == 1) free(p);
    else if (e.vk == 2) free_struct_ptr(e.type, p);
}

inline void decode_fields(int ty, uint8_t *base, Buf &b) {
    const TypeDesc &t = TYPES[ty];
    for (int i = 0; i < t.nf && !b.bad; i++) {
        const Field &f = t.f[i];
        switch (f.kind) {
        case FK_STR:
            wr_ptr<char *>(base, f.off, b.rstr());
            break;
        case FK_STRARR: {
            int n = rd_cnt(base, f);
            char **a = nullptr;
            if (b.r8()) {
                a = (char **)calloc((size_t)(n ? n : 1), sizeof(char *));
                for (int k = 0; k < n && !b.bad; k++) a[k] = b.rstr();
            }
            wr_ptr<char **>(base, f.off, a);
            break;
        }
        case FK_DBLARR: case FK_INTARR: case FK_BYTES: {
            int n = rd_cnt(base, f);
            size_t es = f.kind == FK_DBLARR ? 8 : f.kind == FK_INTARR ? 4 : 1;
            void *a = nullptr;
            if (b.r8()) {
                // one spare zero byte so byte buffers stay NUL terminated
                a = calloc(1, es * (size_t)n + 1);
                b.get(a, es * (size_t)n);
            }
            wr_ptr<void *>(base, f.off, a);
            break;
        }
        case FK_ARR: {
            int n = rd_cnt(base, f);
            uint8_t *a = nullptr;
            if (b.r8()) {
                a = (uint8_t *)calloc((size_t)(n ? n : 1), f.elem);
                for (int k = 0; k < n && !b.bad; k++) {
                    b.get(a + (size_t)k * f.elem, f.elem);
                    decode_fields(f.type, a + (size_t)k * f.elem, b);
                }
            }
            wr_ptr<uint8_t *>(base, f.off, a);
            break;
        }
        case FK_INLINE:
            decode_fields(f.type, base + f.off, b);
            break;
        case FK_VOIDDATA:
            wr_ptr<void *>(base, f.off, dec_vd(f.aux, rd_i32(base, f.aux_off), b));
            break;
        case FK_VOIDARR: {
            int n = rd_cnt(base, f);
            void **a = nullptr;
            if (b.r8()) {
                int tv = rd_i32(base, f.aux_off);
                a = (void **)calloc((size_t)(n ? n : 1), sizeof(void *));
                for (int k = 0; k < n && !b.bad; k++) a[k] = dec_vd(f.aux, tv, b);
            }
            wr_ptr<void **>(base, f.off, a);
            break;
        }
        case FK_CB: {
            uint64_t id = b.r64();
            void *fn = nullptr, *dt = nullptr;
            if (id && hook_dec_cb) hook_dec_cb(f.aux, id, &fn, &dt);
            wr_ptr<void *>(base, f.off, fn);
            wr_ptr<void *>(base, f.aux_off, dt);
            break;
        }
        }
    }
}

inline void free_fields(int ty, uint8_t *base) {
    const TypeDesc &t = TYPES[ty];
    for (int i = 0; i < t.nf; i++) {
        const Field &f = t.f[i];
        switch (f.kind) {
        case FK_STR:
            free(rd_ptr<void *>(base, f.off));
            break;
        case FK_STRARR:
            free_strarr(rd_ptr<char **>(base, f.off), rd_cnt(base, f));
            break;
        case FK_DBLARR: case FK_INTARR: case FK_BYTES:
            free(rd_ptr<void *>(base, f.off));
            break;
        case FK_ARR:
            free_struct_array(f.type, rd_ptr<void *>(base, f.off), rd_cnt(base, f));
            break;
        case FK_INLINE:
            free_fields(f.type, base + f.off);
            break;
        case FK_VOIDDATA:
            free_vd(f.aux, rd_i32(base, f.aux_off), rd_ptr<void *>(base, f.off));
            break;
        case FK_VOIDARR: {
            void **a = rd_ptr<void **>(base, f.off);
            if (a) {
                int n = rd_cnt(base, f), tv = rd_i32(base, f.aux_off);
                for (int k = 0; k < n; k++) free_vd(f.aux, tv, a[k]);
                free(a);
            }
            break;
        }
        case FK_CB:
            break;
        }
    }
}

}  // namespace fb
