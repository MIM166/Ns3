
#include "bolt-queue.h"
#include "bolt-header.h"
#include "ns3/ipv4-header.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/simulator.h"
#include "ns3/tcp-header.h"
#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("BoltQueue");
NS_OBJECT_ENSURE_REGISTERED (BoltQueue);

namespace {

struct ParsedBoltPacket
{
  bool hadL2Prefix{false};
  Ptr<Packet> l2Prefix;
  Ipv4Header ipHeader;
  TcpHeader tcpHeader;
  BoltHeader boltHeader;
  Ptr<Packet> payload;
};

bool
ParseBoltTcpPayload (Ptr<const Packet> packet, ParsedBoltPacket* parsedOut)
{
  for (int trial = 0; trial < 2; ++trial)
    {
      bool skipL2 = (trial == 1);
      Ptr<Packet> copy = packet->Copy ();
      Ptr<Packet> l2Prefix = Create<Packet> ();

      if (skipL2)
        {
          if (copy->GetSize () < 2)
            {
              continue;
            }
          // PointToPoint may prepend a 2-byte PPP protocol field before IPv4.
          l2Prefix = copy->CreateFragment (0, 2);
          copy->RemoveAtStart (2);
        }

      Ipv4Header ipHdr;
      if (copy->RemoveHeader (ipHdr) == 0 || ipHdr.GetProtocol () != 6)
        {
          continue;
        }

      TcpHeader tcpHdr;
      if (copy->RemoveHeader (tcpHdr) == 0)
        {
          continue;
        }

      BoltHeader boltHdr;
      if (copy->GetSize () < boltHdr.GetSerializedSize ())
        {
          continue;
        }
      if (copy->RemoveHeader (boltHdr) == 0)
        {
          continue;
        }

      if (parsedOut)
        {
          parsedOut->hadL2Prefix = skipL2;
          parsedOut->l2Prefix = l2Prefix;
          parsedOut->ipHeader = ipHdr;
          parsedOut->tcpHeader = tcpHdr;
          parsedOut->boltHeader = boltHdr;
          parsedOut->payload = copy;
        }
      return true;
    }

  return false;
}

bool
ExtractBoltFromTcpPayload (Ptr<const Packet> packet,
                           Ipv4Header* ipOut,
                           TcpHeader* tcpOut,
                           BoltHeader* boltOut,
                           bool* hadL2PrefixOut)
{
  ParsedBoltPacket parsed;
  if (!ParseBoltTcpPayload (packet, &parsed))
    {
      return false;
    }

  if (ipOut)
    {
      *ipOut = parsed.ipHeader;
    }
  if (tcpOut)
    {
      *tcpOut = parsed.tcpHeader;
    }
  if (boltOut)
    {
      *boltOut = parsed.boltHeader;
    }
  if (hadL2PrefixOut)
    {
      *hadL2PrefixOut = parsed.hadL2Prefix;
    }

  return true;
}

Ptr<Packet>
RewriteBoltInTcpPayload (Ptr<const Packet> packet, const BoltHeader& newBoltHdr)
{
  ParsedBoltPacket parsed;
  if (!ParseBoltTcpPayload (packet, &parsed))
    {
      return nullptr;
    }

  Ptr<Packet> rebuiltPayload = parsed.payload->Copy ();
  rebuiltPayload->AddHeader (newBoltHdr);

  TcpHeader tcpHdr = parsed.tcpHeader;
  tcpHdr.InitializeChecksum (parsed.ipHeader.GetSource (),
                             parsed.ipHeader.GetDestination (),
                             parsed.ipHeader.GetProtocol ());
  rebuiltPayload->AddHeader (tcpHdr);

  Ipv4Header ipHdr = parsed.ipHeader;
  ipHdr.SetPayloadSize (rebuiltPayload->GetSize ());
  if (Node::ChecksumEnabled ())
    {
      ipHdr.EnableChecksum ();
    }
  rebuiltPayload->AddHeader (ipHdr);

  Ptr<Packet> rebuiltPacket = Create<Packet> ();
  if (parsed.hadL2Prefix && parsed.l2Prefix != nullptr)
    {
      rebuiltPacket->AddAtEnd (parsed.l2Prefix);
    }
  rebuiltPacket->AddAtEnd (rebuiltPayload);
  return rebuiltPacket;
}

} // namespace

