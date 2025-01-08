#include "adaptive-fifo-queue-disc.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/drop-tail-queue.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("AdaptiveFifoQueueDisc");

NS_OBJECT_ENSURE_REGISTERED(AdaptiveFifoQueueDisc);

TypeId AdaptiveFifoQueueDisc::GetTypeId(void) {
    static TypeId tid = TypeId("ns3::AdaptiveFifoQueueDisc")
        .SetParent<QueueDisc>()
        .SetGroupName("TrafficControl")
        .AddConstructor<AdaptiveFifoQueueDisc>()
        .AddAttribute("MaxSize",
                      "The initial max queue size",
                      QueueSizeValue(QueueSize("1000p")),
                      MakeQueueSizeAccessor(&QueueDisc::SetMaxSize,
                                            &QueueDisc::GetMaxSize),
                      MakeQueueSizeChecker())
        .AddAttribute("AdaptationInterval",
                      "The time interval for queue size adaptation",
                      TimeValue(Seconds(1)),
                      MakeTimeAccessor(&AdaptiveFifoQueueDisc::SetAdaptationInterval),
                      MakeTimeChecker())
        .AddAttribute("AdaptationThreshold",
                      "The threshold of queue occupancy to trigger size adaptation",
                      UintegerValue(50),
                      MakeUintegerAccessor(&AdaptiveFifoQueueDisc::SetAdaptationThreshold),
                      MakeUintegerChecker<uint32_t>());
    return tid;
}

AdaptiveFifoQueueDisc::AdaptiveFifoQueueDisc()
    : m_adaptationInterval(Seconds(5)),
      m_adaptationThreshold(50) {}

AdaptiveFifoQueueDisc::~AdaptiveFifoQueueDisc() {}

void AdaptiveFifoQueueDisc::DoInitialize() {
    NS_LOG_FUNCTION(this);
    QueueDisc::DoInitialize();
    AdjustQueueSize();
    // m_adaptationEvent = Simulator::Schedule(m_adaptationInterval, &AdaptiveFifoQueueDisc::AdjustQueueSize, this);
}

void AdaptiveFifoQueueDisc::DoDispose() {
    NS_LOG_FUNCTION(this);
    Simulator::Cancel(m_adaptationEvent);
    QueueDisc::DoDispose();
}

void AdaptiveFifoQueueDisc::SetAdaptationInterval(Time interval) {
    m_adaptationInterval = interval;
}

void AdaptiveFifoQueueDisc::SetAdaptationThreshold(uint32_t threshold) {
    m_adaptationThreshold = threshold;
}

bool AdaptiveFifoQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item) {
    NS_LOG_FUNCTION(this << item);

    if (GetCurrentSize() + item > GetMaxSize()) {
        NS_LOG_LOGIC("Queue full -- dropping pkt");
        DropBeforeEnqueue(item, LIMIT_EXCEEDED_DROP);
        return false;
    }

    bool retval = GetInternalQueue(0)->Enqueue(item);

    // If Queue::Enqueue fails, QueueDisc::DropBeforeEnqueue is called by the
    // internal queue because QueueDisc::AddInternalQueue sets the trace callback

    NS_LOG_LOGIC("Number packets " << GetInternalQueue(0)->GetNPackets());
    NS_LOG_LOGIC("Number bytes " << GetInternalQueue(0)->GetNBytes());

    return retval;
}

Ptr<QueueDiscItem> AdaptiveFifoQueueDisc::DoDequeue(void) {
    NS_LOG_FUNCTION(this);

    Ptr<QueueDiscItem> item = GetInternalQueue(0)->Dequeue();

    if (!item) {
        NS_LOG_LOGIC("Queue empty");
        return 0;
    }

    return item;
}

Ptr<const QueueDiscItem> AdaptiveFifoQueueDisc::DoPeek(void) {
    NS_LOG_FUNCTION(this);

    Ptr<const QueueDiscItem> item = GetInternalQueue(0)->Peek();

    if (!item) {
        NS_LOG_LOGIC("Queue empty");
        return 0;
    }

    return item;
}

bool AdaptiveFifoQueueDisc::CheckConfig(void) {
    NS_LOG_FUNCTION(this);
    if (GetNQueueDiscClasses() > 0) {
        NS_LOG_ERROR("AdaptiveFifoQueueDisc cannot have classes");
        return false;
    }

    if (GetNPacketFilters() > 0) {
        NS_LOG_ERROR("AdaptiveFifoQueueDisc needs no packet filter");
        return false;
    }

    if (GetNInternalQueues() == 0) {
        // Add a drop-tail queue
        AddInternalQueue(CreateObjectWithAttributes<DropTailQueue<QueueDiscItem>>(
            "MaxSize", QueueSizeValue(GetMaxSize())));
    }

    if (GetNInternalQueues() != 1) {
        NS_LOG_ERROR("AdaptiveFifoQueueDisc needs 1 internal queue");
        return false;
    }

    return true;
}

void AdaptiveFifoQueueDisc::InitializeParams(void) {
    NS_LOG_FUNCTION(this);
}

void AdaptiveFifoQueueDisc::AdjustQueueSize() {
    NS_LOG_FUNCTION(this);
    // Current number of packets in the queue  
    uint32_t currentQueueSize = GetNPackets(); 
    uint32_t queueSize = GetInternalQueue(0) ->GetCurrentSize().GetValue();

    // Current Max Size of Queue
    QueueSize currentMaxSize = GetMaxSize();
    uint32_t currentMaxSizeValue = currentMaxSize.GetValue();
    m_adaptationThreshold = currentMaxSizeValue/2;

    // output for troubleshooting
    NS_LOG_UNCOND("MaxSize: "<<currentMaxSizeValue<<" CurrentSize: "<<currentQueueSize << " Q: "<<queueSize);

    if (currentQueueSize > m_adaptationThreshold) {
        // Increase queue size dynamically
        uint32_t newSize = currentMaxSizeValue + 10;
        SetMaxSize(QueueSize(QueueSizeUnit::PACKETS, newSize));
        NS_LOG_INFO("Increased MaxSize to: " << newSize);
        NS_LOG_UNCOND("Increased MaxSize to: " << newSize);
    } 
    else if (currentQueueSize < m_adaptationThreshold / 2 && currentMaxSize.GetValue() > 20) {
        // Decrease queue size dynamically
        uint32_t newSize = currentMaxSizeValue - 10;
        SetMaxSize(QueueSize(QueueSizeUnit::PACKETS, newSize));
        NS_LOG_UNCOND("Decreased MaxSize to: " << newSize);
    }

    // Reschedule the next adaptation
    m_adaptationEvent = Simulator::Schedule(m_adaptationInterval, &AdaptiveFifoQueueDisc::AdjustQueueSize, this);
}

} // namespace ns3
