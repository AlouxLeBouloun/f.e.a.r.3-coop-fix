#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#pragma comment(lib, "user32.lib")

#define FEAR3FIX_VERSION "1.2"

struct CallbackMsg_t {
	int m_hSteamUser;
	int m_iCallback;
	unsigned char *m_pubParam;
	int m_cubParam;
};

//   headers v1.13 Garbage
//   GSClientApprove_t (201) : CSteamID m_SteamID                                   -> 8 octets
//   GSClientDeny_t    (202) : CSteamID m_SteamID ; EDenyReason m_eDenyReason (@8) ; char[128] -> 140 octets
//   GSClientKick_t    (203) : CSteamID m_SteamID ; EDenyReason m_eDenyReason (@8)   -> 12 octets
enum { kGSClientApprove = 201, kGSClientDeny = 202, kGSClientKick = 203 };
enum { kDenySteamValidationStalled = 14 };

typedef bool(__cdecl *PFN_BGetCallback)(int hSteamPipe, CallbackMsg_t *pMsg);
typedef FARPROC(WINAPI *PFN_GetProcAddress)(HMODULE, LPCSTR);
typedef HRESULT(WINAPI *PFN_DirectInput8Create)(HINSTANCE, DWORD, REFIID, LPVOID *, void *);

enum VoiceMode { kVoiceOpen, kVoiceHold, kVoiceToggle };

static struct Config {
	bool enabled;
	bool fov;
	bool log;
	int reasons[16];
	int nReasons;
	char reasonsText[64];
	VoiceMode voiceMode;
	int voiceKey;
	char voiceKeyText[32];
	bool voiceSound;
} g_cfg;

static char g_dir[MAX_PATH];
static CRITICAL_SECTION g_logLock;
static PFN_GetProcAddress g_realGetProcAddress;
static PFN_BGetCallback volatile g_realBGetCallback;
static volatile LONG g_status;
static volatile LONG g_rewrites;

/* ---------- not usefull log but i'm lazy to remove it ---------- */

static void Log(const char *fmt, ...)
{
	if (!g_cfg.log)
		return;
	char line[512];
	SYSTEMTIME t;
	GetLocalTime(&t);
	int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "%04d-%02d-%02d %02d:%02d:%02d.%03d [pid %lu] ",
		t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentProcessId());
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(line + n, sizeof(line) - n, _TRUNCATE, fmt, ap);
	va_end(ap);
	strcat_s(line, "\r\n");

	char path[MAX_PATH];
	_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\fear3fix.log", g_dir);
	EnterCriticalSection(&g_logLock);
	HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		DWORD w;
		WriteFile(h, line, (DWORD)strlen(line), &w, NULL);
		CloseHandle(h);
	}
	LeaveCriticalSection(&g_logLock);
}

/* ---------- config ---------- */

