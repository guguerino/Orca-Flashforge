#!/usr/bin/env python3
"""Generates the marshalling code of the FlashNetwork arm64<->x86_64 bridge
from FlashNetwork.h.  Outputs three include files:
  gen_common.inc  type descriptors + function ids   (shim and helper)
  gen_shim.inc    exported fnet_* wrappers          (arm64 shim)
  gen_helper.inc  call dispatch to the real plugin  (x86_64 helper)
"""
import re, sys, os

HDR = sys.argv[1]
OUT = sys.argv[2]

src = open(HDR, encoding='utf-8').read()


def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    return s


clean = strip_comments(src)

# ---------------------------------------------------------------- structs
pack = clean[clean.index('#pragma pack(push, 8)'):clean.index('#pragma pack(pop)')]
STRUCTS = []          # ordered list of (typedef_name, [fields])
for m in re.finditer(r'typedef\s+struct\s+\w+\s*\{(.*?)\}\s*(\w+)\s*;', pack, flags=re.S):
    body, tname = m.group(1), m.group(2)
    fields = []
    for stmt in body.split(';'):
        stmt = stmt.strip()
        if not stmt:
            continue
        stmt = re.sub(r'\s+', ' ', stmt)
        mm = re.match(r'^(.*?)([A-Za-z_]\w*)(\[[^\]]*\])?$', stmt)
        ctype = mm.group(1).strip()
        name = mm.group(2)
        arr = mm.group(3)
        ctype = ctype.replace(' *', '*').replace('* ', '*')
        fields.append(dict(ctype=ctype, name=name, arr=arr))
    STRUCTS.append((tname, fields))
SDICT = dict(STRUCTS)

# ----------------------------------------------------------------- enums
def parse_enum(name):
    m = re.search(r'typedef\s+enum\s+\w+\s*\{([^}]*)\}\s*' + name + r'\s*;', clean, flags=re.S)
    vals = []
    for item in m.group(1).split(','):
        item = item.strip()
        if item:
            vals.append(item.split('=')[0].strip())
    return vals


WRITE_ENUM = parse_enum('fnet_conn_write_data_type_t')
READ_ENUM = parse_enum('fnet_conn_read_data_type_t')

W = lambda t: 'fnet_%s_t' % t
WRITE_MAP = {
    'FNET_CONN_WRITE_SYNC_LOGIN': 'str',
    'FNET_CONN_WRITE_UPDATE_DETAIL': 'none',
    'FNET_CONN_WRITE_START_JOB': W('local_job_data'),
    'FNET_CONN_WRITE_START_CLOUND_JOB': W('clound_job_data'),
    'FNET_CONN_WRITE_TEMP_CTRL': W('temp_ctrl'),
    'FNET_CONN_WRITE_LIGHT_CTRL': W('light_ctrl'),
    'FNET_CONN_WRITE_AIR_FILTER_CTRL': W('air_filter_ctrl'),
    'FNET_CONN_WRITE_CLEAR_FAN_CTRL': W('clear_fan_ctrl'),
    'FNET_CONN_WRITE_MOVE_CTRL': W('move_ctrl'),
    'FNET_CONN_WRITE_EXTRUDE_CTRL': W('extrude_ctrl'),
    'FNET_CONN_WRITE_HOMING_CTRL': 'none',
    'FNET_CONN_WRITE_MATL_STATION_CTRL': W('matl_station_ctrl'),
    'FNET_CONN_WRITE_INDEP_MATL_CTRL': W('indep_matl_ctrl'),
    'FNET_CONN_WRITE_PRINT_CTRL': W('print_ctrl'),
    'FNET_CONN_WRITE_JOB_CTRL': W('job_ctrl'),
    'FNET_CONN_WRITE_STATE_CTRL': W('state_ctrl'),
    'FNET_CONN_WRITE_ERROR_CODE_CTRL': W('error_code_ctrl'),
    'FNET_CONN_WRITE_PLATE_DETECT_CTRL': W('plate_detect_ctrl'),
    'FNET_CONN_WRITE_FIRST_LAYER_DETECT_CTRL': W('first_layer_detect_ctrl'),
    'FNET_CONN_WRITE_CAMERA_STREAM_CTRL': W('camera_stream_ctrl'),
    'FNET_CONN_WRITE_MATL_STATION_CONFIG': W('matl_station_config'),
    'FNET_CONN_WRITE_INDEP_MATL_CONFIG': W('indep_matl_config'),
}
READ_MAP = {
    'FNET_CONN_READ_SYS_NOTIFY': 'str',
    'FNET_CONN_READ_UPDATE_NOTIFY': 'str',
    'FNET_CONN_READ_SYNC_USER_PROFILE': 'none',
    'FNET_CONN_READ_SYNC_UNREGISTER_USER': 'none',
    'FNET_CONN_READ_SYNC_LOGIN': W('sync_login_info'),
    'FNET_CONN_READ_SYNC_BIND_DEVICE': W('sync_bind_info'),
    'FNET_CONN_READ_SYNC_UNBIND_DEVICE': W('sync_bind_info'),
    'FNET_CONN_READ_SYNC_ONLINE': W('sync_online_info'),
    'FNET_CONN_READ_SYNC_OFFLINE': W('sync_online_info'),
    'FNET_CONN_READ_DEVICE_DETAIL': W('dev_detail'),
    'FNET_CONN_READ_DEVICE_KEEP_ALIVE': 'str',
    'FNET_CONN_READ_SLICE_STATE': W('slice_state'),
    'FNET_CONN_READ_JOB_DOWNLOAD': W('job_info'),
    'FNET_CONN_READ_JOB_UNZIP': W('job_info'),
    'FNET_CONN_READ_UPLOAD_LOG': W('upload_log_info'),
}
for e in WRITE_ENUM:
    assert e in WRITE_MAP, e
