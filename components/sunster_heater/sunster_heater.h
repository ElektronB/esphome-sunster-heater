#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/hal.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/number/number.h"
#include "esphome/components/button/button.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/preferences.h"
#include <cmath>
#include <vector>

namespace esphome {

// Forward declaration for optional time component
namespace time { class RealTimeClock; }

namespace sunster_heater {

static const char *const TAG = "sunster_heater";

// Fuel consumption constants
static const float INJECTED_PER_PULSE = 0.022f; // ml per fuel pump pulse

// Control modes
enum class ControlMode : uint8_t {
  MANUAL = 0,
  AUTOMATIC = 1,
  ANTIFREEZE = 2
};

// Heater states from protocol analysis
enum class HeaterState : uint8_t {
  OFF = 0x00,
  POLLING_STATE = 0x01,  // Used for status polling (was GLOW_PLUG_PREHEAT)
  HEATING_UP = 0x02,
  STABLE_COMBUSTION = 0x03,
  STOPPING_COOLING = 0x04,
  UNKNOWN = 0xFF
};

// Controller command states
enum class ControllerState : uint8_t {
  CMD_OFF = 0x02,
  CMD_START = 0x06,
  CMD_RUNNING = 0x08
};

// Communication constants
static const uint8_t FRAME_START = 0xAA;
static const uint8_t CONTROLLER_ID = 0x66;
static const uint8_t HEATER_ID = 0x77;
static const uint8_t CONTROLLER_FRAME_LENGTH = 0x0B;
static const uint8_t HEATER_FRAME_LENGTH = 0x34;  // 0x34 for newer firmware (57 bytes)
static const uint32_t COMMUNICATION_TIMEOUT_MS = 5000;
static const uint32_t SEND_INTERVAL_MS = 1000;
static const uint32_t DEFAULT_POLLING_INTERVAL_MS = 300000; // 1 minute when not heating

// Fuel consumption tracking structure for persistence
struct FuelConsumptionData {
  float daily_consumption_ml;
  uint32_t last_reset_day;
  float total_pulses;  // Keep as float to avoid precision loss
};

// Config structure for persistence (PI, target temp, hysteresis, injected_per_pulse)
struct HeaterConfigData {
  uint32_t version{3};
  float pi_kp;
  float pi_ki;
  float pi_kd;
  float target_temperature;
  float pi_output_min_off;
  float pi_output_min_on;
  float injected_per_pulse;
  float pi_off_delay;
};

class SunsterHeater : public PollingComponent, public uart::UARTDevice {
 public:
  // Configuration methods
  void set_target_temperature(float temperature) { target_temperature_ = temperature; }
  void set_power_level(uint8_t level) {
    power_level_ = std::max(1, std::min(10, (int)level));
  }
  void set_control_mode(ControlMode mode);
  void set_default_power_percent(float percent) { default_power_percent_ = percent; }
  void set_injected_per_pulse(float ml_per_pulse) { injected_per_pulse_ = ml_per_pulse; }
  float get_injected_per_pulse() const { return injected_per_pulse_; }
  void set_polling_interval(uint32_t interval_ms) { polling_interval_ms_ = interval_ms; }
  void set_passive_sniff_mode(bool enable) { passive_sniff_mode_ = enable; }
  bool is_passive_sniff_mode() const { return passive_sniff_mode_; }
  void set_min_voltage_start(float voltage) { min_voltage_start_ = voltage; }
  void set_min_voltage_operate(float voltage) { min_voltage_operate_ = voltage; }
  void set_antifreeze_temp_on(float temp) { antifreeze_temp_on_ = temp; }
  void set_antifreeze_temp_medium(float temp) { antifreeze_temp_medium_ = temp; }
  void set_antifreeze_temp_low(float temp) { antifreeze_temp_low_ = temp; }
  void set_antifreeze_temp_off(float temp) { antifreeze_temp_off_ = temp; }
  void set_pi_kp(float kp) { pi_kp_ = kp; }
  void set_pi_ki(float ki) { pi_ki_ = ki; }
  void set_pi_kd(float kd) { pi_kd_ = kd; }
  void set_pi_off_delay(float delay_s) { pi_off_delay_ = delay_s; }
  void set_pi_output_min_off(float v) { pi_output_min_off_ = v; }
  void set_pi_output_min_on(float v) { pi_output_min_on_ = v; }

