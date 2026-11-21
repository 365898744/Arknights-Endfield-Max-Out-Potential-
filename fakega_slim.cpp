// fakega_slim.cpp - 验收：三处层数读取点 + 潜能页面 getter
#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

struct Il2CppType { uint8_t raw[16]; };
struct MethodInfo { void* methodPointer; const char* name; uint32_t params; bool inst; Il2CppType ret; };
struct Il2CppClass { const char* name; void* pad; std::vector<MethodInfo*> methods; };

static void SetType(Il2CppType* t, uint8_t e) { memset(t->raw, 0, 16); t->raw[10] = e; }
static Il2CppType T_INT;

static MethodInfo M_ATTR  = {nullptr, "_RefreshPotentialAttribute",       0, true, {}};
static MethodInfo M_SKILL = {nullptr, "_RefreshPotentialModifiedSkillData", 0, true, {}};
static MethodInfo M_BUFF  = {nullptr, "RefreshPotentialBuff",             0, true, {}};
static MethodInfo M_GETL  = {nullptr, "get_PotentialLevel",               0, true, {}};
static MethodInfo M_GETL2 = {nullptr, "get_PotentialLevel",               0, true, {}};
static MethodInfo M_CARD  = {nullptr, "_InitPotentialCardSkill",          0, true, {}};
static MethodInfo M_VFX   = {nullptr, "RefreshMaxPotentialEffectShow",    0, true, {}};

static Il2CppClass C_CHAR  = {"CHAR_INFO",   nullptr, {&M_ATTR, &M_SKILL, &M_GETL}};
static Il2CppClass C_MISC  = {"CharMiscFeature", nullptr, {&M_BUFF, &M_VFX}};
static Il2CppClass C_CARD  = {"ClientCharInfo", nullptr, {&M_CARD}};
static Il2CppClass C_SKILL = {"SERVER_SKILL", nullptr, {&M_GETL2}};

static void* g_asms[1];
static void* g_obj = nullptr;