for e in READ_ENUM:
    assert e in READ_MAP, e
VDMAPS = {'WRITE': [WRITE_MAP[e] for e in WRITE_ENUM], 'READ': [READ_MAP[e] for e in READ_ENUM]}
for v in VDMAPS.values():
    for t in v:
        assert t in ('str', 'none') or t in SDICT, t

# ------------------------------------------------- manual pointer specs
SPEC = {
    'fnet_send_gcode_data_t': {'materialMappings': ('arr', 'fnet_material_mapping_t', 'gcodeToolCnt')},
    'fnet_clound_job_data_t': {
        'devIds': ('strarr', 'devCnt'), 'devSerialNumbers': ('strarr', 'devCnt'),
        'jobIds': ('strarr', 'devCnt'),
        'materialMappings': ('arr', 'fnet_material_mapping_t', 'gcodeToolCnt')},
    'fnet_local_job_data_t': {'materialMappings': ('arr', 'fnet_material_mapping_t', 'gcodeToolCnt')},
    'fnet_conn_write_data_t': {'data': ('vd', 'WRITE', 'type')},
    'fnet_conn_write_multi_data_t': {
        'datas': ('vdarr', 'WRITE', 'type', 'dataCnt'), 'topics': ('strarr', 'topicCnt')},
    'fnet_conn_subscribe_data_t': {'topics': ('strarr', 'topicCnt')},
    'fnet_temp_ctrl_t': {'nozzlesTemp': ('dblarr', 'nozzlesCnt')},
    'fnet_dev_detail_t': {
        'nozzleTemps': ('dblarr', 'nozzleCnt'), 'nozzleTargetTemps': ('dblarr', 'nozzleCnt')},
    'fnet_matl_station_info_t': {'slotInfos': ('arr', 'fnet_matl_slot_info_t', 'slotCnt')},
    'fnet_gcode_data_t': {'gcodeToolDatas': ('arr', 'fnet_gcode_tool_data_t', 'gcodeToolCnt')},
    'fnet_ai_model_job_state_t': {'models': ('arr', 'fnet_ai_model_data_t', 'modelCnt')},
    'fnet_ai_general_job_state_t': {'datas': ('arr', 'fnet_ai_general_job_data_t', 'dataCnt')},
    'fnet_mqtt_config_t': {'commonTopics': ('strarr', 'commonTopicCnt')},
    'fnet_conn_write_multi_result_t': {'failedIndices': ('intarr', 'failedCnt')},
    'fnet_file_data_t': {'data': ('bytes', 'size')},
    'fnet_conn_read_data_t': {'data': ('vd', 'READ', 'type')},
}

