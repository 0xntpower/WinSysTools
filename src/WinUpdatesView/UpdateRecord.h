// UpdateRecord.h -- one Windows Update history entry, reconstructed from the
// record that the original builds in sub_140015348 (the WUA COM path) and
// renders in the column dispatcher sub_140002534.
//
// Field set and the per-column formatting (enum string tables, date+time,
// decimal, 0x%8.8x hResult, KB-number parse) are recovered from the binary.
#pragma once
#include <windows.h>
#include <string>
#include <cstdio>

struct DisplayOptions
{
    bool showTimeInGmt = false;   // Options -> Show Time In GMT (cfg ShowTimeInGMT)
};

struct UpdateRecord
{
    std::wstring title;                 // col 0   IUpdateHistoryEntry::Title
    std::wstring description;           // col 1   Description
    FILETIME     installDate{};         // col 2   Date (DATE -> UTC FILETIME)
    int          operation = 0;         // col 3   Operation (1 Install, 2 Uninstall)
    int          resultCode = 0;        // col 4   ResultCode (0..5)
    std::wstring supportUrl;            // col 5   SupportUrl
    std::wstring uninstallNotes;        // col 6   UninstallationNotes
    std::wstring clientAppId;           // col 7   ClientApplicationID
    std::wstring serviceId;             // col 8   ServiceID
    std::wstring updateId;              // col 9   UpdateIdentity::UpdateID
    int          revisionNumber = 0;    // col 10  UpdateIdentity::RevisionNumber
    int          unmappedResultCode = 0;// col 11  UnmappedResultCode
    int          serverSelection = 0;   // col 12  ServerSelection (0..3)
    int          hResult = 0;           // col 13  HResult
    std::wstring category;              // col 14  Categories[0]::Name
    std::wstring informationUrl;        // col 15  http://support.microsoft.com/kb/<n>

    // --- per-column display text, in the original's column order -------
    std::wstring column(int col, const DisplayOptions& opt) const
    {
        switch (col)
        {
        case 0:  return title;
        case 1:  return description;
        case 2:  return kbNumber();
        case 3:  return formatDate(opt);
        case 4:  return operationText();
        case 5:  return resultText();
        case 6:  return informationUrl;
        case 7:  return supportUrl;
        case 8:  return uninstallNotes;
        case 9:  return category;
        case 10: return clientAppId;
        case 11: return serviceId;
        case 12: return updateId;
        case 13: return numberText(revisionNumber);
        case 14: return numberText(unmappedResultCode);
        case 15: return serverSelectionText();
        case 16: return hResultText();
        default: return {};
        }
    }

    // sort key: numeric columns sort numerically, dates by FILETIME
    long long sortKey(int col, bool& numeric) const
    {
        numeric = true;
        switch (col)
        {
        case 3:  return (static_cast<long long>(installDate.dwHighDateTime) << 32) | installDate.dwLowDateTime;
        case 4:  return operation;
        case 5:  return resultCode;
        case 13: return revisionNumber;
        case 14: return unmappedResultCode;
        case 15: return serverSelection;
        case 16: return static_cast<unsigned int>(hResult);
        default: numeric = false; return 0;
        }
    }

private:
    static const wchar_t* lookup(const wchar_t* const* table, int count, int idx)
    {
        return (idx >= 0 && idx < count) ? table[idx] : L"";
    }

    std::wstring operationText() const
    {
        // 1 -> Install, 2 -> Uninstall (string ids 901/902)
        if (operation == 1) return L"Install";
        if (operation == 2) return L"Uninstall";
        return {};
    }

    std::wstring resultText() const
    {
        static const wchar_t* k[] = { L"Not Started", L"In Progress", L"Succeeded",
                                      L"Succeeded With Errors", L"Failed", L"Aborted" };
        return lookup(k, 6, resultCode);
    }

    std::wstring serverSelectionText() const
    {
        static const wchar_t* k[] = { L"Default", L"Managed Server", L"Windows Update", L"Others" };
        return lookup(k, 4, serverSelection);
    }

    std::wstring hResultText() const
    {
        wchar_t buf[16];
        _snwprintf(buf, 15, L"0x%8.8x", static_cast<unsigned int>(hResult));
        return buf;
    }

    static std::wstring numberText(int v)
    {
        wchar_t buf[16];
        _ultow(static_cast<unsigned long>(v), buf, 10);
        return buf;
    }

    // Install Date -- local time "ShortDate LongTime", or GMT when requested.
    std::wstring formatDate(const DisplayOptions& opt) const
    {
        if (installDate.dwLowDateTime == 0 && installDate.dwHighDateTime == 0)
            return {};

        FILETIME ft = installDate;
        if (!opt.showTimeInGmt)
        {
            SYSTEMTIME utc{}, local{};
            if (FileTimeToSystemTime(&installDate, &utc) &&
                SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
                SystemTimeToFileTime(&local, &ft);
            else
                FileTimeToLocalFileTime(&installDate, &ft);
        }

        SYSTEMTIME st{};
        if (!FileTimeToSystemTime(&ft, &st) || st.wYear <= 1000)
            return {};

        wchar_t date[128]{}, time[128]{};
        GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, 128);
        GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, time, 128);
        std::wstring out = date;
        out += L" ";
        out += time;
        return out;
    }

    // KB Number column: pull "KB<digits>" out of the title, the way the
    // original does -- "(KB....)" takes up to ')', "- KB...." up to a space.
    std::wstring kbNumber() const
    {
        auto take = [&](size_t start, wchar_t term) -> std::wstring {
            std::wstring s = title.substr(start);
            if (auto p = s.find(term); p != std::wstring::npos) s.erase(p);
            return s;
        };
        if (auto p = title.find(L"(KB"); p != std::wstring::npos)
            return take(p + 1, L')');
        if (auto p = title.find(L"- KB"); p != std::wstring::npos)
            return take(p + 2, L' ');
        return {};
    }
};
