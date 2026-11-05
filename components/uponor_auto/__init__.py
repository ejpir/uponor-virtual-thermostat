import esphome.codegen as cg
from esphome.components import button, climate, sensor, text_sensor, uart
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)

AUTO_LOAD = ["button", "climate", "sensor", "text_sensor", "web_server_base"]
DEPENDENCIES = ["uart"]

MAX_THERMOSTATS = 8

CONF_CLIMATE = "climate"
CONF_STATUS = "status"
CONF_MAIN_ADDRESS = "main_address"
CONF_THERMOSTATS = "thermostats"
CONF_RESCAN = "rescan"

uponor_auto_ns = cg.esphome_ns.namespace("uponor_auto")
UponorAuto = uponor_auto_ns.class_("UponorAuto", cg.Component, uart.UARTDevice)
UponorAutoClimate = uponor_auto_ns.class_(
    "UponorAutoClimate", climate.Climate, cg.Component
)
UponorAutoRescanButton = uponor_auto_ns.class_(
    "UponorAutoRescanButton", button.Button, cg.Parented.template(UponorAuto)
)


def _temperature_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
    )


def _diagnostic_text_schema():
    return text_sensor.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC)


_ROOM_SCHEMA = {}
for _i in range(1, MAX_THERMOSTATS + 1):
    _ROOM_SCHEMA[
        cv.Optional(f"room_{_i}_temperature", default={CONF_NAME: f"Room {_i} Temperature"})
    ] = _temperature_schema()
    _ROOM_SCHEMA[
        cv.Optional(f"room_{_i}_setpoint", default={CONF_NAME: f"Room {_i} Setpoint"})
    ] = _temperature_schema()

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(UponorAuto),
            cv.Optional(
                CONF_CLIMATE, default={CONF_NAME: "Main Thermostat"}
            ): climate.climate_schema(UponorAutoClimate),
            cv.Optional(
                CONF_STATUS, default={CONF_NAME: "Bus Status"}
            ): _diagnostic_text_schema(),
            cv.Optional(
                CONF_MAIN_ADDRESS, default={CONF_NAME: "Main Thermostat Address"}
            ): _diagnostic_text_schema(),
            cv.Optional(
                CONF_THERMOSTATS, default={CONF_NAME: "Detected Thermostats"}
            ): _diagnostic_text_schema(),
            cv.Optional(
                CONF_RESCAN, default={CONF_NAME: "Re-run Detection"}
            ): button.button_schema(
                UponorAutoRescanButton, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            **_ROOM_SCHEMA,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "uponor_auto",
    baud_rate=19200,
    require_tx=True,
    require_rx=True,
    data_bits=8,
    parity=None,
    stop_bits=1,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    thermostat = await climate.new_climate(config[CONF_CLIMATE])
    await cg.register_component(thermostat, config[CONF_CLIMATE])
    cg.add(thermostat.set_hub(var))
    cg.add(var.set_climate(thermostat))

    cg.add(var.set_status(await text_sensor.new_text_sensor(config[CONF_STATUS])))
    cg.add(
        var.set_main_address(await text_sensor.new_text_sensor(config[CONF_MAIN_ADDRESS]))
    )
    cg.add(
        var.set_thermostats(await text_sensor.new_text_sensor(config[CONF_THERMOSTATS]))
    )

    rescan = await button.new_button(config[CONF_RESCAN])
    await cg.register_parented(rescan, config[CONF_ID])

    for i in range(1, MAX_THERMOSTATS + 1):
        temperature = await sensor.new_sensor(config[f"room_{i}_temperature"])
        setpoint = await sensor.new_sensor(config[f"room_{i}_setpoint"])
        cg.add(var.set_room(i - 1, temperature, setpoint))