extern "C" {
__declspec(dllexport) void* __stdcall il2cpp_domain_get() { static int d; return &d; }
__declspec(dllexport) void** __stdcall il2cpp_domain_get_assemblies(void*, size_t* n)
{ *n = 1; g_asms[0] = (void*)1; return (void**)g_asms; }
__declspec(dllexport) void* __stdcall il2cpp_assembly_get_image(void* a) { return a; }
__declspec(dllexport) size_t __stdcall il2cpp_image_get_class_count(void*) { return 4; }
__declspec(dllexport) void* __stdcall il2cpp_image_get_class(void*, size_t k)
{ return k == 0 ? (void*)&C_CHAR : (k == 1 ? (void*)&C_MISC : (k == 2 ? (void*)&C_SKILL : (void*)&C_CARD)); }
__declspec(dllexport) void* __stdcall il2cpp_class_get_methods(void* k, void** it)
{
    Il2CppClass* c = (Il2CppClass*)k; size_t i = (size_t)*it;
    if (i >= c->methods.size()) return nullptr; *it = (void*)(i + 1); return c->methods[i];
}
__declspec(dllexport) const char* __stdcall il2cpp_method_get_name(void* m) { return ((MethodInfo*)m)->name; }
__declspec(dllexport) void* __stdcall il2cpp_thread_attach(void*) { static int t; return &t; }
__declspec(dllexport) const char* __stdcall il2cpp_class_get_name(void* k) { return ((Il2CppClass*)k)->name; }

__declspec(dllexport) void* __stdcall MakeObj()
{
    if (!g_obj) {
        g_obj = VirtualAlloc(nullptr, 0x400, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        *(void**)g_obj = &C_CHAR;
        *(int64_t*)((char*)g_obj + 0x38) = 0x1111;
        *(int64_t*)((char*)g_obj + 0x60) = 1;      // 三处补丁读这里
        *(int32_t*)((char*)g_obj + 0x20) = 0;      // getter 读这里（0 潜）
    }
    return g_obj;
}
__declspec(dllexport) int __stdcall CallAttr(void* s)  { return ((int(*)(void*))M_ATTR.methodPointer)(s); }
__declspec(dllexport) int __stdcall CallSkill(void* s) { return ((int(*)(void*))M_SKILL.methodPointer)(s); }
__declspec(dllexport) int __stdcall CallBuff(void* s)  { return ((int(*)(void*))M_BUFF.methodPointer)(s); }
__declspec(dllexport) int __stdcall CallGetLv(void* s) { return ((int(*)(void*))M_GETL.methodPointer)(s); }
__declspec(dllexport) int __stdcall CallSkillLv(void* s) { return ((int(*)(void*))M_GETL2.methodPointer)(s); }
__declspec(dllexport) int __stdcall CallCard(void* s) { return ((int(*)(void*))M_CARD.methodPointer)(s); }
__declspec(dllexport) int __stdcall CallVfx(void* s)  { return ((int(*)(void*))M_VFX.methodPointer)(s); }
__declspec(dllexport) int __stdcall GetField() { return g_obj ? *(int*)((char*)g_obj + 0x60) : -1; }
__declspec(dllexport) unsigned __stdcall GetterRva()
{
    HMODULE h = GetModuleHandleW(L"GameAssembly.dll");
    return (unsigned)((uint8_t*)M_GETL.methodPointer - (uint8_t*)h);
}
}

// 桩：序言(6B) + 读 [this+off] + 返回 + 收尾
static void* MakeStub(int fillLen, int mode)
{
    uint8_t* p = (uint8_t*)VirtualAlloc(nullptr, 0x400, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    int n = 0;

    if (mode == 3) {          // getter：无栈帧，读 +0x60（与运行时 CharInfo.PotentialLevel 同偏移）
        p[n++] = 0x8B; p[n++] = 0x41; p[n++] = 0x60; p[n++] = 0xC3;
        FlushInstructionCache(GetCurrentProcess(), p, 0x400);
        return p;
    }

    p[n++] = 0x40; p[n++] = 0x53;
    p[n++] = 0x48; p[n++] = 0x83; p[n++] = 0xEC; p[n++] = 0x20;
    p[n++] = 0x48; p[n++] = 0x8B; p[n++] = 0xD9;              // mov rbx,rcx
    for (int i = 0; i < fillLen; i++) p[n++] = 0x90;
    if (mode == 0) {          // 真实形状：mov edx,[rbx+0x60] ; mov rcx,[rbx+0x18] ; mov eax,edx
        p[n++] = 0x8B; p[n++] = 0x53; p[n++] = 0x60;
        p[n++] = 0x48; p[n++] = 0x8B; p[n++] = 0x4B; p[n++] = 0x18;
        p[n++] = 0x89; p[n++] = 0xD0;
    } else if (mode == 1) {   // mov ebx,[rdi+0x60]；rdi 是 callee-saved，需保存
        p[n++] = 0x48; p[n++] = 0x89; p[n++] = 0x7C; p[n++] = 0x24; p[n++] = 0x10; // mov [rsp+0x10],rdi
        p[n++] = 0x48; p[n++] = 0x8B; p[n++] = 0xF9;                              // mov rdi,rcx
        p[n++] = 0x8B; p[n++] = 0x5F; p[n++] = 0x60;                              // mov ebx,[rdi+0x60]
        p[n++] = 0x89; p[n++] = 0xD8;                                             // mov eax,ebx
        p[n++] = 0x48; p[n++] = 0x8B; p[n++] = 0x7C; p[n++] = 0x24; p[n++] = 0x10; // mov rdi,[rsp+0x10]
    } else {                  // mode 2：真实形状 41 8B 57 60 = mov edx,[r15+0x60]
        p[n++] = 0x4C; p[n++] = 0x89; p[n++] = 0x7C; p[n++] = 0x24; p[n++] = 0x18; // mov [rsp+0x18],r15
        p[n++] = 0x4C; p[n++] = 0x8B; p[n++] = 0xF9;                              // mov r15,rcx
        p[n++] = 0x41; p[n++] = 0x8B; p[n++] = 0x57; p[n++] = 0x60;               // mov edx,[r15+0x60]
        p[n++] = 0x89; p[n++] = 0xD0;                                             // mov eax,edx
        p[n++] = 0x4C; p[n++] = 0x8B; p[n++] = 0x7C; p[n++] = 0x24; p[n++] = 0x18; // mov r15,[rsp+0x18]
    }
    p[n++] = 0x48; p[n++] = 0x83; p[n++] = 0xC4; p[n++] = 0x20;
    p[n++] = 0x5B; p[n++] = 0xC3;
    FlushInstructionCache(GetCurrentProcess(), p, 0x400);
    return p;
}

// 满潜特效判定桩：cmp dword [rdi+0x60],5 / jb skip / return 1 else 0
static void* MakeVfxStub()
{
    uint8_t* p = (uint8_t*)VirtualAlloc(nullptr, 0x400, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memset(p, 0xCC, 0x400);
    int n = 0;
    p[n++]=0x40; p[n++]=0x53;                                   // push rbx
    p[n++]=0x48; p[n++]=0x83; p[n++]=0xEC; p[n++]=0x20;         // sub rsp,0x20
    while (n < 0x45) p[n++]=0x90;                               // align
    p[n++]=0x48; p[n++]=0x89; p[n++]=0x7C; p[n++]=0x24; p[n++]=0x10; // mov [rsp+0x10],rdi
    p[n++]=0x48; p[n++]=0x8B; p[n++]=0xF9;                      // mov rdi,rcx
    p[n++]=0x83; p[n++]=0x7F; p[n++]=0x60; p[n++]=0x05;         // cmp dword [rdi+0x60],5
    p[n++]=0x0F; p[n++]=0x82; p[n++]=0xAF; p[n++]=0x02; p[n++]=0x00; p[n++]=0x00; // jb 0x306
    p[n++]=0xB8; p[n++]=0x01; p[n++]=0x00; p[n++]=0x00; p[n++]=0x00; // mov eax,1
    p[n++]=0x48; p[n++]=0x8B; p[n++]=0x7C; p[n++]=0x24; p[n++]=0x10; // mov rdi,[rsp+0x10]
    p[n++]=0x48; p[n++]=0x83; p[n++]=0xC4; p[n++]=0x20;         // add rsp,0x20
    p[n++]=0x5B; p[n++]=0xC3;                                   // pop rbx ; ret
    // 跳转目标 0x306：level<5 分支，返回 0 并正常收尾
    n = 0x306;
    p[n++]=0xB8; p[n++]=0x00; p[n++]=0x00; p[n++]=0x00; p[n++]=0x00; // mov eax,0
    p[n++]=0x48; p[n++]=0x8B; p[n++]=0x7C; p[n++]=0x24; p[n++]=0x10; // mov rdi,[rsp+0x10]
    p[n++]=0x48; p[n++]=0x83; p[n++]=0xC4; p[n++]=0x20;         // add rsp,0x20
    p[n++]=0x5B; p[n++]=0xC3;                                   // pop rbx ; ret
    FlushInstructionCache(GetCurrentProcess(), p, 0x400);
    return p;
}



BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        SetType(&T_INT, 0x08);
        M_ATTR.ret = T_INT; M_SKILL.ret = T_INT; M_BUFF.ret = T_INT;
        M_GETL.ret = T_INT; M_GETL2.ret = T_INT;
        M_ATTR.methodPointer  = MakeStub(0x24, 0);
        M_SKILL.methodPointer = MakeStub(0x30, 1);
        M_BUFF.methodPointer  = MakeStub(0x40, 2);
        M_CARD.methodPointer  = MakeStub(0x90, 0);
        M_VFX.methodPointer   = MakeVfxStub();
        M_GETL.methodPointer  = MakeStub(0, 3);
        M_GETL2.methodPointer = MakeStub(0, 3);
    }
    return TRUE;
}
