// ldaptest.cpp -- a tiny benign LDAP client used only to validate AdInsight.
//
// It makes synthetic wldap32 calls against a DUMMY, non-existent target with
// placeholder values and NO real credentials and NO real directory. It prints
// its PID, then loops making the calls so the harness has time to inject the
// hook DLL and capture subsequent iterations. The calls are expected to fail
// (nothing is listening), which is fine: the point is that the hook sees the
// operation and its parameters, not that a server answers.
#include <winsock2.h>
#include <windows.h>
#include <winldap.h>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "wldap32.lib")

int wmain(int argc, wchar_t** argv)
{
    int iterations = 30;
    if (argc > 1)
    {
        iterations = _wtoi(argv[1]);
    }

    wprintf(L"ldaptest PID %lu\n", ::GetCurrentProcessId());
    fflush(stdout);

    for (int i = 0; i < iterations; ++i)
    {
        // A dummy, non-existent target. No real host, no real directory.
        LDAP* ld = ldap_initW(const_cast<PWSTR>(L"localhost"), 389);
        if (ld != nullptr)
        {
            l_timeval tv = {};
            tv.tv_sec = 1;
            ldap_connect(ld, &tv);

            // Placeholder bind values only. The password is synthetic and the
            // hook never captures it.
            ldap_simple_bind_sW(ld, const_cast<PWSTR>(L"cn=test,dc=example,dc=com"),
                                const_cast<PWSTR>(L"PLACEHOLDER-NOT-A-REAL-SECRET"));

            PWSTR attrs[] = {const_cast<PWSTR>(L"cn"), const_cast<PWSTR>(L"mail"), nullptr};
            LDAPMessage* res = nullptr;
            ldap_search_sW(ld, const_cast<PWSTR>(L"dc=example,dc=com"), LDAP_SCOPE_SUBTREE,
                           const_cast<PWSTR>(L"(objectClass=*)"), attrs, 0, &res);
            if (res != nullptr)
            {
                ldap_msgfree(res);
            }

            ldap_unbind(ld);
        }

        wprintf(L"iteration %d done\n", i);
        fflush(stdout);
        ::Sleep(1000);
    }
    return 0;
}
