# Wiring

The checked-in firmware currently uses these ESP32-S3 GPIO assignments:

| Function | GPIO | Notes |
|---|---:|---|
| DS18B20 OneWire bus | 4 | Kettle and HLT sensors share the bus |
| Kettle SSR control | 5 | Time-proportional heater output |
| HLT SSR control | 6 | Time-proportional heater output |
| Circulation pump control | 7 | Active high in the current firmware |

The two DS18B20 devices are selected by their fixed hardware addresses in
`esp32/src/main.cpp`. Replace those addresses when different sensors are used.

## DS18B20 bus

Connect all DS18B20 data pins to GPIO 4 and use a suitable pull-up resistor
between data and the sensor supply. Ensure all low-voltage devices share the
required reference ground.

## Outputs

GPIO pins must not drive mains loads directly. Use appropriately rated,
galvanically isolated SSRs or interface circuits that are compatible with the
ESP32's 3.3 V logic. Verify the required active level before connecting a pump
relay or contactor.

## Before energizing equipment

1. Test with heaters and pump disconnected.
2. Confirm each GPIO starts in the safe/off state.
3. Confirm loss of Wi-Fi and CraftBeerPi communication turns outputs off.
4. Confirm sensor removal prevents heater activation.
5. Have mains wiring and protective devices checked by a qualified person.
