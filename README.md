# PotentialFix — 满潜修复（精简版源码） / PotentialFix — Max Potential Fix (Condensed Source)

## 这一版做了什么 / What This Version Does

在功能完全等价的前提下，把源码从 **805 行精简到 394 行**（-51%），删除的全部是
已用真实游戏验证为「无效」或「一次性诊断」的代码。

While preserving full functional equivalence, the source has been **reduced from 805 lines to 394 lines** (-51%). All removed code was verified in the actual game to be either ineffective or intended for one-time diagnostics.

## 目录内容 / Directory Contents

| 文件 / File | 说明 / Description |
|---|---|
| `PotentialFix.cpp` | 主源码，394 行单文件 / Main source file, 394 lines |
| `PotentialFix.def` | 导出表（仅 `PotentialFixLevel`）/ Export definition file (only `PotentialFixLevel`) |
| `PotentialFix.ini` | 配置 / Configuration |
| `build.cmd` | MSVC 构建脚本 / MSVC build script |
| `fakega_slim.cpp` | 本地测试用假 GameAssembly / Fake GameAssembly for local testing |
| `host_slim.cpp` | 测试驱动，8 项断言 / Test harness with 8 assertions |
| `run_test.cmd` | 一键构建假模块 + 跑测试 / One-command script to build the fake module and run tests |

## 最终实现 / Final Implementation

### 五处补丁 / Five Patches

| # | 位置 / Location | 手法 / Technique | 作用 / Purpose |
|---|---|---|---|
| 1 | `CharInfo::_RefreshPotentialAttribute` `+0x30` | **跳板**写 `[rbx+0x60]=N` / **Trampoline** writes `[rbx+0x60]=N` | 页面节点点亮 + 满潜特效 / Lights up the page nodes and displays the max-potential effect |
| 2 | `CharInfo::_RefreshPotentialModifiedSkillData` `+0x377` | `8B 5F 60` → `6A N 5B` | 技能参数 / Skill parameter |
| 3 | `ClientCharInfo::_InitPotentialCardSkill` `+0xCC` | `8B 53 60` → `6A N 5A` | 潜能卡片 / Potential card |
| 4 | `CharMiscFeature::RefreshPotentialBuff` `+0x488` | `41 8B 57 60` → `6A N 5A 90` | buff / Buff |
| 5 | `CharMiscFeature::RefreshMaxPotentialEffectShow` `+0x18B` | 10 字节 → `C6 47 60 05` + nop×6 / 10 bytes → `C6 47 60 05` + nop×6 | 写回层数 + 恒走满潜分支 / Writes the level back and always takes the max-potential branch |

### 跳板（补丁 1）/ Trampoline (Patch 1)

```
原 / Original:   8B 53 60        mov edx,[rbx+0x60]
                48 8B 4B 18     mov rcx,[rbx+0x18]

入口 / Entry:    E9 rel32        jmp 洞穴 (code cave)      （后接 90 90 / followed by 90 90）

洞穴 / Code cave: C6 43 60 05     mov byte [rbx+0x60], N   ← 关键 / key
                6A N 5A         push N ; pop rdx
                48 8B 4B 18     mov rcx,[rbx+0x18]
                E9 rel32        jmp site+7
```

洞穴取自 `.text` 段的填充区（运行时搜索，不硬编码地址）；找不到则在模块附近
（±1GB）自行分配。三重保险：模式唯一性、`rel32` 可达性、失败自动降级为只改读取值。

The code cave is taken from a padding region in the `.text` section (searched for at runtime; no hard-coded address). If none is found, memory is allocated near the module (within ±1 GB). Three safeguards are used: the pattern must be unique, the target must be reachable with `rel32`, and on failure the patch automatically falls back to changing only the read value.

## 配置 / Configuration

```ini
[lift]
enabled=1       ; 总开关 / Master switch
target=5        ; 目标层数 1..5 / Target level, 1..5
writeBack=1     ; 1=跳板写回字段（页面点亮）；0=只改读取值 / 1=trampoline writes the field back (lights page nodes); 0=changes only the read value

[safety]
startDelayMs=2500

[debug]
verbose=1
```

## 本地验证结果 / Local Validation Results

```
before: attr=1 skill=1 buff=1 getLv=1 skillLv=1
ok   _RefreshPotentialAttribute             got=5
ok   _RefreshPotentialModifiedSkillData     got=5
ok   RefreshPotentialBuff                   got=5
ok   get_PotentialLevel 不覆写(只改字段) / is not overridden (field only) got=1
ok   RefreshMaxPotentialEffectShow -> 1     got=1
ok   obj+0x60 字段被写回 5 / field written back as 5 got=5
ok   _InitPotentialCardSkill -> 5           got=5

RESULT: PASS fails=0
```

## 三条核心事实（供后续维护）/ Three Key Facts (For Future Maintenance)

1. 运行时 `CharInfo.potentialLevel` 在 **+0x60**，是层数唯一的标量字段。
   页面第 i 个节点亮不亮 = `potentialLevel >= i`，**必须把字段写成 N**；
   只改读取值或判定条件都不会点亮。

   At runtime, `CharInfo.potentialLevel` is at **+0x60** and is the only scalar field representing the level.
   Whether page node *i* is lit is determined by `potentialLevel >= i`; **the field must be set to N**.
   Changing only the read value or the condition will not light the nodes.

2. 属性 / 技能 / buff 三条路径各自读 `[obj+0x60]`，读到的值也要是 N。

   The attribute, skill, and buff paths each read `[obj+0x60]`; each must read the value N.

3. IL2CPP 有 **ICF（相同函数体合并）**：`get_PotentialLevel` 的 4 字节函数体在全镜像
   出现 **30 次**，与 `SERVER_SKILL::get_PotentialLv` 等同址。改函数体会波及大量无关
   结构体（曾导致黑屏）→ 只改具体指令。

   IL2CPP uses **ICF (Identical Code Folding)**: the 4-byte function body for `get_PotentialLevel` appears **30 times** across the image and shares an address with `SERVER_SKILL::get_PotentialLv`. Patching the function body would affect many unrelated structures (and has previously caused a black screen), so patch only the specific instructions.

## 版本适配 / Version Adaptation

游戏更新后若偏移变化，日志会明确提示：

If offsets change after a game update, the log will indicate this clearly:

- `[-] XXX: pattern not found` → 该函数内找不到原始字节，需重新反汇编定位 / Original bytes not found in the function; disassemble and locate them again.
- `[!] XXX: pattern matched N times, ambiguous, skipped` → 命中多处，说明该片段被复用 / Multiple matches found; the snippet is reused, so the patch was skipped as ambiguous.
- `[!] writeBack: pattern found N times` → 跳板定位失败，已自动降级 / Trampoline location failed; automatically fell back.
- `[=] ... patched=N` 小于 5 → 有目标未找到 / If `patched=N` is less than 5, one or more targets were not found.

定位方法：静态反汇编 `GameAssembly.dll`（磁盘明文），按方法名枚举找到原生入口，
在函数内搜索层数读取指令（`8B 53 60` / `8B 5F 60` / `41 8B 57 60` 等）。

How to locate them: statically disassemble `GameAssembly.dll` (the file is plaintext on disk), enumerate methods by name to find their native entry points, then search within each function for level-read instructions such as `8B 53 60`, `8B 5F 60`, or `41 8B 57 60`.