TypeId
BoltQueue::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::BoltQueue<Packet>")
    .SetParent<Queue<Packet>> ()
    .SetGroupName ("Network")
    .AddConstructor<BoltQueue> ()
    .AddDeprecatedName ("ns3::BoltQueue")
    .AddAttribute ("MaxSize",
                   "The max queue size",
                   QueueSizeValue (QueueSize ("100p")),
                   MakeQueueSizeAccessor (&BoltQueue::SetMaxSize,
                                          &BoltQueue::GetMaxSize),
                   MakeQueueSizeChecker ())
    .AddAttribute ("LinkRate",
                   "The transmission rate of the link",
                   DataRateValue (DataRate ("100Gbps")),
                   MakeDataRateAccessor (&BoltQueue::m_linkRate),
                   MakeDataRateChecker ())
    .AddAttribute ("CcThreshold",
                   "Queue threshold to trigger SRC generation (in bytes)",
                   UintegerValue (MTU),
                   MakeUintegerAccessor (&BoltQueue::m_ccThreshold),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("EnableSm",
                   "Whether supply matching is enabled",
                   BooleanValue (true),
                   MakeBooleanAccessor (&BoltQueue::m_enableSm),
                   MakeBooleanChecker ())
  ;
  return tid;
}

BoltQueue::BoltQueue ()
  : m_linkRate (DataRate ("100Gbps")),
    m_ccThreshold (MTU),
    m_enableSm (true),
    m_pruToken (0),
    m_smToken (0),
    m_lastSmTime (Simulator::Now ()),
    NS_LOG_TEMPLATE_DEFINE ("BoltQueue")
{
  NS_LOG_FUNCTION (this);
  
  // Create internal queue for actual packet storage
  m_queue = CreateObject<DropTailQueue<Packet>> ();
}

BoltQueue::~BoltQueue ()
{
  NS_LOG_FUNCTION (this);
}

void
BoltQueue::SetLinkRate (DataRate rate)
{
  NS_LOG_FUNCTION (this << rate);
  m_linkRate = rate;
}

DataRate
BoltQueue::GetLinkRate (void) const
{
  return m_linkRate;
}

void
BoltQueue::SetCcThreshold (uint32_t threshold)
{
  NS_LOG_FUNCTION (this << threshold);
  m_ccThreshold = threshold;
}

uint32_t
BoltQueue::GetCcThreshold (void) const
{
  return m_ccThreshold;
}

void
BoltQueue::SetMaxSize (QueueSize size)
{
  NS_LOG_FUNCTION (this << size);
  QueueBase::SetMaxSize (size);
  if (m_queue)
    {
      m_queue->SetMaxSize (size);
    }
}

QueueSize
BoltQueue::GetMaxSize (void) const
{
  if (m_queue)
    {
      return m_queue->GetMaxSize ();
    }
  return QueueBase::GetMaxSize ();
}

void
BoltQueue::SetSendSrcCallback (Callback<void, Ptr<Packet>> cb)
{
  NS_LOG_FUNCTION (this);
  m_sendSrcCallback = cb;
}
 //Core logic
bool
BoltQueue::Enqueue (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);

  // Extract BOLT header from TCP payload (packet on wire is IP+TCP+payload).
  BoltHeader boltHdr;
  if (!ExtractBoltFromTcpPayload (packet, nullptr, nullptr, &boltHdr, nullptr))
    {
      return m_queue->Enqueue (packet);
    }

  // Skip non-DATA packets (ACKs, SRC packets, etc.)
  if (!(boltHdr.GetFlags () & BoltHeader::DATA))
    {
      NS_LOG_LOGIC ("Non-DATA packet, skipping Bolt logic");
      return m_queue->Enqueue (packet);
    }

  // ===================================================================
  // ALGORITHM 1: BOLT LOGIC AT THE SWITCH
  // ===================================================================

 //How much bandwidth is available ?
  CalculateSupplyToken (packet->GetSize ());
  // Include the arriving packet, as in an on-arrival congestion check.
  uint32_t currentQueueSize = m_queue->GetNBytes ();
  bool congested = currentQueueSize > m_ccThreshold;

  

  // -------------------------------------------------------------------
  // Line 4: Check if congested (cur_q_size >= CCThreshold)
  // -------------------------------------------------------------------
  if (congested)
    {
      NS_LOG_LOGIC ("Queue congested: " << currentQueueSize << " >= " << m_ccThreshold);

      // Line 5: Check if DEC flag not already set
      if (!(boltHdr.GetFlags () & BoltHeader::DECWIN))
        {
          // Lines 6-9: Generate SRC packet
          GenerateSrcPacket (packet);

          // Line 10: Mark DEC flag on original packet
          boltHdr.SetFlags ((boltHdr.GetFlags () | BoltHeader::DECWIN) & ~BoltHeader::INCWIN);
          Ptr<Packet> rewritten = RewriteBoltInTcpPayload (packet, boltHdr);
          if (rewritten != nullptr)
            {
              packet = rewritten;
            }

          NS_LOG_LOGIC ("SRC generated, DEC flag set");
        }
    }
  else if (boltHdr.GetFlags () & BoltHeader::LAST)
    {
      UpdatePruToken (packet);
    }
  // -------------------------------------------------------------------
  // Line 13: Process INC flag (packet requests bandwidth)
  // -------------------------------------------------------------------
  else if (boltHdr.GetFlags () & BoltHeader::INCWIN)
    {
      bool keepIncFlag = ProcessIncFlag (packet);

      if (!keepIncFlag)
        {
          // Line 19: Clear INC flag (no token available)
          boltHdr.SetFlags (boltHdr.GetFlags () & ~BoltHeader::INCWIN);
          Ptr<Packet> rewritten = RewriteBoltInTcpPayload (packet, boltHdr);
          if (rewritten != nullptr)
            {
              packet = rewritten;
            }
          NS_LOG_LOGIC ("INC flag cleared (no tokens)");
        }
      else
        {
          NS_LOG_LOGIC ("INC flag kept (token consumed)");
        }
    }

  // Enqueue the packet to internal queue
  bool enqueued = m_queue->Enqueue (packet);

  if (enqueued)
    {
      NS_LOG_LOGIC ("Packet enqueued. Queue size: " << m_queue->GetNBytes () << " bytes");
    }
  else
    {
      NS_LOG_LOGIC ("Packet dropped. Queue full.");
    }

  return enqueued;
}

