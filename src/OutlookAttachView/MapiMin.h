// MapiMin.h -- a minimal, self-contained subset of the Extended MAPI headers
// (mapidefs.h / mapix.h / mapitags.h / mapiutil.h).
//
// The modern Windows SDK ships only Simple MAPI (mapi.h), not the Extended MAPI
// headers that the original links against, and installing the Outlook MAPI
// headers is out of scope. This file declares just the structures, property
// tags and interface vtables that the scanner uses, so the recreation builds
// with no Outlook, no profile and no extra SDK. mapi32.dll is still loaded
// dynamically at run time, so the module also loads where MAPI is absent.
//
// Only the methods the scanner calls carry real signatures. The others are
// present as pure-virtual placeholders purely to keep each interface's vtable
// slots at the offsets the real MAPI objects use.
#pragma once
#include <windows.h>

// ---- basic MAPI types --------------------------------------------------
typedef struct { ULONG cb; LPBYTE lpb; } SBinary, *LPSBINARY;

typedef struct { BYTE ab[16]; } MAPIUID, *LPMAPIUID;

typedef struct { BYTE abFlags[4]; BYTE ab[1]; } ENTRYID, *LPENTRYID;

// A property value. On disk this is the MAPI __UPV union plus the tag.
union MAPI_PV
{
    short          i;
    LONG           l;
    ULONG          ul;
    float          flt;
    double         dbl;
    unsigned short b;      // PT_BOOLEAN (16-bit)
    CURRENCY       cur;
    double         at;
    FILETIME       ft;
    LPSTR          lpszA;
    LPWSTR         lpszW;
    SBinary        bin;
    LPGUID         lpguid;
    LARGE_INTEGER  li;
    struct { ULONG cValues; LONG*     lpl;    } MVl;
    struct { ULONG cValues; LPWSTR*   lppszW; } MVszW;
    struct { ULONG cValues; SBinary*  lpbin;  } MVbin;
    SCODE          err;
    LONG           x;
};

typedef struct
{
    ULONG   ulPropTag;
    ULONG   dwAlignPad;
    MAPI_PV Value;
} SPropValue, *LPSPropValue;

#ifndef MAPI_DIM
#define MAPI_DIM 1
#endif

typedef struct { ULONG cValues; ULONG aulPropTag[MAPI_DIM]; } SPropTagArray, *LPSPropTagArray;

typedef struct { ULONG ulAdrEntryPad; ULONG cValues; LPSPropValue lpProps; } SRow;
typedef struct { ULONG cRows; SRow aRow[MAPI_DIM]; } SRowSet, *LPSRowSet;

typedef struct { ULONG ulVersion; ULONG ulFlags; } MAPIINIT_0, *LPMAPIINIT_0;

// ---- property-tag helpers ----------------------------------------------
#define PT_UNSPECIFIED  0x0000
#define PT_NULL         0x0001
#define PT_I2           0x0002
#define PT_LONG         0x0003
#define PT_BOOLEAN      0x000B
#define PT_OBJECT       0x000D
#define PT_I8           0x0014
#define PT_STRING8      0x001E
#define PT_UNICODE      0x001F
#define PT_SYSTIME      0x0040
#define PT_CLSID        0x0048
#define PT_BINARY       0x0102
#define PT_ERROR        0x000A
#define PT_MV_FLAG      0x1000

#define PROP_TYPE_MASK  0x0000FFFFUL
#define PROP_TYPE(tag)  ((tag) & PROP_TYPE_MASK)
#define PROP_ID(tag)    (((tag) >> 16) & 0xFFFF)
#define PROP_TAG(type, id) (((ULONG)(id) << 16) | (type))
#define PR_NULL         PROP_TAG(PT_NULL, 0)

// Store / object / folder props
#define PR_ENTRYID                      PROP_TAG(PT_BINARY,  0x0FFF)
#define PR_OBJECT_TYPE                  PROP_TAG(PT_LONG,    0x0FFE)
#define PR_DISPLAY_NAME_W               PROP_TAG(PT_UNICODE, 0x3001)
#define PR_DEFAULT_STORE                PROP_TAG(PT_BOOLEAN, 0x3400)
#define PR_IPM_SUBTREE_ENTRYID          PROP_TAG(PT_BINARY,  0x35E0)
#define PR_SUBFOLDERS                   PROP_TAG(PT_BOOLEAN, 0x360A)
#define PR_CONTENT_COUNT                PROP_TAG(PT_LONG,    0x3602)

