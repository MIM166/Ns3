/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * TCP Bolt - Bolt Congestion Control as a TCP variant
 */

#include "tcp-bolt.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <cmath>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpBolt");
NS_OBJECT_ENSURE_REGISTERED (TcpBolt);

TypeId
TcpBolt::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpBolt")
    .SetParent<TcpCongestionOps> ()
    .SetGroupName ("Internet")
    .AddConstructor<TcpBolt> ()
  ;
  return tid;
}

TcpBolt::TcpBolt (void)
  : TcpCongestionOps (),
    m_lastDecTime (Seconds (0)),
    m_nextIncSequence (0),
    m_haveNextIncSequence (false)
{
  NS_LOG_FUNCTION (this);
}

TcpBolt::TcpBolt (const TcpBolt& sock)
  : TcpCongestionOps (sock),
    m_lastDecTime (sock.m_lastDecTime),
    m_nextIncSequence (sock.m_nextIncSequence),
    m_haveNextIncSequence (sock.m_haveNextIncSequence)
{
  NS_LOG_FUNCTION (this);
}

TcpBolt::~TcpBolt (void)
{
  NS_LOG_FUNCTION (this);
}

std::string
TcpBolt::GetName () const
{
  return "TcpBolt";
}

Ptr<TcpCongestionOps>
TcpBolt::Fork ()
{
  return CopyObject<TcpBolt> (this);
}

void
TcpBolt::IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked);
  // Positive growth is driven only by reflected INC feedback.  Leaving this
  // path empty prevents a second additive-increase loop from fighting SRC.
}

bool
TcpBolt::ShouldRequestInc (SequenceNumber32 seq,
                           uint32_t segmentBytes,
                           Ptr<const TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << seq << segmentBytes << tcb);

  if (segmentBytes == 0)
    {
      return false;
    }

  if (!m_haveNextIncSequence || seq >= m_nextIncSequence)
    {
      uint32_t requestStride = std::max (tcb->m_cWnd.Get (), tcb->m_segmentSize);
      m_nextIncSequence = seq + SequenceNumber32 (requestStride);
      m_haveNextIncSequence = true;
      return true;
    }

  return false;
}

uint32_t
TcpBolt::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
  NS_LOG_FUNCTION (this << tcb << bytesInFlight);
  
  // Bolt doesn't use slow start threshold in traditional sense
  // Return a large value to effectively disable slow start exit
  return tcb->m_initialCWnd * tcb->m_segmentSize * 1000;
}

void
TcpBolt::ProcessSrcPacket (uint32_t drainTimeNs, DataRate linkRate, 
                           Time txTimestamp, Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << drainTimeNs << linkRate << txTimestamp);

  // ===================================================================
  // ALGORITHM 2: HandleSrc (Lines 1-8)
  // ===================================================================

  // Line 2: Calculate rttsrc (time since TX)
  Time now = Simulator::Now ();
  Time rttsrc = now - txTimestamp;
  if (rttsrc.IsNegative () || rttsrc.IsZero ())
    {
      rttsrc = NanoSeconds (1);
    }
  
  NS_LOG_LOGIC ("rttsrc = " << rttsrc.GetMicroSeconds () << " us");

  // Line 3: Calculate reaction_factor = flow.rate / link_rate
  // flow.rate ≈ cwnd / RTT
  // Convert flow rate to bits/s to match linkRate units.
  double flowRateBps = (tcb->m_cWnd.Get () * 8.0) / rttsrc.GetSeconds ();
  double reactionFactor = std::clamp (flowRateBps / linkRate.GetBitRate (), 0.0, 1.0);
  
  NS_LOG_LOGIC ("reactionFactor = " << reactionFactor);

  // Line 4-5: Calculate targetq (queue to drain, in packets)
  // drainTimeNs is queue size in nanoseconds of drain time
  // Convert to bytes: queueBytes = drainTimeNs * linkRate / 8 / 1e9
  double queueBytes = drainTimeNs * linkRate.GetBitRate () / 8.0 / 1e9;
  uint32_t targetq =
      std::max (1U, static_cast<uint32_t> (std::ceil (queueBytes * reactionFactor /
                                                      tcb->m_segmentSize)));
  
  NS_LOG_LOGIC ("targetq = " << targetq << " packets");

  // Line 6-8: Decrease cwnd if enough time has passed.
  // Bound the decrease cadence by at least one packet transmission time so
  // SRC feedback cannot drive cwnd down at an unphysical rate.
  Time minInterval = rttsrc / targetq;
  Time pktTxTime = Seconds ((tcb->m_segmentSize * 8.0) / linkRate.GetBitRate ());
  if (minInterval < pktTxTime)
    {
      minInterval = pktTxTime;
    }
  
  if ((now - m_lastDecTime) >= minInterval)
    {
      // Decrease cwnd by 1 MSS
      uint32_t newCwnd = std::max (tcb->m_cWnd.Get () - tcb->m_segmentSize, 
                                   tcb->m_segmentSize);
      tcb->m_cWnd = newCwnd;
      m_lastDecTime = now;
      
      NS_LOG_INFO ("SRC: cwnd decreased to " << tcb->m_cWnd 
                   << " (minInterval=" << minInterval.GetMicroSeconds () << "us)");
    }
  else
    {
      NS_LOG_LOGIC ("SRC: Too soon to decrease (last=" 
                    << (now - m_lastDecTime).GetMicroSeconds () << "us, "
                    << "min=" << minInterval.GetMicroSeconds () << "us)");
    }
}

void
TcpBolt::ProcessIncFlag (bool incFlag, Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << incFlag);

  // ===================================================================
  // ALGORITHM 2: HandleAck (Lines 10-11)
  // ===================================================================

  if (incFlag)
    {
      // Increase cwnd by 1 MSS
      tcb->m_cWnd = tcb->m_cWnd.Get () + tcb->m_segmentSize;
      
      // Intentionally no log here to avoid noisy output on every INC flag.
    }
}

} // namespace ns3
