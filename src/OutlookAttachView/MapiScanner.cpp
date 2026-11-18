// MapiScanner.cpp -- dynamic mapi32.dll logon and the store, folder, message
// and attachment walk. See MapiScanner.h for the contract.
#include "MapiScanner.h"
#include "MapiMin.h"
#include <string>

namespace
{
    // ---- dynamic mapi32.dll binding (reproduces the original's loader) ----
    struct MapiApi
    {
        HMODULE          dll = nullptr;
        MAPIINITIALIZE   Initialize = nullptr;
        MAPIUNINITIALIZE Uninitialize = nullptr;
        MAPILOGONEX      LogonEx = nullptr;
        MAPIFREEBUFFER   FreeBuffer = nullptr;
        void (STDAPICALLTYPE* FreeProws)(LPSRowSet) = nullptr;

        bool load()
        {
            if (dll) return true;
            dll = ::LoadLibraryExW(L"mapi32.dll", nullptr, 0);
            if (!dll) return false;
            Initialize = reinterpret_cast<MAPIINITIALIZE>(::GetProcAddress(dll, "MAPIInitialize"));
            Uninitialize = reinterpret_cast<MAPIUNINITIALIZE>(::GetProcAddress(dll, "MAPIUninitialize"));
            LogonEx = reinterpret_cast<MAPILOGONEX>(::GetProcAddress(dll, "MAPILogonEx"));
            FreeBuffer = reinterpret_cast<MAPIFREEBUFFER>(::GetProcAddress(dll, "MAPIFreeBuffer"));
            FreeProws = reinterpret_cast<decltype(FreeProws)>(::GetProcAddress(dll, "FreeProws"));
            return Initialize && Uninitialize && LogonEx && FreeBuffer;
        }
    };

    void freeRows(MapiApi& api, LPSRowSet rows)
    {
        if (!rows) return;
        if (api.FreeProws) { api.FreeProws(rows); return; }
        for (ULONG i = 0; i < rows->cRows; ++i)
            if (rows->aRow[i].lpProps) api.FreeBuffer(rows->aRow[i].lpProps);
        api.FreeBuffer(rows);
    }

    // A property-tag array sized at compile time, laid out like SPropTagArray.
    template <int N>
    struct TagArray
    {
        ULONG cValues = N;
        ULONG tags[N];
        LPSPropTagArray p() { return reinterpret_cast<LPSPropTagArray>(this); }
    };

    const SPropValue* findProp(const SRow& row, ULONG tag)
    {
        for (ULONG i = 0; i < row.cValues; ++i)
            if (row.lpProps[i].ulPropTag == tag) return &row.lpProps[i];
        return nullptr;
    }
    const SPropValue* findProp(ULONG c, const SPropValue* props, ULONG tag)
    {
        for (ULONG i = 0; i < c; ++i)
            if (props[i].ulPropTag == tag) return &props[i];
        return nullptr;
    }

    std::wstring propStr(const SPropValue* p)
    {
        if (!p) return {};
        if (PROP_TYPE(p->ulPropTag) == PT_UNICODE && p->Value.lpszW) return p->Value.lpszW;
        return {};
    }

    std::wstring hexOf(const SBinary& bin)
    {
        static const wchar_t* digits = L"0123456789ABCDEF";
        std::wstring out;
        out.reserve(bin.cb * 2);
        for (ULONG i = 0; i < bin.cb; ++i)
        {
            out += digits[bin.lpb[i] >> 4];
            out += digits[bin.lpb[i] & 0x0F];
        }
        return out;
    }

