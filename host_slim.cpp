// host_slim.cpp - 验收：三处层数读取点 + 潜能页面 getter（含 SKILL 排除检查）
#include <windows.h>
#include <stdio.h>

typedef void* (__stdcall* fn_vp)();
typedef int   (__stdcall* fn_i1)(void*);

int main()
{
    HMODULE ga = LoadLibraryW(L"GameAssembly.dll");
    if (!ga) { printf("FAIL load GA %lu\n", GetLastError()); return 2; }
    fn_vp make = (fn_vp)GetProcAddress(ga, "MakeObj");
    fn_i1 attr  = (fn_i1)GetProcAddress(ga, "CallAttr");
    fn_i1 skil  = (fn_i1)GetProcAddress(ga, "CallSkill");
    fn_i1 buff  = (fn_i1)GetProcAddress(ga, "CallBuff");
    fn_i1 getlv = (fn_i1)GetProcAddress(ga, "CallGetLv");
    fn_i1 sklv  = (fn_i1)GetProcAddress(ga, "CallSkillLv");
    fn_i1 card  = (fn_i1)GetProcAddress(ga, "CallCard");
    fn_i1 vfx   = (fn_i1)GetProcAddress(ga, "CallVfx");

    void* o = make();
    int a0 = attr(o), s0 = skil(o), b0 = buff(o), g0 = getlv(o), k0 = sklv(o);
    int c0 = card ? card(o) : -1, v0 = vfx ? vfx(o) : -1;
    printf("before: card=%d vfx=%d (expect 1 0)\n", c0, v0);
    printf("before: attr=%d skill=%d buff=%d getLv=%d skillLv=%d\n", a0, s0, b0, g0, k0);
    if (a0 != 1 || s0 != 1 || b0 != 1 || g0 != 1 || k0 != 1 || c0 != 1 || v0 != 0) { printf("FAIL baseline\n"); return 2; }

    HMODULE fix = LoadLibraryW(L"PotentialFix.dll");
    if (!fix) { printf("FAIL load fix %lu\n", GetLastError()); return 2; }
    Sleep(5000);

    int fails = 0;
    auto chk = [&](const char* n, long got, long want) {
        bool ok = (got == want);
        printf("%s %-38s got=%-5ld want=%ld\n", ok ? "ok  " : "FAIL", n, got, want);
        if (!ok) fails++;
    };

    chk("_RefreshPotentialAttribute",          attr(o),  5);
    chk("_RefreshPotentialModifiedSkillData",  skil(o),  5);
    chk("RefreshPotentialBuff",                buff(o),  5);
    chk("get_PotentialLevel 不覆写(只改字段)", getlv(o), 1);
    chk("RefreshMaxPotentialEffectShow -> 1", vfx ? vfx(o) : -1, 1);   // 先调用，触发字段写回
    { typedef int (__stdcall* fnf)(); fnf gf = (fnf)GetProcAddress(ga, "GetField");
      chk("obj+0x60 字段被写回 5", gf ? gf() : -1, 5); }
    chk("_InitPotentialCardSkill -> 5",      card ? card(o) : -1, 5);

    printf("\n%s fails=%d\n", fails ? "RESULT: FAIL" : "RESULT: PASS", fails);
    return fails ? 1 : 0;
}


