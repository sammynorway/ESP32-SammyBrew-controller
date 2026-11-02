# Safety principles

> [!CAUTION]
> ## FARE – NETTSPENNING / LIVSFARE
> Kontrolleren kan styre utstyr som er koblet til nettspenning. Berøring av
> spenningsførende deler eller feil i prosjektering, komponentvalg, isolasjon,
> jording, kapsling, kobling eller vern kan føre til elektrisk støt, lysbue,
> brann, alvorlig personskade eller død. Nettspenningssiden skal bare
> prosjekteres, bygges, kobles, testes, settes i drift, vedlikeholdes og
> kontrolleres av kvalifisert elektrofaglig personell. Der norsk regelverk
> krever det, skal arbeidet utføres av en virksomhet registrert i
> Elvirksomhetsregisteret. Koble fra, sikre mot innkobling og kontroller at
> anlegget er spenningsløst før arbeid begynner.
>
> **DANGER – MAINS VOLTAGE / RISK OF DEATH:** The controller can operate
> equipment connected to mains voltage. Contact with live parts, or errors in
> design, component selection, insulation, earthing, enclosure, wiring or
> protection, can cause electric shock, arc flash, fire, serious injury or
> death. Only appropriately qualified electrical professionals may design,
> build, connect, test, commission, maintain or inspect the mains-voltage side.
> Isolate the supply, prevent reconnection and verify absence of voltage before
> any work begins.

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

The installation must be designed so that a single software, microcontroller,
network, sensor or SSR failure cannot create an uncontrolled heating or pump
hazard. Use independent, correctly rated hardware protection, including
overcurrent protection, residual-current protection where applicable,
protective earthing, thermal cut-outs, contactors and an emergency disconnect.
An SSR can fail closed and must not be treated as a guaranteed isolation
device.

Test every shutdown path with the load disconnected before using heating
elements or pumps. Never rely on a network connection as the only means of
turning equipment off.

For installations in Norway, see the [DSB guidance for finding a registered
electrical contractor](https://www.dsb.no/sikkerhverdag/strom/finn-godkjent-elektriker/)
and the [regulations on electrical enterprises and qualification
requirements](https://lovdata.no/dokument/SF/forskrift/2013-06-19-739).

## Software and license disclaimer

The project is experimental control software, not a certified safety system or
a finished electrical product. The MIT License permits broad use and includes
an "as is" disclaimer without warranty. That disclaimer does not certify the
system, make an installation compliant, or replace duties imposed by mandatory
law. Every person who builds or operates a system based on this project is
responsible for obtaining competent electrical assessment and complying with
the laws, standards and instructions that apply at the installation site.

## Credentials

Wi-Fi credentials belong only in `esp32/include/secrets.h`, which is ignored by
Git. OTA passwords are read from environment variables referenced by
`platformio.ini`. Do not commit CraftBeerPi backups, `.env` files or generated
firmware images containing embedded credentials.

The CraftBeerPi WebSocket route currently has `auth_required=False`, and the
firmware provides unauthenticated Telnet logging on port 23. Keep both services
inside a trusted, firewalled LAN or VLAN and never forward them from the public
internet.
