# irlingo documentation

irlingo (called PIIR before version 3.0.0) sends infrared signals from a Raspberry Pi, like a remote control does. It reads a description of a remote from a JSON file, builds the code for the buttons or settings you ask for, and transmits it through an IR LED.

| Guide | What it covers |
|-------|----------------|
| [Installation](installation.md) | Hardware, release packages, enabling the IR transmitter, building from source, cross compiling, upgrading |
| [Usage](usage.md) | The `irlingo` command: finding a remote, sending keys and air conditioner settings, output, exit codes |
| [Managing remotes](remotes.md) | Adding remotes to an installation, converting LIRC files, deriving a remote from recorded codes |
| [Remote file format](remote-format.md) | Every setting of a remote JSON file, with a worked example |
| [Integration](integration.md) | Calling irlingo from scripts, cron, Home Assistant and Node-RED |
| [Troubleshooting](troubleshooting.md) | Error messages and what to do about them, checking that the LED sends |
| [Development](development.md) | Tests, coverage, continuous integration and releases |

## Quick start

On a Raspberry Pi with an IR LED on GPIO 17:

```
# 1. Install the package for your Pi (see Installation for which one)
sudo apt install ./irlingo_3.0.0_aarch64.deb

# 2. Enable the kernel IR transmitter and reboot
echo 'dtoverlay=gpio-ir-tx,gpio_pin=17' | sudo tee -a /boot/firmware/config.txt
sudo reboot

# 3. Find your remote and its buttons
sudo irlingo-remotes add sony
irlingo --list | grep -i sony
irlingo --remote sony/RM-ED011 --help

# 4. Send
sudo irlingo --remote sony/RM-ED011 --feature key=KEY_POWER
```

An air conditioner takes all its settings in one command:

```
sudo irlingo --remote hvac_panasonic --feature mode=HEAT --feature temperature=21 --feature fan=AUTO
```
