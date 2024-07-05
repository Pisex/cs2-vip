#include <stdio.h>
#include "vip.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"

VIP g_VIP;
PLUGIN_EXPOSE(VIP, g_VIP);
IVEngineServer2* engine = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
CCSGameRules* g_pGameRules = nullptr;

CGameEntitySystem* GameEntitySystem()
{
	g_pGameEntitySystem = *reinterpret_cast<CGameEntitySystem**>(reinterpret_cast<uintptr_t>(g_pGameResourceServiceServer) + WIN_LINUX(0x58, 0x50));
	return g_pGameEntitySystem;
}

std::map<std::string, KeyValues*> g_VipGroups;
std::map<uint32, VipPlayer> g_VipPlayer;
std::map<std::string, VIPFunctions> g_VipFunctions;

KeyValues* pKVVips;

bool g_bPistolRound;
int m_iServerID;

IMySQLClient *g_pMysqlClient;
IMySQLConnection* g_pConnection;

IUtilsApi* g_pUtils;
IMenusApi* g_pMenus;

std::map<std::string, std::string> g_vecPhrases;

KeyValues* g_hKVData;

VIPApi* g_pVIPApi = nullptr;
IVIPApi* g_pVIPCore = nullptr;

class GameSessionConfiguration_t { };
SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK4_void(IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0, CPlayerSlot, char const*, int, uint64);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot, ENetworkDisconnectionReason, const char *, uint64, const char *);
SH_DECL_HOOK3_void(INetworkServerService, StartupServer, SH_NOATTRIB, 0, const GameSessionConfiguration_t&, ISource2WorldSession*, const char*);

void (*UTIL_ClientPrint)(CBasePlayerController *player, int msg_dest, const char* msg_name, const char* param1, const char* param2, const char* param3, const char* param4) = nullptr;

bool containsOnlyDigits(const std::string& str) {
	return str.find_first_not_of("0123456789") == std::string::npos;
}

void VIPApi::VIP_PrintToCenter(int Slot, const char *msg, ...)
{
	CCSPlayerController* pPlayerController =  CCSPlayerController::FromSlot(Slot);
	if (!pPlayerController)
		return;
	uint32 m_steamID = pPlayerController->m_steamID();
	if(m_steamID == 0)
		return;

	va_list args;
	va_start(args, msg);

	char buf[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);
	va_end(args);

	UTIL_ClientPrint(pPlayerController, 4, buf, nullptr, nullptr, nullptr, nullptr);
}

KeyValues* GetGroupKV(int iSlot)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController)
		return nullptr;

	uint32 m_steamID = pController->m_steamID();
	if (m_steamID == 0)
		return nullptr;

	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup == g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(iSlot))
		return nullptr;

	VipPlayer& player = vipGroup->second;
	if (player.TimeEnd <= std::time(0) && player.TimeEnd != 0)
		return nullptr;

	auto vipPlayer = g_VipGroups[player.sGroup];
	return vipPlayer;
}

const char *VIPApi::VIP_GetTranslate(const char* phrase)
{
    return g_vecPhrases[std::string(phrase)].c_str();
}

CON_COMMAND_F(vip_reload, "reloads list of vip groups", FCVAR_NONE)
{	
	char szError[256];
	if (g_VIP.LoadVips(szError, sizeof(szError)))
	{
		ConColorMsg({ 0, 255, 0, 255 }, "VIP groups has been successfully updated\n");
	}
	else
	{
		ConColorMsg({ 255, 0, 0, 255 }, "Reload error: %s\n", szError);
	}
}

