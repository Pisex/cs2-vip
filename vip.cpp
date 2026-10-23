#include <stdio.h>
#include "vip.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"

VIP g_VIP;
PLUGIN_EXPOSE(VIP, g_VIP);
IVEngineServer2* engine = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;

std::map<std::string, std::map<std::string,std::string>> g_VipGroups;
std::map<uint32, VipPlayer> g_VipPlayer;
std::map<std::string, VIPFunctions> g_VipFunctions;

std::map<std::string, std::string> g_pKVUser[64];
std::map<std::string, std::string> g_ClientParams[64];

void ResetClientCache(int iSlot, bool bParams)
{
	if (iSlot < 0 || iSlot >= 64) return;
	g_pKVUser[iSlot].clear();
	if (bParams) g_ClientParams[iSlot].clear();
}

bool g_bPistolRound;
int m_iServerID;

IMySQLClient *g_pMysqlClient;
IMySQLConnection* g_pConnection;

IUtilsApi* g_pUtils;
IMenusApi* g_pMenus;
ICookiesApi* g_pCookies;
IPlayersApi* g_pPlayers;

std::map<std::string, std::string> g_vecPhrases;

KeyValues* g_hKVData;

VIPApi* g_pVIPApi = nullptr;
IVIPApi* g_pVIPCore = nullptr;


bool containsOnlyDigits(const std::string& str) {
	return !str.empty() && str.find_first_not_of("0123456789") == std::string::npos;
}

bool TryParseInt64(const char* str, long long& out) {
	if (!str || !str[0] || !containsOnlyDigits(str))
		return false;
	try {
		size_t pos = 0;
		out = std::stoll(str, &pos);
		return pos == std::strlen(str);
	} catch (const std::exception&) {
		return false;
	}
}

const char* SafeClientName(int iSlot)
{
	const char* szName = engine->GetClientConVarValue(iSlot, "name");
	return szName ? szName : "";
}

void VIPApi::VIP_PrintToCenter(int Slot, const char *msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);
	va_end(args);

	g_pUtils->PrintToCenter(Slot, buf);
}

std::map<std::string, std::string> GetGroupKV(int iSlot)
{
    if (iSlot < 0 || iSlot >= 64) return {};
    CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
    if (!pController) return {};
    uint32 m_steamID = pController->m_steamID();
    if(m_steamID == 0) return {};

    auto vipGroup = g_VipPlayer.find(m_steamID);
    if (vipGroup == g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(iSlot))
        return {};
    
    if(g_pKVUser[iSlot].size() > 0)
        return g_pKVUser[iSlot];

    VipPlayer& player = vipGroup->second;
    if (player.TimeEnd <= std::time(0) && player.TimeEnd != 0)
        return {};

    auto groupIt = g_VipGroups.find(player.sGroup);
    if (groupIt == g_VipGroups.end() || groupIt->second.empty())
        return {};
    std::map<std::string, std::string> vipPlayer = groupIt->second;

    auto it = vipPlayer.find("legacy");
    if (it != vipPlayer.end() && !it->second.empty())
    {
        std::set<std::string> visited;
        visited.insert(player.sGroup);
        std::string sLegacy = it->second;
        while (!sLegacy.empty()) {
            if (!visited.insert(sLegacy).second)
                break;
            auto legacyIt = g_VipGroups.find(sLegacy);
            if (legacyIt == g_VipGroups.end() || legacyIt->second.empty())
                break;
            const std::map<std::string, std::string>& vipLegacy = legacyIt->second;
            for (auto& [key, value] : vipLegacy)
                if (vipPlayer[key].empty()) vipPlayer[key] = value;
            auto nextIt = vipLegacy.find("legacy");
            sLegacy = (nextIt != vipLegacy.end()) ? nextIt->second : std::string();
        }
    }
    for (auto& [key, value] : g_ClientParams[iSlot])
    {
        if (value.empty()) vipPlayer.erase(key);
        else vipPlayer[key] = value;
    }
    g_pKVUser[iSlot] = vipPlayer;
    return vipPlayer;
}

const char *VIPApi::VIP_GetTranslate(const char* phrase)
{
    return g_vecPhrases[std::string(phrase)].c_str();
}

bool LoadVips()
{
	g_VipGroups.clear();
	for (int i = 0; i < 64; i++) g_pKVUser[i].clear();
	KeyValues* pKVVips = new KeyValues("VIP");
	
	if (!pKVVips->LoadFromFile(g_pFullFileSystem, "addons/configs/vip/groups.ini"))
	{
		g_pUtils->ErrorLog("[%s] Failed to load vip config 'addons/configs/vip/groups.ini'", g_PLAPI->GetLogTag());
		return false;
	}
	int iServerID = std::atoi(g_pUtils->GetServerID());
	m_iServerID = pKVVips->GetInt("server_id", iServerID);
	for (KeyValues* pKey = pKVVips->GetFirstSubKey(); pKey; pKey = pKey->GetNextKey())
	{
		const char* sGroup = pKey->GetName();
		std::map<std::string,std::string> group;
		FOR_EACH_VALUE(pKey, pValue)
		{
			group[pValue->GetName()] = pValue->GetString(nullptr, nullptr);
		}
		g_VipGroups[std::string(sGroup)] = group;
	}
	return true;
}

CON_COMMAND_F(vip_reload, "reloads list of vip groups", FCVAR_NONE)
{	
	if (LoadVips())
	{
		ConColorMsg({ 0, 255, 0, 255 }, "VIP groups has been successfully updated\n");
	}
}

