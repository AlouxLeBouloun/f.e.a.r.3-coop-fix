# fear3-coop-fix

Fix for the **co-op disconnections of F.E.A.R. 3 (Steam)**: after a friend joins, the host sees
*"lobby closed"* and the friend sees *"the host kicked you"*.

Since v1.1 it also fixes the **FOV reset when playing Paxton Fettel**: the `-fov:N` launch option is lost after a
sprint, a possession or leaving a possession.

Since v1.2 it adds a **toggle key for the microphone** (V by default, changeable) instead of an always-open mic.

*[Français plus bas](#français)*

---

## English

### What it fixes / what does it happen

About ~2 minutes after a player joins the lobby, Steam sends the host's game server an authentication refusal
(`GSClientDeny_t`, reason 12 = `k_EDenySteamConnectionError`) even though nobody lost their connection: the
2011-era Steam authentication used by the game never completes on today's Steam. The game treats that refusal as
fatal: it kicks the player, then closes the lobby.

The game already tolerates the "Steam validation stalled" case (reason 14): it keeps the player. This fix rewrites
reason 12 into 14 at the moment Steam delivers the message, so the game follows the path its developers intended.
Nothing else is changed. Technical details: [docs/TECHNICAL.md](docs/TECHNICAL.md).

### FOV fix (v1.1)

The game only applies the `-fov:N` launch option (e.g. `-fov:110` in Steam → Properties → Launch options) to the
first player camera. Paxton's sprint and possessions create new cameras that keep the default FOV (85°). The fix
re-applies the option to every new camera, the same way the game does for the first one. Without `-fov:N` it does
nothing. Note: the game itself caps the result (with `-fov:110` you get 105° instead of 85°). It also widens the
other cameras created during the game, for example the aiming view.

### Microphone toggle (v1.2)

The game has a push-to-talk built in, but it reads its key from a Steam setting that today's Steam no longer
writes, so the mic is always open. The fix gives it a key, set in the `[voice]` section of `fear3fix.ini`:

- `mode=toggle` (default): press **V** to turn your mic on, press it again to turn it off. The mic is **off** when
  the game starts. A short beep tells you the new state (high = on, low = off; `sound=0` to disable it). The key
  only counts while the game window is in front.
- `mode=hold`: classic push-to-talk, the mic is on only while you hold the key.
- `mode=open`: always-open mic, like the original game.
- `key=`: any key you want, e.g. `key=B`, `key=F5`, `key=MOUSE4`, `key=CAPSLOCK`, `key=NUMPAD0`, or a Windows
  virtual-key code such as `key=0x56` (the full list is in `fear3fix.ini`). Avoid keys the game already uses.

### Who needs it

- Co-op disconnection fix: only the **host** needs it, the second player it not required (the refusal reaches only the host).
- FOV fix: every player who wants it (it only changes your own camera).
- Microphone toggle: every player who wants it (it only changes your own mic).

### Install

1. Make sure the game is close.
2. Download `fear3fix_v1.2.zip` from the [Releases](../../releases) .
3. Copy `dinput8.dll` and `fear3fix.ini` into the game folder, next to `F.E.A.R. 3.exe`
   (Steam → right-click the game → Manage → Browse local files).
4. Start the game from Steam as usual.
5. enjoy.

No game file is replaced or modified (`dinput8.dll` does not exist in that folder originally).

### Check that it works

After playing, `fear3fix.log` appears next to the game exe:
- at startup: `entrée IAT GetProcAddress (…) remplacée` then `Steam_BGetCallback résolu … enveloppe installée`;
- on the host, when the disconnection would have happened:
  `GSClientDeny_t steamid=… raison=12 -> 14 (corrigé : le joueur est gardé)`;
- FOV fix: `FOV : slot 43 de la vtable caméra … remplacé` at startup, then for each new camera
  `FOV : caméra … brute 85.0 -> 105.0 (-fov:110)`;
- microphone: `micro : slot 1 de la vtable VoiceMgr … remplacé ; mode bascule…` at startup, then
  `micro ACTIF` / `micro COUPÉ` each time you press the key.

(The log is in French. because why not)
you can disable it in `fear3fix.ini`

### Disable / uninstall

- Disable: set `enabled=0` in `fear3fix.ini` (everything), `enabled=0` in its `[fov]` section (FOV fix only), or
  `mode=open` in its `[voice]` section (back to the always-open mic).
- Uninstall: delete `dinput8.dll`, `fear3fix.ini` and `fear3fix.log` from the game folder.

### Limits and risks

- A player who **really** lost their Steam connection is no longer kicked for that reason (a real network loss is still handled by the game's own timeouts).
- Conflicts with any other mod also named `dinput8.dll` (ASI loaders, etc.).
- Proxy DLLs are sometimes flagged by antivirus software: make a exception for it 
- The FOV fix and the microphone toggle rely on fixed addresses of the Steam exe (build 3576). They check them
  first and stay inactive (logged in `fear3fix.log`) if they do not match.
- With the toggle, the mic is off when the game starts: press the key (V) before talking.
- F.E.A.R. 3 has no VAC. This fix only touches the game's co-op authentication handling; it gives no gameplay advantage.

### Compatibility

Steam version of F.E.A.R. 3 (app 21100, build 3576), Windows, 32-bit game. Relies on the game's
`steam_api.dll` (Steamworks SDK v1.13). 

And no gog doesn't work since there is no multiplayer
### Build it yourself if you want

`build.cmd` (Visual Studio or Build Tools with the C++ x86/x64 tools) → `build\dinput8.dll`.

### Disclaimer

Unofficial community fix, not affiliated with or endorsed by Warner Bros., Day 1 Studios, Monolith or Valve.
F.E.A.R. and Steam are trademarks of their respective owners. No game file is distributed. Use at your own risk.
(even is pretty low chance but since Warner Bros don't care much about this franchise anymore i don't see much of a risk but who knows)

---

## Français

### Ce que ça corrige

Environ ~2 minutes après l'arrivée d'un joueur, Steam envoie au serveur de jeu de celui qui héberge un refus
d'authentification (`GSClientDeny_t`, raison 12 = `k_EDenySteamConnectionError`), alors que personne n'a perdu sa
connexion : l'authentification Steam de 2011 utilisée par le jeu n'aboutit jamais avec le Steam actuel. Le jeu traite
ce refus comme fatal : il expulse le joueur (« le host t'a kické »), puis ferme le lobby (« le lobby est fermé »).

Le jeu tolère déjà le cas « validation Steam qui traîne » (raison 14) : il garde le joueur. Le correctif réécrit la
raison 12 en 14 au moment où Steam livre ce message, pour que le jeu suive le chemin prévu par ses développeurs.
Rien d'autre n'est modifié. Détails techniques (en anglais) : [docs/TECHNICAL.md](docs/TECHNICAL.md).

### Correctif FOV (v1.1)

Le jeu n'applique l'option de lancement `-fov:N` (par ex. `-fov:110` dans Steam → Propriétés → Options de
lancement) qu'à la première caméra du joueur. Le sprint et les possessions de Paxton créent de nouvelles caméras qui
gardent le FOV par défaut (85°). Le correctif réapplique l'option à chaque nouvelle caméra, comme le jeu le fait pour
la première. Sans `-fov:N`, il ne fait rien. À noter : le jeu plafonne lui-même le résultat (avec `-fov:110`, on
obtient 105° au lieu de 85°). Les autres caméras créées en cours de partie, comme la vue de visée, sont aussi élargies.

### Bascule du micro (v1.2)

Le jeu a un push-to-talk intégré, mais il lit sa touche dans un réglage de Steam que le Steam actuel n'écrit plus :
le micro reste donc toujours ouvert. Le correctif lui donne une touche, réglée dans la section `[voice]` de
`fear3fix.ini` :

- `mode=toggle` (par défaut) : appuyer sur **V** active le micro, appuyer à nouveau le coupe. Le micro est **coupé**
  au lancement du jeu. Un petit bip indique le nouvel état (aigu = actif, grave = coupé ; `sound=0` pour le retirer).
  La touche ne compte que si la fenêtre du jeu est au premier plan.
- `mode=hold` : push-to-talk classique, le micro n'est actif que tant que la touche est enfoncée.
- `mode=open` : micro toujours ouvert, comme le jeu d'origine.
- `key=` : la touche de votre choix, par ex. `key=B`, `key=F5`, `key=MOUSE4`, `key=CAPSLOCK`, `key=NUMPAD0`, ou un
  code de touche virtuelle Windows comme `key=0x56` (liste complète dans `fear3fix.ini`). Évitez les touches déjà
  utilisées par le jeu.

### Qui doit l'installer

- Correctif des déconnexions : seul **celui qui héberge** en a besoin (le refus arrive que a celui qui host le serveur).
- Correctif FOV : chaque joueur qui le veut (il ne change que sa propre caméra).
- Bascule du micro : chaque joueur qui le veut (elle ne change que son propre micro).

### Installation

1. Fermer le jeu.
2. Télécharger `fear3fix_v1.2.zip` dans la page [Releases](../../releases).
3. Copier `dinput8.dll` et `fear3fix.ini` dans le dossier du jeu, à côté de `F.E.A.R. 3.exe`
   (Steam → clic droit sur le jeu → Gérer → Parcourir les fichiers locaux).
4. Lancer le jeu normalement depuis Steam.

Aucun fichier du jeu n'est remplacé ni modifié (`dinput8.dll` n'existe pas d'origine dans ce dossier).

### Vérifier qu'il fonctionne

Après une partie, `fear3fix.log` apparaît à côté de l'exe :
- au démarrage : `entrée IAT GetProcAddress (…) remplacée` puis `Steam_BGetCallback résolu … enveloppe installée` ;
- chez celui qui héberge, au moment où la déconnexion aurait eu lieu :
  `GSClientDeny_t steamid=… raison=12 -> 14 (corrigé : le joueur est gardé)` ;
- correctif FOV : `FOV : slot 43 de la vtable caméra … remplacé` au démarrage, puis pour chaque nouvelle caméra
  `FOV : caméra … brute 85.0 -> 105.0 (-fov:110)` ;
- micro : `micro : slot 1 de la vtable VoiceMgr … remplacé ; mode bascule…` au démarrage, puis `micro ACTIF` /
  `micro COUPÉ` à chaque appui sur la touche.

### Désactiver / désinstaller

- Désactiver : `enabled=0` dans `fear3fix.ini` (tout), `enabled=0` dans sa section `[fov]` (FOV seulement), ou
  `mode=open` dans sa section `[voice]` (retour au micro toujours ouvert).
- Désinstaller : supprimer `dinput8.dll`, `fear3fix.ini` et `fear3fix.log` du dossier du jeu.

### Limites et risques

- Un joueur **réellement** coupé de Steam n'est plus expulsé pour ce motif (sans enjeu en coop entre amis ; une vraie
  coupure réseau reste gérée par les délais du jeu).
- Conflit avec un autre mod nommé lui aussi `dinput8.dll` (chargeurs ASI, etc.).
- Les DLL proxy sont parfois signalées par les antivirus : le code source est ici, vous pouvez la compiler vous-même.
- Le correctif FOV et la bascule du micro reposent sur des adresses fixes de l'exe Steam (build 3576). Ils les
  vérifient d'abord et restent inactifs (noté dans `fear3fix.log`) si elles ne correspondent pas.
- Avec la bascule, le micro est coupé au lancement du jeu : appuyez sur la touche (V) avant de parler.
- F.E.A.R. 3 n'a pas de VAC. Le correctif ne touche qu'à la gestion de l'authentification coop ; il ne donne aucun
  avantage en jeu.

### Compiler soi-même

`build.cmd` (Visual Studio ou Build Tools avec les outils C++ x86/x64) → `build\dinput8.dll`.

### Avertissement

Correctif communautaire non officiel, sans lien avec Warner Bros., Day 1 Studios, Monolith ou Valve. F.E.A.R. et
Steam sont des marques de leurs propriétaires respectifs. Aucun fichier du jeu n'est distribué. Utilisation à vos
risques.
