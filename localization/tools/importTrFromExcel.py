"""
Import Turkish translations from Flash Studio多语言翻译.xlsx into flashforge_tr.po.

Usage:
    python importTrFromExcel.py [--xlsx XLSX_PATH] [--po PO_PATH]

Defaults:
    xlsx: ../../localization/Flash Studio新版本多语言翻译.xlsx  (relative to this script)
    po  : ../../localization/flashforge/tr/flashforge_tr.po
"""

import os
import sys
import argparse
import openpyxl

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import PoRW

# Same escape table as AutoMergeXlsx.py
ESCAPE_TABLE = {
    ord("\\"): "\\\\",
    ord("\""): "\\\"",
    ord("\n"): "\\n",
    ord("\r"): "\\r",
    ord("\t"): "\\t",
}

COL_EN = "英文-en"
COL_TR = "土耳其语-tr"
SKIP_SHEETS = {"王平飞临时"}


def escape_for_po(text: str) -> str:
    return text.translate(ESCAPE_TABLE)


def normalize_en(text: str) -> str:
    """Strip, remove \\r, then escape for PO format."""
    text = text.strip().replace("\r", "")
    return escape_for_po(text)


def collect_excel_translations(xlsx_path: str):
    """Return (en_key→tr_escaped dict, conflict list)."""
    wb = openpyxl.load_workbook(xlsx_path, data_only=True)
    raw_map = {}      # en_escaped → tr_escaped (last-write-wins)
    conflict_list = []  # [(en_escaped, tr_old, tr_new, sheet)]

    for sheet_name in wb.sheetnames:
        if sheet_name in SKIP_SHEETS:
            print(f"  [skip] sheet '{sheet_name}' (in SKIP_SHEETS)")
            continue

        ws = wb[sheet_name]
        # Locate header row (row 1)
        headers = {
            ws.cell(row=1, column=c).value: c
            for c in range(1, ws.max_column + 1)
        }

        if COL_EN not in headers or COL_TR not in headers:
            col_names = [v for v in headers if v is not None]
            has_en = COL_EN in headers
            has_tr = COL_TR in headers
            print(f"  [skip] sheet '{sheet_name}' (has_en={has_en}, has_tr={has_tr})")
            continue

        col_en_idx = headers[COL_EN]
        col_tr_idx = headers[COL_TR]
        count = 0

        for row in range(2, ws.max_row + 1):
            en_val = ws.cell(row=row, column=col_en_idx).value
            tr_val = ws.cell(row=row, column=col_tr_idx).value
            if en_val is None or tr_val is None:
                continue
            en_str = str(en_val).strip()
            tr_str = str(tr_val).strip()
            if not en_str or not tr_str:
                continue

            en_key = escape_for_po(en_str)
            tr_escaped = escape_for_po(tr_str)

            if en_key in raw_map:
                if raw_map[en_key] != tr_escaped:
                    conflict_list.append((en_key, raw_map[en_key], tr_escaped, sheet_name))
                    raw_map[en_key] = tr_escaped  # last-write-wins
            else:
                raw_map[en_key] = tr_escaped
                count += 1

        print(f"  [ok  ] sheet '{sheet_name}': {count} new entries")

    return raw_map, conflict_list


def _append_msgstr_lines(lines, msgstr, split_by_space=False):
    """Reproduce AutoMergeXlsx._appendMsgStrLines logic."""
    max_len = 79
    if len(msgstr) <= max_len - 7 and "\\n" not in msgstr:
        lines.append('msgstr "%s"\n' % msgstr)
        return
    lines.append('msgstr ""\n')
    while True:
        lf_idx = msgstr.find("\\n")
        if lf_idx != -1 and lf_idx < max_len - 1:
            lines.append('"' + msgstr[:lf_idx + 2] + '"\n')
            msgstr = msgstr[lf_idx + 2:]
            continue
        if len(msgstr) <= max_len:
            if msgstr:
                lines.append('"' + msgstr + '"\n')
            break
        if split_by_space:
            for i in range(max_len, -1, -1):
                if i == 0:
                    lines.append('"' + msgstr + '"\n')
                    break
                elif msgstr[i].isspace() or msgstr[i] == '-':
                    lines.append('"' + msgstr[:i + 1] + '"\n')
                    msgstr = msgstr[i + 1:]
                    break
        else:
            lines.append('"' + msgstr[:max_len] + '"\n')
            msgstr = msgstr[max_len:]