    std::wstring formatTime(const FILETIME& ftUtc, bool gmt)
    {
        if (ftUtc.dwLowDateTime == 0 && ftUtc.dwHighDateTime == 0) return {};
        FILETIME ft = ftUtc;
        if (!gmt)
        {
            FILETIME local{};
            if (::FileTimeToLocalFileTime(&ftUtc, &local)) ft = local;
        }
        SYSTEMTIME st{};
        if (!::FileTimeToSystemTime(&ft, &st)) return {};
        wchar_t date[64]{}, time[64]{};
        ::GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, date, 64);
        ::GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, time, 64);
        std::wstring out = date;
        out += L' ';
        out += time;
        return out;
    }

    std::wstring groupNumber(unsigned long long v)
    {
        wchar_t raw[32];
        wsprintfW(raw, L"%llu", v);
        wchar_t out[48]{};
        ::GetNumberFormatW(LOCALE_USER_DEFAULT, 0, raw, nullptr, out, 48);
        // Strip the fractional part the default format adds.
        std::wstring s = out;
        size_t dot = s.find_last_of(L".,");
        if (dot != std::wstring::npos && s.find_first_not_of(L"0", dot + 1) == std::wstring::npos)
            s.erase(dot);
        return s.empty() ? raw : s;
    }

    std::wstring formatUnit(unsigned long long bytes, int divisorShift, const wchar_t* suffix)
    {
        double v = (double)bytes / (double)(1ull << divisorShift);
        wchar_t num[48];
        swprintf(num, 48, L"%.2f", v);
        std::wstring s = num;
        s += suffix;
        return s;
    }

    std::wstring formatSize(unsigned long long bytes, int unit)
    {
        switch (unit)
        {
        case 2: return formatUnit(bytes, 10, L" KB");
        case 3: return formatUnit(bytes, 20, L" MB");
        case 4: return formatUnit(bytes, 30, L" GB");
        case 5: // Automatic
            if (bytes >= (1ull << 30)) return formatUnit(bytes, 30, L" GB");
            if (bytes >= (1ull << 20)) return formatUnit(bytes, 20, L" MB");
            if (bytes >= (1ull << 10)) return formatUnit(bytes, 10, L" KB");
            return groupNumber(bytes);
        default: return groupNumber(bytes);   // Bytes
        }
    }

    const wchar_t* attachTypeName(int method)
    {
        switch (method)
        {
        case ATTACH_BY_VALUE:       return L"By Value";
        case ATTACH_BY_REFERENCE:
        case ATTACH_BY_REF_RESOLVE:
        case ATTACH_BY_REF_ONLY:    return L"By Reference";
        case ATTACH_EMBEDDED_MSG:   return L"Embedded Item";
        case ATTACH_OLE:            return L"OLE";
        default:                    return L"";
        }
    }

    std::wstring extensionOf(const std::wstring& filename)
    {
        size_t dot = filename.find_last_of(L'.');
        if (dot == std::wstring::npos || dot + 1 >= filename.size()) return {};
        return filename.substr(dot + 1);
    }

    std::wstring domainOf(const std::wstring& email)
    {
        size_t at = email.find_last_of(L'@');
        if (at == std::wstring::npos || at + 1 >= email.size()) return {};
        return email.substr(at + 1);
    }

    // Pull one header value out of PR_TRANSPORT_MESSAGE_HEADERS, matching the
    // original: "Received: from ..." gives the computer address and "x-mailer:"
    // gives the sending software.
    void parseHeaders(const std::wstring& headers, std::wstring& computer, std::wstring& mailer)
    {
        size_t pos = 0;
        while (pos < headers.size())
        {
            size_t end = headers.find_first_of(L"\r\n", pos);
            std::wstring line = headers.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
            if (computer.empty() && _wcsnicmp(line.c_str(), L"Received: from", 14) == 0)
            {
                std::wstring v = line.substr(14);
                size_t b = v.find_first_not_of(L' ');
                if (b != std::wstring::npos) v = v.substr(b);
                size_t paren = v.find_first_of(L")]");
                if (paren != std::wstring::npos) v = v.substr(0, paren + 1);
                computer = v;
            }
            if (mailer.empty() && _wcsnicmp(line.c_str(), L"x-mailer:", 9) == 0)
            {
                std::wstring v = line.substr(9);
                size_t b = v.find_first_not_of(L' ');
                mailer = (b == std::wstring::npos) ? L"" : v.substr(b);
            }
            if (end == std::wstring::npos) break;
            pos = headers.find_first_not_of(L"\r\n", end);
            if (pos == std::wstring::npos) break;
        }
    }

    // ---- the walk ------------------------------------------------------
    struct Walker
    {
        MapiApi& api;
        const ScanParams& params;
        std::vector<AttachRecord>& out;
        std::wstring storeEntryHex;

        bool cancelled() const { return params.cancelled && params.cancelled(); }

        void readRecipients(LPMESSAGE msg, std::wstring& toNames, std::wstring& toEmails)
        {
            LPMAPITABLE table = nullptr;
            if (FAILED(msg->GetRecipientTable(MAPI_UNICODE, &table)) || !table) return;
            TagArray<4> tags{ 4, { PR_DISPLAY_NAME_W, PR_EMAIL_ADDRESS_W, PR_SMTP_ADDRESS_W, PR_RECIPIENT_TYPE } };
            table->SetColumns(tags.p(), 0);
            LPSRowSet rows = nullptr;
            while (SUCCEEDED(table->QueryRows(64, 0, &rows)) && rows && rows->cRows)
            {
                for (ULONG r = 0; r < rows->cRows; ++r)
                {
                    const SPropValue* rt = findProp(rows->aRow[r], PR_RECIPIENT_TYPE);
                    if (rt && rt->Value.l != MAPI_TO) continue;
                    std::wstring name = propStr(findProp(rows->aRow[r], PR_DISPLAY_NAME_W));
                    std::wstring email = propStr(findProp(rows->aRow[r], PR_SMTP_ADDRESS_W));
                    if (email.empty()) email = propStr(findProp(rows->aRow[r], PR_EMAIL_ADDRESS_W));
                    if (!name.empty()) { if (!toNames.empty()) toNames += L", "; toNames += name; }
                    if (!email.empty()) { if (!toEmails.empty()) toEmails += L", "; toEmails += email; }
                }
                freeRows(api, rows);
                rows = nullptr;
            }
            freeRows(api, rows);
            table->Release();
        }

        void readAttachments(LPMESSAGE msg, const std::wstring& msgEntryHex,
                             const std::wstring& folderName, const std::wstring& folderPath,
                             const std::wstring* shared, const FILETIME& created, const FILETIME& delivery)
        {
            LPMAPITABLE table = nullptr;
            if (FAILED(msg->GetAttachmentTable(MAPI_UNICODE, &table)) || !table) return;
            TagArray<9> tags{ 9, { PR_ATTACH_NUM, PR_ATTACH_METHOD, PR_ATTACH_LONG_FILENAME_W,
                                   PR_ATTACH_FILENAME_W, PR_ATTACH_SIZE, PR_ATTACH_MIME_TAG_W,
                                   PR_ATTACH_CONTENT_ID_W, PR_RENDERING_POSITION, PR_DISPLAY_NAME_W } };
            table->SetColumns(tags.p(), 0);
            LPSRowSet rows = nullptr;
            while (SUCCEEDED(table->QueryRows(64, 0, &rows)) && rows && rows->cRows)
            {
                for (ULONG r = 0; r < rows->cRows; ++r)
                {
                    const SRow& row = rows->aRow[r];
                    std::wstring name = propStr(findProp(row, PR_ATTACH_LONG_FILENAME_W));
                    if (name.empty()) name = propStr(findProp(row, PR_ATTACH_FILENAME_W));
                    if (name.empty()) name = propStr(findProp(row, PR_DISPLAY_NAME_W));

                    const SPropValue* pRender = findProp(row, PR_RENDERING_POSITION);
                    std::wstring contentId = propStr(findProp(row, PR_ATTACH_CONTENT_ID_W));
                    bool isInline = !contentId.empty() ||
                                    (pRender && pRender->Value.l != -1 && pRender->Value.l != 0);
                    if (isInline && !params.showInlineAttachments) continue;

                    AttachRecord rec;
                    const SPropValue* pNum = findProp(row, PR_ATTACH_NUM);
                    const SPropValue* pMethod = findProp(row, PR_ATTACH_METHOD);
                    const SPropValue* pSize = findProp(row, PR_ATTACH_SIZE);
                    rec.attachNum = pNum ? (unsigned)pNum->Value.l : 0;
                    rec.attachIdKey = pNum ? pNum->Value.l : 0;
                    rec.sizeBytes = pSize ? (unsigned long long)(unsigned long)pSize->Value.l : 0;
                    rec.inlineAttachment = isInline;
                    rec.storeEntryId = storeEntryHex;
                    rec.msgEntryId = msgEntryHex;

                    rec.cells[0] = name;
                    rec.cells[1] = shared[0];                               // From
                    rec.cells[2] = shared[1];                               // To
                    rec.cells[3] = shared[2];                               // Subject
                    rec.cells[4] = propStr(findProp(row, PR_ATTACH_MIME_TAG_W));
                    rec.cells[5] = attachTypeName(pMethod ? pMethod->Value.l : 0);
                    rec.cells[6] = formatTime(created, params.display.showTimeInGmt);
                    rec.cells[7] = formatSize(rec.sizeBytes, params.display.fileSizeUnit);
                    rec.cells[8] = std::to_wstring(rec.attachNum);
                    rec.cells[9] = isInline ? L"Yes" : L"No";
                    rec.cells[10] = folderName;
                    rec.cells[11] = folderPath;
                    rec.cells[12] = shared[3];                              // Computer Address
                    rec.cells[13] = shared[4];                             // Email Software
                    rec.cells[14] = extensionOf(name);
                    rec.cells[15] = shared[5];                             // From Email
                    rec.cells[16] = shared[6];                             // To Email
                    rec.cells[17] = shared[7];                             // Message Direction
                    rec.cells[18] = shared[8];                             // Domain
                    rec.cells[19] = msgEntryHex;
                    rec.cells[20] = formatTime(delivery, params.display.showTimeInGmt);
                    rec.cells[21] = L"";                                   // Hash (filled on demand)

                    rec.createdKey = ((long long)created.dwHighDateTime << 32) | created.dwLowDateTime;
                    rec.deliveryKey = ((long long)delivery.dwHighDateTime << 32) | delivery.dwLowDateTime;

                    out.push_back(std::move(rec));
                }
                freeRows(api, rows);
                rows = nullptr;
            }
            freeRows(api, rows);
            table->Release();
        }

        void processMessage(LPMESSAGE msg, const std::wstring& folderName, const std::wstring& folderPath)
        {
            TagArray<11> tags{ 11, { PR_SUBJECT_W, PR_SENDER_NAME_W, PR_SENDER_EMAIL_ADDRESS_W,
                                     PR_DISPLAY_TO_W, PR_CREATION_TIME, PR_MESSAGE_DELIVERY_TIME,
                                     PR_MESSAGE_FLAGS, PR_TRANSPORT_MESSAGE_HEADERS_W,
                                     PR_ENTRYID, PR_SENT_REPRESENTING_NAME_W,
                                     PR_SENT_REPRESENTING_EMAIL_W } };
            ULONG cValues = 0;
            LPSPropValue props = nullptr;
            if (FAILED(msg->GetProps(tags.p(), MAPI_UNICODE, &cValues, &props)) || !props) return;

            std::wstring from = propStr(findProp(cValues, props, PR_SENDER_NAME_W));
            if (from.empty()) from = propStr(findProp(cValues, props, PR_SENT_REPRESENTING_NAME_W));
            std::wstring fromEmail = propStr(findProp(cValues, props, PR_SENDER_EMAIL_ADDRESS_W));
            if (fromEmail.empty()) fromEmail = propStr(findProp(cValues, props, PR_SENT_REPRESENTING_EMAIL_W));
            std::wstring subject = propStr(findProp(cValues, props, PR_SUBJECT_W));
            std::wstring displayTo = propStr(findProp(cValues, props, PR_DISPLAY_TO_W));

            const SPropValue* pFlags = findProp(cValues, props, PR_MESSAGE_FLAGS);
            bool outgoing = pFlags && (pFlags->Value.l & MSGFLAG_FROMME);

            std::wstring headers = propStr(findProp(cValues, props, PR_TRANSPORT_MESSAGE_HEADERS_W));
            std::wstring computer, mailer;
            parseHeaders(headers, computer, mailer);

            std::wstring msgEntryHex;
            const SPropValue* pEid = findProp(cValues, props, PR_ENTRYID);
            if (pEid && PROP_TYPE(pEid->ulPropTag) == PT_BINARY) msgEntryHex = hexOf(pEid->Value.bin);

            FILETIME created{}, delivery{};
            const SPropValue* pCreated = findProp(cValues, props, PR_CREATION_TIME);
            const SPropValue* pDelivery = findProp(cValues, props, PR_MESSAGE_DELIVERY_TIME);
            if (pCreated && PROP_TYPE(pCreated->ulPropTag) == PT_SYSTIME) created = pCreated->Value.ft;
            if (pDelivery && PROP_TYPE(pDelivery->ulPropTag) == PT_SYSTIME) delivery = pDelivery->Value.ft;

            std::wstring toNames, toEmails;
            readRecipients(msg, toNames, toEmails);
            if (toNames.empty()) toNames = displayTo;

            std::wstring shared[9] = {
                from, toNames, subject, computer, mailer, fromEmail, toEmails,
                outgoing ? L"Outgoing" : L"Incoming",
                domainOf(outgoing ? toEmails : fromEmail),
            };

            api.FreeBuffer(props);
            readAttachments(msg, msgEntryHex, folderName, folderPath, shared, created, delivery);
        }

        void scanFolder(LPMDB store, LPMAPIFOLDER folder, const std::wstring& name, const std::wstring& path)
        {
            if (cancelled()) return;
            if (params.onFolder) params.onFolder(path);

            // Messages in this folder
            LPMAPITABLE contents = nullptr;
            if (SUCCEEDED(folder->GetContentsTable(MAPI_UNICODE, &contents)) && contents)
            {
                TagArray<1> tags{ 1, { PR_ENTRYID } };
                contents->SetColumns(tags.p(), 0);
                LPSRowSet rows = nullptr;
                while (!cancelled() && SUCCEEDED(contents->QueryRows(64, 0, &rows)) && rows && rows->cRows)
                {
                    for (ULONG r = 0; r < rows->cRows && !cancelled(); ++r)
                    {
                        const SPropValue* pEid = findProp(rows->aRow[r], PR_ENTRYID);
                        if (!pEid || PROP_TYPE(pEid->ulPropTag) != PT_BINARY) continue;
                        ULONG objType = 0;
                        LPUNKNOWN unk = nullptr;
                        if (SUCCEEDED(store->OpenEntry(pEid->Value.bin.cb, (LPENTRYID)pEid->Value.bin.lpb,
                                                       nullptr, 0, &objType, &unk)) && unk)
                        {
                            if (objType == MAPI_MESSAGE)
                                processMessage(reinterpret_cast<LPMESSAGE>(unk), name, path);
                            unk->Release();
                        }
                    }
                    freeRows(api, rows);
                    rows = nullptr;
                }
                freeRows(api, rows);
                contents->Release();
            }

            // Subfolders
            LPMAPITABLE hierarchy = nullptr;
            if (SUCCEEDED(folder->GetHierarchyTable(MAPI_UNICODE, &hierarchy)) && hierarchy)
            {
                TagArray<2> tags{ 2, { PR_ENTRYID, PR_DISPLAY_NAME_W } };
                hierarchy->SetColumns(tags.p(), 0);
                LPSRowSet rows = nullptr;
                while (!cancelled() && SUCCEEDED(hierarchy->QueryRows(64, 0, &rows)) && rows && rows->cRows)
                {
                    for (ULONG r = 0; r < rows->cRows && !cancelled(); ++r)
                    {
                        const SPropValue* pEid = findProp(rows->aRow[r], PR_ENTRYID);
                        if (!pEid || PROP_TYPE(pEid->ulPropTag) != PT_BINARY) continue;
                        std::wstring subName = propStr(findProp(rows->aRow[r], PR_DISPLAY_NAME_W));
                        ULONG objType = 0;
                        LPUNKNOWN unk = nullptr;
                        if (SUCCEEDED(store->OpenEntry(pEid->Value.bin.cb, (LPENTRYID)pEid->Value.bin.lpb,
                                                       nullptr, 0, &objType, &unk)) && unk)
                        {
                            if (objType == MAPI_FOLDER)
                                scanFolder(store, reinterpret_cast<LPMAPIFOLDER>(unk), subName, path + L"\\" + subName);
                            unk->Release();
                        }
                    }
                    freeRows(api, rows);
                    rows = nullptr;
                }
                freeRows(api, rows);
                hierarchy->Release();
            }
        }

        void scanStore(LPMDB store, const std::wstring& storeName)
        {
            TagArray<1> tags{ 1, { PR_IPM_SUBTREE_ENTRYID } };
            ULONG cValues = 0;
            LPSPropValue props = nullptr;
            if (FAILED(store->GetProps(tags.p(), MAPI_UNICODE, &cValues, &props)) || !props) return;
            const SPropValue* pEid = findProp(cValues, props, PR_IPM_SUBTREE_ENTRYID);
            if (pEid && PROP_TYPE(pEid->ulPropTag) == PT_BINARY)
            {
                ULONG objType = 0;
                LPUNKNOWN unk = nullptr;
                if (SUCCEEDED(store->OpenEntry(pEid->Value.bin.cb, (LPENTRYID)pEid->Value.bin.lpb,
                                               nullptr, 0, &objType, &unk)) && unk)
                {
                    if (objType == MAPI_FOLDER)
                        scanFolder(store, reinterpret_cast<LPMAPIFOLDER>(unk), storeName, storeName);
                    unk->Release();
                }
            }
            api.FreeBuffer(props);
        }
    };
}