CB_KINDS = {
    'fnet_progress_callback_t': 'PROGRESS',
    'fnet_setting_check_callback_t': 'SETTING_CHECK',
    'fnet_setting_progress_callback_t': 'SETTING_PROGRESS',
    'fnet_setting_cancel_callback_t': 'SETTING_CANCEL',
    'fnet_conn_status_callback_t': 'CONN_STATUS',
    'fnet_conn_update_callback_t': 'CONN_UPDATE',
    'fnet_conn_read_callback_t': 'CONN_READ',
}
CB_ORDER = list(CB_KINDS.values())

STR_TYPES = ('const char*', 'char*')


def has_ptrs(tname):
    for f in SDICT[tname]:
        if '*' in f['ctype'] or f['ctype'] in CB_KINDS:
            return True
        if f['ctype'] in SDICT and has_ptrs(f['ctype']):
            return True
    return False


def field_desc(tname):
    """Return a list of C++ Field initialisers for a struct type."""
    out = []
    fields = SDICT[tname]
    names = [f['name'] for f in fields]
    spec = SPEC.get(tname, {})
    used = set()
    for i, f in enumerate(fields):
        ct, nm = f['ctype'], f['name']
        off = 'offsetof(%s, %s)' % (tname, nm)
        if ct in CB_KINDS:
            nxt = fields[i + 1]
            assert nxt['ctype'] == 'void*', (tname, nm)
            out.append('{FK_CB, %s, 0, 0, 0, offsetof(%s, %s), CB_%s}' % (off, tname, nxt['name'], CB_KINDS[ct]))
            used.add(nxt['name'])
            continue
        if nm in used:
            continue
        if nm in spec:
            s = spec[nm]
            k = s[0]
            if k == 'arr':
                out.append('{FK_ARR, %s, offsetof(%s, %s), TY_%s, sizeof(%s), 0, 0}' % (off, tname, s[2], s[1], s[1]))
            elif k in ('strarr', 'dblarr', 'intarr', 'bytes'):
                fk = {'strarr': 'FK_STRARR', 'dblarr': 'FK_DBLARR', 'intarr': 'FK_INTARR', 'bytes': 'FK_BYTES'}[k]
                out.append('{%s, %s, offsetof(%s, %s), 0, 0, 0, 0}' % (fk, off, tname, s[1]))
            elif k == 'vd':
                out.append('{FK_VOIDDATA, %s, 0, 0, 0, offsetof(%s, %s), VD_%s}' % (off, tname, s[2], s[1]))
            elif k == 'vdarr':
                out.append('{FK_VOIDARR, %s, offsetof(%s, %s), 0, 0, offsetof(%s, %s), VD_%s}' % (
                    off, tname, s[3], tname, s[2], s[1]))
            continue
        if ct in STR_TYPES:
            out.append('{FK_STR, %s, 0, 0, 0, 0, 0}' % off)
            continue
        if '*' in ct:
            raise SystemExit('unhandled pointer field %s.%s (%s)' % (tname, nm, ct))
        if ct in SDICT and has_ptrs(ct):
            out.append('{FK_INLINE, %s, 0, TY_%s, 0, 0, 0}' % (off, ct))
    for nm in spec:
        assert nm in names, (tname, nm)
    return out


# ------------------------------------------------------------ prototypes
def norm(t):
    t = re.sub(r'\s+', ' ', t.strip())
    t = t.replace(' *', '*').replace('* ', '*')
    return t


FUNCS = []
api_text = clean[clean.index('extern "C" {'):]
for m in re.finditer(r'FNET_API\s+([^;(]+?)\b(fnet_\w+)\s*\(([^)]*)\)\s*;', api_text, flags=re.S):
    ret = norm(m.group(1))
    name = m.group(2)
    plist = [p.strip() for p in m.group(3).split(',') if p.strip()]
    params = []
    for p in plist:
        p = re.sub(r'\s+', ' ', p)
        mm = re.match(r'^(.*?)([A-Za-z_]\w*)$', p)
        params.append(dict(ctype=norm(mm.group(1)), name=mm.group(2)))
    FUNCS.append(dict(name=name, ret=ret, params=params))

# free functions: type -> (name, has_cnt)
FREE_FOR = {}
LOCAL = {'fnet_allocString', 'fnet_freeString'}
for fn in FUNCS:
    n = fn['name']
    if n.startswith('fnet_free') and fn['params'] and n not in LOCAL:
        ct = fn['params'][0]['ctype']
        m = re.match(r'^(?:const )?(fnet_\w+_t)\*$', ct)
        if m:
            fn['local_free'] = True
            fn['free_type'] = m.group(1)
            fn['free_cnt'] = len(fn['params']) == 2
            FREE_FOR[m.group(1)] = (n, fn['free_cnt'])