static int ParseKey(const char *s)
{
	static const struct { const char *name; int vk; } kNames[] = {
		{ "MOUSE3", VK_MBUTTON }, { "MOUSE4", VK_XBUTTON1 }, { "MOUSE5", VK_XBUTTON2 },
		{ "SPACE", VK_SPACE }, { "TAB", VK_TAB }, { "CAPSLOCK", VK_CAPITAL }, { "ENTER", VK_RETURN },
		{ "BACKSPACE", VK_BACK }, { "SHIFT", VK_SHIFT }, { "LSHIFT", VK_LSHIFT }, { "RSHIFT", VK_RSHIFT },
		{ "CTRL", VK_CONTROL }, { "LCTRL", VK_LCONTROL }, { "RCTRL", VK_RCONTROL },
		{ "ALT", VK_MENU }, { "LALT", VK_LMENU }, { "RALT", VK_RMENU },
		{ "INSERT", VK_INSERT }, { "DELETE", VK_DELETE }, { "HOME", VK_HOME }, { "END", VK_END },
		{ "PAGEUP", VK_PRIOR }, { "PAGEDOWN", VK_NEXT }, { "UP", VK_UP }, { "DOWN", VK_DOWN },
		{ "LEFT", VK_LEFT }, { "RIGHT", VK_RIGHT }, { "PAUSE", VK_PAUSE }, { "SCROLLLOCK", VK_SCROLL },
		{ "NUMLOCK", VK_NUMLOCK }, { "MULTIPLY", VK_MULTIPLY }, { "ADD", VK_ADD }, { "SUBTRACT", VK_SUBTRACT },
		{ "DECIMAL", VK_DECIMAL }, { "DIVIDE", VK_DIVIDE },
		{ "`", VK_OEM_3 }, { "-", VK_OEM_MINUS }, { "=", VK_OEM_PLUS }, { "[", VK_OEM_4 }, { "]", VK_OEM_6 },
		{ ";", VK_OEM_1 }, { "'", VK_OEM_7 }, { ",", VK_OEM_COMMA }, { ".", VK_OEM_PERIOD }, { "/", VK_OEM_2 },
		{ "\\", VK_OEM_5 },
	};
	char k[32];
	int n = 0;
	for (; s[n] && n < 31; n++)
		k[n] = (char)toupper((unsigned char)s[n]);
	k[n] = 0;
	if (n == 1 && ((k[0] >= 'A' && k[0] <= 'Z') || (k[0] >= '0' && k[0] <= '9')))
		return k[0];
	if (k[0] == 'F' && n >= 2 && n <= 3) {
		int f = atoi(k + 1);
		if (f >= 1 && f <= 24)
			return VK_F1 + f - 1;
	}
	if (strncmp(k, "NUMPAD", 6) == 0 && n == 7 && k[6] >= '0' && k[6] <= '9')
		return VK_NUMPAD0 + (k[6] - '0');
	if (k[0] == '0' && k[1] == 'X') {
		long v = strtol(k + 2, NULL, 16);
		return v > 0 && v < 0xFF ? (int)v : 0;
	}
	for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++)
		if (strcmp(k, kNames[i].name) == 0)
			return kNames[i].vk;
	return 0;
}

static void LoadConfig()
{
	char ini[MAX_PATH];
	_snprintf_s(ini, sizeof(ini), _TRUNCATE, "%s\\fear3fix.ini", g_dir);
	g_cfg.enabled = GetPrivateProfileIntA("fix", "enabled", 1, ini) != 0;
	g_cfg.log = GetPrivateProfileIntA("fix", "log", 1, ini) != 0;
	g_cfg.fov = GetPrivateProfileIntA("fov", "enabled", 1, ini) != 0;
	char mode[16];
	GetPrivateProfileStringA("voice", "mode", "open", mode, sizeof(mode), ini);
	g_cfg.voiceMode = _stricmp(mode, "toggle") == 0 ? kVoiceToggle : _stricmp(mode, "hold") == 0 ? kVoiceHold : kVoiceOpen;
	GetPrivateProfileStringA("voice", "key", "V", g_cfg.voiceKeyText, sizeof(g_cfg.voiceKeyText), ini);
	g_cfg.voiceKey = ParseKey(g_cfg.voiceKeyText);
	g_cfg.voiceSound = GetPrivateProfileIntA("voice", "sound", 1, ini) != 0;
	GetPrivateProfileStringA("fix", "reasons", "12", g_cfg.reasonsText, sizeof(g_cfg.reasonsText), ini);
	g_cfg.nReasons = 0;
	char tmp[64];
	strcpy_s(tmp, g_cfg.reasonsText);
	char *ctx = NULL;
	for (char *tok = strtok_s(tmp, ", ;", &ctx); tok && g_cfg.nReasons < 16; tok = strtok_s(NULL, ", ;", &ctx)) {
		int r = atoi(tok);
		if (r > 0 && r != kDenySteamValidationStalled)
			g_cfg.reasons[g_cfg.nReasons++] = r;
	}
}

static bool ReasonSelected(int r)
{
	for (int i = 0; i < g_cfg.nReasons; i++)
		if (g_cfg.reasons[i] == r)
			return true;
	return false;
}

/* ---------- callback fix ---------- */

