// MimeEnumerator.cpp -- reconstructs the registry walk that populates the list.
// Mirrors sub_40391E (enumerate HKCR\MIME\Database\Content Type), sub_40150B
// (enrich from HKCR\CLSID\<clsid>) and sub_401614 (enrich from the extension's
// ProgID key), written as straight-line C++ instead of offset arithmetic.
#include "MimeType.h"
#include "RegKey.h"

namespace
{
    constexpr DWORD FTA_OpenIsSafe = 0x00010000; // EditFlags bit the original tests

    // sub_40150B: HKCR\CLSID\<clsid> -> name / version / progid / server dll.
    void enrichFromClsid(MimeType& m)
    {
        if (m.clsid.empty()) return;
        RegKey clsid;
        if (!clsid.open(HKEY_CLASSES_ROOT, L"CLSID\\" + m.clsid)) return;

        if (auto v = clsid.queryString(L"")) m.pluginName = *v;
        if (auto v = clsid.queryString(L"Version")) m.pluginVersion = *v;
        if (auto v = clsid.queryString(L"ProgID")) m.pluginProgId = *v;

        RegKey server;
        if (server.open(HKEY_CLASSES_ROOT, L"CLSID\\" + m.clsid + L"\\InProcServer32"))
            if (auto v = server.queryString(L"")) m.pluginFile = *v;

        // Version and ProgID also appear as subkeys whose default value holds the text.
        if (m.pluginVersion.empty())
        {
            RegKey ver;
            if (ver.open(HKEY_CLASSES_ROOT, L"CLSID\\" + m.clsid + L"\\Version"))
                if (auto v = ver.queryString(L"")) m.pluginVersion = *v;
        }
        if (m.pluginProgId.empty())
        {
            RegKey pid;
            if (pid.open(HKEY_CLASSES_ROOT, L"CLSID\\" + m.clsid + L"\\ProgID"))
                if (auto v = pid.queryString(L"")) m.pluginProgId = *v;
        }
    }

    // sub_401614: extension -> its default ProgID -> HKCR\<progid> for EditFlags,
    // shell\open\command and DefaultIcon.
    void enrichFromExtension(MimeType& m)
    {
        m.fileDialog = true;
        if (m.extension.empty()) return;

        RegKey ext;
        if (!ext.open(HKEY_CLASSES_ROOT, m.extension)) return;
        std::wstring progId;
        if (auto v = ext.queryString(L"")) progId = *v;
        if (progId.empty()) return;

        RegKey prog;
        if (!prog.open(HKEY_CLASSES_ROOT, progId)) return;

        if (auto flags = prog.queryDword(L"EditFlags"))
            if (*flags & FTA_OpenIsSafe) m.fileDialog = false;

        RegKey cmd;
        if (cmd.open(HKEY_CLASSES_ROOT, progId + L"\\shell\\open\\command"))
            if (auto v = cmd.queryString(L"")) m.openCommand = *v;

        RegKey icon;
        if (icon.open(HKEY_CLASSES_ROOT, progId + L"\\DefaultIcon"))
            if (auto v = icon.queryString(L"")) m.defaultIcon = *v;
    }
}

std::vector<MimeType> EnumerateMimeTypes()
{
    std::vector<MimeType> result;

    RegKey db;
    if (!db.open(HKEY_CLASSES_ROOT, L"MIME\\Database\\Content Type"))
        return result;

    for (const auto& name : db.subKeys())
    {
        RegKey type;
        if (!type.open(HKEY_CLASSES_ROOT, L"MIME\\Database\\Content Type\\" + name))
            continue;

        MimeType m;
        m.contentType = name;
        if (auto v = type.queryString(L"Extension")) m.extension = *v;
        if (auto v = type.queryString(L"CLSID")) m.clsid = *v;

        enrichFromClsid(m);
        enrichFromExtension(m);

        result.push_back(std::move(m));
    }

    return result;
}