  float get_target_temperature() const { return target_temperature_; }
  float get_power_level_percent() const { return power_level_ * 10.0f; }
  float get_pi_kp() const { return pi_kp_; }
  float get_pi_ki() const { return pi_ki_; }
  float get_pi_kd() const { return pi_kd_; }
  float get_pi_off_delay() const { return pi_off_delay_; }
  float get_pi_output_min_off() const { return pi_output_min_off_; }
  float get_pi_output_min_on() const { return pi_output_min_on_; }

  void save_config_preferences();

  // Time component setter
  void set_time_component(time::RealTimeClock *time) { time_component_ = time; }

  // Number component setters (for publishing config to HA after load)
  void set_injected_per_pulse_number(number::Number *num) { injected_per_pulse_number_ = num; }
  void set_power_level_number(number::Number *num) { power_level_number_ = num; }
  void set_pi_kp_number(number::Number *num) { pi_kp_number_ = num; }
  void set_pi_ki_number(number::Number *num) { pi_ki_number_ = num; }
  void set_pi_kd_number(number::Number *num) { pi_kd_number_ = num; }
  void set_pi_off_delay_number(number::Number *num) { pi_off_delay_number_ = num; }
  void set_target_temperature_number(number::Number *num) { target_temperature_number_ = num; }
  void set_pi_output_min_off_number(number::Number *num) { pi_output_min_off_number_ = num; }
  void set_pi_output_min_on_number(number::Number *num) { pi_output_min_on_number_ = num; }
  void set_control_mode_select(select::Select *sel) { control_mode_select_ = sel; }

  // External temperature sensor
  void set_external_temperature_sensor(sensor::Sensor *sensor) { external_temperature_sensor_ = sensor; }

  // Sensor setters
  void set_input_voltage_sensor(sensor::Sensor *sensor) { input_voltage_sensor_ = sensor; }
  void set_state_sensor(text_sensor::TextSensor *sensor) { state_sensor_ = sensor; }
  void set_power_level_sensor(sensor::Sensor *sensor) { power_level_sensor_ = sensor; }
  void set_fan_speed_sensor(sensor::Sensor *sensor) { fan_speed_sensor_ = sensor; }
  void set_pump_frequency_sensor(sensor::Sensor *sensor) { pump_frequency_sensor_ = sensor; }
  void set_glow_plug_status_sensor(text_sensor::TextSensor *sensor) { glow_plug_status_sensor_ = sensor; }
  void set_heat_exchanger_temperature_sensor(sensor::Sensor *sensor) { heat_exchanger_temperature_sensor_ = sensor; }
  void set_state_duration_sensor(sensor::Sensor *sensor) { state_duration_sensor_ = sensor; }
  void set_cooling_down_sensor(binary_sensor::BinarySensor *sensor) { cooling_down_sensor_ = sensor; }
  void set_hourly_consumption_sensor(sensor::Sensor *sensor) { hourly_consumption_sensor_ = sensor; }
  void set_daily_consumption_sensor(sensor::Sensor *sensor) { daily_consumption_sensor_ = sensor; }
  void set_total_consumption_sensor(sensor::Sensor *sensor) { total_consumption_sensor_ = sensor; }
  void set_low_voltage_error_sensor(binary_sensor::BinarySensor *sensor) { low_voltage_error_sensor_ = sensor; }
  void set_pi_output_sensor(sensor::Sensor *sensor) { pi_output_sensor_ = sensor; }

  // Control methods
  void turn_on();
  void turn_off();
  void set_power_level_percent(float percent);
  void reset_daily_consumption();
  void reset_total_consumption();

  // Control mode management
  bool is_automatic_mode() const { return control_mode_ == ControlMode::AUTOMATIC; }
  bool is_manual_mode() const { return control_mode_ == ControlMode::MANUAL; }
  bool is_antifreeze_mode() const { return control_mode_ == ControlMode::ANTIFREEZE; }
  float get_external_temperature() const { return external_temperature_; }
  bool has_external_sensor() const {
    return external_temperature_sensor_ != nullptr &&
           !std::isnan(external_temperature_);
  }