// Message props
#define PR_SUBJECT_W                    PROP_TAG(PT_UNICODE, 0x0037)
#define PR_MESSAGE_CLASS_W              PROP_TAG(PT_UNICODE, 0x001A)
#define PR_MESSAGE_FLAGS                PROP_TAG(PT_LONG,    0x0E07)
#define PR_CLIENT_SUBMIT_TIME           PROP_TAG(PT_SYSTIME, 0x0039)
#define PR_MESSAGE_DELIVERY_TIME        PROP_TAG(PT_SYSTIME, 0x0E06)
#define PR_CREATION_TIME                PROP_TAG(PT_SYSTIME, 0x3007)
#define PR_SENDER_NAME_W                PROP_TAG(PT_UNICODE, 0x0C1A)
#define PR_SENDER_EMAIL_ADDRESS_W       PROP_TAG(PT_UNICODE, 0x0C1F)
#define PR_SENT_REPRESENTING_NAME_W     PROP_TAG(PT_UNICODE, 0x0042)
#define PR_SENT_REPRESENTING_EMAIL_W    PROP_TAG(PT_UNICODE, 0x0065)
#define PR_DISPLAY_TO_W                 PROP_TAG(PT_UNICODE, 0x0E04)
#define PR_TRANSPORT_MESSAGE_HEADERS_W  PROP_TAG(PT_UNICODE, 0x007D)

// Recipient-table props
#define PR_EMAIL_ADDRESS_W              PROP_TAG(PT_UNICODE, 0x3003)
#define PR_ADDRTYPE_W                   PROP_TAG(PT_UNICODE, 0x3002)
#define PR_SMTP_ADDRESS_W               PROP_TAG(PT_UNICODE, 0x39FE)
#define PR_RECIPIENT_TYPE               PROP_TAG(PT_LONG,    0x0C15)

// Attachment-table props
#define PR_ATTACH_NUM                   PROP_TAG(PT_LONG,    0x0E21)
#define PR_ATTACH_METHOD                PROP_TAG(PT_LONG,    0x3705)
#define PR_ATTACH_SIZE                  PROP_TAG(PT_LONG,    0x0E20)
#define PR_ATTACH_LONG_FILENAME_W       PROP_TAG(PT_UNICODE, 0x3707)
#define PR_ATTACH_FILENAME_W            PROP_TAG(PT_UNICODE, 0x3704)
#define PR_ATTACH_MIME_TAG_W            PROP_TAG(PT_UNICODE, 0x370E)
#define PR_ATTACH_CONTENT_ID_W          PROP_TAG(PT_UNICODE, 0x3712)
#define PR_RENDERING_POSITION           PROP_TAG(PT_LONG,    0x370B)
#define PR_ATTACH_DATA_BIN              PROP_TAG(PT_BINARY,  0x3701)
#define PR_ATTACH_DATA_OBJ             PROP_TAG(PT_OBJECT,  0x3701)

// Attachment methods (PR_ATTACH_METHOD values)
#define NO_ATTACHMENT        0
#define ATTACH_BY_VALUE      1
#define ATTACH_BY_REFERENCE  2
#define ATTACH_BY_REF_RESOLVE 3
#define ATTACH_BY_REF_ONLY   4
#define ATTACH_EMBEDDED_MSG  5
#define ATTACH_OLE           6

// Recipient types (PR_RECIPIENT_TYPE)
#define MAPI_ORIG  0
#define MAPI_TO    1
#define MAPI_CC    2
#define MAPI_BCC   3

// Object types (PR_OBJECT_TYPE)
#define MAPI_STORE   1
#define MAPI_FOLDER  3
#define MAPI_MESSAGE 5
#define MAPI_ATTACH  7

// Message flags (PR_MESSAGE_FLAGS)
#define MSGFLAG_FROMME  0x00000040

