// PotentialFix.cpp — 满潜修复（精简版）
//
// 作用：所有干员（含 0 潜）按 N 层潜能（默认 5）计算，潜能页面 5 个节点全亮，
//       角色显示满潜特效。
//
// 三条核心事实（均经反汇编 + 实测确认）：
//   1. 运行时 CharInfo.potentialLevel 在 +0x60，是层数唯一的标量字段。
//      页面第 i 个节点亮不亮 = (potentialLevel >= i)，所以必须把该字段写成 N；
//      只改"读到的值"或"判定条件"都不会点亮。
//   2. 属性 / 技能参数 / buff 三条路径各自读 [obj+0x60]，读到的值也必须是 N。
//   3. IL2CPP 有 ICF（相同函数体合并）：get_PotentialLevel 的 4 字节函数体在全镜像
//      出现 30 次，与 SERVER_SKILL::get_PotentialLv 等同址。改函数体会波及一堆无关
//      结构体（曾导致黑屏）→ 只改具体指令，绝不碰共享 thunk。
//
// 五处补丁：
//   #1 CharInfo::_RefreshPotentialAttribute        +0x30   跳板：写 [rbx+0x60] = N
//   #2 CharInfo::_RefreshPotentialModifiedSkillData +0x377  8B 5F 60    -> 6A N 5B
//   #3 ClientCharInfo::_InitPotentialCardSkill     +0xCC   8B 53 60    -> 6A N 5A
//   #4 CharMiscFeature::RefreshPotentialBuff       +0x488  41 8B 57 60 -> 6A N 5A 90
//   #5 CharMiscFeature::RefreshMaxPotentialEffectShow +0x18B
//        83 7F 60 05 0F 82 AF 02 00 00 -> C6 47 60 05 90×6（写回层数 + 恒走满潜分支）
//
// 配置：ini 里 [lift] enabled / target / writeBack。
// 关闭：writeBack=0 只关页面与特效；enabled=0 全关。均无需还原文件。

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <set>

// ---------------- il2cpp 导出（只保留用到的最小集） ----------------
typedef struct Il2CppDomain   Il2CppDomain;
typedef struct Il2CppAssembly Il2CppAssembly;
typedef struct Il2CppImage    Il2CppImage;
typedef struct Il2CppClass    Il2CppClass;
typedef struct MethodInfo     MethodInfo;

static Il2CppDomain*    (*p_domain_get)();
static Il2CppAssembly** (*p_domain_get_assemblies)(const Il2CppDomain*, size_t*);
static Il2CppImage*     (*p_assembly_get_image)(const Il2CppAssembly*);
static size_t           (*p_image_get_class_count)(const Il2CppImage*);
static Il2CppClass*     (*p_image_get_class)(const Il2CppImage*, size_t);
static MethodInfo*      (*p_class_get_methods)(Il2CppClass*, void**);
static const char*      (*p_method_get_name)(const MethodInfo*);
static void*            (*p_thread_attach)(Il2CppDomain*);
static const char*      (*p_class_get_name)(Il2CppClass*);

struct MethodInfoHead { void* methodPointer; };   // MethodInfo 首字段即原生入口

// ---------------- 配置与全局状态 ----------------
static bool     g_enabled   = true;
static int      g_target    = 5;
static bool     g_writeBack = true;
static int      g_startMs   = 2500;
static bool     g_verbose   = true;

static int      g_patched   = 0;
static int      g_skipped   = 0;
static uint8_t* g_gaBase    = nullptr;

static FILE*            g_log = nullptr;
static CRITICAL_SECTION g_cs;

static void Log(const char* fmt, ...)
{
    if (!g_log) return;
    EnterCriticalSection(&g_cs);
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_cs);
}

static std::string Hex(const std::vector<uint8_t>& v)
{
    std::string s;
    char buf[4];
    for (uint8_t b : v) { sprintf_s(buf, "%02X ", b); s += buf; }
    if (!s.empty()) s.pop_back();
    return s;
}

// ---------------- 补丁目标 ----------------
struct Target {
    const char*          method;   // 方法名，用于在 il2cpp 里定位原生入口
    int                  scanLen;  // 在该函数前 scanLen 字节内搜索
    std::vector<uint8_t> pattern;  // 原始字节
    std::vector<uint8_t> patch;    // 替换字节（必须等长）
};

