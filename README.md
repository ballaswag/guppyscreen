# Guppy Screen for Klipper

Guppy Screen is a touch UI for Klipper using APIs exposed by Moonraker. It builds on LVGL as a standalone executable, has no dependency on any display servers such as X/Wayland.
<p align="center">
    <a aria-label="Downloads" href="https://github.com/ballaswag/guppyscreen/releases">
      <img src="https://img.shields.io/github/downloads/ballaswag/guppyscreen/total?style=flat-square">
  </a>
    <a aria-label="Stars" href="https://github.com/ballaswag/guppyscreen/stargazers">
      <img src="https://img.shields.io/github/stars/ballaswag/guppyscreen?style=flat-square">
  </a>
    <a aria-label="Forks" href="https://github.com/ballaswag/guppyscreen/network/members">
      <img src="https://img.shields.io/github/forks/ballaswag/guppyscreen?style=flat-square">
  </a>
    <a aria-label="License" href="https://github.com/ballaswag/guppyscreen/blob/develop/LICENSE">
      <img src="https://img.shields.io/github/license/ballaswag/guppyscreen?style=flat-square">
  </a>
    <a aria-label="Last commit" href="https://github.com/ballswag/guppyscreen/commits/">
      <img src="https://img.shields.io/github/last-commit/ballaswag/guppyscreen?style=flat-square">
  </a>
</p>

## Installation / Update
Run the following interactive script via SSH on your K1/Max/CR-10 SE/Ender 3 V3 KE/Nebula Pad to install Guppy Screen.

#### Material Design Theme
```
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/ballaswag/guppyscreen/main/installer.sh)"
```

#### Z-Bolt Theme (Only for the K1/Max)
```
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/ballaswag/guppyscreen/main/installer.sh)" -s zbolt
```

### Install on a PI (Debian/Raspbian)
Tested on a BTT Pad 7. Please install with care and make sure you're okay with resetting your setup if things break.
```
wget -O - https://raw.githubusercontent.com/ballaswag/guppyscreen/main/installer-deb.sh | bash
```

### Nightly Builds
#### Material (Nightly)
```
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/ballaswag/guppyscreen/main/installer.sh)" -s nightly
```

#### Z-Bolt (Nightly)
```
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/ballaswag/guppyscreen/main/installer.sh)" -s zbolt nightly
```

#### Raspbian Variant (Nightly)
```
wget -O - https://raw.githubusercontent.com/ballaswag/guppyscreen/main/installer-deb.sh | bash -s nightly
```

### Android
Guppy Screen works on Android! Download and try with the latest [APK](https://github.com/ballaswag/guppyscreen/releases/latest/download/app-release.apk).  
  
<img src="https://github.com/ballaswag/guppyscreen/assets/145094472/d0437cd6-9b82-470f-8889-c4a5b74bfa6e" alt="guppyscreen on android" width="600" />

## Uninstall
ssh into your K1/Max and run the follwow command:
```
/usr/data/guppyscreen/reinstall-creality.sh
```

## Features
:white_check_mark: Console/Macro Shell  
:white_check_mark: Bedmesh  
:white_check_mark: Input Shaper (PSD graphs)  
:white_check_mark: Belt Calibration/Excitate  
:white_check_mark: Print Status  
:white_check_mark: Spoolman Integration  
:white_check_mark: Extrude/Retract  
:white_check_mark: Temperature Control  
:white_check_mark: Fans/LED/Move Control  
:white_check_mark: Fine Tune (speed, flow, z-offset, Pressure Advance)  
:white_check_mark: Limits (Velocity, Acel, Square Corner Velocity, etc.)  
:white_check_mark: File Browser  
:white_check_mark: Supports multiple screen resolutions  
:white_check_mark: Cross platform releases (MIPS/ARM/x86)  
:white_check_mark: TMC Metrics  
:white_check_mark: Multi-Printer support  

## Roadmap
:bangbang: Exclude Object  
:bangbang: Firmware Retraction  

Open for feature requests.

## Documentation
You can find various Guppy Screen documents [here](https://ballaswag.github.io/docs/guppyscreen/configuration/).

## Screenshot
### Material Theme
![Material Theme Guppy Screen](https://github.com/ballaswag/guppyscreen/blob/main/screenshots/material/material_screenshot.png)

Earlier development screenshots can be found [here](https://github.com/ballaswag/guppyscreen/blob/main/screenshots)

## Video Demo
https://www.reddit.com/r/crealityk1/comments/17jp59g/new_touch_ui_for_the_k1/

## Support Guppy Screen
You can directly support this project by <a href='https://ko-fi.com/ballaswag' target='_blank'><img height='36' style='border:0px;height:36px;' src='https://storage.ko-fi.com/cdn/kofi3.png?v=3' border='0' alt='Buy Me a Coffee at ko-fi.com' /></a>
or
[![](https://img.shields.io/static/v1?label=Sponsor&message=%E2%9D%A4&logo=GitHub&color=%23fe8e86)](https://github.com/sponsors/ballaswag)

## Credits
[Material Design Icons](https://pictogrammers.com/library/mdi/)  
[Z-Bolt Icons](https://github.com/Z-Bolt/OctoScreen)  
[Moonraker](https://github.com/Arksine/moonraker)  
[KlipperScreen](https://github.com/KlipperScreen/KlipperScreen)  
[Fluidd](https://github.com/fluidd-core/fluidd)  
[Klippain-shaketune](https://github.com/Frix-x/klippain-shaketune)  
