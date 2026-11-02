# -*- coding: utf-8 -*-
import asyncio
import json
import logging
import time
from aiohttp import web
from cbpi.api import *
logger = logging.getLogger(__name__)
# ---------------------------------------------------------------------------
# Shared ESP32 state
# ---------------------------------------------------------------------------
esp32_state = {
    "connected": False,
    "ws": None,
    "temperatures": {
        "kettle": {"value": None, "timestamp": 0.0},
        "hlt": {"value": None, "timestamp": 0.0},
    },
    "heater": {
        "kettle": {
            "state": False,
            "requested_power": 0.0,
            "effective_power": 0.0,
            "fault": "NONE",
            "timestamp": 0.0,
        },
        "hlt": {
            "state": False,
            "requested_power": 0.0,
            "effective_power": 0.0,
            "fault": "NONE",
            "timestamp": 0.0,
        }
    },
    "pump": {
        "circulation": {
            "state": False,
            "fault": "NONE",
            "timestamp": 0.0,
        }
    },
}
# ---------------------------------------------------------------------------
# ESP32 Temperature Sensor
# ---------------------------------------------------------------------------
@parameters([
    Property.Select(
        "Channel",
        options=["kettle", "hlt"]
    )
])
class ESP32TemperatureSensor(CBPiSensor):
    def __init__(self, cbpi, id, props):
        super(ESP32TemperatureSensor, self).__init__(cbpi, id, props)
        self.value = 0.0
    async def run(self):
        while self.running is True:
            channel = self.props.get("Channel", "kettle")
            sensor_state = esp32_state["temperatures"].get(channel)
            if sensor_state is not None:
                value = sensor_state.get("value")
                timestamp = sensor_state.get("timestamp", 0.0)
                age = time.monotonic() - timestamp
                if value is not None and age < 5.0:
                    self.value = float(value)
                    self.push_update(self.value)
            await asyncio.sleep(1)
    def get_state(self):
        return dict(value=self.value)
# ---------------------------------------------------------------------------
# ESP32 Heater Actor
# ---------------------------------------------------------------------------
@parameters([])
class ESP32KettleHeater(CBPiActor):
    def on_start(self):
        self.state = False
        self.power = 0.0
        self.output = 0.0

    def _set_display_power(self, power):
        self.power = max(0.0, min(100.0, float(power)))
        self.output = self.maxoutput * self.power / 100.0

    def _schedule_ui_update(self):
        asyncio.create_task(self._push_ui_update())

    async def _push_ui_update(self):
        # Let CraftBeerPi finish updating its actor cache first, then broadcast
        # the complete actor values to connected dashboards.
        await asyncio.sleep(0)
        try:
            await self.cbpi.actor.actor_update(
                self.id,
                self.power,
                self.output,
                self.maxoutput
            )
        except Exception:
            logger.exception(
                "Failed to push kettle heater power to CraftBeerPi GUI"
            )

    async def _send_heater_command(self, power, state):
        ws = esp32_state.get("ws")
        if ws is None or ws.closed:
            logger.warning(
                "Cannot send kettle heater command - ESP32 WebSocket not connected"
            )
            return
        payload = {
            "type": "heater_power",
            "heater": "kettle",
            "power": float(power),
            "state": bool(state),
        }
        try:
            await ws.send_json(payload)
            logger.warning(
                "ESP32 KETTLE HEATER TX -> state=%s power=%.2f %%",
                state,
                float(power)
            )
        except Exception:
            logger.exception(
                "Failed sending kettle heater command to ESP32"
            )
    async def on(self, power=0, output=None):
        self.state = True
        if output is not None:
            self.output = max(0.0, min(self.maxoutput, float(output)))
            self.power = (
                self.output / self.maxoutput * 100.0
                if self.maxoutput > 0
                else 0.0
            )
        else:
            self._set_display_power(power)
        await self._send_heater_command(self.power, True)
        self._schedule_ui_update()

    async def off(self):
        self.state = False
        self.power = 0.0
        await self._send_heater_command(0.0, False)
        self._schedule_ui_update()

    async def set_power(self, power):
        self._set_display_power(power)
        await self._send_heater_command(self.power, self.state)
        self._schedule_ui_update()

    def get_state(self):
        return self.state
    async def run(self):
        last_power = None
        last_state = None
        while self.running is True:
            heater_state = esp32_state["heater"]["kettle"]
            timestamp = heater_state.get("timestamp", 0.0)
            age = time.monotonic() - timestamp
            if age < 10.0:
                power = float(heater_state.get("effective_power", 0.0))
                state = power > 0.0
            else:
                power = 0.0
                state = False
            self.power = power
            self.state = state
            if state != last_state or power != last_power:
                try:
                    await self.cbpi.actor.actor_update(
                        self.id,
                        power,
                        self.output,
                        self.maxoutput
                    )
                    logger.info(
                        "ESP32 actor GUI update -> state=%s power=%.1f %%",
                        state,
                        power
                    )
                except Exception:
                    logger.exception(
                        "Failed to push ESP32 heater state to CraftBeerPi GUI"
                    )
                last_state = state
                last_power = power
            await asyncio.sleep(0.5)
