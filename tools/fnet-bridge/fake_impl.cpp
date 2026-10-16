// A fake x86_64 FlashNetwork plugin with deterministic behaviour, used to test the bridge
// marshalling (complex structs, arrays, callbacks, connection threads) without a printer or
// an account.  Everything not implemented here comes from the weak stubs.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "FlashNetwork.h"
#include "dump.h"

static char *D(const char *s) { return s ? strdup(s) : nullptr; }
#define P(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)

extern "C" {

int fnet_initlize(const char *path, const fnet_log_settings_t *ls) {
    P("[plugin] initlize path=%s dir=%s expire=%d level=%d\n", S(path), S(ls ? ls->fileDir : nullptr), ls ? ls->expireHours : -1, ls ? (int)ls->level : -1);
    return 0;
}
void fnet_uninitlize() { P("[plugin] uninitlize\n"); }
const char *fnet_getVersion() { return "fake-1.0"; }
const char *fnet_getHomePageUrl() { return "https://fake.example/home"; }
void fnet_setUserAgent(const char *ua) { P("[plugin] ua=%s\n", S(ua)); }
char *fnet_allocString(const char *s, int len) { char *p = (char *)malloc(len + 1); memcpy(p, s, len); p[len] = 0; return p; }
void fnet_freeString(const char *s) { free((void *)s); }

static fnet_dev_detail_t *make_detail(const char *id) {
    fnet_dev_detail_t *d = (fnet_dev_detail_t *)calloc(1, sizeof *d);
    d->devId = D(id); d->pid = 42; d->nozzleCnt = 2; d->name = D("Fake Printer"); d->status = D("printing");
    d->ipAddr = D("10.0.0.7"); d->macAddr = D("aa:bb:cc"); d->jobId = D("job-9"); d->printFileName = D("benchy.gcode");
    d->printFileThumbUrl = D("http://t/b.png"); d->printLayer = 12; d->targetPrintLayer = 340; d->printProgress = 0.375;
    d->coordinate[0] = 1.5; d->coordinate[1] = 2.5; d->coordinate[2] = 3.5;
    d->rightTemp = 210.5; d->leftTemp = 0; d->platTemp = 60.25; d->chamberTemp = 35;
    d->nozzleTemps = (double *)malloc(2 * sizeof(double)); d->nozzleTemps[0] = 200; d->nozzleTemps[1] = 201;
    d->nozzleTargetTemps = (double *)malloc(2 * sizeof(double)); d->nozzleTargetTemps[0] = 210; d->nozzleTargetTemps[1] = 211;
    d->internalFanStatus = D("open"); d->externalFanStatus = D("close"); d->clearFanStatus = D("close");
    d->doorStatus = D("close"); d->lightStatus = D("open"); d->cameraStreamUrl = D("rtmp://cam/1"); d->errorCode = nullptr;
    d->hasMatlStation = 1;
    d->matlStationInfo.slotCnt = 3; d->matlStationInfo.currentSlot = 1; d->matlStationInfo.currentLoadSlot = 2;
    d->matlStationInfo.stateAction = 4; d->matlStationInfo.stateStep = 6;
    d->matlStationInfo.slotInfos = (fnet_matl_slot_info_t *)calloc(3, sizeof(fnet_matl_slot_info_t));
    for (int i = 0; i < 3; i++) {
        d->matlStationInfo.slotInfos[i].slotId = i + 1; d->matlStationInfo.slotInfos[i].hasFilament = i != 1;
        char b[32]; snprintf(b, sizeof b, "PLA-%d", i); d->matlStationInfo.slotInfos[i].materialName = D(b);
        snprintf(b, sizeof b, "#00%d%d%d%d", i, i, i, i); d->matlStationInfo.slotInfos[i].materialColor = D(b);
    }
    d->indepMatlInfo.stateAction = 1; d->indepMatlInfo.stateStep = 2;
    d->indepMatlInfo.materialName = D("PETG"); d->indepMatlInfo.materialColor = D("#ff00ff");
    d->cumulativePrintTime = 1234.5; d->hasCumulativePrintTimeSecond = 1; d->cumulativePrintTimeSecond = 74070;
    return d;
}
static void free_detail(const fnet_dev_detail_t *cd) {
    fnet_dev_detail_t *d = (fnet_dev_detail_t *)cd;
    if (!d) return;
    free(d->devId); free(d->name); free(d->status); free(d->ipAddr); free(d->macAddr); free(d->jobId); free(d->printFileName);
    free(d->printFileThumbUrl); free(d->nozzleTemps); free(d->nozzleTargetTemps); free(d->internalFanStatus); free(d->externalFanStatus);
    free(d->clearFanStatus); free(d->doorStatus); free(d->lightStatus); free(d->cameraStreamUrl);
    for (int i = 0; i < d->matlStationInfo.slotCnt; i++) { free(d->matlStationInfo.slotInfos[i].materialName); free(d->matlStationInfo.slotInfos[i].materialColor); }
    free(d->matlStationInfo.slotInfos); free(d->indepMatlInfo.materialName); free(d->indepMatlInfo.materialColor);
    free(d);
}
void fnet_freeDevDetail(const fnet_dev_detail_t *d) { free_detail(d); }

int fnet_getLanDevDetail(const char *ip, unsigned short port, const char *sn, const char *chk, fnet_dev_detail_t **out, int to) {
    P("[plugin] getLanDevDetail ip=%s port=%u sn=%s chk=%s to=%d\n", S(ip), port, S(sn), S(chk), to);
    *out = make_detail("dev-lan");
    return 0;
}
int fnet_getLanDevProduct(const char *ip, unsigned short port, const char *sn, const char *chk, fnet_dev_product_t **out, int to) {
    (void)ip; (void)port; (void)sn; (void)chk; (void)to;
    *out = (fnet_dev_product_t *)calloc(1, sizeof(fnet_dev_product_t));
    (*out)->nozzleTempCtrlState = 1; (*out)->lightCtrlState = 1; (*out)->externalFanCtrlState = 1;
    return 0;
}
void fnet_freeDevProduct(fnet_dev_product_t *p) { free(p); }

int fnet_getLanDevList(fnet_lan_dev_info_t **infos, int *cnt, int ms) {
    (void)ms;
    *cnt = 2;
    *infos = (fnet_lan_dev_info_t *)calloc(2, sizeof(fnet_lan_dev_info_t));
    strcpy((*infos)[0].serialNumber, "SN-A"); strcpy((*infos)[0].name, "Alpha"); strcpy((*infos)[0].ip, "192.168.1.10"); (*infos)[0].port = 8898; (*infos)[0].pid = 0x23;
    strcpy((*infos)[1].serialNumber, "SN-B"); strcpy((*infos)[1].name, "Beta"); strcpy((*infos)[1].ip, "192.168.1.11"); (*infos)[1].port = 8899; (*infos)[1].bindStatus = 1;
    return 0;
}
void fnet_freeLanDevInfos(fnet_lan_dev_info_t *i) { free(i); }

int fnet_getLanDevGcodeList(const char *ip, unsigned short port, const char *sn, const char *chk, fnet_gcode_data_t **out, int *cnt, int to) {
    (void)ip; (void)port; (void)sn; (void)chk; (void)to;
    *cnt = 2;
    fnet_gcode_data_t *g = (fnet_gcode_data_t *)calloc(2, sizeof(fnet_gcode_data_t));
    g[0].fileName = D("a.gcode"); g[0].thumbUrl = D("http://x/a.png"); g[0].printingTime = 3600; g[0].totalFilamentWeight = 12.5;
    g[0].useMatlStation = 1; g[0].gcodeToolCnt = 2;
    g[0].gcodeToolDatas = (fnet_gcode_tool_data_t *)calloc(2, sizeof(fnet_gcode_tool_data_t));
    g[0].gcodeToolDatas[0].toolId = 0; g[0].gcodeToolDatas[0].slotId = 1; g[0].gcodeToolDatas[0].materialName = D("PLA"); g[0].gcodeToolDatas[0].materialColor = D("#fff"); g[0].gcodeToolDatas[0].filemanetWeight = 7.25;
    g[0].gcodeToolDatas[1].toolId = 1; g[0].gcodeToolDatas[1].slotId = 3; g[0].gcodeToolDatas[1].materialName = D("TPU"); g[0].gcodeToolDatas[1].materialColor = D("#000"); g[0].gcodeToolDatas[1].filemanetWeight = 5.25;
    g[1].fileName = D("b.gcode"); g[1].printingTime = 10;
    *out = g;
    return 0;
}
void fnet_freeGcodeList(fnet_gcode_data_t *g, int n) {
    for (int i = 0; i < n; i++) {
        free(g[i].fileName); free(g[i].thumbUrl);
        for (int k = 0; k < g[i].gcodeToolCnt; k++) { free(g[i].gcodeToolDatas[k].materialName); free(g[i].gcodeToolDatas[k].materialColor); }
        free(g[i].gcodeToolDatas);
    }
    free(g);
}

int fnet_getWanDevList(const char *cid, const char *tok, long long appId, long long platId, fnet_wan_dev_info_t **out, int *cnt, int to) {
    P("[plugin] getWanDevList cid=%s tok=%s app=%lld plat=%lld to=%d\n", S(cid), S(tok), appId, platId, to);
    *cnt = 2;
    fnet_wan_dev_info_t *w = (fnet_wan_dev_info_t *)calloc(2, sizeof(fnet_wan_dev_info_t));
    w[0].devId = D("w1"); w[0].name = D("Wan One"); w[0].model = D("AD5X"); w[0].status = D("ready"); w[0].serialNumber = D("SNW1"); w[0].devTopic = D("t/w1");
    w[0].updateInfo.status = D("new"); w[0].updateInfo.title = D("Update"); w[0].updateInfo.content = D("fixes"); w[0].updateInfo.tips = D("reboot");
    w[1].devId = D("w2"); w[1].name = D("Wan Two");
    *out = w;
    return 0;
}
void fnet_freeWanDevList(fnet_wan_dev_info_t *w, int n) {
    for (int i = 0; i < n; i++) {
        free(w[i].devId); free(w[i].name); free(w[i].model); free(w[i].imageUrl); free(w[i].status); free(w[i].location);
        free(w[i].serialNumber); free(w[i].devTopic); free(w[i].updateInfo.status); free(w[i].updateInfo.title);
        free(w[i].updateInfo.content); free(w[i].updateInfo.tips);
    }
    free(w);
}

int fnet_getTokenByPassword(const char *user, const char *pass, const char *lang, fnet_token_data_t **tok, char **msg, int to) {
    P("[plugin] getTokenByPassword user=%s pass=%s lang=%s to=%d\n", S(user), S(pass), S(lang), to);
    if (pass && !strcmp(pass, "ok")) {
        *tok = (fnet_token_data_t *)calloc(1, sizeof(fnet_token_data_t));
        (*tok)->expiresIn = 3600; (*tok)->accessToken = D("ACCESS"); (*tok)->refreshToken = D("REFRESH");
        *msg = D("welcome");
        return 0;
    }
    *msg = D("bad password");
    return FNET_INVALID_VALIDATION;
}
void fnet_freeToken(fnet_token_data_t *t) { if (t) { free(t->accessToken); free(t->refreshToken); free(t); } }

int fnet_lanDevStartJob(const char *ip, unsigned short port, const char *sn, const char *chk, const fnet_local_job_data_t *j, int to) {
    P("[plugin] lanDevStartJob ip=%s port=%u sn=%s chk=%s to=%d\n", S(ip), port, S(sn), S(chk), to);
    dump_local_job("  ", j);
    return 0;
}
int fnet_ctrlLanDevTemp(const char *ip, unsigned short port, const char *sn, const char *chk, const fnet_temp_ctrl_t *t, int to) {
    (void)ip; (void)port; (void)sn; (void)chk; (void)to;
    dump_temp("  [plugin] ", t);
    return 0;
}
int fnet_ctrlLanDevHoming(const char *ip, unsigned short port, const char *sn, const char *chk, int to) {
    P("[plugin] homing %s:%u %s %s %d\n", S(ip), port, S(sn), S(chk), to);
    return 0;
}

int fnet_lanDevSendGcode(const char *ip, unsigned short port, const char *sn, const char *chk, const fnet_send_gcode_data_t *s, int to) {
    P("[plugin] sendGcode ip=%s port=%u sn=%s chk=%s to=%d file=%s thumb=%s dst=%s now=%d level=%d flow=%d first=%d lapse=%d station=%d tools=%d\n", S(ip), port, S(sn), S(chk), to, S(s->gcodeFilePath), S(s->thumbFilePath), S(s->gcodeDstName), s->printNow, s->levelingBeforePrint, s->flowCalibration, s->firstLayerInspection, s->timeLapseVideo, s->useMatlStation, s->gcodeToolCnt);
    dump_mapping("  ", s->materialMappings, s->gcodeToolCnt);
    for (int i = 0; i <= 4; i++) {
        int r = s->callback ? s->callback(i * 25, 100, s->callbackData) : 0;
        if (r) { P("[plugin] sendGcode aborted at %d\n", i * 25); return FNET_ABORTED_BY_CALLBACK; }
    }
    return 0;
}

int fnet_downloadFileMem(const char *url, fnet_file_data_t **fd, fnet_progress_callback_t cb, void *data, int ct, int to) {
    P("[plugin] downloadFileMem url=%s ct=%d to=%d\n", S(url), ct, to);
    if (cb) cb(1, 5, data);
    *fd = (fnet_file_data_t *)calloc(1, sizeof(fnet_file_data_t));
    (*fd)->data = (char *)malloc(5); memcpy((*fd)->data, "ab\0cd", 5); (*fd)->size = 5;
    return 0;
}
void fnet_freeFileData(fnet_file_data_t *fd) { if (fd) { free(fd->data); free(fd); } }

int fnet_getSettingList2(const char *cid, const char *tok, const char *bv, fnet_setting_check_callback_t chk, void *chkd,
    fnet_setting_progress_callback_t prog, void *progd, fnet_setting_cancel_callback_t cancel, void *cancd, int to) {
    P("[plugin] getSettingList2 cid=%s tok=%s bv=%s to=%d\n", S(cid), S(tok), S(bv), to);
    int r = chk("{\"setting_id\":\"s1\",\"name\":\"x\"}", chkd);
    P("[plugin] check cb returned %d\n", r);
    prog(50, progd); prog(100, progd);
    P("[plugin] cancel cb returned %d\n", cancel(cancd));
    return 0;
}

int fnet_getMqttConfig(const char *cid, const char *tok, fnet_mqtt_config_t **out, int to) {
    (void)cid; (void)tok; (void)to;
    *out = (fnet_mqtt_config_t *)calloc(1, sizeof(fnet_mqtt_config_t));
    (*out)->userTopic = D("u/top"); (*out)->commonTopicCnt = 3;
    (*out)->commonTopics = (char **)calloc(3, sizeof(char *));
    (*out)->commonTopics[0] = D("c1"); (*out)->commonTopics[1] = D("c2"); (*out)->commonTopics[2] = D("c3");
    return 0;
}
void fnet_freeMqttConfig(fnet_mqtt_config_t *c) {
    if (!c) return;
    free(c->userTopic);
    for (int i = 0; i < c->commonTopicCnt; i++) free(c->commonTopics[i]);
    free(c->commonTopics); free(c);
}

int fnet_deleteTimeLapseVideo(const char *cid, const char *tok, const char **ids, int n, int to) {
    P("[plugin] deleteTimeLapseVideo cid=%s tok=%s n=%d to=%d\n", S(cid), S(tok), n, to);
    for (int i = 0; i < n; i++) P("  id[%d]=%s\n", i, S(ids[i]));
    return 0;
}
int fnet_reportTrackingDataBatch(const char *cid, const fnet_tracking_common_data_t *c, const fnet_tracking_event_data_t *e, int n, int to) {
    P("[plugin] tracking cid=%s uid=%s did=%s n=%d to=%d\n", S(cid), S(c->uid), S(c->did), n, to);
    for (int i = 0; i < n; i++) P("  ev[%d] type=%s ext=%s ts=%s\n", i, S(e[i].eventType), S(e[i].extend), S(e[i].timestamp));
    return 0;
}

int fnet_getSystemMessage(const char *cid, const char *tok, const char *lang, fnet_sys_msg_data_t **out, int to) {
    (void)cid; (void)tok; (void)lang; (void)to;
    *out = (fnet_sys_msg_data_t *)calloc(1, sizeof(fnet_sys_msg_data_t));
    (*out)->id = 7; (*out)->messageId = 70; (*out)->notifyType = D("info"); (*out)->title = D("Hello"); (*out)->content = D("World");
    (*out)->linkType = 1; (*out)->linkUrl = D("http://l"); (*out)->duration = 5; (*out)->showCount = 2;
    return 0;
}
void fnet_freeSystemMessage(fnet_sys_msg_data_t *m) {
    if (!m) return;
    free((void *)m->notifyType); free((void *)m->title); free((void *)m->content); free((void *)m->linkUrl); free(m);
}

int fnet_wanDevAddCloundJob(const char *cid, const char *tok, const fnet_clound_job_data_t *j, fnet_add_clound_job_result_t **out, int *cnt, int to) {
    P("[plugin] addCloundJob cid=%s tok=%s to=%d\n", S(cid), S(tok), to);
    dump_cloud_job("  ", j);
    *cnt = 2;
    *out = (fnet_add_clound_job_result_t *)calloc(2, sizeof(fnet_add_clound_job_result_t));
    (*out)[0].error = FNET_ADD_CLOUND_JOB_OK; (*out)[0].devId = D("d0"); (*out)[0].jobId = D("j0");
    (*out)[1].error = FNET_ADD_CLOUND_JOB_DEVICE_BUSY; (*out)[1].devId = D("d1"); (*out)[1].jobId = D("j1");
    return 0;
}
void fnet_freeAddCloudJobResults(fnet_add_clound_job_result_t *r, int n) {
    for (int i = 0; i < n; i++) { free(r[i].devId); free(r[i].jobId); }
    free(r);
}

int fnet_getAiModelJobState(const char *cid, const char *tok, long long jobId, fnet_ai_model_job_state_t **out, int to) {
    P("[plugin] aiModelJobState cid=%s tok=%s job=%lld to=%d\n", S(cid), S(tok), jobId, to);
    *out = (fnet_ai_model_job_state_t *)calloc(1, sizeof(fnet_ai_model_job_state_t));
    (*out)->status = 3; (*out)->jobId = jobId; (*out)->posInQueue = 1; (*out)->queueLength = 4; (*out)->modelCnt = 2;
    (*out)->models = (fnet_ai_model_data_t *)calloc(2, sizeof(fnet_ai_model_data_t));
    (*out)->models[0].modelType = D("stl"); (*out)->models[0].modelUrl = D("http://m/0");
    (*out)->models[1].modelType = D("3mf"); (*out)->models[1].modelUrl = D("http://m/1");
    (*out)->externalJobId = D("ext-1");
    return 0;
}
void fnet_freeAiModelJobState(fnet_ai_model_job_state_t *s) {
    if (!s) return;
    for (int i = 0; i < s->modelCnt; i++) { free((void *)s->models[i].modelType); free((void *)s->models[i].modelUrl); }
    free(s->models); free((void *)s->externalJobId); free(s);
}

// ---- connection
struct Conn {
    fnet_conn_settings_t s;
    std::atomic<bool> stop{false};
    std::thread th;
};

static void send_reads(Conn *c) {
    for (int t = 0; t <= (int)FNET_CONN_READ_UPLOAD_LOG; t++) {
        fnet_conn_read_data_t rd;
        memset(&rd, 0, sizeof rd);
        rd.type = (fnet_conn_read_data_type_t)t;
        char tb[32]; snprintf(tb, sizeof tb, "topic/%d", t);
        rd.topic = D(tb);
        switch (t) {
        case FNET_CONN_READ_SYS_NOTIFY: rd.data = D("sys notify payload"); break;
        case FNET_CONN_READ_UPDATE_NOTIFY: rd.data = D("update notify payload"); break;
        case FNET_CONN_READ_SYNC_LOGIN: { fnet_sync_login_info_t *i = (fnet_sync_login_info_t *)calloc(1, sizeof(fnet_sync_login_info_t)); i->clientType = D("PC"); i->clientId = D("cid-1"); rd.data = i; break; }
        case FNET_CONN_READ_SYNC_BIND_DEVICE: case FNET_CONN_READ_SYNC_UNBIND_DEVICE: { fnet_sync_bind_info_t *i = (fnet_sync_bind_info_t *)calloc(1, sizeof(fnet_sync_bind_info_t)); i->fromClinetId = D("from"); i->devId = D("dev-b"); rd.data = i; break; }
        case FNET_CONN_READ_SYNC_ONLINE: case FNET_CONN_READ_SYNC_OFFLINE: { fnet_sync_online_info_t *i = (fnet_sync_online_info_t *)calloc(1, sizeof(fnet_sync_online_info_t)); i->uid = D("uid-1"); i->devId = D("dev-o"); rd.data = i; break; }
        case FNET_CONN_READ_DEVICE_DETAIL: rd.data = make_detail("dev-mqtt"); break;
        case FNET_CONN_READ_DEVICE_KEEP_ALIVE: rd.data = D("dev-keepalive"); break;
        case FNET_CONN_READ_SLICE_STATE: { fnet_slice_state_t *i = (fnet_slice_state_t *)calloc(1, sizeof(fnet_slice_state_t)); i->id = D("sl-1"); i->deviceId = D("d"); i->status = D("running"); i->waitingCount = 2; i->percentage = 55; i->timestamp = 99; i->fileName = D("f.3mf"); i->thumbImagePath = D("/t.png"); i->weight = 3.5; rd.data = i; break; }
        case FNET_CONN_READ_JOB_DOWNLOAD: case FNET_CONN_READ_JOB_UNZIP: { fnet_job_info_t *i = (fnet_job_info_t *)calloc(1, sizeof(fnet_job_info_t)); i->jobId = D("jb"); i->deviceId = D("dv"); i->status = D("ok"); i->percentage = 80; i->errorCode = nullptr; rd.data = i; break; }
        case FNET_CONN_READ_UPLOAD_LOG: { fnet_upload_log_info_t *i = (fnet_upload_log_info_t *)calloc(1, sizeof(fnet_upload_log_info_t)); i->feedbackId = D("fb-1"); i->clientType = D("PC"); rd.data = i; break; }
        default: break;
        }
        c->s.readCallback(&rd, c->s.readCallbackData);
        // the callee owns rd.data / rd.topic; nothing to free here
    }
}

int fnet_createConnection(void **conn, const fnet_conn_settings_t *s) {
    Conn *c = new Conn;
    c->s = *s;
    std::string cid = s->clientId ? s->clientId : "";
    *conn = c;
    c->th = std::thread([c, cid] {
        c->s.statusCallback(FNET_CONN_STATUS_CONNECTED, c->s.statusCallbackData);
        const char *id = nullptr;
        int r = c->s.updateCallback(&id, c->s.updateCallbackData);
        // report the update result through a read-style status so the driver can print it
        if (r == 0 && id) { fnet_conn_read_data_t rd; memset(&rd, 0, sizeof rd); rd.type = FNET_CONN_READ_SYS_NOTIFY; rd.data = D(id); rd.topic = D("update-callback-result"); free((void *)id); c->s.readCallback(&rd, c->s.readCallbackData); }
        send_reads(c);
        while (!c->stop) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        c->s.statusCallback(FNET_CONN_STATUS_DISCONNECTED, c->s.statusCallbackData);
    });
    (void)cid;
    return 0;
}
void fnet_connectionStop(void *conn) { ((Conn *)conn)->stop = true; }
void fnet_freeConnection(void *conn) { Conn *c = (Conn *)conn; c->stop = true; if (c->th.joinable()) c->th.join(); delete c; }

int fnet_connectionSend(void *conn, const fnet_conn_write_data_t *w) {
    (void)conn;
    P("[plugin] connectionSend topic=%s qos=%d\n", S(w->topic), w->qos);
    dump_write_data("  ", (int)w->type, w->data);
    return 0;
}
int fnet_connectionSendMulti(void *conn, const fnet_conn_write_multi_data_t *w, fnet_conn_write_multi_result_t **res) {
    (void)conn;
    P("[plugin] connectionSendMulti type=%d dataCnt=%d topicCnt=%d qos=%d\n", (int)w->type, w->dataCnt, w->topicCnt, w->qos);
    for (int i = 0; i < w->topicCnt; i++) P("  topic[%d]=%s\n", i, S(w->topics[i]));
    for (int i = 0; i < w->dataCnt; i++) dump_write_data("  ", (int)w->type, w->datas[i]);
    *res = (fnet_conn_write_multi_result_t *)calloc(1, sizeof(fnet_conn_write_multi_result_t));
    (*res)->failedCnt = 2; (*res)->failedIndices = (int *)malloc(2 * sizeof(int)); (*res)->failedIndices[0] = 1; (*res)->failedIndices[1] = 3;
    return 0;
}
void fnet_freeWriteMultiResult(const fnet_conn_write_multi_result_t *r) { if (r) { free(r->failedIndices); free((void *)r); } }
int fnet_connectionSubscribe(void *conn, const fnet_conn_subscribe_data_t *s) {
    (void)conn;
    P("[plugin] subscribe cnt=%d\n", s->topicCnt);
    for (int i = 0; i < s->topicCnt; i++) P("  topic[%d]=%s\n", i, S(s->topics[i]));
    return 0;
}

// frees for the read-callback payloads (the app calls these)
void fnet_freeSyncLoginInfo(const fnet_sync_login_info_t *i) { if (i) { free(i->clientType); free(i->clientId); free((void *)i); } }
void fnet_freeSyncBindInfo(const fnet_sync_bind_info_t *i) { if (i) { free(i->fromClinetId); free(i->devId); free((void *)i); } }
void fnet_freeSyncOnlineInfo(const fnet_sync_online_info_t *i) { if (i) { free(i->uid); free(i->devId); free((void *)i); } }
void fnet_freeSliceState(const fnet_slice_state_t *i) { if (i) { free(i->id); free(i->deviceId); free(i->status); free(i->fileName); free(i->thumbImagePath); free((void *)i); } }
void fnet_freeJobInfo(const fnet_job_info_t *i) { if (i) { free(i->jobId); free(i->deviceId); free(i->status); free(i->errorCode); free((void *)i); } }
void fnet_freeUploadLogInfo(const fnet_upload_log_info_t *i) { if (i) { free(i->feedbackId); free(i->clientType); free((void *)i); } }

}  // extern "C"
