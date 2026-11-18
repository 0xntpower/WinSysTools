// CfgIni.h -- the original .cfg configuration store (Unicode tools).
//
// The modern utilities keep their settings in a <exe>.cfg INI file next
// to the executable, section [General], not in the registry. Recovered from the
// shared config class present in ProductKeyScanner, WinUpdatesView, SimpleWMIView
// and TaskSchedulerView: each binary imports only GetPrivateProfile*/
// WritePrivateProfile* and has no Reg value-writer, the path is the module path
// with its extension replaced by .cfg, and the /cfg switch overrides that path
// with environment variables expanded.
//
// On-disk format (TaskSchedulerView sub_140016628 / sub_1400161A0 / sub_140008188):
//   integers  -- signed decimal, read with GetPrivateProfileInt
//   strings   -- written directly
//   blobs     -- uppercase hex bytes separated by one space ("%2.2X "), read back
//                by pairing consecutive non-space hex characters
#pragma once

#include <windows.h>

#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace cfgini
{
    inline constexpr wchar_t kSection[] = L"General";

    [[nodiscard]] inline std::wstring DefaultPath()
    {
        wchar_t module[MAX_PATH] = {};
        ::GetModuleFileNameW(nullptr, module, MAX_PATH);
        std::wstring path = module;
        const size_t dot = path.find_last_of(L'.');
        const size_t slash = path.find_last_of(L"\\/");
        if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash))
        {
            path.resize(dot);
        }
        return path + L".cfg";
    }

    // Resolve a /cfg argument: expand environment variables, and if the result
    // carries no path separator, anchor it to the current directory (matching the
    // original path builder, for example WinUpdatesView sub_140010D38).
    [[nodiscard]] inline std::wstring Resolve(const std::wstring& arg)
    {
        wchar_t expanded[MAX_PATH] = {};
        if (::ExpandEnvironmentStringsW(arg.c_str(), expanded, MAX_PATH) == 0)
        {
            return arg;
        }
        std::wstring path = expanded;
        if (path.find(L'\\') == std::wstring::npos)
        {
            wchar_t cwd[MAX_PATH] = {};
            const DWORD n = ::GetCurrentDirectoryW(MAX_PATH, cwd);
            if (n != 0 && n < MAX_PATH)
            {
                path = std::wstring(cwd) + L"\\" + path;
            }
        }
        return path;
    }

    [[nodiscard]] inline int GetInt(const std::wstring& path, const wchar_t* name, int def)
    {
        return static_cast<int>(::GetPrivateProfileIntW(kSection, name, def, path.c_str()));
    }

    inline void PutInt(const std::wstring& path, const wchar_t* name, int value)
    {
        wchar_t buf[16] = {};
        _itow_s(value, buf, 10);
        ::WritePrivateProfileStringW(kSection, name, buf, path.c_str());
    }

    [[nodiscard]] inline std::wstring GetStr(const std::wstring& path, const wchar_t* name,
                                             const wchar_t* def = L"")
    {
        wchar_t buf[4096] = {};
        ::GetPrivateProfileStringW(kSection, name, def, buf,
                                   static_cast<DWORD>(std::size(buf)), path.c_str());
        return buf;
    }

    inline void PutStr(const std::wstring& path, const wchar_t* name, const std::wstring& value)
    {
        ::WritePrivateProfileStringW(kSection, name, value.c_str(), path.c_str());
    }

    inline void PutBlob(const std::wstring& path, const wchar_t* name, const void* data, size_t bytes)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        std::wstring hex;
        hex.reserve(bytes * 3);
        wchar_t cell[8] = {};
        for (size_t i = 0; i < bytes; ++i)
        {
            swprintf(cell, std::size(cell), L"%2.2X ", p[i]);
            hex += cell;
        }
        ::WritePrivateProfileStringW(kSection, name, hex.c_str(), path.c_str());
    }

    [[nodiscard]] inline std::vector<uint8_t> GetBlob(const std::wstring& path, const wchar_t* name)
    {
        wchar_t buf[8200] = {};
        ::GetPrivateProfileStringW(kSection, name, L"", buf,
                                   static_cast<DWORD>(std::size(buf)), path.c_str());
        std::vector<uint8_t> out;
        wchar_t pair[3] = {};
        int have = 0;
        for (const wchar_t* c = buf; *c != L'\0'; ++c)
        {
            if (*c == L' ')
            {
                continue;
            }
            pair[have++] = *c;
            if (have == 2)
            {
                out.push_back(static_cast<uint8_t>(wcstoul(pair, nullptr, 16)));
                have = 0;
            }
        }
        return out;
    }
}
