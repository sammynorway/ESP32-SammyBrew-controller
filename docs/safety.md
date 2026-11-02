# Safety principles

The ESP32 is responsible for enforcing local output safety. CraftBeerPi sends
requests, but the firmware decides whether an output can be energized.

## Implemented checks

- Heater and pump outputs initialize off.
- Lost Wi-Fi, WebSocket connection or application handshake blocks outputs.
- A missing `pong` for 15 seconds is treated as failed communication.
- Temperature older than 5 seconds blocks the associated heater.
- Implausible DS18B20 readings are rejected.
- Kettle and HLT heaters are disabled at 105 °C.
- Requested heater power is limited to 0–100 percent.
- A heater `state` of false always forces requested power to zero.
- Acknowledgements report requested power, effective power and fault state.

## Operational requirements

Software is only one safety layer. A real brewing installation should also
use correctly rated fuses, earth protection, contactors or SSRs, independent
overtemperature protection, an accessible emergency disconnect and an
enclosure suitable for heat, moisture and mains voltage.

Test every shutdown path with the load disconnected before using heating
elements or pumps. Never rely on a network connection as the only means of
turning equipment off.

## Credentials

Wi-Fi credentials belong only in `esp32/include/secrets.h`, which is ignored by
Git. OTA passwords are read from environment variables referenced by
`platformio.ini`. Do not commit CraftBeerPi backups, `.env` files or generated
firmware images containing embedded credentials.

The CraftBeerPi WebSocket route currently has `auth_required=False`, and the
firmware provides unauthenticated Telnet logging on port 23. Keep both services
inside a trusted, firewalled LAN or VLAN and never forward them from the public
internet.