static std::vector<Target> BuildTargets(int level)
{
    const uint8_t n = (uint8_t)level;
    std::vector<Target> v;

    // #1 属性刷新。正常情况下由跳板接管（见 InstallLevelWriteTrampoline）；
    //    这里保留等长替换作为跳板失败时的降级方案 —— 属性仍按 N 层算，只是不写字段。
    v.push_back({ "_RefreshPotentialAttribute", 0x100,
                  { 0x8B, 0x53, 0x60 }, { 0x6A, n, 0x5A } });

    // #2 技能参数
    v.push_back({ "_RefreshPotentialModifiedSkillData", 0x400,
                  { 0x8B, 0x5F, 0x60 }, { 0x6A, n, 0x5B } });

    // #3 潜能卡片
    v.push_back({ "_InitPotentialCardSkill", 0x200,
                  { 0x8B, 0x53, 0x60 }, { 0x6A, n, 0x5A } });

    // #4 buff（4 字节，带 REX 前缀）
    v.push_back({ "RefreshPotentialBuff", 0x800,
                  { 0x41, 0x8B, 0x57, 0x60 }, { 0x6A, n, 0x5A, 0x90 } });

    // #5 满潜特效判定 + 写回层数
    //    原: cmp dword [rdi+0x60],5 ; jb 跳过特效
    //    新: mov byte [rdi+0x60],5  ; nop×6（恒走满潜分支）
    v.push_back({ "RefreshMaxPotentialEffectShow", 0x300,
                  { 0x83, 0x7F, 0x60, 0x05, 0x0F, 0x82, 0xAF, 0x02, 0x00, 0x00 },
                  { 0xC6, 0x47, 0x60, 0x05, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } });

    return v;
}

// ---------------- 等长替换 ----------------
// 要求 pattern 在扫描窗口内唯一命中：命中 0 处说明偏移变了，命中多处说明这是
// 多份复用/被 ICF 合并的代码片段，两种情况都宁可跳过也不动手。
static bool PatchOnce(void* fn, const Target& t)
{
    if (!fn || t.pattern.empty() || t.pattern.size() != t.patch.size()) return false;
    uint8_t* p = (uint8_t*)fn;
    const int n = (int)t.pattern.size();

    int found = -1, count = 0;
    for (int i = 0; i + n <= t.scanLen; i++) {
        bool ok = true;
        for (int k = 0; k < n; k++) if (p[i + k] != t.pattern[k]) { ok = false; break; }
        if (ok) { if (found < 0) found = i; count++; }
    }

    if (count == 0) {
        if (g_verbose) Log("[-] %s: pattern not found", t.method);
        return false;
    }
    if (count > 1) {
        Log("[!] %s: pattern matched %d times, ambiguous, skipped", t.method, count);
        return false;
    }

    DWORD old = 0;
    if (!VirtualProtect(p + found, 0x10, PAGE_EXECUTE_READWRITE, &old)) {
        Log("[!] %s: VirtualProtect failed %lu", t.method, GetLastError());
        return false;
    }
    memcpy(p + found, t.patch.data(), n);
    FlushInstructionCache(GetCurrentProcess(), p + found, 0x10);
    VirtualProtect(p + found, 0x10, old, &old);

    if (g_verbose)
        Log("[+] %s +0x%X : %s -> %s", t.method, found,
            Hex(t.pattern).c_str(), Hex(t.patch).c_str());
    return true;
}

