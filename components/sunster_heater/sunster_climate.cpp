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
static const char *const PRESETS[] = {"Manual", "Automatic", "Antifreeze"};

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
  traits.set_supported_custom_presets({"Manual", "Automatic", "Antifreeze"});
  traits.set_visual_min_temperature(min_temperature_);
  traits.set_visual_max_temperature(max_temperature_);
  traits.set_visual_temperature_step(0.5f);
  return traits;
}

void SunsterClimate::control(const climate::ClimateCall &call) {
  if (heater_ == nullptr) {
    return;
  }

  if (call.get_mode().has_value()) {
    climate::ClimateMode mode = *call.get_mode();
    if (mode == climate::CLIMATE_MODE_OFF) {
      heater_->turn_off();
    } else if (mode == climate::CLIMATE_MODE_HEAT) {
      heater_->turn_on();
    } else if (mode == climate::CLIMATE_MODE_FAN_ONLY) {
      ESP_LOGD(CLIMATE_TAG, "FAN_ONLY (Lüften) not yet implemented - protocol TBD");
    }
  }

  if (call.get_target_temperature().has_value()) {
    float target = *call.get_target_temperature();
    heater_->set_target_temperature(target);
  }

  if (call.has_custom_fan_mode()) {
    float pct = parse_power_percent(call.get_custom_fan_mode().c_str());
    heater_->set_power_level_percent(pct);
  }

  if (call.has_custom_preset()) {
    const char *preset = call.get_custom_preset().c_str();
    if (strcmp(preset, "Manual") == 0) {
      heater_->set_control_mode(ControlMode::MANUAL);
    } else if (strcmp(preset, "Automatic") == 0) {
      heater_->set_control_mode(ControlMode::AUTOMATIC);
    } else if (strcmp(preset, "Antifreeze") == 0) {
      heater_->set_control_mode(ControlMode::ANTIFREEZE);
    }
  }

  this->update();
}

void SunsterClimate::update() {
  if (heater_ == nullptr) {
    return;
  }

  float current = heater_->get_external_temperature();
  if (!std::isnan(current) && current >= -50.0f && current <= 100.0f) {
    this->current_temperature = current;
  }

  this->target_temperature = heater_->get_target_temperature();

  this->set_custom_fan_mode_(power_to_fan_mode(heater_->get_power_level_percent()));

  switch (heater_->get_control_mode()) {
    case ControlMode::MANUAL:
      this->set_custom_preset_("Manual");
      break;
    case ControlMode::AUTOMATIC:
      this->set_custom_preset_("Automatic");
      break;
    case ControlMode::ANTIFREEZE:
      this->set_custom_preset_("Antifreeze");
      break;
  }

  if (heater_->get_heater_enabled() && heater_->is_heating()) {
    this->mode = climate::CLIMATE_MODE_HEAT;
    this->action = climate::CLIMATE_ACTION_HEATING;
  } else if (heater_->get_heater_enabled()) {
    this->mode = climate::CLIMATE_MODE_HEAT;
    this->action = climate::CLIMATE_ACTION_IDLE;
  } else {
    this->mode = climate::CLIMATE_MODE_OFF;
    this->action = climate::CLIMATE_ACTION_OFF;
  }

  this->publish_state();
}

}  // namespace sunster_heater
}  // namespace esphome
