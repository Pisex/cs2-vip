#include <stdio.h>
#include "vip.h"
#include "metamod_oslink.h"

VIP g_VIP;
PLUGIN_EXPOSE(VIP, g_VIP);
IVEngineServer2* engine = nullptr;
IGameEventManager2* gameeventmanager = nullptr;
IGameResourceServiceServer* g_pGameResourceService = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
CSchemaSystem* g_pCSchemaSystem = nullptr;
CCSGameRules* g_pGameRules = nullptr;
CPlayerSpawnEvent g_PlayerSpawnEvent;
CRoundPreStartEvent g_RoundPreStartEvent;

std::map<std::string, KeyValues*> g_VipGroups;
std::map<uint32, VipPlayer> g_VipPlayer;
std::map<uint32, MenuPlayer> g_MenuPlayer;
CUtlVector<Menus> g_Menus;

KeyValues* pKVVips;

bool g_bPistolRound;
int m_iServerID;

IMySQLClient *g_pMysqlClient;
IMySQLConnection* g_pConnection;

const char *g_pszLanguage;
std::map<std::string, std::string> g_vecPhrases;

KeyValues* g_hKVData;

VIPApi* g_pVIPApi = nullptr;
IVIPApi* g_pVIPCore = nullptr;

class GameSessionConfiguration_t { };
SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK2(IGameEventManager2, FireEvent, SH_NOATTRIB, 0, bool, IGameEvent*, bool);
SH_DECL_HOOK2_void(IServerGameClients, ClientCommand, SH_NOATTRIB, 0, CPlayerSlot, const CCommand&);
SH_DECL_HOOK4_void(IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0, CPlayerSlot, char const*, int, uint64);
SH_DECL_HOOK3_void(ICvar, DispatchConCommand, SH_NOATTRIB, 0, ConCommandHandle, const CCommandContext&, const CCommand&);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot, int, const char *, uint64, const char *);
SH_DECL_HOOK3_void(INetworkServerService, StartupServer, SH_NOATTRIB, 0, const GameSessionConfiguration_t&, ISource2WorldSession*, const char*);

void (*UTIL_ClientPrint)(CBasePlayerController *player, int msg_dest, const char* msg_name, const char* param1, const char* param2, const char* param3, const char* param4) = nullptr;
void (*UTIL_ClientPrintAll)(int msg_dest, const char* msg_name, const char* param1, const char* param2, const char* param3, const char* param4) = nullptr;

bool containsOnlyDigits(const std::string& str) {
	return str.find_first_not_of("0123456789") == std::string::npos;
}

void VIP::OnClientCommand(CPlayerSlot slot, const CCommand &args)
{
	if(strstr(args.Arg(0),"mm_"))
	{
		std::string szCommand = args.Arg(0);
		size_t found = szCommand.find("mm_");
		g_pVIPApi->FindAndCallCommand(szCommand.substr(found + 3).c_str(), args.ArgS(), slot.Get());
		RETURN_META(MRES_SUPERCEDE);
	}
}

KeyValues* GetGroupKV(int iSlot)
{
	CCSPlayerController* pController = static_cast<CCSPlayerController*>(g_pEntitySystem->GetBaseEntity(static_cast<CEntityIndex>(iSlot + 1)));
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
			CCSPlayerController* pController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(i + 1));
			if (!pController)
				continue;
			uint32 m_steamID = pController->m_steamID();
			if(m_steamID == 0)
				continue;
			if(strstr(std::to_string(m_steamID).c_str(), args[1]) || strstr(pController->m_iszPlayerName(), args[1]) || (containsOnlyDigits(args[1]) && std::stoi(args[1]) == i))
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
			CCSPlayerController* pController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(i + 1));
			if (!pController)
				continue;
			uint32 m_steamID = pController->m_steamID();
			if(m_steamID == 0)
				continue;
			if(strstr(std::to_string(m_steamID).c_str(), args[1]) || strstr(pController->m_iszPlayerName(), args[1]) || (containsOnlyDigits(args[1]) && std::stoi(args[1]) == i))
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
				g_pVIPCore->VIP_GiveClientVIP(iSlot, std::stoi(args[2]), args[3]);
				META_CONPRINT("[VIP] You have successfully granted VIP status\n");
			}
		}
		else META_CONPRINT("[VIP] Player not found\n");
	}
	else META_CONPRINT("[VIP] Usage: vip_give <userid|nickname|accountid> <time_second> <group>\n");
}

