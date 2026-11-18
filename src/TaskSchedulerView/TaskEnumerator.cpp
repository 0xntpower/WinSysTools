// TaskEnumerator.cpp -- Task Scheduler 2.0 COM walk (LoadFrom = 1, the default).
//
// For each IRegisteredTask the 37 display columns are built here: the scalar
// fields come straight from the COM objects, the trigger columns from the
// ITriggerCollection, the action columns from the IActionCollection, the COM
// handler file/description from HKCR\CLSID\{clsid}, the running-user and owner
// names from the account SIDs, and the file columns from the task file under
// %windir%\System32\Tasks.
#include "TaskEnumerator.h"
#include "RegKey.h"
#include <atlbase.h>
#include <taskschd.h>
#include <sddl.h>
#include <aclapi.h>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "taskschd.lib")

namespace
{
    std::wstring bstr(BSTR b) { return (b && *b) ? std::wstring(b, SysStringLen(b)) : std::wstring(); }
    const wchar_t* yn(VARIANT_BOOL b) { return b == VARIANT_FALSE ? L"No" : L"Yes"; }

    // The original expands environment variables in the executable path and
    // arguments (the task XML stores them as %windir%\... etc.).
    std::wstring expandEnv(const std::wstring& s)
    {
        if (s.empty() || s.find(L'%') == std::wstring::npos) return s;
        DWORD need = ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
        if (!need) return s;
        std::wstring out(need, L'\0');
        ExpandEnvironmentStringsW(s.c_str(), out.data(), need);
        out.resize(need - 1);
        return out;
    }

    VARIANT idx(long i) { VARIANT v; VariantInit(&v); v.vt = VT_I4; v.lVal = i; return v; }

    std::wstring join(const std::vector<std::wstring>& parts, const wchar_t* sep)
    {
        std::wstring out;
        for (size_t i = 0; i < parts.size(); ++i) { if (i) out += sep; out += parts[i]; }
        return out;
    }

