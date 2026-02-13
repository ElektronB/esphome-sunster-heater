import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.const import CONF_ID
from . import sunster_heater_ns, SunsterHeater

AUTO_LOAD = ["sunster_heater"]
DEPENDENCIES = ["climate"]

SunsterClimate = sunster_heater_ns.class_("SunsterClimate", climate.Climate, cg.PollingComponent)

CONF_SUNSTER_HEATER_ID = "sunster_heater_id"
CONF_MIN_TEMPERATURE = "min_temperature"
CONF_MAX_TEMPERATURE = "max_temperature"

CONFIG_SCHEMA = climate.CLIMATE_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(SunsterClimate),
        cv.Required(CONF_SUNSTER_HEATER_ID): cv.use_id(SunsterHeater),
        cv.Optional(CONF_MIN_TEMPERATURE, default=5.0): cv.float_range(min=0, max=30),
        cv.Optional(CONF_MAX_TEMPERATURE, default=35.0): cv.float_range(min=10, max=50),
    }
).extend(cv.polling_component_schema("1s"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)

    heater = await cg.get_variable(config[CONF_SUNSTER_HEATER_ID])
    cg.add(var.set_sunster_heater(heater))

    cg.add(var.set_min_temperature(config[CONF_MIN_TEMPERATURE]))
    cg.add(var.set_max_temperature(config[CONF_MAX_TEMPERATURE]))
