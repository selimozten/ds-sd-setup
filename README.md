# DS SD Setup

A GUI tool that sets up your [TwilightMenu++](https://github.com/DS-Homebrew/TWiLightMenu) SD card in one click. Select your console profile, hit start, and it downloads and extracts the latest releases directly to your SD card.

## Download

Grab the latest release for your platform from the [Releases](../../releases) page:

- **macOS** — `ds-sd-setup-macos-arm64`
- **Linux** — `ds-sd-setup-linux-x86_64`
- **Windows** — `ds-sd-setup-windows-x86_64.exe`

No installation needed — just download and run.

> On macOS you may need to right-click and select "Open" the first time, since the app is not signed.

## Usage

1. Insert your SD card
2. Launch DS SD Setup
3. Your SD card should be auto-detected — if not, click **Browse** or **Detect SD**, or drag & drop the folder
4. *(Optional)* Click **Format SD** to wipe and format the card as FAT32 before setup
5. Select your console profile (**DSi**, **Flashcard**, or **3DS**)
6. Choose which components to install (TwilightMenu++, nds-bootstrap)
7. Click **Start Setup** (or press **Enter**)
8. Wait for it to finish, then safely eject your SD card

Your settings (SD path, profile, preferences) are saved automatically between sessions.

## Console Profiles

| Profile | For |
|---------|-----|
| DSi / DSi XL | DSi consoles with Unlaunch installed |
| Flashcard | DS Lite / DS with a flashcard (R4, DSTT, Acekard, etc.) |
| 3DS / 2DS | 3DS family consoles with custom firmware (Luma3DS) |

## What Gets Installed

DS SD Setup downloads the latest releases from [DS-Homebrew](https://github.com/DS-Homebrew) and extracts them to your SD card:

- **TwilightMenu++** — menu replacement and unified launcher for NDS, GBA, SNES, NES, Game Boy, Sega, and more. The correct build is selected based on your console profile.
- **nds-bootstrap** — allows NDS ROMs to run natively from the SD card without a flashcard (DSi/3DS).

Existing `_nds` folders can optionally be backed up with a timestamped rename before setup.

## SD Card Formatting

Click **Format SD** to format the card as FAT32 with MBR partition scheme (label: `DSSD`) before setting up. A confirmation dialog prevents accidental data loss. Safety checks refuse to format internal or system disks.

- **macOS** — uses `diskutil eraseDisk` (verifies the disk is external)
- **Linux** — uses `mkfs.fat` with `udisksctl` for remounting
- **Windows** — uses the built-in `format` command

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Enter` | Start setup |
| `Escape` | Stop setup |
| `Cmd/Ctrl + V` | Paste into text field |

## Building from Source

<details>
<summary>Click to expand build instructions</summary>

### Prerequisites

- C11 compiler (GCC, Clang, or MinGW)
- [raylib](https://www.raylib.com/) 5.0+
- [libcurl](https://curl.se/libcurl/)
- [libarchive](https://libarchive.org/) (for .7z extraction)

### CMake (recommended, all platforms)

```bash
cmake -B build
cmake --build build
```

CMake will automatically download and build raylib if not found on your system. Only libcurl and libarchive need to be installed.

### macOS

```bash
brew install raylib curl libarchive
make
```

### Linux

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install -y libcurl4-openssl-dev libarchive-dev libx11-dev \
    libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev

# Use CMake (it will fetch raylib automatically)
cmake -B build && cmake --build build
```

### Windows (MSYS2/MinGW)

```bash
pacman -S mingw-w64-x86_64-{gcc,cmake,ninja,curl,libarchive}
cmake -B build -G Ninja
cmake --build build
```

</details>

## License

MIT License - see [LICENSE](LICENSE) for details.
