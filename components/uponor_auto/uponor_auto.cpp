#include "uponor_auto.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>

namespace esphome {
namespace uponor_auto {

static const char *const TAG = "uponor_auto";

static const size_t MIN_PACKET_SIZE = 7;
static const size_t MAX_PACKET_SIZE = 55;
static const uint32_t RX_GAP_MS = 50;
static const uint32_t TX_WINDOW_END_MS = 100;
static const uint32_t TX_SPACING_MS = 200;
static const uint32_t DETECTION_WINDOW_MS = 120000;
static const uint32_t BUS_TIMEOUT_MS = 60000;
static const uint16_t MIN_REPLIES = 8;
static const uint32_t HISTORY_INTERVAL_MS = 300000;

void UponorAuto::setup() {
  this->pref_ = global_preferences->make_preference<SavedDetection>(fnv1_hash("uponor_auto_detection"));

  SavedDetection saved{};
  if (this->pref_.load(&saved) && saved.count > 0 && saved.count <= MAX_THERMOSTATS) {
    for (size_t i = 0; i < saved.count; i++) {
      Thermostat thermostat;
      thermostat.address = saved.addresses[i];
      this->thermostats_.push_back(thermostat);
    }
    this->main_address_ = saved.main_address;
    this->state_ = this->has_main() ? DetectionState::READY : DetectionState::READ_ONLY;
    ESP_LOGI(TAG, "Restored %u thermostats, main 0x%08X", saved.count, saved.main_address);
  }
  this->publish_detection_();
  web_server_base::global_web_server_base->add_handler(new UponorAutoHistoryHandler(this));
}

void UponorAuto::dump_config() {
  ESP_LOGCONFIG(TAG, "Uponor Auto");
  ESP_LOGCONFIG(TAG, "  Main thermostat: 0x%08X", this->main_address_);
  for (const auto &thermostat : this->thermostats_) {
    ESP_LOGCONFIG(TAG, "  - 0x%08X", thermostat.address);
  }
  this->check_uart_settings(19200);
}

void UponorAuto::loop() {
  const uint32_t now = App.get_loop_component_start_time();

  if (!this->rx_buffer_.empty() && (now - this->last_rx_ > RX_GAP_MS)) {
    this->rx_buffer_.clear();
  }

  while (this->available()) {
    uint8_t byte;
    this->last_rx_ = now;
    this->read_byte(&byte);
    this->parse_byte_(byte);
  }

  // Only transmit in the silence the controller leaves after a polling burst.
  if (!this->tx_queue_.empty() && this->rx_buffer_.empty() && (now - this->last_rx_ > RX_GAP_MS) &&
      (now - this->last_rx_ < TX_WINDOW_END_MS) && (now - this->last_tx_ > TX_SPACING_MS)) {
    this->write_array(this->tx_queue_.front());
    this->flush();
    this->tx_queue_.pop();
    this->last_tx_ = now;
  }

  if (this->state_ == DetectionState::DETECTING && this->first_packet_ != 0 &&
      (now - this->first_packet_ > DETECTION_WINDOW_MS)) {
    this->finish_detection_(now);
  }

  this->update_status_(now);
  this->record_history_(now);
}

// Packets carry no length or delimiter, so try every valid length until a CRC matches.
void UponorAuto::parse_byte_(uint8_t byte) {
  this->rx_buffer_.push_back(byte);

  while (this->rx_buffer_.size() >= MIN_PACKET_SIZE) {
    const uint8_t *packet = this->rx_buffer_.data();
    const size_t available = this->rx_buffer_.size();
    const size_t limit = std::min(available, MAX_PACKET_SIZE);

    size_t matched = 0;
    for (size_t len = MIN_PACKET_SIZE; len <= limit; len++) {
      if (len != MIN_PACKET_SIZE && (len - 6) % 3 != 0)
        continue;
      uint16_t crc = encode_uint16(packet[len - 1], packet[len - 2]);
      if (crc16(packet, len - 2) == crc) {
        matched = len;
        break;
      }
    }

    if (matched != 0) {
      this->handle_packet_(packet, matched);
      this->rx_buffer_.erase(this->rx_buffer_.begin(), this->rx_buffer_.begin() + matched);
    } else if (available > MAX_PACKET_SIZE) {
      this->rx_buffer_.erase(this->rx_buffer_.begin());
    } else {
      return;
    }
  }
}

Thermostat *UponorAuto::find_or_add_(uint32_t address) {
  for (auto &thermostat : this->thermostats_) {
    if (thermostat.address == address)
      return &thermostat;
  }
  if (this->state_ != DetectionState::DETECTING)
    return nullptr;
  if (this->thermostats_.size() >= MAX_THERMOSTATS) {
    ESP_LOGW(TAG, "Ignoring 0x%08X: more than %u thermostats", address, MAX_THERMOSTATS);
    return nullptr;
  }
  Thermostat thermostat;
  thermostat.address = address;
  this->thermostats_.push_back(thermostat);
  ESP_LOGI(TAG, "Found thermostat 0x%08X", address);
  return &this->thermostats_.back();
}

void UponorAuto::handle_packet_(const uint8_t *packet, size_t packet_len) {
  const uint32_t now = App.get_loop_component_start_time();
  this->last_packet_ = now;
  if (this->first_packet_ == 0)
    this->first_packet_ = now;

  uint32_t address = encode_uint32(packet[0], packet[1], packet[2], packet[3]);
  Thermostat *thermostat = this->find_or_add_(address);
  if (thermostat == nullptr)
    return;

  size_t payload_len = packet_len - 6;
  if (payload_len == 1 && packet[4] == UPONOR_ID_REQUEST)
    return;
  if (payload_len == 0 || payload_len % 3 != 0)
    return;

  size_t count = payload_len / 3;
  std::vector<UponorData> data(count);
  bool has_room_temp = false;
  bool has_setpoint = false;
  for (size_t i = 0; i < count; i++) {
    data[i].id = packet[4 + i * 3];
    data[i].value = encode_uint16(packet[5 + i * 3], packet[6 + i * 3]);
    has_room_temp |= data[i].id == UPONOR_ID_ROOM_TEMP;
    has_setpoint |= data[i].id == UPONOR_ID_TARGET_TEMP;
  }

  // Only a thermostat's own reply carries the room temperature; the controller's update always has a setpoint.
  if (this->state_ == DetectionState::DETECTING) {
    if (has_room_temp) {
      thermostat->replies++;
      if (has_setpoint)
        thermostat->replies_with_setpoint++;
    }
    return;
  }

  size_t index = thermostat - this->thermostats_.data();
  for (const auto &record : data) {
    if (record.id == UPONOR_ID_ROOM_TEMP && record.value != thermostat->temperature_raw) {
      thermostat->temperature_raw = record.value;
      this->room_temperature_[index]->publish_state(raw_to_celsius(record.value));
    } else if (record.id == UPONOR_ID_TARGET_TEMP && record.value != 0 && record.value != UPONOR_INVALID_VALUE &&
               record.value != thermostat->setpoint_raw) {
      thermostat->setpoint_raw = record.value;
      this->room_setpoint_[index]->publish_state(raw_to_celsius(record.value));
    }
  }

  if (address == this->main_address_)
    this->climate_->on_data(data.data(), count);
}

// The main thermostat stays quiet about its setpoint until it changes; dial thermostats repeat it on every poll.
void UponorAuto::finish_detection_(uint32_t now) {
  std::vector<Thermostat> found;
  for (const auto &thermostat : this->thermostats_) {
    if (thermostat.replies >= MIN_REPLIES)
      found.push_back(thermostat);
  }
  if (found.empty()) {
    this->first_packet_ = now;
    return;
  }

  std::sort(found.begin(), found.end(), [](const Thermostat &a, const Thermostat &b) { return a.address < b.address; });

  uint32_t candidate = 0;
  size_t candidates = 0;
  for (const auto &thermostat : found) {
    ESP_LOGI(TAG, "0x%08X: %u replies, %u with setpoint", thermostat.address, thermostat.replies,
             thermostat.replies_with_setpoint);
    if (thermostat.replies_with_setpoint * 2 < thermostat.replies) {
      candidate = thermostat.address;
      candidates++;
    }
  }

  this->thermostats_ = found;
  this->main_address_ = (candidates == 1) ? candidate : 0;
  this->state_ = this->has_main() ? DetectionState::READY : DetectionState::READ_ONLY;
  if (this->has_main()) {
    ESP_LOGI(TAG, "Main thermostat is 0x%08X", this->main_address_);
  } else {
    ESP_LOGW(TAG, "%u candidates for main thermostat, staying read-only", candidates);
  }

  this->save_detection_();
  this->publish_detection_();
}

void UponorAuto::save_detection_() {
  SavedDetection saved{};
  saved.main_address = this->main_address_;
  saved.count = this->state_ == DetectionState::DETECTING ? 0 : this->thermostats_.size();
  for (size_t i = 0; i < saved.count; i++) {
    saved.addresses[i] = this->thermostats_[i].address;
  }
  this->pref_.save(&saved);
  global_preferences->sync();
}

void UponorAuto::publish_detection_() {
  char buffer[16];
  std::string list;
  if (this->state_ != DetectionState::DETECTING) {
    for (const auto &thermostat : this->thermostats_) {
      snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<unsigned>(thermostat.address));
      if (!list.empty())
        list += ", ";
      list += buffer;
    }
  }
  this->thermostats_text_->publish_state(list.empty() ? "None yet" : list);

