// Export.h -- CSV export of a snapshot, matching the original's layout.
//
// The original's GUI shows values in kilobytes, but its CSV and the
// -p <pid> <outputfile> dump write raw byte counts with comma grouping and a
// blank for zero. This reproduces that so a dump is comparable with the
// original's.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <algorithm>
#include "MemSnapshot.h"

namespace vm
{
    [[nodiscard]] inline std::wstring GroupBytes(uint64_t v)
    {
        if (v == 0)
        {
            return std::wstring();
        }
        wchar_t raw[32] = {};
        swprintf(raw, 32, L"%llu", static_cast<unsigned long long>(v));
        std::wstring digits = raw;
        std::wstring g;
        int c = 0;
        for (auto it = digits.rbegin(); it != digits.rend(); ++it)
        {
            if (c && c % 3 == 0)
            {
                g.push_back(L',');
            }
            g.push_back(*it);
            ++c;
        }
        std::reverse(g.begin(), g.end());
        return g;
    }

    // The original's CSV and GUI both report in kilobytes, blank for zero.
    [[nodiscard]] inline std::wstring Kb(uint64_t v)
    {
        return (v == 0) ? std::wstring() : GroupBytes((v + 1023) / 1024);
    }

    [[nodiscard]] inline std::wstring HexAddr(uint64_t a)
    {
        wchar_t buf[24] = {};
        swprintf(buf, 24, L"%016llX", static_cast<unsigned long long>(a));
        return buf;
    }

    namespace detail
    {
        inline void Quote(std::wstring& out, const std::wstring& field)
        {
            out.push_back(L'"');
            for (wchar_t ch : field)
            {
                if (ch == L'"')
                {
                    out.push_back(L'"');
                }
                out.push_back(ch);
            }
            out.push_back(L'"');
        }
    }

    [[nodiscard]] inline std::wstring BuildCsv(const Snapshot& s, bool includeFree = false)
    {
        std::wstring out;
        auto row = [&](std::initializer_list<std::wstring> cells)
        {
            bool first = true;
            for (const std::wstring& c : cells)
            {
                if (!first)
                {
                    out.push_back(L',');
                }
                first = false;
                detail::Quote(out, c);
            }
            out += L"\r\n";
        };

        row({ L"Process:", s.processName });
        out += L"\r\n";
        wchar_t pid[32] = {};
        swprintf(pid, 32, L"%lu", s.pid);
        row({ L"PID:", pid });
        out += L"\r\n\r\n";

        row({ L"Type", L"Size", L"Committed", L"Private", L"Total WS", L"Private WS",
              L"Shareable WS", L"Shared WS", L"Locked WS", L"Blocks", L"Largest" });
        for (int c = 0; c < kCategoryCount; ++c)
        {
            const CategoryTotal& t = s.totals[c];
            wchar_t blocks[16] = {};
            if (t.blocks)
            {
                swprintf(blocks, 16, L"%lu", t.blocks);
            }
            row({ CategoryName(static_cast<Category>(c)), Kb(t.size), Kb(t.committed),
                  Kb(t.privateBytes), Kb(t.totalWs), Kb(t.privateWs),
                  Kb(t.shareableWs), Kb(t.sharedWs), Kb(t.lockedWs),
                  blocks, Kb(t.largest) });
        }
        out += L"\r\n";

        row({ L"Address", L"Type", L"Size", L"Committed", L"Private", L"Total WS", L"Private WS",
              L"Shareable WS", L"Shared WS", L"Locked WS", L"Blocks", L"Protection", L"Details" });
        for (const Region& r : s.regions)
        {
            if (!includeFree && r.category == Category::Free)
            {
                continue;
            }
            row({ HexAddr(r.base), CategoryName(r.category), Kb(r.size), Kb(r.committed),
                  Kb(r.privateBytes), Kb(r.totalWs), Kb(r.privateWs),
                  Kb(r.shareableWs), Kb(r.sharedWs), Kb(r.lockedWs),
                  L"1", ProtectionText(r), r.details });
        }
        return out;
    }

    // Write the snapshot as a UTF-16 CSV. Returns true on success.
    [[nodiscard]] inline bool WriteCsv(const Snapshot& s, const wchar_t* path, bool includeFree = false)
    {
        Handle f(::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!f.valid())
        {
            return false;
        }
        const std::wstring csv = BuildCsv(s, includeFree);
        const unsigned char bom[2] = { 0xFF, 0xFE };
        DWORD wr = 0;
        ::WriteFile(f.get(), bom, 2, &wr, nullptr);
        return ::WriteFile(f.get(), csv.data(), static_cast<DWORD>(csv.size() * sizeof(wchar_t)), &wr, nullptr) != 0;
    }
}