CON_COMMAND_F(mm_reload_vip, "check player vip", FCVAR_NONE)
{
	if (args.ArgC() > 1 && args[1][0])
	{
		bool bFound = false;
		CCSPlayerController* pController = nullptr;
		int iSlot = 0;
		long long llArg = 0;
		bool bIsNum = TryParseInt64(args[1], llArg);
		for (int i = 0; i < 64; i++)
		{
			pController = CCSPlayerController::FromSlot(i);
			if (!pController)
				continue;
			uint32 m_steamID = pController->m_steamID();
			if(m_steamID == 0)
				continue;
			if(strstr(pController->m_iszPlayerName(), args[1]) || (bIsNum && m_steamID == (uint32)llArg) || (bIsNum && llArg == i) || (bIsNum && (uint64)llArg == engine->GetClientXUID(i)))
			{
				bFound = true;
				iSlot = i;
				break;
			}
		}
		if(bFound)
		{
			uint32 m_steamID = pController->m_steamID();
			auto vipGroup = g_VipPlayer.find(m_steamID);
			if (vipGroup != g_VipPlayer.end())
				g_VipPlayer.erase(vipGroup);
			g_pKVUser[iSlot].clear();
			if(!g_pConnection)
			{
				META_CONPRINT("[VIP] Database is not connected\n");
				return;
			}
			char szQuery[256];
			g_SMAPI->Format(szQuery, sizeof(szQuery), "SELECT `group`, `expires` FROM `vip_users` WHERE `account_id` = %d AND `sid` = %d;", m_steamID, m_iServerID);
			g_pConnection->Query(szQuery, [iSlot, m_steamID](ISQLQuery* test)
			{
				if(!test) return;
				auto results = test->GetResultSet();
				if(results && results->FetchRow())
				{
					CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
					bool bStillHere = pController && pController->m_steamID() == m_steamID;

					VipPlayer& player = g_VipPlayer[m_steamID];
					const char* pszGroup = results->GetString(0);
					player.sGroup = pszGroup ? pszGroup : "";
					player.TimeEnd = results->GetInt(1);
					char szQuery[256];
					g_pVIPApi->Call_VIP_OnClientLoaded(iSlot, g_pVIPCore->VIP_IsClientVIP(iSlot));
					if(player.TimeEnd > std::time(0) || player.TimeEnd == 0)
					{
						if(g_pVIPCore->VIP_IsValidVIPGroup(player.sGroup.c_str()) && bStillHere)
						{
							const char* szName = engine->GetClientConVarValue(iSlot, "name");
							g_SMAPI->Format(szQuery, sizeof(szQuery), "UPDATE vip_users SET name = '%s', lastvisit = %i  WHERE account_id = '%d' AND `sid` = %i;", g_pConnection->Escape(szName ? szName : "").c_str(), std::time(0), m_steamID, m_iServerID);
							if(player.TimeEnd == 0) 
								g_pUtils->PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("WelcomePerm"), pController->m_iszPlayerName());
							else
							{
								time_t currentTime_t = static_cast<time_t>(player.TimeEnd);
								char buffer[80];
								std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&currentTime_t));
								g_pUtils->PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("Welcome"), pController->m_iszPlayerName(), buffer);
							}
						}
						else
							szQuery[0] = '\0';
					}
					else
						g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM vip_users WHERE account_id = '%d' AND `sid` = %i;", m_steamID, m_iServerID);
					if(szQuery[0])
						g_pConnection->Query(szQuery, [](ISQLQuery* test){});
				}
				else g_pVIPApi->Call_VIP_OnClientLoaded(iSlot, false);
			});
		}
		else META_CONPRINT("[VIP] Player not found\n");
	}
	else META_CONPRINT("[VIP] Usage: mm_reload_vip <userid|nickname|accountid>\n");
}

CON_COMMAND_F(vip_remove, "remove player vip", FCVAR_NONE)
{	
	if (args.ArgC() > 1 && args[1][0])
	{
		bool bFound = false;
		int iSlot = 0; 
		long long llArg = 0;
		bool bIsNum = TryParseInt64(args[1], llArg);
		for (int i = 0; i < 64; i++)
		{
			CCSPlayerController* pController = CCSPlayerController::FromSlot(i);
			if (!pController)
				continue;
			uint32 m_steamID = pController->m_steamID();
			if(m_steamID == 0)
				continue;
			if(strstr(pController->m_iszPlayerName(), args[1]) || (bIsNum && m_steamID == (uint32)llArg) || (bIsNum && llArg == i))
			{
				bFound = true;
				iSlot = i;
				break;
			}
		}
		if(bFound)
		{
			if(!g_pVIPCore->VIP_IsClientVIP(iSlot))
				META_CONPRINT("[VIP] The player has no VIP status\n");
			else
			{
				g_pVIPCore->VIP_RemoveClientVIP(iSlot, 1);
				META_CONPRINT("[VIP] You have successfully removed the player's VIP status\n");
			}
		}
		else META_CONPRINT("[VIP] Player not found\n");
	}
	else META_CONPRINT("[VIP] Usage: vip_remove <userid|nickname|accountid>\n");
}

CON_COMMAND_F(vip_give, "give player vip", FCVAR_NONE)
{	
	if (args.ArgC() > 3 && args[1][0] && containsOnlyDigits(args[2]) && args[3][0])
	{
		bool bFound = false;
		int iSlot = 0;
		long long llArg = 0;
		bool bIsNum = TryParseInt64(args[1], llArg);
		for (int i = 0; i < 64; i++)
		{
			CCSPlayerController* pController = CCSPlayerController::FromSlot(i);
			if (!pController)
				continue;
			uint32 m_steamID = pController->m_steamID();
			if(m_steamID == 0)
				continue;

			if(!pController->GetPlayerPawn() || !pController->m_hPawn())
				continue;
			if(strstr(pController->m_iszPlayerName(), args[1]) || (bIsNum && m_steamID == (uint32)llArg) || (bIsNum && llArg == i))
			{
				bFound = true;
				iSlot = i;
				break;
			}
		}
		if(bFound)
		{
			if(g_pVIPCore->VIP_IsClientVIP(iSlot))
				META_CONPRINT("[VIP] The player already has VIP status\n");
			else
			{
				g_pVIPCore->VIP_GiveClientVIP(iSlot, atoi(args[2]), args[3], true);
				META_CONPRINT("[VIP] You have successfully granted VIP status\n");
			}
		}
		else if(std::strlen(args[1]) >= 9 && std::strlen(args[1]) <= 11)
		{
			if(!g_pConnection)
			{
				META_CONPRINT("[VIP] Database is not connected\n");
				return;
			}
			char szQuery[256];
			std::string sAccount = g_pConnection->Escape(args[1]);
			std::string sGroup = g_pConnection->Escape(args[3]);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `vip_users` (`account_id`, `name`, `lastvisit`, `sid`, `group`, `expires`) VALUES ('%s', '%s', '%i', '%i', '%s', '%i');", sAccount.c_str(), "none", std::time(0), m_iServerID, sGroup.c_str(), atoi(args[2]) != 0?std::time(0)+atoi(args[2]):0);
			g_pConnection->Query(szQuery, [](ISQLQuery* test){});
			META_CONPRINT("[VIP] You have successfully granted VIP status\n");
		}
		else META_CONPRINT("[VIP] Player not found\n");
	}
	else META_CONPRINT("[VIP] Usage: vip_give <userid|nickname|accountid> <time_second> <group>\n");
}

const char* VIPApi::VIP_GetClientCookie(int iSlot, const char* sCookieName)
{
	if(g_pCookies) {
		char szCookie[256];
		g_SMAPI->Format(szCookie, sizeof(szCookie), "%s.%s", g_pVIPCore->VIP_GetClientVIPGroup(iSlot), sCookieName);
		return g_pCookies->GetCookie(iSlot, szCookie);
	} else {
		CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
		if (!pController) return "";
		uint32 m_steamID = pController->m_steamID();
		if(m_steamID == 0) return "";
		if(!g_hKVData) return "";
		KeyValues *hData = g_hKVData->FindKey(std::to_string(m_steamID).c_str(), false);
		if(!hData) return "";
		const char* sValue = hData->GetString(sCookieName);
		return sValue;
	}
}

bool VIPApi::VIP_SetClientCookie(int iSlot, const char* sCookieName, const char* sData)
{
	if(g_pCookies) {
		char szCookie[256];
		g_SMAPI->Format(szCookie, sizeof(szCookie), "%s.%s", g_pVIPCore->VIP_GetClientVIPGroup(iSlot), sCookieName);
		g_pCookies->SetCookie(iSlot, szCookie, sData);
		return true;
	} else {
		CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
		if (!pController) return false;
		uint32 m_steamID = pController->m_steamID();
		if(m_steamID == 0) return false;
		if(!g_hKVData) return false;

		KeyValues *hData = g_hKVData->FindKey(std::to_string(m_steamID).c_str(), true);
		if(!hData) return false;
		hData->SetString(sCookieName, sData);
		g_hKVData->SaveToFile(g_pFullFileSystem, "addons/data/vip_data.ini");
		return true;
	}
	return false;
}

