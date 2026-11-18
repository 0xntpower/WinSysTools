// RegKey.h -- minimal RAII wrapper over a registry key. DebugView stores its
// settings and the Sysinternals EULA flag under HKCU, so a small typed helper
// keeps the open and close paired.
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
    ~RegKey()
    {
        reset();
    }

    void reset() noexcept
    {
        if (key_)
        {
            ::RegCloseKey(key_);
            key_ = nullptr;
        }
    }

    bool open(HKEY root, const std::wstring& sub, REGSAM access = KEY_READ)
    {
        reset();
        return ::RegOpenKeyExW(root, sub.c_str(), 0, access, &key_) == ERROR_SUCCESS;
    }

    bool create(HKEY root, const std::wstring& sub, REGSAM access = KEY_READ | KEY_WRITE)
    {
        reset();
        return ::RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, access, nullptr, &key_, nullptr) == ERROR_SUCCESS;
    }

    bool valid() const noexcept
    {
        return key_ != nullptr;
    }

    std::optional<DWORD> queryDword(const std::wstring& name) const
    {
        if (!key_)
        {
            return std::nullopt;
        }
        DWORD type = 0, value = 0, cb = sizeof(value);
        if (::RegQueryValueExW(key_, name.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(&value), &cb) !=
            ERROR_SUCCESS)
        {
            return std::nullopt;
        }
        if (type != REG_DWORD)
        {
            return std::nullopt;
        }
        return value;
    }

    std::optional<std::wstring> queryString(const std::wstring& name) const
    {
        if (!key_)
        {
            return std::nullopt;
        }
        DWORD type = 0, bytes = 0;
        if (::RegQueryValueExW(key_, name.c_str(), nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS)
        {
            return std::nullopt;
        }
        if (type != REG_SZ && type != REG_EXPAND_SZ)
        {
            return std::nullopt;
        }
        std::wstring buf(bytes / sizeof(wchar_t) + 1, L'\0');
        DWORD cb = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
        if (::RegQueryValueExW(key_, name.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &cb) !=
            ERROR_SUCCESS)
        {
            return std::nullopt;
        }
        buf.resize(wcslen(buf.c_str()));
        return buf;
    }

    std::optional<std::vector<BYTE>> queryBinary(const std::wstring& name) const
    {
        if (!key_)
        {
            return std::nullopt;
        }
        DWORD type = 0, bytes = 0;
        if (::RegQueryValueExW(key_, name.c_str(), nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS)
        {
            return std::nullopt;
        }
        std::vector<BYTE> buf(bytes);
        if (bytes && ::RegQueryValueExW(key_, name.c_str(), nullptr, &type, buf.data(), &bytes) != ERROR_SUCCESS)
        {
            return std::nullopt;
        }
        return buf;
    }

    bool setDword(const std::wstring& name, DWORD value)
    {
        return key_ && ::RegSetValueExW(key_, name.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                                        sizeof(value)) == ERROR_SUCCESS;
    }

    bool setString(const std::wstring& name, const std::wstring& value)
    {
        return key_ && ::RegSetValueExW(key_, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    }

    bool setBinary(const std::wstring& name, const void* data, DWORD bytes)
    {
        return key_ && ::RegSetValueExW(key_, name.c_str(), 0, REG_BINARY, static_cast<const BYTE*>(data), bytes) ==
                           ERROR_SUCCESS;
    }

  private:
    HKEY key_ = nullptr;
};