static bool WriteReason(int *p, int value)
{
	__try {
		*p = value;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static void OnCallback(int pipe, CallbackMsg_t *m)
{
	const unsigned char *p = m->m_pubParam;
	switch (m->m_iCallback) {
	case kGSClientApprove:
		if (m->m_cubParam >= 8)
			Log("pipe %d : GSClientApprove_t steamid=%llu", pipe, *(const unsigned long long *)p);
		break;
	case kGSClientKick:
		if (m->m_cubParam >= 12)
			Log("pipe %d : GSClientKick_t steamid=%llu raison=%d (non modifié)", pipe,
				*(const unsigned long long *)p, *(const int *)(p + 8));
		break;
	case kGSClientDeny:
		if (m->m_cubParam >= 12) {
			int *reason = (int *)(p + 8);
			int before = *reason;
			if (g_cfg.enabled && ReasonSelected(before)) {
				bool ok = WriteReason(reason, kDenySteamValidationStalled);
				if (ok)
					InterlockedIncrement(&g_rewrites);
				Log("pipe %d : GSClientDeny_t steamid=%llu raison=%d -> %s", pipe,
					*(const unsigned long long *)p, before, ok ? "14 (corrigé : le joueur est gardé)" : "ÉCHEC d'écriture");
			} else {
				Log("pipe %d : GSClientDeny_t steamid=%llu raison=%d (inchangé)", pipe,
					*(const unsigned long long *)p, before);
			}
		}
		break;
	}
}

static bool __cdecl Hook_BGetCallback(int hSteamPipe, CallbackMsg_t *pMsg)
{
	bool ret = g_realBGetCallback(hSteamPipe, pMsg);
	if (ret && pMsg && pMsg->m_pubParam && pMsg->m_cubParam > 0)
		OnCallback(hSteamPipe, pMsg);
	return ret;
}

static FARPROC WINAPI Hook_GetProcAddress(HMODULE hModule, LPCSTR name)
{
	FARPROC p = g_realGetProcAddress(hModule, name);
	if (p && ((ULONG_PTR)name >> 16) != 0 && strcmp(name, "Steam_BGetCallback") == 0) {
		g_realBGetCallback = (PFN_BGetCallback)p;
		InterlockedOr(&g_status, 2);
		Log("Steam_BGetCallback résolu par steam_api.dll (%p) : enveloppe installée", (void *)p);
		return (FARPROC)Hook_BGetCallback;
	}
	return p;
}

static void **FindIatSlot(HMODULE mod, const char *dll, const char *func)
{
	BYTE *base = (BYTE *)mod;
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return NULL;
	IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
	IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress)
		return NULL;
	for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
		if (_stricmp((const char *)(base + d->Name), dll) != 0 || !d->OriginalFirstThunk)
			continue;
		IMAGE_THUNK_DATA32 *names = (IMAGE_THUNK_DATA32 *)(base + d->OriginalFirstThunk);
		IMAGE_THUNK_DATA32 *iat = (IMAGE_THUNK_DATA32 *)(base + d->FirstThunk);
		for (; names->u1.AddressOfData; names++, iat++) {
			if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal))
				continue;
			IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
			if (strcmp((const char *)ibn->Name, func) == 0)
				return (void **)&iat->u1.Function;
		}
	}
	return NULL;
}

static void PatchSteamApi(HMODULE steamApi)
{
	if (g_status & 1)
		return;
	void **slot = FindIatSlot(steamApi, "KERNEL32.dll", "GetProcAddress");
	if (!slot) {
		Log("steam_api.dll : import KERNEL32!GetProcAddress introuvable, correctif INACTIF (version de steam_api inattendue ?)");
		return;
	}
	DWORD old;
	if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) {
		Log("steam_api.dll : VirtualProtect a échoué (%lu), correctif INACTIF", GetLastError());
		return;
	}
	g_realGetProcAddress = (PFN_GetProcAddress)*slot;
	*slot = (void *)Hook_GetProcAddress;
	VirtualProtect(slot, sizeof(void *), old, &old);
	InterlockedOr(&g_status, 1);
	Log("steam_api.dll @ %p : entrée IAT GetProcAddress (%p) remplacée", (void *)steamApi, (void *)slot);
}

// wait steam_api.dll si cette merde de DllMain veut load
typedef struct { ULONG Flags; const void *FullDllName; const void *BaseDllName; PVOID DllBase; ULONG SizeOfImage; } LDR_DLL_LOADED_NOTIFICATION_DATA;
typedef VOID(CALLBACK *PLDR_DLL_NOTIFICATION_FUNCTION)(ULONG, const LDR_DLL_LOADED_NOTIFICATION_DATA *, PVOID);
typedef LONG(NTAPI *PFN_LdrRegisterDllNotification)(ULONG, PLDR_DLL_NOTIFICATION_FUNCTION, PVOID, PVOID *);
static PVOID g_notifCookie;