  // Status getters
  HeaterState get_heater_state() const { return current_state_; }
  float get_current_temperature() const { return current_temperature_; }
  bool is_heating() const {
    return current_state_ == HeaterState::POLLING_STATE ||
           current_state_ == HeaterState::HEATING_UP ||
           current_state_ == HeaterState::STABLE_COMBUSTION;
  }
  bool is_connected() const { return last_received_time_ + COMMUNICATION_TIMEOUT_MS > millis(); }
  bool has_low_voltage_error() const { return low_voltage_error_; }
  bool get_heater_enabled() const { return heater_enabled_; }
  bool is_state_synced_once() const { return heater_state_synced_once_; }
  void set_automatic_master_enabled(bool en) { automatic_master_enabled_ = en; }
  bool is_automatic_master_enabled() const { return automatic_master_enabled_; }

  // Fuel consumption getters
  float get_daily_consumption() const { return daily_consumption_ml_; }
  float get_instantaneous_consumption_rate() const { return pump_frequency_ * injected_per_pulse_ * 3600.0f; }

  // Component lifecycle
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  // Communication handling
  void send_controller_frame();
  void process_heater_frame(const std::vector<uint8_t> &frame);
  void check_uart_data();
  bool validate_frame(const std::vector<uint8_t> &frame, uint8_t expected_length);
  void log_frame_raw(const char* direction, const std::vector<uint8_t> &frame);
  void log_decode_attempt(const std::vector<uint8_t> &frame, uint8_t expected_length);

  // Data parsing helpers
  uint16_t read_uint16_be(const std::vector<uint8_t> &data, size_t offset);
  float parse_temperature(const std::vector<uint8_t> &data, size_t offset);
  float parse_voltage(const std::vector<uint8_t> &data, size_t offset);
  const char* state_to_string(HeaterState state);

  // State management
  void update_sensors(const std::vector<uint8_t> &frame);
  void handle_communication_timeout();
  void check_voltage_safety();
  void handle_antifreeze_mode();
  void handle_automatic_mode();

  // Fuel consumption tracking
  void update_fuel_consumption(float pump_frequency);
  void save_fuel_consumption_data();
  void load_fuel_consumption_data();
  void load_config_data();
  void save_config_data();
  void publish_all_config_entities_();
  void check_daily_reset();
  uint32_t get_days_since_epoch();

  // Communication state
  std::vector<uint8_t> rx_buffer_;
  uint32_t last_received_time_{0};
  uint32_t last_send_time_{0};
  bool frame_sync_{false};
  uint32_t polling_interval_ms_{DEFAULT_POLLING_INTERVAL_MS};
  bool passive_sniff_mode_{false};  // Only log RX/decode, never send
  bool heater_state_synced_once_{false};  // After first heater frame, sync heater_enabled_ from state so switch can init

  // Control state
  bool heater_enabled_{false};
  bool automatic_master_enabled_{true};  // Power switch: when false, automatic mode won't turn on
  uint8_t power_level_{8};  // 1-10 scale, default 80%
  float target_temperature_{20.0};
  HeaterState current_state_{HeaterState::OFF};
  ControlMode control_mode_{ControlMode::MANUAL};
  float default_power_percent_{80.0};
  float injected_per_pulse_{INJECTED_PER_PULSE};
  float min_voltage_start_{12.3f};
  float min_voltage_operate_{11.4f};
  float antifreeze_temp_on_{2.0f};
  float antifreeze_temp_medium_{6.0f};
  float antifreeze_temp_low_{8.0f};
  float antifreeze_temp_off_{9.0f};
  static constexpr float ANTIFREEZE_HYSTERESIS = 0.4f;
  float last_antifreeze_power_{0.0f};
  bool antifreeze_active_{false};