bool LoadVIPData()
{
	g_hKVData = new KeyValues("Data");

	const char *pszPath = "addons/data/vip_data.ini";

	if (!g_hKVData->LoadFromFile(g_pFullFileSystem, pszPath))
	{
		g_pUtils->ErrorLog("[%s] Failed to load vip config 'addons/data/vip_data.ini'", g_PLAPI->GetLogTag());
		return false;
	}

	return true;
}

void* VIP::OnMetamodQuery(const char* iface, int* ret)
{
	if (!strcmp(iface, VIP_INTERFACE))
	{
		*ret = META_IFACE_OK;
		return g_pVIPCore;
	}

	*ret = META_IFACE_FAILED;
	return nullptr;
}

bool VIP::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameClients, IServerGameClients, SOURCE2GAMECLIENTS_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);

	g_SMAPI->AddListener( this, this );

	ConVar_Register(FCVAR_GAMEDLL);

	g_pVIPApi = new VIPApi();
	g_pVIPCore = g_pVIPApi;

	return true;
}

void VIP::OnPluginUnload(PluginId id)
{
	if (g_pVIPApi) g_pVIPApi->RemovePluginHooks(id);
}

bool VIP::Unload(char *error, size_t maxlen)
{
	ConVar_Unregister();

	if (g_pConnection)
		g_pConnection->Destroy();
	
	return true;
}

void VIP::ClientPutInServer(int iSlot)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController) return;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0) return;
	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup != g_VipPlayer.end())
	{
		VipPlayer& player = vipGroup->second;
		if(g_pVIPCore->VIP_IsValidVIPGroup(player.sGroup.c_str()))
		{
			if(player.TimeEnd == 0) 
				g_pUtils->PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("WelcomePerm"), SafeClientName(iSlot));
			else
			{
				time_t currentTime_t = static_cast<time_t>(player.TimeEnd);
				char buffer[80];
				std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&currentTime_t));
				g_pUtils->PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("Welcome"), SafeClientName(iSlot), buffer);
			}
		}
	}
}

void VIP::MapStartHook(const char* szMap)
{
	static bool bDone = false;
	if (!bDone)
	{
		g_pGameEntitySystem = GameEntitySystem();
		g_pEntitySystem = g_pUtils->GetCEntitySystem();
		bDone = true;
	}
	
	if(g_pConnection)
	{
		char szQuery[256];
		g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `vip_users` WHERE `sid` = %i AND `expires` < %i AND `expires` <> 0;", m_iServerID, std::time(0));
		g_pConnection->Query(szQuery, [](ISQLQuery* test){});
	}
}

void VIP::GameFrame(bool simulating, bool bFirstTick, bool bLastTick)
{
	if(g_iLastTime == 0) g_iLastTime = std::time(0);
	else if(std::time(0) - g_iLastTime >= 1)
	{
		g_iLastTime = std::time(0);
		for (int i = 0; i < 64; i++)
		{
			if(g_pPlayers->IsFakeClient(i)) continue;
			if(!g_pPlayers->IsAuthenticated(i)) continue;
			if(!g_pPlayers->IsConnected(i)) continue;
			if(!g_pPlayers->IsInGame(i)) continue;
			if(!g_pPlayers->GetSteamID(i)) continue;
			uint32 m_steamID = g_pPlayers->GetSteamID(i)->GetStaticAccountKey();
			if(m_steamID == 0) continue;
			auto vipGroup = g_VipPlayer.find(m_steamID);
			if (vipGroup == g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(i))
				continue;
			VipPlayer& player = vipGroup->second;
			if(player.TimeEnd < std::time(0) && player.TimeEnd != 0)
			{
				g_VipPlayer.erase(vipGroup);
				ResetClientCache(i, true);
				g_pVIPApi->Call_VIP_OnVIPClientRemoved(i, 1);
				g_pUtils->PrintToChat(i, g_pVIPCore->VIP_GetTranslate("VIPExpired1"));
				g_pUtils->PrintToChat(i, g_pVIPCore->VIP_GetTranslate("VIPExpired2"));
				g_pUtils->PrintToChat(i, g_pVIPCore->VIP_GetTranslate("VIPExpired3"));
				
				if(g_pConnection)
				{
					char szQuery[256];
					g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `vip_users` WHERE `account_id` = '%d' AND `sid` = %i;", m_steamID, m_iServerID);
					g_pConnection->Query(szQuery, [this](ISQLQuery* test){});
				}
			}
		}
	}
}

void OnPlayerSpawn(const char* szName, IGameEvent* event, bool bDontBroadcast)
{	
	CBasePlayerController* pPlayerController = static_cast<CBasePlayerController*>(event->GetPlayerController("userid"));
	if (!pPlayerController || pPlayerController->m_steamID() == 0) // Ignore bots
		return;

	g_pUtils->NextFrame([hPlayerController = CHandle<CBasePlayerController>(pPlayerController), pPlayerSlot = event->GetPlayerSlot("userid")]()
	{
		CCSPlayerController* pPlayerController = static_cast<CCSPlayerController*>(hPlayerController.Get());
		if (!pPlayerController)
			return;

		CCSPlayerPawnBase* pPlayerPawn = pPlayerController->m_hPlayerPawn();
		if (!pPlayerPawn || pPlayerPawn->m_lifeState() != LIFE_ALIVE)
			return;

		int iSlot = pPlayerSlot.Get();
		if (iSlot < 0 || iSlot >= 64)
			return;
		bool isVIP = g_pVIPCore->VIP_IsClientVIP(iSlot);
		g_pVIPApi->Call_VIP_OnPlayerSpawn(iSlot, pPlayerPawn->m_iTeamNum(), isVIP);
	});
}

void OnRoundPreStart(const char* szName, IGameEvent* pEvent, bool bDontBroadcast)
{
	if(g_pUtils->GetCCSGameRules()) g_bPistolRound = g_pUtils->GetCCSGameRules()->m_totalRoundsPlayed() == 0 || (g_pUtils->GetCCSGameRules()->m_bSwitchingTeamsAtRoundReset() && g_pUtils->GetCCSGameRules()->m_nOvertimePlaying() == 0) || g_pUtils->GetCCSGameRules()->m_bGameRestart();
}

bool VIPApi::VIP_WarmupPeriod()
{
	auto pGameRules = g_pUtils->GetCCSGameRules();
	if (!pGameRules) return false;
	return pGameRules->m_bWarmupPeriod();
}

bool VIPApi::VIP_PistolRound()
{
	return g_bPistolRound;
}

int VIPApi::VIP_GetClientAccessTime(int iSlot)
{
	if(g_pPlayers->IsFakeClient(iSlot)) return -1;
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController) return -1;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0) return -1;
	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup == g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(iSlot))
		return -1;

	VipPlayer& player = vipGroup->second;
	if(player.TimeEnd <= std::time(0) && player.TimeEnd != 0) return -1;

	return player.TimeEnd;
}

