/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ipv4-rtt-tag.h"

#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("Ipv4RttTag");

NS_OBJECT_ENSURE_REGISTERED(Ipv4RttTag);

Ipv4RttTag::Ipv4RttTag()
{
    m_pathId = 0;

    m_sourceLeaf = 0;

    m_destLeaf = 0;

    m_tcpSeq = 0;

    m_txTimeNs = 0;

    m_destTimeNs = 0;
}

Ipv4RttTag::~Ipv4RttTag()
{
}

TypeId
Ipv4RttTag::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::Ipv4RttTag")
                            .SetParent<Tag>()
                            .SetGroupName("Internet")
                            .AddConstructor<Ipv4RttTag>();

    return tid;
}

TypeId
Ipv4RttTag::GetInstanceTypeId(void) const
{
    return GetTypeId();
}

void
Ipv4RttTag::SetPathId(uint8_t pathId)
{
    m_pathId = pathId;
}

uint32_t
Ipv4RttTag::GetPathId(void) const
{
    return m_pathId;
}

void
Ipv4RttTag::SetSourceId(uint8_t leafId)
{
    m_sourceLeaf = leafId;
}

uint32_t
Ipv4RttTag::GetSourceId(void) const
{
    return m_sourceLeaf;
}

void
Ipv4RttTag::SetDestId(uint8_t leafId)
{
    m_destLeaf = leafId;
}

uint32_t
Ipv4RttTag::GetDestId(void) const
{
    return m_destLeaf;
}

void
Ipv4RttTag::SetTcpSeq(uint32_t seq)
{
    m_tcpSeq = seq;
}

uint32_t
Ipv4RttTag::GetTcpSeq(void) const
{
    return m_tcpSeq;
}

void
Ipv4RttTag::SetTxTime(Time txTime)
{
    m_txTimeNs = txTime.GetNanoSeconds();
}

Time
Ipv4RttTag::GetTxTime(void) const
{
    return NanoSeconds(m_txTimeNs);
}

void
Ipv4RttTag::SetDestTime(Time rxTime)
{
    m_destTimeNs = rxTime.GetNanoSeconds();
}

Time
Ipv4RttTag::GetDestTime(void) const
{
    return NanoSeconds(m_destTimeNs);
}

uint32_t
Ipv4RttTag::GetSerializedSize(void) const
{
    return sizeof(uint8_t) +  // path
           sizeof(uint8_t) +  // source leaf
           sizeof(uint8_t) +  // dest leaf
           sizeof(uint32_t) + // tcp seq
           sizeof(int64_t) +  // tx time
           sizeof(int64_t);   // dest time
}

void
Ipv4RttTag::Serialize(TagBuffer i) const
{
    i.WriteU8(m_pathId);

    i.WriteU8(m_sourceLeaf);

    i.WriteU8(m_destLeaf);

    i.WriteU32(m_tcpSeq);

    i.WriteU64(static_cast<uint64_t>(m_txTimeNs));

    i.WriteU64(static_cast<uint64_t>(m_destTimeNs));
}

void
Ipv4RttTag::Deserialize(TagBuffer i)
{
    m_pathId = i.ReadU8();

    m_sourceLeaf = i.ReadU8();

    m_destLeaf = i.ReadU8();

    m_tcpSeq = i.ReadU32();

    m_txTimeNs = static_cast<int64_t>(i.ReadU64());

    m_destTimeNs = static_cast<int64_t>(i.ReadU64());
}

void
Ipv4RttTag::Print(std::ostream& os) const
{
    os << "PathId=" << m_pathId << " SourceLeaf=" << m_sourceLeaf << " DestLeaf=" << m_destLeaf
       << " TcpSeq=" << m_tcpSeq << " TxTime(ns)=" << m_txTimeNs
       << " DestTime(ns)=" << m_destTimeNs;
}

} // namespace ns3