  // PI controller (automatic mode)
  float pi_kp_{10.0f};
  float pi_ki_{0.5f};
  float pi_kd_{0.0f};
  float pi_off_delay_{60.0f};
  float pi_output_min_off_{3.0f};
  float pi_output_min_on_{15.0f};
  float pi_integral_{0.0f};
  float last_error_{0.0f};
  uint32_t last_pi_time_{0};
  uint32_t time_entered_off_region_{0};
  uint32_t time_stable_combustion_entered_{0};  // when entered STABLE_COMBUSTION for min-on time
  static constexpr float PI_INTEGRAL_MAX = 100.0f;
  static constexpr uint32_t PI_MIN_ON_TIME_MS = 30000;  // 30s min on after stable combustion

  // Parsed sensor values
  float current_temperature_{0.0};
  float external_temperature_{NAN};
  float input_voltage_{0.0};
  float heat_exchanger_temperature_{0.0};
  uint16_t fan_speed_{0};
  float pump_frequency_{0.0};
  uint16_t state_duration_{0};
  bool cooling_down_{false};
  bool low_voltage_error_{false};

  // Fuel consumption tracking
  float last_pump_frequency_{0.0};
  uint32_t last_consumption_update_{0};
  float daily_consumption_ml_{0.0};
  uint32_t current_day_{0};
  float total_fuel_pulses_{0.0};
  float total_consumption_ml_{0.0};
  ESPPreferenceObject pref_fuel_consumption_;
  ESPPreferenceObject pref_config_;

  time::RealTimeClock *time_component_{nullptr};
  bool time_sync_warning_shown_{false};

  sensor::Sensor *external_temperature_sensor_{nullptr};
  sensor::Sensor *input_voltage_sensor_{nullptr};
  text_sensor::TextSensor *state_sensor_{nullptr};
  sensor::Sensor *power_level_sensor_{nullptr};
  sensor::Sensor *fan_speed_sensor_{nullptr};
  sensor::Sensor *pump_frequency_sensor_{nullptr};
  text_sensor::TextSensor *glow_plug_status_sensor_{nullptr};
  sensor::Sensor *heat_exchanger_temperature_sensor_{nullptr};
  sensor::Sensor *state_duration_sensor_{nullptr};
  binary_sensor::BinarySensor *cooling_down_sensor_{nullptr};
  sensor::Sensor *hourly_consumption_sensor_{nullptr};
  sensor::Sensor *daily_consumption_sensor_{nullptr};
  sensor::Sensor *total_consumption_sensor_{nullptr};
  binary_sensor::BinarySensor *low_voltage_error_sensor_{nullptr};
  sensor::Sensor *pi_output_sensor_{nullptr};
  number::Number *injected_per_pulse_number_{nullptr};
  number::Number *power_level_number_{nullptr};
  number::Number *pi_kp_number_{nullptr};
  number::Number *pi_ki_number_{nullptr};
  number::Number *pi_kd_number_{nullptr};
  number::Number *pi_off_delay_number_{nullptr};
  number::Number *target_temperature_number_{nullptr};
  number::Number *pi_output_min_off_number_{nullptr};
  number::Number *pi_output_min_on_number_{nullptr};
  select::Select *control_mode_select_{nullptr};
  float last_pi_output_{0.0f};
};

// Number component for injected per pulse configuration
class SunsterInjectedPerPulseNumber : public number::Number, public Component {
 public:
  void set_sunster_heater(SunsterHeater *heater) { heater_ = heater; }
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void setup() override {
    if (heater_) {
      float v = heater_->get_injected_per_pulse();
      if (std::isnan(v)) v = 0.022f;
      ESP_LOGI(TAG, "[SEND_HA] InjectedPerPulse = %.3f (setup)", v);
      this->publish_state(v);
    }
  }
  void dump_config() override {
    LOG_NUMBER("", "Sunster Heater Injected Per Pulse", this);
  }

 protected:
  void loop() override {
    Component::loop();
    if (!heater_) return;
    uint32_t now = millis();
    uint32_t interval = (now < 60000u) ? 3000u : 15000u;
    if (last_publish_ == 0u || (now - last_publish_ >= interval)) {
      float v = heater_->get_injected_per_pulse();
      if (std::isnan(v)) v = 0.022f;
      if (last_publish_ == 0u) ESP_LOGI(TAG, "[SEND_HA] InjectedPerPulse = %.3f (first loop)", v);
      last_publish_ = now;
      this->publish_state(v);
    }
  }
  void control(float value) override {
    if (heater_) {
      heater_->set_injected_per_pulse(value);
      heater_->save_config_preferences();
      this->publish_state(value);
    }
  }