std::string Colorizer(std::string str)
{
	for (int i = 0; i < std::size(colors_hex); i++)
	{
		size_t pos = 0;

		while ((pos = str.find(colors_text[i], pos)) != std::string::npos)
		{
			str.replace(pos, colors_text[i].length(), colors_hex[i]);
			pos += colors_hex[i].length();
		}
	}

	return str;
}

void VIPApi::VIP_PrintToChatAll(int hud_dest, const char *msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[256], buf2[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);
	va_end(args);
	if(hud_dest == 3)
		g_SMAPI->Format(buf2, sizeof(buf2), "%s %s", g_pVIPCore->VIP_GetTranslate("Prefix"), buf);

	std::string colorizedBuf = Colorizer(hud_dest == 3?buf2:buf);

	UTIL_ClientPrintAll(hud_dest, colorizedBuf.c_str(), nullptr, nullptr, nullptr, nullptr);
	ConMsg("%s\n", buf2);
}

void ClientPrintAll(int hud_dest, const char *msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[256], buf2[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);
	va_end(args);
	if(hud_dest == 3)
		g_SMAPI->Format(buf2, sizeof(buf2), "%s %s", g_pVIPCore->VIP_GetTranslate("Prefix"), buf);

	std::string colorizedBuf = Colorizer(hud_dest == 3?buf2:buf);
	
	UTIL_ClientPrintAll(hud_dest, colorizedBuf.c_str(), nullptr, nullptr, nullptr, nullptr);
	ConMsg("%s\n", buf2);
}

void VIPApi::VIP_PrintToChat(int Slot, int hud_dest, const char *msg, ...)
{
	CCSPlayerController* pPlayerController =  (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(Slot + 1));
	if (!pPlayerController)
		return;
	uint32 m_steamID = pPlayerController->m_steamID();
	if(m_steamID == 0)
		return;

	va_list args;
	va_start(args, msg);

	char buf[256], buf2[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);
	va_end(args);
	if(hud_dest == 3)
		g_SMAPI->Format(buf2, sizeof(buf2), "%s %s", g_pVIPCore->VIP_GetTranslate("Prefix"), buf);
		
	std::string colorizedBuf = Colorizer(hud_dest == 3?buf2:buf);

	UTIL_ClientPrint(pPlayerController, hud_dest, colorizedBuf.c_str(), nullptr, nullptr, nullptr, nullptr);
}

void ClientPrint(CBasePlayerController *player, int hud_dest, const char *msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[256], buf2[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);
	va_end(args);
	if(hud_dest == 3)
		g_SMAPI->Format(buf2, sizeof(buf2), "%s %s", g_pVIPCore->VIP_GetTranslate("Prefix"), buf);
		
	std::string colorizedBuf = Colorizer(hud_dest == 3?buf2:buf);

	if (player)
		UTIL_ClientPrint(player, hud_dest, colorizedBuf.c_str(), nullptr, nullptr, nullptr, nullptr);
	else
		ConMsg("%s\n", buf2);
}

const char* VIPApi::VIP_GetClientCookie(int iSlot, const char* sCookieName)
{
	CCSPlayerController* pController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iSlot + 1));
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
	CCSPlayerController* pController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iSlot + 1));
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
	GET_V_IFACE_ANY(GetEngineFactory, g_pCSchemaSystem, CSchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameClients, IServerGameClients, SOURCE2GAMECLIENTS_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceService, IGameResourceServiceServer, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);

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

	CModule libserver(g_pSource2Server);
	UTIL_ClientPrint = libserver.FindPatternSIMD(WIN_LINUX("48 85 C9 0F 84 2A 2A 2A 2A 48 8B C4 48 89 58 18", "55 48 89 E5 41 57 49 89 CF 41 56 49 89 D6 41 55 41 89 F5 41 54 4C 8D A5 A0 FE FF FF")).RCast< decltype(UTIL_ClientPrint) >();
	if (!UTIL_ClientPrint)
	{
		V_strncpy(error, "Failed to find function to get UTIL_ClientPrint", maxlen);
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return false;
	}
	UTIL_ClientPrintAll = libserver.FindPatternSIMD(WIN_LINUX("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 81 EC 70 01 2A 2A 8B E9", "55 48 89 E5 41 57 49 89 D7 41 56 49 89 F6 41 55 41 89 FD")).RCast< decltype(UTIL_ClientPrintAll) >();
	if (!UTIL_ClientPrintAll)
	{
		V_strncpy(error, "Failed to find function to get UTIL_ClientPrintAll", maxlen);
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
	SH_ADD_HOOK_MEMFUNC(ICvar, DispatchConCommand, g_pCVar, this, &VIP::OnDispatchConCommand, false);
	SH_ADD_HOOK(IServerGameClients, ClientCommand, g_pSource2GameClients, SH_MEMBER(this, &VIP::OnClientCommand), false);

	gameeventmanager = static_cast<IGameEventManager2*>(CallVFunc<IToolGameEventAPI*, 91>(g_pSource2Server));
	SH_ADD_HOOK(IGameEventManager2, FireEvent, gameeventmanager, SH_MEMBER(this, &VIP::OnFireEvent), false);
	ConVar_Register(FCVAR_GAMEDLL);

	g_pVIPApi = new VIPApi();
	g_pVIPCore = g_pVIPApi;

	return true;
}