SCALARS = {'int', 'unsigned short', 'unsigned int', 'long long'}


def classify(fn):
    ps = fn['params']
    res = []
    i = 0
    while i < len(ps):
        p = ps[i]
        ct, nm = p['ctype'], p['name']
        nxt = ps[i + 1] if i + 1 < len(ps) else None
        mm_struct = re.match(r'^(?:const )?(fnet_\w+_t)\*$', ct)
        mm_sout = re.match(r'^(fnet_\w+_t)\*\*$', ct)
        if ct in CB_KINDS:
            assert nxt and nxt['ctype'] == 'void*'
            res.append(dict(kind='CB', name=nm, cb=CB_KINDS[ct], cbtype=ct, data=nxt['name']))
            i += 2
            continue
        if ct == 'const char*':
            res.append(dict(kind='STR_IN', name=nm))
        elif ct == 'char**':
            res.append(dict(kind='STR_OUT', name=nm))
        elif ct == 'const char**' and nxt and nxt['ctype'] == 'int' and nxt['name'].endswith('Cnt'):
            res.append(dict(kind='STRARR_IN', name=nm, cnt=nxt['name']))
        elif mm_sout:
            if nxt and nxt['ctype'] == 'int*' and nxt['name'].endswith('Cnt'):
                res.append(dict(kind='ARR_OUT', name=nm, type=mm_sout.group(1), cnt=nxt['name']))
            else:
                res.append(dict(kind='STRUCT_OUT', name=nm, type=mm_sout.group(1)))
        elif mm_struct and nxt and nxt['ctype'] == 'int' and nxt['name'].endswith('Cnt'):
            res.append(dict(kind='ARR_IN', name=nm, type=mm_struct.group(1), cnt=nxt['name']))
        elif mm_struct:
            res.append(dict(kind='STRUCT_IN', name=nm, type=mm_struct.group(1)))
        elif ct in ('int*', 'unsigned int*'):
            res.append(dict(kind='INT_OUT', name=nm, ctype=ct[:-1]))
        elif ct == 'void**':
            res.append(dict(kind='HANDLE_OUT', name=nm))
        elif ct == 'void*':
            res.append(dict(kind='HANDLE_IN', name=nm))
        elif ct in SCALARS:
            res.append(dict(kind='SCALAR', name=nm, ctype=ct))
        else:
            raise SystemExit('cannot classify %s %s in %s' % (ct, nm, fn['name']))
        i += 1
    return res


FWD = []
for fn in FUNCS:
    if fn['name'] in LOCAL or fn.get('local_free'):
        continue
    fn['cls'] = classify(fn)
    FWD.append(fn)

# ---------------------------------------------------------- gen_common.inc
c = []
c.append('// GENERATED by gen.py - do not edit\nnamespace fb {\n')
c.append('enum { ' + ', '.join('TY_%s' % t for t, _ in STRUCTS) + ', TY_COUNT };')
c.append('enum { ' + ', '.join('CB_%s' % k for k in CB_ORDER) + ', CB_COUNT };')
c.append('enum { VD_WRITE, VD_READ };')
fid_names = [f['name'] for f in FWD]
c.append('enum { ' + ', '.join('FID_%s' % n for n in fid_names) + ', FID_COUNT };')
c.append('static const char *const FID_NAMES[] = {' + ', '.join('"%s"' % n for n in fid_names) + '};')
for t, _ in STRUCTS:
    fd = field_desc(t)
    if fd:
        c.append('static const Field F_%s[] = {%s};' % (t, ', '.join(fd)))
tdesc = []
for t, _ in STRUCTS:
    fd = field_desc(t)
    tdesc.append('{"%s", sizeof(%s), %s, %d}' % (t, t, ('F_%s' % t) if fd else 'nullptr', len(fd)))
c.append('const TypeDesc TYPES[TY_COUNT] = {%s};' % ',\n  '.join(tdesc))
for nm, lst in VDMAPS.items():
    ents = []
    for t in lst:
        if t == 'none':
            ents.append('{0, 0}')
        elif t == 'str':
            ents.append('{1, 0}')
        else:
            ents.append('{2, TY_%s}' % t)
    c.append('static const VdEntry VDT_%s[] = {%s};' % (nm, ', '.join(ents)))