Ptr<Packet>
BoltQueue::Dequeue (void)
{
  NS_LOG_FUNCTION (this);
  Ptr<Packet> packet = m_queue->Dequeue ();
  
  if (packet)
    {
      NS_LOG_LOGIC ("Packet dequeued. Queue size: " << m_queue->GetNBytes () << " bytes");
    }
  
  return packet;
}

Ptr<Packet>
BoltQueue::Remove (void)
{
  NS_LOG_FUNCTION (this);
  Ptr<Packet> packet = m_queue->Remove ();
  return packet;
}

Ptr<const Packet>
BoltQueue::Peek (void) const
{
  NS_LOG_FUNCTION (this);
  return m_queue->Peek ();
}

// =====================================================================
// PUBLIC QUEUE DIAGNOSTIC METHODS
// =====================================================================

uint32_t
BoltQueue::GetQueueNBytes (void) const
{
  return m_queue ? m_queue->GetNBytes () : 0;
}

uint32_t
BoltQueue::GetQueueNPackets (void) const
{
  return m_queue ? m_queue->GetNPackets () : 0;
}

double
BoltQueue::GetQueueFillFraction (void) const
{
  if (!m_queue) return 0.0;
  QueueSize maxSize = m_queue->GetMaxSize ();
  if (maxSize.GetUnit () == QueueSizeUnit::BYTES)
    {
      uint32_t maxBytes = maxSize.GetValue ();
      return maxBytes > 0 ? (double)m_queue->GetNBytes () / maxBytes : 0.0;
    }
  else
    {
      uint32_t maxPkts = maxSize.GetValue ();
      return maxPkts > 0 ? (double)m_queue->GetNPackets () / maxPkts : 0.0;
    }
}

uint32_t
BoltQueue::GetPruToken (void) const
{
  return m_pruToken;
}

int32_t
BoltQueue::GetSmToken (void) const
{
  return m_smToken;
}

// =====================================================================
// PRIVATE METHODS - ALGORITHM IMPLEMENTATIONS
// =====================================================================

void
BoltQueue::GenerateSrcPacket (Ptr<Packet> dataPacket)
{
  NS_LOG_FUNCTION (this << dataPacket);

  // Extract BOLT header from TCP payload
  BoltHeader dataHdr;
  if (!ExtractBoltFromTcpPayload (dataPacket, nullptr, nullptr, &dataHdr, nullptr))
    {
      NS_LOG_LOGIC ("GenerateSrcPacket: no Bolt payload found");
      return;
    }

  // Create SRC packet (empty payload)
  Ptr<Packet> srcPacket = Create<Packet> (0);

  // Create SRC header
  BoltHeader srcHdr;

  // Set BTS (Back To Sender) flag to indicate this is SRC packet
  srcHdr.SetFlags (BoltHeader::BTS | BoltHeader::DECWIN);

  // Swap source and destination ports (send back to sender)
  srcHdr.SetSrcPort (dataHdr.GetDstPort ());
  srcHdr.SetDstPort (dataHdr.GetSrcPort ());

  // Line 6: Set queue size (drain time in nanoseconds)
  uint32_t currentQueueSize = m_queue->GetNBytes ();
  uint64_t drainTimeNs = (uint64_t)currentQueueSize * 8 * 1000000000ULL / m_linkRate.GetBitRate ();
  srcHdr.SetDrainTime (std::min (drainTimeNs, (uint64_t)0xFFFFFFFF)); // Cap at 32-bit

  // Line 7: Set link rate (encode in flags)
  uint64_t rateGbps = m_linkRate.GetBitRate () / 1000000000;
  if (rateGbps >= 400)
    {
      srcHdr.SetFlags (srcHdr.GetFlags () | BoltHeader::LINK400G);
    }
  else if (rateGbps >= 100)
    {
      srcHdr.SetFlags (srcHdr.GetFlags () | BoltHeader::LINK100G);
    }
  else if (rateGbps >= 40)
    {
      srcHdr.SetFlags (srcHdr.GetFlags () | BoltHeader::LINK40G);
    }
  else if (rateGbps >= 25)
    {
      srcHdr.SetFlags (srcHdr.GetFlags () | BoltHeader::LINK25G);
    }
  else
    {
      srcHdr.SetFlags (srcHdr.GetFlags () | BoltHeader::LINK10G);
    }

  // Line 8: Reflect TX timestamp from data packet
  srcHdr.SetReflectedDelay (dataHdr.GetReflectedDelay ());

  // Add header to SRC packet
  srcPacket->AddHeader (srcHdr);

  NS_LOG_LOGIC ("SRC packet created: drainTime=" << drainTimeNs << "ns, linkRate=" << rateGbps << "Gbps");

  // Line 9: Send SRC packet back to sender
  if (!m_sendSrcCallback.IsNull ())
    {
      m_sendSrcCallback (srcPacket);
      NS_LOG_LOGIC ("SRC packet sent via callback");
    }
  else
    {
      NS_LOG_WARN ("SRC callback not set! SRC packet not sent.");
    }
}

