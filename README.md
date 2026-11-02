# ESP32-SammyBrew-controller

> [!CAUTION]
> ## FARE – NETTSPENNING / LIVSFARE
> Dette prosjektet kan styre varmeelementer, pumper og annet bryggeutstyr som
> er koblet til nettspenning. Feil prosjektering, komponentvalg, montering,
> kobling eller bruk kan føre til elektrisk støt, brann, alvorlig personskade
> eller død. Alt arbeid på nettspenningssiden skal prosjekteres, utføres,
> kontrolleres og dokumenteres av kvalifisert elektrofaglig personell og, når
> regelverket krever det, av en virksomhet registrert i
> Elvirksomhetsregisteret. Arbeid aldri på spenningssatt utstyr. Programvaren
> er ikke sikkerhetssertifisert og må aldri være eneste sikkerhetsbarriere.
>
> **DANGER – MAINS VOLTAGE / RISK OF DEATH:** This project can control
> mains-powered heating elements, pumps and other brewing equipment. Incorrect
> design, component selection, assembly, wiring or use can cause electric
> shock, fire, serious injury or death. All mains-voltage work must be designed,
> installed, inspected and documented by appropriately qualified electrical
> professionals and, where required by local law, a registered electrical
> contractor. Never work on energized equipment. This software is not safety
> certified and must never be the only safety barrier.

ESP32-based brewing controller with CraftBeerPi 4 integration, WebSocket
communication, DS18B20 temperature monitoring, SSR control, circulation-pump
control and local safety checks.


## Repository layout

```text
ESP32-SammyBrew-controller/
├── esp32/                         PlatformIO firmware
│   ├── include/
│   │   └── secrets.example.h
│   ├── src/
│   │   └── main.cpp
│   └── platformio.ini
├── craftbeerpi-plugin/
│   └── cbpi4-ESP32Controller/     CraftBeerPi 4 plugin
├── docs/
│   ├── protocol.md
│   ├── safety.md
│   └── wiring.md
├── LICENSE
└── README.md
```

## Features

- ESP32-S3 firmware built with PlatformIO and Arduino
- Two DS18B20 temperature channels: kettle and HLT
- Two SSR heater outputs with time-proportional power control
- Circulation-pump output
- CraftBeerPi 4 actors, sensors and WebSocket extension
- Application handshake, ping/pong monitoring and acknowledgements
- Local shutdown on lost communication, invalid or stale temperature, and
  overtemperature
- Arduino OTA support with password supplied through an environment variable

## Firmware setup

1. Install [PlatformIO](https://platformio.org/).
2. Copy the example credentials file:

   ```sh
   cd esp32
   cp include/secrets.example.h include/secrets.h
   ```

3. Edit `include/secrets.h` with the local Wi-Fi credentials. This file is
   ignored by Git.
4. Update `WS_HOST` in `src/main.cpp` if CraftBeerPi uses another address.
5. Set the OTA password before building or uploading:

   ```sh
   export ESP32_OTA_PASSWORD='choose-a-strong-password'
   export BEER_ESP32_OTA_PASSWORD="$ESP32_OTA_PASSWORD"
   ```

6. Build and upload over USB:

   ```sh
   pio run -e esp32-s3-usb
   pio run -e esp32-s3-usb -t upload
   ```

The OTA environment uses the upload address configured in `platformio.ini`.

## CraftBeerPi plugin

When CraftBeerPi is managed by pipx, install the plugin into the existing
`cbpi4` environment. Do not use a separate `pipx install`, because that would
create an isolated environment where the plugin cannot access CraftBeerPi.

Install the current `main` branch directly from GitHub:

```sh
pipx runpip cbpi4 install "git+https://github.com/sammynorway/ESP32-SammyBrew-controller.git@main#subdirectory=craftbeerpi-plugin/cbpi4-ESP32Controller"
```

To upgrade or reinstall the plugin later:

```sh
pipx runpip cbpi4 install --upgrade --force-reinstall "git+https://github.com/sammynorway/ESP32-SammyBrew-controller.git@main#subdirectory=craftbeerpi-plugin/cbpi4-ESP32Controller"
```

For development, the local checkout can be installed into the same environment:

```sh
pipx runpip cbpi4 install ./craftbeerpi-plugin/cbpi4-ESP32Controller
```

Verify that CraftBeerPi detects the plugin:

```sh
cbpi plugins
```

Restart CraftBeerPi after installation, then add the required components:

- `ESP32 WebSocket` extension
- `ESP32 Temperature` sensor for each temperature channel
- `ESP32 Kettle Heater` and/or `ESP32 HLT Heater`
- `ESP32 Circulation Pump` when used

The ESP32 connects to `ws://<craftbeerpi-host>:8000/esp32/ws` by default.

## Documentation

- [WebSocket protocol](docs/protocol.md)
- [Wiring](docs/wiring.md)
- [Safety principles](docs/safety.md)

## Security and electrical safety

Never commit `esp32/include/secrets.h`, passwords, tokens or exported
CraftBeerPi configuration. The controller switches mains-powered equipment;
use correctly rated, isolated components and have mains wiring performed or
checked by a qualified person. Software safety checks are not a replacement
for independent thermal cut-outs, fuses and emergency disconnection.

The current WebSocket endpoint and firmware Telnet logging do not authenticate
clients. Run them only on a trusted, firewalled brewing network; do not expose
ports 23 or 8000 directly to the internet.

## License

MIT License. See [LICENSE](LICENSE). The license covers the software and permits
reuse, modification and distribution, including commercial use. Its
"as-is/no-warranty" terms are not a safety approval, product certification or
substitute for compliance with applicable electrical laws and standards.
