# Keeping Arduino build environments separate

The T-LoRa Pager and the Cardputer ADV need **different, conflicting** library
sets, and the Arduino IDE installs libraries into one global folder by default.
If you build both from the same global folder you will eventually hit version
collisions — most painfully on **RadioLib** (LilyGoLib pins a specific bundled
version; CardSat may want another) and on the **ESP32 core version** (LilyGoLib
requires arduino-esp32 ≥ 3.3.0-alpha1; other projects may not tolerate it).

The clean fix is to give each board its own **sketchbook** — a self-contained
folder holding that board's `libraries/`, and optionally its own `hardware/` and
preferences. You switch between them in one click (IDE) or one flag (CLI).

There are two good approaches. Pick one:

- **A — Per-board sketchbook via the IDE** (simplest, fully GUI).
- **B — Per-board profiles via `arduino-cli` + `sketch.yaml`** (reproducible,
  scriptable, best if you also build from a terminal or CI).

Both work identically on macOS, Windows, and Linux; only the paths differ.

---

## Why separation matters here

| | T-LoRa Pager (this app) | Cardputer ADV (CardSat) |
|---|---|---|
| Core | esp32 **≥ 3.3.0-alpha1** | esp32 (stable, e.g. 3.x release) |
| UI stack | LilyGoLib + LVGL + LilyGoLib-ThirdParty | M5GFX / M5Unified (or CardSat's own) |
| Radio lib | RadioLib **7.7.1** (bundled by LilyGo) | per CardSat's build |
| PMU/sensors | XPowersLib, SensorLib (bundled) | not used |

Installing both LilyGoLib-ThirdParty and the M5 stack side-by-side in one global
`libraries/` is the scenario that produces "two copies of RadioLib," "wrong LVGL
config," and the `invalid library: no header files found` noise you already saw.

---

## Approach A — Per-board sketchbooks in the Arduino IDE

The **sketchbook location** sets where the IDE reads `libraries/`. Make one per
board and switch via *Preferences → Sketchbook location*.

### 1. Create two sketchbook folders

**macOS**
```
~/Documents/Arduino-pager
~/Documents/Arduino-cardputer
```

**Windows**
```
%USERPROFILE%\Documents\Arduino-pager
%USERPROFILE%\Documents\Arduino-cardputer
```

**Linux**
```
~/Arduino-pager
~/Arduino-cardputer
```

Each will get its own `libraries/` subfolder automatically the first time you
install a library while that sketchbook is active.

### 2. Switch sketchbook, then install that board's libraries

1. **Arduino IDE → Settings/Preferences → Sketchbook location** → point it at
   `Arduino-pager`. Click OK. (IDE 2.x applies immediately; if in doubt, restart it.)
2. Install **only the Pager's** libraries into it: add LilyGoLib as a ZIP, then
   copy the folders from **LilyGoLib-ThirdParty** into
   `Arduino-pager/libraries/`.
3. To work on CardSat, switch **Sketchbook location** to `Arduino-cardputer` and
   install **only** the Cardputer/CardSat libraries there.

Because each sketchbook has an independent `libraries/`, the two never see each
other's headers.

### 3. (Recommended) also separate the ESP32 core version

Sketchbook separation isolates *libraries* but **not** the installed board cores
— those live in a shared per-user folder and the IDE shows all installed versions
in one *Board* dropdown. That's usually fine: install both the alpha core (for the
Pager) and your stable core, and just **select the right core version per board**
in *Tools → Board → Boards Manager* / the board dropdown before compiling.

If you want them *fully* walled off (so an IDE update can't shuffle cores under
you), use Approach B, which pins the exact core version per sketch.

---

## Approach B — `arduino-cli` profiles (reproducible, recommended for GitHub)

`arduino-cli` supports a **`sketch.yaml`** with named **profiles**. Each profile
pins its own core version *and* its own library versions, and builds into an
isolated cache. This is the most robust option and travels with the repo, so a
fresh clone on any OS reproduces the exact toolchain.

### 1. Install arduino-cli

- **macOS:** `brew install arduino-cli`
- **Windows:** `winget install ArduinoSA.CLI` (or download the zip, add to PATH)
- **Linux:** `curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh`

Add the ESP32 board index once:
```
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json
arduino-cli core update-index
```

### 2. Put a `sketch.yaml` next to `CardSatPager.ino`

A profile records the exact core + libraries for this board. Example
(`sketch.yaml`, Pager profile shown; pin versions to whatever you verified):

```yaml
profiles:
  pager:
    fqbn: esp32:esp32:tlora_pager:Revision=Radio_SX1262,CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB
    platforms:
      - platform: esp32:esp32 (3.3.0-alpha1)
        platform_index_url: https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json
    libraries:
      - LilyGoLib            # plus the ThirdParty folders, see note below

default_profile: pager
```

Build with the profile (isolated — uses only what the profile declares):
```
arduino-cli compile --profile pager
arduino-cli upload  --profile pager -p /dev/cu.usbmodemXXXX   # macOS
arduino-cli upload  --profile pager -p COM5                   # Windows
arduino-cli upload  --profile pager -p /dev/ttyACM0          # Linux
```

The CardSat sketch gets its **own** `sketch.yaml` in **its** folder with a
`cardputer:` profile pinning the M5 stack and its core. The two sketches never
share resolution.

> Note on LilyGoLib-ThirdParty: those bundled libraries aren't all in the
> arduino-cli library index, so `libraries:` in the profile can't always pull
> them by name. Two options: (a) keep using a dedicated sketchbook
> (`--config-file` / `directories.user`) that contains the ThirdParty folders,
> or (b) vendor them under a `libraries/` folder referenced by
> `--libraries ./libraries`. Either way the profile still pins the **core**
> version, which is the collision that hurts most.

### 3. Per-OS isolation of the arduino-cli data dir (optional, strongest)

If you also want each board's *cores and caches* on separate disks/paths, give
each project its own arduino-cli data directory via a project-local config:

`arduino-cli.yaml` (in the Pager project):
```yaml
directories:
  data: ./.arduino15-pager      # cores, tool downloads for THIS board only
  user: ./.arduino-pager        # libraries/ for THIS board only
```
Then always build that project with `--config-file ./arduino-cli.yaml`. The
Cardputer project gets its own pointing at different folders. Add these data
folders to `.gitignore` (they're large and machine-specific) — the repo already
does (see below).

---

## Per-OS path cheat-sheet

| | macOS | Windows | Linux |
|---|---|---|---|
| Default sketchbook | `~/Documents/Arduino` | `%USERPROFILE%\Documents\Arduino` | `~/Arduino` |
| arduino-cli data | `~/Library/Arduino15` | `%LOCALAPPDATA%\Arduino15` | `~/.arduino15` |
| Serial port looks like | `/dev/cu.usbmodem*` | `COM3`, `COM5`, … | `/dev/ttyACM0`, `/dev/ttyUSB0` |
| Serial permission note | works out of the box | install CP210x/CH9102 driver if port absent | add user to `dialout`: `sudo usermod -aG dialout $USER` then re-login |

---

## Recommended setup for this repo

- Use **Approach A** day-to-day (two sketchbooks: `Arduino-pager`,
  `Arduino-cardputer`) — it's the least friction and matches LilyGo's
  ZIP+ThirdParty install flow.
- Commit a **`sketch.yaml`** (Approach B) too, so anyone cloning can reproduce at
  least the **core + FQBN** exactly on macOS/Windows/Linux without guessing.
- Never install LilyGoLib-ThirdParty and the M5/CardSat libraries into the **same**
  `libraries/` folder. That single rule prevents the great majority of
  cross-board build failures.