// Logon / open flags
#define MAPI_LOGON_UI     0x00000001
#define MAPI_NEW_SESSION  0x00000002
#define MAPI_EXTENDED     0x00000020
#define MAPI_USE_DEFAULT  0x00000040
#define MAPI_NO_MAIL      0x00008000
#define MAPI_UNICODE      0x80000000
#define MAPI_DEFERRED_ERRORS 0x00000008
#define MAPI_BEST_ACCESS  0x00000010
#define MDB_NO_DIALOG     0x00000001
#define MDB_NO_MAIL       0x00000080
#define TBL_BATCH         0x00000007
#define TBL_ALL_COLUMNS   0x00000001
#define MAPI_W_ERRORS_RETURNED ((SCODE)0x00040380L)

// ---- interfaces (only the slots the scanner needs carry real signatures) --
struct IMAPITable;
struct IAttach;
struct IMessage;
struct IMAPIFolder;
struct IMsgStore;
struct IMAPISession;
typedef IMAPITable*   LPMAPITABLE;
typedef IAttach*      LPATTACH;
typedef IMessage*     LPMESSAGE;
typedef IMAPIFolder*  LPMAPIFOLDER;
typedef IMsgStore*    LPMDB;
typedef IMAPISession* LPMAPISESSION;
typedef const IID*    LPCIID;
typedef void*         LPMAPIPROGRESS;

// OpenProperty / IStream flags used when extracting attachment data.
#define STGM_READ        0x00000000L
#define MAPI_MODIFY      0x00000001
#define IID_IStream_ptr  (&IID_IStream)

struct IMAPITable : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetLastError() = 0;
    virtual HRESULT STDMETHODCALLTYPE Advise() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unadvise() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStatus() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetColumns(LPSPropTagArray lpPropTagArray, ULONG ulFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryColumns() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRowCount(ULONG ulFlags, ULONG* lpulCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE SeekRow() = 0;
    virtual HRESULT STDMETHODCALLTYPE SeekRowApprox() = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryPosition() = 0;
    virtual HRESULT STDMETHODCALLTYPE FindRow() = 0;
    virtual HRESULT STDMETHODCALLTYPE Restrict() = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBookmark() = 0;
    virtual HRESULT STDMETHODCALLTYPE FreeBookmark() = 0;
    virtual HRESULT STDMETHODCALLTYPE SortTable() = 0;
    virtual HRESULT STDMETHODCALLTYPE QuerySortOrder() = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryRows(LONG lRowCount, ULONG ulFlags, LPSRowSet* lppRows) = 0;
    virtual HRESULT STDMETHODCALLTYPE Abort() = 0;
    virtual HRESULT STDMETHODCALLTYPE ExpandRow() = 0;
    virtual HRESULT STDMETHODCALLTYPE CollapseRow() = 0;
    virtual HRESULT STDMETHODCALLTYPE WaitForCompletion() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCollapseState() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCollapseState() = 0;
};

// IMAPIProp slots, shared by stores, folders, messages and attachments.
#define MAPIPROP_METHODS \
    virtual HRESULT STDMETHODCALLTYPE GetLastError() = 0; \
    virtual HRESULT STDMETHODCALLTYPE SaveChanges(ULONG ulFlags) = 0; \
    virtual HRESULT STDMETHODCALLTYPE GetProps(LPSPropTagArray lpPropTagArray, ULONG ulFlags, ULONG* lpcValues, LPSPropValue* lppPropArray) = 0; \
    virtual HRESULT STDMETHODCALLTYPE GetPropList() = 0; \
    virtual HRESULT STDMETHODCALLTYPE OpenProperty() = 0; \
    virtual HRESULT STDMETHODCALLTYPE SetProps() = 0; \
    virtual HRESULT STDMETHODCALLTYPE DeleteProps() = 0; \
    virtual HRESULT STDMETHODCALLTYPE CopyTo() = 0; \
    virtual HRESULT STDMETHODCALLTYPE CopyProps() = 0; \
    virtual HRESULT STDMETHODCALLTYPE GetNamesFromIDs() = 0; \
    virtual HRESULT STDMETHODCALLTYPE GetIDsFromNames() = 0;

struct IAttach : public IUnknown
{
    MAPIPROP_METHODS
};

struct IMessage : public IUnknown
{
    MAPIPROP_METHODS
    virtual HRESULT STDMETHODCALLTYPE GetAttachmentTable(ULONG ulFlags, LPMAPITABLE* lppTable) = 0;
    virtual HRESULT STDMETHODCALLTYPE OpenAttach(ULONG ulAttachmentNum, LPCIID lpInterface, ULONG ulFlags, LPATTACH* lppAttach) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateAttach() = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteAttach(ULONG ulAttachmentNum, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRecipientTable(ULONG ulFlags, LPMAPITABLE* lppTable) = 0;
    virtual HRESULT STDMETHODCALLTYPE ModifyRecipients() = 0;
    virtual HRESULT STDMETHODCALLTYPE SubmitMessage() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetReadFlag() = 0;
};

struct IMAPIFolder : public IUnknown
{
    MAPIPROP_METHODS
    virtual HRESULT STDMETHODCALLTYPE GetContentsTable(ULONG ulFlags, LPMAPITABLE* lppTable) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetHierarchyTable(ULONG ulFlags, LPMAPITABLE* lppTable) = 0;
    virtual HRESULT STDMETHODCALLTYPE OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG* lpulObjType, LPUNKNOWN* lppUnk) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSearchCriteria() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSearchCriteria() = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateMessage() = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyMessages() = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteMessages() = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFolder() = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyFolder() = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteFolder() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetReadFlags() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMessageStatus() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMessageStatus() = 0;
    virtual HRESULT STDMETHODCALLTYPE SaveContentsSort() = 0;
    virtual HRESULT STDMETHODCALLTYPE EmptyFolder() = 0;
};

struct IMsgStore : public IUnknown
{
    MAPIPROP_METHODS
    virtual HRESULT STDMETHODCALLTYPE Advise() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unadvise() = 0;
    virtual HRESULT STDMETHODCALLTYPE CompareEntryIDs() = 0;
    virtual HRESULT STDMETHODCALLTYPE OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG* lpulObjType, LPUNKNOWN* lppUnk) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetReceiveFolder() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetReceiveFolder() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetReceiveFolderTable() = 0;
    virtual HRESULT STDMETHODCALLTYPE StoreLogoff() = 0;
    virtual HRESULT STDMETHODCALLTYPE AbortSubmit() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetOutgoingQueue() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetLockState() = 0;
    virtual HRESULT STDMETHODCALLTYPE FinishedMsg() = 0;
    virtual HRESULT STDMETHODCALLTYPE NotifyNewMail() = 0;
};