// ---------------- 代码洞 ----------------
// 读 PE 头拿镜像大小。模块约 320MB，不能用固定常数。
static size_t ModuleImageSize(uint8_t* base)
{
    if (!base) return 0;
    __try {
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        return nt->OptionalHeader.SizeOfImage;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// 在模块的可执行区域里找一段连续填充（int3 或 0），用作代码洞。
static uint8_t* FindCodeCave(size_t need)
{
    if (!g_gaBase) return nullptr;
    size_t imgSize = ModuleImageSize(g_gaBase);
    if (!imgSize || imgSize > 0x20000000ULL) imgSize = 0x20000000ULL;   // 上限 512MB，防解析异常
    uint8_t* endAll = g_gaBase + imgSize;

    for (uint8_t* cur = g_gaBase; cur < endAll; ) {
        MEMORY_BASIC_INFORMATION m = {};
        if (!VirtualQuery(cur, &m, sizeof(m))) break;
        uint8_t* rbase = (uint8_t*)m.BaseAddress;
        uint8_t* rend  = rbase + m.RegionSize;

        bool exec = (m.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                  PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
        if (m.State == MEM_COMMIT && exec && !(m.Protect & PAGE_GUARD)) {
            uint8_t* s = (cur > rbase ? cur : rbase);
            uint8_t* e = (rend < endAll ? rend : endAll);
            size_t run = 0;
            for (uint8_t* q = s; q < e; q++) {
                if (*q == 0xCC || *q == 0x00) {
                    if (++run >= need) return q - run + 1;
                } else {
                    run = 0;
                }
            }
        }
        if (rend <= cur) break;
        cur = rend;
    }
    return nullptr;
}

// 兜底：模块附近（±1GB 内）自行分配一块可执行页。
static uint8_t* AllocNearModule(size_t need)
{
    if (!g_gaBase) return nullptr;
    uint8_t* cur   = g_gaBase;
    uint8_t* limit = g_gaBase + 0x40000000ULL;
    for (int guard = 0; guard < 200000 && cur < limit; guard++) {
        MEMORY_BASIC_INFORMATION m = {};
        if (!VirtualQuery(cur, &m, sizeof(m))) break;
        uint8_t* rbase = (uint8_t*)m.BaseAddress;
        uint8_t* rend  = rbase + m.RegionSize;
        if (m.State == MEM_FREE && m.RegionSize >= need) {
            int64_t d = (int64_t)rbase - (int64_t)g_gaBase;
            if (d > -0x40000000LL && d < 0x40000000LL) {
                void* got = VirtualAlloc(rbase, need, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (got) return (uint8_t*)got;
            }
        }
        if (rend <= cur) break;
        cur = rend;
    }
    return nullptr;
}

// ---------------- #1 跳板：把层数写回对象 ----------------
//
// 原 7 字节（+0x30）：
//   8B 53 60        mov edx, [rbx+0x60]
//   48 8B 4B 18     mov rcx, [rbx+0x18]
// 就地改写需要 11 字节，放不下，所以跳到代码洞里做完整动作再跳回：
//   C6 43 60 05     mov byte [rbx+0x60], N    ← 关键：把层数真正写进 CharInfo
//   6A N 5A         push N ; pop rdx          → edx = N
//   48 8B 4B 18     mov rcx, [rbx+0x18]       ← 复原被搬走的指令
//   E9 rel32        jmp site+7                ← 回到原来的 call 之前
static bool InstallLevelWriteTrampoline(void* fn, int level)
{
    if (!fn) return false;

    // 在函数前 0x100 字节内定位那 7 字节（不硬编码偏移）
    uint8_t* base = (uint8_t*)fn;
    const uint8_t expect[7] = { 0x8B, 0x53, 0x60, 0x48, 0x8B, 0x4B, 0x18 };
    uint8_t* site = nullptr;
    int hits = 0;
    for (int i = 0; i + 7 <= 0x100; i++) {
        if (memcmp(base + i, expect, 7) == 0) {
            if (!site) site = base + i;
            if (++hits > 1) break;
        }
    }
    if (!site || hits != 1) {
        Log("[!] writeBack: pattern found %d times, skipped", hits);
        return false;
    }

    uint8_t* cave = FindCodeCave(24);
    const char* caveSrc = "module-cave";
    if (!cave) { cave = AllocNearModule(24); caveSrc = "allocated"; }
    if (!cave) { Log("[!] writeBack: no code cave and no nearby free page"); return false; }

    // e9 rel32 可达性（同模块内必然满足，这里兜底）
    int64_t fwd = (int64_t)cave - (int64_t)(site + 5);
    if (fwd < INT32_MIN || fwd > INT32_MAX) {
        Log("[!] writeBack: cave too far (%lld)", (long long)fwd);
        return false;
    }

    uint8_t code[16];
    int n = 0;
    code[n++] = 0xC6; code[n++] = 0x43; code[n++] = 0x60; code[n++] = (uint8_t)level;
    code[n++] = 0x6A; code[n++] = (uint8_t)level;
    code[n++] = 0x5A;
    code[n++] = 0x48; code[n++] = 0x8B; code[n++] = 0x4B; code[n++] = 0x18;
    code[n++] = 0xE9;
    int32_t back = (int32_t)((int64_t)(site + 7) - (int64_t)(cave + 16));
    memcpy(code + 12, &back, 4);          // 必须把 rel32 一起拷进去（共 16 字节）

    DWORD old = 0;
    if (!VirtualProtect(cave, 32, PAGE_EXECUTE_READWRITE, &old)) {
        Log("[!] writeBack: cave VirtualProtect failed %lu", GetLastError());
        return false;
    }
    memcpy(cave, code, 16);
    FlushInstructionCache(GetCurrentProcess(), cave, 32);
    VirtualProtect(cave, 32, old, &old);

    uint8_t patch[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
    int32_t frel = (int32_t)fwd;
    memcpy(patch + 1, &frel, 4);

    if (!VirtualProtect(site, 16, PAGE_EXECUTE_READWRITE, &old)) {
        Log("[!] writeBack: site VirtualProtect failed %lu", GetLastError());
        return false;
    }
    memcpy(site, patch, 7);
    FlushInstructionCache(GetCurrentProcess(), site, 16);
    VirtualProtect(site, 16, old, &old);

    Log("[+] writeBack trampoline: site +0x%X -> %s, writes [rbx+0x60]=%d",
        (int)(site - base), caveSrc, level);
    return true;
}

// ---------------- 应用补丁 ----------------
static bool ApplyOne(void* fn, const Target& t)
{
    // 属性刷新优先用跳板把层数写回对象；失败则降级为只改读取值
    if (g_writeBack && strcmp(t.method, "_RefreshPotentialAttribute") == 0) {
        if (InstallLevelWriteTrampoline(fn, g_target)) return true;
        Log("[-] writeBack failed, falling back to read-patch for %s", t.method);
    }
    return PatchOnce(fn, t);
}

static void ScanAndPatch()
{
    std::vector<Target> targets = BuildTargets(g_target);
    std::vector<bool>   done(targets.size(), false);
    int remaining = (int)targets.size();

    Il2CppDomain* dom = p_domain_get ? p_domain_get() : nullptr;
    if (!dom) { Log("[-] domain not ready"); return; }
    if (p_thread_attach) p_thread_attach(dom);

    size_t nAsm = 0;
    Il2CppAssembly** asms = p_domain_get_assemblies ? p_domain_get_assemblies(dom, &nAsm) : nullptr;
    if (!asms || !nAsm) { Log("[-] no assemblies"); return; }
    Log("[*] assemblies=%zu", nAsm);

    std::set<void*> seen;      // 同一原生函数可能被多份 MethodInfo 引用，只处理一次
    int classes = 0, methods = 0;

    for (size_t i = 0; i < nAsm && remaining > 0; i++) {
        Il2CppImage* img = p_assembly_get_image ? p_assembly_get_image(asms[i]) : nullptr;
        if (!img) continue;
        size_t nc = p_image_get_class_count ? p_image_get_class_count(img) : 0;
        for (size_t c = 0; c < nc && remaining > 0; c++) {
            Il2CppClass* k = p_image_get_class(img, c);
            if (!k) continue;
            classes++;

            void* it = nullptr;
            MethodInfo* mi = nullptr;
            while (p_class_get_methods && (mi = p_class_get_methods(k, &it)) != nullptr) {
                methods++;
                const char* name = p_method_get_name ? p_method_get_name(mi) : nullptr;
                if (!name) continue;

                void* fn = reinterpret_cast<MethodInfoHead*>(mi)->methodPointer;
                if (!fn || !seen.insert(fn).second) continue;

                for (size_t t = 0; t < targets.size(); t++) {
                    if (done[t] || strcmp(targets[t].method, name) != 0) continue;
                    if (ApplyOne(fn, targets[t])) { g_patched++; done[t] = true; remaining--; }
                    else g_skipped++;
                }
            }
        }
    }

    Log("[=] classes=%d methods=%d | patched=%d skipped=%d target=%d",
        classes, methods, g_patched, g_skipped, g_target);
    if (g_patched == 0) Log("[!] nothing patched — check GameAssembly version / offsets");
    else if (remaining > 0) Log("[!] %d target(s) not found", remaining);
}

// ---------------- 加载 ----------------
static HMODULE WaitGameAssembly(int ms)
{
    for (int i = 0; i < ms / 100; i++) {
        HMODULE h = GetModuleHandleW(L"GameAssembly.dll");
        if (h) return h;
        Sleep(100);
    }
    return nullptr;
}

static bool ResolveApi(HMODULE h)
{
    struct { const char* name; void** slot; } tbl[] = {
        { "il2cpp_domain_get",            (void**)&p_domain_get },
        { "il2cpp_domain_get_assemblies", (void**)&p_domain_get_assemblies },
        { "il2cpp_assembly_get_image",    (void**)&p_assembly_get_image },
        { "il2cpp_image_get_class_count", (void**)&p_image_get_class_count },
        { "il2cpp_image_get_class",       (void**)&p_image_get_class },
        { "il2cpp_class_get_methods",     (void**)&p_class_get_methods },
        { "il2cpp_method_get_name",       (void**)&p_method_get_name },
        { "il2cpp_thread_attach",         (void**)&p_thread_attach },
        { "il2cpp_class_get_name",        (void**)&p_class_get_name },
    };
    int ok = 0;
    for (auto& e : tbl) {
        *e.slot = (void*)GetProcAddress(h, e.name);
        if (*e.slot) ok++;
        else Log("[!] missing export %s", e.name);
    }
    Log("[*] resolved %d/%zu exports", ok, sizeof(tbl) / sizeof(tbl[0]));
    return ok >= 7;
}

static DWORD WINAPI Worker(LPVOID)
{
    HMODULE h = WaitGameAssembly(60000);
    if (!h) { Log("[-] GameAssembly.dll not found"); return 0; }
    g_gaBase = (uint8_t*)h;
    Log("[*] GameAssembly at %p", h);
    if (!ResolveApi(h)) { Log("[-] resolve failed"); return 0; }

    // 等 il2cpp 域就绪
    for (int i = 0; i < 90; i++) {
        Sleep(i == 0 ? g_startMs : 1000);
        Il2CppDomain* dom = p_domain_get ? p_domain_get() : nullptr;
        if (!dom) continue;
        size_t n = 0;
        Il2CppAssembly** a = p_domain_get_assemblies(dom, &n);
        if (a && n > 0) break;
    }

    ScanAndPatch();
    return 0;
}

static void LoadConfig()
{
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t* slash = wcsrchr(exe, L'\\');
    std::wstring dir = slash ? std::wstring(exe, slash - exe + 1) : std::wstring(L".\\");
    std::wstring ini = dir + L"PotentialFix.ini";

    g_enabled   = GetPrivateProfileIntW(L"lift", L"enabled",   1, ini.c_str()) != 0;
    g_writeBack = GetPrivateProfileIntW(L"lift", L"writeBack", 1, ini.c_str()) != 0;
    g_target    = GetPrivateProfileIntW(L"lift", L"target",    5, ini.c_str());
    if (g_target < 1) g_target = 1;
    if (g_target > 5) g_target = 5;

    g_startMs = GetPrivateProfileIntW(L"safety", L"startDelayMs", 2500, ini.c_str());
    g_verbose = GetPrivateProfileIntW(L"debug",  L"verbose",      1, ini.c_str()) != 0;

    std::wstring lp = dir + L"PotentialFix.log";
    _wfopen_s(&g_log, lp.c_str(), L"a+b");   // 必须二进制，配合 ccs 才不会崩
}

extern "C" __declspec(dllexport) int PotentialFixLevel() { return g_target; }

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        InitializeCriticalSection(&g_cs);
        LoadConfig();
        Log("=== PotentialFix loaded pid=%lu enabled=%d target=%d writeBack=%d ===",
            GetCurrentProcessId(), g_enabled ? 1 : 0, g_target, g_writeBack ? 1 : 0);
        if (g_enabled) CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        else Log("[=] disabled by ini, nothing to do");
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_log) { Log("=== detach patched=%d ===", g_patched); fclose(g_log); g_log = nullptr; }
    }
    return TRUE;
}
