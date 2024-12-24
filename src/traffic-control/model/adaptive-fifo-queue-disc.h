#ifndef ADAPTIVE_FIFO_QUEUE_DISC_H
#define ADAPTIVE_FIFO_QUEUE_DISC_H

#include "ns3/queue-disc.h"

namespace ns3 {

class AdaptiveFifoQueueDisc : public QueueDisc {
public:
    /*
    Get Type id
    Return type id
    */
    static TypeId GetTypeId(void);

    /*
    Constructor: create queue size of 1000p
    */
    AdaptiveFifoQueueDisc();

    virtual ~AdaptiveFifoQueueDisc();

    // Reason for dropping packets
    static constexpr const char* LIMIT_EXCEEDED_DROP = "Queue disc limit exceeded";  //!< Packet dropped due to queue disc limit exceeded


    void SetAdaptationInterval(Time interval);
    void SetAdaptationThreshold(uint32_t threshold);

protected:
    virtual void DoInitialize(void) override;
    virtual void DoDispose(void) override;

private:
    virtual bool DoEnqueue (Ptr<QueueDiscItem> item);
    virtual Ptr<QueueDiscItem> DoDequeue (void);
    virtual Ptr<const QueueDiscItem> DoPeek (void);
    virtual bool CheckConfig (void);
    virtual void InitializeParams (void);
    
    void AdjustQueueSize();
    Time m_adaptationInterval;
    uint32_t m_adaptationThreshold;
    EventId m_adaptationEvent;
};

} // namespace ns3

#endif // ADAPTIVE_FIFO_QUEUE_DISC_H
