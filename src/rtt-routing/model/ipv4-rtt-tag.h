/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef NS3_IPV4_RTT_TAG_H
#define NS3_IPV4_RTT_TAG_H

#include "ns3/nstime.h"
#include "ns3/tag.h"
#include "ns3/type-id.h"

namespace ns3
{

/**
 * @ingroup rtt-routing
 * @brief Packet tag that associates a TCP segment with an RTT measurement.
 *
 * The source leaf adds the selected path, leaf identifiers, TCP sequence
 * number, and transmit time to a data packet. The destination leaf adds its
 * receive time before the corresponding ACK carries the tag home.
 */
class Ipv4RttTag : public Tag
{
  public:
    /** Create an RTT tag with the implementation's default field values. */
    Ipv4RttTag();

    ~Ipv4RttTag() override;

    /**
     * Get the TypeId for this tag.
     *
     * @return The RTT tag TypeId.
     */
    static TypeId GetTypeId();

    /** @copydoc Tag::GetInstanceTypeId */
    TypeId GetInstanceTypeId() const override;

    /**
     * Set the path used by a tagged data packet.
     *
     * @param pathId Output interface selected at the source leaf.
     */
    void SetPathId(uint8_t pathId);

    /**
     * Get the tagged data path.
     *
     * @return Output interface selected at the source leaf.
     */
    uint32_t GetPathId() const;

    /**
     * Set the source-leaf identifier.
     *
     * @param leafId Source leaf identifier.
     */
    void SetSourceId(uint8_t leafId);

    /**
     * Get the source-leaf identifier.
     *
     * @return Source leaf identifier.
     */
    uint32_t GetSourceId() const;

    /**
     * Set the destination-leaf identifier.
     *
     * @param leafId Destination leaf identifier.
     */
    void SetDestId(uint8_t leafId);

    /**
     * Get the destination-leaf identifier.
     *
     * @return Destination leaf identifier.
     */
    uint32_t GetDestId() const;

    /**
     * Set the tagged TCP sequence number.
     *
     * @param seq TCP sequence number.
     */
    void SetTcpSeq(uint32_t seq);

    /**
     * Get the tagged TCP sequence number.
     *
     * @return TCP sequence number.
     */
    uint32_t GetTcpSeq() const;

    /**
     * Set the time at which the source leaf forwarded the packet.
     *
     * @param txTime Source-leaf transmit time.
     */
    void SetTxTime(Time txTime);

    /**
     * Get the source-leaf transmit time.
     *
     * @return Source-leaf transmit time.
     */
    Time GetTxTime() const;

    /**
     * Set the time at which the packet reached the destination leaf.
     *
     * @param rxTime Destination-leaf receive time.
     */
    void SetDestTime(Time rxTime);

    /**
     * Get the destination-leaf receive time.
     *
     * @return Destination-leaf receive time.
     */
    Time GetDestTime() const;

    /** @copydoc Tag::GetSerializedSize */
    uint32_t GetSerializedSize() const override;

    /** @copydoc Tag::Serialize */
    void Serialize(TagBuffer i) const override;

    /** @copydoc Tag::Deserialize */
    void Deserialize(TagBuffer i) override;

    /** @copydoc Tag::Print */
    void Print(std::ostream& os) const override;

  private:
    uint8_t m_pathId;     ///< Source-leaf output interface.
    uint8_t m_sourceLeaf; ///< Source-leaf identifier.
    uint8_t m_destLeaf;   ///< Destination-leaf identifier.
    uint32_t m_tcpSeq;    ///< TCP sequence number represented by the tag.
    int64_t m_txTimeNs;   ///< Source-leaf transmit timestamp in nanoseconds.
    int64_t m_destTimeNs; ///< Destination-leaf receive timestamp in nanoseconds.
};

} // namespace ns3

#endif /* NS3_IPV4_RTT_TAG_H */
