// Driver for the fake plugin: prints everything it receives so that running it directly
// (x86_64, fake plugin) and through the bridge (arm64 shim + helper hosting the fake plugin)
// can be diffed.
#include <dlfcn.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "FlashNetwork.h"
#include "dump.h"

static void *g_lib;
template <class T> static T sym(const char *n) {
    T p = (T)dlsym(g_lib, n);
    if (!p) { printf("MISSING %s\n", n); exit(1); }
    return p;
}
#define F(name) auto name = sym<decltype(&fnet_##name)>("fnet_" #name)
#define G(name) static decltype(&fnet_##name) p_##name
G(freeString); G(allocString); G(freeDevDetail); G(freeSyncLoginInfo); G(freeSyncBindInfo);
G(freeSyncOnlineInfo); G(freeSliceState); G(freeJobInfo); G(freeUploadLogInfo);

static std::mutex g_print_mu;

static void cb_status(fnet_conn_status_t s, void *d) {
    std::lock_guard<std::mutex> g(g_print_mu);
    printf("[app] status cb %d data=%s\n", (int)s, (const char *)d);
}
static int cb_update(const char **cid, void *d) {
    std::lock_guard<std::mutex> g(g_print_mu);
    printf("[app] update cb data=%s\n", (const char *)d);
    *cid = p_allocString("my-client-id", 12);
    return 0;
}
static void cb_read(fnet_conn_read_data_t *r, void *d) {
    std::lock_guard<std::mutex> g(g_print_mu);
    printf("[app] read cb data=%s\n", (const char *)d);
    fnet_conn_read_data_t copy = *r;   // the app keeps a shallow copy and frees through the API later
    dump_read_data("    ", &copy);
    switch (copy.type) {
    case FNET_CONN_READ_SYS_NOTIFY: case FNET_CONN_READ_UPDATE_NOTIFY: case FNET_CONN_READ_DEVICE_KEEP_ALIVE: p_freeString((char *)copy.data); break;
    case FNET_CONN_READ_SYNC_LOGIN: p_freeSyncLoginInfo((fnet_sync_login_info_t *)copy.data); break;
    case FNET_CONN_READ_SYNC_BIND_DEVICE: case FNET_CONN_READ_SYNC_UNBIND_DEVICE: p_freeSyncBindInfo((fnet_sync_bind_info_t *)copy.data); break;
    case FNET_CONN_READ_SYNC_ONLINE: case FNET_CONN_READ_SYNC_OFFLINE: p_freeSyncOnlineInfo((fnet_sync_online_info_t *)copy.data); break;
    case FNET_CONN_READ_DEVICE_DETAIL: p_freeDevDetail((fnet_dev_detail_t *)copy.data); break;
    case FNET_CONN_READ_SLICE_STATE: p_freeSliceState((fnet_slice_state_t *)copy.data); break;
    case FNET_CONN_READ_JOB_DOWNLOAD: case FNET_CONN_READ_JOB_UNZIP: p_freeJobInfo((fnet_job_info_t *)copy.data); break;
    case FNET_CONN_READ_UPLOAD_LOG: p_freeUploadLogInfo((fnet_upload_log_info_t *)copy.data); break;
    default: break;
    }
    p_freeString(copy.topic);
}

