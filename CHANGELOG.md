# Changelog

## v1.1

- New: FOV fix for Paxton Fettel. The `-fov:N` launch option is now re-applied to the cameras the game creates
  during play (sprint, possession, leaving a possession), instead of falling back to the default 85°.
- New `[fov] enabled` setting in `fear3fix.ini` (on by default; does nothing without `-fov:N`).

## v1.0

- First release: rewrites `GSClientDeny_t` reason 12 (`k_EDenySteamConnectionError`) into 14
  (`k_EDenySteamValidationStalled`) so that the game keeps the player instead of kicking them and closing the lobby.
- `dinput8.dll` proxy, configurable with `fear3fix.ini` (`enabled`, `reasons`, `log`), log in `fear3fix.log`.
