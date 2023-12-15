# Guppy Screen for Klipper

Guppy Screen is a touch UI for Klipper using APIs exposed by Moonraker. It builds on LVGL as a standalone executable, has no dependency on any display servers such as X/Wayland.

## Installation / Update
Run the following interactive script via SSH on your K1/Max to install Guppy Screen.

#### Material Design Theme
```
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/ballaswag/guppyscreen/main/installer.sh)"
```

#### Z-Bolt Theme
```
sh -c "$(wget --no-check-certificate -qO - https://raw.githubusercontent.com/ballaswag/guppyscreen/main/installer.sh)" -s zbolt
```

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
:white_check_mark: Fine Tune (speed, flow, z-offset)  
:white_check_mark: File Browser 

## Roadmap
:bangbang: Exclude Object  
:bangbang: Cross platform releases (ARM)  
:bangbang: Multi-Printer support  
:bangbang: Vibration graphs  
:bangbang: Firmware Retraction  
:bangbang: Other Fine Tune params (Pressure Advance, Smooth Time)  
:bangbang: Limits (Velocity, Acel, Square Corner Velocity, etc.)  

Open for feature requests.

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
