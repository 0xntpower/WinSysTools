// RegKey.h -- minimal RAII wrapper over a registry key. Clean replacement for the
// original's registry abstraction (the vtable object whose method 32 reads a named
// value, method 40 reads the last-write time, and method 64 opens a subkey).
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
    RegKey(RegKey&& o) noexcept : key_(o.key_), owned_(o.owned_) { o.key_ = nullptr; o.owned_ = false; }
    ~RegKey() { reset(); }

    void reset() noexcept
    {
        if (key_ && owned_) ::RegCloseKey(key_);
        key_ = nullptr; owned_ = false;
    }

    // The original opens with KEY_READ (samDesired 0x20019) in the live scan, and
    // KEY_READ | KEY_WOW64_64KEY where the 64-bit view matters.
    bool open(HKEY root, const std::wstring& sub, REGSAM access = KEY_READ)
    {
        reset();
        LONG r = ::RegOpenKeyExW(root, sub.empty() ? nullptr : sub.c_str(), 0, access, &key_);
        owned_ = (r == ERROR_SUCCESS);
        return owned_;
    }

    bool create(HKEY root, const std::wstring& sub, REGSAM access = KEY_READ | KEY_WRITE)
    {
        reset();
        LONG r = ::RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, access, nullptr, &key_, nullptr);
        owned_ = (r == ERROR_SUCCESS);
        return owned_;
    }

    bool valid() const noexcept { return key_ != nullptr; }
    HKEY get() const noexcept { return key_; }

    std::optional<std::wstring> queryString(const std::wstring& name) const
    {
        if (!key_) return std::nullopt;
        DWORD type = 0, bytes = 0;
        if (::RegQueryValueExW(key_, name.c_str(), nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS)
            return std::nullopt;
        if (type != REG_SZ && type != REG_EXPAND_SZ) return std::nullopt;
        std::wstring buf(bytes / sizeof(wchar_t) + 1, L'\0');
        DWORD cb = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
        if (::RegQueryValueExW(key_, name.c_str(), nullptr, &type,
                               reinterpret_cast<LPBYTE>(buf.data()), &cb) != ERROR_SUCCESS)
            return std::nullopt;
        buf.resize(wcslen(buf.c_str()));
        return buf;
    }

    // DigitalProductId / DigitalProductId4 are REG_BINARY values.
    std::optional<std::vector<BYTE>> queryBinary(const std::wstring& name) const
    {
        if (!key_) return std::nullopt;
        DWORD type = 0, bytes = 0;
        if (::RegQueryValueExW(key_, name.c_str(), nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS)
            return std::nullopt;
        std::vector<BYTE> buf(bytes);
        if (bytes && ::RegQueryValueExW(key_, name.c_str(), nullptr, &type, buf.data(), &bytes) != ERROR_SUCCESS)
            return std::nullopt;
        return buf;
    }

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
        wchar_t name[256];
        for (DWORD i = 0; ; ++i)
        {
            DWORD n = static_cast<DWORD>(std::size(name));
            LONG r = ::RegEnumKeyExW(key_, i, name, &n, nullptr, nullptr, nullptr, nullptr);
            if (r == ERROR_NO_MORE_ITEMS) break;
            if (r != ERROR_SUCCESS) continue;
            out.emplace_back(name, n);
        }
        return out;
    }

    bool setString(const std::wstring& name, const std::wstring& value)
    {
        return key_ && ::RegSetValueExW(key_, name.c_str(), 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    }
    bool setDword(const std::wstring& name, DWORD value)
    {
        return key_ && ::RegSetValueExW(key_, name.c_str(), 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
    }
    bool setBinary(const std::wstring& name, const void* data, DWORD bytes)
    {
        return key_ && ::RegSetValueExW(key_, name.c_str(), 0, REG_BINARY,
                   static_cast<const BYTE*>(data), bytes) == ERROR_SUCCESS;
    }

private:
    HKEY key_ = nullptr;
    bool owned_ = false;
};