bool MapiAvailable()
{
    MapiApi api;
    bool ok = api.load();
    if (api.dll) ::FreeLibrary(api.dll);
    return ok;
}

ScanResult ScanMailbox(const ScanParams& params)
{
    ScanResult result;
    MapiApi api;
    if (!api.load())
    {
        result.status = ScanStatus::NoMapi;
        result.message = L"Extended MAPI (mapi32.dll) is not available on this system. Microsoft Outlook must be installed.";
        return result;
    }

    MAPIINIT_0 init{ 0, 0 };   // MAPI_INIT_VERSION, no flags
    result.hr = api.Initialize(&init);
    if (FAILED(result.hr))
    {
        result.status = ScanStatus::Failed;
        result.message = L"MAPIInitialize failed. Microsoft Outlook may not be installed correctly.";
        if (api.dll) ::FreeLibrary(api.dll);
        return result;
    }

    LPMAPISESSION session = nullptr;
    // No MAPI_LOGON_UI, so with no profile this returns an error rather than prompting.
    ULONG flags = MAPI_EXTENDED | MAPI_NEW_SESSION | MAPI_NO_MAIL | MAPI_USE_DEFAULT | MAPI_UNICODE;
    result.hr = api.LogonEx(0, nullptr, nullptr, flags, &session);
    if (FAILED(result.hr) || !session)
    {
        result.status = ScanStatus::NoProfile;
        result.message = L"No Outlook profile was found. Open Outlook with a mail profile and run OutlookAttachView again.";
        api.Uninitialize();
        if (api.dll) ::FreeLibrary(api.dll);
        return result;
    }

    // Walk every message store in the profile.
    LPMAPITABLE stores = nullptr;
    if (SUCCEEDED(session->GetMsgStoresTable(0, &stores)) && stores)
    {
        TagArray<3> tags{ 3, { PR_ENTRYID, PR_DISPLAY_NAME_W, PR_DEFAULT_STORE } };
        stores->SetColumns(tags.p(), 0);
        LPSRowSet rows = nullptr;
        while (SUCCEEDED(stores->QueryRows(64, 0, &rows)) && rows && rows->cRows)
        {
            for (ULONG r = 0; r < rows->cRows; ++r)
            {
                const SPropValue* pEid = findProp(rows->aRow[r], PR_ENTRYID);
                if (!pEid || PROP_TYPE(pEid->ulPropTag) != PT_BINARY) continue;
                std::wstring storeName = propStr(findProp(rows->aRow[r], PR_DISPLAY_NAME_W));
                LPMDB store = nullptr;
                if (SUCCEEDED(session->OpenMsgStore(0, pEid->Value.bin.cb, (LPENTRYID)pEid->Value.bin.lpb,
                                                    nullptr, MDB_NO_DIALOG | MAPI_DEFERRED_ERRORS, &store)) && store)
                {
                    Walker w{ api, params, result.records, hexOf(pEid->Value.bin) };
                    w.scanStore(store, storeName);
                    store->Release();
                }
                if (params.cancelled && params.cancelled()) break;
            }
            freeRows(api, rows);
            rows = nullptr;
            if (params.cancelled && params.cancelled()) break;
        }
        freeRows(api, rows);
        stores->Release();
    }

    result.status = ScanStatus::Ok;
    session->Logoff(0, 0, 0);
    session->Release();
    api.Uninitialize();
    if (api.dll) ::FreeLibrary(api.dll);
    return result;
}

