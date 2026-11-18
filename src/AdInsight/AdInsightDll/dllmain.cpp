// dllmain.cpp -- ADInsightDll.dll, the hook DLL the AdInsight GUI drops and
// injects to intercept wldap32 LDAP client calls.
//
// Structure matches the original ADInsightDll.dll: the GUI extracts this DLL
// from its own BINRES resource, loads it, and resolves the exports
// AdmonTraceInit, AdmonMonitorStart, AdmonMonitorStop and AdmonPingDLL. When
// mapped into a target process the DLL hooks the process's wldap32 import
// entries and reports each call to the GUI over \\.\pipe\ADInsightPipe.
//
// Interception here is import-address-table patching, which is in-process and
// touches no other process. The original additionally installs a system-wide
// SetWindowsHookEx so new and existing processes load the DLL automatically.
// That all-process auto-injection is out of scope and documented in
// analysis/AdInsight.md. This build is driven into a target by the GUI's
// targeted CreateRemoteThread injection, used only against a benign test
// process.
#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include "../TraceProtocol.h"

#pragma comment(lib, "psapi.lib")

namespace
{
    // ---- wldap32 prototypes (the core intercepted set) ------------------
    struct l_timeval
    {
        long tv_sec;
        long tv_usec;
    };

    using LDAP_PTR = void*;
    using LDAPMsg_PTR = void*;

    using ldap_initW_t = LDAP_PTR(__cdecl*)(const wchar_t*, unsigned long);
    using ldap_connect_t = unsigned long(__cdecl*)(LDAP_PTR, l_timeval*);
    using ldap_simple_bind_sW_t = unsigned long(__cdecl*)(LDAP_PTR, wchar_t*, wchar_t*);
    using ldap_bind_sW_t = unsigned long(__cdecl*)(LDAP_PTR, wchar_t*, wchar_t*, unsigned long);
    using ldap_search_sW_t = unsigned long(__cdecl*)(LDAP_PTR, wchar_t*, unsigned long, wchar_t*, wchar_t**,
                                                     unsigned long, LDAPMsg_PTR*);
    using ldap_search_ext_sW_t = unsigned long(__cdecl*)(LDAP_PTR, wchar_t*, unsigned long, wchar_t*, wchar_t**,
                                                         unsigned long, void*, void*, l_timeval*, unsigned long,
                                                         LDAPMsg_PTR*);
    using ldap_result_t = unsigned long(__cdecl*)(LDAP_PTR, unsigned long, unsigned long, l_timeval*, LDAPMsg_PTR*);
    using ldap_get_valuesW_t = wchar_t**(__cdecl*)(LDAP_PTR, LDAPMsg_PTR, wchar_t*);
    using ldap_unbind_t = unsigned long(__cdecl*)(LDAP_PTR);

    // Saved real targets, resolved once from wldap32.dll.
    ldap_initW_t g_real_initW = nullptr;
    ldap_connect_t g_real_connect = nullptr;
    ldap_simple_bind_sW_t g_real_simple_bind = nullptr;
    ldap_bind_sW_t g_real_bind = nullptr;
    ldap_search_sW_t g_real_search = nullptr;
    ldap_search_ext_sW_t g_real_search_ext = nullptr;
    ldap_result_t g_real_result = nullptr;
    ldap_get_valuesW_t g_real_get_values = nullptr;
    ldap_unbind_t g_real_unbind = nullptr;

    // ---- pipe client state ---------------------------------------------
    HANDLE g_pipe = INVALID_HANDLE_VALUE;
    CRITICAL_SECTION g_pipeLock;
    bool g_lockReady = false;
    volatile LONG g_seq = 0;
    volatile LONG g_capturing = 1; // start reporting as soon as hooks are live
    wchar_t g_procName[adin::kProcNameLen] = {};
    LARGE_INTEGER g_qpcFreq = {};

