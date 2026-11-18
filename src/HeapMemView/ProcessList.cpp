// ProcessList.cpp -- running-process enumeration (sub_14000D318).
#include "ProcessList.h"
#include <tlhelp32.h>

std::vector<ProcessInfo> EnumerateProcesses()
{
    std::vector<ProcessInfo> list;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return list;

    PROCESSENTRY32W pe{ sizeof(pe) };
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            ProcessInfo pi;
            pi.pid = pe.th32ProcessID;
            pi.name = pe.szExeFile;

            HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pi.pid);
            if (proc)
            {
                wchar_t path[MAX_PATH];
                DWORD cch = MAX_PATH;
                if (QueryFullProcessImageNameW(proc, 0, path, &cch))
                    pi.path.assign(path, cch);
                CloseHandle(proc);
            }
            list.push_back(std::move(pi));
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return list;
}
