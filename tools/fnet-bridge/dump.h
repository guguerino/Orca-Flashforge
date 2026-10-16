// Deterministic text dumps of FlashNetwork structures, used by the bridge tests on both
// sides of the bridge (what the plugin receives / what the app receives).
#pragma once
#include <cstdio>
#include <string>

#include "FlashNetwork.h"

#define S(x) ((x) ? (x) : "<null>")

static inline void dump_mapping(const char *ind, const fnet_material_mapping_t *m, int n) {
    printf("%smappings(%d)%s\n", ind, n, m ? "" : " <null>");
    for (int i = 0; m && i < n; i++)
        printf("%s  [%d] tool=%d slot=%d name=%s tcol=%s scol=%s\n", ind, i, m[i].toolId, m[i].slotId, S(m[i].materialName), S(m[i].toolMaterialColor), S(m[i].slotMaterialColor));
}

static inline void dump_dev_detail(const char *ind, const fnet_dev_detail_t *d) {
    if (!d) { printf("%sdetail <null>\n", ind); return; }
    printf("%sdetail devId=%s pid=%d nozzleCnt=%d name=%s status=%s ip=%s mac=%s\n", ind, S(d->devId), d->pid, d->nozzleCnt, S(d->name), S(d->status), S(d->ipAddr), S(d->macAddr));
    printf("%s  job=%s file=%s thumb=%s layer=%d/%d progress=%.3f\n", ind, S(d->jobId), S(d->printFileName), S(d->printFileThumbUrl), d->printLayer, d->targetPrintLayer, d->printProgress);
    printf("%s  coord=%.1f,%.1f,%.1f temps r=%.1f l=%.1f plat=%.1f chamber=%.1f\n", ind, d->coordinate[0], d->coordinate[1], d->coordinate[2], d->rightTemp, d->leftTemp, d->platTemp, d->chamberTemp);
    for (int i = 0; d->nozzleTemps && i < d->nozzleCnt; i++) printf("%s  nozzle[%d] temp=%.1f target=%.1f\n", ind, i, d->nozzleTemps[i], d->nozzleTargetTemps ? d->nozzleTargetTemps[i] : -1.0);
    printf("%s  nozzleTemps=%s targets=%s\n", ind, d->nozzleTemps ? "set" : "null", d->nozzleTargetTemps ? "set" : "null");
    printf("%s  fans int=%s ext=%s clear=%s door=%s light=%s cam=%s err=%s\n", ind, S(d->internalFanStatus), S(d->externalFanStatus), S(d->clearFanStatus), S(d->doorStatus), S(d->lightStatus), S(d->cameraStreamUrl), S(d->errorCode));
    printf("%s  matl hasStation=%d slotCnt=%d cur=%d load=%d action=%d step=%d slots=%s\n", ind, d->hasMatlStation, d->matlStationInfo.slotCnt, d->matlStationInfo.currentSlot, d->matlStationInfo.currentLoadSlot, d->matlStationInfo.stateAction, d->matlStationInfo.stateStep, d->matlStationInfo.slotInfos ? "set" : "null");
    for (int i = 0; d->matlStationInfo.slotInfos && i < d->matlStationInfo.slotCnt; i++) {
        const fnet_matl_slot_info_t &s = d->matlStationInfo.slotInfos[i];
        printf("%s    slot[%d] id=%d has=%d name=%s color=%s\n", ind, i, s.slotId, s.hasFilament, S(s.materialName), S(s.materialColor));
    }
    printf("%s  indep action=%d step=%d name=%s color=%s\n", ind, d->indepMatlInfo.stateAction, d->indepMatlInfo.stateStep, S(d->indepMatlInfo.materialName), S(d->indepMatlInfo.materialColor));
    printf("%s  cumulative=%.1f hasSec=%d sec=%.1f\n", ind, d->cumulativePrintTime, d->hasCumulativePrintTimeSecond, d->cumulativePrintTimeSecond);
}