namespace
{
    std::vector<BYTE> hexToBin(const std::wstring& hex)
    {
        auto nib = [](wchar_t c) -> int {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            return 0;
        };
        std::vector<BYTE> out(hex.size() / 2);
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = (BYTE)((nib(hex[i * 2]) << 4) | nib(hex[i * 2 + 1]));
        return out;
    }

    // A logged-on session plus a cache of opened stores, shared by the save and
    // delete batch operations so the whole batch logs on once.
    struct Session
    {
        MapiApi api;
        LPMAPISESSION session = nullptr;
        bool ok = false;

        bool open()
        {
            if (!api.load()) return false;
            MAPIINIT_0 init{ 0, 0 };
            if (FAILED(api.Initialize(&init))) return false;
            ULONG flags = MAPI_EXTENDED | MAPI_NEW_SESSION | MAPI_NO_MAIL | MAPI_USE_DEFAULT | MAPI_UNICODE;
            ok = SUCCEEDED(api.LogonEx(0, nullptr, nullptr, flags, &session)) && session;
            return ok;
        }
        LPMDB openStore(const std::wstring& storeHex)
        {
            std::vector<BYTE> eid = hexToBin(storeHex);
            if (eid.empty()) return nullptr;
            LPMDB store = nullptr;
            session->OpenMsgStore(0, (ULONG)eid.size(), (LPENTRYID)eid.data(), nullptr,
                                  MDB_NO_DIALOG | MAPI_DEFERRED_ERRORS | MAPI_MODIFY, &store);
            return store;
        }
        LPMESSAGE openMessage(LPMDB store, const std::wstring& msgHex)
        {
            std::vector<BYTE> eid = hexToBin(msgHex);
            if (eid.empty()) return nullptr;
            ULONG objType = 0;
            LPUNKNOWN unk = nullptr;
            if (FAILED(store->OpenEntry((ULONG)eid.size(), (LPENTRYID)eid.data(), nullptr,
                                        MAPI_MODIFY, &objType, &unk)) || !unk)
                return nullptr;
            if (objType != MAPI_MESSAGE) { unk->Release(); return nullptr; }
            return reinterpret_cast<LPMESSAGE>(unk);
        }
        ~Session()
        {
            if (session) { session->Logoff(0, 0, 0); session->Release(); }
            if (ok || api.dll) api.Uninitialize();
            if (api.dll) ::FreeLibrary(api.dll);
        }
    };

