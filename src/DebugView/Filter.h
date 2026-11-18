// Filter.h -- include, exclude and highlight matching for the output list.
// Each field holds filter match strings separated by a semicolon, with '*' as a
// wildcard, the same grammar the original Filter dialog documents. A line is
// shown when it matches the include set and no exclude pattern, and is
// highlighted when it matches the highlight set.
#pragma once
#include <string>
#include <vector>
#include <cwctype>

class Filter
{
public:
    std::wstring include = L"*";
    std::wstring exclude;
    std::wstring highlight;

    bool shows(const std::wstring& line) const
    {
        std::wstring lo = lower(line);
        if (!matchesAny(include, lo, true))
            return false;
        if (matchesAny(exclude, lo, false))
            return false;
        return true;
    }

    bool highlights(const std::wstring& line) const
    {
        return matchesAny(highlight, lower(line), false);
    }

private:
    static std::wstring lower(const std::wstring& s)
    {
        std::wstring r = s;
        for (wchar_t& c : r) c = static_cast<wchar_t>(towlower(c));
        return r;
    }

    // emptyMatchesAll true makes an empty or absent set pass every line, which
    // is what the include field wants. The exclude and highlight sets pass
    // nothing when empty.
    static bool matchesAny(const std::wstring& patterns, const std::wstring& lowLine, bool emptyMatchesAll)
    {
        std::vector<std::wstring> parts = split(patterns);
        if (parts.empty())
            return emptyMatchesAll;
        bool any = false;
        for (const std::wstring& p : parts)
        {
            if (p.empty())
                continue;
            any = true;
            if (p == L"*")
                return true;
            if (wildContains(lowLine, lower(p)))
                return true;
        }
        return any ? false : emptyMatchesAll;
    }

    static std::vector<std::wstring> split(const std::wstring& s)
    {
        std::vector<std::wstring> out;
        size_t start = 0;
        for (size_t i = 0; i <= s.size(); ++i)
        {
            if (i == s.size() || s[i] == L';')
            {
                std::wstring tok = s.substr(start, i - start);
                size_t a = tok.find_first_not_of(L" \t");
                size_t b = tok.find_last_not_of(L" \t");
                if (a != std::wstring::npos)
                    out.push_back(tok.substr(a, b - a + 1));
                start = i + 1;
            }
        }
        return out;
    }

    // Match when every wildcard-separated segment of the pattern appears in
    // order, anywhere in the line. A pattern with no star is a plain substring.
    static bool wildContains(const std::wstring& line, const std::wstring& pat)
    {
        size_t pos = 0;
        size_t seg = 0;
        while (seg < pat.size())
        {
            size_t star = pat.find(L'*', seg);
            std::wstring piece = pat.substr(seg, star == std::wstring::npos ? std::wstring::npos : star - seg);
            if (!piece.empty())
            {
                size_t at = line.find(piece, pos);
                if (at == std::wstring::npos)
                    return false;
                pos = at + piece.size();
            }
            if (star == std::wstring::npos)
                break;
            seg = star + 1;
        }
        return true;
    }
};