bool VIP::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK(IGameEventManager2, FireEvent, gameeventmanager, SH_MEMBER(this, &VIP::OnFireEvent), false);
	SH_REMOVE_HOOK(IServerGameDLL, GameFrame, g_pSource2Server, SH_MEMBER(this, &VIP::GameFrame), true);
	SH_REMOVE_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &VIP::StartupServer), true);
    SH_REMOVE_HOOK(IServerGameClients, ClientPutInServer, g_pSource2GameClients, SH_MEMBER(this, &VIP::OnClientPutInServer), true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientDisconnect, g_pSource2GameClients, this, &VIP::OnClientDisconnect, true);
	SH_REMOVE_HOOK_MEMFUNC(ICvar, DispatchConCommand, g_pCVar, this, &VIP::OnDispatchConCommand, false);
	SH_REMOVE_HOOK(IServerGameClients, ClientCommand, g_pSource2GameClients, SH_MEMBER(this, &VIP::OnClientCommand), false);

	gameeventmanager->RemoveListener(&g_PlayerSpawnEvent);
	gameeventmanager->RemoveListener(&g_RoundPreStartEvent);

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

void VIP::NextFrame(std::function<void()> fn)
{
	m_nextFrame.push_back(fn);
}

void DisplayPlayerMenu(CCSPlayerController* pPlayer, int iSlot)
{
	MenuPlayer& hMenuPlayer = g_MenuPlayer[pPlayer->m_steamID()];
	char sBuff[64] = "\0";
	Menus& hMenu = g_Menus[hMenuPlayer.iMenu];
	int iCount = 0;
	int iItems = size(hMenu.hItems) / 5;
	if (size(hMenu.hItems) % 5 > 0) iItems++;
	ClientPrint(pPlayer, 5, hMenu.szTitle.c_str());
	for (size_t l = hMenuPlayer.iList*5; l < hMenu.hItems.size(); ++l) {
		if(hMenu.hItems[l].iType > 0)
		{
			g_SMAPI->Format(sBuff, sizeof(sBuff), " \x04[!%i]\x01 %s", iCount+1, hMenu.hItems[l].sText.c_str());
			ClientPrint(pPlayer, 5, sBuff);
		}
		iCount++;
		if(iCount == 5 || l == hMenu.hItems.size()-1)
		{
			if(l == hMenu.hItems.size()-1)
			{
				for (size_t i = 0; i <= 5-iCount; i++)
				{
					ClientPrint(pPlayer, 5, " \x08-\x01");
				}
			}
			if(hMenuPlayer.iList > 0) ClientPrint(pPlayer, 5, g_pVIPCore->VIP_GetTranslate("MenuBack"));
			if(iItems > hMenuPlayer.iList+1) ClientPrint(pPlayer, 5, g_pVIPCore->VIP_GetTranslate("MenuNext"));
			ClientPrint(pPlayer, 5, g_pVIPCore->VIP_GetTranslate("MenuExit"));
			break;
		}
	}
}