bool VIPApi::VIP_SetClientAccessTime(int iSlot, int iTime, bool bInDB)
{
	if(g_pPlayers->IsFakeClient(iSlot)) return false;
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController) return false;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0) return false;
	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup == g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(iSlot))
		return false;

	VipPlayer& player = vipGroup->second;
	player.TimeEnd = iTime;

	if(bInDB && g_pConnection)
	{
		char szQuery[256];
		g_SMAPI->Format(szQuery, sizeof(szQuery), "UPDATE `vip_users` SET `expires` = %i  WHERE `account_id` = '%d' AND `sid` = %i;", iTime, m_steamID, m_iServerID);
		g_pConnection->Query(szQuery, [this](ISQLQuery* test){});
	}
	return true;
}

bool VIPApi::VIP_GiveClientVIP(int iSlot, int iTime, const char* szGroup, bool bAddToDB)
{
	if(g_pPlayers->IsFakeClient(iSlot)) return false;
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController) return false;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0) return false;
	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup != g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(iSlot)) return false;

	VipPlayer& player = g_VipPlayer[m_steamID];
	ResetClientCache(iSlot, true);
	player.sGroup = std::string(szGroup);
	player.TimeEnd = iTime != 0?std::time(0)+iTime:0;

	if(bAddToDB && g_pConnection)
	{
		char szQuery[256];
		std::string sGroup = g_pConnection->Escape(szGroup);
		g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `vip_users` (`account_id`, `name`, `lastvisit`, `sid`, `group`, `expires`) VALUES ('%d', '%s', '%i', '%i', '%s', '%i');", m_steamID, g_pConnection->Escape(SafeClientName(iSlot)).c_str(), std::time(0), m_iServerID, sGroup.c_str(), iTime != 0?std::time(0)+iTime:0);
		g_pConnection->Query(szQuery, [this](ISQLQuery* test){});
	}
	if(player.TimeEnd == 0) g_pUtils->PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("WelcomePerm"), SafeClientName(iSlot));
	else
	{
		time_t currentTime_t = (time_t)player.TimeEnd;
		char buffer[80];
    	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&currentTime_t));
		g_pUtils->PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("Welcome"), SafeClientName(iSlot), buffer);
	}
	g_pVIPApi->Call_VIP_OnClientLoaded(iSlot, true);
	g_pVIPApi->Call_VIP_OnVIPClientAdded(iSlot);
	return true;
}

bool VIPApi::VIP_RemoveClientVIP(int iSlot, bool bNotify, bool bInDB)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController) return false;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0) return false;
	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup == g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(iSlot))
		return false;

	g_VipPlayer.erase(vipGroup);
	ResetClientCache(iSlot, true);
	if(bInDB && g_pConnection)
	{
		char szQuery[256];
		g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `vip_users` WHERE `account_id` = '%d' AND `sid` = %i;", m_steamID, m_iServerID);
		g_pConnection->Query(szQuery, [this](ISQLQuery* test){});
	}
	if(bNotify)
	{
		g_pUtils->PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("VIPExpired1"));
		g_pUtils->PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("VIPExpired2"));
		g_pUtils->PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("VIPExpired3"));
	}
	g_pVIPApi->Call_VIP_OnVIPClientRemoved(iSlot, 2);
	g_pVIPApi->Call_VIP_OnClientDisconnect(iSlot, false);
	return true;
}

bool VIPApi::VIP_SetClientVIPGroup(int iSlot, const char* szGroup, bool bInDB)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController) return false;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0) return false;
	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup == g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(iSlot))
		return false;
	
	VipPlayer& player = vipGroup->second;

	if(player.TimeEnd <= std::time(0) && player.TimeEnd != 0)
		return false;

	auto grpIt = g_VipGroups.find(std::string(szGroup));
	if(grpIt == g_VipGroups.end() || grpIt->second.empty())
		return false;

	player.sGroup = std::string(szGroup);
	ResetClientCache(iSlot, true);

	if(bInDB && g_pConnection)
	{
		char szQuery[256];
		g_SMAPI->Format(szQuery, sizeof(szQuery), "UPDATE `vip_users` SET `group` = '%s'  WHERE `account_id` = '%d' AND `sid` = %i;", g_pConnection->Escape(szGroup).c_str(), m_steamID, m_iServerID);
		g_pConnection->Query(szQuery, [this](ISQLQuery* test){});
	}
	return true;
}

bool VIPApi::VIP_IsClientVIP(int iSlot)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController) return false;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0) return false;
	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup == g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(iSlot))
		return false;

	VipPlayer& player = vipGroup->second;
	if(player.TimeEnd <= std::time(0) && player.TimeEnd != 0) return false;

	auto groupIt = g_VipGroups.find(player.sGroup);
	if (groupIt == g_VipGroups.end() || groupIt->second.empty())
		return false;
	return true;
}

int VIPApi::VIP_GetClientFeatureInt(int iSlot, const char* szFeature)
{
	std::map<std::string,std::string> Group = GetGroupKV(iSlot);
	if (Group.empty())
		return -1;
	const char* sCookie = VIP_GetClientCookie(iSlot, szFeature);
	if(strlen(sCookie) == 0 || atoi(sCookie) != 0)
		return Group[szFeature] != ""?atoi(Group[szFeature].c_str()):-1;
	return -1;
}

bool VIPApi::VIP_GetClientFeatureBool(int iSlot, const char* szFeature)
{
	std::map<std::string,std::string> Group = GetGroupKV(iSlot);
	if (Group.empty())
		return false;
	const char* sCookie = VIP_GetClientCookie(iSlot, szFeature);
	if(strlen(sCookie) == 0 || atoi(sCookie) != 0)
		return Group[szFeature] != ""?atoi(Group[szFeature].c_str()):false;
	return false;
}

float VIPApi::VIP_GetClientFeatureFloat(int iSlot, const char* szFeature)
{
	std::map<std::string,std::string> Group = GetGroupKV(iSlot);
	if (Group.empty())
		return 1.f;
	const char* sCookie = VIP_GetClientCookie(iSlot, szFeature);
	if(strlen(sCookie) == 0 || atoi(sCookie) != 0)
		return Group[szFeature] != ""?atof(Group[szFeature].c_str()):1.f;
	return 1.f;
}

const char* VIPApi::VIP_GetClientFeatureString(int iSlot, const char* szFeature)
{
	std::map<std::string,std::string> Group = GetGroupKV(iSlot);
	if (Group.empty())
		return "";
	const char* sCookie = VIP_GetClientCookie(iSlot, szFeature);
	if(strlen(sCookie) == 0 || atoi(sCookie) != 0)
	{
		auto it = g_pKVUser[iSlot].find(szFeature);
		return it != g_pKVUser[iSlot].end() ? it->second.c_str() : "";
	}
	return "";
}

const char* VIPApi::VIP_GetClientVIPGroup(int iSlot)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController) return "";
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0) return "";

	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup == g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(iSlot))
		return "";
	
	return vipGroup->second.sGroup.c_str();
}

CGameEntitySystem* VIPApi::VIP_GetEntitySystem()
{
	return g_pGameEntitySystem;
}

int VIPApi::VIP_GetTotalRounds()
{
	auto pGameRules = g_pUtils->GetCCSGameRules();
	if (!pGameRules) return -1;
	return pGameRules->m_totalRoundsPlayed();
}

