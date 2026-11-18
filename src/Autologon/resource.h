// resource.h -- resource identifiers for the Autologon recreation.
// The original Autologon64.exe carries two named dialog templates (AUTOLOG and
// ABOUT) with no numeric IDs and no icon resource. The recreation assigns numeric
// IDs so WTL CDialogImpl binds them directly. Button command IDs match the
// original (Enable 1, Disable 2, About 3) and the edit IDs match (1000..1002).
#pragma once

// ---- Dialog templates ----
#define IDD_MAIN            100
#define IDD_ABOUT           101

// ---- Main dialog control IDs (from the extracted AUTOLOG template) ----
#define IDC_USERNAME        1000
#define IDC_DOMAIN          1001
#define IDC_PASSWORD        1002
// Enable is IDOK (1), Disable is IDCANCEL (2).
#define IDC_ABOUTBTN        3

// ---- About dialog control IDs ----
#define IDC_ABOUT_NAME      1003
#define IDC_ABOUT_DESC      1004
