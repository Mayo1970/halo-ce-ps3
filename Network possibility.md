# Network possibility

Discord status and invites on the PS3 (2026-10-16).

**Short answer:** the PS3 cannot show a Discord status by itself. Discord
takes a status only from its desktop client's local socket, which is what
[p2p_discord.c](port/linux/src/p2p_discord.c) uses, or from its Social SDK,
which has no PS3 build. Something on a PC or a server must pass the status
on. A separate invite tab on the console is possible.

## Options

**1. A Discord relay on the PC (recommended)**

- All the Discord code is already in the PS3 build. Only the 4 stubs in
  [posix_ps3.c:333-357](port/ps3/xbox/posix_ps3.c#L333-L357) return -1.
- The change: those stubs open a TCP connection to a small relay program on
  the PC. A new `config.toml` setting gives the relay's address. The relay
  passes the bytes to Discord's local pipe.
- The result: when you host on the PS3, your Discord profile shows "Hosting
  a game". Friends can use Discord's "Invite to Join". When you accept an
  invite in Discord on the PC, the PS3 joins that game.
- The limits:
  - Discord must be running on the PC.
  - Upstream shows a status only while you host.
  - The relay must put its own process id in place of the PS3's.
  - The relay must accept connections only from the PS3's address.
- The machines send each other nothing new, so the network version does not
  change.

**2. A webhook, with no PC**

- When you host, the game posts a message to a Discord channel: "hosting on
  PS3: halo://join/...". This works over HTTPS, and Mbed TLS is already in
  the PS3 build. The webhook address goes in `config.toml` on the console.
- This is a message in a channel, not a profile status. Anyone who can read
  that channel can join with the link.

**3. An INVITES tab next to INTERNET PLAY**

- For the host: the tab shows the game's invite. Today the invite is only in
  the log. A QR code on the screen would let a phone copy it.
- For joining: an invite link has 76 characters, which is too long to type
  with a pad. There are three ways to get it in:
  - (a) Put an `invite.txt` file on the console, by FTP or USB. This works
    like the PC's `network.join_from_clipboard`.
  - (b) Accept the invite in Discord, through the relay of option 1.
  - (c) Type a short code, as the Vita port does. Codes work only between
    PS3s, and they need a network version change.

Recommendation: 1, plus 3 with 3a and 3b.

## How the other ports handle it

| Port | Discord / status | Invites |
| --- | --- | --- |
| PS Vita (BirchWoodGod/halo-ce-vita) | No Discord and no status. A line in the settings panel shows the name, the address, what the game is doing and the code. | An 8-letter code (`ABCD-EFGH`), typed on the Vita's keyboard. Codes work between Vitas only. There is also a server browser. |
| 3DS (Generalkidd/Halo3DS) | None | Its README describes no online play. 3DS local wireless play is only a stated goal. |
| PS4 | No public repo found, only news posts. | No information |
| OpenCE Android and web | No Discord ([port/android/README.md](port/android/README.md)) | Links and the server browser |

The Vita port is GPL-3, so we can read its approach but not copy its code.
Other console tools work the same way: PS3-Rich-Presence-for-Discord, which
needs webMAN MOD on the PS3, and 3DS-RPC both run on a PC or a server.

## Online co-op campaign

**Status:** the online co-op netcode is already in the PS3 build. Upstream's
co-op code (`network_coop.c`, `coop_enemies.c`, `coop_spectate.c`) compiles
for the PS3, and its messages are byte-swapped (network version 29). Matt
tested PS3-to-PC LAN play on a multiplayer map (2026-10-16). Online co-op on
the PS3 is not tested yet. The missing parts are the menus and a hardware
test.

**What should work now, with no code change**

- Joining a PC's co-op game from LAN PLAY. LAN PLAY lists the network's
  games and does not filter out co-op games.
- Joining a PC's co-op game from INTERNET PLAY. This works only if the PC
  host sets LISTING to PUBLIC. Co-op games start as PRIVATE, because
  `network.coop_public` defaults to `false`.
- What does not work: joining a private co-op game. The PS3 has no way to
  take an invite yet (option 3 above).

**What is missing**

1. Hosting co-op from the PS3. A network game becomes co-op only through
   `ui_widget_port_cooperative_level_choose`
   ([ui_widget_event_handler_functions.c:5141](source/interface/ui_widget_event_handler_functions.c#L5141)).
   Only the PC menus' Map screen calls it
   ([menu_functions.c:2497](port/linux/game/menu_functions.c#L2497)). The
   Xbox's CO-OP CAMPAIGN screen is local split screen only. The fix: add an
   ONLINE choice to the Xbox co-op level and difficulty screens, and call
   that function from it.
2. Others finding a PS3-hosted game. A game that the PS3 hosts does not
   appear in the server browser
   ([port/ps3/README.md:191](port/ps3/README.md#L191)). So other players can
   reach PS3-hosted co-op only by invite. This needs the invite tab
   (option 3), or a fix to the listing.
3. Co-op options. The Xbox menus have no Server Setup screen for co-op. The
   `network.coop_*` values in `config.toml` still apply.
4. Performance on the PS3 as host. A host runs all the AI and the scripts,
   and also sends the game state to the clients. The extra enemies setting
   (per player, 50% by default) adds more load. For reference, the PS3 runs
   a10 alone at 30 fps, but it ran a 44-player game at 10.8 fps. Use
   `network.coop_enemies_mode = "none"` when the PS3 hosts.

One known issue: the description panel of LAN PLAY and INTERNET PLAY knows
only the multiplayer maps, so a campaign level shows as an unknown map. This
affects only what the panel shows.

**Suggested order**

1. Test the PS3 as a co-op client (below).
2. Add co-op hosting to the Xbox menus.
3. Add the invite tab, for private games.

**Test: the PS3 as a co-op client, over LAN**

1. On the PC, use the same commit as the PS3 (network version 29). Go to
   Create Game > LAN > Map > SINGLEPLAYER. Pick a level (a10 is the safest;
   it is the level the PS3 already runs well). Pick a difficulty, then
   start.
2. On the PS3, open LAN PLAY, select the PC's game, and press A.
3. Check these points:
   - The PS3 loads the level.
   - The cutscenes and chapter titles play on both machines.
   - The doors and the AI move the same way on both machines.
   - The BSP switches when the PC player crosses a loading zone.
   - A dead PS3 player watches a teammate and then comes back.
   - The frame rate on the PS3.

## Custom maps

**Status:** possible, but a full goal, not a setting to turn on. Upstream's
Custom Edition code is already compiled into the PS3 build. The PS3 turns it
off in [port_stubs.c:178-198](port/ps3/xbox/port_stubs.c#L178-L198): the
comment there says CE tags are not swapped to the PS3's byte order.

**What it needs**

1. Byte order. CE maps are little-endian PC caches.
   - The loader already reads them in an order-safe way: `read_u32` in
     [cache_file_formats.c:842](port/linux/game/cache_file_formats.c#L842).
   - Upstream converts CE tags to the Xbox layout. The PS3 can then swap the
     converted tags with its existing Xbox tag swap,
     [xbox_tag_swap.c](port/ps3/xbox/xbox_tag_swap.c).
   - The other CE files (about 8,900 lines) need an audit for reads that
     depend on byte order, like the a10 audit.
2. Address window. CE tag data must sit at `0x40440000`-`0x41B40000`
   (23 MB).
   - The PS3 already reserves its Xbox window at `0x80000000` with
     `ps3_host_window_reserve`, which can take any address aligned to
     256 MB.
   - CE needs a second area at `0x40000000`. It is not checked yet that lv2
     gives that address.
3. Textures. PC bitmaps are linear, not swizzled, and they use Halo PC's
   channel orders. The `halo_custom_edition_texels_channels` stub must be
   implemented, and [d3d8_rsx.c](port/ps3/xbox/d3d8_rsx.c) must accept these
   textures. The RSX supports DXT and linear textures natively.
4. Memory. This is the main risk.
   - With stock maps, free main memory already falls to 36 MB at its
     lowest.
   - The PS3's texture cache is the Xbox's 22 MB, and the CE doc says CE
     maps draw more texture than that.
   - Ogg sounds are decoded to ADPCM in memory when the map loads.
   - Textures may have to go in the RSX's 256 MB of video memory.
5. Files. Put a `custom_maps` folder beside `maps` on the HDD. It must hold
   the maps plus CE's `bitmaps.map`, `sounds.map` and `loc.map` from a Halo
   Custom Edition install, which the user supplies.
6. Menus. The Xbox map list shows only the Xbox's maps. To host on a CE map,
   the CE multiplayer maps must be added to it. The PC menus already do this
   (`MAP_KIND_CUSTOM_MULTIPLAYER`).
7. Network (the payoff). Map sharing (`map_share.c`) is already compiled,
   and its 4 messages are byte-swapped. When CE maps load on the PS3, the
   PS3 can download a PC host's map into `custom_maps`. Then it can join
   public CE games from INTERNET PLAY.

Xbox-format mod maps that keep a stock map's name already take the Xbox swap
path, so they will probably load now. This is not tested.

**Suggested order**

1. A read-only audit: byte order in the CE code, the `0x40000000` area, and
   the memory budget.
2. Load one small CE multiplayer map from `custom_maps` on the PS3.
3. Textures and sounds.
4. The Xbox map list, then map sharing.

## Sources

- [halo-ce-vita](https://github.com/BirchWoodGod/halo-ce-vita), [its port/vita/README.md](https://raw.githubusercontent.com/BirchWoodGod/halo-ce-vita/main/port/vita/README.md)
- [Halo3DS](https://github.com/Generalkidd/Halo3DS)
- [XDA: PS4/PS5 port news](https://www.xda-developers.com/original-halo-just-got-an-unofficial-windows-linux-ps5-android-port/), [Razzle on X](https://x.com/razzlelul/status/2105158395611468140)
- [PS3-Rich-Presence-for-Discord](https://github.com/zorua98741/PS3-Rich-Presence-for-Discord), [3DS-RPC](https://github.com/MCMi460/3DS-RPC)
