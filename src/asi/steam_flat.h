/*
 * steam_flat.h - minimal hand-written prototypes for the Steamworks flat
 * API, resolved at runtime from the already-loaded steam_api_wse2.dll
 * (a clean rename of the official steam_api.dll, SDK ~1.55 -- export
 * audit: patches/Warband_WSE2/findings.md "steam_api_wse2.dll flat-API
 * export audit"). Nothing bundled, nothing redistributed.
 *
 * Struct layouts mirror the public SDK's steamnetworkingtypes.h for the
 * exact interface versions this DLL exports (Sockets v012, Utils v004).
 */
#ifndef STEAM_FLAT_H
#define STEAM_FLAT_H

typedef unsigned int   HSteamNetConnection;   /* 0 = invalid */
typedef unsigned int   HSteamListenSocket;    /* 0 = invalid */
typedef unsigned int   HSteamNetPollGroup;    /* 0 = invalid */
typedef __int64        SteamNetworkingMicroseconds;
typedef int            EResult;               /* k_EResultOK = 1 */

#define SF_RESULT_OK             1
#define SF_RESULT_LIMITEXCEEDED  25

/* ESteamNetworkingConnectionState */
#define SF_STATE_NONE                    0
#define SF_STATE_CONNECTING              1
#define SF_STATE_FINDING_ROUTE           2
#define SF_STATE_CONNECTED               3
#define SF_STATE_CLOSED_BY_PEER          4
#define SF_STATE_PROBLEM_DETECTED        5

/* Send flags */
#define SF_SEND_UNRELIABLE_NO_NAGLE      1

/* ESteamNetworkingConfigValue */
#define SF_CFG_TIMEOUT_INITIAL           24   /* int32, ms (default 10000) */
#define SF_CFG_MTU_DATASIZE              33   /* read-only, int32 */
#define SF_CFG_P2P_TRANSPORT_ICE_ENABLE  104  /* int32 bitmask */
#define SF_CFG_LOGLEVEL_P2P_RENDEZVOUS   17   /* int32: debug-output detail */
#define SF_CFG_LOGLEVEL_SDR_RELAY_PINGS  18
#define SF_CFG_CB_CONNECTION_STATUS      201  /* Ptr: status-changed cb */
#define SF_ICE_ENABLE_ALL                0x7fffffff
/* ESteamNetworkingSocketsDebugOutputType */
#define SF_DEBUG_WARNING                 4
#define SF_DEBUG_VERBOSE                 6

/* ESteamNetworkingConfigScope / DataType */
#define SF_SCOPE_CONNECTION              4
#define SF_CFGTYPE_INT32                 1
#define SF_CFGTYPE_PTR                   5

#pragma pack(push, 1)
typedef struct {
    int m_eType;                 /* k_ESteamNetworkingIdentityType_SteamID = 16 */
    int m_cbSize;
    union {
        unsigned __int64 m_steamID64;
        char m_szUnknownRawString[128];
    } u;
} SteamNetworkingIdentity;

typedef struct {
    union { unsigned char m_ipv6[16]; } u;
    unsigned short m_port;
} SteamNetworkingIPAddr;
#pragma pack(pop)

#define SF_IDENTITY_TYPE_STEAMID 16

typedef struct {
    int m_eValue;
    int m_eDataType;             /* 1=int32 2=int64 3=float 4=string 5=ptr */
    union {
        int m_int32;
        __int64 m_int64;
        float m_float;
        const char *m_string;
        void *m_ptr;
    } m_val;
} SteamNetworkingConfigValue_t;

typedef struct SteamNetworkingMessage_t SteamNetworkingMessage_t;
struct SteamNetworkingMessage_t {
    void *m_pData;
    int m_cbSize;
    HSteamNetConnection m_conn;
    SteamNetworkingIdentity m_identityPeer;
    __int64 m_nConnUserData;
    SteamNetworkingMicroseconds m_usecTimeReceived;
    __int64 m_nMessageNumber;
    void (__cdecl *m_pfnFreeData)(SteamNetworkingMessage_t *);
    void (__cdecl *m_pfnRelease)(SteamNetworkingMessage_t *);
    int m_nChannel;
    int m_nFlags;
    __int64 m_nUserData;
    unsigned short m_idxLane;
    unsigned short _pad1__;
};

