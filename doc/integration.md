# Integration

irlingo is a command: anything that can run a command can send IR. This page shows common ways to do that.

- [Running without sudo](#running-without-sudo)
- [Shell scripts](#shell-scripts)
- [Scheduled with cron or systemd timers](#scheduled-with-cron-or-systemd-timers)
- [Home Assistant](#home-assistant)
- [Node-RED](#node-red)
- [From another computer](#from-another-computer)
- [Good practice](#good-practice)

## Running without sudo

Home automation software usually runs as its own user, without `sudo`. With the `lirc` backend that user only needs write access to the transmitter device: add it to the group owning `/dev/lirc0` (see `ls -l /dev/lirc0`, often `video`):

```
sudo usermod -aG video homeassistant      # the user your software runs as
```

and restart the software so it gets the new group. The `gpiod` backend needs access to `/dev/gpiochip*` (group `gpio` on Raspberry Pi OS); `pigpio` needs root.

For remotes with a toggle bit, set `IRLINGO_STATE_DIR` to a directory the user can write, so the toggle state survives and is shared, see [Usage](usage.md#remotes-with-a-toggle-bit).

## Shell scripts

```sh
#!/bin/sh
# tv-off.sh: switch off the TV and the receiver
irlingo -r sony/RM-ED011 -f key=KEY_POWER || exit 1
sleep 0.5
irlingo -r yamaha/RAV300 -f key=KEY_POWER
```

irlingo exits with status 0 when it has sent the code and 1 otherwise, see [Usage](usage.md#exit-status). It does not know whether the device received the code: IR has no reply.

To set an air conditioner, give all settings each time, since every code carries the complete state:

```sh
#!/bin/sh
# heat.sh TEMPERATURE
irlingo -r hvac_panasonic -f mode=HEAT -f temperature="$1" -f option=AUTO -f fan=AUTO \
     -f updown=AUTO -f leftright=AUTO
```

## Scheduled with cron or systemd timers

Heat the summer house at 06:30 on weekdays, with `crontab -e`:

```
30 6 * * 1-5  /usr/bin/irlingo -r hvac_panasonic -f mode=HEAT -f temperature=21 >> /var/log/irlingo.log 2>&1
```

Use the full path (`/usr/bin/irlingo` for the packages, `/usr/local/bin/irlingo` when built from source), since cron has a short `PATH`. Run it as a user allowed to use the transmitter, or put it in root's crontab (`sudo crontab -e`).

With systemd, a service and a timer:

```ini
# /etc/systemd/system/morning-heat.service
[Unit]
Description=Heat the house in the morning

[Service]
Type=oneshot
ExecStart=/usr/bin/irlingo -r hvac_panasonic -f mode=HEAT -f temperature=21
```

```ini
# /etc/systemd/system/morning-heat.timer
[Unit]
Description=Heat the house in the morning

[Timer]
OnCalendar=Mon..Fri 06:30

[Install]
WantedBy=timers.target
```

```
sudo systemctl daemon-reload
sudo systemctl enable --now morning-heat.timer
journalctl -u morning-heat.service        # see what happened
```

## Home Assistant

Home Assistant runs commands with [shell_command](https://www.home-assistant.io/integrations/shell_command/). When Home Assistant runs on the Pi with the IR LED (Home Assistant Core in a Python environment, or the container with `/dev/lirc0` and irlingo made available to it), call irlingo directly. In `configuration.yaml`:

```yaml
shell_command:
  tv_power: irlingo -r sony/RM-ED011 -f key=KEY_POWER
  aircon_heat: >-
    irlingo -r hvac_panasonic -f mode=HEAT -f temperature={{ temperature | int }}
    -f option=AUTO -f fan=AUTO -f updown=AUTO -f leftright=AUTO
  aircon_off: irlingo -r hvac_panasonic -f mode=OFF
```

Call them from scripts and automations, e.g. a button and a temperature slider:

```yaml
script:
  aircon_heat:
    sequence:
      - action: shell_command.aircon_heat
        data:
          temperature: "{{ states('input_number.aircon_temperature') }}"
```

With the container, map the device and give irlingo to it, e.g. in `docker-compose.yml`: `devices: ["/dev/lirc0:/dev/lirc0"]`, and a volume with the static `irlingo` binary and `/usr/share/irlingo`. The release binaries are statically linked, so they run in the container.

When Home Assistant runs elsewhere (e.g. Home Assistant OS on another machine), run irlingo on the Pi over SSH, see [From another computer](#from-another-computer):

```yaml
shell_command:
  tv_power: ssh -i /config/.ssh/id_ed25519 -o StrictHostKeyChecking=accept-new pi@ir-pi.local irlingo -r sony/RM-ED011 -f key=KEY_POWER
```

## Node-RED

Use an **exec** node with the command `irlingo` and the arguments in the node or in `msg.payload`, e.g. a payload of `-r sony/RM-ED011 -f key=KEY_POWER` with "append msg.payload" ticked. The node's third output carries the exit status.

Node-RED on Raspberry Pi OS runs as your login user: add it to the transmitter's group as described [above](#running-without-sudo).

## From another computer

The simplest way to send IR from another machine is SSH with a key:

```
ssh-copy-id pi@ir-pi.local
ssh pi@ir-pi.local irlingo -r sony/RM-ED011 -f key=KEY_POWER
```

To restrict what the key can do, prefix it in the Pi's `~/.ssh/authorized_keys` with `command="..."` and a script that only runs irlingo with the arguments from `SSH_ORIGINAL_COMMAND` you allow.

## Good practice

- **Wait between codes.** Devices need time to process a code; 300-500 ms between commands is usually enough. Do not run two irlingo commands at the same time, e.g. from two automations: codes sent at once disturb each other, so run them one after the other.
- **Send the complete state.** Air conditioner codes carry every setting, so give all of them in every command, from your own record of the state you want.
- **Check the exit status.** It tells you whether the code was sent, not whether the device received it. Line of sight and distance matter; see [Troubleshooting](troubleshooting.md).