  if (this->has_main()) {
    snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<unsigned>(this->main_address_));
    this->main_address_text_->publish_state(buffer);
  } else {
    this->main_address_text_->publish_state("None");
  }
}

void UponorAuto::update_status_(uint32_t now) {
  const char *status;
  if (this->last_packet_ == 0 || (now - this->last_packet_ > BUS_TIMEOUT_MS)) {
    status = "No bus data: check wiring";
  } else if (this->state_ == DetectionState::DETECTING) {
    status = "Detecting thermostats";
  } else if (this->state_ == DetectionState::READY) {
    status = "Ready";
  } else {
    status = "No single main thermostat: read-only";
  }
  if (this->status_->state != status)
    this->status_->publish_state(status);
}

void UponorAuto::record_history_(uint32_t now) {
  if (!this->has_main() || (this->history_count_ != 0 && now - this->last_sample_ < HISTORY_INTERVAL_MS))
    return;
  if (std::isnan(this->climate_->current_temperature) || std::isnan(this->climate_->target_temperature))
    return;

  HistorySample &sample = this->history_[this->history_next_];
  sample.temperature = lroundf(this->climate_->current_temperature * 10.0f);
  sample.target = lroundf(this->climate_->target_temperature * 10.0f);
  sample.heating = this->climate_->action == climate::CLIMATE_ACTION_HEATING;
  this->history_next_ = (this->history_next_ + 1) % HISTORY_SIZE;
  this->history_count_ = std::min(this->history_count_ + 1, HISTORY_SIZE);
  this->last_sample_ = now;
}

