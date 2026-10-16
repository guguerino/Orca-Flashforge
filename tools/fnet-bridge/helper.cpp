// fnet_helper (x86_64, runs under Rosetta).
// Hosts the original proprietary libFlashNetwork (x86_64) and executes the calls that the
// arm64 shim forwards over a unix socket.
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "types.h"
#include "gen_common.inc"
using namespace fb;
#include "engine.h"
#include "proto.h"

static int g_fd = -1;
static std::mutex g_wmu;

namespace fb {

inline void enc_struct_array_out(Buf &b, int ty, const void *p, int n) { enc_struct_array(b, ty, p, n); }

struct CbCtx {
    uint64_t id;
    int kind;
};

struct TlCtx;
inline thread_local TlCtx *tl_cur = nullptr;

struct TlCtx {
    std::vector<CbCtx *> v;
    TlCtx *prev;
    TlCtx() : prev(tl_cur) { tl_cur = this; }
    ~TlCtx() { tl_cur = prev; release(); }
    void release() {
        for (CbCtx *c : v) delete c;
        v.clear();
    }
};

void dec_cb(int kind, uint64_t id, void **fn, void **data);

struct ConnInfo {
    std::vector<CbCtx *> ctx;
    fnet_conn_settings_t *settings;
};
static std::mutex g_conn_mu;
static std::unordered_map<void *, ConnInfo> g_conns;

inline void conn_keep(void *conn, TlCtx &tl, fnet_conn_settings_t *settings) {
    std::lock_guard<std::mutex> g(g_conn_mu);
    ConnInfo ci;
    ci.ctx = std::move(tl.v);
    tl.v.clear();
    ci.settings = settings;
    g_conns[conn] = std::move(ci);
}

inline void conn_drop(void *conn);

}  // namespace fb

#include "gen_helper.inc"

namespace fb {

inline void conn_drop(void *conn) {
    ConnInfo ci{};
    {
        std::lock_guard<std::mutex> g(g_conn_mu);
        auto it = g_conns.find(conn);
        if (it == g_conns.end()) return;
        ci = std::move(it->second);
        g_conns.erase(it);
    }
    for (CbCtx *c : ci.ctx) delete c;
    free_struct_ptr(TY_fnet_conn_settings_t, ci.settings);
}

// ------------------------------------------------------------ callback trampolines
struct CbWait {
    std::mutex m;
    std::condition_variable cv;
    bool done = false, ok = false;
    Buf resp;
};
static std::mutex g_wait_mu;
static std::unordered_map<uint64_t, CbWait *> g_waits;
static std::atomic<uint64_t> g_cbreq_next{1};

static bool cb_roundtrip(int kind, Buf &rq, Buf &rs) {
    uint64_t id = g_cbreq_next++;
    CbWait w;
    {
        std::lock_guard<std::mutex> g(g_wait_mu);
        g_waits[id] = &w;
    }
    bool sent = send_frame(g_fd, g_wmu, K_CB, id, (uint32_t)kind, rq);
    if (sent) {
        std::unique_lock<std::mutex> l(w.m);
        w.cv.wait(l, [&] { return w.done; });
    }
    {
        std::lock_guard<std::mutex> g(g_wait_mu);
        g_waits.erase(id);
    }
    if (!sent || !w.ok) return false;
    rs = std::move(w.resp);
    rs.pos = 0;
    return true;
}

static void cb_async(int kind, Buf &rq) { send_frame(g_fd, g_wmu, K_CB_ASYNC, 0, (uint32_t)kind, rq); }

static int tramp_progress(long long now, long long total, void *data) {
    CbCtx *c = (CbCtx *)data;
    Buf rq, rs;
    rq.u64(c->id);
    rq.i64(now);
    rq.i64(total);
    if (!cb_roundtrip(CB_PROGRESS, rq, rs)) return 1;   // bridge broken: abort the transfer
    return (int)rs.ri64();
}

static int tramp_setting_check(const char *json, void *data) {
    CbCtx *c = (CbCtx *)data;
    Buf rq, rs;
    rq.u64(c->id);
    rq.str(json);
    if (!cb_roundtrip(CB_SETTING_CHECK, rq, rs)) return 1;
    return (int)rs.ri64();
}

static void tramp_setting_progress(int progress, void *data) {
    CbCtx *c = (CbCtx *)data;
    Buf rq, rs;
    rq.u64(c->id);
    rq.i64(progress);
    cb_roundtrip(CB_SETTING_PROGRESS, rq, rs);
}

static int tramp_setting_cancel(void *data) {
    CbCtx *c = (CbCtx *)data;
    Buf rq, rs;
    rq.u64(c->id);
    if (!cb_roundtrip(CB_SETTING_CANCEL, rq, rs)) return 1;
    return (int)rs.ri64();
}

static void tramp_conn_status(fnet_conn_status_t status, void *data) {
    CbCtx *c = (CbCtx *)data;
    Buf rq;
    rq.u64(c->id);
    rq.i64((int64_t)status);
    cb_async(CB_CONN_STATUS, rq);
}

static int tramp_conn_update(const char **clientId, void *data) {
    CbCtx *c = (CbCtx *)data;
    Buf rq, rs;
    rq.u64(c->id);
    if (!cb_roundtrip(CB_CONN_UPDATE, rq, rs)) return 1;
    int ret = (int)rs.ri64();
    char *s = rs.rstr();
    if (ret == 0 && s && clientId) *clientId = real_fnet_allocString(s, (int)strlen(s));
    free(s);
    return ret;
}

static void tramp_conn_read(fnet_conn_read_data_t *rd, void *data) {
    CbCtx *c = (CbCtx *)data;
    Buf rq;
    rq.u64(c->id);
    enc_struct_ptr(rq, TY_fnet_conn_read_data_t, rd);
    // the callee owns data/topic: now that they are serialised, release the originals
    int t = (int)rd->type;
    if (t >= 0 && t < VD_N[VD_READ]) {
        const VdEntry &e = VD[VD_READ][t];
        if (e.vk == 1) real_fnet_freeString((const char *)rd->data);
        else if (e.vk == 2) real_free_struct(e.type, rd->data);
    }
    if (rd->topic) real_fnet_freeString(rd->topic);
    cb_async(CB_CONN_READ, rq);
}

void dec_cb(int kind, uint64_t id, void **fn, void **data) {
    CbCtx *c = new CbCtx{id, kind};
    if (tl_cur) tl_cur->v.push_back(c);
    *data = c;
    switch (kind) {
    case CB_PROGRESS: *fn = (void *)tramp_progress; break;
    case CB_SETTING_CHECK: *fn = (void *)tramp_setting_check; break;
    case CB_SETTING_PROGRESS: *fn = (void *)tramp_setting_progress; break;
    case CB_SETTING_CANCEL: *fn = (void *)tramp_setting_cancel; break;
    case CB_CONN_STATUS: *fn = (void *)tramp_conn_status; break;
    case CB_CONN_UPDATE: *fn = (void *)tramp_conn_update; break;
    case CB_CONN_READ: *fn = (void *)tramp_conn_read; break;
    default: *fn = nullptr; break;
    }
}

}  // namespace fb