    std::wstring uniquePath(const std::wstring& folder, const std::wstring& name)
    {
        std::wstring base = folder;
        if (!base.empty() && base.back() != L'\\') base += L'\\';
        return base + (name.empty() ? std::wstring(L"attachment") : name);
    }
}

AttachOpResult SaveAttachments(const std::vector<AttachRecord>& recs, const std::wstring& folder)
{
    AttachOpResult res;
    if (recs.empty()) { res.status = ScanStatus::Ok; return res; }
    Session s;
    if (!s.open())
    {
        res.status = ScanStatus::NoProfile;
        res.message = L"No Outlook session is available. Open Outlook with a mail profile and try again.";
        return res;
    }
    res.status = ScanStatus::Ok;
    for (const auto& rec : recs)
    {
        LPMDB store = s.openStore(rec.storeEntryId);
        if (!store) { ++res.failed; continue; }
        LPMESSAGE msg = s.openMessage(store, rec.msgEntryId);
        if (!msg) { ++res.failed; store->Release(); continue; }
        LPATTACH attach = nullptr;
        if (SUCCEEDED(msg->OpenAttach(rec.attachNum, nullptr, 0, &attach)) && attach)
        {
            TagArray<1> tags{ 1, { PR_ATTACH_DATA_BIN } };
            ULONG cValues = 0;
            LPSPropValue props = nullptr;
            if (SUCCEEDED(attach->GetProps(tags.p(), 0, &cValues, &props)) && props &&
                props[0].ulPropTag == PR_ATTACH_DATA_BIN)
            {
                std::wstring path = uniquePath(folder, rec.cells[0]);
                HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                         FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h != INVALID_HANDLE_VALUE)
                {
                    DWORD written = 0;
                    ::WriteFile(h, props[0].Value.bin.lpb, props[0].Value.bin.cb, &written, nullptr);
                    ::CloseHandle(h);
                    ++res.saved;
                }
                else ++res.failed;
            }
            else ++res.failed;
            if (props) s.api.FreeBuffer(props);
            attach->Release();
        }
        else ++res.failed;
        msg->Release();
        store->Release();
    }
    return res;
}

AttachOpResult DeleteAttachments(const std::vector<AttachRecord>& recs)
{
    AttachOpResult res;
    if (recs.empty()) { res.status = ScanStatus::Ok; return res; }
    Session s;
    if (!s.open())
    {
        res.status = ScanStatus::NoProfile;
        res.message = L"No Outlook session is available. Open Outlook with a mail profile and try again.";
        return res;
    }
    res.status = ScanStatus::Ok;
    for (const auto& rec : recs)
    {
        LPMDB store = s.openStore(rec.storeEntryId);
        if (!store) { ++res.failed; continue; }
        LPMESSAGE msg = s.openMessage(store, rec.msgEntryId);
        if (!msg) { ++res.failed; store->Release(); continue; }
        if (SUCCEEDED(msg->DeleteAttach(rec.attachNum, 0, nullptr, 0)) &&
            SUCCEEDED(msg->SaveChanges(0)))
            ++res.saved;
        else
            ++res.failed;
        msg->Release();
        store->Release();
    }
    return res;
}