void VIPApi::VIP_RegisterFeature(const char* szFeature, VIP_ValueType eValType, VIP_FeatureType eType, ItemSelectableCallback Item_select_callback, ItemTogglableCallback Item_togglable_callback, ItemDisplayCallback Item_display_callback)
{
	VIPFunctions& vip_func = g_VipFunctions[std::string(szFeature)];
	vip_func.eValType = eValType;
	vip_func.eType = eType;
	vip_func.Select_callback = Item_select_callback;
	vip_func.Togglable_callback = Item_togglable_callback;
	vip_func.Display_callback = Item_display_callback;
	vip_func.bRegistered = true;
}

bool VIPApi::VIP_IsValidVIPGroup(const char* szGroup)
{
	auto it = g_VipGroups.find(szGroup);
	return (it != g_VipGroups.end() && !it->second.empty());
}

void VIP::OnClientAuthorized(int iSlot, uint64 iSteamID64)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController) return;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0) return;
	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup != g_VipPlayer.end())
		g_VipPlayer.erase(vipGroup);
	ResetClientCache(iSlot, true);
	if(!g_pConnection) return;
	char szQuery[256];
	g_SMAPI->Format(szQuery, sizeof(szQuery), "SELECT `group`, `expires` FROM `vip_users` WHERE `account_id` = %d AND `sid` = %d;", m_steamID, m_iServerID);
	g_pConnection->Query(szQuery, [iSlot, m_steamID](ISQLQuery* test)
	{
		if(!test) return;
		auto results = test->GetResultSet();
		if(results && results->FetchRow())
		{
			CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
			bool bStillHere = pController && pController->m_steamID() == m_steamID;

			VipPlayer& player = g_VipPlayer[m_steamID];
			const char* pszGroup = results->GetString(0);
			player.sGroup = pszGroup ? pszGroup : "";
			player.TimeEnd = results->GetInt(1);
			char szQuery[256];
			g_pVIPApi->Call_VIP_OnClientLoaded(iSlot, g_pVIPCore->VIP_IsClientVIP(iSlot));
			if(player.TimeEnd <= std::time(0) && player.TimeEnd != 0)
				g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM vip_users WHERE account_id = '%d' AND `sid` = %i;", m_steamID, m_iServerID);
			else if(bStillHere)
			{
				const char* szName = engine->GetClientConVarValue(iSlot, "name");
				g_SMAPI->Format(szQuery, sizeof(szQuery), "UPDATE vip_users SET name = '%s', lastvisit = %i  WHERE account_id = '%d' AND `sid` = %i;", g_pConnection->Escape(szName ? szName : "").c_str(), std::time(0), m_steamID, m_iServerID);
			}
			else
				szQuery[0] = '\0';
			if(szQuery[0])
				g_pConnection->Query(szQuery, [](ISQLQuery* test){});
		}
		else g_pVIPApi->Call_VIP_OnClientLoaded(iSlot, false);
	});
}

void VIP::ClientDisconnect(int iSlot)
{
	g_pVIPApi->Call_VIP_OnClientDisconnect(iSlot, g_pVIPCore->VIP_IsClientVIP(iSlot));
	ResetClientCache(iSlot, true);
}

void ShowVIPMenu(int iSlot, bool bReopen);


static VIPFunctions* FindFeature(const char* szFeature)
{
	if (!szFeature) return nullptr;
	auto it = g_VipFunctions.find(szFeature);
	if (it == g_VipFunctions.end() || !it->second.bRegistered) return nullptr;
	return &it->second;
}

static bool IsMenuFeature(const std::string& sKey, const VIPFunctions* pFunc, bool bIncludeHidden)
{
	if (!pFunc || sKey == "legacy") return false;
	if (pFunc->eValType == VIP_NULL) return false;
	return bIncludeHidden || pFunc->eType != HIDE;
}

static bool IsFeatureEnabledByCookie(int iSlot, const char* szFeature)
{
	const char* sCookie = g_pVIPCore->VIP_GetClientCookie(iSlot, szFeature);
	return !sCookie || strlen(sCookie) == 0 || atoi(sCookie) != 0;
}

static VIP_ToggleState GetFeatureStatus(int iSlot, const char* szFeature)
{
	if (!szFeature) return NO_ACCESS;
	std::map<std::string, std::string> Group = GetGroupKV(iSlot);
	auto it = Group.find(szFeature);
	if (it == Group.end() || it->second.empty()) return NO_ACCESS;
	VIPFunctions* pFunc = FindFeature(szFeature);
	if (pFunc && pFunc->eType == DROPDOWN) return ENABLED;
	return IsFeatureEnabledByCookie(iSlot, szFeature) ? ENABLED : DISABLED;
}

static std::string DropdownCookie(const char* szFeature)
{
	return std::string(szFeature) + "_opt";
}

static std::vector<VIP_FeatureOption> GetFeatureOptions(int iSlot, const char* szFeature)
{
	VIPFunctions* pFunc = FindFeature(szFeature);
	if (!pFunc || pFunc->eType != DROPDOWN) return {};
	if (GetFeatureStatus(iSlot, szFeature) == NO_ACCESS) return {};
	if (pFunc->Options_callback) return pFunc->Options_callback(iSlot, szFeature);

	std::vector<VIP_FeatureOption> vecOptions;
	std::map<std::string, std::string> Group = GetGroupKV(iSlot);
	const std::string& sValue = Group[szFeature];
	size_t iStart = 0;
	while (iStart <= sValue.size())
	{
		size_t iEnd = sValue.find(';', iStart);
		if (iEnd == std::string::npos) iEnd = sValue.size();
		std::string sPart = sValue.substr(iStart, iEnd - iStart);
		size_t b = sPart.find_first_not_of(" \t"), e = sPart.find_last_not_of(" \t");
		sPart = b == std::string::npos ? "" : sPart.substr(b, e - b + 1);
		if (!sPart.empty())
		{
			VIP_FeatureOption opt;
			opt.sValue = sPart;
			const char* szTrans = g_pVIPCore->VIP_GetTranslate((std::string(szFeature) + "_" + sPart).c_str());
			opt.sName = (szTrans && szTrans[0]) ? szTrans : sPart;
			vecOptions.push_back(std::move(opt));
		}
		iStart = iEnd + 1;
	}
	return vecOptions;
}

static int GetFeatureOption(int iSlot, const char* szFeature, const std::vector<VIP_FeatureOption>& vecOptions)
{
	if (vecOptions.empty()) return -1;
	const char* szCookie = g_pVIPCore->VIP_GetClientCookie(iSlot, DropdownCookie(szFeature).c_str());
	std::string sSaved = szCookie ? szCookie : "";
	if (sSaved.empty())
		sSaved = g_pVIPCore->VIP_GetClientParam(iSlot, szFeature);
	for (int i = 0; i < (int)vecOptions.size(); i++)
		if (vecOptions[i].sValue == sSaved) return i;
	return 0;
}

static bool ApplyFeatureOption(int iSlot, const char* szFeature, int iOption, bool bCallback)
{
	VIPFunctions* pFunc = FindFeature(szFeature);
	if (!pFunc || pFunc->eType != DROPDOWN) return false;
	std::vector<VIP_FeatureOption> vecOptions = GetFeatureOptions(iSlot, szFeature);
	if (vecOptions.empty() || iOption < 0 || iOption >= (int)vecOptions.size()) return false;
	int iOld = GetFeatureOption(iSlot, szFeature, vecOptions);
	int iNew = iOption;
	if (bCallback && pFunc->Dropdown_callback && pFunc->Dropdown_callback(iSlot, szFeature, iOld, iNew))
		return false;
	if (iNew < 0 || iNew >= (int)vecOptions.size() || iNew == iOld) return false;
	g_pVIPCore->VIP_SetClientCookie(iSlot, DropdownCookie(szFeature).c_str(), vecOptions[iNew].sValue.c_str());
	return true;
}