static int cb_progress_ok(long long n, long long t, void *d) { printf("  [app] progress %lld/%lld %s\n", n, t, (const char *)d); return 0; }
static int cb_progress_abort(long long n, long long t, void *d) { printf("  [app] progress %lld/%lld %s\n", n, t, (const char *)d); return n >= 50; }
static int cb_check(const char *j, void *d) { printf("  [app] setting check %s data=%s\n", j, (const char *)d); return 7; }
static void cb_sprog(int p, void *d) { printf("  [app] setting progress %d data=%s\n", p, (const char *)d); }
static int cb_cancel(void *d) { printf("  [app] cancel? data=%s\n", (const char *)d); return 0; }

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    setvbuf(stdout, nullptr, _IONBF, 0);
    g_lib = dlopen(argv[1], RTLD_NOW);
    if (!g_lib) { printf("dlopen failed: %s\n", dlerror()); return 1; }
    F(initlize); F(uninitlize); F(getVersion); F(getHomePageUrl); F(setUserAgent); F(getLanDevList); F(freeLanDevInfos);
    F(getLanDevProduct); F(freeDevProduct); F(getLanDevDetail); F(getLanDevGcodeList); F(freeGcodeList); F(lanDevStartJob);
    F(ctrlLanDevTemp); F(ctrlLanDevHoming); F(lanDevSendGcode); F(downloadFileMem); F(freeFileData);
    F(getTokenByPassword); F(freeToken); F(getWanDevList); F(freeWanDevList); F(deleteTimeLapseVideo); F(wanDevAddCloundJob);
    F(freeAddCloudJobResults); F(getAiModelJobState); F(freeAiModelJobState); F(getSettingList2); F(getMqttConfig); F(freeMqttConfig);
    F(reportTrackingDataBatch); F(getSystemMessage); F(freeSystemMessage); F(createConnection); F(freeConnection);
    F(connectionStop); F(connectionSend); F(connectionSendMulti); F(connectionSubscribe); F(freeWriteMultiResult);
    F(freeDevDetail); F(freeString); F(allocString); F(freeSyncLoginInfo); F(freeSyncBindInfo); F(freeSyncOnlineInfo);
    F(freeSliceState); F(freeJobInfo); F(freeUploadLogInfo);
    p_freeString = freeString; p_allocString = allocString; p_freeDevDetail = freeDevDetail; p_freeSyncLoginInfo = freeSyncLoginInfo;
    p_freeSyncBindInfo = freeSyncBindInfo; p_freeSyncOnlineInfo = freeSyncOnlineInfo; p_freeSliceState = freeSliceState;
    p_freeJobInfo = freeJobInfo; p_freeUploadLogInfo = freeUploadLogInfo;

    fnet_log_settings_t ls = {"/tmp/logdir", 168, FNET_LOG_LEVEL_INFO};
    printf("initlize -> %d\n", initlize("/some/path.DAT", &ls));
    printf("version=%s home=%s\n", getVersion(), getHomePageUrl());
    setUserAgent("UA/1.0 (PC; macOS)");

    { fnet_lan_dev_info_t *i = nullptr; int n = 0; int r = getLanDevList(&i, &n, 100);
      printf("getLanDevList -> %d n=%d\n", r, n);
      for (int k = 0; k < n; k++) printf("  sn=%s name=%s ip=%s port=%u pid=%u bind=%u\n", i[k].serialNumber, i[k].name, i[k].ip, i[k].port, i[k].pid, i[k].bindStatus);
      freeLanDevInfos(i); }
    { fnet_dev_product_t *p = nullptr; int r = getLanDevProduct("1.2.3.4", 8898, "SN", "CHK", &p, 500);
      printf("getLanDevProduct -> %d nozzle=%d light=%d ext=%d\n", r, p->nozzleTempCtrlState, p->lightCtrlState, p->externalFanCtrlState); freeDevProduct(p); }
    { fnet_dev_detail_t *d = nullptr; int r = getLanDevDetail("1.2.3.4", 8898, "SN", "CHK", &d, 500);
      printf("getLanDevDetail -> %d\n", r); dump_dev_detail("  ", d); freeDevDetail(d); }
    { fnet_gcode_data_t *g = nullptr; int n = 0; int r = getLanDevGcodeList("1.2.3.4", 8898, "SN", "CHK", &g, &n, 500);
      printf("getLanDevGcodeList -> %d\n", r); dump_gcode_list("  ", g, n); freeGcodeList(g, n); }

    fnet_material_mapping_t maps[2] = {{0, 1, "PLA", "#111111", "#222222"}, {1, 3, "TPU", "#333333", nullptr}};
    { fnet_local_job_data_t j; memset(&j, 0, sizeof j);
      j.jobId = "job-1"; j.thumbUrl = "http://t"; j.fileName = "x.gcode"; j.printNow = 1; j.flowCalibration = 1; j.useMatlStation = 1;
      j.gcodeToolCnt = 2; j.materialMappings = maps;
      printf("lanDevStartJob -> %d\n", lanDevStartJob("1.2.3.4", 8898, "SN", "CHK", &j, 300)); }
    { double nz[3] = {200.5, 201.5, 202.5};
      fnet_temp_ctrl_t t = {60, 210, 0, 40, nz, 3};
      printf("ctrlLanDevTemp -> %d\n", ctrlLanDevTemp("1.2.3.4", 8898, "SN", "CHK", &t, 300));
      printf("homing -> %d\n", ctrlLanDevHoming("1.2.3.4", 8898, "SN", "CHK", 300)); }
    { fnet_send_gcode_data_t s; memset(&s, 0, sizeof s);
      s.gcodeFilePath = "/tmp/a.gcode"; s.thumbFilePath = "/tmp/a.png"; s.gcodeDstName = "a.gcode"; s.printNow = 1; s.timeLapseVideo = 1;
      s.gcodeToolCnt = 2; s.materialMappings = maps;
      s.callback = cb_progress_ok; s.callbackData = (void *)"send-ok";
      printf("sendGcode(ok) -> %d\n", lanDevSendGcode("1.2.3.4", 8898, "SN", "CHK", &s, 300));
      s.callback = cb_progress_abort; s.callbackData = (void *)"send-abort";
      printf("sendGcode(abort) -> %d\n", lanDevSendGcode("1.2.3.4", 8898, "SN", "CHK", &s, 300)); }
    { fnet_file_data_t *fd = nullptr; int r = downloadFileMem("http://f/x", &fd, cb_progress_ok, (void *)"dl", 100, 200);
      printf("downloadFileMem -> %d size=%u bytes=", r, fd->size);
      for (unsigned i = 0; i < fd->size; i++) printf("%02x", (unsigned char)fd->data[i]);
      printf("\n"); freeFileData(fd); }
    { fnet_token_data_t *t = nullptr; char *m = nullptr;
      int r = getTokenByPassword("user", "ok", "en", &t, &m, 100);
      printf("token(ok) -> %d exp=%d acc=%s ref=%s msg=%s\n", r, t ? t->expiresIn : -1, t ? t->accessToken : "<null>", t ? t->refreshToken : "<null>", S(m));
      freeToken(t); freeString(m); t = nullptr; m = nullptr;
      r = getTokenByPassword("user", "bad", "pt", &t, &m, 100);
      printf("token(bad) -> %d tok=%s msg=%s\n", r, t ? "set" : "null", S(m)); freeString(m); }
    { fnet_wan_dev_info_t *w = nullptr; int n = 0; int r = getWanDevList("cid", "tok", 123456789012LL, -5LL, &w, &n, 100);
      printf("getWanDevList -> %d\n", r); dump_wan_list("  ", w, n); freeWanDevList(w, n); }
    { const char *ids[3] = {"a", "bb", "ccc"};
      printf("deleteTimeLapse -> %d\n", deleteTimeLapseVideo("cid", "tok", ids, 3, 100)); }
    { const char *dev[2] = {"d0", "d1"}, *sn[2] = {"s0", nullptr}, *jb[2] = {"j0", "j1"};
      fnet_clound_job_data_t j; memset(&j, 0, sizeof j);
      j.devIds = dev; j.devSerialNumbers = sn; j.jobIds = jb; j.devCnt = 2; j.gcodeName = "g.3mf"; j.gcodeType = "3mf"; j.gcodeMd5 = "md5";
      j.gcodeSize = 5000000000LL; j.thumbName = "t.png"; j.bucketName = "bkt"; j.plateNo = 3; j.gcodeToolCnt = 1; j.materialMappings = maps;
      fnet_add_clound_job_result_t *res = nullptr; int n = 0;
      int r = wanDevAddCloundJob("cid", "tok", &j, &res, &n, 100);
      printf("addCloundJob -> %d n=%d\n", r, n);
      for (int i = 0; i < n; i++) printf("  res[%d] err=%d dev=%s job=%s\n", i, (int)res[i].error, S(res[i].devId), S(res[i].jobId));
      freeAddCloudJobResults(res, n); }
    { fnet_ai_model_job_state_t *s = nullptr; int r = getAiModelJobState("cid", "tok", 99LL, &s, 100);
      printf("aiModelJobState -> %d status=%d job=%lld pos=%d len=%d ext=%s\n", r, s->status, s->jobId, s->posInQueue, s->queueLength, S(s->externalJobId));
      for (int i = 0; i < s->modelCnt; i++) printf("  model[%d] %s %s\n", i, S(s->models[i].modelType), S(s->models[i].modelUrl));
      freeAiModelJobState(s); }
    { printf("getSettingList2 -> %d\n", getSettingList2("cid", "tok", "bundle", cb_check, (void *)"chk", cb_sprog, (void *)"prog", cb_cancel, (void *)"cancel", 100)); }
    { fnet_mqtt_config_t *c = nullptr; int r = getMqttConfig("cid", "tok", &c, 100);
      printf("mqttConfig -> %d user=%s n=%d:", r, S(c->userTopic), c->commonTopicCnt);
      for (int i = 0; i < c->commonTopicCnt; i++) printf(" %s", c->commonTopics[i]);
      printf("\n"); freeMqttConfig(c); }
    { fnet_tracking_common_data_t c; memset(&c, 0, sizeof c); c.uid = "u1"; c.did = "d1";
      fnet_tracking_event_data_t e[2]; memset(e, 0, sizeof e); e[0].eventType = "click"; e[0].timestamp = "123"; e[1].eventType = "view"; e[1].extend = "{}";
      printf("tracking -> %d\n", reportTrackingDataBatch("cid", &c, e, 2, 100)); }
    { fnet_sys_msg_data_t *m = nullptr; int r = getSystemMessage("cid", "tok", "en", &m, 100);
      printf("sysmsg -> %d id=%d mid=%d type=%s title=%s content=%s link=%d %s dur=%d show=%d\n", r, m->id, m->messageId, S(m->notifyType), S(m->title), S(m->content), m->linkType, S(m->linkUrl), m->duration, m->showCount);
      freeSystemMessage(m); }

    // ---- connection with callbacks
    void *conn = nullptr;
    {
        fnet_conn_settings_t cs; memset(&cs, 0, sizeof cs);
        cs.clientId = "client-xyz";
        cs.statusCallback = cb_status; cs.statusCallbackData = (void *)"S";
        cs.updateCallback = cb_update; cs.updateCallbackData = (void *)"U";
        cs.readCallback = cb_read; cs.readCallbackData = (void *)"R";
        int r = createConnection(&conn, &cs);
        printf("createConnection -> %d conn=%s\n", r, conn ? "set" : "null");
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        std::lock_guard<std::mutex> g(g_print_mu);   // wait until the callbacks are quiet
        fnet_conn_write_data_t w; memset(&w, 0, sizeof w);
        w.topic = "t/one"; w.qos = 1;
        w.type = FNET_CONN_WRITE_SYNC_LOGIN; w.data = "cid-777";
        printf("send login -> %d\n", connectionSend(conn, &w));
        fnet_local_job_data_t j; memset(&j, 0, sizeof j); j.jobId = "J"; j.fileName = "f.gcode"; j.gcodeToolCnt = 2; j.materialMappings = maps;
        w.type = FNET_CONN_WRITE_START_JOB; w.data = &j;
        printf("send start job -> %d\n", connectionSend(conn, &w));
        double nz[2] = {1.5, 2.5}; fnet_temp_ctrl_t tc = {61, 211, 0, 41, nz, 2};
        w.type = FNET_CONN_WRITE_TEMP_CTRL; w.data = &tc;
        printf("send temp -> %d\n", connectionSend(conn, &w));
        fnet_light_ctrl_t lc = {"close"};
        w.type = FNET_CONN_WRITE_LIGHT_CTRL; w.data = &lc;
        printf("send light -> %d\n", connectionSend(conn, &w));

        const void *nullData = nullptr; const char *topics[2] = {"t/a", "t/b"};
        fnet_conn_write_multi_data_t mw; memset(&mw, 0, sizeof mw);
        mw.type = FNET_CONN_WRITE_UPDATE_DETAIL; mw.datas = &nullData; mw.topics = topics; mw.dataCnt = 1; mw.topicCnt = 2; mw.qos = 1;
        fnet_conn_write_multi_result_t *wr = nullptr;
        r = connectionSendMulti(conn, &mw, &wr);
        printf("sendMulti(update) -> %d failed=%d:", r, wr ? wr->failedCnt : -1);
        for (int i = 0; wr && i < wr->failedCnt; i++) printf(" %d", wr->failedIndices[i]);
        printf("\n"); freeWriteMultiResult(wr);
        fnet_light_ctrl_t l0 = {"open"}, l1 = {"close"}; const void *lights[2] = {&l0, &l1};
        mw.type = FNET_CONN_WRITE_LIGHT_CTRL; mw.datas = lights; mw.dataCnt = 2;
        wr = nullptr;
        r = connectionSendMulti(conn, &mw, &wr);
        printf("sendMulti(light x2) -> %d failed=%d\n", r, wr ? wr->failedCnt : -1); freeWriteMultiResult(wr);

        fnet_conn_subscribe_data_t sub = {topics, 2};
        printf("subscribe -> %d\n", connectionSubscribe(conn, &sub));
        connectionStop(conn);
    }
    // give the DISCONNECTED status callback time to arrive, then free
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    freeConnection(conn);
    printf("connection freed\n");
    printf("done\n");
    uninitlize();
    return 0;
}
