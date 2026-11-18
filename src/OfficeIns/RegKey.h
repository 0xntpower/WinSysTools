// RegKey.h -- minimal RAII wrapper over a registry key (clean replacement for the
// original's RegOpenKeyExA / RegQueryValueExA helpers sub_14000D580 / sub_14000D288 /
// sub_14000D2DC). Unicode W APIs read any add-in name faithfully.
#pragma once
#include <windows.h>
#include <string>
#include <optional>
#include <vector>

class RegKey
{
public:
    RegKey() = default;
    RegKey(const RegKey&) = delete;
    RegKey& operator=(const RegKey&) = delete;
    RegKey(RegKey&& o) noexcept : key_(o.key_) { o.key_ = nullptr; }
    ~RegKey() { reset(); }

    void reset() noexcept { if (key_) { ::RegCloseKey(key_); key_ = nullptr; } }

    // The original opens read-only with samDesired 0x20019 == KEY_READ, and
    // read/write with 0x2001F == KEY_READ|KEY_WRITE for the LoadBehavior change.
    bool open(HKEY root, const std::wstring& sub, REGSAM access = KEY_READ)
    {
        reset();
        return ::RegOpenKeyExW(root, sub.empty() ? nullptr : sub.c_str(), 0, access, &key_) == ERROR_SUCCESS;
    }

    bool adopt(HKEY k) { reset(); key_ = k; return k != nullptr; }

    bool valid() const noexcept { return key_ != nullptr; }
    HKEY get() const noexcept { return key_; }

    // Read a named REG_SZ value. Empty name reads the key's unnamed (Default) value.
    std::optional<std::wstring> queryString(const std::wstring& name) const
    {
        if (!key_) return std::nullopt;
        const wchar_t* n = name.empty() ? nullptr : name.c_str();
        DWORD type = 0, bytes = 0;
        if (::RegQueryValueExW(key_, n, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS) return std::nullopt;
        if (type != REG_SZ && type != REG_EXPAND_SZ) return std::nullopt;
        std::wstring buf(bytes / sizeof(wchar_t) + 1, L'\0');
        DWORD cb = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
        if (::RegQueryValueExW(key_, n, nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &cb) != ERROR_SUCCESS)
            return std::nullopt;
        buf.resize(wcslen(buf.c_str()));
        return buf;
    }
    std::wstring queryStringOr(const std::wstring& name, const std::wstring& fallback = L"") const
    {
        auto v = queryString(name);
        return v ? *v : fallback;
    }
    std::wstring queryDefault() const { return queryStringOr(L""); }

    std::optional<DWORD> queryDword(const std::wstring& name) const
    {
        if (!key_) return std::nullopt;
        DWORD type = 0, value = 0, cb = sizeof(value);
        if (::RegQueryValueExW(key_, name.c_str(), nullptr, &type,
                               reinterpret_cast<LPBYTE>(&value), &cb) != ERROR_SUCCESS)
            return std::nullopt;
        if (type != REG_DWORD) return std::nullopt;
        return value;
    }

    bool setDword(const std::wstring& name, DWORD value)
    {
        return key_ && ::RegSetValueExW(key_, name.c_str(), 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
    }

    std::vector<std::wstring> subKeys() const
    {
        std::vector<std::wstring> out;
        if (!key_) return out;
        for (DWORD i = 0; ; ++i)
        {
            wchar_t name[256];
            DWORD cch = static_cast<DWORD>(std::size(name));
            LSTATUS r = ::RegEnumKeyExW(key_, i, name, &cch, nullptr, nullptr, nullptr, nullptr);
            if (r != ERROR_SUCCESS) break;
            out.emplace_back(name, cch);
        }
        return out;
    }

private:
    HKEY key_ = nullptr;
};
