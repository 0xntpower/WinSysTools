// Signature.h -- Authenticode verification and version-info lookup.
//
// Query reads the image's version info (FileDescription, CompanyName,
// FileVersion) and runs WinVerifyTrust with WINTRUST_ACTION_GENERIC_VERIFY_V2
// over the file. When the file is trusted the Authenticode subject name is
// pulled from the embedded PKCS7 signer certificate. This runs on a background
// worker, exactly as the original defers verification off the UI thread.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <softpub.h>
#include <wintrust.h>
#include <wincrypt.h>
#include <mscat.h>
#include <bcrypt.h>
#include <string>
#include <vector>

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "version.lib")

#include "AutorunEntry.h"

namespace ar::sig
{
    struct Info
    {
        SigState state = SigState::NotVerified;
        std::wstring signer;
        std::wstring description;
        std::wstring company;
        std::wstring version;
    };

    inline std::wstring VerString(const unsigned char* block, const wchar_t* lang, const wchar_t* field)
    {
        wchar_t sub[128]{};
        wsprintfW(sub, L"\\StringFileInfo\\%s\\%s", lang, field);
        void* p = nullptr;
        UINT n = 0;
        if (::VerQueryValueW(block, sub, &p, &n) && p && n)
        {
            return std::wstring(static_cast<wchar_t*>(p), n ? n - 1 : 0);
        }
        return {};
    }