static bool ApplyFeatureToggle(int iSlot, const char* szFeature, bool bEnable, bool bCallback)
{
	VIPFunctions* pFunc = FindFeature(szFeature);
	if (!pFunc || pFunc->eType == SELECTABLE || pFunc->eType == DROPDOWN) return false;
	if (GetFeatureStatus(iSlot, szFeature) == NO_ACCESS) return false;

	bool bOld = IsFeatureEnabledByCookie(iSlot, szFeature);
	VIP_ToggleState eOld = bOld ? ENABLED : DISABLED;
	VIP_ToggleState eNew = bEnable ? ENABLED : DISABLED;
	bool bBlock = false;
	if (bCallback && pFunc->Togglable_callback)
		bBlock = pFunc->Togglable_callback(iSlot, szFeature, eOld, eNew);

	bool bResult = bBlock ? bOld : (eNew == ENABLED);
	g_pVIPCore->VIP_SetClientCookie(iSlot, szFeature, bResult ? "1" : "0");
	return bResult != bOld;
}

static bool PressFeature(int iSlot, const char* szFeature)
{
	VIPFunctions* pFunc = FindFeature(szFeature);
	if (!pFunc) return false;
	if (GetFeatureStatus(iSlot, szFeature) == NO_ACCESS) return false;
	if (pFunc->eType == SELECTABLE)
	{
		if (pFunc->Select_callback) return pFunc->Select_callback(iSlot, szFeature);
		return false;
	}
	if (pFunc->eType == DROPDOWN)
	{
		std::vector<VIP_FeatureOption> vecOptions = GetFeatureOptions(iSlot, szFeature);
		if (!vecOptions.empty())
			ApplyFeatureOption(iSlot, szFeature, (GetFeatureOption(iSlot, szFeature, vecOptions) + 1) % (int)vecOptions.size(), true);
		return false;
	}
	ApplyFeatureToggle(iSlot, szFeature, !IsFeatureEnabledByCookie(iSlot, szFeature), true);
	return false;
}

static std::string GetFeatureName(const char* szFeature)
{
	const char* szTrans = g_pVIPCore->VIP_GetTranslate(szFeature);
	return (szTrans && szTrans[0]) ? szTrans : szFeature;
}

static std::string BuildFeatureDisplay(int iSlot, const char* szFeature, const VIPFunctions& vip_func)
{
	if (vip_func.Display_callback)
	{
		std::string szDisplay = vip_func.Display_callback(iSlot, szFeature);
		if (!szDisplay.empty()) return szDisplay;
	}
	std::string sName = GetFeatureName(szFeature);
	if (vip_func.eType == SELECTABLE) return sName;
	if (vip_func.eType == DROPDOWN)
	{
		std::vector<VIP_FeatureOption> vecOptions = GetFeatureOptions(iSlot, szFeature);
		int iOption = GetFeatureOption(iSlot, szFeature, vecOptions);
		return iOption >= 0 ? sName + " [" + vecOptions[iOption].sName + "]" : sName;
	}

	const char* szValue = g_pVIPCore->VIP_GetClientFeatureString(iSlot, szFeature);
	char sBuff[128];
	g_SMAPI->Format(sBuff, sizeof(sBuff), "%s [%s]", sName.c_str(), strlen(szValue)?vip_func.eValType == VIP_BOOL?g_pVIPCore->VIP_GetTranslate("On"):szValue:g_pVIPCore->VIP_GetTranslate("Off"));
	return sBuff;
}

std::vector<VIP_MenuItem> VIPApi::VIP_GetClientMenuItems(int iSlot, bool bIncludeHidden)
{
	std::vector<VIP_MenuItem> vecItems;
	if (iSlot < 0 || iSlot >= 64) return vecItems;
	std::map<std::string, std::string> Group = GetGroupKV(iSlot);
	for (auto& [key, value] : Group)
	{
		VIPFunctions* pFunc = FindFeature(key.c_str());
		if (!IsMenuFeature(key, pFunc, bIncludeHidden)) continue;
		VIP_MenuItem item;
		item.sFeature = key;
		item.sName = GetFeatureName(key.c_str());
		item.sValue = value;
		item.sDisplay = BuildFeatureDisplay(iSlot, key.c_str(), *pFunc);
		item.eValType = pFunc->eValType;
		item.eType = pFunc->eType;
		item.eState = GetFeatureStatus(iSlot, key.c_str());
		if (item.eType == DROPDOWN)
		{
			item.vecOptions = GetFeatureOptions(iSlot, key.c_str());
			item.iOption = GetFeatureOption(iSlot, key.c_str(), item.vecOptions);
		}
		vecItems.push_back(std::move(item));
	}
	return vecItems;
}

std::string VIPApi::VIP_GetClientFeatureDisplay(int iSlot, const char* szFeature)
{
	VIPFunctions* pFunc = FindFeature(szFeature);
	if (!pFunc || GetFeatureStatus(iSlot, szFeature) == NO_ACCESS) return "";
	return BuildFeatureDisplay(iSlot, szFeature, *pFunc);
}

VIP_ToggleState VIPApi::VIP_GetClientFeatureStatus(int iSlot, const char* szFeature)
{
	if (iSlot < 0 || iSlot >= 64) return NO_ACCESS;
	return GetFeatureStatus(iSlot, szFeature);
}

bool VIPApi::VIP_SetClientFeatureStatus(int iSlot, const char* szFeature, VIP_ToggleState eStatus, bool bCallback)
{
	if (iSlot < 0 || iSlot >= 64 || eStatus == NO_ACCESS) return false;
	return ApplyFeatureToggle(iSlot, szFeature, eStatus == ENABLED, bCallback);
}

bool VIPApi::VIP_PressClientFeature(int iSlot, const char* szFeature)
{
	if (iSlot < 0 || iSlot >= 64) return false;
	return PressFeature(iSlot, szFeature);
}

std::vector<std::string> VIPApi::VIP_GetFeatures()
{
	std::vector<std::string> vecFeatures;
	for (auto& [key, func] : g_VipFunctions)
		if (func.bRegistered) vecFeatures.push_back(key);
	return vecFeatures;
}

bool VIPApi::VIP_IsFeatureRegistered(const char* szFeature)
{
	return FindFeature(szFeature) != nullptr;
}

VIP_ValueType VIPApi::VIP_GetFeatureValueType(const char* szFeature)
{
	VIPFunctions* pFunc = FindFeature(szFeature);
	return pFunc ? pFunc->eValType : VIP_NULL;
}

VIP_FeatureType VIPApi::VIP_GetFeatureType(const char* szFeature)
{
	VIPFunctions* pFunc = FindFeature(szFeature);
	return pFunc ? pFunc->eType : HIDE;
}

void VIPApi::VIP_UnregisterFeature(const char* szFeature)
{
	if (!szFeature) return;
	g_VipFunctions.erase(szFeature);
}