void
BoltQueue::UpdatePruToken (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);

  // Extract Bolt header
  BoltHeader boltHdr;
  if (!ExtractBoltFromTcpPayload (packet, nullptr, nullptr, &boltHdr, nullptr))
    {
      return;
    }

  // Line 12: If not FIRST window, increment PRU token
  if (!(boltHdr.GetFlags () & BoltHeader::FIRST))
    {
      m_pruToken++;
      NS_LOG_LOGIC ("PRU token incremented to " << m_pruToken);
    }
  else
    {
      NS_LOG_LOGIC ("FIRST flag set, PRU token not incremented");
    }
}

void
BoltQueue::CalculateSupplyToken (uint32_t packetSize)
{
  NS_LOG_FUNCTION (this << packetSize);

  if (!m_enableSm)
    {
      return;
    }

  // Algorithm 3, Line 2: Calculate inter-arrival time
  Time now = Simulator::Now ();
  Time interArrivalTime = now - m_lastSmTime;
  m_lastSmTime = now;

  // Algorithm 3, Line 4: Calculate supply (bandwidth × time)
  // supply = BW × inter_arrival_time (in bytes)
  double interArrivalSeconds = interArrivalTime.GetSeconds ();
  uint64_t supplyBits = (uint64_t)(m_linkRate.GetBitRate () * interArrivalSeconds);
  uint32_t supplyBytes = supplyBits / 8;

  // Algorithm 3, Line 5: Demand = packet size
  uint32_t demandBytes = packetSize;

  // Algorithm 3, Line 6: Update SM token
  int64_t delta = (int64_t)supplyBytes - (int64_t)demandBytes;
  // Keep SM token bounded to one packet worth in each direction.
  m_smToken = static_cast<int32_t> (std::clamp ((int64_t)m_smToken + delta,
                                                -(int64_t)MTU,
                                                (int64_t)MTU));

  NS_LOG_LOGIC ("SM token updated: supply=" << supplyBytes 
                << " demand=" << demandBytes 
                << " smToken=" << m_smToken);
}

bool
BoltQueue::ProcessIncFlag (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);

  // Line 14: Check if PRU token available
  if (ConsumePruToken ())
    {
      return true; // Keep INC flag
    }
  // Line 16: Check if SM token >= MTU
  else if (ConsumeSmToken ())
    {
      return true; // Keep INC flag
    }
  else
    {
      // Line 19: No token available, clear INC flag
      NS_LOG_LOGIC ("No tokens available (PRU=" << m_pruToken << ", SM=" << m_smToken << ")");
      return false; // Clear INC flag
    }
}

bool
BoltQueue::ConsumePruToken ()
{
  NS_LOG_FUNCTION (this);

  if (m_pruToken == 0)
    {
      return false;
    }

  m_pruToken--;
  NS_LOG_LOGIC ("PRU token consumed. Remaining: " << m_pruToken);
  return true;
}

bool
BoltQueue::ConsumeSmToken ()
{
  NS_LOG_FUNCTION (this);

  if (!m_enableSm || m_smToken < (int32_t)MTU)
    {
      return false;
    }

  m_smToken -= MTU;
  NS_LOG_LOGIC ("SM token consumed. Remaining: " << m_smToken);
  return true;
}

} // namespace ns3