CON_COMMAND_F(vip_remove, "remove player vip", FCVAR_NONE)
{	
	if (args.ArgC() > 1 && args[1][0])
	{
		bool bFound = false;
		int iSlot = 0; 
		for (int i = 0; i < 64; i++)
		{
			CCSPlayerController* pController = CCSPlayerController::FromSlot(i);
			if (!pController)
				continue;
			uint32 m_steamID = pController->m_steamID();
			if(m_steamID == 0)
				continue;
			if(strstr(pController->m_iszPlayerName(), args[1]) || (containsOnlyDigits(args[1]) && m_steamID == std::stoll(args[1])) || (containsOnlyDigits(args[1]) && std::stoll(args[1]) == i))
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
		for (int i = 0; i < 64; i++)
		{
			CCSPlayerController* pController = CCSPlayerController::FromSlot(i);
			if (!pController)
				continue;
			uint32 m_steamID = pController->m_steamID();
			if(m_steamID == 0)
				continue;
			if(strstr(pController->m_iszPlayerName(), args[1]) || (containsOnlyDigits(args[1]) && m_steamID == std::stoll(args[1])) || (containsOnlyDigits(args[1]) && std::stoll(args[1]) == i))
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
				g_pVIPCore->VIP_GiveClientVIP(iSlot, std::stoi(args[2]), args[3], true);
				META_CONPRINT("[VIP] You have successfully granted VIP status\n");
			}
		}
		else META_CONPRINT("[VIP] Player not found\n");
	}
	else META_CONPRINT("[VIP] Usage: vip_give <userid|nickname|accountid> <time_second> <group>\n");
}

const char* VIPApi::VIP_GetClientCookie(int iSlot, const char* sCookieName)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController)
		return "";
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0)
		return "";
	KeyValues *hData = g_hKVData->FindKey(std::to_string(m_steamID).c_str(), false);
	if(!hData) return "";
	const char* sValue = hData->GetString(sCookieName);
	return sValue;
}

bool VIPApi::VIP_SetClientCookie(int iSlot, const char* sCookieName, const char* sData)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController)
		return false;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0)
		return false;

	KeyValues *hData = g_hKVData->FindKey(std::to_string(m_steamID).c_str(), true);
	hData->SetString(sCookieName, sData);
	g_hKVData->SaveToFile(g_pFullFileSystem, "addons/data/vip_data.ini");
	return true;
}

bool VIP::LoadVIPData(char* error, size_t maxlen)
{
	g_hKVData = new KeyValues("Data");

	const char *pszPath = "addons/data/vip_data.ini";

	if (!g_hKVData->LoadFromFile(g_pFullFileSystem, pszPath))
	{
		V_strncpy(error, "Failed to load vip config 'addons/data/vip_data.ini'", maxlen);
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

	if (!g_VIP.LoadVips(error, maxlen))
	{
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		
		return false;
	}

	if (!g_VIP.LoadVIPData(error, maxlen))
	{
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		
		return false;
	}

	{
		KeyValues* g_kvPhrases = new KeyValues("Phrases");
		const char *pszPath = "addons/translations/vip.phrases.txt";

		if (!g_kvPhrases->LoadFromFile(g_pFullFileSystem, pszPath))
		{
			Warning("Failed to load %s\n", pszPath);
			return false;
		}

		const char* g_pszLanguage = g_kvPhrases->GetString("language", "en");
		for (KeyValues *pKey = g_kvPhrases->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey())
			g_vecPhrases[std::string(pKey->GetName())] = std::string(pKey->GetString(g_pszLanguage));
	}

	DynLibUtils::CModule libserver(g_pSource2Server);
	UTIL_ClientPrint = libserver.FindPattern(WIN_LINUX("48 85 C9 0F 84 2A 2A 2A 2A 48 8B C4 48 89 58 18", "55 48 89 E5 41 57 49 89 CF 41 56 49 89 D6 41 55 41 89 F5 41 54 4C 8D A5 A0 FE FF FF")).RCast< decltype(UTIL_ClientPrint) >();
	if (!UTIL_ClientPrint)
	{
		V_strncpy(error, "Failed to find function to get UTIL_ClientPrint", maxlen);
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return false;
	}

	g_SMAPI->AddListener( this, this );

	SH_ADD_HOOK(IServerGameDLL, GameFrame, g_pSource2Server, SH_MEMBER(this, &VIP::GameFrame), true);
	SH_ADD_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &VIP::StartupServer), true);
	SH_ADD_HOOK(IServerGameClients, ClientPutInServer, g_pSource2GameClients, SH_MEMBER(this, &VIP::OnClientPutInServer), true);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, ClientDisconnect, g_pSource2GameClients, this, &VIP::OnClientDisconnect, true);

	ConVar_Register(FCVAR_GAMEDLL);

	g_pVIPApi = new VIPApi();
	g_pVIPCore = g_pVIPApi;

	return true;
}