void VIP::OnDispatchConCommand(ConCommandHandle cmdHandle, const CCommandContext& ctx, const CCommand& args)
{
	if (!g_pEntitySystem)
		return;

	auto iCommandPlayerSlot = ctx.GetPlayerSlot();

	bool bSay = !V_strcmp(args.Arg(0), "say");
	bool bTeamSay = !V_strcmp(args.Arg(0), "say_team");

	if (iCommandPlayerSlot != -1 && (bSay || bTeamSay))
	{
		auto pController = (CCSPlayerController*)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iCommandPlayerSlot.Get() + 1));
		bool bCommand = *args[1] == '!' || *args[1] == '/';
		bool bSilent = *args[1] == '/';

		if (bCommand)
		{
			char *pszMessage = (char *)(args.ArgS() + 2);
			// pszMessage[V_strlen(pszMessage) - 1] = 0;
			CCommand arg;
			arg.Tokenize(args.ArgS() + 2);

			if(containsOnlyDigits(std::string(arg[0])))
			{
				MenuPlayer& hMenuPlayer = g_MenuPlayer[pController->m_steamID()];
				Menus& hMenu = g_Menus[hMenuPlayer.iMenu];
				if(hMenuPlayer.bEnabled)
				{
					int iButton = std::stoi(arg[0]);
					if(iButton == 9 && hMenu.bExit)
					{
						hMenuPlayer.iList = 0;
						hMenuPlayer.bEnabled = false;
						g_Menus.Remove(hMenuPlayer.iMenu);
						for (size_t i = 0; i < 8; i++)
						{
							ClientPrint(pController, 5, " \x08-\x01");
						}
					}
					else if(iButton == 8)
					{
						int iItems = size(hMenu.hItems) / 5;
						if (size(hMenu.hItems) % 5 > 0) iItems++;
						if(iItems > hMenuPlayer.iList+1)
						{
							hMenuPlayer.iList++;
							DisplayPlayerMenu(pController, iCommandPlayerSlot.Get());
						}
					}
					else if(iButton == 7)
					{
						if(hMenuPlayer.iList != 0)
						{
							hMenuPlayer.iList--;
							DisplayPlayerMenu(pController, iCommandPlayerSlot.Get());
						}
					}
					else
					{
						int iItem = hMenuPlayer.iList*5+iButton-1;
						if(hMenu.hItems.size() <= iItem) return;
						const char* szFunction = hMenu.hItems[iItem].sBack.c_str();
						const char* sCookie = g_pVIPCore->VIP_GetClientCookie(iCommandPlayerSlot.Get(), szFunction);
						if(strlen(sCookie) == 0 || std::stoi(sCookie) != 0)
							g_pVIPCore->VIP_SetClientCookie(iCommandPlayerSlot.Get(), szFunction, "0");
						else
							g_pVIPCore->VIP_SetClientCookie(iCommandPlayerSlot.Get(), szFunction, "1");
						char sBuff[64];
						const char* pszValue = g_pVIPCore->VIP_GetClientFeatureString(iCommandPlayerSlot.Get(), szFunction);
						const char *szTrans = g_pVIPCore->VIP_GetTranslate(szFunction);
						g_SMAPI->Format(sBuff, sizeof(sBuff), "%s [%s]", strlen(szTrans)?szTrans:szFunction, strlen(pszValue)?containsOnlyDigits(pszValue) && std::stoi(pszValue) == 1?g_pVIPCore->VIP_GetTranslate("On"):pszValue:g_pVIPCore->VIP_GetTranslate("Off"));
						hMenu.hItems[iItem].sText = std::string(sBuff);
						DisplayPlayerMenu(pController, iCommandPlayerSlot.Get());
					}
				}
				else SH_CALL(g_pCVar, &ICvar::DispatchConCommand)(cmdHandle, ctx, args);
			}
			else
			{
				SH_CALL(g_pCVar, &ICvar::DispatchConCommand)(cmdHandle, ctx, args);
				g_pVIPApi->FindAndCallCommand(arg[0], pszMessage, iCommandPlayerSlot.Get());
			}

			RETURN_META(MRES_SUPERCEDE);
		}
	}
}

void VIP::StartupServer(const GameSessionConfiguration_t& config, ISource2WorldSession*, const char*)
{
	g_pGameRules = nullptr;

	static bool bDone = false;
	if (!bDone)
	{
		g_pGameEntitySystem = *reinterpret_cast<CGameEntitySystem**>(reinterpret_cast<uintptr_t>(g_pGameResourceService) + WIN_LINUX(0x58, 0x50));
		g_pEntitySystem = g_pGameEntitySystem;

		gameeventmanager->AddListener(&g_PlayerSpawnEvent, "player_spawn", true);
		gameeventmanager->AddListener(&g_RoundPreStartEvent, "round_prestart", true);
		bDone = true;
	}
}