c.append('const VdEntry *const VD[2] = {VDT_WRITE, VDT_READ};')
c.append('const int VD_N[2] = {%d, %d};' % (len(VDMAPS['WRITE']), len(VDMAPS['READ'])))
c.append('}  // namespace fb')
open(os.path.join(OUT, 'gen_common.inc'), 'w').write('\n'.join(c) + '\n')

# ------------------------------------------------------------ gen_shim.inc
def cdecl_params(fn):
    return ', '.join('%s %s' % (p['ctype'], p['name']) if '**' not in p['ctype'] else '%s %s' % (p['ctype'], p['name'])
                     for p in fn['params']) or 'void'


def fix_decl(ct, nm):
    return '%s %s' % (ct, nm)


s = ['// GENERATED by gen.py - do not edit\n']
for fn in FUNCS:
    if fn.get('local_free'):
        t = fn['free_type']
        p0 = fn['params'][0]
        if fn['free_cnt']:
            s.append('extern "C" void %s(%s %s, int %s) { fb::free_struct_array(TY_%s, (void *)%s, %s); }' % (
                fn['name'], p0['ctype'], p0['name'], fn['params'][1]['name'], t, p0['name'], fn['params'][1]['name']))
        else:
            if t == 'fnet_lan_dev_info_t':
                s.append('extern "C" void %s(%s %s) { free((void *)%s); }' % (fn['name'], p0['ctype'], p0['name'], p0['name']))
            else:
                s.append('extern "C" void %s(%s %s) { fb::free_struct_ptr(TY_%s, (void *)%s); }' % (
                    fn['name'], p0['ctype'], p0['name'], t, p0['name']))
s.append('')
for fn in FWD:
    n = fn['name']
    retc = fn['ret']
    decl = ', '.join('%s %s' % (p['ctype'], p['name']) for p in fn['params'])
    L = []
    L.append('extern "C" %s %s(%s) {' % (retc, n, decl or 'void'))
    L.append('  fb::Buf rq; fb::CbScope scope;')
    for p in fn['cls']:
        k, nm = p['kind'], p['name']
        if k == 'STR_IN':
            L.append('  rq.str(%s);' % nm)
        elif k == 'STRUCT_IN':
            L.append('  fb::enc_struct_ptr(rq, TY_%s, %s);' % (p['type'], nm))
        elif k == 'ARR_IN':
            L.append('  fb::enc_struct_array(rq, TY_%s, %s, %s);' % (p['type'], nm, p['cnt']))
        elif k == 'STRARR_IN':
            L.append('  fb::enc_strarr(rq, %s, %s);' % (nm, p['cnt']))
        elif k == 'SCALAR':
            L.append('  rq.i64((int64_t)%s);' % nm)
        elif k == 'CB':
            L.append('  rq.u64(fb::cb_register(CB_%s, (void *)%s, (void *)%s, scope));' % (p['cb'], nm, p['data']))
        elif k == 'HANDLE_IN':
            L.append('  rq.u64((uint64_t)(uintptr_t)%s);' % nm)
    L.append('  fb::Buf rs;')
    L.append('  if (!fb::bridge_call(FID_%s, rq, rs)) return %s;' % (n, 'FNET_ERROR' if retc == 'int' else ('nullptr' if '*' in retc else '')))
    if retc == 'int':
        L.append('  int ret = (int)rs.ri64();')
    elif '*' in retc:
        L.append('  const char *ret = fb::intern(rs);')
    for p in fn['cls']:
        k, nm = p['kind'], p['name']
        if k == 'STRUCT_OUT':
            L.append('  { void *v = fb::dec_struct_ptr(rs, TY_%s); if (%s) *%s = (%s *)v; else fb::free_struct_ptr(TY_%s, v); }' % (
                p['type'], nm, nm, p['type'], p['type']))
        elif k == 'ARR_OUT':
            L.append('  { int64_t n = 0; void *v = fb::dec_struct_array(rs, TY_%s, &n); if (%s) *%s = (%s *)v; else fb::free_struct_array(TY_%s, v, (int)n); }' % (
                p['type'], nm, nm, p['type'], p['type']))
        elif k == 'STR_OUT':
            L.append('  { char *v = rs.rstr(); if (%s) *%s = v; else free(v); }' % (nm, nm))
        elif k == 'INT_OUT':
            L.append('  { int64_t v = rs.ri64(); if (%s) *%s = (%s)v; }' % (nm, nm, p['ctype']))
        elif k == 'HANDLE_OUT':
            L.append('  { uint64_t v = rs.r64(); if (%s) *%s = (void *)(uintptr_t)v; }' % (nm, nm))
    if n == 'fnet_createConnection':
        L.append('  if (ret == FNET_OK && conn && *conn) scope.keep_for_conn(*conn);')
    if n == 'fnet_freeConnection':
        L.append('  fb::conn_release(conn);')
    if retc == 'int' or '*' in retc:
        L.append('  return ret;')
    L.append('}')
    s.extend(L)
