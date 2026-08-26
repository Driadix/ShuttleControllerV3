#pragma once
#include "v3/observability/types.h"
#include "v3/observability/queue.h"
#include <atomic>
#include <array>

namespace v3::observability {

class EventPublisher {
public:
    virtual void publishEvent(uint16_t eventId, uint8_t severity, const void* context) = 0;
};

class ObservabilityProducer {
public:
    struct CounterRegistry {
        std::atomic<uint16_t> dropCount{0};
        std::atomic<uint16_t> highWater{0};
    };

    ObservabilityProducer(EventPublisher& publisher) : publisher_(publisher) {}

    void notifyDrop(uint8_t classId) {
        if (classId < 4) {
            counters[classId].dropCount.fetch_add(1, std::memory_order_relaxed);
        }
        publisher_.publishEvent(0x0500 + classId, 1, nullptr); 
    }

    // Вспомогательный метод для трекинга highWater
    void updateHighWater(uint8_t classId, size_t size) {
        auto& reg = counters[classId];
        if (size > reg.highWater.load(std::memory_order_relaxed)) {
            reg.highWater.store(static_cast<uint16_t>(size), std::memory_order_relaxed);
        }
    }

    void enqueueTelemetry(const TelemetryRecord& record) {
        if (telemetryQueue.enqueue(record) != EnqueueStatus::Success) notifyDrop(0);
        else updateHighWater(0, telemetryQueue.size());
    }
    
    void enqueueEvent(const EventRecord& record) {
        if (eventQueue.enqueue(record) != EnqueueStatus::Success) notifyDrop(1);
        else updateHighWater(1, eventQueue.size());
    }
    
    void enqueueLog(const LogRecord& record) {
        if (logQueue.enqueue(record) != EnqueueStatus::Success) notifyDrop(2);
        else updateHighWater(2, logQueue.size());
    }

    void enqueueTrace(const TraceRecord& record) {
        if (traceQueue.enqueue(record) != EnqueueStatus::Success) notifyDrop(3);
        else updateHighWater(3, traceQueue.size());
    }

    BoundedQueue<TelemetryRecord, 8, DropPolicy::Oldest>& getTelemetryQueue() { return telemetryQueue; }
    BoundedQueue<EventRecord, 32, DropPolicy::Newest>& getEventQueue() { return eventQueue; }
    BoundedQueue<LogRecord, 32, DropPolicy::Newest>& getLogQueue() { return logQueue; }
    BoundedQueue<TraceRecord, 16, DropPolicy::Oldest>& getTraceQueue() { return traceQueue; }

private:
    EventPublisher& publisher_;
    BoundedQueue<TelemetryRecord, 8, DropPolicy::Oldest> telemetryQueue;
    BoundedQueue<EventRecord, 32, DropPolicy::Newest> eventQueue;
    BoundedQueue<LogRecord, 32, DropPolicy::Newest> logQueue;
    BoundedQueue<TraceRecord, 16, DropPolicy::Oldest> traceQueue;
    std::array<CounterRegistry, 4> counters;
};

} // namespace v3::observability
