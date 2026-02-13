#pragma once

#include "esphome/components/climate/climate.h"
#include "esphome/components/climate/climate_mode.h"
#include "esphome/components/climate/climate_traits.h"
#include "esphome/core/component.h"

namespace esphome {
namespace sunster_heater {

class SunsterHeater;

class SunsterClimate : public climate::Climate, public PollingComponent, public Component {
 public:
  void set_sunster_heater(SunsterHeater *heater) { heater_ = heater; }
  void set_min_temperature(float min_temp) { min_temperature_ = min_temp; }
  void set_max_temperature(float max_temp) { max_temperature_ = max_temp; }

  void setup() override;
  void update() override;

  climate::ClimateTraits traits() override;

 protected:
  void control(const climate::ClimateCall &call) override;

  SunsterHeater *heater_{nullptr};
  float min_temperature_{5.0f};
  float max_temperature_{35.0f};
};

}  // namespace sunster_heater
}  // namespace esphome
