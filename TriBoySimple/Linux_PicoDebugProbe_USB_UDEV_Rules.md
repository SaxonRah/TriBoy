How to write a udev rule for Ubuntu that allows OpenOCD to find both a Raspberry Pi Pico and a debug probe.

1. Create a new udev rules file:
```bash
sudo nano /etc/udev/rules.d/60-openocd.rules
```

2. Add these rules to the file:
```
# Raspberry Pi Pico
SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="0003", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000a", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", MODE="0666", GROUP="plugdev"

# Common debug probes (like CMSIS-DAP, J-Link, ST-Link)
# CMSIS-DAP
SUBSYSTEM=="usb", ATTRS{idVendor}=="03eb", ATTRS{idProduct}=="2111", MODE="0666", GROUP="plugdev"
# J-Link
SUBSYSTEM=="usb", ATTRS{idVendor}=="1366", ATTRS{idProduct}=="0101", MODE="0666", GROUP="plugdev"
# ST-Link/V2
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3748", MODE="0666", GROUP="plugdev"
```

3. Save the file (Ctrl+O, then Enter, then Ctrl+X)

4. Reload the udev rules:
```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

5. Make sure your user is in the "plugdev" group:
```bash
sudo usermod -a -G plugdev $USER
```

6. Log out and log back in for the group changes to take effect.

Note: If you're using a specific debug probe not listed above, you may need to find its vendor and product IDs.
You can do this by running `lsusb` with the device connected, which will show a line like `Bus 001 Device 005: ID 2e8a:0003 Raspberry Pi Pico`.

7. Add your user to dialout Group
```bash
sudo usermod -a -G dialout $USER
```
7. Change the permissions on your serial device for Serial Monitor

```bash
sudo chmod 766 /dev/ttyACM0
```
7: File Owner (Root): read write, and execute
6: Group: read and write
6: Others: read and write

766 is insecure, so use 664 instead.

```bash
sudo chmod 660 /dev/ttyACM0
sudo chown root:dialout /dev/ttyACM0
```