<div align="center">

<img src="icons/yamp-256.png" width="120" height="120" alt="YAMP" />

# YAMP

**Yet Another Music Player**

A performance-oriented, ultra-lightweight music player built with C++, Qt 6 and QML.

[![License](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)
[![AUR version](https://img.shields.io/aur/version/yamp-git?label=AUR)](https://aur.archlinux.org/packages/yamp-git)
[![Made with Qt](https://img.shields.io/badge/Qt-6.8+-41CD52?logo=qt&logoColor=white)](https://www.qt.io/)

</div>

---

> **⚠️ Project status — experimental early access**
>
> Functional for daily use, but may contain bugs. Active development is ongoing and features are subject to change.

## Screenshots

<p align="center">
  <img src="preview1.png" width="49%" />
  <img src="Preview2.png" width="49%" />
</p>
<p align="center">
  <img src="preview3.png" width="49%" />
  <img src="preview4.png" width="49%" />
</p>
<p align="center">
  <img src="preview5.png" width="98%" />
</p>

## Features

- **Fast media scanning** — SQLite-backed library handles thousands of tracks instantly.
- **MPRIS support** — full integration with system media controllers and lock screens.
- **Modern stack** — built on Qt 6.8+, QtMultimedia and TagLib.

## Installation

### Arch Linux — AUR

YAMP is in the AUR. Install via any helper:

```bash
yay -S yamp-git
```

### From source — `makepkg`

```bash
sudo pacman -S --needed base-devel cmake git ninja qt6-base qt6-declarative qt6-multimedia taglib
git clone https://github.com/Wu28ri/yamp.git
cd yamp
makepkg -si
```

### Manual build — CMake

```bash
git clone https://github.com/Wu28ri/yamp.git
cd yamp
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/appyamp
```

## License

YAMP is released under the GPLv3 license. See [LICENSE](LICENSE) for details.
