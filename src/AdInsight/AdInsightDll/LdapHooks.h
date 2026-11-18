// LdapHooks.h -- the set of wldap32 LDAP client functions the hook DLL
// intercepts.
//
// The original ADInsightDll.dll hooks the full wldap32 surface. This core
// reproduction hooks a representative set in dllmain.cpp, each with its own
// trampoline:
//
//   ldap_initW           connection setup (host and port)
//   ldap_connect         connect to the server
//   ldap_simple_bind_sW  simple bind (the password is never captured)
//   ldap_bind_sW         bind with an auth method
//   ldap_search_sW       synchronous search
//   ldap_search_ext_sW   extended synchronous search
//   ldap_result          retrieve an asynchronous result
//   ldap_get_valuesW     read attribute values
//   ldap_unbind          unbind and close
//
// The exhaustive per-operation attribute decode is out of scope and noted in
// analysis/AdInsight.md.
#pragma once