bool VIP::OnFireEvent(IGameEvent* pEvent, bool bDontBroadcast)
{
    if (!pEvent) {
        RETURN_META_VALUE(MRES_IGNORED, false);
    }

    const char* szName = pEvent->GetName();
	g_pVIPApi->Call_VIP_OnFireEvent(szName, pEvent, bDontBroadcast);
    RETURN_META_VALUE(MRES_IGNORED, true);
}

void VIP::GameFrame(bool simulating, bool bFirstTick, bool bLastTick)
{
	if (!g_pGameRules)
	{
		CCSGameRulesProxy* pGameRulesProxy = static_cast<CCSGameRulesProxy*>(UTIL_FindEntityByClassname(nullptr, "cs_gamerules"));
		if (pGameRulesProxy)
		{
			g_pGameRules = pGameRulesProxy->m_pGameRules();
		}
	}
	
	while (!m_nextFrame.empty())
	{
		m_nextFrame.front()();
		m_nextFrame.pop_front();
	}

	if(g_iLastTime == 0) g_iLastTime = std::time(0);
	else if(std::time(0) - g_iLastTime >= 1)
	{
		g_iLastTime = std::time(0);
		for (int i = 0; i < 64; i++)
		{
			CCSPlayerController* pPlayerController =  (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(i + 1));
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
				ClientPrint(pPlayerController, 3, g_pVIPCore->VIP_GetTranslate("VIPExpired1"));
				ClientPrint(pPlayerController, 3, g_pVIPCore->VIP_GetTranslate("VIPExpired2"));
				ClientPrint(pPlayerController, 3, g_pVIPCore->VIP_GetTranslate("VIPExpired3"));
				
				char szQuery[256];
				g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `vip_users` WHERE `account_id` = '%d' AND `sid` = %i;", m_steamID, m_iServerID);
				g_pConnection->Query(szQuery, [this](IMySQLQuery* test){});
			}
		}
	}
}

void CPlayerSpawnEvent::FireGameEvent(IGameEvent* event)
{	
	CBasePlayerController* pPlayerController = static_cast<CBasePlayerController*>(event->GetPlayerController("userid"));
	if (!pPlayerController || pPlayerController->m_steamID() == 0) // Ignore bots
		return;

	g_VIP.NextFrame([hPlayerController = CHandle<CBasePlayerController>(pPlayerController), pPlayerSlot = event->GetPlayerSlot("userid")]()
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

void CRoundPreStartEvent::FireGameEvent(IGameEvent* event)
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
	CCSPlayerController* pController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iSlot + 1));
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
	CCSPlayerController* pController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iSlot + 1));
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
		g_pConnection->Query(szQuery, [this](IMySQLQuery* test){});
	}
	return true;
}

bool VIPApi::VIP_GiveClientVIP(int iSlot, int iTime, const char* szGroup, bool bAddToDB)
{
	CCSPlayerController* pController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iSlot + 1));
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
		g_pConnection->Query(szQuery, [this](IMySQLQuery* test){});
	}
	if(player.TimeEnd == 0) ClientPrint(pController, 3, g_pVIPCore->VIP_GetTranslate("WelcomePerm"), pController->m_iszPlayerName());
	else
	{
		time_t currentTime_t = (time_t)player.TimeEnd;
		char buffer[80];
    	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&currentTime_t));
		ClientPrint(pController, 3, g_pVIPCore->VIP_GetTranslate("Welcome"), pController->m_iszPlayerName(), buffer);
	}
	g_pVIPApi->Call_VIP_OnVIPClientAdded(iSlot);
	return true;
}

bool VIPApi::VIP_RemoveClientVIP(int iSlot, bool bNotify, bool bInDB)
{
	CCSPlayerController* pController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iSlot + 1));
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
		g_pConnection->Query(szQuery, [this](IMySQLQuery* test){});
	}
	if(bNotify)
	{
		ClientPrint(pController, 3, g_pVIPCore->VIP_GetTranslate("VIPExpired1"));
		ClientPrint(pController, 3, g_pVIPCore->VIP_GetTranslate("VIPExpired2"));
		ClientPrint(pController, 3, g_pVIPCore->VIP_GetTranslate("VIPExpired3"));
	}
	g_pVIPApi->Call_VIP_OnVIPClientRemoved(iSlot, 2);
	return true;
}