struct IMAPISession : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetLastError() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMsgStoresTable(ULONG ulFlags, LPMAPITABLE* lppTable) = 0;
    virtual HRESULT STDMETHODCALLTYPE OpenMsgStore(ULONG_PTR ulUIParam, ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, LPMDB* lppMDB) = 0;
    virtual HRESULT STDMETHODCALLTYPE OpenAddressBook() = 0;
    virtual HRESULT STDMETHODCALLTYPE OpenProfileSection() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStatusTable() = 0;
    virtual HRESULT STDMETHODCALLTYPE OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG* lpulObjType, LPUNKNOWN* lppUnk) = 0;
    virtual HRESULT STDMETHODCALLTYPE CompareEntryIDs() = 0;
    virtual HRESULT STDMETHODCALLTYPE Advise() = 0;
    virtual HRESULT STDMETHODCALLTYPE Unadvise() = 0;
    virtual HRESULT STDMETHODCALLTYPE MessageOptions() = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryDefaultMessageOpt() = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumAdrTypes() = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryIdentity() = 0;
    virtual HRESULT STDMETHODCALLTYPE Logoff(ULONG_PTR ulUIParam, ULONG ulFlags, ULONG ulReserved) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultStore() = 0;
    virtual HRESULT STDMETHODCALLTYPE AdminServices() = 0;
    virtual HRESULT STDMETHODCALLTYPE ShowForm() = 0;
    virtual HRESULT STDMETHODCALLTYPE PrepareForm() = 0;
};

// Dynamic entry points resolved from mapi32.dll.
typedef HRESULT (STDAPICALLTYPE* MAPIINITIALIZE)(LPVOID lpMapiInit);
typedef void    (STDAPICALLTYPE* MAPIUNINITIALIZE)(void);
typedef HRESULT (STDAPICALLTYPE* MAPILOGONEX)(ULONG_PTR ulUIParam, LPWSTR lpszProfileName,
                                              LPWSTR lpszPassword, ULONG ulFlags, LPMAPISESSION* lppSession);
typedef ULONG   (STDAPICALLTYPE* MAPIFREEBUFFER)(LPVOID lpBuffer);
