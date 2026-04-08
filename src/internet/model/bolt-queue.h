
#ifndef BOLT_QUEUE_H
#define BOLT_QUEUE_H

#include "ns3/queue.h"
#include "ns3/data-rate.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/drop-tail-queue.h"

namespace ns3 {

/**
 * \ingroup network
 * \brief Bolt Queue implements switch-side congestion control logic
 *
 * This queue implements the Bolt switch logic from NSDI'23:
 * - SRC (Sub-RTT Control): Generates switch-to-sender feedback packets
 * - PRU (Proactive Ramp-Up): Manages tokens for flow completions
 * - SM (Supply Matching): Tracks instantaneous link utilization
 */
class BoltQueue : public Queue<Packet>
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * \brief Constructor
   */
  BoltQueue ();

  /**
   * \brief Destructor
   */
  virtual ~BoltQueue ();

  /**
   * \brief Set the link rate for SM calculation
   * \param rate Link capacity (e.g., 100Gbps)
   */
  void SetLinkRate (DataRate rate);

  /**
   * \brief Get the link rate
   * \return Current link rate
   */
  DataRate GetLinkRate (void) const;

  /**
   * \brief Set congestion threshold (queue size to trigger SRC)
   * \param threshold Threshold in bytes (default: 1 MTU = 1500 bytes)
   */
  void SetCcThreshold (uint32_t threshold);

  /**
   * \brief Get congestion threshold
   * \return Threshold in bytes
   */
  uint32_t GetCcThreshold (void) const;

  /**
   * \brief Get current number of bytes stored in the internal queue.
   *
   * Packets are stored in the private m_queue (DropTailQueue), not in
   * the QueueBase counters, so this method must be used instead of
   * the inherited GetNBytes() when sampling queue depth externally.
   *
   * \return Number of bytes currently in the queue
   */
  uint32_t GetQueueNBytes (void) const;

  /**
   * \brief Get current number of packets stored in the internal queue.
   * \return Number of packets currently in the queue
   */
  uint32_t GetQueueNPackets (void) const;

  /**
   * \brief Get current queue occupancy as a fraction of the maximum size.
   * \return Queue fill fraction in [0.0, 1.0]
   */
  double GetQueueFillFraction (void) const;

  /**
   * \brief Get current PRU token count (for diagnostics)
   * \return PRU token value
   */
  uint32_t GetPruToken (void) const;

  /**
   * \brief Get current SM token value (for diagnostics)
   * \return SM token value (can be negative)
   */
  int32_t GetSmToken (void) const;

  /**
   * \brief Set maximum queue size
   * \param size Maximum size in packets or bytes
   */
  void SetMaxSize (QueueSize size);

  /**
   * \brief Get maximum queue size
   * \return Maximum size in packets or bytes
   */
  QueueSize GetMaxSize (void) const;

  /**
   * \brief Set callback for sending SRC packets back to sender
   * \param cb Callback function that takes a packet
   */
  void SetSendSrcCallback (Callback<void, Ptr<Packet>> cb);

  // Override Queue<Packet> methods
  bool Enqueue (Ptr<Packet> packet) override;
  Ptr<Packet> Dequeue (void) override;
  Ptr<Packet> Remove (void) override;
  Ptr<const Packet> Peek (void) const override;

private:

  /**
   * \brief Generate SRC packet (Algorithm 1, lines 5-9)
   * \param dataPacket Original data packet that triggered SRC
   */
  void GenerateSrcPacket (Ptr<Packet> dataPacket);

  /**
   * \brief Update PRU token on flow completion (Algorithm 1, lines 11-12)
   * \param packet Packet to check for LAST/FIRST flags
   */
  void UpdatePruToken (Ptr<Packet> packet);

  /**
   * \brief Calculate supply-demand mismatch (Algorithm 3)
   * \param packetSize Size of arriving packet (demand)
   */
  void CalculateSupplyToken (uint32_t packetSize);

  /**
   * \brief Process INC flag and consume tokens (Algorithm 1, lines 13-19)
   * \param packet Packet with potential INC flag
   * \return true if INC flag should remain set, false if cleared
   */
  bool ProcessIncFlag (Ptr<Packet> packet);

  /**
   * \brief Consume one PRU token without consulting SM state.
   * \return true if a PRU token was consumed
   */
  bool ConsumePruToken ();

  /**
   * \brief Consume one MTU worth of SM credit.
   * \return true if SM credit was consumed
   */
  bool ConsumeSmToken ();

  // State variables
  DataRate m_linkRate;              //!< Link capacity
  uint32_t m_ccThreshold;           //!< Congestion threshold (1 MTU)
  bool m_enableSm;                  //!< Whether supply matching is enabled
  uint32_t m_pruToken;              //!< PRU tokens available
  int32_t m_smToken;                //!< SM token (can be negative)
  Time m_lastSmTime;                //!< Last packet arrival time for SM
  
  Ptr<DropTailQueue<Packet>> m_queue;  //!< Internal queue for actual storage
  Callback<void, Ptr<Packet>> m_sendSrcCallback;  //!< Callback to send SRC packets
  
  static const uint32_t MTU = 1500; //!< Default MTU in bytes

  NS_LOG_TEMPLATE_DECLARE; //!< redefinition of the log component
};

} // namespace ns3

#endif /* BOLT_QUEUE_H */