    std::wstring fmtSystemTime(const SYSTEMTIME& st)
    {
        if (st.wYear < 1900) return {};
        wchar_t d[128]{}, t[128]{};
        GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, d, 128);
        GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, t, 128);
        return std::wstring(d) + L" " + t;
    }

    // A COM DATE (local time) -> display text, plus a UTC FILETIME sort key.
    std::wstring fmtDate(DATE date, bool gmt, long long& key)
    {
        key = 0;
        if (date < 1.0) return {};
        SYSTEMTIME local{};
        if (!VariantTimeToSystemTime(date, &local) || local.wYear < 1900) return {};
        SYSTEMTIME utc{};
        if (TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc))
        {
            FILETIME ft{};
            if (SystemTimeToFileTime(&utc, &ft))
                key = (static_cast<long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        }
        return fmtSystemTime(gmt ? utc : local);
    }

    // A UTC FILETIME (file timestamp) -> display text, plus a sort key.
    std::wstring fmtFileTime(const FILETIME& ft, bool gmt, long long& key)
    {
        key = (static_cast<long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0) { key = 0; return {}; }
        SYSTEMTIME st{};
        if (gmt)
        {
            if (!FileTimeToSystemTime(&ft, &st)) return {};
        }
        else
        {
            SYSTEMTIME utc{}, local{};
            if (!FileTimeToSystemTime(&ft, &utc)) return {};
            if (SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) st = local;
            else st = utc;
        }
        return fmtSystemTime(st);
    }

    // Resolve a SID string or plain account string to a display name.
    // When toFull is false only the account name is used (Running User Account),
    // when true the "domain\\name" form is used (Task Owner).
    std::wstring sidString(const std::wstring& value, bool toFull)
    {
        if (value.empty()) return {};
        PSID sid = nullptr;
        if (!ConvertStringSidToSidW(value.c_str(), &sid))
            return value;  // already a plain account name
        std::wstring result = value;
        wchar_t name[256]{}, dom[256]{};
        DWORD cn = 256, cd = 256;
        SID_NAME_USE use{};
        if (LookupAccountSidW(nullptr, sid, name, &cn, dom, &cd, &use))
            result = toFull && dom[0] ? (std::wstring(dom) + L"\\" + name) : std::wstring(name);
        LocalFree(sid);
        return result;
    }

    std::wstring ownerOfFile(const std::wstring& path)
    {
        PSID owner = nullptr;
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                  &owner, nullptr, nullptr, nullptr, &sd) != ERROR_SUCCESS)
            return {};
        std::wstring result;
        wchar_t name[256]{}, dom[256]{};
        DWORD cn = 256, cd = 256;
        SID_NAME_USE use{};
        if (owner && LookupAccountSidW(nullptr, owner, name, &cn, dom, &cd, &use))
            result = dom[0] ? (std::wstring(dom) + L"\\" + name) : std::wstring(name);
        if (sd) LocalFree(sd);
        return result;
    }

    const wchar_t* triggerName(int type)
    {
        switch (type)
        {
        case TASK_TRIGGER_EVENT:                return L"Event";
        case TASK_TRIGGER_TIME:                 return L"Specific Time";
        case TASK_TRIGGER_DAILY:                return L"Daily";
        case TASK_TRIGGER_WEEKLY:               return L"Weekly";
        case TASK_TRIGGER_MONTHLY:              return L"Monthly";
        case TASK_TRIGGER_MONTHLYDOW:           return L"Monthly Day-Of-Week";
        case TASK_TRIGGER_IDLE:                 return L"Idle";
        case TASK_TRIGGER_REGISTRATION:         return L"Task Registration";
        case TASK_TRIGGER_BOOT:                 return L"Boot";
        case TASK_TRIGGER_LOGON:                return L"Logon";
        case TASK_TRIGGER_SESSION_STATE_CHANGE: return L"Session State Change";
        default:                                return L"";
        }
    }

    const wchar_t* actionName(int type)
    {
        switch (type)
        {
        case TASK_ACTION_EXEC:        return L"Run EXE";
        case TASK_ACTION_COM_HANDLER: return L"COM Handler";
        case TASK_ACTION_SEND_EMAIL:  return L"Send Email";
        case TASK_ACTION_SHOW_MESSAGE:return L"Show Message";
        default:                      return L"";
        }
    }

    const wchar_t* multiInstanceName(int policy)
    {
        switch (policy)
        {
        case TASK_INSTANCES_PARALLEL:      return L"Parallel";
        case TASK_INSTANCES_QUEUE:         return L"Queue";
        case TASK_INSTANCES_IGNORE_NEW:    return L"Ignore New";
        case TASK_INSTANCES_STOP_EXISTING: return L"Stop Existing";
        default:                           return L"";
        }
    }

    const wchar_t* stateName(int state)
    {
        switch (state)
        {
        case TASK_STATE_DISABLED: return L"Disabled";
        case TASK_STATE_QUEUED:   return L"Queued";
        case TASK_STATE_READY:    return L"Ready";
        case TASK_STATE_RUNNING:  return L"Running";
        default:                  return L"Unknown";
        }
    }

    std::wstring weekDays(short daysOfWeek)
    {
        static const wchar_t* k[] = { L"Sunday", L"Monday", L"Tuesday", L"Wednesday",
                                      L"Thursday", L"Friday", L"Saturday" };
        std::vector<std::wstring> on;
        for (int i = 0; i < 7; ++i)
            if (daysOfWeek & (1 << i)) on.push_back(k[i]);
        return join(on, L", ");
    }

    // The inner query of an event trigger's subscription XML (the <Select> body),
    // which is exactly what the original shows in the "Run On Event" column.
    std::wstring eventQuery(const std::wstring& subscription)
    {
        std::vector<std::wstring> selects;
        size_t pos = 0;
        while ((pos = subscription.find(L"<Select", pos)) != std::wstring::npos)
        {
            size_t gt = subscription.find(L'>', pos);
            if (gt == std::wstring::npos) break;
            size_t end = subscription.find(L"</Select>", gt);
            if (end == std::wstring::npos) break;
            selects.push_back(subscription.substr(gt + 1, end - gt - 1));
            pos = end + 9;
        }
        return selects.empty() ? subscription : join(selects, L" ");
    }

    // HKCR\CLSID\{clsid}: (Default) -> description, InprocServer32\(Default) -> filename.
    void resolveComHandler(const std::wstring& clsid, std::wstring& filename, std::wstring& description)
    {
        if (clsid.empty()) return;
        RegKey k;
        if (k.open(HKEY_CLASSES_ROOT, L"CLSID\\" + clsid))
        {
            if (auto d = k.queryString(L"")) description = *d;
        }
        RegKey srv;
        if (srv.open(HKEY_CLASSES_ROOT, L"CLSID\\" + clsid + L"\\InprocServer32"))
        {
            if (auto f = srv.queryString(L""))
            {
                filename = *f;
                if (!filename.empty() && filename.find(L'\\') == std::wstring::npos)
                {
                    wchar_t sys[MAX_PATH]{};
                    if (GetSystemDirectoryW(sys, MAX_PATH))
                        filename = std::wstring(sys) + L"\\" + filename;
                }
            }
        }
    }

    std::wstring taskFilePath(const std::wstring& path)
    {
        wchar_t win[MAX_PATH]{};
        if (!GetWindowsDirectoryW(win, MAX_PATH)) return {};
        return std::wstring(win) + L"\\system32\\Tasks" + path;
    }

    void readFileTimes(const std::wstring& file, bool /*gmt*/, TaskRecord& r)
    {
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW(file.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        FindClose(h);
        // The original shows the task file's created/modified timestamps in UTC
        // (the raw FILETIME), with no local-time conversion.
        r.cells[34] = fmtFileTime(fd.ftCreationTime, true, r.fileCreatedKey);
        r.cells[35] = fmtFileTime(fd.ftLastWriteTime, true, r.fileModifiedKey);
    }

    void buildRecord(IRegisteredTask* rt, const std::wstring& folderPath,
                     const DisplayOptions& opt, TaskRecord& r)
    {
        CComBSTR b;
        if (SUCCEEDED(rt->get_Name(&b))) r.cells[0] = bstr(b);
        r.cells[7] = folderPath;                      // Task Folder
        r.taskPath = (folderPath == L"\\") ? (L"\\" + r.cells[0]) : (folderPath + L"\\" + r.cells[0]);

        TASK_STATE state = TASK_STATE_UNKNOWN;
        rt->get_State(&state);
        r.cells[1] = stateName(state);

        DATE date = 0;
        if (SUCCEEDED(rt->get_LastRunTime(&date))) r.cells[4] = fmtDate(date, opt.showTimeInGmt, r.lastRunKey);
        date = 0;
        if (SUCCEEDED(rt->get_NextRunTime(&date))) r.cells[5] = fmtDate(date, opt.showTimeInGmt, r.nextRunKey);

        LONG v = 0;
        if (SUCCEEDED(rt->get_LastTaskResult(&v))) { r.lastResult = static_cast<unsigned int>(v); r.cells[3] = std::to_wstring(r.lastResult); }
        v = 0;
        if (SUCCEEDED(rt->get_NumberOfMissedRuns(&v))) { r.missedRuns = v; r.cells[6] = std::to_wstring(v); }

        LONG running = 0;
        CComPtr<IRunningTaskCollection> runColl;
        if (SUCCEEDED(rt->GetInstances(0, &runColl)) && runColl) runColl->get_Count(&running);
        r.runningInstances = running;
        r.cells[26] = std::to_wstring(running);

        // Task Filename and the file timestamps.
        std::wstring file = taskFilePath(folderPath == L"\\" ? (L"\\" + r.cells[0]) : (folderPath + L"\\" + r.cells[0]));
        r.cells[36] = file;
        r.cells[24] = ownerOfFile(file);              // Task Owner
        readFileTimes(file, opt.showTimeInGmt, r);

        CComPtr<ITaskDefinition> def;
        if (FAILED(rt->get_Definition(&def)) || !def) return;

        CComPtr<IRegistrationInfo> ri;
        if (SUCCEEDED(def->get_RegistrationInfo(&ri)) && ri)
        {
            b.Empty(); if (SUCCEEDED(ri->get_Author(&b)))      r.cells[10] = bstr(b);
            b.Empty(); if (SUCCEEDED(ri->get_Description(&b)))  r.cells[11] = bstr(b);
            b.Empty(); if (SUCCEEDED(ri->get_Source(&b)))       r.cells[12] = bstr(b);
        }

        CComPtr<ITaskSettings> set;
        if (SUCCEEDED(def->get_Settings(&set)) && set)
        {
            VARIANT_BOOL vb = VARIANT_FALSE;
            if (SUCCEEDED(set->get_Hidden(&vb)))                   r.cells[2]  = yn(vb);
            vb = VARIANT_FALSE;
            if (SUCCEEDED(set->get_AllowDemandStart(&vb)))         r.cells[30] = yn(vb);
            vb = VARIANT_FALSE;
            if (SUCCEEDED(set->get_RunOnlyIfNetworkAvailable(&vb)))r.cells[31] = yn(vb);
            vb = VARIANT_FALSE;
            if (SUCCEEDED(set->get_WakeToRun(&vb)))                r.cells[33] = yn(vb);
            TASK_INSTANCES_POLICY pol = TASK_INSTANCES_IGNORE_NEW;
            if (SUCCEEDED(set->get_MultipleInstances(&pol)))       r.cells[32] = multiInstanceName(pol);
            int prio = 7;
            if (SUCCEEDED(set->get_Priority(&prio)))               { r.priority = prio; r.cells[25] = std::to_wstring(prio); }
        }

        CComPtr<IPrincipal> pr;
        if (SUCCEEDED(def->get_Principal(&pr)) && pr)
        {
            b.Empty();
            std::wstring account;
            if (SUCCEEDED(pr->get_UserId(&b))) account = bstr(b);
            if (account.empty()) { b.Empty(); if (SUCCEEDED(pr->get_GroupId(&b))) account = bstr(b); }
            r.cells[27] = sidString(account, false);
            TASK_RUNLEVEL_TYPE level = TASK_RUNLEVEL_LUA;
            if (SUCCEEDED(pr->get_RunLevel(&level))) r.cells[28] = (level == TASK_RUNLEVEL_HIGHEST) ? L"Yes" : L"No";
        }

        // Triggers: the type list plus the derived per-type columns.
        CComPtr<ITriggerCollection> trigs;
        std::wstring onBoot = L"No", onLogon = L"No", onEvent = L"No",
                     daily = L"No", weekly = L"No", monthly = L"No";
        std::vector<int> trigTypes;
        if (SUCCEEDED(def->get_Triggers(&trigs)) && trigs)
        {
            LONG tc = 0; trigs->get_Count(&tc);
            for (LONG i = 1; i <= tc; ++i)
            {
                CComPtr<ITrigger> tr;
                if (FAILED(trigs->get_Item(i, &tr)) || !tr) continue;
                TASK_TRIGGER_TYPE2 type = TASK_TRIGGER_EVENT;
                tr->get_Type(&type);
                trigTypes.push_back(type);
                switch (type)
                {
                case TASK_TRIGGER_BOOT:  onBoot = L"Yes"; break;
                case TASK_TRIGGER_LOGON: onLogon = L"Yes"; break;
                case TASK_TRIGGER_EVENT:
                    if (CComQIPtr<IEventTrigger> ev = tr)
                    { CComBSTR s; if (SUCCEEDED(ev->get_Subscription(&s))) onEvent = eventQuery(bstr(s)); }
                    break;
                case TASK_TRIGGER_DAILY:
                    if (daily == L"No")
                        if (CComQIPtr<IDailyTrigger> d = tr)
                        { short n = 1; d->get_DaysInterval(&n); daily = L"Every " + std::to_wstring(n) + L" day(s)"; }
                    break;
                case TASK_TRIGGER_WEEKLY:
                    if (weekly == L"No")
                        if (CComQIPtr<IWeeklyTrigger> w = tr)
                        {
                            short wi = 1, dw = 0; w->get_WeeksInterval(&wi); w->get_DaysOfWeek(&dw);
                            weekly = L"Every " + std::to_wstring(wi) + L" week(s) - On " + weekDays(dw);
                        }
                    break;
                case TASK_TRIGGER_MONTHLY:
                case TASK_TRIGGER_MONTHLYDOW:
                    if (monthly == L"No") monthly = L"Yes";
                    break;
                default: break;
                }
            }
        }
        // The Triggers column lists the distinct trigger types, sorted by type
        // value ascending (the original collapses repeats and orders this way).
        std::sort(trigTypes.begin(), trigTypes.end());
        trigTypes.erase(std::unique(trigTypes.begin(), trigTypes.end()), trigTypes.end());
        std::vector<std::wstring> trigNames;
        for (int t : trigTypes) { std::wstring nm = triggerName(t); if (!nm.empty()) trigNames.push_back(nm); }
        r.cells[23] = join(trigNames, L", ");
        r.cells[13] = onBoot; r.cells[14] = onLogon; r.cells[22] = onEvent;
        r.cells[15] = daily;  r.cells[16] = weekly;  r.cells[17] = monthly;

        // Actions: the type list plus the exec / COM handler columns.
        CComPtr<IActionCollection> acts;
        std::vector<std::wstring> actNames;
        bool haveExec = false, haveCom = false;
        if (SUCCEEDED(def->get_Actions(&acts)) && acts)
        {
            LONG ac = 0; acts->get_Count(&ac);
            for (LONG i = 1; i <= ac; ++i)
            {
                CComPtr<IAction> a;
                if (FAILED(acts->get_Item(i, &a)) || !a) continue;
                TASK_ACTION_TYPE type = TASK_ACTION_EXEC;
                a->get_Type(&type);
                std::wstring an = actionName(type);  // distinct action types only
                if (std::find(actNames.begin(), actNames.end(), an) == actNames.end())
                    actNames.push_back(an);
                if (type == TASK_ACTION_EXEC && !haveExec)
                {
                    if (CComQIPtr<IExecAction> ex = a)
                    {
                        haveExec = true;
                        CComBSTR s;
                        if (SUCCEEDED(ex->get_Path(&s)))             r.cells[8]  = expandEnv(bstr(s));
                        s.Empty(); if (SUCCEEDED(ex->get_Arguments(&s)))        r.cells[9]  = bstr(s);
                        s.Empty(); if (SUCCEEDED(ex->get_WorkingDirectory(&s))) r.cells[29] = bstr(s);
                    }
                }
                else if (type == TASK_ACTION_COM_HANDLER && !haveCom)
                {
                    if (CComQIPtr<IComHandlerAction> co = a)
                    {
                        haveCom = true;
                        CComBSTR s;
                        if (SUCCEEDED(co->get_ClassId(&s))) r.cells[20] = bstr(s);
                        std::wstring fn, desc;
                        resolveComHandler(r.cells[20], fn, desc);
                        r.cells[18] = fn; r.cells[19] = desc;
                    }
                }
            }
        }
        r.cells[21] = join(actNames, L", ");
    }

    void walkFolder(ITaskFolder* folder, const DisplayOptions& opt, std::vector<TaskRecord>& out)
    {
        CComBSTR pathB;
        std::wstring folderPath = L"\\";
        if (SUCCEEDED(folder->get_Path(&pathB))) folderPath = bstr(pathB);

        CComPtr<IRegisteredTaskCollection> tasks;
        if (SUCCEEDED(folder->GetTasks(TASK_ENUM_HIDDEN, &tasks)) && tasks)
        {
            LONG n = 0; tasks->get_Count(&n);
            for (LONG i = 1; i <= n; ++i)
            {
                CComPtr<IRegisteredTask> rt;
                if (SUCCEEDED(tasks->get_Item(idx(i), &rt)) && rt)
                {
                    TaskRecord r;
                    buildRecord(rt, folderPath, opt, r);
                    out.push_back(std::move(r));
                }
            }
        }

        CComPtr<ITaskFolderCollection> subs;
        if (SUCCEEDED(folder->GetFolders(0, &subs)) && subs)
        {
            LONG n = 0; subs->get_Count(&n);
            for (LONG i = 1; i <= n; ++i)
            {
                CComPtr<ITaskFolder> sub;
                if (SUCCEEDED(subs->get_Item(idx(i), &sub)) && sub)
                    walkFolder(sub, opt, out);
            }
        }
    }
}

