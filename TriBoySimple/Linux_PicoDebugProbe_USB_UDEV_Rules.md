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

7. A. Change the permissions on your serial device for Serial Monitor

```bash
sudo chmod 766 /dev/ttyACM0
```
7: File Owner (Root): read write, and execute
6: Group: read and write
6: Others: read and write

766 gives everyone write access which is not ideal for security. A slightly better approach would be:
```bash
sudo chmod 664 /dev/ttyACM0
```
This gives read/write to owner and group, and just read to others.

You can revert the permissions with:
```bash
sudo chmod 660 /dev/ttyACM0
sudo chown root:dialout /dev/ttyACM0
```

7. B. Add your user to dialout Group

I could not get grou permissions to work via
```bash
sudo usermod -a -G dialout $USER
```
After running this command, you'll need to log out and log back in for the changes to take effect. Alternatively, you can apply the changes to your current session using:
```bash
newgrp dialout
```
You can verify your group membership with:
```bash
groups
```

If adding your user to the dialout group didn't work but using `chmod 766` or `chmod 664` did, there could be a few reasons for this:

1. You might not have logged out and back in after adding yourself to the dialout group. Group membership changes require a new login session to take effect.
2. The device might not be owned by the dialout group but by a different group.
3. There might be an underlying permission issue with how the device is being registered.

First, check what group actually owns the device:
```bash
ls -l /dev/ttyACM0
```

Based on the output, you can add yourself to the correct group:
```bash
sudo usermod -a -G [group_name] $USER
```

You could also create a udev rule that specifically sets the permissions when the device connects:
```bash
sudo nano /etc/udev/rules.d/99-serial.rules
```

And add:
```
SUBSYSTEM=="tty", ATTRS{idVendor}=="[your_vendor_id]", ATTRS{idProduct}=="[your_product_id]", MODE="0666", GROUP="dialout"
```
Replace [your_vendor_id] and [your_product_id] with the actual values from `lsusb`.

My Pico DebugProbe is:
```
SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", MODE="0666", GROUP="dialout"
```
CTRL-O to save rule. Hit Enter to confirm file name. CTRL-X to exit Nano.

After adding the udev rule, reload with:
```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Now OpenOCD, CMSIS-DAP, and Serial Monitor should work.