typedef struct {
    SteamNetworkingIdentity m_identityRemote;
    __int64 m_nUserData;
    HSteamListenSocket m_hListenSocket;
    SteamNetworkingIPAddr m_addrRemote;
    unsigned short m__pad1;
    unsigned int m_idPOPRemote;
    unsigned int m_idPOPRelay;
    int m_eState;
    int m_eEndReason;
    char m_szEndDebug[128];
    char m_szConnectionDescription[128];
    int m_nFlags;
    unsigned int reserved[63];
} SteamNetConnectionInfo_t;

typedef struct {
    HSteamNetConnection m_hConn;
    SteamNetConnectionInfo_t m_info;
    int m_eOldState;
} SteamNetConnectionStatusChangedCallback_t;

typedef struct {
    int m_eState;
    int m_nPing;
    float m_flConnectionQualityLocal;
    float m_flConnectionQualityRemote;
    float m_flOutPacketsPerSec;
    float m_flOutBytesPerSec;
    float m_flInPacketsPerSec;
    float m_flInBytesPerSec;
    int m_nSendRateBytesPerSecond;
    int m_cbPendingUnreliable;
    int m_cbPendingReliable;
    int m_cbSentUnackedReliable;
    SteamNetworkingMicroseconds m_usecQueueTime;
    unsigned int reserved[16];
} SteamNetConnectionRealTimeStatus_t;

typedef void (__cdecl *SF_StatusChangedFn)(SteamNetConnectionStatusChangedCallback_t *);

/* Compile-time layout guards (C89-compatible static asserts). The three
   structs Steam writes into are pinned too: a silent ABI drift there
   corrupts callback/telemetry reads with no diagnosable symptom. */
typedef char sf_assert_identity[(sizeof(SteamNetworkingIdentity) == 136) ? 1 : -1];
typedef char sf_assert_ipaddr[(sizeof(SteamNetworkingIPAddr) == 18) ? 1 : -1];
typedef char sf_assert_cfgval[(sizeof(SteamNetworkingConfigValue_t) == 16) ? 1 : -1];
/* ESteamNetworkingAvailability: 100=Current, 3=Attempting, 2=Waiting,
   1=NeverTried, -10=Retrying, -100=Previously, -101=Failed, -102=CannotTry */
typedef struct {
    int m_eAvail;
    int m_bPingMeasurementInProgress;
    int m_eAvailNetworkConfig;
    int m_eAvailAnyRelay;
    char m_debugMsg[256];
} SteamRelayNetworkStatus_t;

typedef char sf_assert_conninfo[(sizeof(SteamNetConnectionInfo_t) == 696) ? 1 : -1];
typedef char sf_assert_rtstatus[(sizeof(SteamNetConnectionRealTimeStatus_t) == 120) ? 1 : -1];
typedef char sf_assert_message[(sizeof(SteamNetworkingMessage_t) == 208) ? 1 : -1];

/* CCallbackBase emulation (x86 MSVC thiscall via the repo's __fastcall
   (ecx,edx) idiom). MSVC lays out same-name virtual overloads in REVERSE
   declaration order, so the real vtable is RunIO (3-arg) first, then the
   1-arg Run that Steam's plain-callback dispatch actually calls, then
   GetCallbackSizeBytes -- do not "fix" this back to declaration order. */
typedef struct sf_cbase sf_cbase_t;
typedef struct {
    void (__fastcall *RunIO)(sf_cbase_t *self, void *edx, void *pvParam,
                              unsigned char bIOFailure, unsigned __int64 hSteamAPICall);
    void (__fastcall *Run)(sf_cbase_t *self, void *edx, void *pvParam);
    int  (__fastcall *GetCallbackSizeBytes)(sf_cbase_t *self, void *edx);
} sf_cbase_vtbl_t;
struct sf_cbase {
    const sf_cbase_vtbl_t *vtbl;
    unsigned char m_nCallbackFlags;   /* 0; Steam sets registered-flag bits */
    int m_iCallback;                  /* set by SteamAPI_RegisterCallback */
};

/* k_iSteamFriendsCallbacks(300) + 37 -- RE-verified (Task 1) */
#define SF_CB_GAME_RICH_PRESENCE_JOIN_REQUESTED 337
typedef struct {
    unsigned __int64 m_steamIDFriend;
    char m_rgchConnect[256];          /* k_cchMaxRichPresenceValueLength */
} sf_GameRichPresenceJoinRequested_t;

