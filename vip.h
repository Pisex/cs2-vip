#ifndef _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
#define _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_

#include <ISmmPlugin.h>
#include <sh_vector.h>
#include "utlvector.h"
#include "ehandle.h"
#include <iserver.h>
#include <entity2/entitysystem.h>
#include "igameevents.h"
#include "vector.h"
#include <deque>
#include <functional>
#include "sdk/utils.hpp"
#include <utlstring.h>
#include <KeyValues.h>
#include "sdk/schemasystem.h"
#include "sdk/CBaseEntity.h"
#include "sdk/CGameRulesProxy.h"
#include "sdk/CBasePlayerPawn.h"
#include "sdk/CCSPlayerController.h"
#include "sdk/CCSPlayer_ItemServices.h"
#include "sdk/CSmokeGrenadeProjectile.h"
#include "sdk/module.h"
#include "include/mysql_mm.h"
#include "include/vip.h"
#include <map>
#include <ctime>
#include <chrono>
#include <array>
#include <thread>

class VIP final : public ISmmPlugin, public IMetamodListener
{
public:
	bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late);
	bool Unload(char* error, size_t maxlen);
	void NextFrame(std::function<void()> fn);
	bool LoadVips(char* error, size_t maxlen);
    bool LoadVIPData(char* error, size_t maxlen);
	void AllPluginsLoaded();
	void* OnMetamodQuery(const char* iface, int* ret);
private:
	const char* GetAuthor();
	const char* GetName();
	const char* GetDescription();
	const char* GetURL();
	const char* GetLicense();
	const char* GetVersion();
	const char* GetDate();
	const char* GetLogTag();

private: // Hooks
    void OnClientCommand(CPlayerSlot slot, const CCommand &args);
	bool OnFireEvent(IGameEvent* pEvent, bool bDontBroadcast);
	void StartupServer(const GameSessionConfiguration_t& config, ISource2WorldSession*, const char*);
	void GameFrame(bool simulating, bool bFirstTick, bool bLastTick);
	void OnClientPutInServer(CPlayerSlot slot, char const* pszName, int type, uint64 xuid);
	void OnClientDisconnect(CPlayerSlot slot, int reason, const char *pszName, uint64 xuid, const char *pszNetworkID);
    void OnDispatchConCommand(ConCommandHandle cmd, const CCommandContext& ctx, const CCommand& args);
	
	int g_iLastTime;

	std::deque<std::function<void()>> m_nextFrame;
};

class VIPApi : public IVIPApi {
	bool bReady;
    std::vector<ReadyCallbackFunc> vipOnVIPLoadeds;
    std::vector<SpawnCallbackFunc> vipOnPlayerSpawn;
    std::vector<EventCallbackFunc> vipOnFireEvent;
    std::vector<ClientLoadedOrDisconnectCallbackFunc> vipOnClientLoaded;
    std::vector<ClientLoadedOrDisconnectCallbackFunc> vipOnClientDisconnect;
    std::vector<VIPAddCallbackFunc> vipOnVIPClientAdded;
    std::vector<VIPRemoveCallbackFunc> vipOnVIPClientRemoved;
    std::map<std::string, VIPCommandCallbackFunc> vipCommand;
    bool VIP_IsVIPLoaded() override {
		return bReady;
	}
	bool VIP_IsClientVIP(int iSlot);
	int VIP_GetClientAccessTime(int iSlot);

    void VIP_PrintToChat(int Slot, int hud_dest, const char *msg, ...);
    void VIP_PrintToChatAll(int hud_dest, const char *msg, ...);

	int VIP_GetClientFeatureInt(int iSlot, const char* szFeature);
	bool VIP_GetClientFeatureBool(int iSlot, const char* szFeature);
	float VIP_GetClientFeatureFloat(int iSlot, const char* szFeature);
	const char *VIP_GetClientFeatureString(int iSlot, const char* szFeature);
	const char *VIP_GetClientVIPGroup(int iSlot);
	bool VIP_IsValidVIPGroup(const char* szGroup);
	bool VIP_SetClientAccessTime(int iSlot, int iTime, bool bInDB);
	bool VIP_SetClientVIPGroup(int iSlot, const char* szGroup, bool bInDB);
	bool VIP_GiveClientVIP(int iSlot, int iTime, const char* szGroup, bool bAddToDB);
	bool VIP_RemoveClientVIP(int iSlot, bool bNotify, bool bInDB);
    bool VIP_PistolRound();
    bool VIP_WarmupPeriod();
    CGameEntitySystem* VIP_GetEntitySystem();
    int VIP_GetTotalRounds();

    const char *VIP_GetTranslate(const char* phrase);

    bool VIP_SetClientCookie(int iSlot, const char* sCookieName, const char* sData);
    const char *VIP_GetClientCookie(int iSlot, const char* sCookieName);
	

