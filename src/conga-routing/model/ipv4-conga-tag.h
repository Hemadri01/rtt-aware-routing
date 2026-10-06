/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef NS3_IPV4_CONGA_TAG
#define NS3_IPV4_CONGA_TAG

#include "ns3/tag.h"

namespace ns3
{

/**
 * @ingroup conga-routing
 * @brief Packet tag that carries CONGA path and congestion feedback.
 *
 * The forward data path records the selected uplink and local congestion
 * estimate. A returning packet can carry one remote-leaf congestion estimate
 * back to its source leaf.
 */
class Ipv4CongaTag : public Tag
{
  public:
    /** Create a CONGA tag with the implementation's default field values. */
    Ipv4CongaTag();

    /**
     * Get the TypeId for this tag.
     *
     * @return The CONGA tag TypeId.
     */
    static TypeId GetTypeId();

    /**
     * Set the uplink selected for the forward path.
     *
     * @param lbTag Output interface selected by the source leaf.
     */
    void SetLbTag(uint32_t lbTag);

    /**
     * Get the uplink selected for the forward path.
     *
     * @return Output interface selected by the source leaf.
     */
    uint32_t GetLbTag() const;

    /**
     * Set the forward-path congestion estimate.
     *
     * @param ce Quantized congestion estimate.
     */
    void SetCe(uint32_t ce);

    /**
     * Get the forward-path congestion estimate.
     *
     * @return Quantized congestion estimate.
     */
    uint32_t GetCe() const;

    /**
     * Set the uplink associated with remote feedback.
     *
     * @param fbLbTag Uplink to which the feedback applies.
     */
    void SetFbLbTag(uint32_t fbLbTag);

    /**
     * Get the uplink associated with remote feedback.
     *
     * @return Uplink to which the feedback applies.
     */
    uint32_t GetFbLbTag() const;

    /**
     * Set the remote congestion feedback metric.
     *
     * @param fbMetric Quantized congestion estimate from a remote leaf.
     */
    void SetFbMetric(uint32_t fbMetric);

    /**
     * Get the remote congestion feedback metric.
     *
     * @return Quantized congestion estimate from a remote leaf.
     */
    uint32_t GetFbMetric() const;

    /** @copydoc Tag::GetInstanceTypeId */
    TypeId GetInstanceTypeId() const override;

    /** @copydoc Tag::GetSerializedSize */
    uint32_t GetSerializedSize() const override;

    /** @copydoc Tag::Serialize */
    void Serialize(TagBuffer i) const override;

    /** @copydoc Tag::Deserialize */
    void Deserialize(TagBuffer i) override;

    /** @copydoc Tag::Print */
    void Print(std::ostream& os) const override;

  private:
    uint32_t m_lbTag;    ///< Uplink selected for the forward path.
    uint32_t m_ce;       ///< Quantized forward-path congestion estimate.
    uint32_t m_fbLbTag;  ///< Uplink associated with remote feedback.
    uint32_t m_fbMetric; ///< Quantized remote congestion feedback metric.
};

} // namespace ns3

#endif /* NS3_IPV4_CONGA_TAG */