    void InitProcName()
    {
        wchar_t path[MAX_PATH] = {};
        if (::GetModuleFileNameW(nullptr, path, MAX_PATH) != 0)
        {
            const wchar_t* base = path;
            for (const wchar_t* p = path; *p != 0; ++p)
            {
                if (*p == L'\\' || *p == L'/')
                {
                    base = p + 1;
                }
            }
            wcsncpy_s(g_procName, base, _TRUNCATE);
        }
    }

    bool EnsurePipe()
    {
        if (g_pipe != INVALID_HANDLE_VALUE)
        {
            return true;
        }
        HANDLE h = ::CreateFileW(adin::kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            if (::GetLastError() == ERROR_PIPE_BUSY && ::WaitNamedPipeW(adin::kPipeName, 2000))
            {
                h = ::CreateFileW(adin::kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            }
        }
        if (h == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        DWORD mode = PIPE_READMODE_MESSAGE;
        ::SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
        g_pipe = h;
        return true;
    }

    void SendRecord(const adin::TraceRecord& rec)
    {
        if (::InterlockedCompareExchange(&g_capturing, 1, 1) == 0 || !g_lockReady)
        {
            return;
        }
        ::EnterCriticalSection(&g_pipeLock);
        if (EnsurePipe())
        {
            DWORD written = 0;
            if (!::WriteFile(g_pipe, &rec, sizeof(rec), &written, nullptr))
            {
                ::CloseHandle(g_pipe);
                g_pipe = INVALID_HANDLE_VALUE;
            }
        }
        ::LeaveCriticalSection(&g_pipeLock);
    }

    uint64_t NowFileTime()
    {
        FILETIME ft = {};
        ::GetSystemTimeAsFileTime(&ft);
        return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }

    void FillCommon(adin::TraceRecord& rec, const wchar_t* op)
    {
        rec.magic = adin::kRecordMagic;
        rec.pid = ::GetCurrentProcessId();
        rec.tid = ::GetCurrentThreadId();
        rec.fileTime = NowFileTime();
        wcsncpy_s(rec.process, g_procName, _TRUNCATE);
        wcsncpy_s(rec.operation, op, _TRUNCATE);
    }

    const wchar_t* ScopeName(unsigned long scope)
    {
        switch (scope)
        {
        case 0:
            return L"LDAP_SCOPE_BASE";
        case 1:
            return L"LDAP_SCOPE_ONELEVEL";
        case 2:
            return L"LDAP_SCOPE_SUBTREE";
        default:
            return L"LDAP_SCOPE_?";
        }
    }

    const wchar_t* AuthName(unsigned long method)
    {
        switch (method)
        {
        case 0x80:
            return L"LDAP_AUTH_SIMPLE";
        case 0x86:
            return L"LDAP_AUTH_NTLM";
        case 0x87:
            return L"LDAP_AUTH_SICILY";
        case 0x8a:
            return L"LDAP_AUTH_DPA";
        case 0x8b:
            return L"LDAP_AUTH_MSN";
        case 0x8e:
            return L"LDAP_AUTH_SSPI";
        case 0x8f:
            return L"LDAP_AUTH_DIGEST";
        default:
            return L"LDAP_AUTH_OTHER";
        }
    }

    // Log the request phase and return a sequence id so the response can pair.
    uint32_t LogRequest(const wchar_t* op, const wchar_t* dn, const wchar_t* detail)
    {
        adin::TraceRecord rec = {};
        FillCommon(rec, op);
        rec.phase = static_cast<uint32_t>(adin::Phase::Request);
        rec.seq = static_cast<uint32_t>(::InterlockedIncrement(&g_seq));
        if (dn != nullptr)
        {
            wcsncpy_s(rec.dn, dn, _TRUNCATE);
        }
        if (detail != nullptr)
        {
            wcsncpy_s(rec.detail, detail, _TRUNCATE);
        }
        SendRecord(rec);
        return rec.seq;
    }

    void LogResponse(uint32_t seq, const wchar_t* op, int32_t result, uint64_t startQpc)
    {
        adin::TraceRecord rec = {};
        FillCommon(rec, op);
        rec.phase = static_cast<uint32_t>(adin::Phase::Response);
        rec.seq = seq;
        rec.resultCode = result;
        LARGE_INTEGER now = {};
        ::QueryPerformanceCounter(&now);
        if (g_qpcFreq.QuadPart != 0 && startQpc != 0)
        {
            rec.durationUs =
                static_cast<uint64_t>(now.QuadPart - static_cast<LONGLONG>(startQpc)) * 1000000ULL / g_qpcFreq.QuadPart;
        }
        SendRecord(rec);
    }

    uint64_t Qpc()
    {
        LARGE_INTEGER c = {};
        ::QueryPerformanceCounter(&c);
        return static_cast<uint64_t>(c.QuadPart);
    }

    // ---- trampolines ----------------------------------------------------
    LDAP_PTR __cdecl Hook_initW(const wchar_t* host, unsigned long port)
    {
        wchar_t detail[adin::kDetailLen] = {};
        swprintf_s(detail, L"HostName: %s  PortNumber: %lu", host ? host : L"(null)", port);
        const uint32_t seq = LogRequest(L"ldap_initW", host, detail);
        const uint64_t start = Qpc();
        LDAP_PTR r = g_real_initW(host, port);
        LogResponse(seq, L"ldap_initW", r != nullptr ? 0 : -1, start);
        return r;
    }

    unsigned long __cdecl Hook_connect(LDAP_PTR ld, l_timeval* tv)
    {
        const uint32_t seq = LogRequest(L"ldap_connect", nullptr, L"Opens a connection to the server.");
        const uint64_t start = Qpc();
        unsigned long r = g_real_connect(ld, tv);
        LogResponse(seq, L"ldap_connect", static_cast<int32_t>(r), start);
        return r;
    }

    unsigned long __cdecl Hook_simple_bind(LDAP_PTR ld, wchar_t* dn, wchar_t* passwd)
    {
        // The bind password is never captured or forwarded, only its presence.
        wchar_t detail[adin::kDetailLen] = {};
        swprintf_s(detail, L"dn: %s  Method: LDAP_AUTH_SIMPLE  Credentials: %s", dn ? dn : L"(null)",
                   (passwd && passwd[0]) ? L"(present)" : L"(none)");
        const uint32_t seq = LogRequest(L"ldap_simple_bind_sW", dn, detail);
        const uint64_t start = Qpc();
        unsigned long r = g_real_simple_bind(ld, dn, passwd);
        LogResponse(seq, L"ldap_simple_bind_sW", static_cast<int32_t>(r), start);
        return r;
    }

    unsigned long __cdecl Hook_bind(LDAP_PTR ld, wchar_t* dn, wchar_t* cred, unsigned long method)
    {
        wchar_t detail[adin::kDetailLen] = {};
        swprintf_s(detail, L"dn: %s  Method: %s", dn ? dn : L"(null)", AuthName(method));
        const uint32_t seq = LogRequest(L"ldap_bind_sW", dn, detail);
        const uint64_t start = Qpc();
        unsigned long r = g_real_bind(ld, dn, cred, method);
        LogResponse(seq, L"ldap_bind_sW", static_cast<int32_t>(r), start);
        return r;
    }

    unsigned long __cdecl Hook_search(LDAP_PTR ld, wchar_t* base, unsigned long scope, wchar_t* filter, wchar_t** attrs,
                                      unsigned long attrsonly, LDAPMsg_PTR* res)
    {
        wchar_t detail[adin::kDetailLen] = {};
        swprintf_s(detail, L"base: %s  scope: %s  filter: %s  attrsonly: %lu", base ? base : L"(null)",
                   ScopeName(scope), filter ? filter : L"(null)", attrsonly);
        const uint32_t seq = LogRequest(L"ldap_search_sW", base, detail);
        const uint64_t start = Qpc();
        unsigned long r = g_real_search(ld, base, scope, filter, attrs, attrsonly, res);
        LogResponse(seq, L"ldap_search_sW", static_cast<int32_t>(r), start);
        return r;
    }

    unsigned long __cdecl Hook_search_ext(LDAP_PTR ld, wchar_t* base, unsigned long scope, wchar_t* filter,
                                          wchar_t** attrs, unsigned long attrsonly, void* sc, void* cc, l_timeval* to,
                                          unsigned long sizelimit, LDAPMsg_PTR* res)
    {
        wchar_t detail[adin::kDetailLen] = {};
        swprintf_s(detail, L"base: %s  scope: %s  filter: %s  sizelimit: %lu", base ? base : L"(null)",
                   ScopeName(scope), filter ? filter : L"(null)", sizelimit);
        const uint32_t seq = LogRequest(L"ldap_search_ext_sW", base, detail);
        const uint64_t start = Qpc();
        unsigned long r = g_real_search_ext(ld, base, scope, filter, attrs, attrsonly, sc, cc, to, sizelimit, res);
        LogResponse(seq, L"ldap_search_ext_sW", static_cast<int32_t>(r), start);
        return r;
    }

    unsigned long __cdecl Hook_result(LDAP_PTR ld, unsigned long msgid, unsigned long all, l_timeval* to,
                                      LDAPMsg_PTR* res)
    {
        wchar_t detail[adin::kDetailLen] = {};
        swprintf_s(detail, L"msgid: %lu  all: %lu", msgid, all);
        const uint32_t seq = LogRequest(L"ldap_result", nullptr, detail);
        const uint64_t start = Qpc();
        unsigned long r = g_real_result(ld, msgid, all, to, res);
        LogResponse(seq, L"ldap_result", static_cast<int32_t>(r), start);
        return r;
    }

    wchar_t** __cdecl Hook_get_values(LDAP_PTR ld, LDAPMsg_PTR entry, wchar_t* attr)
    {
        wchar_t detail[adin::kDetailLen] = {};
        swprintf_s(detail, L"attr: %s", attr ? attr : L"(null)");
        const uint32_t seq = LogRequest(L"ldap_get_valuesW", attr, detail);
        const uint64_t start = Qpc();
        wchar_t** r = g_real_get_values(ld, entry, attr);
        LogResponse(seq, L"ldap_get_valuesW", r != nullptr ? 0 : -1, start);
        return r;
    }

    unsigned long __cdecl Hook_unbind(LDAP_PTR ld)
    {
        const uint32_t seq = LogRequest(L"ldap_unbind", nullptr, L"Unbinds and closes the connection.");
        const uint64_t start = Qpc();
        unsigned long r = g_real_unbind(ld);
        LogResponse(seq, L"ldap_unbind", static_cast<int32_t>(r), start);
        return r;
    }

    // ---- IAT patch engine ----------------------------------------------
    struct HookEntry
    {
        void** realSlot;
        void* real;
        void* hook;
    };

    HookEntry g_hooks[9] = {};
    int g_hookCount = 0;

    void AddHook(void** realSlot, void* real, void* hook)
    {
        if (real == nullptr)
        {
            return;
        }
        *realSlot = real;
        g_hooks[g_hookCount++] = HookEntry{realSlot, real, hook};
    }

    // Swap one import slot if its current value matches a hooked real target.
    void PatchSlot(void** slot)
    {
        for (int i = 0; i < g_hookCount; ++i)
        {
            if (*slot == g_hooks[i].real)
            {
                DWORD prot = 0;
                if (::VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &prot))
                {
                    *slot = g_hooks[i].hook;
                    ::VirtualProtect(slot, sizeof(void*), prot, &prot);
                }
                return;
            }
        }
    }

    // Walk one module's import address table and patch any hooked slot.
    void PatchModuleIat(HMODULE module)
    {
        auto* base = reinterpret_cast<BYTE*>(module);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return;
        }
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
        {
            return;
        }
        const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (dir.VirtualAddress == 0)
        {
            return;
        }
        auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
        for (; imp->Name != 0; ++imp)
        {
            auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
            for (; thunk->u1.Function != 0; ++thunk)
            {
                PatchSlot(reinterpret_cast<void**>(&thunk->u1.Function));
            }
        }
    }