  SunsterHeater *heater_{nullptr};
  uint32_t last_publish_{0};
};

// Button component for resetting total consumption
class SunsterResetTotalConsumptionButton : public button::Button, public Component {
 public:
  void set_sunster_heater(SunsterHeater *heater) { heater_ = heater; }
  void dump_config() override {
    LOG_BUTTON("", "Sunster Heater Reset Total", this);
  }

 protected:
  void press_action() override {
    if (heater_) {
      heater_->reset_total_consumption();
    }
  }

  SunsterHeater *heater_{nullptr};
};

// Switch component for heater power on/off (works in all modes; in Automatic, PI sets power level)
class SunsterHeaterPowerSwitch : public switch_::Switch, public Component {
 public:
  void set_sunster_heater(SunsterHeater *heater) { heater_ = heater; }
  void dump_config() override {
    LOG_SWITCH("", "Sunster Heater Power Switch", this);
  }

 protected:
  void loop() override {
    Component::loop();
    if (!heater_) return;
    uint32_t now = millis();
    if (heater_->is_state_synced_once()) {
      bool current_enabled = heater_->get_heater_enabled();
      if (!initial_state_published_) {
        // First publish after sync - always publish current state
        this->publish_state(current_enabled);
        initial_state_published_ = true;
        last_sync_publish_ = now;
        last_published_state_ = current_enabled;
      } else {
        // Keep switch in sync: publish if state changed or every 2s
        if (current_enabled != last_published_state_ || (now - last_sync_publish_ >= 2000u)) {
          this->publish_state(current_enabled);
          last_sync_publish_ = now;
          last_published_state_ = current_enabled;
        }
      }
    } else if (!pre_sync_published_ && now > 500u) {
      this->publish_state(false);
      pre_sync_published_ = true;
    }
  }
  void write_state(bool state) override {
    if (heater_) {
      heater_->set_automatic_master_enabled(state);
      if (state) {
        heater_->turn_on();
      } else {
        heater_->turn_off();
      }
      this->publish_state(state);
      last_published_state_ = state;
      if (heater_->is_state_synced_once()) {
        last_sync_publish_ = millis();
      }
    }
  }

  SunsterHeater *heater_{nullptr};
  bool initial_state_published_{false};
  bool pre_sync_published_{false};
  bool last_published_state_{false};
  uint32_t last_sync_publish_{0};
};

// Number component for power level control (Manual mode only)
class SunsterHeaterPowerLevelNumber : public number::Number, public Component {
 public:
  void set_sunster_heater(SunsterHeater *heater) { heater_ = heater; }
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void setup() override {
    if (heater_) {
      float v = heater_->get_power_level_percent();
      if (std::isnan(v)) v = 10.0f;
      ESP_LOGI(TAG, "[SEND_HA] PowerLevel = %.1f (setup)", v);
      this->publish_state(v);
    }
  }
  void dump_config() override {
    LOG_NUMBER("", "Sunster Heater Power Level", this);
  }

 protected:
  void loop() override {
    Component::loop();
    if (!heater_) return;
    uint32_t now = millis();
    uint32_t interval = (now < 60000u) ? 3000u : 15000u;
    if (last_publish_ == 0u || (now - last_publish_ >= interval)) {
      float v = heater_->get_power_level_percent();
      if (std::isnan(v)) v = 10.0f;
      if (last_publish_ == 0u) ESP_LOGI(TAG, "[SEND_HA] PowerLevel = %.1f (first loop)", v);
      last_publish_ = now;
      this->publish_state(v);
    }
  }
  void control(float value) override {
    if (heater_) {
      if (!heater_->is_manual_mode()) {
        ESP_LOGW("sunster_heater", "Power level only works in Manual mode");
        return;
      }
      heater_->set_power_level_percent(value);
      this->publish_state(value);
    }
  }