// Oldest first. The stick has no clock, so "age" lets the page place the samples using its own.
std::string UponorAuto::history_json() const {
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "{\"interval\":%u,\"age\":%u,\"samples\":[",
           static_cast<unsigned>(HISTORY_INTERVAL_MS / 1000), static_cast<unsigned>((millis() - this->last_sample_) / 1000));
  std::string json = buffer;
  json.reserve(json.size() + this->history_count_ * 14 + 2);

  const size_t start = (this->history_next_ + HISTORY_SIZE - this->history_count_) % HISTORY_SIZE;
  for (size_t i = 0; i < this->history_count_; i++) {
    const HistorySample &sample = this->history_[(start + i) % HISTORY_SIZE];
    snprintf(buffer, sizeof(buffer), "%s[%d,%d,%d]", i == 0 ? "" : ",", sample.temperature, sample.target,
             sample.heating ? 1 : 0);
    json += buffer;
  }
  json += "]}";
  return json;
}

void UponorAuto::send_setpoint(uint16_t raw) {
  if (!this->has_main())
    return;

  // Thermostats ignore a new setpoint unless a null setpoint comes first.
  std::vector<uint8_t> packet = {
      static_cast<uint8_t>(this->main_address_ >> 24), static_cast<uint8_t>(this->main_address_ >> 16),
      static_cast<uint8_t>(this->main_address_ >> 8),  static_cast<uint8_t>(this->main_address_ >> 0),
      UPONOR_ID_TARGET_TEMP,                           0x00,
      0x00,                                            UPONOR_ID_TARGET_TEMP,
      static_cast<uint8_t>(raw >> 8),                  static_cast<uint8_t>(raw >> 0),
  };
  auto crc = crc16(packet.data(), packet.size());
  packet.push_back(crc >> 0);
  packet.push_back(crc >> 8);
  this->tx_queue_.push(packet);
}