def get_new_msg_lines(msg, new_msgstr, split_by_space=False):
    """Reproduce AutoMergeXlsx._getNewMsgLines."""
    lines = []
    is_msgstr = None
    for line in msg.lines:
        lt = line.lstrip()
        if lt.startswith("msgstr") and len(lt) > 6 and lt[6].isspace():
            _append_msgstr_lines(lines, new_msgstr, split_by_space)
            is_msgstr = True
        elif lt.startswith("msgstr[0]") or lt.startswith("msgstr[1]"):
            is_msgstr = True
        elif lt.startswith("msgctxt") or lt.startswith("msgid") or lt.startswith("msgid_plural"):
            lines.append(line)
            is_msgstr = False
        elif not is_msgstr:
            lines.append(line)
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    repo_root = os.path.normpath(os.path.join(SCRIPT_DIR, "..", ".."))
    parser.add_argument("--xlsx", default=os.path.join(
        repo_root, "localization", "Flash Studio新版本多语言翻译.xlsx"))
    parser.add_argument("--po", default=os.path.join(
        repo_root, "localization", "flashforge", "tr", "flashforge_tr.po"))
    args = parser.parse_args()

    out_dir = SCRIPT_DIR
    conflicts_path = os.path.join(out_dir, "tr_import_conflicts.txt")
    unmatched_path = os.path.join(out_dir, "tr_import_unmatched.txt")

    print(f"Excel: {args.xlsx}")
    print(f"PO   : {args.po}")
    print()

    # ── 1. Collect translations from Excel ──────────────────────────────────
    print("Reading Excel sheets...")
    en_to_tr, conflicts = collect_excel_translations(args.xlsx)

    total_en = len(en_to_tr)
    print(f"\nExcel 汇聚：唯一 en 键 {total_en} 条，冲突 {len(conflicts)} 条")

    # Build a second lookup keyed by normalize_en (for layer-2 matching)
    norm_to_tr = {}
    for en_key, tr_val in en_to_tr.items():
        # en_key is already escaped (but not stripped of leading/trailing spaces
        # and \r isn't removed from multi-char sequences after escaping)
        # We build a separate normalized map from the same raw data
        pass

    # We need the raw (un-escaped) en values too, so rebuild from Excel
    wb2 = openpyxl.load_workbook(args.xlsx, data_only=True)
    norm_map = {}  # normalize_en(raw) → tr_escaped (last-write-wins same as above)
    raw_record_count = 0
    for sheet_name in wb2.sheetnames:
        if sheet_name in SKIP_SHEETS:
            continue
        ws = wb2[sheet_name]
        headers = {
            ws.cell(row=1, column=c).value: c
            for c in range(1, ws.max_column + 1)
        }
        if COL_EN not in headers or COL_TR not in headers:
            continue
        col_en_idx = headers[COL_EN]
        col_tr_idx = headers[COL_TR]
        for row in range(2, ws.max_row + 1):
            en_val = ws.cell(row=row, column=col_en_idx).value
            tr_val = ws.cell(row=row, column=col_tr_idx).value
            if en_val is None or tr_val is None:
                continue
            en_str = str(en_val)
            tr_str = str(tr_val).strip()
            if not en_str.strip() or not tr_str:
                continue
            nk = normalize_en(en_str)
            tr_escaped = escape_for_po(tr_str)
            norm_map[nk] = tr_escaped
            raw_record_count += 1

    print(f"Excel 原始 (en,tr) 记录数（含重复）: {raw_record_count}")

    # ── 2. Write conflicts report ───────────────────────────────────────────
    with open(conflicts_path, "w", encoding="utf-8") as f:
        f.write(f"# 冲突报告：同一 en 对应不同 tr（共 {len(conflicts)} 条）\n\n")
        for i, (en_key, tr_old, tr_new, sheet) in enumerate(conflicts, 1):
            f.write(f"[{i}] sheet={sheet}\n")
            f.write(f"  en  : {en_key}\n")
            f.write(f"  tr旧: {tr_old}\n")
            f.write(f"  tr新: {tr_new}\n\n")
    print(f"冲突报告写入: {conflicts_path}")

    # ── 3. Read PO file ─────────────────────────────────────────────────────
    print(f"\n读取 PO 文件...")
    msg_list, invalid_list = PoRW.readMsgList(args.po, False)
    if invalid_list:
        print(f"警告: PO 文件有 {len(invalid_list)} 条无效条目")

    empty_msgs = [m for m in msg_list if m.msgStr == ""]
    print(f"PO 总条目: {len(msg_list)}, 空 msgstr: {len(empty_msgs)}")

    # ── 4. Match and fill ───────────────────────────────────────────────────
    hit_count = 0
    unmatched = []
    hit_examples = []

    for msg in msg_list:
        if msg.msgStr != "":
            continue  # never overwrite existing translations

        msgid = msg.msgId  # already in escaped PO format

        # Layer 1: exact match (escaped Excel en == PO msgId)
        tr_val = en_to_tr.get(msgid)
        match_layer = 1

        # Layer 2: normalized match
        if tr_val is None:
            tr_val = norm_map.get(msgid)
            match_layer = 2

        if tr_val is not None and tr_val:
            msg.lines = get_new_msg_lines(msg, tr_val, split_by_space=False)
            msg.msgStr = tr_val
            hit_count += 1
            if len(hit_examples) < 3:
                hit_examples.append((msgid, tr_val, match_layer))
        else:
            unmatched.append(msgid)

    still_empty = len([m for m in msg_list if m.msgStr == ""])
    print(f"\n匹配结果：命中 {hit_count} 条，仍空 {still_empty} 条")

    # ── 5. Write updated PO ─────────────────────────────────────────────────
    PoRW.saveMsgList(msg_list, args.po)
    print(f"PO 文件已原地更新: {args.po}")

    # ── 6. Write unmatched report ───────────────────────────────────────────
    with open(unmatched_path, "w", encoding="utf-8") as f:
        f.write(f"# 未命中报告：flashforge_tr.po 中无法在 Excel 中找到译文的 msgid（共 {len(unmatched)} 条）\n\n")
        for i, mid in enumerate(unmatched, 1):
            f.write(f"[{i:03d}] {mid}\n\n")
    print(f"未命中报告写入: {unmatched_path}（{len(unmatched)} 条）")

    # ── 7. Sample output ────────────────────────────────────────────────────
    samples_path = os.path.join(out_dir, "tr_import_samples.txt")
    with open(samples_path, "w", encoding="utf-8") as f:
        f.write("=== 抽样展示（前 3 条命中）===\n\n")
        for msgid, tr_val, layer in hit_examples:
            f.write(f"[layer {layer}]\n")
            f.write(f"  msgid : {msgid}\n")
            f.write(f"  msgstr: {tr_val}\n\n")
    print(f"抽样样本写入: {samples_path}")


if __name__ == "__main__":
    main()
