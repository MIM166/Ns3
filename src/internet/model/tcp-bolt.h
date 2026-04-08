/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * TCP Bolt - Bolt Congestion Control as a TCP variant
 * Implements Algorithm 2 from NSDI'23 Bolt paper
 */

#ifndef TCP_BOLT_H
#define TCP_BOLT_H

#include "tcp-congestion-ops.h"
#include "ns3/traced-value.h"
#include "ns3/data-rate.h"
#include "ns3/sequence-number.h"

namespace ns3 {

/**
 * \brief Bolt congestion control algorithm
 *
 * Implements the Bolt CC algorithm from NSDI'23 as a TCP variant.
 * Handles SRC packets from switches and adjusts cwnd accordingly.
 */
class TcpBolt : public TcpCongestionOps
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
  TcpBolt (void);

  /**
   * \brief Copy constructor
   * \param sock object to copy
   */
  TcpBolt (const TcpBolt& sock);

  /**
   * \brief Destructor
   */
  virtual ~TcpBolt (void);

  /**
   * \brief Get algorithm name
   * \return "TcpBolt"
   */
  virtual std::string GetName () const;

  /**
   * \brief Handle congestion window increase
   * \param tcb Transmission control block
   * \param segmentsAcked Number of segments ACKed
   */
  virtual void IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked);

  /**
   * \brief Get slow start threshold (not used in Bolt)
   * \param tcb Transmission control block
   * \param bytesInFlight Bytes in flight
   * \return ssthresh value
   */
  virtual uint32_t GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight);

  /**
   * \brief Create and return a copy
   * \return Copy of this object
   */
  virtual Ptr<TcpCongestionOps> Fork ();

  /**
   * \brief Decide whether the outgoing data segment should carry INCWIN.
   * \param seq Sequence number of the segment being transmitted
   * \param segmentBytes Number of payload bytes in the segment
   * \param tcb Transmission control block
   * \return true if the segment should request one additive increase
   */
  bool ShouldRequestInc (SequenceNumber32 seq,
                         uint32_t segmentBytes,
                         Ptr<const TcpSocketState> tcb);

  /**
   * \brief Process SRC packet (Algorithm 2, lines 1-8)
   * \param queueSize Queue size from SRC packet (drain time in ns)
   * \param linkRate Link rate from SRC packet flags
   * \param txTimestamp TX timestamp reflected in SRC
   * \param tcb Transmission control block
   */
  void ProcessSrcPacket (uint32_t queueSize, DataRate linkRate, Time txTimestamp, Ptr<TcpSocketState> tcb);

  /**
   * \brief Process INC flag from ACK (Algorithm 2, lines 10-14)
   * \param incFlag Whether INC flag is set
   * \param tcb Transmission control block
   */
  void ProcessIncFlag (bool incFlag, Ptr<TcpSocketState> tcb);

private:
  Time m_lastDecTime;                    //!< Last cwnd decrease time
  SequenceNumber32 m_nextIncSequence;    //!< Next seq eligible to request INC
  bool m_haveNextIncSequence;            //!< Whether m_nextIncSequence is initialized
  
  TracedValue<uint32_t> m_cWnd;          //!< Congestion window (for tracing)
};

} // namespace ns3

#endif /* TCP_BOLT_H */
