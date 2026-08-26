// UARTSink drain-loop tests (ticket #72 follow-up: приоритетный цикл drain
// в UARTSink::processTick): порядок классов Events -> Logs -> Traces ->
// Telemetry, бюджет тика 128 Б, фрагментация Traces между тиками,
// defer-on-backpressure при занятом транспорте.
//
// NOTE: This is a new parallel implementation (not wired into PIO build yet).
// Divergences from docs/observability-design-v3.md:
//   - T7 spec: per-class caps + 230B total budget; this impl uses flat 128B/tick
//   - T8 spec: no Control/Service priority class (only Telemetry/Events/Logs/Traces)
//   - Wire format: simplified reinterpret_cast of raw structs vs envelope-encoded 10/14B layouts
#include "v3/observability/producer.h"
#include "v3/observability/sink.h"
#include <gtest/gtest.h>

using namespace v3::observability;

namespace {

class MockPublisher : public EventPublisher
{
  public:
    void publishEvent(std::uint16_t eventId, std::uint8_t /*severity*/,
                      const void* /*context*/) override
    {
        lastEventId = eventId;
    }
    std::uint16_t lastEventId = 0;
};

class MockTransport : public Transport
{
  public:
    bool send(const std::uint8_t* /*data*/, std::size_t size) override
    {
        if (failNext)
        {
            return false;
        }
        sentSize += size;
        ++sentCount;
        frameSizes.push_back(size);
        return true;
    }
    int sentCount = 0;
    std::size_t sentSize = 0;
    bool failNext = false;
    std::vector<std::size_t> frameSizes;
};

} // namespace

TEST(SinkDrop, OverflowNotifiesPublisherWithClassEvent)
{
    MockPublisher pub;
    ObservabilityProducer prod(pub);
    for (int i = 0; i < 40; ++i)
    {
        prod.enqueueEvent(EventRecord{});
    }
    EXPECT_EQ(pub.lastEventId, static_cast<std::uint16_t>(0x0501));
}

TEST(SinkDrain, PriorityOrderAndTickBudget)
{
    MockPublisher pub;
    MockTransport trans;
    ObservabilityProducer prod(pub);
    UARTSink sink(prod, trans);

    prod.enqueueTelemetry(TelemetryRecord{});
    prod.enqueueEvent(EventRecord{});

    sink.processTick();

    ASSERT_EQ(trans.sentCount, 2);
    EXPECT_LE(trans.sentSize, static_cast<std::size_t>(128));
    // Приоритет: Event уходит раньше Telemetry в пределах одного прохода.
    ASSERT_EQ(trans.frameSizes.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(trans.frameSizes[0], sizeof(EventRecord));
    EXPECT_EQ(trans.frameSizes[1], sizeof(TelemetryRecord));
}

TEST(SinkDrain, TraceFragmentationAcrossTicks)
{
    MockPublisher pub;
    MockTransport trans;
    ObservabilityProducer prod(pub);
    UARTSink sink(prod, trans);

    prod.enqueueTrace(TraceRecord{});

    sink.processTick();
    EXPECT_LE(trans.sentSize, static_cast<std::size_t>(128));
    const std::size_t afterTick1 = trans.sentSize;

    sink.processTick();
    EXPECT_GT(trans.sentSize, afterTick1);

    // Хвост записи доезжает не позже третьего тика; дальше транспорт молчит.
    sink.processTick();
    sink.processTick();
    const std::size_t fragments =
        (sizeof(TraceRecord) + (128 - 2) - 1) / (128 - 2);
    std::size_t expected = 0;
    for (std::size_t i = 0; i < fragments; ++i)
    {
        const std::size_t payload =
            std::min(static_cast<std::size_t>(128 - 2),
                     sizeof(TraceRecord) - i * (128 - 2));
        expected += payload + 2;
    }
    EXPECT_EQ(trans.sentSize, expected);
    // Очередь пуста, повторный тик ничего не добавляет.
    sink.processTick();
    EXPECT_EQ(trans.sentSize, expected);
}

TEST(SinkDrain, BusyTransportDefersRecordToNextTick)
{
    MockPublisher pub;
    MockTransport trans;
    ObservabilityProducer prod(pub);
    UARTSink sink(prod, trans);

    prod.enqueueEvent(EventRecord{});
    prod.enqueueEvent(EventRecord{});

    trans.failNext = true;
    sink.processTick();
    EXPECT_EQ(trans.sentCount, 0);
    // Кадр остался в очереди (defer-on-backpressure), drop на drain запрещён.
    EXPECT_EQ(prod.getEventQueue().size(), static_cast<std::size_t>(2));

    trans.failNext = false;
    prod.enqueueEvent(EventRecord{});
    sink.processTick();
    // 128 Б хватает на два события по 44 Б; третий (142 Б суммарно не влезает)
    // откладывается на следующий тик - бюджет исчерпан, очередь не пуста.
    EXPECT_EQ(trans.sentCount, 2);
    EXPECT_EQ(prod.getEventQueue().size(), static_cast<std::size_t>(1));

    sink.processTick();
    EXPECT_EQ(trans.sentCount, 3);
    EXPECT_EQ(prod.getEventQueue().size(), static_cast<std::size_t>(0));

    // Тик на пустых очередях - транспорт молчит, состояние не меняется.
    sink.processTick();
    EXPECT_EQ(trans.sentCount, 3);
}
