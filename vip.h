#ifndef _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
#define _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_

#include <ISmmPlugin.h>
#include <igameevents.h>
#include <iplayerinfo.h>
#include "utlvector.h"
#include "ehandle.h"
#include <sh_vector.h>
#include <entity2/entitysystem.h>
#include "sdk/utils.hpp"
#include "module.h"
#include "CCSPlayerController.h"
#include "CGameRules.h"
#include "iserver.h"
#include "include/vip.h"
#include "include/menus.h"

#include <ctime>


#include "vector.h"
#include <deque>
#include <functional>
#include <utlstring.h>
#include <KeyValues.h>

#include "include/mysql_mm.h"

#include <map>

#include <chrono>
#include <array>
#include <thread>

class VIP final : public ISmmPlugin, public IMetamodListener
{
public:
	bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late);
	bool Unload(char* error, size_t maxlen);
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
	void StartupServer(const GameSessionConfiguration_t& config, ISource2WorldSession*, const char*);
	void GameFrame(bool simulating, bool bFirstTick, bool bLastTick);
	void OnClientPutInServer(CPlayerSlot slot, char const* pszName, int type, uint64 xuid);
	void OnClientDisconnect( CPlayerSlot slot, ENetworkDisconnectionReason reason, const char *pszName, uint64 xuid, const char *pszNetworkID );
	int g_iLastTime;
};

class VIPApi : public IVIPApi {
	bool bReady;
    std::vector<ReadyCallbackFunc> vipOnVIPLoadeds;
    std::vector<SpawnCallbackFunc> vipOnPlayerSpawn;
    std::vector<ClientLoadedOrDisconnectCallbackFunc> vipOnClientLoaded;
    std::vector<ClientLoadedOrDisconnectCallbackFunc> vipOnClientDisconnect;
    std::vector<VIPAddCallbackFunc> vipOnVIPClientAdded;
    std::vector<VIPRemoveCallbackFunc> vipOnVIPClientRemoved;
    bool VIP_IsVIPLoaded() override {
		return bReady;
	}
	bool VIP_IsClientVIP(int iSlot);
	int VIP_GetClientAccessTime(int iSlot);

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

    void VIP_PrintToCenter(int iSlot, const char* msg, ...);

    bool VIP_SetClientCookie(int iSlot, const char* sCookieName, const char* sData);
    const char *VIP_GetClientCookie(int iSlot, const char* sCookieName);
	
    void VIP_RegisterFeature(const char*			    szFeature,
								VIP_ValueType			eValType,
								VIP_FeatureType			eType,
								ItemSelectableCallback	Select_callback,
								ItemTogglableCallback	Togglable_callback,
                                ItemDisplayCallback		Item_display_callback);

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
    void VIP_OnVIPLoaded(ReadyCallbackFunc callback) override {
        vipOnVIPLoadeds.push_back(callback);
    }

public:
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

class CEntityListener : public IEntityListener
{
	void OnEntitySpawned(CEntityInstance* pEntity) override;
};

struct VipPlayer
{
	std::string sGroup;
	int TimeEnd;
};

struct VIPFunctions
{
    VIP_ValueType           eValType;
    VIP_FeatureType	        eType;
    ItemSelectableCallback	Select_callback;
    ItemTogglableCallback	Togglable_callback;
    ItemDisplayCallback		Display_callback;
};

#endif //_INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