typedef char sf_assert_cbase[(sizeof(sf_cbase_t) == 12) ? 1 : -1];
typedef char sf_assert_join_req[(sizeof(sf_GameRichPresenceJoinRequested_t) == 264) ? 1 : -1];

/* Lobby API constants (ISteamMatchmaking, SDK header values). */
#define SF_LOBBY_TYPE_PUBLIC     2
#define SF_LOBBY_TYPE_INVISIBLE  3   /* returned by search; not friend-visible */
#define SF_LOBBY_CMP_EQUAL       0   /* k_ELobbyComparisonEqual */
#define SF_LOBBY_DIST_WORLDWIDE  3   /* default filter is geo-restricted */
#define SF_CHAT_ENTER_SUCCESS    1   /* k_EChatRoomEnterResponseSuccess */

/* k_iSteamMatchmakingCallbacks = 500; k_iSteamFriendsCallbacks = 300 */
#define SF_CB_LOBBY_CREATED               513  /* +13 */
#define SF_CB_LOBBY_ENTER                 504  /* +4 */
#define SF_CB_LOBBY_MATCH_LIST            510  /* +10 */
#define SF_CB_GAME_LOBBY_JOIN_REQUESTED   333  /* +33, web-verified 2026-08-23 */

/* Callback payloads. MSVC's default 8-byte member alignment matches the
   SDK's Windows callback pack (VALVE_CALLBACK_PACK_LARGE) -- no pragma. */
typedef struct {
    int              m_eResult;        /* SF_RESULT_OK on success */
    unsigned __int64 m_ulSteamIDLobby; /* offset 8 (4 pad bytes before) */
} sf_LobbyCreated_t;

typedef struct {
    unsigned __int64 m_ulSteamIDLobby;
    unsigned int     m_rgfChatPermissions;
    unsigned char    m_bLocked;
    unsigned int     m_EChatRoomEnterResponse;  /* SF_CHAT_ENTER_SUCCESS = in */
} sf_LobbyEnter_t;

typedef struct {
    unsigned int m_nLobbiesMatching;
} sf_LobbyMatchList_t;

typedef struct {
    unsigned __int64 m_steamIDLobby;
    unsigned __int64 m_steamIDFriend;
} sf_GameLobbyJoinRequested_t;

typedef char sf_assert_lobby_created[(sizeof(sf_LobbyCreated_t) == 16) ? 1 : -1];
typedef char sf_assert_lobby_enter[(sizeof(sf_LobbyEnter_t) == 24) ? 1 : -1];
typedef char sf_assert_lobby_matchlist[(sizeof(sf_LobbyMatchList_t) == 4) ? 1 : -1];
typedef char sf_assert_lobby_join_req[(sizeof(sf_GameLobbyJoinRequested_t) == 16) ? 1 : -1];

/* Flat-function pointer table. All flat calls are __cdecl with the
   interface pointer as the first argument. bool returns are 1 byte. */
