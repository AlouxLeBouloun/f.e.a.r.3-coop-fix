# Technical notes
note (this part is hard writing with ai, too lazy to explain but is mainly just old steam doesn't work with the new one)

These notes summarize how the disconnection was diagnosed and how the fix works. Everything was observed on the
Steam version of F.E.A.R. 3 (app 21100, build 3576) with its bundled `steam_api.dll` from the Steamworks SDK
**v1.13** (SHA-256 `11C7A5DF676FFE1AA0A498103D061E8C594A3D9DF3B9CC23E5B5F642B12F7163`, byte-identical to the SDK
v1.13 redistributable).

## Symptom

In co-op (Steam lobby), the session ends roughly 2 minutes after the second player joins, including in the level
selection menu. Host: "lobby closed". Client: "the host kicked you".

## What actually happens

Observed with a Frida agent (read-only hooks on `steam_api.dll` / `steamclient.dll`, none in the game exe), a socket
logger, packet captures and the Steam client logs, in two sessions with swapped host/client roles:

- When hosting, the game starts a Steam game server (`SteamGameServer_Init`, `eServerModeAuthenticationAndSecure`)
  and registers the game-server authentication callbacks (`GSClientApprove_t`, `GSClientDeny_t`, `GSClientKick_t`),
  i.e. joining players are authenticated through the 2011-era game-server API (presumably
  `ISteamUser::InitiateGameConnection` / `ISteamGameServer::SendUserConnectAndAuthenticate`; those two calls were not
  traced directly).
- **`GSClientApprove_t` (201) never arrives.** About 2 minutes after the player joins (117 s and ~122 s in the two
  sessions), Steam delivers **`GSClientDeny_t` (202) with `m_eDenyReason = 12` (`k_EDenySteamConnectionError`)**
  to the host's game server.
- Nobody actually lost connectivity: both Steam clients stayed logged on, the game server's own Steam connection
  stayed up until the game shut it down, pings showed no loss, and the P2P session (direct, no relay) carried
  traffic until the kick.
- The game then kicks the player and the host goes back to a "delete session" state that leaves the lobby and
  shuts the game server down. Both messages come from that single refusal.

## Where the game decides

Addresses are for the game exe as loaded (it has no ASLR). Class names come from the game's RTTI.

`NetConnectionSteamMgr::OnGSClientDeny` at `0x0059FBB0` (the `CCallback<NetConnectionSteamMgr, GSClientDeny_t>`
handler), with `ebp` = `GSClientDeny_t*`:

```
0059FBE0  call [vtbl(conn)+0x40]      ; peer = conn->FindPeer(deny->m_SteamID)
0059FBEA  cmp  dword ptr [ebp+8], 0Eh ; deny->m_eDenyReason == 14 (k_EDenySteamValidationStalled) ?
0059FBEE  je   0059FC32               ; yes: keep the player
0059FC28  call [vtbl(conn)+0x120]     ; no: conn->Disconnect(peer)  -> kick
```

The second handler, `Client::OnGSClientDeny` at `0x00EEFC50`, treats reason 14 exactly like `GSClientApprove_t`
(marks the client as accepted). In other words, the developers explicitly anticipated "Steam validation stalled"
and chose to keep the player in that case. Today's Steam reports the same never-completing validation with reason
12, which the game does not recognize.

The host-side teardown afterwards is `NetConnectionSteamStateDelete::OnEnter` (`0x00586F30`):
`SetLobbyJoinable(false)` → `LeaveLobby` → `SteamGameServer_Shutdown`.

## The fix

Smallest possible change: rewrite `m_eDenyReason` 12 → 14 in the `GSClientDeny_t` message before the game reads it.
The game then takes its own intended path (player kept and marked as accepted). Other deny reasons, `GSClientKick_t`
and all other callbacks are untouched.

How, **without modifying any code bytes** (not in the exe, not in `steam_api.dll`, not in `steamclient.dll`):

- `steam_api.dll` v1.13 resolves `Steam_BGetCallback` from `steamclient.dll` **once**, by name, through
  `GetProcAddress` read from its import address table (`steam_api+0x1332`: `mov ebx, [IAT GetProcAddress]`;
  `push "Steam_BGetCallback"`; result stored and reused for every callback).
- `dinput8.dll` (a proxy: the exe imports `DINPUT8.dll!DirectInput8Create`, which is forwarded to the system DLL)
  is loaded before `SteamAPI_Init`. In `DllMain` it replaces **one data pointer**: the `GetProcAddress` entry of
  `steam_api.dll`'s IAT (found by walking the import table, no hard-coded address).
- For `"Steam_BGetCallback"` it returns a wrapper: call the real function, then, if the message is
  `GSClientDeny_t` (id 202, `m_cubParam` ≥ 12) and the reason at `m_pubParam + 8` is in the configured list,
  write 14.

`CallbackMsg_t` layout (SDK v1.13): `HSteamUser m_hSteamUser; int m_iCallback; uint8 *m_pubParam; int m_cubParam;`.
`GSClientDeny_t`: `CSteamID m_SteamID; EDenyReason m_eDenyReason; char m_rgchOptionalText[128];` (140 bytes).

Note: the game exe contains anti-tamper code that ends the process (`ExitProcess(0x8000DEAD)`) in some
situations. This fix leaves the exe untouched and does not interfere with that protection.

## Validation

- Test harness built against the SDK v1.13 headers (real Steam, no game): IAT entry replaced, wrapper installed,
  real callbacks still delivered, rewrite logic 12 → 14 with other reasons unchanged, `DirectInput8Create` forwarded,
  `enabled=0` gives a plain proxy.
- Real co-op session (fix on the host): `GSClientDeny_t` reason 12 arrived at the usual ~2 minutes, was rewritten to
  14, and the session went on normally for several more minutes until the players quit.
- Both `steam_api` resolutions are covered (client at `SteamAPI_Init`, game server at `SteamGameServer_Init`).

## FOV fix (v1.1)

### Symptom

With the `-fov:N` launch option, the FOV goes back to the default as soon as Paxton sprints, possesses an enemy or
leaves a possession. This happens in solo and in co-op.

### What the game does

- `-fov:N` is read by `0x008D5E90`. It is clamped to [65, 140] and stored as a float in degrees at `0x01833030`
  (-1 when the option is absent). The format is `fov:%f`, so the option must be written `-fov:110`, not `-fov=110`.
- Its only reader is `Despair::BaseCameraComponent::SetAdjustedCameraFOV` (vtable `0x01897DD4`, slot 25,
  `0x00744120`). With `r = base / 85`, it sets the final FOV (`+0xB8`) to
  `clamp(r * N, max(min(65r, req), 0.75 req), min(max(105r, req), 1.3 req))` and leaves the base FOV (`+0xBC`)
  untouched. With a base of 85 and `-fov:110`, that gives 105.
- New cameras get their FOV from the game data (`cameraFOV` property setter `0x004E7A20`: final = base). Only the first
  player camera gets a call to `SetAdjustedCameraFOV`. Observed in game: the first camera is 105/85; after a sprint it is
  destroyed and every following camera is 85/85, 95/95, 55/55… (final == base).
- A per-slot call counter on the vtable (diagnostic only) showed that slot 25 is never called again, and that slot 43
  (`0x004245C0`) is called once per frame on the active camera.

### The fix

- At the first `DirectInput8Create` (fallback: 20 s after load), if the exe is loaded at `0x400000` and the vtable
  holds the expected pointers (slot 25 = `0x00744120`, slot 43 = `0x004245C0`), the slot 43 **pointer** is replaced
  by a stub. The stub runs `pushfd; pushad; push ecx; call FovCheck; add esp, 4; popad; popfd; jmp [original]`, so it
  is transparent whatever the method's signature.
- `FovCheck(cam)`: if `-fov:N` is set and the camera is still raw (final == base), call the original
  `SetAdjustedCameraFOV(cam, base)`, which is exactly what the game does for its first camera. The call happens once
  per camera, since afterwards final != base. All of it is SEH-guarded.
- No code byte of the exe is modified; only one data pointer, in a writable section (`Game_dat`), is changed.

### Validation

- Test harness with a fake 47-entry vtable:
  - 85/85 → 105/85 on the first call, no further calls;
  - 55/55 → 67.9;
  - nothing happens without `-fov`;
  - `this`, the arguments and `esi`/`edi`/`ebx` are preserved over 100 calls.
- In game (Paxton, co-op): the FOV stays at the `-fov` value through sprints and possessions.

## Microphone toggle (v1.2)

### What the game does

- Voice goes through the Steamworks v1.13 voice API (`ISteamUser::GetVoice`). `Despair::VoiceMgr` (vtable
  `0x019642F0`) runs an update every frame: slot 1, `0x005D1DA0`, which calls `0x00659630`.
- `0x00659630` reads a push-to-talk key at `VoiceMgr+0xC`:
  - `0`: the voice is always sent (open mic);
  - otherwise, `GetAsyncKeyState(key)`. While the key is held, the voice is sent and Steam is told the player is
    speaking (`ISteamFriends::SetInGameVoiceSpeaking`). When it is up, the recorded voice is discarded and Steam is
    told the player is not speaking.
- At startup, `0x00CB48A0` sets the key to 0. Then, unless `-noptt` is given, it reads Steam's
  `config\localconfig.vdf` (found via `ISteamUser::GetUserDataFolder`) and loads `PushToTalkKey` only if
  `UsePushToTalk` is non-zero. Today's Steam no longer writes `UsePushToTalk`, so the key stays 0 and the mic is
  always open.

### The fix

- Same technique as the FOV fix: the slot 1 **pointer** of the `VoiceMgr` vtable (in a writable section) is replaced
  by a transparent stub (`pushfd; pushad; push ecx; call VoiceCheck; add esp, 4; popad; popfd; jmp [original]`),
  after checking that it holds `0x005D1DA0`. No code byte is modified.
- `VoiceCheck(voiceMgr)`, before each update:
  - `hold`: write the configured key to `+0xC`. This is the game's own push-to-talk.
  - `toggle`: a press of the key (rising edge, counted only when the foreground window belongs to the game) flips
    the state. On: write `0` (the game's open-mic path). Off: write `0x07`, an unassigned virtual-key code that is
    never down, so the game discards the voice. Optional 70 ms beep on a thread-pool thread.
- Without a `[voice]` section (ini from v1.1 or older) the mode is `open` and nothing is installed.

### Validation

- Test harness with a fake `VoiceMgr` vtable:
  - toggle: off at start (`0x07`), first press → on (`0`), held key → no extra switch, second press → off, third → on;
  - hold: the game sees the configured key;
  - key names (letters, F-keys, mouse buttons, numpad, hex codes, unknown names rejected);
  - `this`, the argument and `esi`/`edi`/`ebx` are preserved.
- In game (co-op): the friend does not hear the player at start, hears them after a press of V, and no longer
  hears them after a second press.