bool VIP::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK(IServerGameDLL, GameFrame, g_pSource2Server, SH_MEMBER(this, &VIP::GameFrame), true);
	SH_REMOVE_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &VIP::StartupServer), true);
    SH_REMOVE_HOOK(IServerGameClients, ClientPutInServer, g_pSource2GameClients, SH_MEMBER(this, &VIP::OnClientPutInServer), true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientDisconnect, g_pSource2GameClients, this, &VIP::OnClientDisconnect, true);

	ConVar_Unregister();

	if (g_pConnection)
		g_pConnection->Destroy();
	
	return true;
}

bool VIP::LoadVips(char* error, size_t maxlen)
{
	if(pKVVips)
	{
		g_VipGroups.clear();
		delete pKVVips;
	}
	pKVVips = new KeyValues("VIP");
	
	if (!pKVVips->LoadFromFile(g_pFullFileSystem, "addons/configs/vip/groups.ini"))
	{
		V_strncpy(error, "Failed to load vip config 'addons/configs/vip/groups.ini'", maxlen);
		return false;
	}
	m_iServerID = pKVVips->GetInt("server_id");
	for (KeyValues* pKey = pKVVips->GetFirstSubKey(); pKey; pKey = pKey->GetNextKey())
	{
		const char* sGroup = pKey->GetName();
		g_VipGroups[std::string(sGroup)] = pKey;
	}
	return true;
}

void VIP::StartupServer(const GameSessionConfiguration_t& config, ISource2WorldSession*, const char*)
{
	g_pGameRules = nullptr;

	static bool bDone = false;
	if (!bDone)
	{
    	g_pEntitySystem = GameEntitySystem();
		bDone = true;
	}
}

void VIP::GameFrame(bool simulating, bool bFirstTick, bool bLastTick)
{
	if (!g_pGameRules)
	{
		g_pGameRules = g_pUtils->GetCCSGameRules();
	}

	if(g_iLastTime == 0) g_iLastTime = std::time(0);
	else if(std::time(0) - g_iLastTime >= 1)
	{
		g_iLastTime = std::time(0);
		for (int i = 0; i < 64; i++)
		{
			CCSPlayerController* pPlayerController =  CCSPlayerController::FromSlot(i);
			if (!pPlayerController)
				continue;
			uint32 m_steamID = pPlayerController->m_steamID();
			if(m_steamID == 0)
				continue;
			auto vipGroup = g_VipPlayer.find(m_steamID);
			if (vipGroup == g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(i))
				continue;
			VipPlayer& player = vipGroup->second;
			if(player.TimeEnd < std::time(0) & player.TimeEnd != 0)
			{
				g_VipPlayer.erase(vipGroup);
				g_pVIPApi->Call_VIP_OnVIPClientRemoved(i, 1);
				g_pUtils->PrintToChat(i, g_pVIPCore->VIP_GetTranslate("VIPExpired1"));
				g_pUtils->PrintToChat(i, g_pVIPCore->VIP_GetTranslate("VIPExpired2"));
				g_pUtils->PrintToChat(i, g_pVIPCore->VIP_GetTranslate("VIPExpired3"));
				
				char szQuery[256];
				g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `vip_users` WHERE `account_id` = '%d' AND `sid` = %i;", m_steamID, m_iServerID);
				g_pConnection->Query(szQuery, [this](ISQLQuery* test){});
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

		bool isVIP = g_pVIPCore->VIP_IsClientVIP(pPlayerPawn->m_hController()->m_pEntity->m_EHandle.GetEntryIndex() - 1);
		g_pVIPApi->Call_VIP_OnPlayerSpawn(pPlayerSlot.Get(), pPlayerPawn->m_iTeamNum(), isVIP);
	});
}

void OnRoundPreStart(const char* szName, IGameEvent* pEvent, bool bDontBroadcast)
{
	if (g_pGameRules)
	{
		g_bPistolRound = g_pGameRules->m_totalRoundsPlayed() == 0 || (g_pGameRules->m_bSwitchingTeamsAtRoundReset() && g_pGameRules->m_nOvertimePlaying() == 0) || g_pGameRules->m_bGameRestart();
	}
}

bool VIPApi::VIP_WarmupPeriod()
{
	return g_pGameRules->m_bWarmupPeriod();
}

bool VIPApi::VIP_PistolRound()
{
	return g_bPistolRound;
}

int VIPApi::VIP_GetClientAccessTime(int iSlot)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController)
		return -1;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0)
		return -1;
	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup == g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(iSlot))
		return -1;

	VipPlayer& player = vipGroup->second;
	if(player.TimeEnd <= std::time(0) && player.TimeEnd != 0) return -1;

	return player.TimeEnd;
}