  SunsterHeater *heater_{nullptr};
  uint32_t last_publish_{0};
};

// Number component for PI Kp (automatic mode)
class SunsterPiKpNumber : public number::Number, public Component {
 public:
  SunsterPiKpNumber() { }
  void set_sunster_heater(SunsterHeater *heater) { heater_ = heater; }
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }
  void setup() override {
    if (heater_) {
      float v = heater_->get_pi_kp();
      if (std::isnan(v)) v = 6.0f;
      ESP_LOGI(TAG, "[SEND_HA] PI Kp = %.2f (setup)", v);
      this->publish_state(v);
    }
  }
  void dump_config() override {
    LOG_NUMBER("", "Sunster Heater PI Kp", this);
  }
 protected:
  void loop() override {
    Component::loop();
    if (!heater_) return;
    uint32_t now = millis();
    uint32_t interval = (now < 60000u) ? 3000u : 15000u;
    if (last_publish_ == 0u || (now - last_publish_ >= interval)) {
      float v = heater_->get_pi_kp();
      if (std::isnan(v)) v = 6.0f;
      if (last_publish_ == 0u) ESP_LOGI(TAG, "[SEND_HA] PI Kp = %.2f (first loop)", v);
      last_publish_ = now;
      this->publish_state(v);
    }
  }
  void control(float value) override {
    if (heater_) {
      heater_->set_pi_kp(value);
      heater_->save_config_preferences();
      this->publish_state(value);
    }
  }
  SunsterHeater *heater_{nullptr};
  uint32_t last_publish_{0};
};

// Number component for PI Ki (automatic mode)
class SunsterPiKiNumber : public number::Number, public Component {
 public:
  void set_sunster_heater(SunsterHeater *heater) { heater_ = heater; }
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }
  void setup() override {
    if (heater_) {
      float v = heater_->get_pi_ki();
      if (std::isnan(v)) v = 0.03f;
      ESP_LOGI(TAG, "[SEND_HA] PI Ki = %.2f (setup)", v);
      this->publish_state(v);
    }
  }
  void dump_config() override {
    LOG_NUMBER("", "Sunster Heater PI Ki", this);
  }
 protected:
  void loop() override {
    Component::loop();
    if (!heater_) return;
    uint32_t now = millis();
    uint32_t interval = (now < 60000u) ? 3000u : 15000u;
    if (last_publish_ == 0u || (now - last_publish_ >= interval)) {
      float v = heater_->get_pi_ki();
      if (std::isnan(v)) v = 0.03f;
      if (last_publish_ == 0u) ESP_LOGI(TAG, "[SEND_HA] PI Ki = %.2f (first loop)", v);
      last_publish_ = now;
      this->publish_state(v);
    }
  }
  void control(float value) override {
    if (heater_) {
      heater_->set_pi_ki(value);
      heater_->save_config_preferences();
      this->publish_state(value);
    }
  }
  SunsterHeater *heater_{nullptr};
  uint32_t last_publish_{0};
};

// Number component for PI Kd (automatic mode)
class SunsterPiKdNumber : public number::Number, public Component {
 public:
  void set_sunster_heater(SunsterHeater *heater) { heater_ = heater; }
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }
  void setup() override {
    if (heater_) {
      float v = heater_->get_pi_kd();
      if (std::isnan(v)) v = 2.0f;
      ESP_LOGI(TAG, "[SEND_HA] PI Kd = %.2f (setup)", v);
      this->publish_state(v);
    }
  }
  void dump_config() override {
    LOG_NUMBER("", "Sunster Heater PI Kd", this);
  }
 protected:
  void loop() override {
    Component::loop();
    if (!heater_) return;
    uint32_t now = millis();
    uint32_t interval = (now < 60000u) ? 3000u : 15000u;
    if (last_publish_ == 0u || (now - last_publish_ >= interval)) {
      float v = heater_->get_pi_kd();
      if (std::isnan(v)) v = 2.0f;
      if (last_publish_ == 0u) ESP_LOGI(TAG, "[SEND_HA] PI Kd = %.2f (first loop)", v);
      last_publish_ = now;
      this->publish_state(v);
    }
  }
  void control(float value) override {
    if (heater_) {
      heater_->set_pi_kd(value);
      heater_->save_config_preferences();
      this->publish_state(value);
    }
  }
  SunsterHeater *heater_{nullptr};
  uint32_t last_publish_{0};
};