    void PatchAllModules()
    {
        HMODULE mods[256] = {};
        DWORD needed = 0;
        if (::K32EnumProcessModules(::GetCurrentProcess(), mods, sizeof(mods), &needed))
        {
            const int count = static_cast<int>(needed / sizeof(HMODULE));
            for (int i = 0; i < count && i < 256; ++i)
            {
                PatchModuleIat(mods[i]);
            }
        }
    }

    bool ResolveReals()
    {
        HMODULE w = ::LoadLibraryW(L"wldap32.dll");
        if (w == nullptr)
        {
            return false;
        }
        auto gp = [&](const char* n)
        {
            return reinterpret_cast<void*>(::GetProcAddress(w, n));
        };
        AddHook(reinterpret_cast<void**>(&g_real_initW), gp("ldap_initW"), reinterpret_cast<void*>(&Hook_initW));
        AddHook(reinterpret_cast<void**>(&g_real_connect), gp("ldap_connect"), reinterpret_cast<void*>(&Hook_connect));
        AddHook(reinterpret_cast<void**>(&g_real_simple_bind), gp("ldap_simple_bind_sW"),
                reinterpret_cast<void*>(&Hook_simple_bind));
        AddHook(reinterpret_cast<void**>(&g_real_bind), gp("ldap_bind_sW"), reinterpret_cast<void*>(&Hook_bind));
        AddHook(reinterpret_cast<void**>(&g_real_search), gp("ldap_search_sW"), reinterpret_cast<void*>(&Hook_search));
        AddHook(reinterpret_cast<void**>(&g_real_search_ext), gp("ldap_search_ext_sW"),
                reinterpret_cast<void*>(&Hook_search_ext));
        AddHook(reinterpret_cast<void**>(&g_real_result), gp("ldap_result"), reinterpret_cast<void*>(&Hook_result));
        AddHook(reinterpret_cast<void**>(&g_real_get_values), gp("ldap_get_valuesW"),
                reinterpret_cast<void*>(&Hook_get_values));
        AddHook(reinterpret_cast<void**>(&g_real_unbind), gp("ldap_unbind"), reinterpret_cast<void*>(&Hook_unbind));
        return g_hookCount > 0;
    }

