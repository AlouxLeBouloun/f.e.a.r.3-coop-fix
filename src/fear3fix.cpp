#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FEAR3FIX_VERSION "1.1"

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

static struct Config {
	bool enabled;
	bool fov;
	bool log;
	int reasons[16];
	int nReasons;
	char reasonsText[64];
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

static void LoadConfig()
{
	char ini[MAX_PATH];
	_snprintf_s(ini, sizeof(ini), _TRUNCATE, "%s\\fear3fix.ini", g_dir);
	g_cfg.enabled = GetPrivateProfileIntA("fix", "enabled", 1, ini) != 0;
	g_cfg.log = GetPrivateProfileIntA("fix", "log", 1, ini) != 0;
	g_cfg.fov = GetPrivateProfileIntA("fov", "enabled", 1, ini) != 0;
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
	DWORD old;
	if (!VirtualProtect(&vt[kSlotPerFrame], sizeof(void *), PAGE_READWRITE, &old)) {
		Log("FOV : VirtualProtect a échoué (%lu) : correctif FOV INACTIF", GetLastError());
		return false;
	}
	g_setAdjusted = (PFN_SetAdjustedFov)vt[kSlotSetAdjusted];
	g_fovOption = fovOption;
	g_origPerFrame = vt[kSlotPerFrame];
	InterlockedExchangePointer(&vt[kSlotPerFrame], (void *)Hook_PerFrame);
	VirtualProtect(&vt[kSlotPerFrame], sizeof(void *), old, &old);
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

static DWORD WINAPI FovFallbackThread(LPVOID)
{
	Sleep(20000);
	InstallFovFixForGame();
	return 0;
}

extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE hinst, DWORD ver, REFIID riid, LPVOID *out, void *outer)
{
	static PFN_DirectInput8Create real;
	InstallFovFixForGame();
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
	Log("fear3fix %s chargé dans %s ; enabled=%d reasons=%s fov=%d", FEAR3FIX_VERSION, exe, g_cfg.enabled ? 1 : 0,
		g_cfg.reasonsText, g_cfg.fov ? 1 : 0);
	if (!g_cfg.enabled) {
		Log("correctif désactivé (fear3fix.ini) : simple proxy dinput8");
		return TRUE;
	}

	if (g_cfg.fov && GetModuleHandleA(NULL) == (HMODULE)0x00400000) {
		HANDLE t = CreateThread(NULL, 0, FovFallbackThread, NULL, 0, NULL);
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
