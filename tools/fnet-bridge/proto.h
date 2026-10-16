// Frame transport shared by shim and helper (unix stream socket).
#pragma once
#include <unistd.h>
#include <errno.h>
#include <mutex>

namespace fb {

enum { K_CALL = 1, K_RET = 2, K_CB = 3, K_CBRET = 4, K_CB_ASYNC = 5 };

inline bool read_full(int fd, void *p, size_t n) {
    uint8_t *b = (uint8_t *)p;
    while (n) {
        ssize_t r = ::read(fd, b, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        b += r;
        n -= (size_t)r;
    }
    return true;
}

inline bool write_full(int fd, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    while (n) {
        ssize_t r = ::write(fd, b, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        b += r;
        n -= (size_t)r;
    }
    return true;
}

// frame: u32 len | u8 kind | u64 id | u32 fid | payload      (len counts what follows it)
inline bool send_frame(int fd, std::mutex &mu, uint8_t kind, uint64_t id, uint32_t fid, const Buf &payload) {
    uint8_t hdr[4 + 1 + 8 + 4];
    uint32_t len = 1 + 8 + 4 + (uint32_t)payload.d.size();
    memcpy(hdr, &len, 4);
    hdr[4] = kind;
    memcpy(hdr + 5, &id, 8);
    memcpy(hdr + 13, &fid, 4);
    std::lock_guard<std::mutex> g(mu);
    return write_full(fd, hdr, sizeof hdr) && (payload.d.empty() || write_full(fd, payload.d.data(), payload.d.size()));
}

inline bool recv_frame(int fd, uint8_t &kind, uint64_t &id, uint32_t &fid, Buf &payload) {
    uint32_t len;
    if (!read_full(fd, &len, 4)) return false;
    if (len < 13 || len > (1u << 30)) return false;
    uint8_t h[13];
    if (!read_full(fd, h, 13)) return false;
    kind = h[0];
    memcpy(&id, h + 1, 8);
    memcpy(&fid, h + 9, 4);
    payload.d.resize(len - 13);
    payload.pos = 0;
    payload.bad = false;
    return payload.d.empty() || read_full(fd, payload.d.data(), payload.d.size());
}

}  // namespace fb
