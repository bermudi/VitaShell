# VitaShell

VitaShell is a file manager and system utility for the PlayStation Vita. It replaces the LiveArea with tools for managing files, installing packages, transferring files over FTP or USB, browsing media, and more.

This repository contains an unofficial VitaShell 2.16 build with front-touch support and additional maintenance fixes.

> **Warning:** This is unofficial homebrew. Back up your data before installing or modifying system storage.

## Features

- File browser with copy, move, delete, rename, and archive support
- `.vpk` package installation
- FTP server and USB device mode
- Text editor, hex editor, image viewer, and audio player
- ZIP, 7z, TAR, and other archive formats
- QR code scanning
- LiveArea refresh
- Front-touch navigation, scrolling, double-tap opening, and touch context menus
- Custom themes and multiple languages

## Installation

1. Build or download `VitaShell.vpk`.
2. Copy the VPK to the Vita using FTP, USB, or another supported method.
3. Install it with VitaShell or another homebrew package installer.

The application uses the title ID `VITASHELL`.

## Building

Install the [VitaSDK](https://github.com/vitasdk) and make sure `VITASDK` points to your SDK installation. Then run:

```sh
git clone https://github.com/bermudi/VitaShell.git
cd VitaShell
cmake -S . -B build
cmake --build build
```

The finished package is written to `build/VitaShell.vpk`.

## PS TV USB storage

VitaShell can temporarily mount a USB flash drive as `ux0:` on a PS TV:

1. Format the drive as FAT32 or exFAT.
2. Open VitaShell and press **Triangle** in the `home` section.
3. Select **Mount uma0:** and connect the drive.
4. Press **Triangle** again and select **Mount USB ux0:**.
5. Use **Refresh LiveArea** to update applications on the drive.
6. Select **Umount USB ux0:** before disconnecting or reverting the change.

This mount is temporary and must be repeated after restarting the PS TV. Refreshing LiveArea does not refresh PSP games.

## Themes

Themes are loaded from:

```text
ux0:/VitaShell/theme/theme.txt
```

Set the theme name in that file:

```text
THEME_NAME = "YOUR_THEME_NAME"
```

Theme assets are stored in the theme directory. Missing assets fall back to VitaShell's defaults. Colors can be changed in `colors.txt`; dialog and context-menu images can also be replaced with custom PNG files.

## Languages

Language files are stored in UTF-8 format at:

```text
ux0:/VitaShell/language/<language>.txt
```

Available translations are maintained in [`l10n/`](l10n/), including:

- Bulgarian
- Chinese (simplified and traditional)
- Danish
- Dutch
- English (US)
- Finnish
- French
- German
- Greeklish
- Hungarian
- Italian
- Japanese
- Korean
- Norwegian
- Polish
- Portuguese and Brazilian Portuguese
- Russian
- Spanish
- Swedish
- Turkish

VitaShell selects the translation matching the system language when available.

## Development

- [Changelog](CHANGELOG.md)
- [Original VitaShell](https://github.com/TheOfficialFloW/VitaShell)
- [VitaSDK](https://github.com/vitasdk)

## Credits

VitaShell was created by [TheFloW](https://github.com/TheOfficialFloW). Thanks to Team Molecule, xerpi, wololo, sakya, the VitaSDK community, and everyone who has contributed code, translations, testing, and themes.

This unofficial line includes work by theheroGAC, TheRealYoti, isage, chronoss09, yyoossk, and other community contributors.

## License

See [LICENSE](LICENSE).
