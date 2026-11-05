#pragma once

#include "esphome/components/button/button.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"

#include <cmath>
#include <queue>
#include <string>
#include <vector>

namespace esphome {
namespace uponor_auto {

static const size_t MAX_THERMOSTATS = 8;
static const size_t HISTORY_SIZE = 288;  // 24 hours at one sample every 5 minutes

static const uint8_t UPONOR_ID_TARGET_TEMP_MIN = 0x37;
static const uint8_t UPONOR_ID_TARGET_TEMP_MAX = 0x38;
static const uint8_t UPONOR_ID_TARGET_TEMP = 0x3B;
static const uint8_t UPONOR_ID_ECO_SETBACK = 0x3C;
static const uint8_t UPONOR_ID_DEMAND = 0x3D;
static const uint8_t UPONOR_ID_MODE1 = 0x3E;
static const uint8_t UPONOR_ID_ROOM_TEMP = 0x40;
static const uint8_t UPONOR_ID_HUMIDITY = 0x42;
static const uint8_t UPONOR_ID_REQUEST = 0xFF;

static const uint16_t UPONOR_INVALID_VALUE = 0x7FFF;

struct UponorData {
  uint8_t id;
  uint16_t value;
};

// Bus temperatures are tenths of a degree Fahrenheit.
inline float raw_to_celsius(uint16_t raw) {
  return (raw == UPONOR_INVALID_VALUE) ? NAN : (raw / 10.0f - 32.0f) / 1.8f;
}

inline uint16_t celsius_to_raw(float celsius) {
  return std::isnan(celsius) ? UPONOR_INVALID_VALUE : static_cast<uint16_t>(lroundf((celsius * 1.8f + 32.0f) * 10.0f));
}

class UponorAutoClimate;

struct Thermostat {
  uint32_t address{0};
  uint16_t replies{0};
  uint16_t replies_with_setpoint{0};
  uint16_t temperature_raw{UPONOR_INVALID_VALUE};
  uint16_t setpoint_raw{UPONOR_INVALID_VALUE};
};

struct SavedDetection {
  uint32_t main_address;
  uint32_t addresses[MAX_THERMOSTATS];
  uint8_t count;
};

// Temperatures are in tenths of a degree Celsius.
struct HistorySample {
  int16_t temperature;
  int16_t target;
  bool heating;
};

enum class DetectionState : uint8_t { DETECTING, READY, READ_ONLY };

class UponorAuto : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_climate(UponorAutoClimate *climate) { this->climate_ = climate; }
  void set_status(text_sensor::TextSensor *status) { this->status_ = status; }
  void set_main_address(text_sensor::TextSensor *main_address) { this->main_address_text_ = main_address; }
  void set_thermostats(text_sensor::TextSensor *thermostats) { this->thermostats_text_ = thermostats; }
  void set_room(size_t index, sensor::Sensor *temperature, sensor::Sensor *setpoint) {
    this->room_temperature_[index] = temperature;
    this->room_setpoint_[index] = setpoint;
  }

  bool has_main() const { return this->main_address_ != 0; }
  void send_setpoint(uint16_t raw);
  void rescan();
  std::string history_json() const;

 protected:
  void parse_byte_(uint8_t byte);
  void handle_packet_(const uint8_t *packet, size_t packet_len);
  Thermostat *find_or_add_(uint32_t address);
  void finish_detection_(uint32_t now);
  void save_detection_();
  void publish_detection_();
  void update_status_(uint32_t now);
  void record_history_(uint32_t now);

  UponorAutoClimate *climate_{nullptr};
  text_sensor::TextSensor *status_{nullptr};
  text_sensor::TextSensor *main_address_text_{nullptr};
  text_sensor::TextSensor *thermostats_text_{nullptr};
  sensor::Sensor *room_temperature_[MAX_THERMOSTATS]{};
  sensor::Sensor *room_setpoint_[MAX_THERMOSTATS]{};

  ESPPreferenceObject pref_;
  DetectionState state_{DetectionState::DETECTING};
  std::vector<Thermostat> thermostats_;
  uint32_t main_address_{0};

  std::vector<uint8_t> rx_buffer_;
  std::queue<std::vector<uint8_t>> tx_queue_;
  uint32_t last_rx_{0};
  uint32_t last_tx_{0};
  uint32_t first_packet_{0};
  uint32_t last_packet_{0};

  HistorySample history_[HISTORY_SIZE]{};
  size_t history_count_{0};
  size_t history_next_{0};
  uint32_t last_sample_{0};
};

class UponorAutoHistoryHandler : public AsyncWebHandler {
 public:
  explicit UponorAutoHistoryHandler(UponorAuto *hub) : hub_(hub) {}

  bool canHandle(AsyncWebServerRequest *request) const override {
    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->url_to(url) == "/history";
  }
  void handleRequest(AsyncWebServerRequest *request) override {
    request->send(200, "application/json", this->hub_->history_json().c_str());
  }

 protected:
  UponorAuto *hub_;
};

class UponorAutoClimate : public climate::Climate, public Component {
 public:
  void loop() override;
  void dump_config() override;

  void set_hub(UponorAuto *hub) { this->hub_ = hub; }
  void on_data(const UponorData *data, size_t data_len);

 protected:
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  UponorAuto *hub_{nullptr};
  uint32_t last_data_{0};
  float min_temperature_{5.0f};
  float max_temperature_{35.0f};
  uint16_t eco_setback_value_raw_{0x0048};
  uint16_t target_temperature_raw_{0};
};

class UponorAutoRescanButton : public button::Button, public Parented<UponorAuto> {
 protected:
  void press_action() override { this->parent_->rescan(); }
};

}  // namespace uponor_auto
}  // namespace esphome
