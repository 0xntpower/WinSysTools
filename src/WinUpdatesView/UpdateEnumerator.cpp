// UpdateEnumerator.cpp -- WUA COM history reader (LoadFrom = 1, the default).
#include "UpdateEnumerator.h"
#include <atlbase.h>
#include <wuapi.h>

namespace
{
    std::wstring bstrToStr(BSTR b)
    {
        return (b && *b) ? std::wstring(b, SysStringLen(b)) : std::wstring();
    }

    // Build "http://support.microsoft.com/kb/<digits>" from the first "KB"
    // token in the title, exactly like the original (sub_140015348 tail).
    std::wstring kbInformationUrl(const std::wstring& title)
    {
        if (title.size() < 4) return {};
        for (size_t i = 0; i + 4 <= title.size(); ++i)
        {
            if ((title[i] == L'K' || title[i] == L'k') &&
                (title[i + 1] == L'B' || title[i + 1] == L'b'))
            {
                size_t j = i + 2;
                std::wstring digits;
                while (j < title.size() && title[j] >= L'0' && title[j] <= L'9')
                    digits += title[j++];
                if (!digits.empty())
                    return L"http://support.microsoft.com/kb/" + digits;
                return {};
            }
        }
        return {};
    }
}

std::vector<UpdateRecord> EnumerateUpdateHistory(HRESULT* hrOut)
{
    std::vector<UpdateRecord> items;
    auto setHr = [&](HRESULT hr) { if (hrOut) *hrOut = hr; };

    CComPtr<IUpdateSearcher> searcher;
    HRESULT hr = searcher.CoCreateInstance(CLSID_UpdateSearcher, nullptr,
                                           CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER);
    if (FAILED(hr) || !searcher) { setHr(hr); return items; }

    LONG total = 0;
    hr = searcher->GetTotalHistoryCount(&total);
    if (FAILED(hr) || total <= 0) { setHr(hr); return items; }

    CComPtr<IUpdateHistoryEntryCollection> history;
    hr = searcher->QueryHistory(0, total, &history);
    if (FAILED(hr) || !history) { setHr(hr); return items; }

    LONG count = 0;
    history->get_Count(&count);
    items.reserve(count);

    for (LONG i = 0; i < count; ++i)
    {
        CComPtr<IUpdateHistoryEntry> e;
        if (FAILED(history->get_Item(i, &e)) || !e) continue;

        UpdateRecord r;

        UpdateOperation op = uoInstallation;
        if (SUCCEEDED(e->get_Operation(&op))) r.operation = static_cast<int>(op);
        OperationResultCode rc = orcNotStarted;
        if (SUCCEEDED(e->get_ResultCode(&rc))) r.resultCode = static_cast<int>(rc);
        LONG v = 0;
        if (SUCCEEDED(e->get_HResult(&v))) r.hResult = v;
        if (SUCCEEDED(e->get_UnmappedResultCode(&v))) r.unmappedResultCode = v;
        ServerSelection ss = ssDefault;
        if (SUCCEEDED(e->get_ServerSelection(&ss))) r.serverSelection = static_cast<int>(ss);

        CComBSTR b;
        if (SUCCEEDED(e->get_Title(&b)))               r.title = bstrToStr(b);
        b.Empty();
        if (SUCCEEDED(e->get_Description(&b)))         r.description = bstrToStr(b);
        b.Empty();
        if (SUCCEEDED(e->get_SupportUrl(&b)))          r.supportUrl = bstrToStr(b);
        b.Empty();
        if (SUCCEEDED(e->get_UninstallationNotes(&b))) r.uninstallNotes = bstrToStr(b);
        b.Empty();
        if (SUCCEEDED(e->get_ClientApplicationID(&b))) r.clientAppId = bstrToStr(b);
        b.Empty();
        if (SUCCEEDED(e->get_ServiceID(&b)))           r.serviceId = bstrToStr(b);

        // Date (DATE in UTC) -> UTC FILETIME, as the original stores it.
        DATE d = 0.0;
        if (SUCCEEDED(e->get_Date(&d)))
        {
            SYSTEMTIME st{};
            if (VariantTimeToSystemTime(d, &st))
                SystemTimeToFileTime(&st, &r.installDate);
        }

        // UpdateIdentity -> UpdateID + RevisionNumber
        CComPtr<IUpdateIdentity> ident;
        if (SUCCEEDED(e->get_UpdateIdentity(&ident)) && ident)
        {
            CComBSTR id;
            if (SUCCEEDED(ident->get_UpdateID(&id))) r.updateId = bstrToStr(id);
            LONG rev = 0;
            if (SUCCEEDED(ident->get_RevisionNumber(&rev))) r.revisionNumber = rev;
        }

        // Categories[0].Name -- the original Queries the entry for
        // IUpdateHistoryEntry2 (same IID found in the binary) and reads it there.
        CComQIPtr<IUpdateHistoryEntry2> e2(e);
        if (e2)
        {
            CComPtr<ICategoryCollection> cats;
            if (SUCCEEDED(e2->get_Categories(&cats)) && cats)
            {
                LONG cn = 0;
                cats->get_Count(&cn);
                if (cn > 0)
                {
                    CComPtr<ICategory> cat;
                    if (SUCCEEDED(cats->get_Item(0, &cat)) && cat)
                    {
                        CComBSTR name;
                        if (SUCCEEDED(cat->get_Name(&name))) r.category = bstrToStr(name);
                    }
                }
            }
        }

        r.informationUrl = kbInformationUrl(r.title);
        items.push_back(std::move(r));
    }

    setHr(S_OK);
    return items;
}