    bool g_installed = false;

    void InstallHooks()
    {
        if (g_installed)
        {
            return;
        }
        g_installed = true;
        if (!g_lockReady)
        {
            ::InitializeCriticalSection(&g_pipeLock);
            g_lockReady = true;
        }
        ::QueryPerformanceFrequency(&g_qpcFreq);
        InitProcName();
        if (ResolveReals())
        {
            PatchAllModules();
        }
    }
} // namespace

// ---- exports (matching the original DLL's resolved names) ---------------
extern "C" __declspec(dllexport) int __cdecl AdmonTraceInit(void*, void*, void*, int)
{
    // The original hands back shared trace-buffer pointers here. This core
    // uses a named pipe instead, so the call just arms the hook machinery.
    InstallHooks();
    return 1;
}

extern "C" __declspec(dllexport) void __cdecl AdmonMonitorStart()
{
    ::InterlockedExchange(&g_capturing, 1);
}

extern "C" __declspec(dllexport) void __cdecl AdmonMonitorStop()
{
    ::InterlockedExchange(&g_capturing, 0);
}

extern "C" __declspec(dllexport) void __cdecl AdmonPingDLL()
{
    // Heartbeat the original uses to keep the DLL responsive. No-op here.
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        ::DisableThreadLibraryCalls(module);
        // When injected into a target process the DLL arms itself immediately,
        // so the target needs no cooperation. When the GUI loads the DLL into
        // its own process it calls AdmonTraceInit explicitly, and a second arm
        // is harmless.
        InstallHooks();
    }
    return TRUE;
}
