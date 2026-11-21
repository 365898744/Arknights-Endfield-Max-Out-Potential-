# PotentialFix — 满潜修复（精简版源码）


## 目录内容

| 文件 | 说明 |
|---|---|
| `PotentialFix.cpp` | 主源码，394 行单文件 |
| `PotentialFix.def` | 导出表（仅 `PotentialFixLevel`） |
| `PotentialFix.ini` | 配置 |
| `build.cmd` | MSVC 构建脚本 |
| `fakega_slim.cpp` | 本地测试用假 GameAssembly |
| `host_slim.cpp` | 测试驱动，8 项断言 |
| `run_test.cmd` | 一键构建假模块 + 跑测试 |


## 最终实现

### 五处补丁

| # | 位置 | 手法 | 作用 |
|---|---|---|---|
| 1 | `CharInfo::_RefreshPotentialAttribute` `+0x30` | **跳板**写 `[rbx+0x60]=N` | 页面节点点亮 + 满潜特效 |
| 2 | `CharInfo::_RefreshPotentialModifiedSkillData` `+0x377` | `8B 5F 60` → `6A N 5B` | 技能参数 |
| 3 | `ClientCharInfo::_InitPotentialCardSkill` `+0xCC` | `8B 53 60` → `6A N 5A` | 潜能卡片 |
| 4 | `CharMiscFeature::RefreshPotentialBuff` `+0x488` | `41 8B 57 60` → `6A N 5A 90` | buff |
| 5 | `CharMiscFeature::RefreshMaxPotentialEffectShow` `+0x18B` | 10 字节 → `C6 47 60 05` + nop×6 | 写回层数 + 恒走满潜分支 |

### 跳板（补丁 1）

```
原:   8B 53 60        mov edx,[rbx+0x60]
      48 8B 4B 18     mov rcx,[rbx+0x18]

入口: E9 rel32        jmp 洞穴      （后接 90 90）

洞穴: C6 43 60 05     mov byte [rbx+0x60], N   ← 关键
      6A N 5A         push N ; pop rdx
      48 8B 4B 18     mov rcx,[rbx+0x18]
      E9 rel32        jmp site+7
```

洞穴取自 `.text` 段的填充区（运行时搜索，不硬编码地址）；找不到则在模块附近
（±1GB）自行分配。三重保险：模式唯一性、`rel32` 可达性、失败自动降级为只改读取值。

## 配置

```ini
[lift]
enabled=1       ; 总开关
target=5        ; 目标层数 1..5
writeBack=1     ; 1=跳板写回字段（页面点亮）；0=只改读取值

[safety]
startDelayMs=2500

[debug]
verbose=1
```

## 本地验证结果

```
before: attr=1 skill=1 buff=1 getLv=1 skillLv=1
ok   _RefreshPotentialAttribute             got=5
ok   _RefreshPotentialModifiedSkillData     got=5
ok   RefreshPotentialBuff                   got=5
ok   get_PotentialLevel 不覆写(只改字段)     got=1
ok   RefreshMaxPotentialEffectShow -> 1     got=1
ok   obj+0x60 字段被写回 5                   got=5
ok   _InitPotentialCardSkill -> 5           got=5

RESULT: PASS fails=0
```

## 三条核心事实（供后续维护）

1. 运行时 `CharInfo.potentialLevel` 在 **+0x60**，是层数唯一的标量字段。
   页面第 i 个节点亮不亮 = `potentialLevel >= i`，**必须把字段写成 N**；
   只改读取值或判定条件都不会点亮。
2. 属性 / 技能 / buff 三条路径各自读 `[obj+0x60]`，读到的值也要是 N。
3. IL2CPP 有 **ICF（相同函数体合并）**：`get_PotentialLevel` 的 4 字节函数体在全镜像
   出现 **30 次**，与 `SERVER_SKILL::get_PotentialLv` 等同址。改函数体会波及大量无关
   结构体（曾导致黑屏）→ 只改具体指令。

## 版本适配

游戏更新后若偏移变化，日志会明确提示：

- `[-] XXX: pattern not found` → 该函数内找不到原始字节，需重新反汇编定位
- `[!] XXX: pattern matched N times, ambiguous, skipped` → 命中多处，说明该片段被复用
- `[!] writeBack: pattern found N times` → 跳板定位失败，已自动降级
- `[=] ... patched=N` 小于 5 → 有目标未找到

定位方法：静态反汇编 `GameAssembly.dll`（磁盘明文），按方法名枚举找到原生入口，
在函数内搜索层数读取指令（`8B 53 60` / `8B 5F 60` / `41 8B 57 60` 等）。
