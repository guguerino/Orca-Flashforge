// Exercises the FlashNetwork API through dlopen so the same source can run
//   (a) x86_64, directly against the original plugin, and
//   (b) arm64, through the bridge shim.
// Output must be identical (except for time-dependent values).
#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "FlashNetwork.h"

static void *g_lib;
template <class T> static T sym(const char *n) {
    T p = (T)dlsym(g_lib, n);
    if (!p) { printf("MISSING %s\n", n); exit(1); }
    return p;
}
#define F(name) auto name = sym<decltype(&fnet_##name)>("fnet_" #name)

static std::atomic<int> g_status_cb{0}, g_read_cb{0}, g_update_cb{0};
static void on_status(fnet_conn_status_t s, void *d) { g_status_cb++; printf("  [cb status %d data=%s]\n", (int)s, (const char *)d); }
static int on_update(const char **cid, void *d) { g_update_cb++; *cid = nullptr; (void)d; return 1; }
static void on_read(fnet_conn_read_data_t *r, void *d) { g_read_cb++; (void)d; (void)r; }
static int on_progress(long long now, long long total, void *d) { printf("  [progress %lld/%lld %s]\n", now, total, (const char *)d); return 0; }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <lib> <dat> [net]\n", argv[0]); return 2; }
    setvbuf(stdout, nullptr, _IONBF, 0);
    bool net = argc > 3;
    g_lib = dlopen(argv[1], RTLD_NOW);
    if (!g_lib) { printf("dlopen failed: %s\n", dlerror()); return 1; }
    F(initlize); F(uninitlize); F(getVersion); F(getHomePageUrl); F(setUserAgent);
    F(getLanDevList); F(freeLanDevInfos); F(getTokenByPassword); F(freeToken); F(freeString);
    F(allocString); F(getSystemMessage); F(freeSystemMessage); F(createConnection);
    F(freeConnection); F(connectionStop); F(connectionSubscribe); F(downloadFileMem);
    F(freeFileData); F(getLanDevDetail); F(freeDevDetail); F(sendSMSCode);
    F(getUserProfile); F(freeUserProfile); F(doBusGetRequest); F(getMqttConfig);
    F(getWanDevList); F(freeWanDevList); F(getSettingList2); F(getUserPresets);
    F(connectionSend); F(lanDevSendGcode); F(reportTrackingDataBatch);
    F(deleteTimeLapseVideo);

    fnet_log_settings_t ls;
    ls.fileDir = "/tmp/fnet-test-logs";
    ls.expireHours = 1;
    ls.level = FNET_LOG_LEVEL_INFO;
    printf("initlize -> %d\n", initlize(argv[2], &ls));
    printf("version  -> %s\n", getVersion());
    setUserAgent("Orca-Flashforge/test (PC; macOS)");
    const char *home = getHomePageUrl();
    printf("home     -> %s\n", home ? home : "(null)");

    // unreachable LAN printer: error path + out struct untouched
    {
        fnet_dev_detail_t *det = nullptr;
        int r = getLanDevDetail("127.0.0.1", 1, "SN", "CHK", &det, 1000);
        printf("getLanDevDetail(unreachable) -> %d detail=%s\n", r, det ? "set" : "null");
        if (det) freeDevDetail(det);
    }
    // wrong credentials: out struct + out string
    {
        fnet_token_data_t *tok = nullptr;
        char *msg = nullptr;
        int r = net ? getTokenByPassword("nobody@example.invalid", "wrong", "en", &tok, &msg, 15000) : -999;
        printf("getTokenByPassword(bad) -> %d tok=%s msg=%s\n", r, tok ? "set" : "null", msg ? msg : "(null)");
        if (tok) freeToken(tok);
        if (msg) freeString(msg);
    }
    // array output
    {
        fnet_lan_dev_info_t *infos = nullptr;
        int cnt = -1;
        int r = getLanDevList(&infos, &cnt, 1500);
        printf("getLanDevList -> %d cnt=%d\n", r, cnt);
        for (int i = 0; i < cnt; i++) printf("  lan dev: sn=%s name=%s ip=%s port=%u\n", infos[i].serialNumber, infos[i].name, infos[i].ip, infos[i].port);
        if (infos) freeLanDevInfos(infos);
    }
    // bytes output + progress callback
    if (net) {
        fnet_file_data_t *fd = nullptr;
        int r = downloadFileMem("https://www.flashforge.com/robots.txt", &fd, on_progress, (void *)"dl", 8000, 15000);
        printf("downloadFileMem -> %d size=%u first=%.20s\n", r, fd ? fd->size : 0, fd && fd->data ? fd->data : "");
        if (fd) freeFileData(fd);
    }
    // string-array input / struct-array input / null struct
    {
        const char *ids[2] = {"job1", "job2"};
        int r = deleteTimeLapseVideo("cid", "tok", ids, 2, 1500);
        printf("deleteTimeLapseVideo -> %d\n", r);
        fnet_tracking_common_data_t common;
        memset(&common, 0, sizeof common);
        for (const char **q = &common.uid; q <= &common.ext; q++) *q = "";
        common.uid = "u"; common.did = "d";
        fnet_tracking_event_data_t ev[2];
        memset(ev, 0, sizeof ev);
        for (int i = 0; i < 2; i++)
            for (const char **q = &ev[i].eventType; q <= &ev[i].timestamp; q++) *q = "";
        ev[0].eventType = "t0"; ev[1].eventType = "t1"; ev[1].extend = "x";
        r = reportTrackingDataBatch("cid", &common, ev, 2, 1500);
        printf("reportTrackingDataBatch -> %d\n", r);
        char *resp = nullptr;
        r = doBusGetRequest("cid", "tok", "en", "/x", &resp, 1500);
        printf("doBusGetRequest -> %d resp=%s\n", r, resp ? resp : "(null)");
        if (resp) freeString(resp);
        fnet_sys_msg_data_t *sm = nullptr;
        r = getSystemMessage("cid", "tok", "en", &sm, 1500);
        printf("getSystemMessage -> %d sm=%s\n", r, sm ? "set" : "null");
        if (sm) freeSystemMessage(sm);
    }
    // alloc/free string symmetry
    {
        char *s = allocString("hello", 5);
        printf("allocString -> %s\n", s);
        freeString(s);
    }
    // connection with callbacks, subscribe, send, stop
    {
        fnet_conn_settings_t cs;
        memset(&cs, 0, sizeof cs);
        cs.clientId = "test-client-id";
        cs.statusCallback = on_status; cs.statusCallbackData = (void *)"conn-1";
        cs.updateCallback = on_update;
        cs.readCallback = on_read;
        void *conn = nullptr;
        int r = createConnection(&conn, &cs);
        printf("createConnection -> %d conn=%s\n", r, conn ? "set" : "null");
        if (conn) {
            const char *topics[2] = {"t/a", "t/b"};
            fnet_conn_subscribe_data_t sub = {topics, 2};
            printf("connectionSubscribe -> %d\n", connectionSubscribe(conn, &sub));
            fnet_conn_write_data_t wd;
            memset(&wd, 0, sizeof wd);
            wd.type = FNET_CONN_WRITE_SYNC_LOGIN;
            wd.data = "cid";
            wd.topic = "t/a";
            printf("connectionSend(sync login) -> %d\n", connectionSend(conn, &wd));
            fnet_light_ctrl_t lc = {"open"};
            wd.type = FNET_CONN_WRITE_LIGHT_CTRL; wd.data = &lc;
            printf("connectionSend(light) -> %d\n", connectionSend(conn, &wd));
            std::this_thread::sleep_for(std::chrono::milliseconds(net ? 3000 : 500));
            connectionStop(conn);
            freeConnection(conn);
            printf("connection freed; status_cbs>0=%d\n", g_status_cb.load() > 0);
        }
    }
    uninitlize();
    printf("done\n");
    fflush(stdout);
    return 0;
}
