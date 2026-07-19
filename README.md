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

| Action | Button |
| --- | --- |
| Move | D-pad |
| Use / Fire | A / B |
| Menu / Pause | START / SELECT |
| Map / Automap | L / R or X / Y (depends on menu) |
| Weapon cycle | L / R |

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

### Note on music

The original Chex Quest music is OPL/AdLib (FM synthesis) MIDI. A software
OPL3 emulator was prototyped for this port but **removed**: the Nuked OPL3
emulator is far too heavy for the DS ARM9 CPU (it consumes nearly 100% of the
CPU in real time, causing audio glitches and crashes). Music playback is
therefore disabled; only sound effects play. A proper music implementation
would require an ARM7-side synthesizer, which is out of scope here.

## Use of AI assistance

Parts of this project (build fixes, the NDS sound and panel code, and this
documentation) were developed with the assistance of an AI coding tool. All
code was reviewed and verified against the upstream Chocolate Doom sources and
public libnds / BlocksDS documentation before being committed.

## License

This project is distributed under the GNU GPL, the same license as Chocolate
Doom. See the `COPYING.md` file for details.