static inline void dump_gcode_list(const char *ind, const fnet_gcode_data_t *g, int n) {
    printf("%sgcodes(%d)%s\n", ind, n, g ? "" : " <null>");
    for (int i = 0; g && i < n; i++) {
        printf("%s  [%d] file=%s thumb=%s time=%.1f w=%.1f station=%d tools=%d%s\n", ind, i, S(g[i].fileName), S(g[i].thumbUrl), g[i].printingTime, g[i].totalFilamentWeight, g[i].useMatlStation, g[i].gcodeToolCnt, g[i].gcodeToolDatas ? "" : " <null>");
        for (int k = 0; g[i].gcodeToolDatas && k < g[i].gcodeToolCnt; k++) {
            const fnet_gcode_tool_data_t &t = g[i].gcodeToolDatas[k];
            printf("%s      tool[%d] id=%d slot=%d name=%s color=%s w=%.2f\n", ind, k, t.toolId, t.slotId, S(t.materialName), S(t.materialColor), t.filemanetWeight);
        }
    }
}

static inline void dump_wan_list(const char *ind, const fnet_wan_dev_info_t *w, int n) {
    printf("%swan devs(%d)%s\n", ind, n, w ? "" : " <null>");
    for (int i = 0; w && i < n; i++)
        printf("%s  [%d] id=%s name=%s model=%s status=%s loc=%s sn=%s topic=%s upd(%s|%s|%s|%s)\n", ind, i, S(w[i].devId), S(w[i].name), S(w[i].model), S(w[i].status), S(w[i].location), S(w[i].serialNumber), S(w[i].devTopic), S(w[i].updateInfo.status), S(w[i].updateInfo.title), S(w[i].updateInfo.content), S(w[i].updateInfo.tips));
}

static inline void dump_local_job(const char *ind, const fnet_local_job_data_t *j) {
    if (!j) { printf("%slocal job <null>\n", ind); return; }
    printf("%slocal job id=%s thumb=%s file=%s now=%d level=%d flow=%d first=%d lapse=%d station=%d tools=%d\n", ind, S(j->jobId), S(j->thumbUrl), S(j->fileName), j->printNow, j->levelingBeforePrint, j->flowCalibration, j->firstLayerInspection, j->timeLapseVideo, j->useMatlStation, j->gcodeToolCnt);
    dump_mapping(ind, j->materialMappings, j->gcodeToolCnt);
}

static inline void dump_cloud_job(const char *ind, const fnet_clound_job_data_t *j) {
    if (!j) { printf("%scloud job <null>\n", ind); return; }
    printf("%scloud job devCnt=%d gcode=%s(%s,%s,%lld) thumb=%s(%s,%s,%lld) bucket=%s ep=%s plate=%d tools=%d\n", ind, j->devCnt, S(j->gcodeName), S(j->gcodeType), S(j->gcodeMd5), j->gcodeSize, S(j->thumbName), S(j->thumbType), S(j->thumbMd5), j->thumbSize, S(j->bucketName), S(j->endpoint), j->plateNo, j->gcodeToolCnt);
    for (int i = 0; i < j->devCnt; i++)
        printf("%s  dev[%d] id=%s sn=%s job=%s\n", ind, i, S(j->devIds ? j->devIds[i] : nullptr), S(j->devSerialNumbers ? j->devSerialNumbers[i] : nullptr), S(j->jobIds ? j->jobIds[i] : nullptr));
    dump_mapping(ind, j->materialMappings, j->gcodeToolCnt);
}

static inline void dump_temp(const char *ind, const fnet_temp_ctrl_t *t) {
    printf("%stemp plat=%.1f r=%.1f l=%.1f ch=%.1f nozzles(%d)", ind, t->platformTemp, t->rightTemp, t->leftTemp, t->chamberTemp, t->nozzlesCnt);
    for (int i = 0; t->nozzlesTemp && i < t->nozzlesCnt; i++) printf(" %.1f", t->nozzlesTemp[i]);
    printf("\n");
}

static inline void dump_sync_info_login(const char *ind, const fnet_sync_login_info_t *i) { printf("%slogin type=%s id=%s\n", ind, S(i->clientType), S(i->clientId)); }
static inline void dump_sync_info_bind(const char *ind, const fnet_sync_bind_info_t *i) { printf("%sbind from=%s dev=%s\n", ind, S(i->fromClinetId), S(i->devId)); }
static inline void dump_sync_info_online(const char *ind, const fnet_sync_online_info_t *i) { printf("%sonline uid=%s dev=%s\n", ind, S(i->uid), S(i->devId)); }

