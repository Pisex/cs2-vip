#pragma once

#include <functional>
#include <string>

/////////////////////////////////////////////////////////////////
///////////////////////      PLAYERS     //////////////////////////
/////////////////////////////////////////////////////////////////

typedef std::function<void(int iSlot, uint64 iSteamID64)> OnClientAuthorizedCallback;

#define PLAYERS_INTERFACE "IPlayersApi"
class IPlayersApi
{
public:
    virtual bool IsFakeClient(int iSlot) = 0;
    virtual bool IsAuthenticated(int iSlot) = 0;
    virtual bool IsConnected(int iSlot) = 0;
    virtual bool IsInGame(int iSlot) = 0;
    virtual const char* GetIpAddress(int iSlot) = 0;
    virtual uint64 GetSteamID64(int iSlot) = 0;
    virtual const CSteamID* GetSteamID(int iSlot) = 0;

    virtual void HookOnClientAuthorized(SourceMM::PluginId id, OnClientAuthorizedCallback callback) = 0;
};

/////////////////////////////////////////////////////////////////
///////////////////////      UTILS     //////////////////////////
/////////////////////////////////////////////////////////////////

class CCSGameRules;

#define Utils_INTERFACE "IUtilsApi"

typedef std::function<bool(int iSlot, const char* szContent)> CommandCallback;
typedef std::function<bool(int iSlot, const char* szContent, bool bMute)> CommandCallbackPost;
typedef std::function<void(const char* szName, IGameEvent* pEvent, bool bDontBroadcast)> EventCallback;
typedef std::function<void()> StartupCallback;

class IUtilsApi
{
public:
    virtual void PrintToChat(int iSlot, const char* msg, ...) = 0;
    virtual void PrintToChatAll(const char* msg, ...) = 0;
    virtual void NextFrame(std::function<void()> fn) = 0;
    virtual CCSGameRules* GetCCSGameRules() = 0;
    virtual CGameEntitySystem* GetCGameEntitySystem() = 0;
    virtual CEntitySystem* GetCEntitySystem() = 0;
	virtual CGlobalVars* GetCGlobalVars() = 0;
	virtual IGameEventManager2* GetGameEventManager() = 0;

    virtual const char* GetLanguage() = 0;

    virtual void StartupServer(SourceMM::PluginId id, StartupCallback fn) = 0;
    virtual void OnGetGameRules(SourceMM::PluginId id, StartupCallback fn) = 0;

    virtual void RegCommand(SourceMM::PluginId id, const std::vector<std::string> &console, const std::vector<std::string> &chat, const CommandCallback &callback) = 0;
    virtual void AddChatListenerPre(SourceMM::PluginId id, CommandCallback callback) = 0;
    virtual void AddChatListenerPost(SourceMM::PluginId id, CommandCallbackPost callback) = 0;
    virtual void HookEvent(SourceMM::PluginId id, const char* sName, EventCallback callback) = 0;

    virtual void SetStateChanged(CBaseEntity* entity, const char* sClassName, const char* sFieldName, int extraOffset = 0) = 0;

    virtual void ClearAllHooks(SourceMM::PluginId id) = 0;

    virtual void LoadTranslations(const char* szFile) = 0;
	virtual void PrintToConsole(int iSlot, const char* msg, ...) = 0;
	virtual void PrintToConsoleAll(const char* msg, ...) = 0;
	virtual void PrintToCenter(int iSlot, const char* msg, ...) = 0;
	virtual void PrintToCenterAll(const char* msg, ...) = 0;
	virtual void PrintToCenterHtml(int iSlot, int iDuration, const char* msg, ...) = 0;
	virtual void PrintToCenterHtmlAll(int iDuration, const char* msg, ...) = 0;

    virtual void LogToFile(const char* szFile, const char* szText, ...) = 0;
    virtual void ErrorLog(const char* msg, ...) = 0;
};

/////////////////////////////////////////////////////////////////
///////////////////////      MENUS     //////////////////////////
/////////////////////////////////////////////////////////////////

#define Menus_INTERFACE "IMenusApi"

#define ITEM_HIDE 0
#define ITEM_DEFAULT 1
#define ITEM_DISABLED 2

typedef std::function<void(const char* szBack, const char* szFront, int iItem, int iSlot)> MenuCallbackFunc;

struct Items
{
    int iType;
    std::string sBack;
    std::string sText;
};

struct Menu
{
    std::string szTitle;	
    std::vector<Items> hItems;
    bool bBack;
    bool bExit;
	MenuCallbackFunc hFunc;
};

struct MenuPlayer
{
    bool bEnabled;
    int iList;
    Menu hMenu;
    int iEnd;
};

class IMenusApi
{
public:
	virtual void AddItemMenu(Menu& hMenu, const char* sBack, const char* sText, int iType = 1) = 0;
	virtual void DisplayPlayerMenu(Menu& hMenu, int iSlot, bool bClose = true) = 0;
	virtual void SetExitMenu(Menu& hMenu, bool bExit) = 0;
	virtual void SetBackMenu(Menu& hMenu, bool bBack) = 0;
	virtual void SetTitleMenu(Menu& hMenu, const char* szTitle) = 0;
	virtual void SetCallback(Menu& hMenu, MenuCallbackFunc func) = 0;
    virtual void ClosePlayerMenu(int iSlot) = 0;
    virtual std::string escapeString(const std::string& input) = 0;
};

/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////