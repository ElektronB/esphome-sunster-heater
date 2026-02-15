#include "sunster_climate.h"
#include "sunster_heater.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sunster_heater {

static const char *const CLIMATE_TAG = "sunster_climate";

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
  traits.add_supported_mode(climate::CLIMATE_MODE_COOL);
  traits.add_supported_mode(climate::CLIMATE_MODE_FAN_ONLY);
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE |
                           climate::CLIMATE_SUPPORTS_ACTION);
  traits.set_visual_min_temperature(min_temperature_);
  traits.set_visual_max_temperature(max_temperature_);
  traits.set_visual_temperature_step(1.0f);
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
      case climate::CLIMATE_MODE_COOL:
        heater_->set_control_mode(ControlMode::MANUAL);
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

  // Target temperature → temperature in AUTOMATIC, power level in MANUAL
  if (call.get_target_temperature().has_value()) {
    float target = *call.get_target_temperature();
    ControlMode cm = heater_->get_control_mode();
    if (cm == ControlMode::AUTOMATIC || cm == ControlMode::ANTIFREEZE) {
      heater_->set_target_temperature(target);
    } else if (cm == ControlMode::MANUAL) {
      heater_->set_power_level_percent(std::max(10.0f, std::min(100.0f, target)));
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

  // Map ControlMode + heater state to HVAC mode and action
  ControlMode cmode = heater_->get_control_mode();
  bool heater_on = heater_->get_heater_enabled();
  bool is_heating = heater_->is_heating();

  switch (cmode) {
    case ControlMode::AUTOMATIC:
      this->target_temperature = heater_->get_target_temperature();
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
      this->target_temperature = heater_->get_power_level_percent();
      if (heater_on) {
        this->mode = climate::CLIMATE_MODE_COOL;
        this->action = is_heating ? climate::CLIMATE_ACTION_HEATING
                                  : climate::CLIMATE_ACTION_IDLE;
      } else {
        this->mode = climate::CLIMATE_MODE_OFF;
        this->action = climate::CLIMATE_ACTION_OFF;
      }
      break;

    case ControlMode::ANTIFREEZE:
      this->target_temperature = heater_->get_target_temperature();
      this->mode = climate::CLIMATE_MODE_HEAT;
      this->action = is_heating ? climate::CLIMATE_ACTION_HEATING
                                : climate::CLIMATE_ACTION_IDLE;
      break;

    case ControlMode::FAN_ONLY:
      this->target_temperature = NAN;
      this->mode = climate::CLIMATE_MODE_FAN_ONLY;
      this->action = climate::CLIMATE_ACTION_FAN;
      break;
  }

  this->publish_state();
}

}  // namespace sunster_heater
}  // namespace esphome
