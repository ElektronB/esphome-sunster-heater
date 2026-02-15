#include "sunster_climate.h"
#include "sunster_heater.h"
#include "esphome/core/log.h"
#include <cstdlib>
#include <cstring>

namespace esphome {
namespace sunster_heater {

static const char *const CLIMATE_TAG = "sunster_climate";

// Custom fan modes: power level 10%..100%
static const char *const FAN_MODES[] = {"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"};

static float parse_power_percent(const char *s) {
  if (!s) return 10.0f;
  char *end;
  float v = strtof(s, &end);
  if (end == s) return 10.0f;
  return std::max(10.0f, std::min(100.0f, v));
}

static const char *power_to_fan_mode(float pct) {
  int idx = static_cast<int>((pct - 5.0f) / 10.0f);
  idx = std::max(0, std::min(9, idx));
  return FAN_MODES[idx];
}

void SunsterClimate::setup() {
  if (heater_ == nullptr) {
    ESP_LOGE(CLIMATE_TAG, "SunsterHeater not set");
    return;
  }
  this->update();
}

climate::ClimateTraits SunsterClimate::traits() {
  climate::ClimateTraits traits;
  traits.add_supported_mode(climate::CLIMATE_MODE_OFF);
  traits.add_supported_mode(climate::CLIMATE_MODE_HEAT);
  traits.add_supported_mode(climate::CLIMATE_MODE_FAN_ONLY);
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE |
                           climate::CLIMATE_SUPPORTS_ACTION);
  traits.set_supported_custom_fan_modes({"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"});
  traits.set_supported_custom_presets({"Power"});
  traits.set_visual_min_temperature(min_temperature_);
  traits.set_visual_max_temperature(max_temperature_);
  traits.set_visual_temperature_step(0.5f);
  return traits;
}

void SunsterClimate::control(const climate::ClimateCall &call) {
  if (heater_ == nullptr) return;

  if (call.get_mode().has_value()) {
    climate::ClimateMode mode = *call.get_mode();
    switch (mode) {
      case climate::CLIMATE_MODE_OFF:
        heater_->set_automatic_master_enabled(false);
        heater_->turn_off();
        break;
      case climate::CLIMATE_MODE_HEAT:
        heater_->set_control_mode(ControlMode::AUTOMATIC);
        heater_->set_automatic_master_enabled(true);
        heater_->turn_on();
        break;
      case climate::CLIMATE_MODE_FAN_ONLY:
        heater_->set_control_mode(ControlMode::FAN_ONLY);
        ESP_LOGW(CLIMATE_TAG, "FAN_ONLY mode: protocol support TBD, placeholder only");
        break;
      default:
        break;
    }
  }

  // Custom preset "Power" → switch to manual power control
  if (call.has_custom_preset() && call.get_custom_preset() == "Power") {
    heater_->set_control_mode(ControlMode::MANUAL);
  }

  // Standard preset NONE → switch back to automatic temperature control
  if (call.get_preset().has_value() && *call.get_preset() == climate::CLIMATE_PRESET_NONE) {
    heater_->set_control_mode(ControlMode::AUTOMATIC);
  }

  // Target temperature only in AUTOMATIC mode
  if (call.get_target_temperature().has_value() && heater_->get_control_mode() == ControlMode::AUTOMATIC) {
    float target = *call.get_target_temperature();
    heater_->set_target_temperature(target);
  }

  // Power level only in MANUAL / FAN_ONLY mode
  if (call.has_custom_fan_mode()) {
    ControlMode cm = heater_->get_control_mode();
    if (cm == ControlMode::MANUAL || cm == ControlMode::FAN_ONLY) {
      float pct = parse_power_percent(call.get_custom_fan_mode().c_str());
      heater_->set_power_level_percent(pct);
    }
  }

  this->update();
}

void SunsterClimate::update() {
  if (heater_ == nullptr) return;

  // Current temperature from external sensor
  float current = heater_->get_external_temperature();
  if (!std::isnan(current) && current >= -50.0f && current <= 100.0f) {
    this->current_temperature = current;
  }

  // Map ControlMode + heater state to HVAC mode, preset, and action
  ControlMode cmode = heater_->get_control_mode();
  bool heater_on = heater_->get_heater_enabled();
  bool is_heating = heater_->is_heating();

  switch (cmode) {
    case ControlMode::AUTOMATIC:
      this->target_temperature = heater_->get_target_temperature();
      this->set_preset_(climate::CLIMATE_PRESET_NONE);
      if (heater_->is_automatic_master_enabled()) {
        this->mode = climate::CLIMATE_MODE_HEAT;
        this->action = is_heating ? climate::CLIMATE_ACTION_HEATING
                                  : climate::CLIMATE_ACTION_IDLE;
      } else {
        this->mode = climate::CLIMATE_MODE_OFF;
        this->action = climate::CLIMATE_ACTION_OFF;
      }
      break;

    case ControlMode::MANUAL:
      this->target_temperature = NAN;
      this->set_custom_fan_mode_(power_to_fan_mode(heater_->get_power_level_percent()));
      this->set_custom_preset_("Power");
      if (heater_on) {
        this->mode = climate::CLIMATE_MODE_HEAT;
        this->action = is_heating ? climate::CLIMATE_ACTION_HEATING
                                  : climate::CLIMATE_ACTION_IDLE;
      } else {
        this->mode = climate::CLIMATE_MODE_OFF;
        this->action = climate::CLIMATE_ACTION_OFF;
      }
      break;

    case ControlMode::ANTIFREEZE:
      this->target_temperature = heater_->get_target_temperature();
      this->set_preset_(climate::CLIMATE_PRESET_NONE);
      this->mode = climate::CLIMATE_MODE_HEAT;
      this->action = is_heating ? climate::CLIMATE_ACTION_HEATING
                                : climate::CLIMATE_ACTION_IDLE;
      break;

    case ControlMode::FAN_ONLY:
      this->target_temperature = NAN;
      this->set_custom_fan_mode_(power_to_fan_mode(heater_->get_power_level_percent()));
      this->set_preset_(climate::CLIMATE_PRESET_NONE);
      this->mode = climate::CLIMATE_MODE_FAN_ONLY;
      this->action = climate::CLIMATE_ACTION_FAN;
      break;
  }

  this->publish_state();
}

}  // namespace sunster_heater
}  // namespace esphome
