// RegKey.h -- minimal RAII wrapper over a registry key. Clean replacement for the
// original's RegOpenKeyExW / RegQueryValueExW helpers (sub_140019D44 / sub_140019D80 /
// sub_140019DD0 / sub_14001A10C). All Unicode W APIs, reading HKEY_CLASSES_ROOT.
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

    // The original reads with samDesired 0x20019 == KEY_READ, and read/writes file
    // associations with KEY_READ|KEY_WRITE.
    bool open(HKEY root, const std::wstring& sub, REGSAM access = KEY_READ)
    {
        reset();
        return ::RegOpenKeyExW(root, sub.empty() ? nullptr : sub.c_str(), 0, access, &key_) == ERROR_SUCCESS;
    }

    bool create(HKEY root, const std::wstring& sub, REGSAM access = KEY_READ | KEY_WRITE)
    {
        reset();
        return ::RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, access, nullptr, &key_, nullptr) == ERROR_SUCCESS;
    }

    bool valid() const noexcept { return key_ != nullptr; }
    HKEY get() const noexcept { return key_; }

    // Read a named REG_SZ / REG_EXPAND_SZ value. Empty name reads the default value.
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
    bool hasValue(const std::wstring& name) const { return queryString(name).has_value(); }

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

    // Read a 4-byte flag value regardless of REG_DWORD vs REG_BINARY. EditFlags and
    // BrowserFlags are stored either way, and the original reads them into a fixed
    // 4-byte buffer without checking the type.
    std::optional<DWORD> queryFlags(const std::wstring& name) const
    {
        if (!key_) return std::nullopt;
        DWORD type = 0, value = 0, cb = sizeof(value);
        if (::RegQueryValueExW(key_, name.c_str(), nullptr, &type,
                               reinterpret_cast<LPBYTE>(&value), &cb) != ERROR_SUCCESS)
            return std::nullopt;
        return value;
    }

    bool setString(const std::wstring& name, const std::wstring& value)
    {
        return key_ && ::RegSetValueExW(key_, name.empty() ? nullptr : name.c_str(), 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    }
    bool setDword(const std::wstring& name, DWORD value)
    {
        return key_ && ::RegSetValueExW(key_, name.c_str(), 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
    }
    bool deleteValue(const std::wstring& name)
    {
        return key_ && ::RegDeleteValueW(key_, name.c_str()) == ERROR_SUCCESS;
    }

    // Last write time of the key (sub_14001A10C).
    std::optional<FILETIME> lastWriteTime() const
    {
        if (!key_) return std::nullopt;
        FILETIME ft{};
        if (::RegQueryInfoKeyW(key_, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr, nullptr, nullptr, nullptr, &ft) != ERROR_SUCCESS)
            return std::nullopt;
        return ft;
    }

    std::vector<std::wstring> subKeys() const
    {
        std::vector<std::wstring> out;
        if (!key_) return out;
        for (DWORD i = 0; ; ++i)
        {
            wchar_t name[256];
            DWORD cch = static_cast<DWORD>(std::size(name));
            if (::RegEnumKeyExW(key_, i, name, &cch, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            out.emplace_back(name, cch);
        }
        return out;
    }

    // Open a subkey and read one of its values (sub_140019DD0).
    std::wstring subValue(const std::wstring& subKey, const std::wstring& valueName) const
    {
        RegKey k;
        if (!key_ || !k.open(key_, subKey)) return {};
        return k.queryStringOr(valueName);
    }

private:
    HKEY key_ = nullptr;
};