std::vector<TaskRecord> EnumerateTasks(const DisplayOptions& opt,
                                       const std::wstring& computerName,
                                       HRESULT* hrOut)
{
    std::vector<TaskRecord> items;
    auto setHr = [&](HRESULT hr) { if (hrOut) *hrOut = hr; };

    CComPtr<ITaskService> svc;
    HRESULT hr = svc.CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER);
    if (FAILED(hr) || !svc) { setHr(hr); return items; }

    VARIANT server, user, domain, pwd;
    VariantInit(&server); VariantInit(&user); VariantInit(&domain); VariantInit(&pwd);
    if (!computerName.empty()) { server.vt = VT_BSTR; server.bstrVal = SysAllocString(computerName.c_str()); }
    hr = svc->Connect(server, user, domain, pwd);
    VariantClear(&server);
    if (FAILED(hr)) { setHr(hr); return items; }

    CComPtr<ITaskFolder> root;
    hr = svc->GetFolder(CComBSTR(L"\\"), &root);
    if (FAILED(hr) || !root) { setHr(hr); return items; }

    walkFolder(root, opt, items);
    setHr(S_OK);
    return items;
}

std::wstring TaskOperation(TaskOp op, const std::wstring& taskPath,
                           const std::wstring& computerName, bool* ok)
{
    if (ok) *ok = false;

    CComPtr<ITaskService> svc;
    if (FAILED(svc.CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER)) || !svc)
        return {};

    VARIANT server, user, domain, pwd;
    VariantInit(&server); VariantInit(&user); VariantInit(&domain); VariantInit(&pwd);
    if (!computerName.empty()) { server.vt = VT_BSTR; server.bstrVal = SysAllocString(computerName.c_str()); }
    HRESULT hr = svc->Connect(server, user, domain, pwd);
    VariantClear(&server);
    if (FAILED(hr)) return {};

    CComPtr<ITaskFolder> root;
    if (FAILED(svc->GetFolder(CComBSTR(L"\\"), &root)) || !root) return {};

    CComPtr<IRegisteredTask> rt;
    if (FAILED(root->GetTask(CComBSTR(taskPath.c_str()), &rt)) || !rt) return {};

    switch (op)
    {
    case TaskOp::Enable:  if (SUCCEEDED(rt->put_Enabled(VARIANT_TRUE)))  { if (ok) *ok = true; } break;
    case TaskOp::Disable: if (SUCCEEDED(rt->put_Enabled(VARIANT_FALSE))) { if (ok) *ok = true; } break;
    case TaskOp::Run:
    {
        VARIANT e; VariantInit(&e);
        CComPtr<IRunningTask> running;
        if (SUCCEEDED(rt->Run(e, &running))) { if (ok) *ok = true; }
        break;
    }
    case TaskOp::Stop:  if (SUCCEEDED(rt->Stop(0))) { if (ok) *ok = true; } break;
    case TaskOp::GetXml:
    {
        CComPtr<ITaskDefinition> def;
        if (SUCCEEDED(rt->get_Definition(&def)) && def)
        {
            CComBSTR xml;
            if (SUCCEEDED(def->get_XmlText(&xml))) { if (ok) *ok = true; return bstr(xml); }
        }
        break;
    }
    }
    return {};
}
