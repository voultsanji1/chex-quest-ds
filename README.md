# Chex Quest DS

A port of **Chex Quest** for the **Nintendo DS**, built on the
[Chocolate Doom](https://www.chocolate-doom.org/) engine adapted to the
Nintendo DS (chocolate-doom-ds). Just launch `chexquest.nds` and the game
starts on its own: the `chex.wad` IWAD and the `chex.deh` dehacked patch are
bundled inside the ROM via NitroFS.

> **Legal note:** `chex.wad` and `chex.deh` are proprietary files (Chex Quest
> was given away for free inside boxes of Chex cereal, but it is not
> open source). The build script downloads them automatically; download and
> use them only if you are authorized to do so (for example, if you own an
> original copy).

## Getting the `.nds` file (GitHub Actions)

You do not need to install anything: the file is produced automatically by
GitHub Actions.

1. Create a GitHub repository and push this content into it:
   ```bash
   git init
   git add -A
   git commit -m "Chex Quest DS"
   git branch -M main
   git remote add origin https://github.com/YOUR-USER/chex-quest-ds.git
   git push -u origin main
   ```
2. Open the **Actions** tab of your repo and let the `Build Chex Quest DS`
   job finish.
3. Download the `chexquest-nds-...` artifact (it contains `chexquest.nds`).

To publish a release: create a `vX.Y.Z` tag (`git tag v1.0.0 && git push --tags`);
GitHub automatically creates a release containing the `.nds`.

## Running it on a Nintendo DS

- **Slot-1 flashcart** (R4, Acekard, ...) or a **DSi/3DS** with a modern
  homebrew menu (e.g. [NDS Homebrew Menu](https://wiki.ds-homebrew.com/)).
- Copy `chexquest.nds` to the SD card and launch it. The game starts
  immediately: it reads `chex.wad`/`chex.deh` straight from the ROM.

### SD card fallback

If for some reason the bundled files are not found (older loaders that do not
pass the ROM path), the game still looks for an IWAD in the `/doom/` folder
on the SD card:

```
/doom/chex.wad
/doom/chex.deh        (optional, for original texts/behaviors)
```

Drop the two files there and it works the same way.

## Local build (optional)

You need the **BlocksDS** SDK (Docker) and, for the game data, `curl`/`unzip`:

```bash
# 1) download chex.wad and chex.deh into nitrofs/ (verifies MD5)
bash scripts/fetch_assets.sh

# 2) build inside the BlocksDS container
docker run --rm -v "$PWD":/work -w /work skylyrac/blocksds:slim-latest \
  bash -c "make -C arm7 && make -f Makefile.arm9"

# 3) the result is chexquest.nds
```

## Controls

Button names follow the standard Nintendo DS layout (D-pad ↑ ↓ ← →,
A, B, X, Y, L, R, START, SELECT — see
[StrategyWiki](https://strategywiki.org/wiki/Category:Nintendo_DS_controller_buttons)).

| Action | Button |
| --- | --- |
| Move / Turn | D-pad ↑ ↓ ← → |
| Fire | **A** |
| Use / Open (doors, switches) | **B** |
| Run (hold) | **X** |
| Confirm menu / Save / Y–N dialogs | **Y** |
| Previous / Next owned weapon | **L** / **R** |
| Menu / Pause | **START** |
| Automap toggle | **SELECT** |

> **Note:** `A` sends only the fire key. It does **not** send `ENTER`, so
> firing never re-shows the last on-screen message and never phantom-confirms
> a menu. Use **Y** to confirm menus, save games, and yes/no prompts.

## Project structure

- `src/nds/i_main_nds.c` — NDS startup: mounts FAT + NitroFS, picks the IWAD
  (embedded `nitro:/chex.wad` or `/doom/chex.wad`) and passes `-iwad`/`-deh`.
- `Makefile.arm9` — ARM9 build; bundles `nitrofs/` into the ROM.
- `icon_chex.png` — 32×32 NDS icon generated from `download.jpg`.
- `scripts/fetch_assets.sh` — downloads and verifies `chex.wad`/`chex.deh`.
- `.github/workflows/main.yml` — automated build on GitHub Actions.

## Credits and attribution

This project is derived from other free software, and would not exist without
them:

- **Chocolate Doom** ([chocolate-doom/chocolate-doom](https://github.com/chocolate-doom/chocolate-doom),
  GPL-2.0). The original engine this port is based on. Chocolate Doom itself
  is built on the **Linux Doom 1.10** source release by **id Software**
  (1997, GPL), the canonical open-source release of the Doom engine.
- **chocolate-doom-ds** ([gufranco/chocolate-doom-ds](https://github.com/gufranco/chocolate-doom-ds),
  GPL-2.0). The NDS adaptation this repository is directly based on.

**Chex Quest** is the property of **Digital Café / Ralston-Purina**. The
`chex.wad` and `chex.deh` game files are proprietary and are not included in
this repository; they are downloaded at build time by `scripts/fetch_assets.sh`.

## Code written for this project

The NDS-specific platform layer was written for this port (authored by hand,
in the style of the surrounding Chocolate Doom code):

- `src/nds/i_main_nds.c` — NDS boot, FAT/NitroFS mounting, IWAD selection.
- `src/nds/i_input_nds.c` — NDS button and touch-screen input mapping.
- `src/nds/i_sound_nds.c` — sound effects playback via libnds
  `soundPlaySample()` (signed 8-bit PCM), with SFX caching in the zone heap.
- `src/nds/i_video_nds.c` — framebuffer render into NDS VRAM.
- `src/nds/i_system_nds.c`, `src/nds/i_timer_nds.c`, `src/nds/i_stubs_nds.c`
  — system, timing, and platform stubs.
- `src/nds/nds_panel.c` / `nds_panel.h` — bottom-screen panel: boot screen,
  WAD selector, and gameplay HUD (FPS, frame count, zone memory, sound cache,
  automap indicator).

## Known issues and limitations

This section is kept up to date with the actual state of the code.

### Fixed bugs (history)

- **SELECT froze all input.** A custom parallel automap toggle blocked every
  subsequent key event. Fixed by mapping `SELECT → KEY_TAB` (Doom's native
  automap) and mirroring the real `automapactive` state to the HUD, instead of
  maintaining a second toggle.
- **"GAME SAVED" / status message repeated on every fire.** `A` used to send
  both the fire key and `KEY_ENTER`. In Doom, `KEY_ENTER` is the
  *message-refresh* key, so every shot re-printed the last on-screen message
  ("Picked up…", "GAME SAVED", …) and could phantom-confirm a menu. Fixed by
  making `A` fire-only and moving menu/save confirmation to **Y**.
- **Save-game loop.** The NDS auto-save path left `saveStringEnter` set, so a
  later `ENTER` re-triggered a save. Fixed by clearing `saveStringEnter` in
  `M_SaveSelect` and in `M_ClearMenus`.

### Current limitations

- **No music.** OPL/AdLib music is disabled (see above). Only sound effects
  play. This is a permanent limitation of this build unless an ARM7-side
  synth is added.
- **No keyboard / text entry.** The NDS has no keyboard, so save-game slots
  are auto-named (map name) instead of typed by the player.
- **No multiplayer / network play.** The DS wireless stack is not used; the
  netcode is compiled out.
- **No touch-screen mouse-look aiming beyond what the original engine
  supports.** The touch screen drives the bottom-screen panel/HUD only.
- **No save "overwrite confirmation" typing.** Saving to a used slot
  overwrites it immediately (auto-named), matching the no-keyboard constraint.

### Things that will NOT be added

- **OPL3 music emulation on ARM9** — too slow, causes crashes (already tried
  and removed).
- **In-game typed text / chat** — no NDS keyboard; not planned.
- **Widescreen / high-resolution rendering** — the DS hardware is fixed at
  256×192 per screen.
- **DSi/3DS enhancement features** (camera, motion, 3D slider) — out of scope
  for a vanilla-accurate Chocolate Doom port.

## Use of AI assistance

Parts of this project (build fixes, the NDS sound and panel code, and this
documentation) were developed with the assistance of an AI coding tool. All
code was reviewed and verified against the upstream Chocolate Doom sources and
public libnds / BlocksDS documentation before being committed.

## License

This project is distributed under the GNU GPL, the same license as Chocolate
Doom. See the `COPYING.md` file for details.