std::map<std::string, std::string> VIPApi::VIP_GetClientParams(int iSlot)
{
	std::map<std::string, std::string> Group = GetGroupKV(iSlot);
	Group.erase("legacy");
	return Group;
}

std::string VIPApi::VIP_GetClientParam(int iSlot, const char* szParam)
{
	if (!szParam) return "";
	std::map<std::string, std::string> Group = GetGroupKV(iSlot);
	auto it = Group.find(szParam);
	return it != Group.end() ? it->second : "";
}

bool VIPApi::VIP_SetClientParam(int iSlot, const char* szParam, const char* szValue)
{
	if (iSlot < 0 || iSlot >= 64 || !szParam || !szParam[0]) return false;
	if (!VIP_IsClientVIP(iSlot)) return false;
	g_ClientParams[iSlot][szParam] = szValue ? szValue : "";
	g_pKVUser[iSlot].clear();
	return true;
}

void VIPApi::VIP_ResetClientParams(int iSlot)
{
	ResetClientCache(iSlot, true);
}

std::vector<std::string> VIPApi::VIP_GetGroups()
{
	std::vector<std::string> vecGroups;
	for (auto& [key, value] : g_VipGroups)
		if (!value.empty()) vecGroups.push_back(key);
	return vecGroups;
}

std::map<std::string, std::string> VIPApi::VIP_GetGroupParams(const char* szGroup)
{
	if (!szGroup) return {};
	auto it = g_VipGroups.find(szGroup);
	return it != g_VipGroups.end() ? it->second : std::map<std::string, std::string>();
}

void VIPApi::VIP_RegisterDropdownFeature(const char* szFeature, VIP_ValueType eValType, ItemOptionsCallback Item_options_callback, ItemDropdownCallback Item_dropdown_callback, ItemDisplayCallback Item_display_callback)
{
	if (!szFeature || !szFeature[0]) return;
	VIPFunctions& vip_func = g_VipFunctions[std::string(szFeature)];
	vip_func.eValType = eValType == VIP_NULL ? VIP_STRING : eValType;
	vip_func.eType = DROPDOWN;
	vip_func.Select_callback = nullptr;
	vip_func.Togglable_callback = nullptr;
	vip_func.Display_callback = Item_display_callback;
	vip_func.Options_callback = Item_options_callback;
	vip_func.Dropdown_callback = Item_dropdown_callback;
	vip_func.bRegistered = true;
}

std::vector<VIP_FeatureOption> VIPApi::VIP_GetClientFeatureOptions(int iSlot, const char* szFeature)
{
	if (iSlot < 0 || iSlot >= 64) return {};
	return GetFeatureOptions(iSlot, szFeature);
}

int VIPApi::VIP_GetClientFeatureOption(int iSlot, const char* szFeature)
{
	if (iSlot < 0 || iSlot >= 64) return -1;
	return GetFeatureOption(iSlot, szFeature, GetFeatureOptions(iSlot, szFeature));
}

std::string VIPApi::VIP_GetClientFeatureOptionValue(int iSlot, const char* szFeature)
{
	if (iSlot < 0 || iSlot >= 64) return "";
	std::vector<VIP_FeatureOption> vecOptions = GetFeatureOptions(iSlot, szFeature);
	int iOption = GetFeatureOption(iSlot, szFeature, vecOptions);
	return iOption >= 0 ? vecOptions[iOption].sValue : "";
}

bool VIPApi::VIP_SetClientFeatureOption(int iSlot, const char* szFeature, int iOption, bool bCallback)
{
	if (iSlot < 0 || iSlot >= 64) return false;
	return ApplyFeatureOption(iSlot, szFeature, iOption, bCallback);
}

static void RequestVIPMenu(int iSlot)
{
	if (iSlot < 0 || iSlot >= 64) return;
	if (g_pPlayers->IsFakeClient(iSlot)) return;
	if (g_pVIPApi->Call_VIP_OnMenuOpen(iSlot)) return;
	ShowVIPMenu(iSlot, true);
}

void VIPApi::VIP_OpenMenu(int iSlot) {
	RequestVIPMenu(iSlot);
}

void VIPApi::VIP_OpenDefaultMenu(int iSlot) {
	if (iSlot < 0 || iSlot >= 64) return;
	ShowVIPMenu(iSlot, true);
}

void VIPToggleCallback(const char* szBack, bool bState, int iItem, int iSlot)
{
	ApplyFeatureToggle(iSlot, szBack, bState, true);
}

void VIPSelectCallback(const char* szBack, const char* szOptionBack, int iOption, int iItem, int iSlot)
{
	ApplyFeatureOption(iSlot, szBack, iOption, true);
	if (g_pVIPCore->VIP_GetClientFeatureOption(iSlot, szBack) == iOption) return;
	g_pUtils->NextFrame([iSlot]() {
		if (g_pMenus->IsMenuOpen(iSlot)) ShowVIPMenu(iSlot, false);
	});
}

static void ShowVIPDropdownMenu(int iSlot, const std::string& sFeature)
{
	std::vector<VIP_FeatureOption> vecOptions = GetFeatureOptions(iSlot, sFeature.c_str());
	if (vecOptions.empty()) { ShowVIPMenu(iSlot, false); return; }
	int iSel = GetFeatureOption(iSlot, sFeature.c_str(), vecOptions);

	Menu hMenu;
	g_pMenus->SetTitleMenu(hMenu, GetFeatureName(sFeature.c_str()).c_str());
	for (int i = 0; i < (int)vecOptions.size(); i++)
	{
		std::string sText = vecOptions[i].sName;
		if (i == iSel) sText += " [+]";
		g_pMenus->AddItemMenu(hMenu, std::to_string(i).c_str(), sText.c_str(), i == iSel ? ITEM_DISABLED : ITEM_DEFAULT);
	}
	g_pMenus->SetBackMenu(hMenu, true);
	g_pMenus->SetExitMenu(hMenu, true);
	g_pMenus->SetCallback(hMenu, [sFeature](const char* szBack, const char* szFront, int iItem, int iSlot) {
		if (iItem < 7)
		{
			ApplyFeatureOption(iSlot, sFeature.c_str(), atoi(szBack), true);
			ShowVIPDropdownMenu(iSlot, sFeature);
		}
		else if (iItem == 7) ShowVIPMenu(iSlot, true);
	});
	g_pMenus->DisplayPlayerMenu(hMenu, iSlot, true, true);
}

void VIPCallback(const char* szBack, const char* szFront, int iItem, int iSlot)
{
	if(iItem < 7)
	{
		VIPFunctions* pFunc = FindFeature(szBack);
		if (!pFunc) return;
		if(pFunc->eType == DROPDOWN)
			ShowVIPDropdownMenu(iSlot, szBack);
		else if(pFunc->eType != SELECTABLE)
		{
			PressFeature(iSlot, szBack);
			ShowVIPMenu(iSlot, false);
		}
		else if(PressFeature(iSlot, szBack))
		{
			g_pUtils->NextFrame([iSlot](){
				g_pMenus->ClosePlayerMenu(iSlot);
			});
		}
	}
}