bool VIPApi::VIP_SetClientAccessTime(int iSlot, int iTime, bool bInDB)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController)
		return false;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0)
		return false;
	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup == g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(iSlot))
		return false;

	VipPlayer& player = vipGroup->second;
	player.TimeEnd = iTime;

	if(bInDB)
	{
		char szQuery[256];
		g_SMAPI->Format(szQuery, sizeof(szQuery), "UPDATE `vip_users` SET `expires` = %i  WHERE `account_id` = '%d' AND `sid` = %i;", iTime, m_steamID, m_iServerID);
		g_pConnection->Query(szQuery, [this](ISQLQuery* test){});
	}
	return true;
}

bool VIPApi::VIP_GiveClientVIP(int iSlot, int iTime, const char* szGroup, bool bAddToDB)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController)
		return false;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0)
		return false;
	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup != g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(iSlot)) return false;

	VipPlayer& player = g_VipPlayer[m_steamID];
	player.sGroup = std::string(szGroup);
	player.TimeEnd = iTime != 0?std::time(0)+iTime:0;

	if(bAddToDB)
	{
		char szQuery[256];
		g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `vip_users` (`account_id`, `name`, `lastvisit`, `sid`, `group`, `expires`) VALUES ('%d', '%s', '%i', '%i', '%s', '%i');", m_steamID, g_pConnection->Escape(engine->GetClientConVarValue(iSlot, "name")).c_str(), std::time(0), m_iServerID, szGroup, iTime != 0?std::time(0)+iTime:0);
		g_pConnection->Query(szQuery, [this](ISQLQuery* test){});
	}
	if(player.TimeEnd == 0) g_pUtils->PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("WelcomePerm"), pController->m_iszPlayerName());
	else
	{
		time_t currentTime_t = (time_t)player.TimeEnd;
		char buffer[80];
    	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&currentTime_t));
		g_pUtils->PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("Welcome"), pController->m_iszPlayerName(), buffer);
	}
	g_pVIPApi->Call_VIP_OnVIPClientAdded(iSlot);
	return true;
}

bool VIPApi::VIP_RemoveClientVIP(int iSlot, bool bNotify, bool bInDB)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController)
		return false;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0)
		return false;
	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup == g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(iSlot))
		return false;

	g_VipPlayer.erase(vipGroup);
	if(bInDB)
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
	return true;
}

bool VIPApi::VIP_SetClientVIPGroup(int iSlot, const char* szGroup, bool bInDB)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController)
		return false;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0)
		return false;
	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup == g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(iSlot))
		return false;
	
	VipPlayer& player = vipGroup->second;

	if(player.TimeEnd <= std::time(0) && player.TimeEnd != 0)
		return false;

	if(g_VipGroups[std::string(szGroup)] == NULL)
		return false;

	player.sGroup = std::string(szGroup);

	if(bInDB)
	{
		char szQuery[256];
		g_SMAPI->Format(szQuery, sizeof(szQuery), "UPDATE `vip_users` SET `group` = '%s'  WHERE `account_id` = '%d' AND `sid` = %i;", szGroup, m_steamID, m_iServerID);
		g_pConnection->Query(szQuery, [this](ISQLQuery* test){});
	}
	return true;
}

bool VIPApi::VIP_IsClientVIP(int iSlot)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController)
		return false;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0)
		return false;
	auto vipGroup = g_VipPlayer.find(m_steamID);
	if (vipGroup == g_VipPlayer.end() || !engine->IsClientFullyAuthenticated(iSlot))
		return false;

	VipPlayer& player = vipGroup->second;
	if(player.TimeEnd <= std::time(0) && player.TimeEnd != 0) return false;

	auto vipPlayer = g_VipGroups[player.sGroup];
	if (vipPlayer == NULL)
		return false;
	return true;
}