// Number component for PI Off Delay (automatic mode)
class SunsterPiOffDelayNumber : public number::Number, public Component {
 public:
  void set_sunster_heater(SunsterHeater *heater) { heater_ = heater; }
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }
  void setup() override {
    if (heater_) {
      float v = heater_->get_pi_off_delay();
      if (std::isnan(v)) v = 60.0f;
      ESP_LOGI(TAG, "[SEND_HA] PI OffDelay = %.0f (setup)", v);
      this->publish_state(v);
    }
  }
  void dump_config() override {
    LOG_NUMBER("", "Sunster Heater PI Off Delay", this);
  }
 protected:
  void loop() override {
    Component::loop();
    if (!heater_) return;
    uint32_t now = millis();
    uint32_t interval = (now < 60000u) ? 3000u : 15000u;
    if (last_publish_ == 0u || (now - last_publish_ >= interval)) {
      float v = heater_->get_pi_off_delay();
      if (std::isnan(v)) v = 60.0f;
      if (last_publish_ == 0u) ESP_LOGI(TAG, "[SEND_HA] PI OffDelay = %.0f (first loop)", v);
      last_publish_ = now;
      this->publish_state(v);
    }
  }
  void control(float value) override {
    if (heater_) {
      heater_->set_pi_off_delay(value);
      heater_->save_config_preferences();
      this->publish_state(value);
    }
  }
  SunsterHeater *heater_{nullptr};
  uint32_t last_publish_{0};
};

// Number component for target temperature (automatic mode)
class SunsterTargetTemperatureNumber : public number::Number, public Component {
 public:
  void set_sunster_heater(SunsterHeater *heater) { heater_ = heater; }
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }
  void setup() override {
    if (heater_) {
      float v = heater_->get_target_temperature();
      if (std::isnan(v)) v = 20.0f;
      ESP_LOGI(TAG, "[SEND_HA] TargetTemp = %.1f (setup)", v);
      this->publish_state(v);
    }
  }
  void dump_config() override {
    LOG_NUMBER("", "Sunster Heater Target Temp", this);
  }
 protected:
  void loop() override {
    Component::loop();
    if (!heater_) return;
    uint32_t now = millis();
    uint32_t interval = (now < 60000u) ? 3000u : 15000u;
    if (last_publish_ == 0u || (now - last_publish_ >= interval)) {
      float v = heater_->get_target_temperature();
      if (std::isnan(v)) v = 20.0f;
      if (last_publish_ == 0u) ESP_LOGI(TAG, "[SEND_HA] TargetTemp = %.1f (first loop)", v);
      last_publish_ = now;
      this->publish_state(v);
    }
  }
  void control(float value) override {
    if (heater_) {
      heater_->set_target_temperature(value);
      heater_->save_config_preferences();
      this->publish_state(value);
    }
  }
  SunsterHeater *heater_{nullptr};
  uint32_t last_publish_{0};
};

// Number component for PI output min off = hysteresis lower (automatic mode)
class SunsterPiOutputMinOffNumber : public number::Number, public Component {
 public:
  void set_sunster_heater(SunsterHeater *heater) { heater_ = heater; }
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }
  void setup() override {
    if (heater_) {
      float v = heater_->get_pi_output_min_off();
      if (std::isnan(v)) v = 3.0f;
      ESP_LOGI(TAG, "[SEND_HA] PI MinOff = %.1f (setup)", v);
      this->publish_state(v);
    }
  }
  void dump_config() override {
    LOG_NUMBER("", "Sunster Heater PI Min Off", this);
  }
 protected:
  void loop() override {
    Component::loop();
    if (!heater_) return;
    uint32_t now = millis();
    uint32_t interval = (now < 60000u) ? 3000u : 15000u;
    if (last_publish_ == 0u || (now - last_publish_ >= interval)) {
      float v = heater_->get_pi_output_min_off();
      if (std::isnan(v)) v = 3.0f;
      if (last_publish_ == 0u) ESP_LOGI(TAG, "[SEND_HA] PI MinOff = %.1f (first loop)", v);
      last_publish_ = now;
      this->publish_state(v);
    }
  }
  void control(float value) override {
    if (heater_) {
      heater_->set_pi_output_min_off(value);
      heater_->save_config_preferences();
      this->publish_state(value);
    }
  }
  SunsterHeater *heater_{nullptr};
  uint32_t last_publish_{0};
};