# ---------------------------------------------------------------------------
# ESP32 HLT Heater Actor
# ---------------------------------------------------------------------------
@parameters([])
class ESP32HltHeater(CBPiActor):
    def on_start(self):
        self.state = False
        self.power = 0.0
        self.output = 0.0

    def _set_display_power(self, power):
        self.power = max(0.0, min(100.0, float(power)))
        self.output = self.maxoutput * self.power / 100.0

    def _schedule_ui_update(self):
        asyncio.create_task(self._push_ui_update())

    async def _push_ui_update(self):
        # Let CraftBeerPi finish updating its actor cache first, then broadcast
        # the complete actor values to connected dashboards.
        await asyncio.sleep(0)
        try:
            await self.cbpi.actor.actor_update(
                self.id,
                self.power,
                self.output,
                self.maxoutput
            )
        except Exception:
            logger.exception(
                "Failed to push HLT heater power to CraftBeerPi GUI"
            )

    async def _send_heater_command(self, power, state):
        ws = esp32_state.get("ws")
        if ws is None or ws.closed:
            logger.warning(
                "Cannot send HLT heater command - ESP32 WebSocket not connected"
            )
            return
        payload = {
            "type": "heater_power",
            "heater": "hlt",
            "power": float(power),
            "state": bool(state),
        }
        try:
            await ws.send_json(payload)
            logger.warning(
                "ESP32 HLT HEATER TX -> state=%s power=%.2f %%",
                state,
                float(power)
            )
        except Exception:
            logger.exception(
                "Failed sending HLT heater command to ESP32"
            )

    async def on(self, power=0, output=None):
        self.state = True
        if output is not None:
            self.output = max(0.0, min(self.maxoutput, float(output)))
            self.power = (
                self.output / self.maxoutput * 100.0
                if self.maxoutput > 0
                else 0.0
            )
        else:
            self._set_display_power(power)
        await self._send_heater_command(self.power, True)
        self._schedule_ui_update()

    async def off(self):
        self.state = False
        self.power = 0.0
        await self._send_heater_command(0.0, False)
        self._schedule_ui_update()

    async def set_power(self, power):
        self._set_display_power(power)
        await self._send_heater_command(self.power, self.state)
        self._schedule_ui_update()

    def get_state(self):
        return self.state

    async def run(self):
        last_power = None
        last_state = None
        while self.running is True:
            heater_state = esp32_state["heater"]["hlt"]
            timestamp = heater_state.get("timestamp", 0.0)
            age = time.monotonic() - timestamp
            if age < 10.0:
                power = float(heater_state.get("effective_power", 0.0))
                state = power > 0.0
            else:
                power = 0.0
                state = False
            self.power = power
            self.state = state
            if state != last_state or power != last_power:
                try:
                    await self.cbpi.actor.actor_update(
                        self.id,
                        power,
                        self.output,
                        self.maxoutput
                    )
                    logger.info(
                        "ESP32 HLT actor GUI update -> state=%s power=%.1f %%",
                        state,
                        power
                    )
                except Exception:
                    logger.exception(
                        "Failed to push ESP32 HLT heater state to CraftBeerPi GUI"
                    )
                last_state = state
                last_power = power
            await asyncio.sleep(0.5)
