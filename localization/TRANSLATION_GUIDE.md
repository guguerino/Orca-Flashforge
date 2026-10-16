# OrcaSlicer (Flash Studio / orca_flashforge) 翻译与多语言开发指南

本指南描述 Flash Studio（内部代号 orca_flashforge）的完整本地化流程，涵盖文件角色、翻译管线、新增语言、常用宏与常见陷阱，所有路径与命令均已在仓库中实际验证。

---

## 目录

1. [总览：源 vs 产物](#一总览源-vs-产物)
2. [目录与文件角色](#二目录与文件角色)
3. [完整翻译管线（6 步）](#三完整翻译管线6-步)
4. [两种常见情形及命令](#四两种常见情形及命令)
5. [新增一门语言](#五新增一门语言)
6. [翻译宏速查](#六翻译宏速查)
7. [三条铁律与常见陷阱](#七三条铁律与常见陷阱)
8. [Excel 批量导入（可选）](#八excel-批量导入可选)

---

## 一、总览：源 vs 产物

| 目录 | 性质 | 说明 |
|------|------|------|
| `localization/` | **翻译源**（可手动编辑） | 所有人工维护的 `.po` 文件、脚本、Excel 均在此 |
| `resources/i18n/` | **生成产物**（禁止手编） | 由脚本自动生成，手动修改会在下次运行时被覆盖 |

**两个关键品牌常量**（定义于 `version.inc`）：

- `SLIC3R_APP_NAME = "Flash Studio"` — 界面显示名
- `SLIC3R_APP_KEY  = "Orca-Flashforge"` — 内部 key，**绝不能改**
  - 决定运行时加载的 `.mo` 文件名：`Orca-Flashforge.mo`
  - 决定用户配置目录名及构建产物路径

运行时调用 `AddCatalog(SLIC3R_APP_KEY)`，加载路径为：

```
resources/i18n/{lang}/Orca-Flashforge.mo
```

---

## 二、目录与文件角色

```
localization/
├── i18n/
│   ├── Orca-Flashforge.pot          # xgettext 从源码抽取的模板（产物，勿手编）
│   ├── list.txt                     # xgettext 扫描的源文件清单（新增 .cpp 要登记）
│   └── {lang}/
│       └── OrcaSlicer_{lang}.po     # 各语言【上游通用】翻译源（注意前缀是 OrcaSlicer_）
│
├── flashforge/
│   └── {lang}/
│       ├── flashforge_{lang}.po     # FlashForge【专属新增】字符串
│       │                            #   合并时以 _appendPo 追加
│       │                            #   msgid 不得与 OrcaSlicer 层重复（否则报 duplicate）
│       └── orca_{lang}.po           # FlashForge 对【上游已有 msgid】的译文【覆写】
│                                    #   合并时以 _replaceMsgStr 按 msgid 精确匹配
│                                    #   会把新译文写回 OrcaSlicer_{lang}.po 对应条目
│
└── tools/
    ├── AutoMergeDestPo.py           # 合并脚本（语言列表硬编码在第 60、70 行）
    └── importTrFromExcel.py         # Excel 批量导入脚本

resources/i18n/{lang}/
├── Orca-Flashforge_{lang}.po        # 最终合并后的 .po（产物）
└── Orca-Flashforge.mo               # 编译后的运行时二进制（产物）
```

**工具链**（仓库自带，位于 `tools/`）：

| 工具 | 用途 |
|------|------|
| `tools/xgettext.exe` | 从源码提取待译字符串，生成 `.pot` |
| `tools/msgmerge.exe` | 将新 `.pot` 合并进已有 `.po` |
| `tools/msgfmt.exe`   | 将 `.po` 编译为运行时 `.mo`；`AutoMergeDestPo.py` 优先使用此路径，找不到时回退到系统 PATH 中的 `msgfmt` |

---

## 三、完整翻译管线（6 步）

```
① 源码标记 _L("text")
        ↓
② translate.bat  →  抽取生成 localization/i18n/Orca-Flashforge.pot
        ↓
③ msgmerge  →  并入各 localization/i18n/{lang}/OrcaSlicer_{lang}.po
        ↓
④ 填写 msgstr  →  在 .po 文件中完成翻译
        ↓
⑤ python AutoMergeDestPo.py  →  合并 flashforge 层，生成 resources/i18n/{lang}/Orca-Flashforge_{lang}.po
                                  并自动编译各语言 Orca-Flashforge.mo（一键完成，无需单独 msgfmt）
        ↓
⑥ 重新构建 / 重启应用
```

> **记住**：步骤 ①~④ 操作 `localization/` 下的源文件；步骤 ⑤~⑥ 生成并使用 `resources/` 下的产物。

---

## 四、两种常见情形及命令

以下命令以**土耳其语（tr）**为例，其他语言替换语言代码即可。

---

### 情形 C：补译【已存在但未译】的条目（最常用）

适用场景：`.pot`/`.po` 中已有条目，`msgstr` 为空，需要填入译文。

**步骤：**

**1. 编辑源 `.po` 文件，填写 `msgstr`**

- 若为 FlashForge 专属字符串：编辑 `localization/flashforge/tr/flashforge_tr.po`
- 若为上游通用字符串：编辑 `localization/i18n/tr/OrcaSlicer_tr.po`
- 若需覆写上游已有译文：编辑 `localization/flashforge/tr/orca_tr.po`

**2. 运行合并脚本——合并并自动编译所有语言的 `.mo`**

```bash
python localization/tools/AutoMergeDestPo.py
```

无需再单独跑 `msgfmt`；若只想单独编某一个语言，可选：

```bash
./tools/msgfmt.exe --check-format \
  -o resources/i18n/tr/Orca-Flashforge.mo \
  resources/i18n/tr/Orca-Flashforge_tr.po
```

**3. 重新构建并重启应用**

```bash
# Windows 构建
cmake --build . --config %build_type% --target ALL_BUILD -- -m
```

---

### 情形 A：新增【源码里全新】的字符串

适用场景：在 C++ 源码中新增了用户可见文本，需走完整的提取→翻译→编译流程。

**步骤：**

**1. 在代码中用翻译宏包裹新字符串**

```cpp
// GUI 字符串用 _L()
wxString label = _L("My new string");

// 若该 .cpp 文件从未被 xgettext 扫描，在 list.txt 中登记：
// localization/i18n/list.txt
```

**2. 运行 `translate.bat` 刷新 `.pot`**

```bat
scripts\translate.bat
```

这会更新 `localization/i18n/Orca-Flashforge.pot`，将新字符串纳入模板。

**3. 将新条目合并进目标语言 `.po`**

```bash
./tools/msgmerge.exe -N \
  -o localization/i18n/tr/OrcaSlicer_tr.po \
  localization/i18n/tr/OrcaSlicer_tr.po \
  localization/i18n/Orca-Flashforge.pot
```

**4. 填写 `msgstr` 译文**

在 `localization/i18n/tr/OrcaSlicer_tr.po` 中找到新条目并填写翻译。

**5. 合并 → 编译 → 重建**

```bash
python localization/tools/AutoMergeDestPo.py
# 脚本自动编译所有语言的 .mo，无需单独运行 msgfmt

cmake --build . --config %build_type% --target ALL_BUILD -- -m
```

---

## 五、新增一门语言

以已启用的**土耳其语（tr）**为参考模板，新增语言代码以 `{lang}` 表示。

**步骤：**

**1. 创建 FlashForge 层目录与文件**

```
localization/flashforge/{lang}/
├── flashforge_{lang}.po    # FF 专属字符串（msgstr 可为空）
└── orca_{lang}.po          # 覆写层
```

> **注意 `orca_{lang}.po`**：若无覆写需求，只保留 PO header、不含任何 `msgid` 条目。空 `msgstr` 会把上游好译文覆盖为空串，造成界面显示空白。

参照已有语言（如 `localization/flashforge/de/`）的文件结构创建。

**2. 在 `AutoMergeDestPo.py` 中注册新语言**

打开 `localization/tools/AutoMergeDestPo.py`，在**第 60 行**和**第 70 行**的语言列表中各加入新语言代码：

```python
# 第 60 行附近
langs = ["de", "en", "es", "fr", "ja", "ko", "ru", "tr", "zh_CN", "{lang}"]

# 第 70 行附近
ff_langs = ["de", "en", "es", "fr", "ja", "ko", "ru", "tr", "zh_CN", "{lang}"]
```

**3. 准备上游通用 `.po`**

若上游仓库已提供 `localization/i18n/{lang}/OrcaSlicer_{lang}.po`，直接使用；否则从 `.pot` 生成空壳：

```bash
./tools/msginit.exe \
  -i localization/i18n/Orca-Flashforge.pot \
  -o localization/i18n/{lang}/OrcaSlicer_{lang}.po \
  -l {lang}
```

**4. 合并并编译**

```bash
python localization/tools/AutoMergeDestPo.py
```

脚本自动编译所有已注册语言（含新增语言）的 `.mo`，无需单独运行 `msgfmt`。

**5. 在 `Preferences.cpp` 中注册新语言**

文件路径：`src/slic3r/GUI/Preferences.cpp`

在 `supported_languages[]` 数组中添加 `wxLANGUAGE_XXX` 枚举值，并在显示名 `if/else` 链中加入本地化名称：

```cpp
// 数组中加入：
wxLANGUAGE_TURKISH,

// 显示名链中加入（以土耳其语为例）：
// 注意：C++ \x 十六进制转义会贪婪匹配后续字母，
// 遇到名字中紧跟字母时须用相邻字符串字面量断开：
else if (lang == wxLANGUAGE_TURKISH)
    name = "T\xc3\xbcrk\xc3\xa7" "e";
```

**6. 重新构建**

```bash
cmake --build . --config %build_type% --target ALL_BUILD -- -m
```

**7. 处理 `.gitignore` 问题（见坑 A）**

新 `.mo` 文件受 `.gitignore` 的 `*.mo` 规则屏蔽，需强制加入版本控制：

```bash
git add -f resources/i18n/{lang}/Orca-Flashforge.mo
```

---

## 六、翻译宏速查

宏定义位于 `src/slic3r/GUI/I18N.hpp`。

| 宏 | 返回类型 | 使用场景 |
|----|----------|----------|
| `_L(s)` | `wxString` | GUI 字符串，最常用 |
| `_u8L(s)` | `std::string` | 给 `boost::format`、错误消息等需要 UTF-8 `std::string` 的场合 |
| `L(s)` | 编译期标记 | 静态字符串表（仅标记，实际翻译须另用 `_L()` 包裹） |
| `L_CONTEXT(s, ctx)` | — | 同一 `msgid` 在不同上下文有不同译文时消歧 |
| `_L_PLURAL(singular, plural, n)` | `wxString` | 复数形式（如"1 item" / "N items"） |

---

## 七、三条铁律与常见陷阱

### 三条铁律

1. **源只在 `localization/`，`resources/` 是产物，禁止手编产物。**
2. **改了 `.mo` 必须重新构建 + 重启应用才生效。**
   常见误区：`.po` 里译好了但应用仍显示英文——原因是没有重新编译 `.mo` 或没有重启。
3. **占位符原样保留，译完删除 `#, fuzzy` 标记。**
   `%s`、`%d`、`%1`、`{number}` 等占位符必须在译文中原样出现；保留 `#, fuzzy` 标记的条目会被 `msgfmt` 忽略，不会出现在运行时。

---

### 常见陷阱

**坑 A：新语言 `.mo` 被 `.gitignore` 屏蔽**

仓库 `.gitignore` 包含 `*.mo` 规则，新增语言的 `.mo` 文件不会被 `git add` 自动追踪。发版前需强制添加：

```bash
git add -f resources/i18n/{lang}/Orca-Flashforge.mo
```

---

**坑 B：`run_gettext.sh` 输出文件名错误**

`scripts/run_gettext.sh` 存在 bug，输出的 `.mo` 文件名为 `OrcaSlicer.mo`，而运行时需要的是 `Orca-Flashforge.mo`。

**Windows 上请使用 `scripts/run_gettext.bat` 或手动执行 `msgfmt`（本指南采用此方式）。**

---

**坑 C：`run_gettext.bat` / CMake gettext target 输出路径错误**

`run_gettext.bat` 和 CMake 的 `gettext` target 因文件名前缀不匹配，会将 `.mo` 输出到错误路径：

```
resources/i18n/OrcaSlicer_{lang}/   ← 错误路径
resources/i18n/{lang}/              ← 正确路径
```

此外，CMake 的 `gettext` target 未挂入 `ALL_BUILD`，**不会在常规构建时自动执行**。

结论：始终手动运行 `AutoMergeDestPo.py`（已内置 `msgfmt` 编译步骤，一键合并+编译所有语言），不要依赖上述自动化 target。

---

**坑 D：品牌名替换走覆写层，不改源码 `msgid`**

将显示名从 "OrcaSlicer" 改为 "Flash Studio" 应通过 **FlashForge 覆写层**（`orca_{lang}.po`）实现，而非修改源码中的 `msgid`：

- **自指文本**（应用自称自己的名字）：在 `orca_{lang}.po` 中覆写 `msgstr`
- **署名文本**（如 "Based on OrcaSlicer"）：保留原样
- **英文也需处理**：在 `localization/flashforge/en/orca_en.po` 中补充覆写，否则英文界面仍显示旧名

---

## 八、Excel 批量导入（可选）

脚本路径：`localization/tools/importTrFromExcel.py`

**功能：** 从多 sheet Excel 文件中批量读取译文，导入到对应的 `flashforge_{lang}.po`。

**Excel 格式要求：**

- 每个 sheet 对应一门语言
- 必须包含列名 `英文-en`（原文）和目标语言列（如 `土耳其语-tr`）

**行为说明：**

- **只填空 `msgstr`**，不覆盖已有译文
- 自动规范化换行符以匹配 `.po` 中的条目
- 输出两份报告：
  - `localization/tools/tr_import_unmatched.txt` — Excel 中有但 `.po` 中找不到对应 `msgid` 的条目
  - `localization/tools/tr_import_conflicts.txt` — 同一 `msgid` 在多处有不同译文的冲突记录

**用法示例（导入土耳其语）：**

```bash
python localization/tools/importTrFromExcel.py
# 脚本内部已配置 Excel 路径和目标语言，根据实际参数调整
```

导入完成后仍需执行完整的后续步骤：

```bash
python localization/tools/AutoMergeDestPo.py
# 脚本自动编译所有语言的 .mo
```