	//iReason: 1 - Expired, 2 - VIP_RemoveClientVIP
	void VIP_OnVIPClientRemoved(VIPRemoveCallbackFunc callback) override {
        vipOnVIPClientRemoved.push_back(callback);
    }
	void VIP_OnVIPClientAdded(VIPAddCallbackFunc callback) override {
        vipOnVIPClientAdded.push_back(callback);
    }
	void VIP_OnClientLoaded(ClientLoadedOrDisconnectCallbackFunc callback) override {
        vipOnClientLoaded.push_back(callback);
    }
	void VIP_OnClientDisconnect(ClientLoadedOrDisconnectCallbackFunc callback) override {
        vipOnClientDisconnect.push_back(callback);
    }
    void VIP_OnPlayerSpawn(SpawnCallbackFunc callback) override {
        vipOnPlayerSpawn.push_back(callback);
    }
	void VIP_OnFireEvent(EventCallbackFunc callback) override {
        vipOnFireEvent.push_back(callback);
    }
    void VIP_OnVIPLoaded(ReadyCallbackFunc callback) override {
        vipOnVIPLoadeds.push_back(callback);
    }

    void VIP_RegCommand(const char* szCommand, VIPCommandCallbackFunc callback) override {
        vipCommand[std::string(szCommand)] = callback;
    }

public:
    void FindAndCallCommand(std::string szCommand, const char* szContent, int iSlot) {
        if(auto it = vipCommand.find(szCommand); it != vipCommand.end()) {
            it->second(szContent, iSlot);
        }
    }
    void Call_VIP_OnVIPLoaded() {
        for (auto& callback : vipOnVIPLoadeds) {
            if (callback) {
                callback();
            }
        }
    }
    void Call_VIP_OnPlayerSpawn(int iSlot, int iTeam, bool bIsVIP) {
        for (auto& callback : vipOnPlayerSpawn) {
            if (callback) {
                callback(iSlot, iTeam, bIsVIP);
            }
        }
    }
    void Call_VIP_OnFireEvent(const char* szName, IGameEvent* pEvent, bool bDontBroadcast) {
        for (auto& callback : vipOnFireEvent) {
            if (callback) {
                callback(szName, pEvent, bDontBroadcast);
            }
        }
    }
    void Call_VIP_OnClientLoaded(int iSlot, bool bIsVIP) {
        for (auto& callback : vipOnClientLoaded) {
            if (callback) {
                callback(iSlot, bIsVIP);
            }
        }
    }
	void Call_VIP_OnClientDisconnect(int iSlot, bool bIsVIP) {
        for (auto& callback : vipOnClientDisconnect) {
            if (callback) {
                callback(iSlot, bIsVIP);
            }
        }
    }

	void Call_VIP_OnVIPClientRemoved(int iSlot, int iReason) {
        for (auto& callback : vipOnVIPClientRemoved) {
            if (callback) {
                callback(iSlot, iReason);
            }
        }
    }
	void Call_VIP_OnVIPClientAdded(int iSlot) {
        for (auto& callback : vipOnVIPClientAdded) {
            if (callback) {
                callback(iSlot);
            }
        }
    }

	void SetReady(bool ready) {
		bReady = ready;
	}
};


class CPlayerSpawnEvent : public IGameEventListener2
{
	void FireGameEvent(IGameEvent* event) override;
};

class CRoundPreStartEvent : public IGameEventListener2
{
	void FireGameEvent(IGameEvent* event) override;
};

class CEntityListener : public IEntityListener
{
	void OnEntitySpawned(CEntityInstance* pEntity) override;
};

struct VipPlayer
{
	std::string sGroup;
	int TimeEnd;
};

struct Items
{
    int iType;
    std::string sBack;
    std::string sText;
};

struct Menus
{
    std::string szTitle;
    std::vector<Items> hItems;
    // bool bBack;
    bool bExit;
};

struct MenuPlayer
{
    bool bEnabled;
    int iList;
    int iMenu;
};

const std::string colors_text[] = {
	"{DEFAULT}",
	"{RED}",
	"{LIGHTPURPLE}",
	"{GREEN}",
	"{LIME}",
	"{LIGHTGREEN}",
	"{LIGHTRED}",
	"{GRAY}",
	"{LIGHTOLIVE}",
	"{OLIVE}",
	"{LIGHTBLUE}",
	"{BLUE}",
	"{PURPLE}",
	"{GRAYBLUE}",
	"\\n"
};

const std::string colors_hex[] = {
	"\x01",
	"\x02",
	"\x03",
	"\x04",
	"\x05",
	"\x06",
	"\x07",
	"\x08",
	"\x09",
	"\x10",
	"\x0B",
	"\x0C",
	"\x0E",
	"\x0A",
	"\xe2\x80\xa9"
};

#endif //_INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