bool VIPApi::VIP_SetClientVIPGroup(int iSlot, const char* szGroup, bool bInDB)
{
	CCSPlayerController* pController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iSlot + 1));
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
		g_pConnection->Query(szQuery, [this](IMySQLQuery* test){});
	}
	return true;
}

bool VIPApi::VIP_IsClientVIP(int iSlot)
{
	CCSPlayerController* pController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iSlot + 1));
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
		return Group->GetBool(szFeature, 1.f);
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

bool VIPApi::VIP_IsValidVIPGroup(const char* szGroup)
{
	return g_VipGroups[szGroup] != NULL;
}

void VIP::OnClientPutInServer(CPlayerSlot slot, char const* pszName, int type, uint64 xuid)
{
	if (slot.Get() == -1)
    	return;

	CCSPlayerController* pPlayerController = static_cast<CCSPlayerController*>(g_pEntitySystem->GetBaseEntity(static_cast<CEntityIndex>(slot.Get() + 1)));
	if (!pPlayerController)
		return;

	uint32 m_steamID = pPlayerController->m_steamID();

	char szQuery[256];
	g_SMAPI->Format(szQuery, sizeof(szQuery), "SELECT `group`, `expires` FROM `vip_users` WHERE `account_id` = %d AND `sid` = %d;", m_steamID, m_iServerID);
	g_pConnection->Query(szQuery, [slot, m_steamID, pPlayerController, this](IMySQLQuery* test)
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
				g_SMAPI->Format(szQuery, sizeof(szQuery), "UPDATE vip_users SET name = '%s', lastvisit = %i  WHERE account_id = '%d' AND `sid` = %i;", g_pConnection->Escape(engine->GetClientConVarValue(slot, "name")).c_str(), std::time(0), m_steamID, m_iServerID);
				if(player.TimeEnd == 0) 
					ClientPrint(pPlayerController, 3, g_pVIPCore->VIP_GetTranslate("WelcomePerm"), pPlayerController->m_iszPlayerName());
				else
				{
					time_t currentTime_t = static_cast<time_t>(player.TimeEnd);
					char buffer[80];
					std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&currentTime_t));
					ClientPrint(pPlayerController, 3, g_pVIPCore->VIP_GetTranslate("Welcome"), pPlayerController->m_iszPlayerName(), buffer);
				}
			}
			else
				g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM vip_users WHERE account_id = '%d' AND `sid` = %i;", m_steamID, m_iServerID);
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
		}
		else g_pVIPApi->Call_VIP_OnClientLoaded(slot.Get(), false);
	});
}

void VIP::OnClientDisconnect(CPlayerSlot slot, int reason, const char *pszName, uint64 xuid, const char *pszNetworkID)
{
	if (xuid == 0)
    	return;

	g_pVIPApi->Call_VIP_OnClientDisconnect(slot.Get(), g_pVIPCore->VIP_IsClientVIP(slot.Get()));

	CCSPlayerController* pPlayerController = static_cast<CCSPlayerController*>(g_pEntitySystem->GetBaseEntity(static_cast<CEntityIndex>(slot.Get() + 1)));
	if (!pPlayerController)
		return;

	uint32 m_steamID = pPlayerController->m_steamID();
	if (m_steamID == 0)
		return;

	auto vipGroup = g_VipPlayer.find(m_steamID);
	auto PlayerMenu = g_MenuPlayer.find(m_steamID);

	if (vipGroup == g_VipPlayer.end() || PlayerMenu == g_MenuPlayer.end() || !engine->IsClientFullyAuthenticated(slot.Get())) {
		if (vipGroup != g_VipPlayer.end())
			g_VipPlayer.erase(vipGroup);
		
		if (PlayerMenu != g_MenuPlayer.end())
			g_MenuPlayer.erase(PlayerMenu);
	}
}

