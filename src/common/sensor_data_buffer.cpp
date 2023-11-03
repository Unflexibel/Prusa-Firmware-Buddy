#include "sensor_data_buffer.hpp"

static freertos::Mutex mutex;
static SensorData::SensorDataBuffer *buffer = nullptr;

void info_screen_handler(metric_point_t *point) {
    SensorData::HandleNewData(point);
}

using namespace SensorData;

SensorDataBuffer::SensorDataBuffer() {
    RegisterBuffer(this);
    enableMetrics();
}

SensorDataBuffer::~SensorDataBuffer() {
    UnregisterBuffer();
    disableMetrics();
}

metric_handler_t *SensorDataBuffer::getHandler() {

    for (metric_handler_t **handlers = metric_get_handlers(); *handlers != nullptr; handlers++) {
        metric_handler_t *handler = *handlers;
        if (strcmp(handler->name, "SENSOR_INFO_SCREEN") == 0) {
            return handler;
        }
    }
    return nullptr;
}

#if BOARD_IS_XLBUDDY
static constexpr Sensor first_sensor_to_log = Sensor::bedTemp;
#else
static constexpr Sensor first_sensor_to_log = Sensor::printFan;
#endif
bool SensorDataBuffer::enableMetrics() {

    if (allMetricsEnabled)
        return true;
    size_t count = 0;
    metric_handler_t *handler = getHandler();
    if (!handler) {
        return false;
    }
    // step through all metrics and enable the handler for metrics which we want to display
    for (auto metric = metric_get_iterator_begin(), e = metric_get_iterator_end(); metric != e; metric++) {
        auto it = std::lower_bound(sensors.begin(), sensors.end(), pair { metric->name, first_sensor_to_log }, compareFN {});
        if (it != sensors.end() && strcmp(metric->name, it->first) == 0) {
            count++;
            metric->enabled_handlers |= (1 << handler->identifier);
            sensorValues[static_cast<size_t>(it->second)].attribute.enabled = true;
        }
        // check if we enabled handler for all metrics, if yes return true
        if (count == sensors.size()) {
            allMetricsEnabled = true;
            return true;
        }
    }
    return false;
}

void SensorDataBuffer::disableMetrics() {
    metric_handler_t *handler = getHandler();
    if (!handler) {
        return;
    }
    // step through all metrics and disable the handler for metrics which we want to display
    for (auto metric = metric_get_iterator_begin(), e = metric_get_iterator_end(); metric != e; metric++) {
        auto it = std::lower_bound(sensors.begin(), sensors.end(), pair { metric->name, first_sensor_to_log }, compareFN {});
        if (it != sensors.end() && strcmp(metric->name, it->first) == 0) {
            metric->enabled_handlers &= ~(1 << handler->identifier);
            sensorValues[static_cast<size_t>(it->second)].attribute.enabled = false;
            sensorValues[static_cast<size_t>(it->second)].attribute.valid = false;
        }
    }
    allMetricsEnabled = false;
}

Value SensorDataBuffer::GetValue(Sensor type) {
    enableMetrics();
    std::unique_lock lock { mutex };
    return sensorValues[static_cast<size_t>(type)];
}

void SensorDataBuffer::HandleNewData(metric_point_t *point) {
    // check if metric value is int or float, because we want only these
    if (point->metric->type == METRIC_VALUE_FLOAT || point->metric->type == METRIC_VALUE_INTEGER) {
        auto it = std::lower_bound(sensors.begin(), sensors.end(), pair { point->metric->name, first_sensor_to_log }, compareFN {});
        if (it != sensors.end() && strcmp(it->first, point->metric->name) == 0) {
            std::unique_lock lock { mutex };
            sensorValues[static_cast<uint16_t>(it->second)].float_val = point->value_float; // This can be int, but both are union of float/int
            sensorValues[static_cast<uint16_t>(it->second)].attribute.valid = true;
            sensorValues[static_cast<uint16_t>(it->second)].attribute.type = point->metric->type == METRIC_VALUE_FLOAT ? Type::floatType : Type::intType;
        }
    }
}

void SensorData::RegisterBuffer(SensorDataBuffer *buff) {
    assert(buffer == nullptr); // some other SensorData is already registered
    std::unique_lock lock { mutex };
    buffer = buff;
}

void SensorData::UnregisterBuffer() {
    std::unique_lock lock { mutex };
    buffer = nullptr;
}

void SensorData::HandleNewData(metric_point_t *point) {
    std::unique_lock lock { mutex };
    if (buffer != nullptr) {
        buffer->HandleNewData(point);
    }
}