# ---------------------------------------------------------------------------
# ESP32 Circulation Pump Actor
# ---------------------------------------------------------------------------
@parameters([])
class ESP32CirculationPump(CBPiActor):
    def on_start(self):
        self.state = False
        self.power = 0.0
        self.output = 0.0

    def _set_display_power(self, power):
        power = max(0.0, min(100.0, float(power)))
        self.power = power
        self.output = self.maxoutput * power / 100.0

    def _schedule_ui_update(self):
        # ActorController updates its cached output after set_power returns.
        # Publish on the next event-loop turn so the dashboard receives the
        # complete, current actor data without requiring a browser reload.
        asyncio.create_task(self._push_ui_update())

    async def _push_ui_update(self):
        await asyncio.sleep(0)
        try:
            await self.cbpi.actor.actor_update(
                self.id,
                self.power,
                self.output,
                self.maxoutput
            )
        except Exception:
            logger.exception(
                "Failed to push circulation pump power to CraftBeerPi GUI"
            )

    async def _send_pump_command(self, state):
        ws = esp32_state.get("ws")
        if ws is None or ws.closed:
            logger.warning(
                "Cannot send circulation pump command - ESP32 WebSocket not connected"
            )
            return

        payload = {
            "type": "pump_state",
            "pump": "circulation",
            "state": bool(state),
        }

        try:
            await ws.send_json(payload)
            logger.warning(
                "ESP32 CIRCULATION PUMP TX -> state=%s",
                state
            )
        except Exception:
            logger.exception(
                "Failed sending circulation pump command to ESP32"
            )

    async def on(self, power=100, output=None):
        self.state = True
        self._set_display_power(power)
        if output is not None:
            self.output = max(
                0.0,
                min(float(self.maxoutput), float(output))
            )
        await self._send_pump_command(True)
        self._schedule_ui_update()

    async def off(self):
        self.state = False
        self._set_display_power(0.0)
        await self._send_pump_command(False)
        self._schedule_ui_update()

    async def set_power(self, power):
        self._set_display_power(power)
        await self._send_pump_command(self.state)
        self._schedule_ui_update()

    def get_state(self):
        return self.state

    async def run(self):
        last_state = None
        while self.running is True:
            pump_state = esp32_state["pump"]["circulation"]
            timestamp = pump_state.get("timestamp", 0.0)
            age = time.monotonic() - timestamp
            state = bool(pump_state.get("state", False)) if age < 10.0 else False
            self.state = state

            if state != last_state:
                try:
                    await self.cbpi.actor.actor_update(
                        self.id,
                        self.power,
                        self.output,
                        self.maxoutput
                    )
                    logger.info(
                        "ESP32 circulation pump GUI update -> state=%s",
                        state
                    )
                except Exception:
                    logger.exception(
                        "Failed to push circulation pump state to CraftBeerPi GUI"
                    )
                last_state = state

            await asyncio.sleep(0.5)
