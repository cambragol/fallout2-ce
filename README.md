# Fallout 2 Community Engine (Fallout2-CE)

**Fallout2-CE** is a cross-platform reimplementation of Fallout 2, preserving the original gameplay while fixing engine bugs and adding quality-of-life improvements. It runs smoothly on Windows, Linux, macOS, Android, iOS, and even in browsers.

This is an active fork of the original Community Edition, focused on features for Fallout: Sonora.

## ✅ Features

- Original gameplay, improved engine
- Runs natively on multiple platforms
- Support for large game areas and widescreens
- Added features (improved barter, extended pathfinding, worldmap trails, etc)
- Fully supports some popular mods (see below)

## ⚠️ Mod Compatibility

Fully support:
- Fallout: Nevada (original version)
- Fallout: Sonora (original version)

**Not supported yet**:
- [Restoration Project](https://github.com/BGforgeNet/Fallout2_Restoration_Project)
- [Fallout: Et Tu](https://github.com/rotators/Fo1in2)
- [Olympus 2207](https://olympus2207.com)
- Resurrection, Yesterday — untested

For Fallout 1, see [Fallout1-CE](https://github.com/alexbatalov/fallout1-ce) (unrelated project).

---

## 💾 Installation

You must own Fallout 2. Purchase it from [GOG](https://www.gog.com/game/fallout_2), [Steam](https://store.steampowered.com/app/38410), or [Epic Games](https://store.epicgames.com/p/fallout-2). Then:

- Download the latest [release](https://github.com/fallout2-ce/fallout2-ce/releases)
- Or [build from source](https://github.com/fallout2-ce/fallout2-ce#building)

### ▶️ Quick Start by Platform

#### 🪟 Windows

1. Copy `fallout2-ce.exe` into your `Fallout2` folder.
2. Run it instead of `fallout2.exe`.

#### 🐧 Linux

1. Use a Windows install as a base (or extract from GoG):

```bash
sudo apt install innoextract libsdl2-2.0-0
innoextract ~/Downloads/setup_fallout2_2.1.0.18.exe -I app
mv app ~/Fallout2
```

2. Copy `fallout2-ce` to that folder and run `./fallout2-ce`.

#### 🍎 macOS

> Requires macOS 10.11+ (Intel or Apple Silicon)

1. Use a Windows or MacPlay install as a base.
2. Copy `fallout2-ce.app` into the same folder.
3. Run `fallout2-ce.app`.

#### 🤖 Android

> Touch interface simulates a trackpad:
- 1 finger = move cursor
- 1-tap = left click
- 2-tap = right click
- 2-finger drag = scroll

1. Copy required files to your device: `master.dat`, `critter.dat`, `patch000.dat`, and `data/`.
2. Install `fallout2-ce.apk` and launch it.
3. Select game data folder when prompted.

#### 🍏 iOS

> Same controls as Android. Requires sideloading.

1. Sideload `fallout2-ce.ipa` using [AltStore](https://altstore.io/) or [Sideloadly](https://sideloadly.io/).
2. Launch once to enable File Sharing.
3. Use Finder or iTunes to copy data files to the app (see [Apple guide](https://support.apple.com/HT210598)).

## ⚙️ Configuration

Edit `fallout2.cfg` to ensure correct file paths. Depending on source, filenames may need to be lowercase or uppercase:

- `master_dat`, `critter_dat`, `master_patches`, `critter_patches`
- `music_path1` (e.g. `data/sound/music/` or `SOUND/MUSIC/`)

### Graphics Settings (`[graphics]` in `fallout2.cfg`)

```ini
fullscreen=0          ; 0 = windowed, 1 = fullscreen
game_width=640
game_height=480
high_quality=0        
highres_stencil=1     
preserve_aspect=0
splash_size=0
stretch_enabled=0
widescreen=0
```

- **Desktops**: Any size you want.
- **Tablets**: Use logical resolution (e.g. iPad Pro = 834x1194)
- **Phones**: Use 480 for height, calculate width by aspect ratio.

### Advanced Settings (`ddraw.ini`)

This config (part of Sfall) provides fine-tuning and modding options. Example:

```ini
[Misc]
IFACE_BAR_MODE=0
IFACE_BAR_SIDE_ART=2
IFACE_BAR_SIDES_ORI=0
;IFACE_BAR_WIDTH=800
```

You’ll need `f2_res.dat` for these options:  
➡ [Download f2_res.dat](https://github.com/fallout2-ce/fallout2-ce/raw/refs/heads/mainmenu/files/f2_res.dat)

Sample config: [ddraw.ini](https://raw.githubusercontent.com/fallout2-ce/fallout2-ce/refs/heads/main/files/ddraw.ini)

---

## 🚀 Quality of Life Improvements

- 5x pathfinding nodes for smoother movement
- `Ctrl+Click` to quickly transfer items
- Press `A` to “Take All” or set quantity
- Auto-calculate caps during bartering
- Music continues across maps (with config)
- Auto-open doors (with config)

---

## 🛠️ Contributing

We're good.

---

## 📜 License

Code is released under the [Sustainable Use License](LICENSE.md).
