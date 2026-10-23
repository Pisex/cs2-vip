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
#include "utils.hpp"
#include <utlstring.h>
#include <keyvalues.h>
#include "CCSPlayerController.h"
#include "CGameRules.h"
#include "module.h"
#include "include/vip.h"
#include "include/menus.h"
#include "include/utils.h"
#include "include/players.h"
#include "include/cookies.h"
#include "include/mysql_mm.h"
#include <map>
#include <set>
#include <ctime>
#include <chrono>
#include <array>
#include <thread>

class VIP final : public ISmmPlugin, public IMetamodListener, public IServerListener, public IPlayerListener
{
public:
	bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late);
	bool Unload(char* error, size_t maxlen);
	void AllPluginsLoaded();
	void* OnMetamodQuery(const char* iface, int* ret);
	void OnPluginUnload(PluginId id);
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
	void GameFrame(bool bSimulating, bool bFirstTick, bool bLastTick) override;
    void ClientPutInServer(int iSlot) override;
    void ClientDisconnect(int iSlot) override;
    void MapStartHook(const char* szMap) override;
    void OnClientAuthorized(int iSlot, uint64 iSteamID64) override;
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
    std::map<int, std::vector<VIPMenuOpenCallbackFunc>> vipOnMenuOpen;
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

    void VIP_OpenMenu(int iSlot);

    void VIP_OnMenuOpen(SourceMM::PluginId id, VIPMenuOpenCallbackFunc callback) override {
        vipOnMenuOpen[id].push_back(callback);
    }
    void VIP_OpenDefaultMenu(int iSlot) override;
    std::vector<std::string> VIP_GetFeatures() override;
    bool VIP_IsFeatureRegistered(const char* szFeature) override;
    VIP_ValueType VIP_GetFeatureValueType(const char* szFeature) override;
    VIP_FeatureType VIP_GetFeatureType(const char* szFeature) override;
    void VIP_UnregisterFeature(const char* szFeature) override;
    std::vector<VIP_MenuItem> VIP_GetClientMenuItems(int iSlot, bool bIncludeHidden) override;
    std::string VIP_GetClientFeatureDisplay(int iSlot, const char* szFeature) override;
    VIP_ToggleState VIP_GetClientFeatureStatus(int iSlot, const char* szFeature) override;
    bool VIP_SetClientFeatureStatus(int iSlot, const char* szFeature, VIP_ToggleState eStatus, bool bCallback) override;
    bool VIP_PressClientFeature(int iSlot, const char* szFeature) override;
    std::map<std::string, std::string> VIP_GetClientParams(int iSlot) override;
    std::string VIP_GetClientParam(int iSlot, const char* szParam) override;
    bool VIP_SetClientParam(int iSlot, const char* szParam, const char* szValue) override;
    void VIP_ResetClientParams(int iSlot) override;
    std::vector<std::string> VIP_GetGroups() override;
    std::map<std::string, std::string> VIP_GetGroupParams(const char* szGroup) override;
    void VIP_RegisterDropdownFeature(const char* szFeature, VIP_ValueType eValType, ItemOptionsCallback Item_options_callback, ItemDropdownCallback Item_dropdown_callback, ItemDisplayCallback Item_display_callback) override;
    std::vector<VIP_FeatureOption> VIP_GetClientFeatureOptions(int iSlot, const char* szFeature) override;
    int VIP_GetClientFeatureOption(int iSlot, const char* szFeature) override;
    std::string VIP_GetClientFeatureOptionValue(int iSlot, const char* szFeature) override;
    bool VIP_SetClientFeatureOption(int iSlot, const char* szFeature, int iOption, bool bCallback) override;
public:
    // true - открытие меню отменено
    bool Call_VIP_OnMenuOpen(int iSlot) {
        bool bBlock = false;
        for (auto& item : vipOnMenuOpen)
            for (auto& callback : item.second)
                if (callback && callback(iSlot))
                    bBlock = true;
        return bBlock;
    }
    void RemovePluginHooks(SourceMM::PluginId id) {
        vipOnMenuOpen.erase(id);
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
    VIP_ValueType           eValType = VIP_NULL;
    VIP_FeatureType	        eType = TOGGLABLE;
    ItemSelectableCallback	Select_callback;
    ItemTogglableCallback	Togglable_callback;
    ItemDisplayCallback		Display_callback;
    ItemOptionsCallback		Options_callback;
    ItemDropdownCallback	Dropdown_callback;
    bool                    bRegistered = false;
};

#endif //_INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