    inline void ReadVersionInfo(const std::wstring& path, Info& out)
    {
        DWORD handle = 0;
        const DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), &handle);
        if (size == 0)
        {
            return;
        }
        std::wstring buf;
        buf.resize((size + 1) / sizeof(wchar_t) + 1);
        if (!::GetFileVersionInfoW(path.c_str(), 0, size, buf.data()))
        {
            return;
        }
        const auto* block = reinterpret_cast<const unsigned char*>(buf.data());

        // Pick the first translation, then fall back to the common US-English
        // and Unicode code-page pairings when no translation table is present.
        struct LangCp
        {
            WORD lang;
            WORD cp;
        } pair{0x0409, 0x04B0};
        void* tp = nullptr;
        UINT tn = 0;
        if (::VerQueryValueW(block, L"\\VarFileInfo\\Translation", &tp, &tn) && tp && tn >= sizeof(LangCp))
        {
            pair = *static_cast<LangCp*>(tp);
        }
        wchar_t lang[16]{};
        wsprintfW(lang, L"%04x%04x", pair.lang, pair.cp);

        out.description = VerString(block, lang, L"FileDescription");
        out.company = VerString(block, lang, L"CompanyName");
        out.version = VerString(block, lang, L"FileVersion");
        if (out.description.empty())
        {
            out.description = VerString(block, L"040904B0", L"FileDescription");
        }
        if (out.company.empty())
        {
            out.company = VerString(block, L"040904B0", L"CompanyName");
        }
    }

    // Pull the subject display name from the file's embedded PKCS7 signer cert.
    inline std::wstring EmbeddedSignerName(const std::wstring& path)
    {
        HCERTSTORE store = nullptr;
        HCRYPTMSG msg = nullptr;
        if (!::CryptQueryObject(CERT_QUERY_OBJECT_FILE, path.c_str(), CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                                CERT_QUERY_FORMAT_FLAG_BINARY, 0, nullptr, nullptr, nullptr, &store, &msg, nullptr))
        {
            return {};
        }
        std::wstring result;
        DWORD cb = 0;
        if (::CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &cb) && cb)
        {
            std::string raw(cb, '\0');
            auto* si = reinterpret_cast<CMSG_SIGNER_INFO*>(raw.data());
            if (::CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, si, &cb))
            {
                CERT_INFO ci{};
                ci.Issuer = si->Issuer;
                ci.SerialNumber = si->SerialNumber;
                if (PCCERT_CONTEXT cert = ::CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                                       0, CERT_FIND_SUBJECT_CERT, &ci, nullptr))
                {
                    const DWORD n = ::CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nullptr, 0);
                    if (n > 1)
                    {
                        result.resize(n);
                        ::CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, result.data(), n);
                        result.resize(n - 1);
                    }
                    ::CertFreeCertificateContext(cert);
                }
            }
        }
        if (msg)
        {
            ::CryptMsgClose(msg);
        }
        if (store)
        {
            ::CertCloseStore(store, 0);
        }
        return result;
    }

    // Verify an embedded Authenticode signature on the file itself.
    inline bool VerifyEmbedded(const std::wstring& path)
    {
        WINTRUST_FILE_INFO fi{};
        fi.cbStruct = sizeof(fi);
        fi.pcwszFilePath = path.c_str();

        WINTRUST_DATA wd{};
        wd.cbStruct = sizeof(wd);
        wd.dwUIChoice = WTD_UI_NONE;
        wd.fdwRevocationChecks = WTD_REVOKE_NONE;
        wd.dwUnionChoice = WTD_CHOICE_FILE;
        wd.pFile = &fi;
        wd.dwStateAction = WTD_STATEACTION_VERIFY;
        wd.dwProvFlags = WTD_SAFER_FLAG | WTD_CACHE_ONLY_URL_RETRIEVAL;

        GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        const LONG r = ::WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);

        wd.dwStateAction = WTD_STATEACTION_CLOSE;
        ::WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
        return r == ERROR_SUCCESS;
    }

    // Many Windows components carry no embedded signature and are trusted
    // through a security catalog instead. Hash the file, find the catalog that
    // vouches for it, verify through WTD_CHOICE_CATALOG and take the signer from
    // the catalog's own certificate (typically Microsoft Windows), matching how
    // the original reports these images as verified.
    inline bool VerifyCatalog(const std::wstring& path, std::wstring& signer)
    {
        HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        bool ok = false;
        HCATADMIN admin = nullptr;
        GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        // Modern Windows catalogs hash with SHA-256, so the SHA-256 context is
        // used through CryptCATAdminAcquireContext2. The legacy SHA-1 path would
        // miss every current OS catalog and leave Windows images unverified.
        if (::CryptCATAdminAcquireContext2(&admin, nullptr, BCRYPT_SHA256_ALGORITHM, nullptr, 0))
        {
            DWORD hashLen = 0;
            ::CryptCATAdminCalcHashFromFileHandle2(admin, file, &hashLen, nullptr, 0);
            if (hashLen)
            {
                std::vector<BYTE> hash(hashLen);
                if (::CryptCATAdminCalcHashFromFileHandle2(admin, file, &hashLen, hash.data(), 0))
                {
                    HCATINFO cat = ::CryptCATAdminEnumCatalogFromHash(admin, hash.data(), hashLen, 0, nullptr);
                    if (cat)
                    {
                        CATALOG_INFO ci{};
                        ci.cbStruct = sizeof(ci);
                        if (::CryptCATCatalogInfoFromContext(cat, &ci, 0))
                        {
                            std::wstring tag;
                            tag.reserve(hashLen * 2);
                            for (DWORD i = 0; i < hashLen; ++i)
                            {
                                wchar_t hx[3]{};
                                wsprintfW(hx, L"%02X", hash[i]);
                                tag += hx;
                            }
                            WINTRUST_CATALOG_INFO wci{};
                            wci.cbStruct = sizeof(wci);
                            wci.pcwszCatalogFilePath = ci.wszCatalogFile;
                            wci.pcwszMemberFilePath = path.c_str();
                            wci.pcwszMemberTag = tag.c_str();
                            wci.hMemberFile = file;
                            wci.pbCalculatedFileHash = hash.data();
                            wci.cbCalculatedFileHash = hashLen;
                            // The admin context carries the SHA-256 algorithm, so
                            // WinVerifyTrust hashes the member the same way the
                            // catalog did. Without it the verify defaults to SHA-1
                            // and fails with TRUST_E_NOSIGNATURE.
                            wci.hCatAdmin = admin;

                            WINTRUST_DATA wd{};
                            wd.cbStruct = sizeof(wd);
                            wd.dwUIChoice = WTD_UI_NONE;
                            wd.fdwRevocationChecks = WTD_REVOKE_NONE;
                            wd.dwUnionChoice = WTD_CHOICE_CATALOG;
                            wd.pCatalog = &wci;
                            wd.dwStateAction = WTD_STATEACTION_VERIFY;
                            wd.dwProvFlags = WTD_SAFER_FLAG | WTD_CACHE_ONLY_URL_RETRIEVAL;

                            const LONG r = ::WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
                            wd.dwStateAction = WTD_STATEACTION_CLOSE;
                            ::WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
                            ok = (r == ERROR_SUCCESS);
                            if (ok)
                            {
                                signer = EmbeddedSignerName(ci.wszCatalogFile);
                            }
                        }
                        ::CryptCATAdminReleaseCatalogContext(admin, cat, 0);
                    }
                }
            }
            ::CryptCATAdminReleaseContext(admin, 0);
        }
        ::CloseHandle(file);
        return ok;
    }

    inline Info Query(const std::wstring& path)
    {
        Info out;
        if (path.empty())
        {
            return out;
        }
        ReadVersionInfo(path, out);
        if (VerifyEmbedded(path))
        {
            out.state = SigState::Verified;
            out.signer = EmbeddedSignerName(path);
        }
        else if (VerifyCatalog(path, out.signer))
        {
            out.state = SigState::Verified;
        }
        else
        {
            out.state = SigState::NotVerified;
        }
        return out;
    }
} // namespace ar::sig
