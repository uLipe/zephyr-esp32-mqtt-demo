# Zephyr-MQTT Demonstration using ESP32.

# Installation:

After installing West metatool in you machine:

```
$ mkdir your_ws
$ cd your_ws
$ git clone <this project url>
$ west init                 # installs Zephyr
$ west update               # update Zephyr modules
$ west espressif install    # pulls espressif toolchain details

# Export or set the following variables into the environment:
$ export ESPRESSIF_TOOLCHAIN_PATH="/home/ulipe/.espressif/tools/zephyr"
$ export ZEPHYR_TOOLCHAIN_VARIANT="espressif"
```

# Building plus flashing:

```
$ cd <your_ws>
$ west build -besp32c3_devkitm /<this_project_path> #for esp32c3
$ west build -besp32 /<this_project_path> #for esp32

# connect the board to your PC/Laptop, then:

$ west flash && west espressif monitor 

```