static inline void dump_read_data(const char *ind, const fnet_conn_read_data_t *r) {
    printf("%sread type=%d topic=%s\n", ind, (int)r->type, S(r->topic));
    switch (r->type) {
    case FNET_CONN_READ_SYS_NOTIFY: case FNET_CONN_READ_UPDATE_NOTIFY: case FNET_CONN_READ_DEVICE_KEEP_ALIVE:
        printf("%s  str=%s\n", ind, S((const char *)r->data)); break;
    case FNET_CONN_READ_SYNC_LOGIN: dump_sync_info_login(ind, (const fnet_sync_login_info_t *)r->data); break;
    case FNET_CONN_READ_SYNC_BIND_DEVICE: case FNET_CONN_READ_SYNC_UNBIND_DEVICE: dump_sync_info_bind(ind, (const fnet_sync_bind_info_t *)r->data); break;
    case FNET_CONN_READ_SYNC_ONLINE: case FNET_CONN_READ_SYNC_OFFLINE: dump_sync_info_online(ind, (const fnet_sync_online_info_t *)r->data); break;
    case FNET_CONN_READ_DEVICE_DETAIL: dump_dev_detail(ind, (const fnet_dev_detail_t *)r->data); break;
    case FNET_CONN_READ_SLICE_STATE: {
        const fnet_slice_state_t *s = (const fnet_slice_state_t *)r->data;
        printf("%s  slice id=%s dev=%s status=%s wait=%d pct=%d ts=%d file=%s thumb=%s w=%.1f\n", ind, S(s->id), S(s->deviceId), S(s->status), s->waitingCount, s->percentage, s->timestamp, S(s->fileName), S(s->thumbImagePath), s->weight);
        break;
    }
    case FNET_CONN_READ_JOB_DOWNLOAD: case FNET_CONN_READ_JOB_UNZIP: {
        const fnet_job_info_t *j = (const fnet_job_info_t *)r->data;
        printf("%s  job id=%s dev=%s status=%s pct=%d err=%s\n", ind, S(j->jobId), S(j->deviceId), S(j->status), j->percentage, S(j->errorCode));
        break;
    }
    case FNET_CONN_READ_UPLOAD_LOG: {
        const fnet_upload_log_info_t *u = (const fnet_upload_log_info_t *)r->data;
        printf("%s  uploadlog fb=%s client=%s\n", ind, S(u->feedbackId), S(u->clientType));
        break;
    }
    default: printf("%s  data=%s\n", ind, r->data ? "set" : "null"); break;
    }
}

static inline void dump_write_data(const char *ind, int type, const void *data) {
    printf("%swrite type=%d ", ind, type);
    switch (type) {
    case FNET_CONN_WRITE_SYNC_LOGIN: printf("login id=%s\n", S((const char *)data)); break;
    case FNET_CONN_WRITE_UPDATE_DETAIL: printf("update-detail data=%s\n", data ? "set" : "null"); break;
    case FNET_CONN_WRITE_START_JOB: printf("\n"); dump_local_job(ind, (const fnet_local_job_data_t *)data); break;
    case FNET_CONN_WRITE_START_CLOUND_JOB: printf("\n"); dump_cloud_job(ind, (const fnet_clound_job_data_t *)data); break;
    case FNET_CONN_WRITE_TEMP_CTRL: printf("\n"); dump_temp(ind, (const fnet_temp_ctrl_t *)data); break;
    case FNET_CONN_WRITE_LIGHT_CTRL: printf("light=%s\n", S(((const fnet_light_ctrl_t *)data)->lightStatus)); break;
    case FNET_CONN_WRITE_AIR_FILTER_CTRL: printf("air int=%s ext=%s\n", S(((const fnet_air_filter_ctrl_t *)data)->internalFanStatus), S(((const fnet_air_filter_ctrl_t *)data)->externalFanStatus)); break;
    case FNET_CONN_WRITE_MOVE_CTRL: printf("move %s %.2f\n", S(((const fnet_move_ctrl_t *)data)->axis), ((const fnet_move_ctrl_t *)data)->delta); break;
    case FNET_CONN_WRITE_JOB_CTRL: printf("job %s %s\n", S(((const fnet_job_ctrl_t *)data)->jobId), S(((const fnet_job_ctrl_t *)data)->action)); break;
    case FNET_CONN_WRITE_MATL_STATION_CONFIG: printf("matl cfg slot=%d %s %s\n", ((const fnet_matl_station_config_t *)data)->slotId, S(((const fnet_matl_station_config_t *)data)->materialName), S(((const fnet_matl_station_config_t *)data)->materialColor)); break;
    default: printf("data=%s\n", data ? "set" : "null"); break;
    }
}