open(os.path.join(OUT, 'gen_shim.inc'), 'w').write('\n'.join(s) + '\n')

# ---------------------------------------------------------- gen_helper.inc
NOFREE_STR = {'fnet_initlize', 'fnet_setUserAgent'}   # plugin may keep these pointers
h = ['// GENERATED by gen.py - do not edit\n']
used_real = set(f['name'] for f in FWD) | {'fnet_allocString', 'fnet_freeString'}
for t, (fname, _) in FREE_FOR.items():
    used_real.add(fname)
for n in sorted(used_real):
    h.append('static decltype(&%s) real_%s;' % (n, n))
h.append('static void load_real(void *lib) {')
h.append('  bool ok = true;')
for n in sorted(used_real):
    h.append('  real_%s = (decltype(&%s))dlsym(lib, "%s"); if (!real_%s) { fprintf(stderr, "helper: missing symbol %s\\n"); ok = false; }' % (n, n, n, n, n))
h.append('  if (!ok) exit(3);')
h.append('}')
# real free of a struct by type (single-pointer free functions only)
h.append('static void real_free_struct(int ty, void *p) {')
h.append('  if (!p) return;')
h.append('  switch (ty) {')
for t, (fname, hascnt) in FREE_FOR.items():
    if not hascnt:
        h.append('  case TY_%s: real_%s((%s *)p); break;' % (t, fname, t))
h.append('  default: break;')
h.append('  }')
h.append('}')

