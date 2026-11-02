# WebSocket protocol

The firmware is a WebSocket client. The CraftBeerPi plugin exposes the endpoint
`/esp32/ws` on port 8000. Messages are UTF-8 JSON objects with a `type` field.

## Connection and health

| Direction | Type | Important fields | Purpose |
|---|---|---|---|
| ESP32 → CBPi | `hello` | `device`, `firmware` | Starts the application handshake |
| CBPi → ESP32 | `hello_ack` | `server`, `protocol` | Confirms the handshake |
| ESP32 → CBPi | `ping` | — | Application-level health check |
| CBPi → ESP32 | `pong` | — | Keeps the controller communication watchdog healthy |

## Temperature

The ESP32 sends `temperature` with `sensor` set to `kettle` or `hlt` and a
numeric `value` in degrees Celsius. CraftBeerPi replies with
`temperature_ack`. Invalid values receive an `error` response.

```json
{"type":"temperature","sensor":"kettle","value":67.4}
```

## Heaters

CraftBeerPi sends `heater_power`. `heater` is `kettle` or `hlt`, `state` is a
boolean, and `power` is clamped by the firmware to 0–100 percent.

```json
{"type":"heater_power","heater":"kettle","state":true,"power":42.5}
```

The ESP32 returns `heater_ack` containing `state`, `ssr_on`,
`requested_power`, `effective_power` and `fault`. Effective power may be zero
even when power was requested if a local safety check blocks the output.

## Circulation pump

CraftBeerPi sends `pump_state` with `pump` set to `circulation` and a boolean
`state`. The ESP32 replies with `pump_ack`, including the effective state and
current fault code.

```json
{"type":"pump_state","pump":"circulation","state":true}
```

## Fault values

Current firmware fault strings include `NONE`, `WIFI_LOST`,
`WEBSOCKET_LOST`, `HANDSHAKE_MISSING`, `HEATER_COMMAND_TIMEOUT`,
`TEMPERATURE_STALE`, `TEMPERATURE_INVALID` and `OVERTEMPERATURE`.

Protocol version in `hello_ack` is currently `1`; firmware reports version
`0.4.0` in `hello`.