void OnVIPCommand(const char* szContent, int iSlot)
{
	CCSPlayerController* pController = static_cast<CCSPlayerController*>(g_pEntitySystem->GetBaseEntity(static_cast<CEntityIndex>(iSlot + 1)));
	if (!pController)
		return;
	
	uint32 m_steamID = pController->m_steamID();
	if (m_steamID == 0)
		return;

	KeyValues* Group = GetGroupKV(iSlot);
	if (!Group) {
		ClientPrint(pController, 3, g_pVIPCore->VIP_GetTranslate("NotAccess"));
		return;
	}

	MenuPlayer& pPlayer = g_MenuPlayer[m_steamID];
	if (pPlayer.bEnabled) {
		ClientPrint(pController, 3, g_pVIPCore->VIP_GetTranslate("MenuIsAlreadyOpen"));
		return;
	}

	CCommand arg;
	arg.Tokenize(szContent);

	Menus hMenu;
	hMenu.szTitle = std::string(g_pVIPCore->VIP_GetTranslate("MenuTitle"));
	char sBuff[128];

	FOR_EACH_VALUE(Group, pKey)
	{
		const char *pszParam = pKey->GetName();
		const char *pszValue = pKey->GetString(nullptr, nullptr);
		const char *szTrans = g_pVIPCore->VIP_GetTranslate(pszParam);
		const char *szValue = g_pVIPCore->VIP_GetClientFeatureString(iSlot, pszParam); 

		g_SMAPI->Format(sBuff, sizeof(sBuff), "%s [%s]", strlen(szTrans)?szTrans:pszParam, strlen(szValue)?containsOnlyDigits(szValue) && std::stoi(szValue) == 1?g_pVIPCore->VIP_GetTranslate("On"):pszValue:g_pVIPCore->VIP_GetTranslate("Off"));
		
		Items hItem;
		hItem.iType = 1;
		hItem.sBack = std::string(pszParam);
		hItem.sText = std::string(sBuff);
		hMenu.hItems.push_back(hItem);
	}

	hMenu.bExit = true;
	pPlayer.bEnabled = true;
	pPlayer.iMenu = g_Menus.AddToTail(hMenu);
	DisplayPlayerMenu(pController, iSlot);
}

void VIP::AllPluginsLoaded()
{
	char error[64] = { 0 };
	int ret;
	g_pMysqlClient = static_cast<IMySQLClient*>(g_SMAPI->MetaFactory(MYSQLMM_INTERFACE, &ret, nullptr));

	if (ret == META_IFACE_FAILED) {
		V_strncpy(error, "Missing MYSQL plugin", sizeof(error));
		return;
	}

	KeyValues* pKVConfigVIP = new KeyValues("Databases");

	if (!pKVConfigVIP->LoadFromFile(g_pFullFileSystem, "addons/configs/databases.cfg")) {
		V_strncpy(error, "Failed to load vip config 'addons/config/databases.cfg'", sizeof(error));
		return;
	}

	pKVConfigVIP = pKVConfigVIP->FindKey("vip", false);
	if (!pKVConfigVIP) {
		V_strncpy(error, "No databases.cfg 'vip'", sizeof(error));
		return;
	}

	MySQLConnectionInfo info {
		pKVConfigVIP->GetString("host", nullptr),
		pKVConfigVIP->GetString("user", nullptr),
		pKVConfigVIP->GetString("pass", nullptr),
		pKVConfigVIP->GetString("database", nullptr),
		pKVConfigVIP->GetInt("port", 3306)
	};

	g_pConnection = g_pMysqlClient->CreateMySQLConnection(info);

	g_pConnection->Connect([this](bool connect) {
		if (!connect) {
			META_CONPRINT("Failed to connect the mysql database\n");
		} else {
			g_pConnection->Query("CREATE TABLE IF NOT EXISTS `vip_users` (`account_id` INTEGER NOT NULL PRIMARY KEY AUTO_INCREMENT, `name` VARCHAR(64) NOT NULL default 'unknown', `lastvisit` INTEGER UNSIGNED NOT NULL default 0, `sid` INTEGER UNSIGNED NOT NULL, `group` VARCHAR(64) NOT NULL, `expires` INTEGER UNSIGNED NOT NULL default 0);", [this](IMySQLQuery* test) {});
			g_pVIPApi->Call_VIP_OnVIPLoaded();
			g_pVIPApi->SetReady(true);
		}
	});

	g_pVIPCore->VIP_RegCommand("vip", OnVIPCommand);
}

///////////////////////////////////////
const char* VIP::GetLicense()
{
	return "GPL";
}

const char* VIP::GetVersion()
{
	return "1.0.0";
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
