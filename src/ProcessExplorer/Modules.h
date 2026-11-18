// Modules.h -- the DLL list for one process.
//
// The lower pane's DLL view enumerates the selected process's loaded modules
// with Toolhelp Module32, then reads each module's description and company
// from its version info. This mirrors the original's DLL pane columns.
#pragma once

#include "ProcessData.h"
#include <tlhelp32.h>
#include <string>
#include <vector>

namespace pe
{
    struct ModuleRow
    {
        std::wstring name;
        std::wstring description;
        std::wstring company;
        std::wstring version;
        std::wstring path;
        uint64_t base = 0;
        uint64_t size = 0;
    };

    [[nodiscard]] inline std::vector<ModuleRow> EnumModules(DWORD pid)
    {
        std::vector<ModuleRow> mods;
        HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap == INVALID_HANDLE_VALUE)
        {
            return mods;
        }
        MODULEENTRY32W me{};
        me.dwSize = sizeof(me);
        if (::Module32FirstW(snap, &me))
        {
            do
            {
                ModuleRow m;
                m.name = me.szModule;
                m.path = me.szExePath;
                m.base = reinterpret_cast<uint64_t>(me.modBaseAddr);
                m.size = me.modBaseSize;
                Meta meta;
                ReadVersionInfo(m.path, meta);
                m.description = meta.description;
                m.company = meta.company;
                m.version = meta.version;
                mods.push_back(std::move(m));
            } while (::Module32NextW(snap, &me));
        }
        ::CloseHandle(snap);
        return mods;
    }
} // namespace pe
