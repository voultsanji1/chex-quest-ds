# Chex Quest DS

Un porting di **Chex Quest** per **Nintendo DS**, basato sul motore
[Chocolate Doom](https://www.chocolate-doom.org/) adattato a NDS
(chocolate-doom-ds). Avviando `chexquest.nds` il gioco parte da solo:
la IWAD `chex.wad` e la patch dehacked `chex.deh` vengono incapsulate
dentro la ROM tramite NitroFS.

> **Nota legale:** `chex.wad` e `chex.deh` sono file proprietari (Chex Quest
> è stato distribuito gratuitamente dentro scatole di cereali Chex, ma non è
> open source). Lo script di build li scarica automaticamente; scaricali e
> usali solo se sei autorizzato (ad esempio possiedi una copia originale).

## Come ottenere il file `.nds` (GitHub Actions)

Non serve installare nulla: il file viene prodotto automaticamente da
GitHub Actions.

1. Crea un repository su GitHub e carica dentro questo contenuto:
   ```bash
   git init
   git add -A
   git commit -m "Chex Quest DS"
   git branch -M main
   git remote add origin https://github.com/TUO-UTENTE/chex-quest-ds.git
   git push -u origin main
   ```
2. Vai nella scheda **Actions** del tuo repo e lascia completare il job
   `Build Chex Quest DS`.
3. Scarica l'artifact `chexquest-nds-...` (contiene `chexquest.nds`).

Per rilasciare una versione: crea un tag `vX.Y.Z` (`git tag v1.0.0 &&
git push --tags`); GitHub crea automaticamente una release con il `.nds`.

## Come eseguirlo su NDS

- **Slot-1 flashcart** (R4, Acekard, ...) oppure **DSi/3DS** con un
  homebrew menu moderno (es. [NDS Homebrew Menu](https://wiki.ds-homebrew.com/)).
- Copia `chexquest.nds` nella SD e avvialo. Il gioco parte subito:
  legge `chex.wad`/`chex.deh` direttamente dalla ROM.

### Fallback da scheda SD

Se per qualche motivo i file incapsulati non venissero trovati (loader
vecchi che non passano il percorso della ROM), il gioco cerca comunque
una IWAD nella cartella `/doom/` della SD:

```
/doom/chex.wad
/doom/chex.deh        (opzionale, per i testi/comportamenti originali)
```

Inserisci lì i due file e funziona allo stesso modo.

## Build locale (opzionale)

Serve il SDK **BlocksDS** (Docker) e, per i dati di gioco, `curl`/`unzip`:

```bash
# 1) scarica chex.wad e chex.deh dentro nitrofs/ (verifica gli MD5)
bash scripts/fetch_assets.sh

# 2) compila dentro il container BlocksDS
docker run --rm -v "$PWD":/work -w /work skylyrac/blocksds:slim-latest \
  bash -c "make -C arm7 && make -f Makefile.arm9"

# 3) il risultato è chexquest.nds
```

## Comandi durante il gioco

| Azione | Tasto |
| --- | --- |
| Muoviti | Croce direzionale |
| Usa/spara | A / B |
| Menu / pausa | START / SELECT |
| Maps / automappa | L / R o tasto X/Y (dipende dal menu) |
| Rotazione armi | L / R |

## Struttura

- `src/nds/i_main_nds.c` — avvio NDS: monta FAT + NitroFS, sceglie la IWAD
  (embedded `nitro:/chex.wad` o `/doom/chex.wad`) e passa `-iwad`/`-deh`.
- `Makefile.arm9` — compilazione ARM9; incapsula `nitrofs/` nella ROM.
- `icon_chex.png` — icona NDS 32×32 generata da `download.jpg`.
- `scripts/fetch_assets.sh` — scarica e verifica `chex.wad`/`chex.deh`.
- `.github/workflows/main.yml` — build automatico su GitHub Actions.

Basato su https://github.com/gufranco/chocolate-doom-ds (GPL-2.0).
Chex Quest è proprietà di Digital Café / Ralston-Purina.