int VIPApi::VIP_GetClientFeatureInt(int iSlot, const char* szFeature)
{
	KeyValues* Group = GetGroupKV(iSlot);
	if (Group == NULL)
		return -1;
	const char* sCookie = VIP_GetClientCookie(iSlot, szFeature);
	if(strlen(sCookie) == 0 || std::stoi(sCookie) != 0)
		return Group->GetInt(szFeature, -1);
	return -1;
}

bool VIPApi::VIP_GetClientFeatureBool(int iSlot, const char* szFeature)
{
	KeyValues* Group = GetGroupKV(iSlot);
	if (Group == NULL)
		return false;
	const char* sCookie = VIP_GetClientCookie(iSlot, szFeature);
	if(strlen(sCookie) == 0 || std::stoi(sCookie) != 0)
		return Group->GetBool(szFeature, false);
	return false;
}

float VIPApi::VIP_GetClientFeatureFloat(int iSlot, const char* szFeature)
{
	KeyValues* Group = GetGroupKV(iSlot);
	if (Group == NULL)
		return 1.f;
	const char* sCookie = VIP_GetClientCookie(iSlot, szFeature);
	if(strlen(sCookie) == 0 || std::stoi(sCookie) != 0)
		return Group->GetFloat(szFeature, 1.f);
	return 1.f;
}

const char* VIPApi::VIP_GetClientFeatureString(int iSlot, const char* szFeature)
{
	KeyValues* Group = GetGroupKV(iSlot);
	if (Group == NULL)
		return "";
	const char* sCookie = VIP_GetClientCookie(iSlot, szFeature);
	if(strlen(sCookie) == 0 || std::stoi(sCookie) != 0)
		return Group->GetString(szFeature, "");
	return "";
}

const char* VIPApi::VIP_GetClientVIPGroup(int iSlot)
{
	KeyValues* Group = GetGroupKV(iSlot);
	if (Group == NULL)
		return "";
	return Group->GetName();
}

CGameEntitySystem* VIPApi::VIP_GetEntitySystem()
{
	return g_pGameEntitySystem;
}

int VIPApi::VIP_GetTotalRounds()
{
	return g_pGameRules->m_totalRoundsPlayed();
}

void VIPApi::VIP_RegisterFeature(const char* szFeature, VIP_ValueType eValType, VIP_FeatureType eType, ItemSelectableCallback Item_select_callback, ItemTogglableCallback Item_togglable_callback, ItemDisplayCallback Item_display_callback)
{
	VIPFunctions& vip_func = g_VipFunctions[std::string(szFeature)];
	vip_func.eValType = eValType;
	vip_func.eType = eType;
	vip_func.Select_callback = Item_select_callback;
	vip_func.Togglable_callback = Item_togglable_callback;
	vip_func.Display_callback = Item_display_callback;
}

bool VIPApi::VIP_IsValidVIPGroup(const char* szGroup)
{
	return g_VipGroups[szGroup] != NULL;
}

