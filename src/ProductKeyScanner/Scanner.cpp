// Scanner.cpp -- registry / WMI / BIOS product-key collection.
#include "Scanner.h"
#include "Decoder.h"
#include "RegKey.h"
#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>
#include <string>

#pragma comment(lib, "wbemuuid.lib")

namespace
{
    // Full display paths (relative to the HKLM root) whose subtrees the original
    // walk refuses to descend, matching the four wcsicmp guards in sub_1400100E4.
    const wchar_t* kSkipPaths[] = {
        L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Perflib",
        L"Software\\Wow6432Node\\Microsoft\\Windows NT\\CurrentVersion\\Perflib",
        L"Software\\Classes",
        L"Software\\Wow6432Node\\Classes",
    };

    bool isSkipped(const std::wstring& path)
    {
        for (const wchar_t* s : kSkipPaths)
            if (_wcsicmp(path.c_str(), s) == 0)
                return true;
        return false;
    }

    std::wstring firstNonEmpty(const RegKey& k, std::initializer_list<const wchar_t*> names)
    {
        for (const wchar_t* n : names)
            if (auto v = k.queryString(n); v && !v->empty())
                return *v;
        return {};
    }

    // Product ID fallback from the blob (offset 8): ANSI for a DigitalProductId
    // blob, UTF-16 for a DigitalProductId4 blob (the distinction in sub_14000FE18).
    std::wstring productIdFromBlob(const std::vector<BYTE>& blob)
    {
        if (blob.size() < 10) return {};
        if (blob.size() >= 0x338)
        {
            const wchar_t* w = reinterpret_cast<const wchar_t*>(blob.data() + 8);
            size_t maxw = (blob.size() - 8) / sizeof(wchar_t);
            size_t n = 0;
            while (n < maxw && n < 63 && w[n]) ++n;
            return std::wstring(w, n);
        }
        const char* a = reinterpret_cast<const char*>(blob.data() + 8);
        size_t maxa = blob.size() - 8;
        size_t n = 0;
        while (n < maxa && n < 63 && a[n]) ++n;
        std::wstring out;
        if (n)
        {
            int wn = ::MultiByteToWideChar(CP_ACP, 0, a, (int)n, nullptr, 0);
            out.resize(wn);
            ::MultiByteToWideChar(CP_ACP, 0, a, (int)n, out.data(), wn);
        }
        return out;
    }