# ---------------------------------------------------------------------------
# ESP32 WebSocket Extension
# ---------------------------------------------------------------------------
class ESP32WebSocket(CBPiExtension):
    def __init__(self, cbpi):
        self.cbpi = cbpi
        self.ws = None
        self.cbpi.register(self, "/")
    @request_mapping(
        path="/esp32/ws",
        auth_required=False
    )
    async def websocket_handler(self, request):
        ws = web.WebSocketResponse(heartbeat=10)
        await ws.prepare(request)
        self.ws = ws
        esp32_state["ws"] = ws
        esp32_state["connected"] = True
        logger.warning(
            "ESP32 WebSocket connected from %s",
            request.remote
        )
        try:
            async for msg in ws:
                if msg.type == web.WSMsgType.TEXT:
                    logger.debug(
                        "ESP32 WS RX: %s",
                        msg.data
                    )
                    try:
                        data = json.loads(msg.data)
                    except json.JSONDecodeError:
                        logger.warning("ESP32 sent invalid JSON")
                        await ws.send_json({
                            "type": "error",
                            "error": "invalid_json"
                        })
                        continue
                    message_type = data.get("type")
                    # HELLO / HANDSHAKE
                    if message_type == "hello":
                        esp32_state["connected"] = True
                        logger.warning(
                            "ESP32 hello received. Device=%s Firmware=%s",
                            data.get("device"),
                            data.get("firmware")
                        )
                        await ws.send_json({
                            "type": "hello_ack",
                            "server": "CraftBeerPi",
                            "protocol": 1
                        })
                    # APPLICATION PING
                    elif message_type == "ping":
                        await ws.send_json({"type": "pong"})
                    # TEMPERATURE
                    elif message_type == "temperature":
                        sensor = data.get("sensor")
                        value = data.get("value")
                        if sensor in ["kettle", "hlt"] and value is not None:
                            try:
                                value = float(value)
                                if -20.0 <= value <= 130.0:
                                    esp32_state["temperatures"][sensor]["value"] = value
                                    esp32_state["temperatures"][sensor]["timestamp"] = time.monotonic()
                                    logger.info(
                                        "ESP32 %s temperature: %.2f C",
                                        sensor,
                                        value
                                    )
                                    await ws.send_json({
                                        "type": "temperature_ack",
                                        "sensor": sensor,
                                        "value": value
                                    })
                                else:
                                    logger.warning(
                                        "Rejected ESP32 %s temperature: %s",
                                        sensor,
                                        value
                                    )
                                    await ws.send_json({
                                        "type": "error",
                                        "error": "temperature_out_of_range",
                                        "sensor": sensor
                                    })
                            except (TypeError, ValueError):
                                logger.warning(
                                    "Invalid ESP32 temperature value: %s",
                                    value
                                )
                                await ws.send_json({
                                    "type": "error",
                                    "error": "invalid_temperature",
                                    "sensor": sensor
                                })
                        else:
                            logger.warning(
                                "Unknown ESP32 temperature sensor: %s",
                                sensor
                            )
                            await ws.send_json({
                                "type": "error",
                                "error": "unknown_sensor"
                            })
                    # HEATER ACK
                    elif message_type == "heater_ack":
                        heater = data.get("heater", "kettle")
                        if heater in esp32_state["heater"]:
                            heater_state = esp32_state["heater"][heater]
                            heater_state["state"] = bool(
                                data.get("state", False)
                            )
                            heater_state["requested_power"] = float(
                                data.get("requested_power", 0.0)
                            )
                            heater_state["effective_power"] = float(
                                data.get("effective_power", 0.0)
                            )
                            heater_state["fault"] = data.get(
                                "fault",
                                "UNKNOWN"
                            )
                            heater_state["timestamp"] = time.monotonic()
                            logger.info(
                                "ESP32 %s heater ACK -> state=%s requested=%.1f %% effective=%.1f %% fault=%s",
                                heater,
                                heater_state["state"],
                                heater_state["requested_power"],
                                heater_state["effective_power"],
                                heater_state["fault"]
                            )
                    # PUMP ACK
                    elif message_type == "pump_ack":
                        pump = data.get("pump", "circulation")
                        if pump in esp32_state["pump"]:
                            pump_state = esp32_state["pump"][pump]
                            pump_state["state"] = bool(
                                data.get("state", False)
                            )
                            pump_state["fault"] = data.get(
                                "fault",
                                "UNKNOWN"
                            )
                            pump_state["timestamp"] = time.monotonic()
                            logger.info(
                                "ESP32 %s pump ACK -> state=%s fault=%s",
                                pump,
                                pump_state["state"],
                                pump_state["fault"]
                            )
                    # UNKNOWN MESSAGE TYPE
                    else:
                        logger.warning(
                            "Unknown ESP32 message type: %s",
                            message_type
                        )
                        await ws.send_json({
                            "type": "ack",
                            "received": message_type
                        })
                elif msg.type == web.WSMsgType.ERROR:
                    logger.error(
                        "ESP32 WebSocket error: %s",
                        ws.exception()
                    )
        except Exception:
            logger.exception(
                "ESP32 WebSocket handler exception"
            )
        finally:
            logger.warning("ESP32 WebSocket disconnected")
            esp32_state["connected"] = False
            if esp32_state.get("ws") is ws:
                esp32_state["ws"] = None
            if self.ws is ws:
                self.ws = None
        return ws
# ---------------------------------------------------------------------------
# Plugin setup
# ---------------------------------------------------------------------------
def setup(cbpi):
    cbpi.plugin.register(
        "ESP32 Temperature",
        ESP32TemperatureSensor
    )
    cbpi.plugin.register(
        "ESP32 Kettle Heater",
        ESP32KettleHeater
    )
    # Keep existing installations working until their actor type is migrated.
    cbpi.plugin.register(
        "ESP32 Test Heater",
        ESP32KettleHeater
    )
    cbpi.plugin.register(
        "ESP32 HLT Heater",
        ESP32HltHeater
    )
    cbpi.plugin.register(
        "ESP32 Circulation Pump",
        ESP32CirculationPump
    )
    cbpi.plugin.register(
        "ESP32 WebSocket",
        ESP32WebSocket
    )
