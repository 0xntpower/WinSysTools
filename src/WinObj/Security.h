// Security.h -- the Security property page for an object's Properties sheet.
//
// The original adds a standard Security page through ACLUI's CreateSecurityPage,
// driven by an ISecurityInformation implementation. This provides the same, in
// read-only mode: the page reads the object's security descriptor through
// NtQuerySecurityObject over a handle opened for the object's type, and ACLUI
// renders the owner, group and DACL exactly as the Explorer Security tab does.
#pragma once

#include <windows.h>
#include <aclui.h>
#include <string>
#include "NtObjMgr.h"

#pragma comment(lib, "aclui.lib")

class CObjectSecurity : public ISecurityInformation
{
  public:
    CObjectSecurity(std::wstring path, std::wstring type)
        : m_refs(1)
        , m_path(std::move(path))
        , m_type(std::move(type))
    {
        m_name = m_path;
        size_t slash = m_name.rfind(L'\\');
        if (slash != std::wstring::npos)
        {
            m_name = m_name.substr(slash + 1);
        }
        m_handle = nt::OpenForQuery(m_path, m_type, READ_CONTROL);
    }

    ~CObjectSecurity()
    {
        if (m_handle)
        {
            ::CloseHandle(m_handle);
        }
    }

    bool HasHandle() const
    {
        return m_handle != nullptr;
    }

    // ---- IUnknown --------------------------------------------------------
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
        {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_ISecurityInformation)
        {
            *ppv = static_cast<ISecurityInformation*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override
    {
        return ::InterlockedIncrement(&m_refs);
    }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        LONG n = ::InterlockedDecrement(&m_refs);
        if (n == 0)
        {
            delete this;
        }
        return n;
    }

    // ---- ISecurityInformation -------------------------------------------
    IFACEMETHODIMP GetObjectInformation(PSI_OBJECT_INFO pInfo) override
    {
        pInfo->dwFlags = SI_READONLY | SI_ADVANCED | SI_EDIT_ALL | SI_NO_ADDITIONAL_PERMISSION;
        pInfo->hInstance = nullptr;
        pInfo->pszServerName = nullptr;
        pInfo->pszObjectName = const_cast<LPWSTR>(m_name.c_str());
        pInfo->pszPageTitle = nullptr;
        pInfo->guidObjectType = GUID_NULL;
        return S_OK;
    }

    IFACEMETHODIMP GetSecurity(SECURITY_INFORMATION si, PSECURITY_DESCRIPTOR* ppSD, BOOL /*fDefault*/) override
    {
        if (!ppSD)
        {
            return E_POINTER;
        }
        PSECURITY_DESCRIPTOR sd = nt::QuerySecurity(m_handle, si);
        if (!sd)
        {
            return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
        }
        *ppSD = sd; // ACLUI frees this with LocalFree
        return S_OK;
    }

    IFACEMETHODIMP SetSecurity(SECURITY_INFORMATION si, PSECURITY_DESCRIPTOR sd) override
    {
        return nt::ApplySecurity(m_handle, si, sd) ? S_OK : HRESULT_FROM_WIN32(::GetLastError());
    }

    IFACEMETHODIMP GetAccessRights(const GUID* /*type*/, DWORD /*flags*/, PSI_ACCESS* ppAccess, ULONG* pcAccess,
                                   ULONG* piDefault) override
    {
        static SI_ACCESS accesses[] = {
            {&GUID_NULL, GENERIC_ALL, L"Full Control", SI_ACCESS_GENERAL | SI_ACCESS_SPECIFIC},
            {&GUID_NULL, GENERIC_READ, L"Read", SI_ACCESS_GENERAL},
            {&GUID_NULL, GENERIC_WRITE, L"Write", SI_ACCESS_GENERAL},
            {&GUID_NULL, GENERIC_EXECUTE, L"Execute", SI_ACCESS_GENERAL},
        };
        *ppAccess = accesses;
        *pcAccess = ARRAYSIZE(accesses);
        *piDefault = 0;
        return S_OK;
    }

    IFACEMETHODIMP MapGeneric(const GUID* /*type*/, UCHAR* /*pAceFlags*/, ACCESS_MASK* pMask) override
    {
        GENERIC_MAPPING map{};
        map.GenericRead = STANDARD_RIGHTS_READ | 0x0001;
        map.GenericWrite = STANDARD_RIGHTS_WRITE | 0x0002;
        map.GenericExecute = STANDARD_RIGHTS_EXECUTE;
        map.GenericAll = STANDARD_RIGHTS_ALL | 0xFFFF;
        ::MapGenericMask(pMask, &map);
        return S_OK;
    }

    IFACEMETHODIMP GetInheritTypes(PSI_INHERIT_TYPE* ppInheritTypes, ULONG* pcInheritTypes) override
    {
        *ppInheritTypes = nullptr;
        *pcInheritTypes = 0;
        return S_OK;
    }

    IFACEMETHODIMP PropertySheetPageCallback(HWND /*hwnd*/, UINT /*uMsg*/, SI_PAGE_TYPE /*uPage*/) override
    {
        return S_OK;
    }

  private:
    LONG m_refs;
    std::wstring m_path;
    std::wstring m_type;
    std::wstring m_name;
    HANDLE m_handle = nullptr;
};