// Number component for PI output min on = hysteresis upper (automatic mode)
class SunsterPiOutputMinOnNumber : public number::Number, public Component {
 public:
  void set_sunster_heater(SunsterHeater *heater) { heater_ = heater; }
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }
  void setup() override {
    if (heater_) {
      float v = heater_->get_pi_output_min_on();
      if (std::isnan(v)) v = 15.0f;
      ESP_LOGI(TAG, "[SEND_HA] PI MinOn = %.1f (setup)", v);
      this->publish_state(v);
    }
  }
  void dump_config() override {
    LOG_NUMBER("", "Sunster Heater PI Min On", this);
  }
 protected:
  void loop() override {
    Component::loop();
    if (!heater_) return;
    uint32_t now = millis();
    uint32_t interval = (now < 60000u) ? 3000u : 15000u;
    if (last_publish_ == 0u || (now - last_publish_ >= interval)) {
      float v = heater_->get_pi_output_min_on();
      if (std::isnan(v)) v = 15.0f;
      if (last_publish_ == 0u) ESP_LOGI(TAG, "[SEND_HA] PI MinOn = %.1f (first loop)", v);
      last_publish_ = now;
      this->publish_state(v);
    }
  }
  void control(float value) override {
    if (heater_) {
      heater_->set_pi_output_min_on(value);
      heater_->save_config_preferences();
      this->publish_state(value);
    }
  }
  SunsterHeater *heater_{nullptr};
  uint32_t last_publish_{0};
};

// Select component for control mode
class SunsterControlModeSelect : public select::Select, public Component {
 public:
  SunsterControlModeSelect() { }
  void set_sunster_heater(SunsterHeater *heater) { heater_ = heater; }
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void setup() override {
    if (heater_) {
      const char *mode = "Manual";
      if (heater_->is_automatic_mode()) mode = "Automatic";
      else if (heater_->is_antifreeze_mode()) mode = "Antifreeze";
      ESP_LOGI(TAG, "[SEND_HA] ControlMode = %s (setup)", mode);
      this->publish_state(mode);
    }
  }
  void dump_config() override {
    LOG_SELECT("", "Sunster Heater Control Mode", this);
  }

 protected:
  void publish_mode_state_() {
    if (!heater_) return;
    if (heater_->is_manual_mode()) {
      this->publish_state("Manual");
    } else if (heater_->is_automatic_mode()) {
      this->publish_state("Automatic");
    } else if (heater_->is_antifreeze_mode()) {
      this->publish_state("Antifreeze");
    } else {
      this->publish_state("Manual");
    }
  }
  void loop() override {
    Component::loop();
    if (!heater_) return;
    uint32_t now = millis();
    uint32_t interval = (now < 60000u) ? 3000u : 15000u;
    if (last_mode_publish_ == 0u || (now - last_mode_publish_ >= interval)) {
      if (last_mode_publish_ == 0u) {
        const char *mode = "Manual";
        if (heater_->is_automatic_mode()) mode = "Automatic";
        else if (heater_->is_antifreeze_mode()) mode = "Antifreeze";
        ESP_LOGI(TAG, "[SEND_HA] ControlMode = %s (first loop)", mode);
      }
      last_mode_publish_ = now;
      publish_mode_state_();
    }
  }
  void control(const std::string &value) override {
    if (heater_) {
      if (value == "Manual") {
        heater_->set_control_mode(ControlMode::MANUAL);
      } else if (value == "Automatic") {
        heater_->set_control_mode(ControlMode::AUTOMATIC);
      } else if (value == "Antifreeze") {
        heater_->set_control_mode(ControlMode::ANTIFREEZE);
      }
      this->publish_state(value);
    }
  }

  SunsterHeater *heater_{nullptr};
  uint32_t last_mode_publish_{0};
};

}  // namespace sunster_heater
}  // namespace esphome