void VIP::OnClientPutInServer(CPlayerSlot slot, char const* pszName, int type, uint64 xuid)
{
	if (slot.Get() == -1)
    	return;

	CCSPlayerController* pPlayerController = CCSPlayerController::FromSlot(slot.Get());
	if (!pPlayerController)
		return;

	uint32 m_steamID = pPlayerController->m_steamID();

	char szQuery[256];
	g_SMAPI->Format(szQuery, sizeof(szQuery), "SELECT `group`, `expires` FROM `vip_users` WHERE `account_id` = %d AND `sid` = %d;", m_steamID, m_iServerID);
	g_pConnection->Query(szQuery, [slot, m_steamID, pPlayerController, this](ISQLQuery* test)
	{
		auto results = test->GetResultSet();
		if(results->FetchRow())
		{
			VipPlayer& player = g_VipPlayer[m_steamID];
			player.sGroup = results->GetString(0);
			player.TimeEnd = results->GetInt(1);
			char szQuery[256];
			g_pVIPApi->Call_VIP_OnClientLoaded(slot.Get(), g_pVIPCore->VIP_IsClientVIP(slot.Get()));
			if(player.TimeEnd > std::time(0) || player.TimeEnd == 0)
			{
				if(g_pVIPCore->VIP_IsValidVIPGroup(player.sGroup.c_str()))
				{
					g_SMAPI->Format(szQuery, sizeof(szQuery), "UPDATE vip_users SET name = '%s', lastvisit = %i  WHERE account_id = '%d' AND `sid` = %i;", g_pConnection->Escape(engine->GetClientConVarValue(slot, "name")).c_str(), std::time(0), m_steamID, m_iServerID);
					if(player.TimeEnd == 0) 
						g_pUtils->PrintToChat(slot.Get(), g_pVIPCore->VIP_GetTranslate("WelcomePerm"), pPlayerController->m_iszPlayerName());
					else
					{
						time_t currentTime_t = static_cast<time_t>(player.TimeEnd);
						char buffer[80];
						std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&currentTime_t));
						g_pUtils->PrintToChat(slot.Get(), g_pVIPCore->VIP_GetTranslate("Welcome"), pPlayerController->m_iszPlayerName(), buffer);
					}
				}
			}
			else
				g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM vip_users WHERE account_id = '%d' AND `sid` = %i;", m_steamID, m_iServerID);
			g_pConnection->Query(szQuery, [](ISQLQuery* test){});
		}
		else g_pVIPApi->Call_VIP_OnClientLoaded(slot.Get(), false);
	});
}

void VIP::OnClientDisconnect( CPlayerSlot slot, ENetworkDisconnectionReason reason, const char *pszName, uint64 xuid, const char *pszNetworkID )
{
	if (xuid == 0)
    	return;

	g_pVIPApi->Call_VIP_OnClientDisconnect(slot.Get(), g_pVIPCore->VIP_IsClientVIP(slot.Get()));

	// CCSPlayerController* pPlayerController = static_cast<CCSPlayerController*>(g_pEntitySystem->GetBaseEntity(static_cast<CEntityIndex>(slot.Get() + 1)));
	// if (!pPlayerController)
	// 	return;

	// uint32 m_steamID = pPlayerController->m_steamID();
	// if (m_steamID == 0)
	// 	return;

	// auto vipGroup = g_VipPlayer.find(m_steamID);

	// if (vipGroup != g_VipPlayer.end())
	// 	g_VipPlayer.erase(vipGroup);
}

bool OnVIPCommand(int iSlot, const char* szContent);

void VIPCallback(const char* szBack, const char* szFront, int iItem, int iSlot)
{
	if(iItem < 7)
	{
		const char* sCookie = g_pVIPCore->VIP_GetClientCookie(iSlot, szBack);
		VIPFunctions& vip_func = g_VipFunctions[std::string(szBack)];
		if(vip_func.eType != SELECTABLE)
		{
			int oldStatusValue;
			int newStatusValue;
			if(strlen(sCookie) == 0 || std::stoi(sCookie) != 0)
			{
				oldStatusValue = 1;
				newStatusValue = 0;
			}
			else
			{
				oldStatusValue = 0;
				newStatusValue = 1;
			}
			VIP_ToggleState oldStatus = static_cast<VIP_ToggleState>(oldStatusValue);
			VIP_ToggleState newStatus = static_cast<VIP_ToggleState>(newStatusValue);
			bool bBlock = false;
			if(vip_func.Togglable_callback) bBlock = vip_func.Togglable_callback(iSlot, szBack, oldStatus, newStatus);
			char szStatus[16];
			g_SMAPI->Format(szStatus, sizeof(szStatus), "%i", bBlock?oldStatusValue:newStatus);
			g_pVIPCore->VIP_SetClientCookie(iSlot, szBack, szStatus);
			OnVIPCommand(iSlot, "");
		}
		else
		{
			bool bClose = false;
			if(vip_func.Select_callback) bClose = vip_func.Select_callback(iSlot, szBack);
			if(bClose)
			{
				g_pUtils->NextFrame([iSlot](){
					g_pMenus->ClosePlayerMenu(iSlot);
				});
			}
		}
	}
}