typedef struct {
    void *sockets;   /* ISteamNetworkingSockets012* */
    void *utils;     /* ISteamNetworkingUtils004* */

    HSteamListenSocket (__cdecl *CreateListenSocketP2P)(void *, int nLocalVirtualPort, int nOptions, const SteamNetworkingConfigValue_t *);
    HSteamNetConnection (__cdecl *ConnectP2P)(void *, const SteamNetworkingIdentity *, int nRemoteVirtualPort, int nOptions, const SteamNetworkingConfigValue_t *);
    EResult (__cdecl *AcceptConnection)(void *, HSteamNetConnection);
    unsigned char (__cdecl *CloseConnection)(void *, HSteamNetConnection, int nReason, const char *pszDebug, unsigned char bEnableLinger);
    unsigned char (__cdecl *CloseListenSocket)(void *, HSteamListenSocket);
    EResult (__cdecl *SendMessageToConnection)(void *, HSteamNetConnection, const void *, unsigned int cb, int nSendFlags, __int64 *pOutMessageNumber);
    HSteamNetPollGroup (__cdecl *CreatePollGroup)(void *);
    unsigned char (__cdecl *DestroyPollGroup)(void *, HSteamNetPollGroup);
    unsigned char (__cdecl *SetConnectionPollGroup)(void *, HSteamNetConnection, HSteamNetPollGroup);
    int (__cdecl *ReceiveMessagesOnPollGroup)(void *, HSteamNetPollGroup, SteamNetworkingMessage_t **, int nMaxMessages);
    void (__cdecl *RunCallbacks)(void *);
    EResult (__cdecl *GetConnectionRealTimeStatus)(void *, HSteamNetConnection, SteamNetConnectionRealTimeStatus_t *, int nLanes, void *pLanes);

    void (__cdecl *InitRelayNetworkAccess)(void *);
    unsigned char (__cdecl *SetGlobalConfigValueInt32)(void *, int eValue, int val);
    /* Process-wide SteamAPI callback pump (no interface arg). The engine
       never pumps it after boot, and SDR relay-config call-results ride on
       it -- without a pump, relay access wedges at "Attempting" forever. */
    void (__cdecl *RunSteamCallbacks)(void);
    /* optional (MTU telemetry only; may be NULL after a good resolve).
       scopeObj is intptr_t in the SDK -- 4 bytes on x86, NOT __int64
       (an 8-byte push would shift the three out-params on the stack). */
    int (__cdecl *GetConfigValue)(void *, int eValue, int eScopeType, int scopeObj, int *pOutDataType, void *pResult, unsigned int *cbResult);
    /* optional (diagnostics only) */
    int (__cdecl *GetRelayNetworkStatus)(void *, SteamRelayNetworkStatus_t *);
    void (__cdecl *SetDebugOutputFunction)(void *, int eDetailLevel,
                                           void (__cdecl *)(int, const char *));

    /* optional -- invites only; absence disables Join Game, never the
       tunnel. All-or-nothing: see steam_flat_resolve. */
    void *friends;   /* ISteamFriends017* */
    void *user;      /* ISteamUser* */
    unsigned char (__cdecl *SetRichPresence)(void *, const char *pchKey, const char *pchValue);
    void (__cdecl *ClearRichPresence)(void *);
    void (__cdecl *RegisterCallback)(void *pCallback, int iCallback);
    void (__cdecl *UnregisterCallback)(void *pCallback);
    unsigned __int64 (__cdecl *GetSteamID)(void *);

    /* optional -- lobby invites only (phase 5); absence disables the
       right-click Invite to Game path, never the tunnel and never the
       rich-presence chain invites. All-or-nothing, and only attempted
       when the friends group above resolved (callback registration and
       the invite parse path live there). CSteamID travels as uint64. */
    void *matchmaking;   /* ISteamMatchmaking009* */
    unsigned __int64 (__cdecl *CreateLobby)(void *, int eLobbyType, int cMaxMembers);            /* SteamAPICall_t */
    unsigned __int64 (__cdecl *JoinLobby)(void *, unsigned __int64 lobby);                       /* SteamAPICall_t */
    void (__cdecl *LeaveLobby)(void *, unsigned __int64 lobby);
    unsigned char (__cdecl *SetLobbyData)(void *, unsigned __int64 lobby, const char *k, const char *v);
    const char *(__cdecl *GetLobbyData)(void *, unsigned __int64 lobby, const char *k);
    unsigned __int64 (__cdecl *RequestLobbyList)(void *);                                        /* SteamAPICall_t */
    void (__cdecl *AddRequestLobbyListStringFilter)(void *, const char *k, const char *v, int eCmp);
    void (__cdecl *AddRequestLobbyListDistanceFilter)(void *, int eDist);
    unsigned __int64 (__cdecl *GetLobbyByIndex)(void *, int iLobby);
    unsigned __int64 (__cdecl *GetLobbyOwner)(void *, unsigned __int64 lobby);
    void (__cdecl *RegisterCallResult)(void *pCallback, unsigned __int64 hAPICall);
    void (__cdecl *UnregisterCallResult)(void *pCallback, unsigned __int64 hAPICall);
} steam_flat_t;

extern steam_flat_t SF;

/* Binds SF off GetModuleHandleA("steam_api_wse2.dll"). Returns 1 when every
   required member is bound; 0 otherwise (missing exports coop_log'd,
   SF zeroed). Never LoadLibrary's anything. */
int steam_flat_resolve(void);

#endif /* STEAM_FLAT_H */