// ------------------------------------------------------------------------- main
struct CallArgs {
    uint64_t id;
    uint32_t fid;
    fb::Buf in;
};

static void *call_thread(void *p) {
    std::unique_ptr<CallArgs> a((CallArgs *)p);
    fb::Buf out;
    if (a->fid < FID_COUNT) CALLS[a->fid](a->in, out);
    fb::send_frame(g_fd, g_wmu, fb::K_RET, a->id, a->fid, out);
    return nullptr;
}

int main(int argc, char **argv) {
    const char *lib = nullptr;
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--fd")) g_fd = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--lib")) lib = argv[i + 1];
    }
    if (g_fd < 0 || !lib) {
        fprintf(stderr, "usage: fnet_helper --fd N --lib path\n");
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    void *h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        fprintf(stderr, "fnet_helper: cannot load %s: %s\n", lib, dlerror());
        return 2;
    }
    load_real(h);
    fb::hook_dec_cb = fb::dec_cb;

    for (;;) {
        uint8_t kind;
        uint64_t id;
        uint32_t fid;
        fb::Buf payload;
        if (!fb::recv_frame(g_fd, kind, id, fid, payload)) break;   // shim gone: exit
        if (kind == fb::K_CALL) {
            CallArgs *a = new CallArgs{id, fid, std::move(payload)};
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, 8u << 20);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            pthread_t t;
            if (pthread_create(&t, &attr, call_thread, a) != 0) {
                fb::Buf out;
                fb::send_frame(g_fd, g_wmu, fb::K_RET, id, fid, out);
                delete a;
            }
            pthread_attr_destroy(&attr);
        } else if (kind == fb::K_CBRET) {
            std::lock_guard<std::mutex> g(fb::g_wait_mu);
            auto it = fb::g_waits.find(id);
            if (it != fb::g_waits.end()) {
                fb::CbWait *w = it->second;
                std::lock_guard<std::mutex> l(w->m);
                w->resp = std::move(payload);
                w->done = true;
                w->ok = true;
                w->cv.notify_all();
            }
        }
    }
    _exit(0);
}