for fn in FWD:
    n = fn['name']
    retc = fn['ret']
    L = []
    L.append('static void call_%s(fb::Buf &in, fb::Buf &out) {' % n)
    L.append('  fb::TlCtx tl;')
    args = []
    cleanup = []
    post = []
    for p in fn['cls']:
        k, nm = p['kind'], p['name']
        v = 'a_' + nm
        if k == 'STR_IN':
            L.append('  char *%s = in.rstr();' % v)
            args.append(v)
            if n not in NOFREE_STR:
                cleanup.append('free(%s);' % v)
        elif k == 'STRUCT_IN':
            L.append('  %s *%s = (%s *)fb::dec_struct_ptr(in, TY_%s);' % (p['type'], v, p['type'], p['type']))
            args.append(v)
            if n != 'fnet_createConnection':
                cleanup.append('fb::free_struct_ptr(TY_%s, %s);' % (p['type'], v))
        elif k == 'ARR_IN':
            L.append('  int64_t n_%s = 0; %s *%s = (%s *)fb::dec_struct_array(in, TY_%s, &n_%s);' % (nm, p['type'], v, p['type'], p['type'], nm))
            args.append(v)
            cleanup.append('fb::free_struct_array(TY_%s, %s, (int)n_%s);' % (p['type'], v, nm))
        elif k == 'STRARR_IN':
            L.append('  int64_t n_%s = 0; char **%s = fb::dec_strarr(in, &n_%s);' % (nm, v, nm))
            args.append('(const char **)' + v)
            cleanup.append('fb::free_strarr(%s, (int)n_%s);' % (v, nm))
        elif k == 'SCALAR':
            L.append('  %s %s = (%s)in.ri64();' % (p['ctype'], v, p['ctype']))
            args.append(v)
        elif k == 'CB':
            L.append('  uint64_t id_%s = in.r64(); void *fn_%s = nullptr, *dt_%s = nullptr;' % (nm, nm, nm))
            L.append('  if (id_%s) fb::dec_cb(CB_%s, id_%s, &fn_%s, &dt_%s);' % (nm, p['cb'], nm, nm, nm))
            args.append('(%s)fn_%s' % (p['cbtype'], nm))
            args.append('dt_%s' % nm)
        elif k == 'HANDLE_IN':
            L.append('  void *%s = (void *)(uintptr_t)in.r64();' % v)
            args.append(v)
        elif k == 'STRUCT_OUT':
            L.append('  %s *%s = nullptr;' % (p['type'], v))
            args.append('&' + v)
        elif k == 'ARR_OUT':
            L.append('  %s *%s = nullptr;' % (p['type'], v))
            args.append('&' + v)
        elif k == 'STR_OUT':
            L.append('  char *%s = nullptr;' % v)
            args.append('&' + v)
        elif k == 'INT_OUT':
            L.append('  %s %s = 0;' % (p['ctype'], v))
            args.append('&' + v)
        elif k == 'HANDLE_OUT':
            L.append('  void *%s = nullptr;' % v)
            args.append('&' + v)
    call = 'real_%s(%s)' % (n, ', '.join(args))
    if retc == 'int':
        L.append('  int ret = %s;' % call)
        L.append('  out.i64(ret);')
    elif '*' in retc:
        L.append('  const char *ret = %s;' % call)
        L.append('  out.str(ret);')
    else:
        L.append('  %s;' % call)
    outparams = {p['name']: p for p in fn['cls']}
    for p in fn['cls']:
        k, nm = p['kind'], p['name']
        v = 'a_' + nm
        if k == 'STRUCT_OUT':
            L.append('  fb::enc_struct_ptr(out, TY_%s, %s);' % (p['type'], v))
            L.append('  if (%s) %s;' % (v, ('real_%s(%s)' % (FREE_FOR[p['type']][0], v)) if not FREE_FOR[p['type']][1] else 'abort()'))
        elif k == 'ARR_OUT':
            cv = 'a_' + p['cnt']
            fname, hascnt = FREE_FOR[p['type']]
            L.append('  fb::enc_struct_array_out(out, TY_%s, %s, (int)%s);' % (p['type'], v, cv))
            L.append('  if (%s) real_%s(%s%s);' % (v, fname, v, (', (int)' + cv) if hascnt else ''))
        elif k == 'STR_OUT':
            L.append('  out.str(%s); if (%s) real_fnet_freeString(%s);' % (v, v, v))
        elif k == 'INT_OUT':
            L.append('  out.i64((int64_t)%s);' % v)
        elif k == 'HANDLE_OUT':
            L.append('  out.u64((uint64_t)(uintptr_t)%s);' % v)
    for cl in cleanup:
        L.append('  ' + cl)
    if n == 'fnet_createConnection':
        L.append('  if (ret == FNET_OK && a_conn) fb::conn_keep(a_conn, tl, a_settings); else { tl.release(); fb::free_struct_ptr(TY_fnet_conn_settings_t, a_settings); }')
    elif n == 'fnet_freeConnection':
        L.append('  fb::conn_drop(a_conn);')
    else:
        L.append('  tl.release();')
    L.append('}')
    h.extend(L)

h.append('typedef void (*CallFn)(fb::Buf &, fb::Buf &);')
h.append('static const CallFn CALLS[FID_COUNT] = {%s};' % ', '.join('call_%s' % f['name'] for f in FWD))
# callback function types for casts
for typ, k in CB_KINDS.items():
    pass
open(os.path.join(OUT, 'gen_helper.inc'), 'w').write('\n'.join(h) + '\n')

print('structs=%d forwarded=%d local_free=%d' % (len(STRUCTS), len(FWD), len(FREE_FOR)))


# ---------------------------------------------------------- fake_stubs.inc (tests only)
f = ['// GENERATED by gen.py - weak default stubs for the fake test plugin\n']
for fn in FUNCS:
    retc = fn['ret']
    decl = ', '.join('%s %s' % (p['ctype'], p['name']) for p in fn['params']) or 'void'
    if retc == 'int':
        body = 'return FNET_ERROR;'
    elif '*' in retc:
        body = 'return nullptr;'
    else:
        body = ''
    f.append('extern "C" __attribute__((weak)) %s %s(%s) { %s }' % (retc, fn['name'], decl, body))
open(os.path.join(OUT, 'fake_stubs.inc'), 'w').write('\n'.join(f) + '\n')