    std::wstring formatTime(const std::optional<FILETIME>& ft)
    {
        if (!ft) return {};
        FILETIME local{};
        if (!::FileTimeToLocalFileTime(&*ft, &local)) return {};
        SYSTEMTIME st{};
        if (!::FileTimeToSystemTime(&local, &st)) return {};
        wchar_t date[64] = {}, time[64] = {};
        ::GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, date, 64);
        ::GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, time, 64);
        return std::wstring(date) + L" " + time;
    }

    // Read DigitalProductId / DigitalProductId4 at one key and, on a successful
    // decode, append a row. Mirrors sub_14000FE18.
    void tryValue(const RegKey& k, const std::wstring& displayPath,
                  const wchar_t* valueName, std::vector<ProductKeyRecord>& out)
    {
        auto blob = k.queryBinary(valueName);
        if (!blob || blob->empty()) return;
        std::wstring key = DecodeProductKeyBlob(blob->data(), blob->size());
        if (key.empty()) return;

        ProductKeyRecord rec;
        rec.productKey = key;
        rec.productId = firstNonEmpty(k, { L"ProductId" });
        if (rec.productId.empty())
            rec.productId = productIdFromBlob(*blob);
        rec.productName = firstNonEmpty(k, { L"ProductName", L"ConvertToEdition", L"ProductNameNonQualified" });
        rec.scanType = L"Registry";
        rec.registryKey = L"HKEY_LOCAL_MACHINE\\" + displayPath;
        rec.registryValue = valueName;
        rec.modifiedTime = formatTime(k.lastWriteTime());
        out.push_back(std::move(rec));
    }

    void walk(RegKey& node, const std::wstring& displayPath, int depth,
              std::vector<ProductKeyRecord>& out, std::atomic<bool>* stop)
    {
        if (depth > 0x20) return;                         // sub_1400100E4 depth cap
        if (stop && stop->load()) return;
        if (isSkipped(displayPath)) return;

        tryValue(node, displayPath, L"DigitalProductId", out);
        tryValue(node, displayPath, L"DigitalProductId4", out);

        for (const auto& sub : node.subKeys())
        {
            if (stop && stop->load()) return;
            RegKey child;
            if (child.open(node.get(), sub))
                walk(child, displayPath + L"\\" + sub, depth + 1, out, stop);
        }
    }

    void scanRegistryLive(std::vector<ProductKeyRecord>& out, std::atomic<bool>* stop)
    {
        RegKey sw;
        if (sw.open(HKEY_LOCAL_MACHINE, L"Software", KEY_READ | KEY_WOW64_64KEY))
            walk(sw, L"Software", 0, out, stop);
    }

    void scanRegistryHive(const std::wstring& hivePath, std::vector<ProductKeyRecord>& out,
                          std::atomic<bool>* stop)
    {
        HKEY loaded = nullptr;
        // RegLoadAppKey mounts a hive file without SeRestore privilege (Win7+).
        if (::RegLoadAppKeyW(hivePath.c_str(), &loaded, KEY_READ, 0, 0) != ERROR_SUCCESS)
            return;
        RegKey root;
        // Walk from a "Software" virtual prefix so the skip paths and the Registry
        // Key column match the live-scan shape. Opening the loaded root with an
        // empty subkey yields an owned duplicate handle for the RAII wrapper.
        if (root.open(loaded, L"", KEY_READ))
            walk(root, L"Software", 0, out, stop);
        ::RegCloseKey(loaded);
    }

    // ---- WMI (sub_140010AAC) -------------------------------------------------
    std::wstring bstrToW(VARIANT& v)
    {
        if (v.vt == VT_BSTR && v.bstrVal) return std::wstring(v.bstrVal, ::SysStringLen(v.bstrVal));
        return {};
    }

    void queryWmiClass(IWbemServices* svc, const wchar_t* query, bool licensingService,
                       std::vector<ProductKeyRecord>& out, std::atomic<bool>* stop)
    {
        IEnumWbemClassObject* en = nullptr;
        if (FAILED(svc->ExecQuery(_bstr_t(L"WQL"), _bstr_t(query),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en)) || !en)
            return;

        IWbemClassObject* obj = nullptr;
        ULONG got = 0;
        while (!(stop && stop->load()) && en->Next(WBEM_INFINITE, 1, &obj, &got) == S_OK && got)
        {
            auto get = [&](const wchar_t* prop) -> std::wstring {
                VARIANT v; VariantInit(&v);
                std::wstring r;
                if (SUCCEEDED(obj->Get(prop, 0, &v, nullptr, nullptr)))
                    r = bstrToW(v);
                VariantClear(&v);
                return r;
            };

            ProductKeyRecord rec;
            rec.scanType = L"WMI";
            if (licensingService)
            {
                rec.productKey = get(L"OA3xOriginalProductKey");
                rec.registryValue = L"OA3xOriginalProductKey";
                rec.registryKey = L"SoftwareLicensingService";
            }
            else
            {
                rec.productName = get(L"Name");
                rec.productId = get(L"ProductKeyID");
                std::wstring partial = get(L"PartialProductKey");
                if (partial.size() == 5)
                    rec.productKey = L"XXXXX-XXXXX-XXXXX-XXXXX-" + partial;
                else
                    rec.productKey = partial;
                rec.registryValue = L"PartialProductKey";
                rec.registryKey = L"SoftwareLicensingProduct";
            }
            if (!rec.productKey.empty())
                out.push_back(std::move(rec));
            obj->Release();
            obj = nullptr;
        }
        if (obj) obj->Release();
        en->Release();
    }

    void scanWmi(std::vector<ProductKeyRecord>& out, std::atomic<bool>* stop)
    {
        IWbemLocator* loc = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                IID_IWbemLocator, reinterpret_cast<void**>(&loc))) || !loc)
            return;
        IWbemServices* svc = nullptr;
        if (SUCCEEDED(loc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr,
                0, nullptr, nullptr, &svc)) && svc)
        {
            CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
            queryWmiClass(svc,
                L"SELECT * FROM SoftwareLicensingProduct WHERE PartialProductKey IS NOT NULL",
                false, out, stop);
            queryWmiClass(svc,
                L"SELECT * FROM SoftwareLicensingService WHERE OA3xOriginalProductKey IS NOT NULL",
                true, out, stop);
            svc->Release();
        }
        loc->Release();
    }

    // ---- BIOS: ACPI MSDM firmware table -------------------------------------
    constexpr DWORD fourcc(const char* s)
    {
        return (DWORD)(BYTE)s[0] | ((DWORD)(BYTE)s[1] << 8) | ((DWORD)(BYTE)s[2] << 16) | ((DWORD)(BYTE)s[3] << 24);
    }

    void scanBios(std::vector<ProductKeyRecord>& out)
    {
        UINT size = ::GetSystemFirmwareTable(fourcc("ACPI"), fourcc("MSDM"), nullptr, 0);
        if (size < 56 + 29) return;
        std::vector<BYTE> buf(size);
        if (::GetSystemFirmwareTable(fourcc("ACPI"), fourcc("MSDM"), buf.data(), size) != size)
            return;
        // MSDM: 36-byte ACPI header, then version/reserved/datatype/reserved/length,
        // then the product key as 29 ASCII bytes at offset 56.
        const char* k = reinterpret_cast<const char*>(buf.data() + 56);
        size_t n = 0;
        while (n < 29 && k[n]) ++n;
        if (!n) return;
        std::wstring key;
        int wn = ::MultiByteToWideChar(CP_ACP, 0, k, (int)n, nullptr, 0);
        key.resize(wn);
        ::MultiByteToWideChar(CP_ACP, 0, k, (int)n, key.data(), wn);

        ProductKeyRecord rec;
        rec.productKey = key;
        rec.scanType = L"BIOS";
        rec.registryValue = L"MSDM";
        out.push_back(std::move(rec));
    }
}

std::vector<ProductKeyRecord> EnumerateProductKeys(const ScanOptions& opts, std::atomic<bool>* stop)
{
    std::vector<ProductKeyRecord> out;

    if (opts.source == ScanSource::ExternalHive)
    {
        if (!opts.hivePath.empty())
            scanRegistryHive(opts.hivePath, out, stop);
        return out;  // WMI and BIOS only apply to the running system
    }

    scanRegistryLive(out, stop);

    const bool comReady = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    if (opts.useWMI && !(stop && stop->load()))
        scanWmi(out, stop);
    if (opts.useBIOS && !(stop && stop->load()))
        scanBios(out);
    if (comReady)
        CoUninitialize();

    return out;
}