void UponorAuto::rescan() {
  ESP_LOGI(TAG, "Re-running detection");
  for (size_t i = 0; i < MAX_THERMOSTATS; i++) {
    this->room_temperature_[i]->publish_state(NAN);
    this->room_setpoint_[i]->publish_state(NAN);
  }
  this->thermostats_.clear();
  this->main_address_ = 0;
  this->first_packet_ = 0;
  this->history_count_ = 0;
  this->state_ = DetectionState::DETECTING;
  this->save_detection_();
  this->publish_detection_();
}

void UponorAutoClimate::dump_config() {
  LOG_CLIMATE("", "Uponor Auto Climate", this);
}

void UponorAutoClimate::loop() {
  const uint32_t now = App.get_loop_component_start_time();

  // Publish once all packets of a polling cycle are in.
  if (this->last_data_ != 0 && (now - this->last_data_ > 100) && this->target_temperature_raw_ != 0) {
    float temp = raw_to_celsius((this->preset == climate::CLIMATE_PRESET_ECO)
                                    ? (this->target_temperature_raw_ - this->eco_setback_value_raw_)
                                    : this->target_temperature_raw_);
    float step = this->get_traits().get_visual_target_temperature_step();
    this->target_temperature = roundf(temp / step) * step;
    this->publish_state();
    this->last_data_ = 0;
  }
}

climate::ClimateTraits UponorAutoClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_CURRENT_HUMIDITY |
                           climate::CLIMATE_SUPPORTS_ACTION);
  traits.set_supported_modes({climate::CLIMATE_MODE_HEAT});
  traits.set_supported_presets({climate::CLIMATE_PRESET_ECO});
  traits.set_visual_min_temperature(this->min_temperature_);
  traits.set_visual_max_temperature(this->max_temperature_);
  traits.set_visual_current_temperature_step(0.1f);
  traits.set_visual_target_temperature_step(0.5f);
  return traits;
}

void UponorAutoClimate::control(const climate::ClimateCall &call) {
  if (!this->hub_->has_main()) {
    ESP_LOGW(TAG, "No main thermostat detected, ignoring control call");
    return;
  }
  if (call.get_target_temperature().has_value()) {
    uint16_t temp = celsius_to_raw(*call.get_target_temperature());
    // In ECO mode the thermostat subtracts the setback itself, so add it first.
    if (this->preset == climate::CLIMATE_PRESET_ECO)
      temp += this->eco_setback_value_raw_;
    this->hub_->send_setpoint(temp);
  }
}

void UponorAutoClimate::on_data(const UponorData *data, size_t data_len) {
  for (size_t i = 0; i < data_len; i++) {
    const uint16_t value = data[i].value;
    switch (data[i].id) {
      case UPONOR_ID_TARGET_TEMP_MIN:
        this->min_temperature_ = raw_to_celsius(value);
        break;
      case UPONOR_ID_TARGET_TEMP_MAX:
        this->max_temperature_ = raw_to_celsius(value);
        break;
      case UPONOR_ID_TARGET_TEMP:
        if (value != UPONOR_INVALID_VALUE && value != 0)
          this->target_temperature_raw_ = value;
        break;
      case UPONOR_ID_ECO_SETBACK:
        this->eco_setback_value_raw_ = value;
        break;
      case UPONOR_ID_DEMAND:
        if (value & 0x1000) {
          this->mode = climate::CLIMATE_MODE_COOL;
          this->action = (value & 0x0040) ? climate::CLIMATE_ACTION_COOLING : climate::CLIMATE_ACTION_IDLE;
        } else {
          this->mode = climate::CLIMATE_MODE_HEAT;
          this->action = (value & 0x0040) ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE;
        }
        break;
      case UPONOR_ID_MODE1:
        this->set_preset_((value & 0x0008) ? climate::CLIMATE_PRESET_ECO : climate::CLIMATE_PRESET_NONE);
        break;
      case UPONOR_ID_ROOM_TEMP:
        this->current_temperature = raw_to_celsius(value);
        break;
      case UPONOR_ID_HUMIDITY:
        this->current_humidity = value & 0x00FF;
        break;
      default:
        break;
    }
  }
  this->last_data_ = millis();
}

}  // namespace uponor_auto
}  // namespace esphome
