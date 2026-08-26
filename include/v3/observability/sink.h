#pragma once
// NOTE: Parallel implementation to domain/observability.{h,cpp}. Not wired into PIO build.
// Divergences from docs/observability-design-v3.md (#72):
//   - T7: per-class caps + 230B total budget; this uses flat 128B/tick across all classes
//   - T8: no Control/Service priority classes (only Telemetry/Events/Logs/Traces)
//   - Wire format: raw struct reinterpret_cast vs envelope-encoded 10/14B layouts
#include "v3/observability/producer.h"
#include <cstdint>
#include <cstring>
#include <algorithm>
namespace v3::observability {

class Transport {
public:
    virtual bool send(const uint8_t* data, size_t size) = 0;
};

class UARTSink {
public:
    UARTSink(ObservabilityProducer& producer, Transport& transport) 
        : producer_(producer), transport_(transport) {}

    void processTick() {
        size_t budget = 128; // MTU 128 Б

        // Приоритеты: 1. Events, 2. Logs, 3. Traces, 4. Telemetry.
        // Цикл drain: пока бюджет тика не исчерпан, классы опрашиваются
        // по убыванию приоритета (defer-on-backpressure, #49 §10: кадр, не
        // поместившийся в бюджет/кап, остаётся в очереди - drop только на
        // enqueue). Выход: бюджет < минимального фрейма или все очереди
        // пусты за проход.
        for (;;) {
            bool progressed = false;
            if (budget >= sizeof(EventRecord)) {
                progressed |= drainQueue(producer_.getEventQueue(), budget, sizeof(EventRecord));
            }
            if (budget >= sizeof(LogRecord)) {
                progressed |= drainQueue(producer_.getLogQueue(), budget, sizeof(LogRecord));
            }
            if (budget >= 3) { // минимум: заголовок фрагмента + 1 Б payload
                progressed |= drainTraces(budget);
            }
            if (budget >= sizeof(TelemetryRecord)) {
                progressed |= drainQueue(producer_.getTelemetryQueue(), budget, sizeof(TelemetryRecord));
            }
            if (!progressed) break;
        }
    }

private:
    template<typename T, size_t N, DropPolicy P>
    bool drainQueue(BoundedQueue<T, N, P>& queue, size_t& budget, size_t recordSize) {
        bool sent = false;
        T record;
        // Defer-on-backpressure (#49 §10): peek -> send -> pop. Кадр покидает
        // очередь только после успешной отправки; занятый транспорт
        // останавливает drain без потери записи.
        while (budget >= recordSize && queue.peek(record)) {
            if (transport_.send(reinterpret_cast<const uint8_t*>(&record), recordSize)) {
                queue.pop();
                budget -= recordSize;
                sent = true;
            } else {
                break;
            }
        }
        return sent;
    }

    bool drainTraces(size_t& budget) {
        const size_t MTU = 128;
        const size_t HeaderSize = 2;
        const size_t FragmentPayloadSize = MTU - HeaderSize;
        bool sent = false;

        if (!partialTrace.active) {
            if (!producer_.getTraceQueue().dequeue(partialTrace.record)) return false;
            partialTrace.offset = 0;
            partialTrace.active = true;
        }

        const uint8_t* data = reinterpret_cast<const uint8_t*>(&partialTrace.record);
        size_t totalSize = sizeof(TraceRecord);
        size_t numFragments = (totalSize + FragmentPayloadSize - 1) / FragmentPayloadSize;

        while (budget >= HeaderSize + 1 && partialTrace.offset < totalSize) {
            size_t chunkSize = std::min(FragmentPayloadSize, totalSize - partialTrace.offset);

            if (budget < chunkSize + HeaderSize) break;

            uint8_t buffer[MTU];
            buffer[0] = static_cast<uint8_t>(partialTrace.offset / FragmentPayloadSize);
            buffer[1] = static_cast<uint8_t>(numFragments);
            std::memcpy(buffer + HeaderSize, data + partialTrace.offset, chunkSize);

            if (transport_.send(buffer, chunkSize + HeaderSize)) {
                budget -= (chunkSize + HeaderSize);
                partialTrace.offset += chunkSize;
                sent = true;
            } else {
                break; // Транспорт занят
            }
        }

        if (partialTrace.offset >= totalSize) {
            partialTrace.active = false;
        }
        return sent;
    }

    struct PartialTraceState {
        TraceRecord record;
        size_t offset;
        bool active = false;
    } partialTrace;

    ObservabilityProducer& producer_;
    Transport& transport_;
};

} // namespace v3::observability
