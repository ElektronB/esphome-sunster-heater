#include "sunster_climate.h"
#include "sunster_heater.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sunster_heater {

static const char *const TAG = "sunster_climate";

void SunsterClimate::setup() {
  if (heater_ == nullptr) {
    ESP_LOGE(TAG, "SunsterHeater not set");
    return;
  }
  // Initial state sync
  this->update();
}

climate::ClimateTraits SunsterClimate::traits() {
  climate::ClimateTraits traits;
  traits.add_supported_mode(climate::CLIMATE_MODE_OFF);
  traits.add_supported_mode(climate::CLIMATE_MODE_HEAT);
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE |
                           climate::CLIMATE_SUPPORTS_ACTION);
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
    }
  }

  if (call.get_target_temperature().has_value()) {
    float target = *call.get_target_temperature();
    heater_->set_target_temperature(target);
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