void ShowVIPMenu(int iSlot, bool bReopen)
{
	if(g_pPlayers->IsFakeClient(iSlot)) return;
	
	std::vector<VIP_MenuItem> vecItems = g_pVIPCore->VIP_GetClientMenuItems(iSlot, false);
	if (vecItems.empty() && !g_pVIPCore->VIP_IsClientVIP(iSlot)) {
		g_pUtils->PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("NotAccess"));
		return;
	}

	Menu hMenu;
	g_pMenus->SetTitleMenu(hMenu, g_pVIPCore->VIP_GetTranslate("MenuTitle"));

	bool bHud = g_pMenus->GetMenuType(iSlot) == MenuType::HUD_LAYOUT;
	for (auto& item : vecItems) {
		if(bHud && item.eType == DROPDOWN && !item.vecOptions.empty())
		{
			ItemRef hItem = g_pMenus->AddSelectMenu(hMenu, item.sFeature.c_str(), item.sName.c_str(), item.iOption < 0 ? 0 : item.iOption, VIPSelectCallback);
			for (auto& opt : item.vecOptions)
				g_pMenus->AddSelectOption(hItem, opt.sValue.c_str(), opt.sName.c_str());
		}
		else if(bHud && item.eType == TOGGLABLE)
			g_pMenus->AddToggleMenu(hMenu, item.sFeature.c_str(), item.sName.c_str(), item.eState == ENABLED, VIPToggleCallback);
		else
			g_pMenus->AddItemMenu(hMenu, item.sFeature.c_str(), item.sDisplay.c_str());
	}

	g_pMenus->SetBackMenu(hMenu, false);
	g_pMenus->SetExitMenu(hMenu, true);
	g_pMenus->SetCallback(hMenu, VIPCallback);
	g_pMenus->DisplayPlayerMenu(hMenu, iSlot, true, bReopen);
}

bool OnVIPCommand(int iSlot, const char* szContent)
{
	RequestVIPMenu(iSlot);
	return false;
}

void OnClientCookiesLoaded(int iSlot)
{
	g_pVIPApi->Call_VIP_OnClientLoaded(iSlot, g_pVIPCore->VIP_IsClientVIP(iSlot));
}

void VIP::AllPluginsLoaded()
{
	char error[64] = { 0 };
	int ret;
	g_pUtils = (IUtilsApi *)g_SMAPI->MetaFactory(UTILS_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		V_strncpy(error, "Missing Utils system plugin", 64);
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}

	g_pMenus = (IMenusApi *)g_SMAPI->MetaFactory(MENUS_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_pUtils->ErrorLog("[%s] Missing Menus system plugin", g_PLAPI->GetLogTag());
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	
	g_pPlayers = (IPlayersApi *)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_pUtils->ErrorLog("[%s] Missing Players system plugin", g_PLAPI->GetLogTag());
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}

	g_pCookies = (ICookiesApi *)g_SMAPI->MetaFactory(COOKIES_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
		g_pCookies = nullptr;
	else {
		g_pCookies->HookClientCookieLoaded(g_PLID, OnClientCookiesLoaded);
	}

	ISQLInterface* g_SqlInterface = (ISQLInterface *)g_SMAPI->MetaFactory(SQLMM_INTERFACE, &ret, nullptr);
	if (ret == META_IFACE_FAILED) {
		g_pUtils->ErrorLog("[%s] Missing MYSQL plugin", g_PLAPI->GetLogTag());
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	g_pMysqlClient = g_SqlInterface->GetMySQLClient();

	{
		KeyValues* g_kvPhrases = new KeyValues("Phrases");
		const char *pszPath = "addons/translations/vip.phrases.txt";

		if (!g_kvPhrases->LoadFromFile(g_pFullFileSystem, pszPath))
		{
			g_pUtils->ErrorLog("[%s] Failed to load %s", g_PLAPI->GetLogTag(), pszPath);
			return;
		}

		const char* g_pszLanguage = g_pUtils->GetLanguage();
		for (KeyValues *pKey = g_kvPhrases->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey())
			g_vecPhrases[std::string(pKey->GetName())] = std::string(pKey->GetString(g_pszLanguage));
	}

	KeyValues* pKVConfigRoot = new KeyValues("Databases");

	if (!pKVConfigRoot->LoadFromFile(g_pFullFileSystem, "addons/configs/databases.cfg")) {
		g_pUtils->ErrorLog("[%s] Failed to load databases config 'addons/config/databases.cfg'", g_PLAPI->GetLogTag());
		ConColorMsg(Color(255, 0, 0, 255), "[VIP] %s\n", error);
		return;
	}

	KeyValues* pKVConfig = pKVConfigRoot->FindKey("vip", false);
	if (!pKVConfig) {
		g_pUtils->ErrorLog("[%s] No databases.cfg 'vip'", g_PLAPI->GetLogTag());
		return;
	}

	MySQLConnectionInfo info;
	info.host = pKVConfig->GetString("host", nullptr);
	info.user = pKVConfig->GetString("user", nullptr);
	info.pass = pKVConfig->GetString("pass", nullptr);
	info.database = pKVConfig->GetString("database", nullptr);
	info.port = pKVConfig->GetInt("port");
	g_pConnection = g_pMysqlClient->CreateMySQLConnection(info);
	if (!g_pConnection) {
		g_pUtils->ErrorLog("[%s] Failed to create MySQL connection", g_PLAPI->GetLogTag());
		return;
	}

	g_pConnection->Connect([this](bool connect) {
		if (!connect) {
			META_CONPRINT("Failed to connect the mysql database\n");
		} else {
			g_pConnection->Query("CREATE TABLE IF NOT EXISTS `vip_users` (\
`account_id` INT NOT NULL, \
`name` VARCHAR(64) NOT NULL default 'unknown' COLLATE 'utf8mb4_unicode_ci', \
`lastvisit` INT UNSIGNED NOT NULL default 0, \
`sid` INT UNSIGNED NOT NULL, \
`group` VARCHAR(64) NOT NULL, \
`expires` INT UNSIGNED NOT NULL default 0, \
CONSTRAINT pk_PlayerID PRIMARY KEY (`account_id`, `sid`) \
) DEFAULT CHARSET=utf8mb4;", [this](ISQLQuery* test) {});
			g_pVIPApi->Call_VIP_OnVIPLoaded();
			g_pVIPApi->SetReady(true);
		}
	});
	g_pUtils->RegCommand(g_PLID, {"mm_vip", "sm_vip"}, {"!vip"}, OnVIPCommand);
	g_pUtils->HookEvent(g_PLID, "player_spawn", OnPlayerSpawn);
	g_pUtils->HookEvent(g_PLID, "round_prestart", OnRoundPreStart);
	g_pPlayers->AddListener(g_PLID, this);
	g_pUtils->AddServerListener(g_PLID, this);
	LoadVips();
	LoadVIPData();
}

///////////////////////////////////////
const char* VIP::GetLicense()
{
	return "GPL";
}

const char* VIP::GetVersion()
{
	return "1.2.5f";
}

const char* VIP::GetDate()
{
	return __DATE__;
}

const char *VIP::GetLogTag()
{
	return "VIP";
}

const char* VIP::GetAuthor()
{
	return "Pisex";
}

const char* VIP::GetDescription()
{
	return "[VIP] Core";
}

const char* VIP::GetName()
{
	return "[VIP] Core";
}

const char* VIP::GetURL()
{
	return "https://discord.gg/g798xERK5Y";
}