// libFlashNetwork.dylib (arm64 shim).
// Exports the FlashNetwork C API and forwards every call to the x86_64 helper
// (fnet_helper, running under Rosetta) that hosts the original plugin.
#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

#include "types.h"
#include "gen_common.inc"
using namespace fb;
#include "engine.h"
#include "proto.h"

extern char **environ;

namespace fb {

// ------------------------------------------------------------ callback registry
struct CbEntry {
    int kind;
    void *fn;
    void *data;
};
static std::mutex g_cb_mu;
static std::unordered_map<uint64_t, CbEntry> g_cbs;
static std::unordered_map<void *, std::vector<uint64_t>> g_conn_cbs;
static std::atomic<uint64_t> g_cb_next{1};

struct CbScope;
static thread_local CbScope *g_cur_scope = nullptr;

struct CbScope {
    std::vector<uint64_t> ids;
    CbScope *prev;
    bool kept = false;
    CbScope() : prev(g_cur_scope) { g_cur_scope = this; }
    ~CbScope() {
        g_cur_scope = prev;
        if (!kept) {
            std::lock_guard<std::mutex> g(g_cb_mu);
            for (uint64_t id : ids) g_cbs.erase(id);
        }
    }
    void keep_for_conn(void *conn) {
        kept = true;
        std::lock_guard<std::mutex> g(g_cb_mu);
        g_conn_cbs[conn] = ids;
    }
};

static uint64_t cb_register(int kind, void *fn, void *data, CbScope &s) {
    if (!fn) return 0;
    uint64_t id = g_cb_next++;
    std::lock_guard<std::mutex> g(g_cb_mu);
    g_cbs[id] = CbEntry{kind, fn, data};
    s.ids.push_back(id);
    return id;
}

static void conn_release(void *conn) {
    std::lock_guard<std::mutex> g(g_cb_mu);
    auto it = g_conn_cbs.find(conn);
    if (it == g_conn_cbs.end()) return;
    for (uint64_t id : it->second) g_cbs.erase(id);
    g_conn_cbs.erase(it);
}

static uint64_t enc_cb_hook(int kind, void *fn, void *data) {
    return g_cur_scope ? cb_register(kind, fn, data, *g_cur_scope) : 0;
}

// ------------------------------------------------------------------ transport
struct Pending {
    std::mutex m;
    std::condition_variable cv;
    bool done = false, ok = false;
    Buf resp;
};

struct CbItem {
    uint8_t kind;
    uint64_t id;
    uint32_t cbkind;
    Buf payload;
};

static std::mutex g_state_mu;
static int g_fd = -1;
static bool g_spawned = false;
static std::atomic<bool> g_dead{false};
static std::mutex g_wmu;
static std::mutex g_pend_mu;
static std::unordered_map<uint64_t, Pending *> g_pend;
static std::atomic<uint64_t> g_req_next{1};
static std::mutex g_q_mu;
static std::condition_variable g_q_cv;
static std::deque<CbItem> g_q;

static std::string self_dir() {
    const char *env = getenv("FNET_BRIDGE_DIR");
    if (env && *env) return env;
    Dl_info di;
    if (dladdr((void *)&self_dir, &di) && di.dli_fname) {
        std::string p = di.dli_fname;
        size_t s = p.rfind('/');
        if (s != std::string::npos) return p.substr(0, s);
    }
    return ".";
}

static void fail_all() {
    std::lock_guard<std::mutex> g(g_pend_mu);
    for (auto &kv : g_pend) {
        std::lock_guard<std::mutex> l(kv.second->m);
        kv.second->done = true;
        kv.second->ok = false;
        kv.second->cv.notify_all();
    }
}

static void reader_loop(int fd) {
    for (;;) {
        uint8_t kind;
        uint64_t id;
        uint32_t fid;
        Buf payload;
        if (!recv_frame(fd, kind, id, fid, payload)) break;
        if (kind == K_RET) {
            std::lock_guard<std::mutex> g(g_pend_mu);
            auto it = g_pend.find(id);
            if (it != g_pend.end()) {
                Pending *p = it->second;
                std::lock_guard<std::mutex> l(p->m);
                p->resp = std::move(payload);
                p->done = true;
                p->ok = true;
                p->cv.notify_all();
            }
        } else if (kind == K_CB || kind == K_CB_ASYNC) {
            std::lock_guard<std::mutex> g(g_q_mu);
            g_q.push_back(CbItem{kind, id, fid, std::move(payload)});
            g_q_cv.notify_one();
        }
    }
    g_dead = true;
    fail_all();
    fprintf(stderr, "FlashNetwork bridge: helper connection closed\n");
}

static void handle_cb(CbItem &it) {
    uint64_t cbid = it.payload.r64();
    CbEntry e{};
    bool found = false;
    {
        std::lock_guard<std::mutex> g(g_cb_mu);
        auto f = g_cbs.find(cbid);
        if (f != g_cbs.end()) { e = f->second; found = true; }
    }
    Buf rs;
    switch (it.cbkind) {
    case CB_PROGRESS: {
        long long now = it.payload.ri64(), total = it.payload.ri64();
        int r = found ? ((fnet_progress_callback_t)e.fn)(now, total, e.data) : 0;
        rs.i64(r);
        break;
    }
    case CB_SETTING_CHECK: {
        char *j = it.payload.rstr();
        int r = found ? ((fnet_setting_check_callback_t)e.fn)(j, e.data) : 0;
        free(j);
        rs.i64(r);
        break;
    }
    case CB_SETTING_PROGRESS: {
        int p = (int)it.payload.ri64();
        if (found) ((fnet_setting_progress_callback_t)e.fn)(p, e.data);
        rs.i64(0);
        break;
    }
    case CB_SETTING_CANCEL: {
        int r = found ? ((fnet_setting_cancel_callback_t)e.fn)(e.data) : 0;
        rs.i64(r);
        break;
    }
    case CB_CONN_STATUS: {
        fnet_conn_status_t st = (fnet_conn_status_t)it.payload.ri64();
        if (found) ((fnet_conn_status_callback_t)e.fn)(st, e.data);
        break;
    }
    case CB_CONN_UPDATE: {
        const char *cid = nullptr;
        int r = found ? ((fnet_conn_update_callback_t)e.fn)(&cid, e.data) : 1;
        rs.i64(r);
        rs.str(cid);
        free((void *)cid);   // allocated by our fnet_allocString (malloc); helper re-allocates it
        break;
    }
    case CB_CONN_READ: {
        void *rd = dec_struct_ptr(it.payload, TY_fnet_conn_read_data_t);
        if (found && rd) {
            ((fnet_conn_read_callback_t)e.fn)((fnet_conn_read_data_t *)rd, e.data);
            free(rd);    // the app took ownership of data/topic, only the shell is ours
        } else {
            free_struct_ptr(TY_fnet_conn_read_data_t, rd);
        }
        break;
    }
    default:
        break;
    }
    if (it.kind == K_CB) send_frame(g_fd, g_wmu, K_CBRET, it.id, it.cbkind, rs);
}

static void executor_loop() {
    for (;;) {
        CbItem it;
        {
            std::unique_lock<std::mutex> l(g_q_mu);
            g_q_cv.wait(l, [] { return !g_q.empty(); });
            it = std::move(g_q.front());
            g_q.pop_front();
        }
        handle_cb(it);
    }
}

static bool spawn_helper() {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return false;
    fcntl(sv[0], F_SETFD, FD_CLOEXEC);
    int one = 1;
    setsockopt(sv[0], SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    setsockopt(sv[1], SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);

    std::string dir = self_dir();
    std::string helper = dir + "/fnet_helper";
    std::string lib = dir + "/libFlashNetwork_x64.dylib";
    std::string fdstr = std::to_string(sv[1]);
    const char *argv[] = {helper.c_str(), "--fd", fdstr.c_str(), "--lib", lib.c_str(), nullptr};

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addinherit_np(&fa, 0);
    posix_spawn_file_actions_addinherit_np(&fa, 1);
    posix_spawn_file_actions_addinherit_np(&fa, 2);
    posix_spawn_file_actions_addinherit_np(&fa, sv[1]);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, helper.c_str(), &fa, &attr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    close(sv[1]);
    if (rc != 0) {
        fprintf(stderr, "FlashNetwork bridge: cannot start %s (%s). Is Rosetta installed?\n", helper.c_str(), strerror(rc));
        close(sv[0]);
        return false;
    }
    g_fd = sv[0];
    g_dead = false;
    std::thread(reader_loop, g_fd).detach();
    static std::once_flag once;
    std::call_once(once, [] { std::thread(executor_loop).detach(); });
    return true;
}

static bool ensure_helper(bool allow_respawn) {
    std::lock_guard<std::mutex> g(g_state_mu);
    if (!g_spawned) {
        hook_enc_cb = enc_cb_hook;
        g_spawned = true;
        return spawn_helper();
    }
    if (g_fd >= 0 && !g_dead) return true;
    if (g_dead && allow_respawn) {
        if (g_fd >= 0) close(g_fd);
        g_fd = -1;
        return spawn_helper();
    }
    return false;
}

static bool bridge_call(uint32_t fid, Buf &rq, Buf &rs) {
    if (!ensure_helper(fid == FID_fnet_initlize)) return false;
    uint64_t id = g_req_next++;
    Pending p;
    {
        std::lock_guard<std::mutex> g(g_pend_mu);
        g_pend[id] = &p;
    }
    bool sent = send_frame(g_fd, g_wmu, K_CALL, id, fid, rq);
    if (sent) {
        std::unique_lock<std::mutex> l(p.m);
        p.cv.wait(l, [&] { return p.done; });
    }
    {
        std::lock_guard<std::mutex> g(g_pend_mu);
        g_pend.erase(id);
    }
    if (!sent || !p.ok) return false;
    rs = std::move(p.resp);
    rs.pos = 0;
    return true;
}

static const char *intern(Buf &rs) {
    char *s = rs.rstr();
    if (!s) return nullptr;
    static std::mutex mu;
    static std::set<std::string> pool;
    std::lock_guard<std::mutex> g(mu);
    const char *r = pool.insert(s).first->c_str();
    free(s);
    return r;
}

}  // namespace fb

#include "gen_shim.inc"

extern "C" char *fnet_allocString(const char *str, int len) {
    if (!str) return nullptr;
    if (len < 0) len = (int)strlen(str);
    char *p = (char *)malloc((size_t)len + 1);
    memcpy(p, str, (size_t)len);
    p[len] = 0;
    return p;
}

extern "C" void fnet_freeString(const char *str) { free((void *)str); }