bool OnVIPCommand(int iSlot, const char* szContent)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController)
		return false;

	KeyValues* Group = GetGroupKV(iSlot);
	if (!Group) {
		g_pUtils->PrintToChat(iSlot, g_pVIPCore->VIP_GetTranslate("NotAccess"));
		return false;
	}

	Menu hMenu;
	g_pMenus->SetTitleMenu(hMenu, g_pVIPCore->VIP_GetTranslate("MenuTitle"));
	
	char sBuff[128];
	FOR_EACH_VALUE(Group, pKey)
	{
		const char *pszParam = pKey->GetName();
		const char *pszValue = pKey->GetString(nullptr, nullptr);
		const char *szTrans = g_pVIPCore->VIP_GetTranslate(pszParam);
		const char *szValue = g_pVIPCore->VIP_GetClientFeatureString(iSlot, pszParam); 
		VIPFunctions& vip_func = g_VipFunctions[std::string(pszParam)];
		if(vip_func.eValType != VIP_NULL && vip_func.eType != HIDE)
		{
			if(vip_func.eType == SELECTABLE)
				g_SMAPI->Format(sBuff, sizeof(sBuff), "%s", strlen(szTrans)?szTrans:pszParam);
			else
				g_SMAPI->Format(sBuff, sizeof(sBuff), "%s [%s]", strlen(szTrans)?szTrans:pszParam, strlen(szValue)?vip_func.eValType == VIP_BOOL?g_pVIPCore->VIP_GetTranslate("On"):szValue:g_pVIPCore->VIP_GetTranslate("Off"));
			std::string szDisplay;
			if(vip_func.Display_callback)
				szDisplay = vip_func.Display_callback(iSlot, pszParam);
			g_pMenus->AddItemMenu(hMenu, pszParam, size(szDisplay)?szDisplay.c_str():sBuff);
		}
	}

	g_pMenus->SetBackMenu(hMenu, false);
	g_pMenus->SetExitMenu(hMenu, true);
	g_pMenus->SetCallback(hMenu, VIPCallback);
	g_pMenus->DisplayPlayerMenu(hMenu, iSlot, false);
	return false;
}

void VIP::AllPluginsLoaded()
{
	char error[64] = { 0 };
	int ret;
	g_pMysqlClient = ((ISQLInterface *)g_SMAPI->MetaFactory(SQLMM_INTERFACE, &ret, nullptr))->GetMySQLClient();
	if (ret == META_IFACE_FAILED)
	{
		V_strncpy(error, "Missing MYSQL plugin", 64);
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}

	g_pUtils = (IUtilsApi *)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		V_strncpy(error, "Missing Utils system plugin", 64);
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}

	g_pMenus = (IMenusApi *)g_SMAPI->MetaFactory(Menus_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		V_strncpy(error, "Missing Menus system plugin", 64);
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	
	KeyValues* pKVConfig = new KeyValues("Databases");

	if (!pKVConfig->LoadFromFile(g_pFullFileSystem, "addons/configs/databases.cfg")) {
		V_strncpy(error, "Failed to load databases config 'addons/config/databases.cfg'", sizeof(error));
		ConColorMsg(Color(255, 0, 0, 255), "[LR] %s\n", error);
		return;
	}

	pKVConfig = pKVConfig->FindKey("levels_ranks", false);
	if (!pKVConfig) {
		g_SMAPI->Format(error, sizeof(error), "No databases.cfg 'levels_ranks'");
		ConColorMsg(Color(255, 0, 0, 255), "[LR] %s\n", error);
		return;
	}

	MySQLConnectionInfo info;
	info.host = pKVConfig->GetString("host", nullptr);
	info.user = pKVConfig->GetString("user", nullptr);
	info.pass = pKVConfig->GetString("pass", nullptr);
	info.database = pKVConfig->GetString("database", nullptr);
	info.port = pKVConfig->GetInt("port");
	g_pConnection = g_pMysqlClient->CreateMySQLConnection(info);

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
	g_pUtils->RegCommand(g_PLID, {"mm_vip", "sm_vip", "vip"}, {"vip", "!vip"}, OnVIPCommand);
	g_pUtils->HookEvent(g_PLID, "player_spawn", OnPlayerSpawn);
	g_pUtils->HookEvent(g_PLID, "round_prestart", OnRoundPreStart);
}

///////////////////////////////////////
const char* VIP::GetLicense()
{
	return "GPL";
}

const char* VIP::GetVersion()
{
	return "1.1";
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