static VOID CALLBACK OnDllNotification(ULONG reason, const LDR_DLL_LOADED_NOTIFICATION_DATA *data, PVOID)
{
	if (reason != 1 || (g_status & 1))
		return;
	HMODULE h = GetModuleHandleA("steam_api.dll");
	if (h && (PVOID)h == data->DllBase)
		PatchSteamApi(h);
}

static void **const kCamVtable = (void **)0x01897DD4;
static float *const kFovOption = (float *)0x01833030;
enum { kSlotSetAdjusted = 25, kSlotPerFrame = 43 };
static void *const kExpectedSetAdjusted = (void *)0x00744120;
static void *const kExpectedPerFrame = (void *)0x004245C0;
enum { kCamFov = 0xB8, kCamBase = 0xBC };

typedef void(__thiscall *PFN_SetAdjustedFov)(void *cam, float fov);
static PFN_SetAdjustedFov g_setAdjusted;
static void *g_origPerFrame;
static float *g_fovOption;
static volatile LONG g_fovApplied;
static void *g_lastStuckCam;

static void __cdecl FovCheck(unsigned char *cam)
{
	__try {
		float opt = *g_fovOption;
		if (!(opt > 0.0f))
			return;
		float fov = *(float *)(cam + kCamFov), base = *(float *)(cam + kCamBase);
		if (fov != base || !(base > 1.0f && base < 179.0f) || cam == g_lastStuckCam)
			return;
		g_setAdjusted(cam, base);
		float after = *(float *)(cam + kCamFov);
		if (after == base)
			g_lastStuckCam = cam;
		LONG n = InterlockedIncrement(&g_fovApplied);
		if (n <= 200)
			Log("FOV : caméra %p brute %.1f -> %.1f (-fov:%.0f)%s", cam, base, after, opt,
				n == 200 ? " ; journal FOV arrêté après 200 lignes" : "");
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

static bool SwapVtableSlot(void **slot, void *hook, void **orig)
{
	DWORD old;
	if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
		return false;
	*orig = *slot;
	InterlockedExchangePointer(slot, hook);
	VirtualProtect(slot, sizeof(void *), old, &old);
	return true;
}

static __declspec(naked) void Hook_PerFrame()
{
	__asm {
		pushfd
		pushad
		push ecx
		call FovCheck
		add esp, 4
		popad
		popfd
		jmp dword ptr [g_origPerFrame]
	}
}

static bool InstallFovHook(void **vt, float *fovOption, bool checkExpected)
{
	if (g_origPerFrame)
		return true;
	__try {
		if (checkExpected && (vt[kSlotSetAdjusted] != kExpectedSetAdjusted || vt[kSlotPerFrame] != kExpectedPerFrame)) {
			Log("FOV : vtable caméra inattendue (slot 25 = %p, slot 43 = %p) : correctif FOV INACTIF (autre version du jeu ?)",
				vt[kSlotSetAdjusted], vt[kSlotPerFrame]);
			return false;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		Log("FOV : vtable caméra illisible : correctif FOV INACTIF");
		return false;
	}
	g_setAdjusted = (PFN_SetAdjustedFov)vt[kSlotSetAdjusted];
	g_fovOption = fovOption;
	if (!SwapVtableSlot(&vt[kSlotPerFrame], (void *)Hook_PerFrame, &g_origPerFrame)) {
		Log("FOV : VirtualProtect a échoué (%lu) : correctif FOV INACTIF", GetLastError());
		return false;
	}
	Log("FOV : slot 43 de la vtable caméra %p remplacé (origine %p) ; -fov actuel = %.1f", (void *)vt, g_origPerFrame,
		*fovOption);
	return true;
}

static void InstallFovFixForGame()
{
	static volatile LONG tried;
	if (!g_cfg.enabled || !g_cfg.fov || InterlockedExchange(&tried, 1))
		return;
	if (GetModuleHandleA(NULL) != (HMODULE)0x00400000) {
		Log("FOV : exe chargé ailleurs qu'en 0x400000 : correctif FOV INACTIF");
		return;
	}
	InstallFovHook(kCamVtable, kFovOption, true);
}

static void **const kVoiceVtable = (void **)0x019642F0;
enum { kSlotVoiceUpdate = 1, kVoicePttKey = 0x0C };
static void *const kExpectedVoiceUpdate = (void *)0x005D1DA0;
enum { kVkNeverPressed = 0x07 };

static void *g_origVoiceUpdate;
static volatile LONG g_voiceOn;
static bool g_voiceKeyWasDown;
static int g_testKeyState = -1;
static volatile LONG g_voiceToggles;

static bool GameInForeground()
{
	DWORD pid = 0;
	HWND w = GetForegroundWindow();
	return w && GetWindowThreadProcessId(w, &pid) && pid == GetCurrentProcessId();
}

static DWORD WINAPI BeepThread(LPVOID freq)
{
	Beep((DWORD)(ULONG_PTR)freq, 70);
	return 0;
}

static void __cdecl VoiceCheck(unsigned char *voiceMgr)
{
	__try {
		int *key = (int *)(voiceMgr + kVoicePttKey);
		if (g_cfg.voiceMode == kVoiceHold) {
			*key = g_cfg.voiceKey;
			return;
		}
		bool down = g_testKeyState >= 0 ? g_testKeyState != 0
			: (GetAsyncKeyState(g_cfg.voiceKey) & 0x8000) != 0 && GameInForeground();
		if (down && !g_voiceKeyWasDown) {
			LONG on = !g_voiceOn;
			InterlockedExchange(&g_voiceOn, on);
			LONG n = InterlockedIncrement(&g_voiceToggles);
			if (n <= 500)
				Log("micro %s (touche %s)", on ? "ACTIF" : "COUPÉ", g_cfg.voiceKeyText);
			if (g_cfg.voiceSound)
				QueueUserWorkItem(BeepThread, (LPVOID)(ULONG_PTR)(on ? 880 : 440), WT_EXECUTEDEFAULT);
		}
		g_voiceKeyWasDown = down;
		*key = g_voiceOn ? 0 : kVkNeverPressed;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

static __declspec(naked) void Hook_VoiceUpdate()
{
	__asm {
		pushfd
		pushad
		push ecx
		call VoiceCheck
		add esp, 4
		popad
		popfd
		jmp dword ptr [g_origVoiceUpdate]
	}
}

static bool InstallVoiceHook(void **vt, bool checkExpected)
{
	if (g_origVoiceUpdate)
		return true;
	__try {
		if (checkExpected && vt[kSlotVoiceUpdate] != kExpectedVoiceUpdate) {
			Log("micro : vtable VoiceMgr inattendue (slot 1 = %p) : réglage du micro INACTIF (autre version du jeu ?)",
				vt[kSlotVoiceUpdate]);
			return false;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		Log("micro : vtable VoiceMgr illisible : réglage du micro INACTIF");
		return false;
	}
	if (!SwapVtableSlot(&vt[kSlotVoiceUpdate], (void *)Hook_VoiceUpdate, &g_origVoiceUpdate)) {
		Log("micro : VirtualProtect a échoué (%lu) : réglage du micro INACTIF", GetLastError());
		return false;
	}
	Log("micro : slot 1 de la vtable VoiceMgr %p remplacé (origine %p) ; mode %s, touche %s (0x%02X)%s", (void *)vt,
		g_origVoiceUpdate, g_cfg.voiceMode == kVoiceToggle ? "bascule (coupé au départ)" : "push-to-talk",
		g_cfg.voiceKeyText, g_cfg.voiceKey, g_cfg.voiceSound ? ", bip" : "");
	return true;
}

static void InstallVoiceFixForGame()
{
	static volatile LONG tried;
	if (!g_cfg.enabled || g_cfg.voiceMode == kVoiceOpen || InterlockedExchange(&tried, 1))
		return;
	if (!g_cfg.voiceKey) {
		Log("micro : touche \"%s\" inconnue dans fear3fix.ini : micro laissé ouvert", g_cfg.voiceKeyText);
		return;
	}
	if (GetModuleHandleA(NULL) != (HMODULE)0x00400000) {
		Log("micro : exe chargé ailleurs qu'en 0x400000 : réglage du micro INACTIF");
		return;
	}
	InstallVoiceHook(kVoiceVtable, true);
}

static DWORD WINAPI VtableFallbackThread(LPVOID)
{
	Sleep(20000);
	InstallFovFixForGame();
	InstallVoiceFixForGame();
	return 0;
}

extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE hinst, DWORD ver, REFIID riid, LPVOID *out, void *outer)
{
	static PFN_DirectInput8Create real;
	InstallFovFixForGame();
	InstallVoiceFixForGame();
	if (!real) {
		char path[MAX_PATH];
		UINT n = GetSystemDirectoryA(path, MAX_PATH);
		if (n == 0 || n >= MAX_PATH - 16)
			return E_FAIL;
		strcat_s(path, "\\dinput8.dll");
		HMODULE h = LoadLibraryA(path);
		if (h)
			real = (PFN_DirectInput8Create)GetProcAddress(h, "DirectInput8Create");
		if (!real) {
			Log("ERREUR : impossible de charger %s", path);
			return E_FAIL;
		}
	}
	return real(hinst, ver, riid, out, outer);
}

/* ---------- auto-test (old test) ---------- */

extern "C" int __cdecl Fear3Fix_TestRewrite(int reason)
{
	unsigned char buf[140] = { 0 };
	*(unsigned long long *)buf = 76561197960265729ULL;
	*(int *)(buf + 8) = reason;
	CallbackMsg_t m = { 1, kGSClientDeny, buf, (int)sizeof(buf) };
	OnCallback(-1, &m);
	return *(int *)(buf + 8);
}

extern "C" int __cdecl Fear3Fix_TestFovInstall(void **vt, float *fovOption)
{
	return InstallFovHook(vt, fovOption, false) ? 1 : 0;
}

extern "C" int __cdecl Fear3Fix_FovApplied()
{
	return (int)g_fovApplied;
}

extern "C" int __cdecl Fear3Fix_TestVoiceInstall(void **vt, int mode, int vk)
{
	g_cfg.voiceMode = (VoiceMode)mode;
	g_cfg.voiceKey = vk;
	strcpy_s(g_cfg.voiceKeyText, "test");
	g_cfg.voiceSound = false;
	return InstallVoiceHook(vt, false) ? 1 : 0;
}

extern "C" int __cdecl Fear3Fix_TestParseKey(const char *name)
{
	return ParseKey(name);
}

extern "C" int __cdecl Fear3Fix_TestVoiceKey(int keyState)
{
	g_testKeyState = keyState;
	return (int)g_voiceOn;
}

extern "C" int __cdecl Fear3Fix_Status()
{
	return (int)g_status | (g_cfg.enabled ? 0x100 : 0) | ((int)g_rewrites << 16);
}

/* ---------- start ---------- */

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
	if (reason != DLL_PROCESS_ATTACH)
		return TRUE;
	DisableThreadLibraryCalls(hinst);
	InitializeCriticalSection(&g_logLock);
	GetModuleFileNameA(hinst, g_dir, MAX_PATH);
	char *slash = strrchr(g_dir, '\\');
	if (slash)
		*slash = 0;
	LoadConfig();

	char exe[MAX_PATH];
	GetModuleFileNameA(NULL, exe, MAX_PATH);
	static const char *const kModeNames[] = { "open", "hold", "toggle" };
	Log("fear3fix %s chargé dans %s ; enabled=%d reasons=%s fov=%d voice=%s key=%s", FEAR3FIX_VERSION, exe,
		g_cfg.enabled ? 1 : 0, g_cfg.reasonsText, g_cfg.fov ? 1 : 0, kModeNames[g_cfg.voiceMode], g_cfg.voiceKeyText);
	if (!g_cfg.enabled) {
		Log("correctif désactivé (fear3fix.ini) : simple proxy dinput8");
		return TRUE;
	}

	if ((g_cfg.fov || g_cfg.voiceMode != kVoiceOpen) && GetModuleHandleA(NULL) == (HMODULE)0x00400000) {
		HANDLE t = CreateThread(NULL, 0, VtableFallbackThread, NULL, 0, NULL);
		if (t)
			CloseHandle(t);
	}

	HMODULE steamApi = GetModuleHandleA("steam_api.dll");
	if (steamApi) {
		PatchSteamApi(steamApi);
	} else {
		PFN_LdrRegisterDllNotification reg =
			(PFN_LdrRegisterDllNotification)GetProcAddress(GetModuleHandleA("ntdll.dll"), "LdrRegisterDllNotification");
		if (reg && reg(0, OnDllNotification, NULL, &g_notifCookie) == 0)
			Log("steam_api.dll pas encore chargée : en attente de son chargement");
		else
			Log("steam_api.dll absente et notification impossible : correctif INACTIF");
	}
	return TRUE;
}
