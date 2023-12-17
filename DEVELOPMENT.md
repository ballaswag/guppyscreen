## Development

This repository contains the Guppy Screen source code and all its external dependencies.

Dependencies:
 - [lvgl](https://github.com/lvgl/lvgl)
   An embeded graphics library
 - [libhv](https://github.com/ithewei/libhv)
   A network library
 - [spdlog](https://github.com/gabime/spdlog)
   A logging library
 - [wpa_supplicant](https://w1.fi/wpa_supplicant/)
   Handles wireless connections

## Toolchains
The Guppy Screen uses features (filesystem) from C++17, so a gcc/g++ version (7.2+) with C++17 support is required.

### Environment Variables
`CROSS_COMPILE` - The prefix to the toolchain architecture, e.g. `mips-linux-gnu-`
`SIMULAUTION` - Define it to build with SDL for running on your local machine.
`ZBOLT` - Define it to use the Z-Bolt icon set. By default the build uses the Material Design Icons.
`GUPPYSCREEN_VERSION` - Version string displayed in the System Panel in the UI.

### Build Essentials
Based on Ubuntu/Debian, install build essentials

`sudo apt-get install -y build-essential`

### Mipsel (Ingenic X2000E) - specific to the K1 SoC
Building for the K1/Max

1. Download the toolchain [here](https://github.com/ballaswag/k1-discovery/releases/download/1.0.0/mips-gcc720-glibc229.tar.gz)
2. `tar xf mips-gcc720-glibc229.tar.gz && export PATH=<path-to-mips-toolchain/bin>:$PATH`
3. `git clone --recursive https://github.com/ballaswag/guppyscreen && cd guppyscreen`
4. `unset SIMULATION && export CROSS_COMPILE=mips-linux-gnu-`
5. `(cd lv_drivers/ && git apply ../patches/0001-lv_driver_fb_ioctls.patch)`
6. `(cd spdlog/ && git apply ../patches/0002-spdlog_fmt_initializer_list.patch)`
7. `(cd libhv/ && git apply ../patches/0003-libhv-link-atomic-mipsel.patch)`
7. `make -C wpa_supplicant/wpa_supplicant/ clean && make -j$(nproc) -C wpa_supplicant/wpa_supplicant/ libwpa_client.a`
8. `make -C libhv clean && make -j$(nproc) -C libhv  #### note the libhv sometimes fails with the -j option, rerun if you see an error`
9. `make clean && make -j$(nproc) ### executable is in ./build/bin/guppyscreen`


### x86_64 (Intel/AMD)
Building and running Guppy Screen on your local machine speeds up development. Changes can tested on the local machine before rebuilding for the other architectures.

1. `sudo apt-get install -y build-essential libsdl2-dev`
2. `git clone --recursive https://github.com/ballaswag/guppyscreen && cd guppyscreen`
3. `unset CROSS_COMPILE && export SIMULATION=1`
4. `(cd lv_drivers/ && git apply ../patches/0001-lv_driver_fb_ioctls.patch)`
5. `(cd spdlog/ && git apply ../patches/0002-spdlog_fmt_initializer_list.patch)`
6. `(cd libhv/ && git apply ../patches/0003-libhv-link-atomic-mipsel.patch)`
7. `make -C wpa_supplicant/wpa_supplicant/ clean && make -j$(nproc) -C wpa_supplicant/wpa_supplicant/ libwpa_client.a`
8. `make -C libhv clean && make -j$(nproc) -C libhv  #### note the libhv sometimes fails with the -j option, rerun if you see an error`
9. `make clean && make -j$(nproc) ### executable is in ./build/bin/guppyscreen`


### Simulation
Guppy Screen default configurations (guppyconfig.json) is configured for the K1/Max. In order to run it remotely as a simulator build, a few thing needs to be setup.
The following attributes need to be configured in `guppyconfig.json`

1. `log_path` - Absolute path to `guppyscreen.log`. Directory must exists locally.
2. `thumbnail_path` - Absolute path to a local directory for storing gcode thumbnails.
3. `moonraker_host` - Moonraker IP address
4. `moonraker_port` - Moonraker Port
5. `wpa_supplicant` - Path to the wpa_supplicant socket (usually under /var/run/wpa_supplicant/)

```
{
  "default_printer": "k1",
  "log_path": "<local_path_to_guppyscreen.log>",
  "printers": {
    "k1": {
      "display_sleep_sec": 300,
      "moonraker_api_key": false,
      "moonraker_host": "<remote_ip_to_moonraker>",
      "moonraker_port": <moonraker_port_if_not_7125>
    }
  },
  "thumbnail_path": "<local_path_to_thumbnail_directory_for_storing_gcode_thumbs>",
  "wpa_supplicant": "<path_to_the_wireless_interface_wpa_supplicant_socket-e.g. /var/run/wpa_supplicant/wlo1>
}

```

Note: Guppy Screen currently requires running as `root` because it directly interacts with wpa_supplicant.
