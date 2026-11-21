# PotentialFix — 满潜修复（最终版）

## 效果（已实测确认）

所有干员（含 0 潜）在客户端按 **N 层潜能**（默认 5）处理：

| 项 | 状态 |
|---|---|
| 属性加成（`modifyType 4`） | ✅ 0 潜昼雪意志 +20 |
| 技能参数（`modifyType 3`） | ✅ `atk_scale`、`sword_duration`、`atb_return` 等 |
| buff 挂载（`modifyType 5`） | ✅ `buff_chr_XXXX_potentialN` |
| **潜能页面 5 个节点点亮** | ✅ |
| **角色满潜特效** | ✅ |

## 原理：五处补丁

`GameAssembly.dll` 磁盘上是明文（约 320MB），可直接静态反汇编。
潜能层数在运行时 `CharInfo.potentialLevel`，字段表实测：

```
[fields] === CharInfo ===   (43 fields)
  instId +0x10 / templateId +0x18 / level +0x44 / breakStage +0x4C
  potentialLevel          +0x60   ← 层数，唯一标量
  potentialCgIds          +0x68
  potentialUnlockTs       +0x70
  potentialAttributeModifier  +0xE0
```

**关键认知：`CharInfo` 里层数只有 `potentialLevel` 一个标量，没有任何"按层解锁数组/位图"。**
所以页面第 i 个节点亮不亮，判据就是 `potentialLevel >= i`。只改"读到的值"或"判定条件"都不会点亮，
必须把 `+0x60` 字段本身写成 5。

| # | 位置 | 手法 | 作用 |
|---|---|---|---|
| 1 | `CharInfo::_RefreshPotentialAttribute` `+0x30` | **跳板**（见下） | 把 `[rbx+0x60]` 写成 5 ← 页面与特效的关键 |
| 2 | `CharInfo::_RefreshPotentialModifiedSkillData` `+0x377` | `8B 5F 60` → `6A 05 5B` | 技能参数按 5 层算 |
| 3 | `ClientCharInfo::_InitPotentialCardSkill` `+0xCC` | `8B 53 60` → `6A 05 5A` | 潜能卡片按 5 层算 |
| 4 | `CharMiscFeature::RefreshPotentialBuff` `+0x488` | `41 8B 57 60` → `6A 05 5A 90` | buff 按 5 层挂 |
| 5 | `CharMiscFeature::RefreshMaxPotentialEffectShow` `+0x18B` | 10 字节 → `C6 47 60 05` + nop×6 | 写回层数 + 恒走满潜分支 |

### 跳板（补丁 #1）

`+0x30` 处的 7 字节窗口装不下完整动作（需 11 字节），因此改为跳转执行：

```
原:  8B 53 60        mov edx, [rbx+0x60]
     48 8B 4B 18     mov rcx, [rbx+0x18]

入口: E9 rel32       jmp 洞穴          （后接 90 90）

洞穴（.text 段 rva=0x1092A5，3083 字节 CC 填充）:
     C6 43 60 05     mov byte [rbx+0x60], 5   ← 关键：写回对象
     6A 05           push 5
     5A              pop rdx                 → edx = 5
     48 8B 4B 18     mov rcx, [rbx+0x18]     ← 复原被搬走的指令
     E9 rel32        jmp site+7              ← 回到原 call 之前
```

三重保险：7 字节模式**唯一性扫描**（不硬编码偏移）、`E9 rel32` 可达性校验、
任一失败自动回退到原 3 字节读取补丁（属性不受影响）。

## 配置

```ini
[lift]
enabled=1       ; 总开关
target=5        ; 目标层数 1..5
writeBack=1     ; 1=写回字段（页面点亮）；0=只改读取值（页面不亮）
```

`[ui]` / `[display]` / `[call]` 三段是验证无效的方案，默认关闭，保留供后续排查。

## 关闭方法

- 只关页面/特效：`[lift] writeBack=0`（属性、技能、buff 仍生效）
- 全关：`[lift] enabled=0`（恢复原版）

均无需替换或还原文件。

## 实测日志

```
=== PotentialFix loaded pid=40364 enabled=1 target=5 ===
[*] resolved 12/12 exports
[*] assemblies=172
[+] writeBack trampoline: site +0x30 -> module-cave, writes [rbx+0x60]=5
[+] _RefreshPotentialModifiedSkillData +0x377 : 8B 5F 60 -> 6A 05 5B
[+] _InitPotentialCardSkill +0xCC : 8B 53 60 -> 6A 05 5A
[+] RefreshPotentialBuff +0x488 : 41 8B 57 60 -> 6A 05 5A 90
[+] RefreshMaxPotentialEffectShow +0x18B : 83 7F 60 05 0F 82 AF 02 00 00 -> C6 47 60 05 90 ×6
[=] classes=9465 methods=58955 | patched=5 getters=0 skipped=0 target=5
```

## 排查记录：七条走过的弯路

| 方案 | 结果 | 原因 |
|---|---|---|
| 改 `MethodInfo::methodPointer` | 无效 | il2cpp 直接 call 原生地址，`entered=0` |
| 钉 `SERVER_SKILL::get_PotentialLv` 返回 5 | 黑屏 | 该值被当技能表下标，越界 |
| 改 `CreatePotentialAttributeModifier` 的 r8 | 卡死 | 层数其实是 rdx（第 2 参） |
| 改 `potentialLevel` 字段（早期） | 越界崩溃 | 当时改的是错的偏移，且未区分对象 |
| 无条件覆写所有 `get_PotentialLevel` 函数体 | **黑屏 ×2** | ①立绘按层索引越界；②IL2CPP **ICF** 合并，同址 30 个无关结构体被一起改 |
| 改 10 处 `cmp [reg+0x74],5` / 3 处 `call get_PotentialLevel` / 单 getter 覆写 | 全部无效 | 页面读的是字段本身，不是这些判定点 |
| **跳板写回字段** | **成功** | 直接改数据，绕开所有判定层 |

### 两条重要的方法论教训

1. **ICF（相同代码折叠）**：`8B 41 28 C3` 全镜像出现 30 次、`89 51 28 C3` 14 次、
   `89 51 74 C3` 4 次。改这类共享 thunk 会波及大量无关结构体 —— 必须先做全镜像唯一性校验。
2. **IL2CPP 内联**：简单属性 setter / static getter 被内联，`E8` 调用点扫描会返回 0
   （`set_PotentialLevel`、`get_characterPotentialTable` 均如此），静态调用图分析在此失效。

### IFix 假设已验证为否

游戏确实集成了 IFix（metadata 含 `IFix.Core`、`WrappersManagerImpl`、`VirtualMachine`、
`PatchManager`、`Interpret`、`HotfixManager`、`PatchBridge`），但**未接管本项目涉及的方法**：

- 运行时函数头全是正常编译序言（无 `E9`/`EB`/`FF 25`）
- 带硬编码 `rel32 = AF 02 00 00` 的 10 字节模式精确匹配成功
- 3 处调用点的 `rel32` 精确解析到 `0x2271A60`

即补丁确实在执行，问题不在 IFix。

## 产物

- `PotentialFix.dll` 212,480 字节，SHA256
  `6D0D1377BAFF934B75C47A25E85E968B58640D89367D94BC131F292628B771B0`
- `PotentialFix.ini`
- `LauncherDlls.txt`：`Cheat.dll` + `PotentialFix.dll`
- 源码：`PotentialFix.cpp` + `build.cmd` + `PotentialFix.def`
- 本地测试壳：`fakega_slim.cpp` + `host_slim.cpp` + `run_slim.cmd`
