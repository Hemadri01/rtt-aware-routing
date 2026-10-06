/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ipv4-rtt-routing.h"

#include "ipv4-rtt-tag.h"

#include "ns3/channel.h"
#include "ns3/flow-id-tag.h"
#include "ns3/log.h"
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/tcp-header.h"

#include <algorithm>
#include <cstdlib>

constexpr bool RTT_ROUTING_DEBUG_False = false;
constexpr bool RTT_ROUTING_DEBUG_True = true;

// Per-function diagnostic switches retained for detailed routing traces.
bool LookupRouteEntriesDEBUG = RTT_ROUTING_DEBUG_False;
bool Constructipv4RouteDEBUG = RTT_ROUTING_DEBUG_False;
bool InitializeBaseRttsDEBUG = RTT_ROUTING_DEBUG_False;
bool UpdatePortRttDEBUG = RTT_ROUTING_DEBUG_False;
bool GetEffectiveRttDEBUG = RTT_ROUTING_DEBUG_False;
bool SelectWeightedPortDEBUG = RTT_ROUTING_DEBUG_False;
bool SelectPowerOf2RandomDEBUG = RTT_ROUTING_DEBUG_False;
bool SelectPowerOf2Top2DEBUG = RTT_ROUTING_DEBUG_False;
bool SelectPortDEBUG = RTT_ROUTING_DEBUG_False;
bool RouteInputDEBUG = RTT_ROUTING_DEBUG_False;
bool UpdateFlowletDEBUG = RTT_ROUTING_DEBUG_False;
bool ForwardDEBUG = RTT_ROUTING_DEBUG_False;
bool HandleOutgoingDataDEBUG = RTT_ROUTING_DEBUG_False;
bool HandleIncomingDataDEBUG = RTT_ROUTING_DEBUG_False;
bool HandleOutgoingAckDEBUG = RTT_ROUTING_DEBUG_False;
bool HandleIncomingAckDEBUG = RTT_ROUTING_DEBUG_False;
bool CleanupOldStateDEBUG = RTT_ROUTING_DEBUG_False;
bool DoDisposeDEBUG = RTT_ROUTING_DEBUG_False;

namespace ns3
{

void
Ipv4RttRouting::PrintFlowKeyLine(const Ipv4RttRouting::BidirectionalFlowKey& k) const
{
    // std::cout << k.ip1 << ":" << k.port1
    //           << "->"
    //           << k.ip2 << ":" << k.port2
    //           << "|proto=" << (int)k.protocol;

    Ipv4Address(k.ip1).Print(std::cout);
    std::cout << ":" << k.port1 << " -> ";

    Ipv4Address(k.ip2).Print(std::cout);
    std::cout << ":" << k.port2 << " | proto=" << static_cast<int>(k.protocol) << std::endl;
}

NS_LOG_COMPONENT_DEFINE("Ipv4RttRouting");

NS_OBJECT_ENSURE_REGISTERED(Ipv4RttRouting);

TypeId
Ipv4RttRouting::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::Ipv4RttRouting")
                            .SetParent<Ipv4RoutingProtocol>()
                            .SetGroupName("Internet")
                            .AddConstructor<Ipv4RttRouting>();

    return tid;
}

Ipv4RttRouting::Ipv4RttRouting()
    : m_ipv4(0),
      m_isLeaf(false),
      m_leafId(0),
      m_alpha(0.125),
      m_timeouttrue(false),
      m_C(DataRate("1Gbps")),
      m_mode(WEIGHTED_ECMP)
{
    m_rand = CreateObject<UniformRandomVariable>();
}

Ipv4RttRouting::~Ipv4RttRouting()
{
}

void
Ipv4RttRouting::SetLeafId(uint32_t leafId)
{
    m_isLeaf = true;

    m_leafId = leafId;
}

void
Ipv4RttRouting::SetSelectionMode(SelectionMode mode)
{
    m_mode = mode;
}

void
Ipv4RttRouting::SetAlpha(double alpha)
{
    m_alpha = alpha;
}

void
Ipv4RttRouting::SetTimeouttrue(bool timeouttrue)
{
    m_timeouttrue = timeouttrue;
}

void
Ipv4RttRouting::SetLinkCapacity(DataRate dataRate)
{
    m_C = dataRate;
}

void
Ipv4RttRouting::AddRoute(Ipv4Address network, Ipv4Mask mask, uint32_t port)
{
    RttRouteEntry entry;

    entry.network = network;

    entry.mask = mask;

    entry.port = port;

    m_routeEntryList.push_back(entry);
}

std::vector<Ipv4RttRouting::RttRouteEntry>
Ipv4RttRouting::LookupRouteEntries(Ipv4Address dest)
{
    if (!LookupRouteEntriesDEBUG)
    {
        std::vector<RttRouteEntry> routes;

        std::vector<RttRouteEntry>::iterator itr;

        for (itr = m_routeEntryList.begin(); itr != m_routeEntryList.end(); ++itr)
        {
            if ((*itr).mask.IsMatch(dest, (*itr).network))
            {
                routes.push_back(*itr);
            }
        }

        return routes;
    }

    else
    {
        std::cout << "[Ipv4RttRouting] LookupRouteEntries: ENTER"
                  << " dest=" << dest << " totalRoutes=" << m_routeEntryList.size() << std::endl;

        std::vector<RttRouteEntry> routes;

        std::vector<RttRouteEntry>::iterator itr;
        uint32_t checked = 0;
        uint32_t matched = 0;

        for (itr = m_routeEntryList.begin(); itr != m_routeEntryList.end(); ++itr)
        {
            checked++;

            std::cout << "[Ipv4RttRouting] Checking route " << checked
                      << " network=" << (*itr).network << " mask=" << (*itr).mask
                      << " port=" << (*itr).port << std::endl;

            if ((*itr).mask.IsMatch(dest, (*itr).network))
            {
                std::cout << "[Ipv4RttRouting] MATCH found for dest=" << dest
                          << " -> network=" << (*itr).network << " port=" << (*itr).port
                          << std::endl;

                routes.push_back(*itr);
                matched++;
            }
        }

        std::cout << "[Ipv4RttRouting] LookupRouteEntries: SUMMARY"
                  << " dest=" << dest << " checked=" << checked << " matched=" << matched
                  << " returningRoutes=" << routes.size() << std::endl;

        std::cout << "[Ipv4RttRouting] LookupRouteEntries: EXIT" << std::endl;

        return routes;
    }
}

Ptr<Ipv4Route>
Ipv4RttRouting::ConstructIpv4Route(uint32_t port, Ipv4Address dest)
{
    if (!Constructipv4RouteDEBUG)
    {
        Ptr<NetDevice> dev = m_ipv4->GetNetDevice(port);

        Ptr<Channel> channel = dev->GetChannel();

        uint32_t otherEnd = (channel->GetDevice(0) == dev) ? 1 : 0;

        Ptr<Node> nextHop = channel->GetDevice(otherEnd)->GetNode();

        uint32_t nextIf = channel->GetDevice(otherEnd)->GetIfIndex();

        Ipv4Address nextHopAddr = nextHop->GetObject<Ipv4>()->GetAddress(nextIf, 0).GetLocal();

        Ptr<Ipv4Route> route = Create<Ipv4Route>();

        route->SetOutputDevice(dev);

        route->SetGateway(nextHopAddr);

        route->SetSource(m_ipv4->GetAddress(port, 0).GetLocal());

        route->SetDestination(dest);

        return route;
    }
    else
    {
        std::cout << "[Ipv4RttRouting] ConstructIpv4Route: ENTER"
                  << " port=" << port << " dest=" << dest << std::endl;

        Ptr<NetDevice> dev = m_ipv4->GetNetDevice(port);

        std::cout << "[Ipv4RttRouting] Device fetched for port=" << port << std::endl;

        Ptr<Channel> channel = dev->GetChannel();

        std::cout << "[Ipv4RttRouting] Channel obtained for device" << std::endl;

        uint32_t otherEnd = (channel->GetDevice(0) == dev) ? 1 : 0;

        std::cout << "[Ipv4RttRouting] Other end index=" << otherEnd << std::endl;

        Ptr<Node> nextHop = channel->GetDevice(otherEnd)->GetNode();

        std::cout << "[Ipv4RttRouting] Next hop node found=" << nextHop->GetId() << std::endl;

        uint32_t nextIf = channel->GetDevice(otherEnd)->GetIfIndex();

        std::cout << "[Ipv4RttRouting] Next hop interface index=" << nextIf << std::endl;

        Ipv4Address nextHopAddr = nextHop->GetObject<Ipv4>()->GetAddress(nextIf, 0).GetLocal();

        std::cout << "[Ipv4RttRouting] Next hop IP address=" << nextHopAddr << std::endl;

        Ptr<Ipv4Route> route = Create<Ipv4Route>();

        route->SetOutputDevice(dev);
        route->SetGateway(nextHopAddr);
        route->SetSource(m_ipv4->GetAddress(port, 0).GetLocal());
        route->SetDestination(dest);

        std::cout << "[Ipv4RttRouting] Route constructed:"
                  << " src=" << route->GetSource() << " dst=" << route->GetDestination()
                  << " gw=" << route->GetGateway() << std::endl;

        std::cout << "[Ipv4RttRouting] ConstructIpv4Route: EXIT" << std::endl;

        return route;
    }
}

// Checked
void
Ipv4RttRouting::InitializeBaseRtts()
{
    if (!InitializeBaseRttsDEBUG)
    {
        std::vector<RttRouteEntry>::iterator itr;

        for (itr = m_routeEntryList.begin(); itr != m_routeEntryList.end(); ++itr)
        {
            uint32_t port = itr->port;

            if (m_baseRttMap.find(port) != m_baseRttMap.end())
            {
                continue;
            }

            Ptr<NetDevice> dev = m_ipv4->GetNetDevice(port);

            Ptr<PointToPointChannel> p2p = DynamicCast<PointToPointChannel>(dev->GetChannel());

            if (p2p)
            {
                TimeValue delayValue;

                p2p->GetAttribute("Delay", delayValue);

                Time delay = delayValue.Get();

                m_baseRttMap[port] = delay * 4; // For 2 tier leaf-spine topology, no. of hops= 2
            }
            else
            {
                m_baseRttMap[port] = MicroSeconds(100);
            }
        }
    }
    else
    {
        std::cout << "[Ipv4RttRouting] InitializeBaseRtts: ENTER"
                  << " routeCount=" << m_routeEntryList.size()
                  << " existingBaseRtts=" << m_baseRttMap.size() << std::endl;

        std::vector<RttRouteEntry>::iterator itr;

        for (itr = m_routeEntryList.begin(); itr != m_routeEntryList.end(); ++itr)
        {
            uint32_t port = itr->port;

            std::cout << "[Ipv4RttRouting] Processing route entry:"
                      << " port=" << port << " network=" << itr->network << " mask=" << itr->mask
                      << std::endl;

            if (m_baseRttMap.find(port) != m_baseRttMap.end())
            {
                std::cout << "[Ipv4RttRouting] Base RTT already exists for port=" << port
                          << " skipping" << std::endl;
                continue;
            }

            Ptr<NetDevice> dev = m_ipv4->GetNetDevice(port);

            std::cout << "[Ipv4RttRouting] Got NetDevice for port=" << port << std::endl;

            Ptr<PointToPointChannel> p2p = DynamicCast<PointToPointChannel>(dev->GetChannel());

            if (p2p)
            {
                TimeValue delayValue;

                p2p->GetAttribute("Delay", delayValue);

                Time delay = delayValue.Get();

                Time computedRtt = delay * 4;

                m_baseRttMap[port] = computedRtt;

                std::cout << "[Ipv4RttRouting] P2P link detected"
                          << " port=" << port << " oneWayDelay=" << delay.GetSeconds() << "s"
                          << " baseRtt=" << computedRtt.GetSeconds() << "s" << std::endl;
            }
            else
            {
                m_baseRttMap[port] = MicroSeconds(100);

                std::cout << "[Ipv4RttRouting] Non-P2P link fallback"
                          << " port=" << port << " baseRtt=100us" << std::endl;
            }
        }

        std::cout << "[Ipv4RttRouting] InitializeBaseRtts: EXIT"
                  << " finalBaseRttCount=" << m_baseRttMap.size() << std::endl;
    }
}

void
Ipv4RttRouting::UpdatePortRtt(uint32_t port, Time rttSample)
{
    if (!UpdatePortRttDEBUG)
    {
        PortRttState& state = m_portRttMap[port];

        state.rawRtt = rttSample;

        if (!state.initialized)
        {
            state.smoothRtt = rttSample;

            state.initialized = true;

            return;
        }

        double oldRtt = state.smoothRtt.GetSeconds();

        double sample = rttSample.GetSeconds();

        double newRtt = ((1.0 - m_alpha) * oldRtt) + (m_alpha * sample);

        state.smoothRtt = Seconds(newRtt);
    }
    else
    {
        std::cout << "[Ipv4RttRouting] UpdatePortRtt: ENTER"
                  << " port=" << port << " sampleRtt=" << rttSample.GetSeconds() << "s"
                  << std::endl;

        PortRttState& state = m_portRttMap[port];

        std::cout << "[Ipv4RttRouting] Current state:"
                  << " initialized=" << state.initialized
                  << " smoothRtt=" << state.smoothRtt.GetSeconds() << "s" << std::endl;

        state.rawRtt = rttSample;

        if (!state.initialized)
        {
            state.smoothRtt = rttSample;
            state.initialized = true;

            std::cout << "[Ipv4RttRouting] First RTT sample for port=" << port
                      << " setting smoothRtt=" << state.smoothRtt.GetSeconds() << "s" << std::endl;

            std::cout << "[Ipv4RttRouting] UpdatePortRtt: EXIT (initialized)" << std::endl;

            return;
        }

        double oldRtt = state.smoothRtt.GetSeconds();
        double sample = rttSample.GetSeconds();

        double newRtt = ((1.0 - m_alpha) * oldRtt) + (m_alpha * sample);

        state.smoothRtt = Seconds(newRtt);

        std::cout << "[Ipv4RttRouting] RTT smoothing:"
                  << " alpha=" << m_alpha << " oldRtt=" << oldRtt << " sample=" << sample
                  << " newRtt=" << newRtt << " port=" << port << std::endl;

        std::cout << "[Ipv4RttRouting] Updated smoothRtt=" << state.smoothRtt.GetSeconds() << "s"
                  << std::endl;

        std::cout << "[Ipv4RttRouting] UpdatePortRtt: EXIT" << std::endl;
    }
}

Time
Ipv4RttRouting::GetEffectiveRtt(uint32_t port)
{
    if (!GetEffectiveRttDEBUG)
    {
        std::map<uint32_t, PortRttState>::iterator itr;

        itr = m_portRttMap.find(port);

        if (itr == m_portRttMap.end())
        {
            return m_baseRttMap[port];
        }

        if (!itr->second.initialized)
        {
            return m_baseRttMap[port];
        }

        return itr->second.smoothRtt;
    }
    else
    {
        std::cout << "[Ipv4RttRouting] GetEffectiveRtt: ENTER"
                  << " port=" << port << std::endl;

        std::map<uint32_t, PortRttState>::iterator itr;

        itr = m_portRttMap.find(port);

        if (itr == m_portRttMap.end())
        {
            std::cout << "[Ipv4RttRouting] No RTT state found for port=" << port
                      << " using baseRtt=" << m_baseRttMap[port].GetSeconds() << "s" << std::endl;

            return m_baseRttMap[port];
        }

        std::cout << "[Ipv4RttRouting] RTT state found:"
                  << " initialized=" << itr->second.initialized
                  << " smoothRtt=" << itr->second.smoothRtt.GetSeconds() << "s" << std::endl;

        if (!itr->second.initialized)
        {
            std::cout << "[Ipv4RttRouting] RTT state NOT initialized for port=" << port
                      << " using baseRtt=" << m_baseRttMap[port].GetSeconds() << "s" << std::endl;

            return m_baseRttMap[port];
        }

        std::cout << "[Ipv4RttRouting] Using SMOOTH RTT for port=" << port
                  << " rtt=" << itr->second.smoothRtt.GetSeconds() << "s" << std::endl;

        std::cout << "[Ipv4RttRouting] GetEffectiveRtt: EXIT" << std::endl;

        return itr->second.smoothRtt;
    }
}

uint32_t
Ipv4RttRouting::SelectWeightedPort(const std::vector<RttRouteEntry>& routes)
{
    if (!SelectWeightedPortDEBUG)
    {
        std::vector<uint32_t> scores;

        uint32_t totalRtt = 0;

        // Sum RTTs
        for (uint32_t i = 0; i < routes.size(); ++i)
        {
            uint32_t rtt = GetEffectiveRtt(routes[i].port).GetMicroSeconds();

            totalRtt += rtt;
        }

        uint32_t totalScore = 0;

        // Reverse normalization
        for (uint32_t i = 0; i < routes.size(); ++i)
        {
            uint32_t rtt = GetEffectiveRtt(routes[i].port).GetMicroSeconds();

            uint32_t score = totalRtt - rtt;

            scores.push_back(score);

            totalScore += score;
        }

        // Fallback ECMP
        if (totalScore == 0)
        {
            uint32_t idx = m_rand->GetInteger(0, routes.size() - 1);

            return routes[idx].port;
        }

        // Weighted selection
        uint32_t hash = m_rand->GetInteger(0, totalScore - 1);

        uint32_t cumulative = 0;

        for (uint32_t i = 0; i < routes.size(); ++i)
        {
            cumulative += scores[i];

            if (hash < cumulative)
            {
                return routes[i].port;
            }
        }

        return routes.back().port;
    }
    else
    {
        std::cout << "[Ipv4RttRouting] SelectWeightedPort: ENTER"
                  << " routeCount=" << routes.size() << std::endl;

        std::vector<uint32_t> scores;

        uint32_t totalRtt = 0;

        // Sum RTTs
        for (uint32_t i = 0; i < routes.size(); ++i)
        {
            uint32_t rtt = GetEffectiveRtt(routes[i].port).GetMicroSeconds();

            std::cout << "[Ipv4RttRouting] RTT for port=" << routes[i].port << " rtt(us)=" << rtt
                      << std::endl;

            totalRtt += rtt;
        }

        std::cout << "[Ipv4RttRouting] Total RTT sum=" << totalRtt << std::endl;

        uint32_t totalScore = 0;

        // Reverse normalization
        for (uint32_t i = 0; i < routes.size(); ++i)
        {
            uint32_t rtt = GetEffectiveRtt(routes[i].port).GetMicroSeconds();

            uint32_t score = totalRtt - rtt;

            scores.push_back(score);

            totalScore += score;

            std::cout << "[Ipv4RttRouting] Score computed:"
                      << " port=" << routes[i].port << " rtt(us)=" << rtt << " score=" << score
                      << std::endl;
        }

        std::cout << "[Ipv4RttRouting] Total score=" << totalScore << std::endl;

        // Fallback ECMP
        if (totalScore == 0)
        {
            uint32_t idx = m_rand->GetInteger(0, routes.size() - 1);

            std::cout << "[Ipv4RttRouting] ECMP fallback triggered"
                      << " selectedIndex=" << idx << " port=" << routes[idx].port << std::endl;

            return routes[idx].port;
        }

        // Weighted selection
        uint32_t hash = m_rand->GetInteger(0, totalScore - 1);

        std::cout << "[Ipv4RttRouting] Random hash=" << hash << std::endl;

        uint32_t cumulative = 0;

        for (uint32_t i = 0; i < routes.size(); ++i)
        {
            cumulative += scores[i];

            std::cout << "[Ipv4RttRouting] Cumulative check:"
                      << " port=" << routes[i].port << " cumulative=" << cumulative << std::endl;

            if (hash < cumulative)
            {
                std::cout << "[Ipv4RttRouting] SELECTED port=" << routes[i].port << std::endl;

                return routes[i].port;
            }
        }

        std::cout << "[Ipv4RttRouting] WARNING: fallback to last port=" << routes.back().port
                  << std::endl;

        return routes.back().port;
    }
}

uint32_t
Ipv4RttRouting::SelectPowerOf2Random(const std::vector<RttRouteEntry>& routes)
{
    if (!SelectPowerOf2RandomDEBUG)
    {
        uint32_t r = routes.size();

        uint32_t a = m_rand->GetInteger(0, r - 1);

        uint32_t b = m_rand->GetInteger(0, r - 1);

        if (r > 1)
        {
            while (b == a)
            {
                b = m_rand->GetInteger(0, routes.size() - 1);
            }
        }

        uint32_t portA = routes[a].port;

        uint32_t portB = routes[b].port;

        Time rttA = GetEffectiveRtt(portA);

        Time rttB = GetEffectiveRtt(portB);

        if (rttA <= rttB)
        {
            return portA;
        }

        return portB;
    }
    else
    {
        uint32_t r = routes.size();

        std::cout << "[Ipv4RttRouting] SelectPowerOf2Random: ENTER"
                  << " routeCount=" << r << std::endl;

        uint32_t a = m_rand->GetInteger(0, r - 1);

        uint32_t b = m_rand->GetInteger(0, r - 1);

        if (r > 1)
        {
            while (b == a)
            {
                std::cout << "[Ipv4RttRouting] Collision on b==a (" << a << "), reselecting b"
                          << std::endl;

                b = m_rand->GetInteger(0, routes.size() - 1);
            }
        }

        std::cout << "[Ipv4RttRouting] Selected candidates:"
                  << " a=" << a << " b=" << b << std::endl;

        uint32_t portA = routes[a].port;
        uint32_t portB = routes[b].port;

        Time rttA = GetEffectiveRtt(portA);
        Time rttB = GetEffectiveRtt(portB);

        std::cout << "[Ipv4RttRouting] RTT comparison:"
                  << " portA=" << portA << " rttA=" << rttA.GetSeconds() << "s"
                  << " portB=" << portB << " rttB=" << rttB.GetSeconds() << "s" << std::endl;

        if (rttA <= rttB)
        {
            std::cout << "[Ipv4RttRouting] SELECTED portA=" << portA << std::endl;

            return portA;
        }

        std::cout << "[Ipv4RttRouting] SELECTED portB=" << portB << std::endl;

        return portB;
    }
}

// uint32_t
// Ipv4RttRouting::SelectPowerOf2Top2 (const std::vector<RttRouteEntry>& routes)
//{
//
//     if(!SelectPowerOf2Top2DEBUG)
//     {
//
//
//
//
//
//         uint32_t bestPort = 0;
//
//         uint32_t secondPort = 0;
//
//         Time bestRtt = Time::Max ();
//
//         Time secondRtt = Time::Max ();
//
//         uint32_t i;
//
//         for (i = 0; i < routes.size (); ++i)
//         {
//           uint32_t port = routes[i].port;
//
//           Time rtt = GetEffectiveRtt (port);
//
//           if (rtt < bestRtt)
//           {
//             secondRtt = bestRtt;
//
//             secondPort = bestPort;
//
//             bestRtt = rtt;
//
//             bestPort = port;
//           }
//           else if (rtt < secondRtt)
//           {
//             secondRtt = rtt;
//
//             secondPort = port;
//           }
//         }
//
//         if (secondPort == 0)
//         {
//           return bestPort;
//         }
//
//         if (m_rand->GetInteger (0, 1) == 0)
//         {
//           return bestPort;
//         }
//
//         return secondPort;

uint32_t
Ipv4RttRouting::SelectPowerOf2Top2(const std::vector<RttRouteEntry>& routes)
{
    if (!SelectPowerOf2Top2DEBUG)
    {
        if (routes.empty())
        {
            return 0;
        }

        uint32_t bestPort = 0;
        uint32_t secondPort = 0;

        Time bestRtt = Time::Max();
        Time secondRtt = Time::Max();

        bool hasBest = false;
        bool hasSecond = false;

        for (const auto& route : routes)
        {
            uint32_t port = route.port;
            Time rtt = GetEffectiveRtt(port);

            // New best route.
            if (!hasBest || rtt < bestRtt)
            {
                // Move current best to second-best.
                if (hasBest)
                {
                    secondPort = bestPort;
                    secondRtt = bestRtt;
                    hasSecond = true;
                }

                bestPort = port;
                bestRtt = rtt;
                hasBest = true;
            }
            // New second-best route.
            else if (!hasSecond || rtt < secondRtt)
            {
                secondPort = port;
                secondRtt = rtt;
                hasSecond = true;
            }
        }

        // No valid route.
        if (!hasBest)
        {
            return 0;
        }

        // Only one route available.
        if (!hasSecond)
        {
            return bestPort;
        }

        // Randomly select one of the two best RTT routes.
        if (m_rand->GetInteger(0, 1) == 0)
        {
            return bestPort;
        }

        return secondPort;
    }
    else
    {
        std::cout << "[Ipv4RttRouting] SelectPowerOf2Top2: ENTER"
                  << " routeCount=" << routes.size() << std::endl;

        uint32_t bestPort = 0;
        uint32_t secondPort = 0;

        Time bestRtt = Time::Max();
        Time secondRtt = Time::Max();

        bool hasBest = false;
        bool hasSecond = false;

        for (const auto& route : routes)
        {
            uint32_t port = route.port;

            Time rtt = GetEffectiveRtt(port);

            std::cout << "[Ipv4RttRouting] Checking port=" << port << " rtt=" << rtt.GetSeconds()
                      << "s" << std::endl;

            if (!hasBest || rtt < bestRtt)
            {
                std::cout << "[Ipv4RttRouting] New BEST found:"
                          << " port=" << port << " rtt=" << rtt.GetSeconds() << "s" << std::endl;

                // Move current best to second-best.
                if (hasBest)
                {
                    secondPort = bestPort;
                    secondRtt = bestRtt;
                    hasSecond = true;
                }

                bestPort = port;
                bestRtt = rtt;
                hasBest = true;
            }
            else if (!hasSecond || rtt < secondRtt)
            {
                std::cout << "[Ipv4RttRouting] New SECOND found:"
                          << " port=" << port << " rtt=" << rtt.GetSeconds() << "s" << std::endl;

                secondRtt = rtt;
                secondPort = port;
                hasSecond = true;
            }
        }

        std::cout << "[Ipv4RttRouting] Final ranking:"
                  << " bestPort=" << bestPort << " bestRtt=" << bestRtt.GetSeconds() << "s"
                  << " secondPort=" << secondPort << " secondRtt=" << secondRtt.GetSeconds() << "s"
                  << std::endl;

        // No valid route.
        if (!hasBest)
        {
            std::cout << "[Ipv4RttRouting] No valid route.\n";
            return 0;
        }

        // Only one route available.
        if (!hasSecond)
        {
            std::cout << "[Ipv4RttRouting] Only one valid candidate, returning bestPort="
                      << bestPort << std::endl;

            return bestPort;
        }

        // Randomly select one of the two best RTT routes.
        if (m_rand->GetInteger(0, 1) == 0)
        {
            std::cout << "[Ipv4RttRouting] SELECTED bestPort=" << bestPort << std::endl;

            return bestPort;
        }

        std::cout << "[Ipv4RttRouting] SELECTED secondPort=" << secondPort << std::endl;

        return secondPort;
    }
}

uint32_t
Ipv4RttRouting::SelectLowestRttPort(const std::vector<RttRouteEntry>& routes)
{
    if (routes.empty())
    {
        return 0;
    }

    uint32_t bestPort = routes[0].port;
    Time bestRtt = GetEffectiveRtt(bestPort);

    for (uint32_t i = 1; i < routes.size(); ++i)
    {
        uint32_t port = routes[i].port;
        Time rtt = GetEffectiveRtt(port);

        if (rtt < bestRtt)
        {
            bestRtt = rtt;
            bestPort = port;
        }
    }

    return bestPort;
}

uint32_t
Ipv4RttRouting::SelectPort(const std::vector<RttRouteEntry>& routes)
{
    if (!SelectPortDEBUG)
    {
        if (m_mode == 0)
        {
            return SelectWeightedPort(routes);
        }
        else if (m_mode == 1)
        {
            return SelectPowerOf2Random(routes);
        }
        else if (m_mode == 2)
        {
            return SelectPowerOf2Top2(routes);
        }
        else
        {
            return SelectLowestRttPort(routes);
        }
    }
    else
    {
        std::cout << "[Ipv4RttRouting] SelectPort: ENTER"
                  << " routeCount=" << routes.size() << " mode=" << m_mode << std::endl;

        uint32_t result;

        if (m_mode == 0)
        {
            std::cout << "[Ipv4RttRouting] Using SelectWeightedPort" << std::endl;

            result = SelectWeightedPort(routes);
        }
        else if (m_mode == 1)
        {
            std::cout << "[Ipv4RttRouting] Using SelectPowerOf2Random" << std::endl;

            result = SelectPowerOf2Random(routes);
        }
        else if (m_mode == 2)
        {
            std::cout << "[Ipv4RttRouting] Using SelectPowerOf2Top2" << std::endl;

            result = SelectPowerOf2Top2(routes);
        }
        else
        {
            std::cout << "[Ipv4RttRouting] Using SelectLowestRttPort" << std::endl;

            result = SelectLowestRttPort(routes);
        }

        std::cout << "[Ipv4RttRouting] SelectPort: RESULT"
                  << " mode=" << m_mode << " selectedPort=" << result << std::endl;

        return result;
    }
}

Ipv4RttRouting::BidirectionalFlowKey
Ipv4RttRouting::MakeFlowKey(Ipv4Address srcIp,
                            uint16_t srcPort,
                            Ipv4Address dstIp,
                            uint16_t dstPort,
                            uint8_t protocol)
{
    uint32_t ip1 = srcIp.Get();
    uint32_t ip2 = dstIp.Get();
    uint16_t p1 = srcPort;
    uint16_t p2 = dstPort;

    // Consistently sort so that direction A->B and B->A yield the exact same tuple
    if (ip1 > ip2 || (ip1 == ip2 && p1 > p2))
    {
        std::swap(ip1, ip2);
        std::swap(p1, p2);
    }

    return BidirectionalFlowKey{ip1, ip2, p1, p2, protocol};
}

Ptr<Ipv4Route>
Ipv4RttRouting::RouteOutput(Ptr<Packet>, const Ipv4Header&, Ptr<NetDevice>, Socket::SocketErrno&)
{
    return 0;
}

bool
Ipv4RttRouting::RouteInput(Ptr<const Packet> p,
                           const Ipv4Header& header,
                           Ptr<const NetDevice> idev,
                           const UnicastForwardCallback& ucb,
                           const MulticastForwardCallback&,
                           const LocalDeliverCallback& lcb,
                           const ErrorCallback& ecb)
{
    if (!RouteInputDEBUG)
    {
        if (m_baseRttMap.empty())
        {
            InitializeBaseRtts();
        }

        // CleanupOldState ();

        Ptr<Packet> packet = ConstCast<Packet>(p);

        // FlowIdTag flowTag;
        //
        // if (!packet->PeekPacketTag (flowTag))
        //{
        //    std::cout<< "FlowId not found\n";
        //    return false;
        //}

        // uint32_t flowId = flowTag.GetFlowId ();

        Time now = Simulator::Now();

        Ipv4Address dest = header.GetDestination();

        std::vector<RttRouteEntry> routes = LookupRouteEntries(dest);

        if (routes.empty())
        {
            ecb(packet, header, Socket::ERROR_NOROUTETOHOST);

            return false;
        }

        /*
         * Spines simply forward.
         */
        if (!m_isLeaf)
        {
            // Spines retain their existing first-matching-route forwarding.
            uint32_t port = routes[0].port;

            Ptr<Ipv4Route> route = ConstructIpv4Route(port, dest);

            ucb(route, packet, header);

            return true;
        }

        Ptr<Packet> copy = packet->Copy();

        if (header.GetProtocol() != 6)
        {
            // Not TCP.
            return false; // or let the normal routing continue.
        }

        // Ipv4Header ip;
        //
        // copy->RemoveHeader (ip);

        Ipv4Address srcIp = header.GetSource();

        Ipv4Address dstIp = header.GetDestination();

        TcpHeader tcp;

        copy->PeekHeader(tcp);

        auto flowKey = MakeFlowKey(srcIp, tcp.GetSourcePort(), dstIp, tcp.GetDestinationPort(), 6);

        // bool isAck = tcp.GetFlags () & TcpHeader::ACK;

        //       /*
        //       * Checking for pure ACK
        //       */
        //
        //       uint8_t flags = tcp.GetFlags();
        //
        //       uint32_t payload = copy->GetSize();
        //
        //       bool isAck =(flags & TcpHeader::ACK) &&!(flags & TcpHeader::SYN) &&!(flags &
        //       TcpHeader::FIN) &&!(flags & TcpHeader::RST) &&payload == 0;
        //
        //       /*
        //       * Checking for pure ACK
        //       */

        uint8_t flags = tcp.GetFlags();

        Ptr<Packet> tcpCopy = copy->Copy();
        tcpCopy->RemoveHeader(tcp);

        uint32_t packetPayloadSize = tcpCopy->GetSize();

        bool isAck = (flags & TcpHeader::ACK) && !(flags & TcpHeader::SYN) &&
                     !(flags & TcpHeader::FIN) && !(flags & TcpHeader::RST) &&
                     packetPayloadSize == 0;

        if (!isAck && (packetPayloadSize == 0))
        {
            // This is to not treat TCP Control Packets other than ACK as data

            uint32_t port = routes[0].port; // FIX

            Ptr<Ipv4Route> route = ConstructIpv4Route(port, dest);

            ucb(route, packet, header);

            return true;
        }

        uint32_t seq = tcp.GetSequenceNumber().GetValue();

        uint32_t ackNo = tcp.GetAckNumber().GetValue();

        Ipv4RttTag tag;

        bool hasTag = packet->PeekPacketTag(tag);

        // uint32_t incomingInterface = m_ipv4->GetInterfaceForDevice (ConstCast<NetDevice> (idev));

        /*
         * ====================================================
         * HOST ---> LEAF
         * ====================================================
         */
        if (!hasTag)
        {
            /*
             * Destination is attached to this leaf (i.e inra-leaf traffic)
             * No RTT tag.
             * No path selection.
             * Just forward to the host port.
             */

            if (routes.size() == 1)
            {
                Ptr<Ipv4Route> route = ConstructIpv4Route(routes[0].port, dest);

                ucb(route, packet, header);

                return true;
            }
            // else
            //{
            //  Destination is on another leaf.
            //  Choose spine/path.
            //  Attach RTT tag.
            //}

            if (isAck)
            {
                HandleOutgoingAck(packet, flowKey, ackNo, now, routes, header, ucb);

                return true;
            }

            HandleOutgoingData(packet, flowKey, seq, now, routes, header, ucb);

            return true;
        }

        /*
         * ====================================================
         * SPINE ---> LEAF
         * ====================================================
         */
        else
        {
            if (isAck)
            {
                HandleIncomingAck(packet, flowKey, ackNo, tag, now);

                packet->RemovePacketTag(tag);

                // lcb(packet, header, incomingInterface);

                uint32_t port = routes[0].port;

                Ptr<Ipv4Route> route = ConstructIpv4Route(port, dest);

                ucb(route, packet, header);

                return true;
            }

            HandleIncomingData(packet, flowKey, seq, tag, now);

            packet->RemovePacketTag(tag);

            // lcb(packet, header, incomingInterface);

            uint32_t port = routes[0].port;

            Ptr<Ipv4Route> route = ConstructIpv4Route(port, dest);

            ucb(route, packet, header);

            return true;
        }

        uint32_t port = routes[0].port;

        Ptr<Ipv4Route> route = ConstructIpv4Route(port, dest);

        ucb(route, packet, header);

        return true;
    }
    else
    {
        std::cout << "***************************************************************\n";

        std::cout << "[Ipv4RttRouting] RouteInput: ENTER"
                  << " node=" << m_ipv4->GetObject<Node>()->GetId()
                  << " dest=" << header.GetDestination() << std::endl;

        if (m_baseRttMap.empty())
        {
            std::cout << "[Ipv4RttRouting] Base RTT map empty, initializing" << std::endl;

            InitializeBaseRtts();
        }

        // CleanupOldState ();

        Ptr<Packet> packet = ConstCast<Packet>(p);

        // FlowIdTag flowTag;
        //
        // if (!packet->PeekPacketTag (flowTag))
        //{
        //    std::cout << "[Ipv4RttRouting] Missing FlowIdTag -> DROP"
        //              << std::endl;
        //
        //    return false;
        //}

        // uint32_t flowId = flowTag.GetFlowId ();

        Time now = Simulator::Now();

        Ipv4Address dest = header.GetDestination();

        std::vector<RttRouteEntry> routes = LookupRouteEntries(dest);

        std::cout << "[Ipv4RttRouting] routes found="
                  << routes.size()
                  //<< " flowId=" << flowId
                  << " node=" << m_ipv4->GetObject<Node>()->GetId() << std::endl;

        if (routes.empty())
        {
            std::cout << "[Ipv4RttRouting] NO ROUTE -> ERROR_NOROUTETOHOST" << std::endl;

            ecb(packet, header, Socket::ERROR_NOROUTETOHOST);
            return false;
        }

        /*
         * Spines simply forward.
         */
        if (!m_isLeaf)
        {
            uint32_t port = routes[0].port;

            std::cout << "[Ipv4RttRouting] SPINE forwarding"
                      //<< " flowId=" << flowId
                      //<< " selectedIndex=" << (flowId % routes.size ())
                      << " port=" << port << std::endl;

            Ptr<Ipv4Route> route = ConstructIpv4Route(port, dest);

            ucb(route, packet, header);

            return true;
        }

        Ptr<Packet> copy = packet->Copy();

        if (header.GetProtocol() != 6)
        {
            // Not TCP.
            std::cout << "Not TCP traffic\n";
            return false; // or let the normal routing continue.
        }

        // Ipv4Header ip;
        // copy->RemoveHeader (ip);

        Ipv4Address srcIp = header.GetSource();

        Ipv4Address dstIp = header.GetDestination();

        std::cout << "\n========== PACKET HEADER DEBUG ==========\n";

        std::cout << "Packet size = " << copy->GetSize() << " bytes\n";

        Ipv4Header testIp;
        uint32_t ipHeaderSize = copy->PeekHeader(testIp);

        std::cout << "IPv4 PeekHeader size = " << ipHeaderSize << "\n";

        std::cout << "IPv4 src = " << testIp.GetSource() << "\n";

        std::cout << "IPv4 dst = " << testIp.GetDestination() << "\n";

        std::cout << "IPv4 protocol = " << (uint32_t)testIp.GetProtocol() << "\n";

        TcpHeader testTcp;
        uint32_t tcpHeaderSize = copy->PeekHeader(testTcp);

        std::cout << "TCP PeekHeader size = " << tcpHeaderSize << "\n";

        std::cout << "TCP srcPort = " << testTcp.GetSourcePort() << "\n";

        std::cout << "TCP dstPort = " << testTcp.GetDestinationPort() << "\n";

        std::cout << "TCP flags = " << (uint32_t)testTcp.GetFlags() << "\n";

        std::cout << "=========================================\n";

        TcpHeader tcp;
        copy->PeekHeader(tcp);

        auto flowKey = MakeFlowKey(srcIp, tcp.GetSourcePort(), dstIp, tcp.GetDestinationPort(), 6);

        uint8_t flags = tcp.GetFlags();

        Ptr<Packet> tcpCopy = copy->Copy();

        tcpCopy->RemoveHeader(tcp);

        uint32_t packetPayloadSize = tcpCopy->GetSize();

        std::cout << "Flags=" << (uint32_t)flags << " ACK=" << ((flags & TcpHeader::ACK) ? 1 : 0)
                  << " SYN=" << ((flags & TcpHeader::SYN) ? 1 : 0)
                  << " FIN=" << ((flags & TcpHeader::FIN) ? 1 : 0)
                  << " RST=" << ((flags & TcpHeader::RST) ? 1 : 0)
                  << " PSH=" << ((flags & TcpHeader::PSH) ? 1 : 0) << std::endl;

        // bool isAck = (tcp.GetFlags() & TcpHeader::ACK) && packetPayloadSize == 0;

        bool isAck = (flags & TcpHeader::ACK) && !(flags & TcpHeader::SYN) &&
                     !(flags & TcpHeader::FIN) && !(flags & TcpHeader::RST) &&
                     packetPayloadSize == 0;

        if (!isAck && (packetPayloadSize == 0))
        {
            // This is to not treat TCP Control Packets other than ACK as data

            uint32_t port = routes[0].port; // FIX

            Ptr<Ipv4Route> route = ConstructIpv4Route(port, dest);

            ucb(route, packet, header);

            return true;
        }

        uint32_t seq = tcp.GetSequenceNumber().GetValue();
        uint32_t ackNo = tcp.GetAckNumber().GetValue();

        Ipv4RttTag tag;
        bool hasTag = packet->PeekPacketTag(tag);

        // uint32_t incomingInterface = m_ipv4->GetInterfaceForDevice (ConstCast<NetDevice> (idev));

        std::cout << "[Ipv4RttRouting] LEAF processing"
                  << " hasTag=" << hasTag << " isAck=" << isAck << " seq=" << seq
                  << " ack=" << ackNo << std::endl;

        /*
         * ====================================================
         * HOST ---> LEAF
         * ====================================================
         */
        if (!hasTag)
        {
            std::cout << "[Ipv4RttRouting] Currently at SOURCE LEAF" << std::endl;

            /*
             * Destination is attached to this leaf (i.e inra-leaf traffic)
             * No RTT tag.
             * No path selection.
             * Just forward to the host port.
             */

            if (routes.size() == 1)
            {
                std::cout << "[Ipv4RttRouting] Intra-Rack Traffic\n";

                Ptr<Ipv4Route> route = ConstructIpv4Route(routes[0].port, dest);

                ucb(route, packet, header);

                return true;
            }
            // else
            //{
            //  Destination is on another leaf.
            //  Choose spine/path.
            //  Attach RTT tag.
            //}

            if (isAck)
            {
                std::cout << "[Ipv4RttRouting] Outgoing ACK" << std::endl;

                HandleOutgoingAck(packet, flowKey, ackNo, now, routes, header, ucb);

                return true;
            }

            std::cout << "[Ipv4RttRouting] Outgoing DATA" << std::endl;

            HandleOutgoingData(packet, flowKey, seq, now, routes, header, ucb);

            return true;
        }

        /*
         * ====================================================
         * SPINE ---> LEAF
         * ====================================================
         */
        else
        {
            std::cout << "[Ipv4RttRouting] Currently at DEST LEAF" << std::endl;

            if (isAck)
            {
                std::cout << "[Ipv4RttRouting] Incoming ACK" << std::endl;

                HandleIncomingAck(packet, flowKey, ackNo, tag, now);

                packet->RemovePacketTag(tag);

                // std::cout   << "LCB node="
                //             << m_ipv4->GetObject<Node>()->GetId()
                //             << " dst="
                //             << header.GetDestination()
                //             << std::endl;

                // lcb(packet, header, incomingInterface);

                uint32_t port = routes[0].port;

                Ptr<Ipv4Route> route = ConstructIpv4Route(port, dest);

                ucb(route, packet, header);

                std::cout << "UCB node=" << m_ipv4->GetObject<Node>()->GetId()
                          << " dst=" << header.GetDestination() << " port=" << port << std::endl;

                return true;
            }

            std::cout << "[Ipv4RttRouting] Incoming DATA" << std::endl;

            HandleIncomingData(packet, flowKey, seq, tag, now);

            packet->RemovePacketTag(tag);

            // std::cout   << "LCB node="
            //             << m_ipv4->GetObject<Node>()->GetId()
            //             << " dst="
            //             << header.GetDestination()
            //             << std::endl;

            // lcb(packet, header, incomingInterface);

            uint32_t port = routes[0].port;

            Ptr<Ipv4Route> route = ConstructIpv4Route(port, dest);

            ucb(route, packet, header);

            std::cout << "UCB node=" << m_ipv4->GetObject<Node>()->GetId()
                      << " dst=" << header.GetDestination() << " port=" << port << std::endl;

            return true;
        }

        std::cout << "***************************************************************\n";

        std::cout << "Something Went Wrong\n";

        std::cout << "***************************************************************\n";

        return false;
    }
}

void
Ipv4RttRouting::UpdateFlowlet(const BidirectionalFlowKey& flowKey, uint32_t port, Time now)
{
    if (!UpdateFlowletDEBUG)
    {
        Flowlet* f;

        // std::cout << "entered UpdateFlowlet"
        //           << " flowKey=" ; PrintFlowKeyLine(flowKey);
        // std::cout << " port=" << port
        //           << " now=" << now.GetNanoSeconds()
        //           << std::endl;

        auto it = m_flowletTable.find(flowKey);

        // std::cout << "table size = "
        //           << m_flowletTable.size()
        //           << std::endl;

        if (it == m_flowletTable.end())
        {
            // std::cout << "new flowlet" << std::endl;

            f = new Flowlet;

            // std::cout << "allocated f=" << f << std::endl;

            m_flowletTable[flowKey] = f;

            // std::cout << "stored in map" << std::endl;
        }
        else
        {
            // std::cout << "existing flowlet" << std::endl;

            f = it->second;

            // std::cout << "retrieved f=" << f << std::endl;
        }

        f->port = port;
        f->activeTime = now;
    }
    else
    {
        std::cout << "[Ipv4RttRouting] UpdateFlowlet: ENTER"
                  << " flowKey=";
        PrintFlowKeyLine(flowKey);
        std::cout << " port=" << port << " now=" << now.GetNanoSeconds() << std::endl;

        std::cout << "[Ipv4RttRouting] flowletTable size=" << m_flowletTable.size() << std::endl;

        Flowlet* f;

        auto it = m_flowletTable.find(flowKey);

        if (it == m_flowletTable.end())
        {
            std::cout << "[Ipv4RttRouting] NEW flowlet created"
                      << " flowKey=";
            PrintFlowKeyLine(flowKey);
            std::cout << std::endl;

            f = new Flowlet;

            std::cout << "[Ipv4RttRouting] allocated Flowlet* f=" << f << std::endl;

            m_flowletTable[flowKey] = f;

            std::cout << "[Ipv4RttRouting] inserted into map"
                      << " flowKey=";
            PrintFlowKeyLine(flowKey);
            std::cout << " ptr=" << f << std::endl;
        }
        else
        {
            std::cout << "[Ipv4RttRouting] EXISTING flowlet found"
                      << " flowKey=";
            PrintFlowKeyLine(flowKey);
            std::cout << std::endl;

            f = it->second;

            std::cout << "[Ipv4RttRouting] retrieved Flowlet* f=" << f << std::endl;
        }

        std::cout << "[Ipv4RttRouting] updating flowlet state:"
                  << " oldPort=" << (it != m_flowletTable.end() ? it->second->port : -1)
                  << " newPort=" << port << " oldActiveTime="
                  << (it != m_flowletTable.end() ? it->second->activeTime.GetNanoSeconds() : -1)
                  << std::endl;

        f->port = port;
        f->activeTime = now;

        std::cout << "[Ipv4RttRouting] updated flowlet:"
                  << " flowKey=";
        PrintFlowKeyLine(flowKey);
        std::cout << " port=" << f->port << " activeTime=" << f->activeTime.GetNanoSeconds()
                  << std::endl;
    }
}

void
Ipv4RttRouting::Forward(Ptr<Packet> packet,
                        uint32_t port,
                        const Ipv4Header& header,
                        UnicastForwardCallback ucb)
{
    if (!ForwardDEBUG)
    {
        Ptr<Ipv4Route> route = ConstructIpv4Route(port, header.GetDestination());

        ucb(route, packet, header);
    }
    else
    {
        std::cout << "[Ipv4RttRouting] Forward: ENTER"
                  << " node=" << m_ipv4->GetObject<Node>()->GetId() << " port=" << port
                  << " dest=" << header.GetDestination() << " packetSize=" << packet->GetSize()
                  << std::endl;

        Ptr<Ipv4Route> route = ConstructIpv4Route(port, header.GetDestination());

        std::cout << "[Ipv4RttRouting] Forward: route constructed"
                  << " src=" << route->GetSource() << " dst=" << route->GetDestination()
                  << " gw=" << route->GetGateway() << " outDevIndex=" << route->GetOutputDevice()
                  << std::endl;

        std::cout << "[Ipv4RttRouting] Forward: calling ucb" << std::endl;

        ucb(route, packet, header);

        std::cout << "[Ipv4RttRouting] Forward: EXIT" << std::endl;
    }
}

void
Ipv4RttRouting::HandleOutgoingData(Ptr<Packet> packet,
                                   const BidirectionalFlowKey& flowKey,
                                   uint32_t seq,
                                   Time now,
                                   const std::vector<RttRouteEntry>& routes,
                                   const Ipv4Header& header,
                                   UnicastForwardCallback ucb)
{
    if (!HandleOutgoingDataDEBUG)
    {
        uint32_t selectedPort;

        // Flowlet *f = m_flowletTable[flowId];

        Flowlet* f = nullptr;

        auto it = m_flowletTable.find(flowKey);

        if (it != m_flowletTable.end())
        {
            f = it->second;
        }

        bool reuse = false;

        if (f)
        {
            Time timeout;

            if (!m_timeouttrue)
            {
                timeout = GetEffectiveRtt(f->port);
            }
            else
            {
                timeout = MicroSeconds(500);
            }

            if ((now - f->activeTime) <= timeout)
            {
                reuse = true;

                selectedPort = f->port;

                f->activeTime = now;
            }
        }

        if (!reuse)
        {
            selectedPort = SelectPort(routes);

            // PrintFlowKeyLine(flowKey);

            // std::cout << " "<<selectedPort<<" "<<now<<"\n";

            UpdateFlowlet(flowKey, selectedPort, now);
        }

        PacketState state;

        state.seq = seq;

        state.pathId = selectedPort;

        state.sourceLeaf = m_leafId;

        state.txTime = now;

        state.lastSeen = now;

        m_packetTable[flowKey][seq] = state;

        Ipv4RttTag tag;

        tag.SetPathId(selectedPort);

        tag.SetSourceId(m_leafId);

        tag.SetTcpSeq(seq);

        tag.SetTxTime(now);

        packet->AddPacketTag(tag);

        Forward(packet, selectedPort, header, ucb);
    }
    else
    {
        std::cout << "[Ipv4RttRouting] HandleOutgoingData: ENTER"
                  << " flowKey=";
        PrintFlowKeyLine(flowKey);
        std::cout << " seq=" << seq << " time=" << now.GetNanoSeconds()
                  << " routeCount=" << routes.size() << std::endl;

        uint32_t selectedPort;

        Flowlet* f = nullptr;

        auto it = m_flowletTable.find(flowKey);

        if (it != m_flowletTable.end())
        {
            f = it->second;

            std::cout << "[Ipv4RttRouting] Flowlet found"
                      << " port=" << f->port << " lastActive=" << f->activeTime.GetNanoSeconds()
                      << std::endl;
        }
        else
        {
            std::cout << "[Ipv4RttRouting] No existing flowlet for flowKey=";
            PrintFlowKeyLine(flowKey);
            std::cout << std::endl;
        }

        bool reuse = false;

        if (f)
        {
            Time timeout;

            if (!m_timeouttrue)
            {
                timeout = GetEffectiveRtt(f->port);
            }
            else
            {
                timeout = MicroSeconds(500);
            }

            std::cout << "[Ipv4RttRouting] Flowlet reuse check"
                      << " port=" << f->port << " timeout=" << timeout.GetSeconds() << "s"
                      << " delta=" << (now - f->activeTime).GetSeconds() << "s" << std::endl;

            if ((now - f->activeTime) <= timeout)
            {
                reuse = true;
                selectedPort = f->port;

                // A Fix
                f->activeTime = now;

                std::cout << "[Ipv4RttRouting] FLOWLET REUSE"
                          << " flowKey=";
                PrintFlowKeyLine(flowKey);
                std::cout << " port=" << selectedPort << std::endl;
            }
        }

        if (!reuse)
        {
            selectedPort = SelectPort(routes);

            std::cout << "[Ipv4RttRouting] NEW PATH SELECTION"
                      << " flowKey=";
            PrintFlowKeyLine(flowKey);
            std::cout << " selectedPort=" << selectedPort << " time=" << now.GetNanoSeconds()
                      << std::endl;

            UpdateFlowlet(flowKey, selectedPort, now);
        }

        std::cout << "[Ipv4RttRouting] FINAL DECISION"
                  << " flowKey=";
        PrintFlowKeyLine(flowKey);
        std::cout << " seq=" << seq << " port=" << selectedPort << " reuse=" << reuse << std::endl;

        PacketState state;

        state.seq = seq;
        state.pathId = selectedPort;
        state.sourceLeaf = m_leafId;
        state.txTime = now;
        state.lastSeen = now;

        m_packetTable[flowKey][seq] = state;

        Ipv4RttTag tag;

        tag.SetPathId(selectedPort);
        tag.SetSourceId(m_leafId);
        tag.SetTcpSeq(seq);
        tag.SetTxTime(now);

        packet->AddPacketTag(tag);

        std::cout << "[Ipv4RttRouting] TAG ADDED"
                  << " pathId=" << selectedPort << " flowKey=";
        PrintFlowKeyLine(flowKey);
        std::cout << " seq=" << seq << std::endl;

        Forward(packet, selectedPort, header, ucb);

        std::cout << "[Ipv4RttRouting] HandleOutgoingData: EXIT" << std::endl;
    }
}

void
Ipv4RttRouting::HandleIncomingData(Ptr<Packet> packet,
                                   const BidirectionalFlowKey& flowKey,
                                   uint32_t seq,
                                   const Ipv4RttTag& tag,
                                   Time now)
{
    if (!HandleIncomingDataDEBUG)
    {
        // ReverseInfo &info = m_reverseInfo[flowId];
        //
        // info.destLeaf = m_leafId;
        //
        // info.destTime = now;
        //
        // info.valid = true;

        PacketState state;

        state.seq = seq;

        state.pathId = tag.GetPathId();

        state.sourceLeaf = tag.GetSourceId();

        state.txTime = tag.GetTxTime();

        state.lastSeen = now;

        m_packetTable[flowKey][seq] = state;
    }
    else
    {
        std::cout << "[Ipv4RttRouting] HandleIncomingData: ENTER"
                  << " flowKey=";
        PrintFlowKeyLine(flowKey);
        std::cout << " seq=" << seq << " time=" << now.GetNanoSeconds() << std::endl;

        // ReverseInfo &info = m_reverseInfo[flowId];
        //
        // std::cout << "[Ipv4RttRouting] ReverseInfo before update:"
        //          << " valid=" << info.valid
        //          << " prevDestLeaf=" << info.destLeaf
        //          << " prevDestTime=" << info.destTime.GetNanoSeconds ()
        //          << std::endl;
        //
        // info.destLeaf = m_leafId;
        // info.destTime = now;
        // info.valid = true;
        //
        // std::cout << "[Ipv4RttRouting] ReverseInfo updated:"
        //          << " flowId=" << flowId
        //          << " destLeaf=" << info.destLeaf
        //          << " destTime=" << info.destTime.GetNanoSeconds ()
        //          << " valid=" << info.valid
        //          << std::endl;

        PacketState state;

        state.seq = seq;
        state.pathId = tag.GetPathId();
        state.sourceLeaf = tag.GetSourceId();
        state.txTime = tag.GetTxTime();
        state.lastSeen = now;

        std::cout << "[Ipv4RttRouting] Incoming DATA state:"
                  << " pathId=" << state.pathId << " sourceLeaf=" << state.sourceLeaf
                  << " txTime=" << state.txTime.GetNanoSeconds()
                  << " lastSeen=" << state.lastSeen.GetNanoSeconds() << std::endl;

        m_packetTable[flowKey][seq] = state;

        std::cout << "[Ipv4RttRouting] Packet table updated"
                  << " flowKey=";
        PrintFlowKeyLine(flowKey);
        std::cout << " seq=" << seq << std::endl;

        std::cout << "[Ipv4RttRouting] HandleIncomingData: EXIT" << std::endl;
    }
}

void
Ipv4RttRouting::HandleOutgoingAck(Ptr<Packet> packet,
                                  const BidirectionalFlowKey& flowKey,
                                  uint32_t ackNo,
                                  Time now,
                                  const std::vector<RttRouteEntry>& routes,
                                  const Ipv4Header& header,
                                  UnicastForwardCallback ucb)
{
    if (!HandleOutgoingAckDEBUG)
    {
        std::map<uint32_t, PacketState>& table = m_packetTable[flowKey];

        uint32_t bestSeq = 0;

        bool found = false;

        for (auto it = table.begin(); it != table.end(); ++it)
        {
            if (it->first < ackNo)
            {
                bestSeq = it->first;

                found = true;
            }
        }

        if (!found)
        {
            std::cout << "localLeaf=" << m_leafId << std::endl;

            PrintFlowKeyLine(flowKey);
            std::cout << "No existing flow for ACK\n";

            // This is most probably for the ACK of the TCP Handshake since the previous messages
            // are not tagged

            uint32_t port = routes[0].port; // FIX

            Ptr<Ipv4Route> route = ConstructIpv4Route(port, header.GetDestination());

            ucb(route, packet, header);

            return;
        }

        PacketState state = table[bestSeq];

        state.lastSeen = now;

        Ipv4RttTag tag;

        tag.SetPathId(state.pathId);

        tag.SetSourceId(state.sourceLeaf);

        tag.SetDestId(m_leafId);

        tag.SetTcpSeq(bestSeq);

        tag.SetTxTime(state.txTime);

        tag.SetDestTime(now);

        packet->AddPacketTag(tag);

        uint32_t selectedPort = state.pathId;

        Forward(packet, selectedPort, header, ucb);
    }
    else
    {
        std::cout << "[Ipv4RttRouting] HandleOutgoingAck: ENTER"
                  << " flowKey=";
        PrintFlowKeyLine(flowKey);
        std::cout << " ackNo=" << ackNo << " time=" << now.GetNanoSeconds()
                  << " routeCount=" << routes.size() << std::endl;

        std::map<uint32_t, PacketState>& table = m_packetTable[flowKey];

        std::cout << "[Ipv4RttRouting] Packet table size=" << table.size() << std::endl;

        uint32_t bestSeq = 0;
        bool found = false;

        for (auto it = table.begin(); it != table.end(); ++it)
        {
            // std::cout << "[Ipv4RttRouting] checking seq="
            //           << it->first
            //           << " <= ackNo=" << ackNo
            //           << std::endl;

            if (it->first < ackNo)
            {
                bestSeq = it->first;
                found = true;

                // std::cout << "[Ipv4RttRouting] candidate match seq="
                //           << bestSeq
                //           << std::endl;
            }
        }

        if (!found)
        {
            std::cout << "[Ipv4RttRouting] NO matching seq for ACK"
                      << " flowKey=";
            PrintFlowKeyLine(flowKey);
            std::cout << " ackNo=" << ackNo << std::endl;

            if (!found)
            {
                std::cout << "localLeaf=" << m_leafId << std::endl;

                PrintFlowKeyLine(flowKey);
                std::cout << "No existing flow for ACK\n";

                // This is most probably for the ACK of the TCP Handshake since the previous
                // messages are not tagged. This ACK cld be either in it's SOURCE LEAF or in it's
                // DESTINATION LEAF since it !hasTag and isAck.

                uint32_t port = routes[0].port; // FIX

                Ptr<Ipv4Route> route = ConstructIpv4Route(port, header.GetDestination());

                ucb(route, packet, header);

                return;
            }
        }

        PacketState state = table[bestSeq];

        std::cout << "[Ipv4RttRouting] selected packet state:"
                  << " seq=" << bestSeq << " pathId=" << state.pathId
                  << " sourceLeaf=" << state.sourceLeaf
                  << " txTime=" << state.txTime.GetNanoSeconds() << std::endl;

        Ipv4RttTag tag;

        tag.SetPathId(state.pathId);
        tag.SetSourceId(state.sourceLeaf);
        tag.SetDestId(m_leafId);
        tag.SetTcpSeq(bestSeq);
        tag.SetTxTime(state.txTime);
        tag.SetDestTime(now);

        packet->AddPacketTag(tag);

        std::cout << "[Ipv4RttRouting] ACK TAG CREATED"
                  << " pathId=" << state.pathId << " seq=" << bestSeq
                  << " txTime=" << state.txTime.GetNanoSeconds()
                  << " destTime=" << now.GetNanoSeconds() << std::endl;

        uint32_t selectedPort = state.pathId;

        std::cout << "[Ipv4RttRouting] FORWARD ACK"
                  << " port=" << selectedPort << " flowKey=";
        PrintFlowKeyLine(flowKey);
        std::cout << std::endl;

        Forward(packet, selectedPort, header, ucb);

        std::cout << "[Ipv4RttRouting] HandleOutgoingAck: EXIT" << std::endl;
    }
}

void
Ipv4RttRouting::HandleIncomingAck(Ptr<Packet> packet,
                                  const BidirectionalFlowKey& flowKey,
                                  uint32_t ackNo,
                                  const Ipv4RttTag& tag,
                                  Time now)
{
    if (!HandleIncomingAckDEBUG)
    {
        if (tag.GetSourceId() != m_leafId)
        {
            std::cout << "localLeaf=" << m_leafId << " tag.source=" << tag.GetSourceId()
                      << " tag.dest=" << tag.GetDestId() << " tag.path=" << tag.GetPathId()
                      << " tag.tcpseq=" << tag.GetTcpSeq() << " tag.txtime=" << tag.GetTxTime()
                      << " tag.desttime=" << tag.GetDestTime() << std::endl;

            PrintFlowKeyLine(flowKey);
            std::cout << "SourceId not of this Leaf\n";
            return;
        }

        Time rtt = now - tag.GetTxTime();

        UpdatePortRtt(tag.GetPathId(), rtt);

        // m_reverseInfo[flowId].destLeaf = tag.GetDestId ();
        //
        // m_reverseInfo[flowId].destTime = tag.GetDestTime ();
        //
        // m_reverseInfo[flowId].valid = true;
    }
    else
    {
        std::cout << "[Ipv4RttRouting] HandleIncomingAck: ENTER"
                  << " flowKey=";
        PrintFlowKeyLine(flowKey);
        std::cout << " ackNo=" << ackNo << " time=" << now.GetNanoSeconds() << std::endl;

        std::cout << "[Ipv4RttRouting] TAG info:"
                  << " srcLeaf=" << tag.GetSourceId() << " destLeaf=" << tag.GetDestId()
                  << " pathId=" << tag.GetPathId() << " txTime=" << tag.GetTxTime().GetNanoSeconds()
                  << " destTime=" << tag.GetDestTime().GetNanoSeconds() << std::endl;

        if (tag.GetSourceId() != m_leafId)
        {
            std::cout << "[Ipv4RttRouting] IGNORE ACK (not origin leaf)"
                      << " tagSource=" << tag.GetSourceId() << " localLeaf=" << m_leafId
                      << std::endl;

            return;
        }

        Time rtt = now - tag.GetTxTime();

        std::cout << "[Ipv4RttRouting] RTT computed:"
                  << " pathId=" << tag.GetPathId() << " rtt=" << rtt.GetSeconds() << "s"
                  << std::endl;

        UpdatePortRtt(tag.GetPathId(), rtt);

        std::cout << "[Ipv4RttRouting] RTT updated for port=" << tag.GetPathId() << std::endl;

        // m_reverseInfo[flowId].destLeaf = tag.GetDestId ();
        // m_reverseInfo[flowId].destTime = tag.GetDestTime ();
        // m_reverseInfo[flowId].valid = true;
        //
        // std::cout << "[Ipv4RttRouting] ReverseInfo updated:"
        //          << " flowId=" << flowId
        //          << " destLeaf=" << m_reverseInfo[flowId].destLeaf
        //          << " destTime=" << m_reverseInfo[flowId].destTime.GetNanoSeconds ()
        //          << " valid=" << m_reverseInfo[flowId].valid
        //          << std::endl;

        std::cout << "[Ipv4RttRouting] HandleIncomingAck: EXIT" << std::endl;
    }
}

void
Ipv4RttRouting::CleanupOldState()
{
    if (!CleanupOldStateDEBUG)
    {
        Time now = Simulator::Now();

        for (auto flowIt = m_packetTable.begin(); flowIt != m_packetTable.end();)
        {
            auto& table = flowIt->second;

            for (auto pktIt = table.begin(); pktIt != table.end();)
            {
                Time timeout = MilliSeconds(100);

                if ((now - pktIt->second.lastSeen) > timeout)
                {
                    pktIt = table.erase(pktIt);
                }
                else
                {
                    ++pktIt;
                }
            }

            if (table.empty())
            {
                flowIt = m_packetTable.erase(flowIt);
            }
            else
            {
                ++flowIt;
            }
        }
    }
    else
    {
        Time now = Simulator::Now();

        std::cout << "[Ipv4RttRouting] CleanupOldState: ENTER"
                  << " packetTableFlows=" << m_packetTable.size()
                  << " time=" << now.GetNanoSeconds() << std::endl;

        for (auto flowIt = m_packetTable.begin(); flowIt != m_packetTable.end();)
        {
            auto flowKey = flowIt->first;
            auto& table = flowIt->second;

            std::cout << "[Ipv4RttRouting] Flow check:"
                      << " flowKey=";
            PrintFlowKeyLine(flowKey);
            std::cout << " packetCount=" << table.size() << std::endl;

            for (auto pktIt = table.begin(); pktIt != table.end();)
            {
                Time timeout = MilliSeconds(100);

                Time age = now - pktIt->second.lastSeen;

                if (age > timeout)
                {
                    std::cout << "[Ipv4RttRouting] DELETE packet state:"
                              << " flowKey=";
                    PrintFlowKeyLine(flowKey);
                    std::cout << " seq=" << pktIt->first << " age=" << age.GetMilliSeconds() << "ms"
                              << std::endl;

                    pktIt = table.erase(pktIt);
                }
                else
                {
                    ++pktIt;
                }
            }

            if (table.empty())
            {
                std::cout << "[Ipv4RttRouting] Removing empty flow:"
                          << " flowKey=";
                PrintFlowKeyLine(flowKey);
                std::cout << std::endl;

                flowIt = m_packetTable.erase(flowIt);
            }
            else
            {
                std::cout << "[Ipv4RttRouting] Flow retained:"
                          << " flowKey=";
                PrintFlowKeyLine(flowKey);
                std::cout << " remainingPackets=" << table.size() << std::endl;

                ++flowIt;
            }
        }

        std::cout << "[Ipv4RttRouting] CleanupOldState: EXIT"
                  << " remainingFlows=" << m_packetTable.size() << std::endl;
    }
}

void
Ipv4RttRouting::NotifyInterfaceUp(uint32_t)
{
}

void
Ipv4RttRouting::NotifyInterfaceDown(uint32_t)
{
}

void
Ipv4RttRouting::NotifyAddAddress(uint32_t, Ipv4InterfaceAddress)
{
}

void
Ipv4RttRouting::NotifyRemoveAddress(uint32_t, Ipv4InterfaceAddress)
{
}

void
Ipv4RttRouting::SetIpv4(Ptr<Ipv4> ipv4)
{
    m_ipv4 = ipv4;

    // InitializeBaseRtts ();
}

void
Ipv4RttRouting::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
}

void
Ipv4RttRouting::DoDispose()
{
    if (!DoDisposeDEBUG)
    {
        std::map<BidirectionalFlowKey, Flowlet*>::iterator itr;

        for (itr = m_flowletTable.begin(); itr != m_flowletTable.end(); ++itr)
        {
            delete itr->second;
        }

        m_flowletTable.clear();

        // m_flowToPort.clear ();

        m_portRttMap.clear();

        m_baseRttMap.clear();

        m_packetTable.clear();

        m_reverseInfo.clear();

        m_ipv4 = 0;

        Ipv4RoutingProtocol::DoDispose();
    }
    else
    {
        std::cout << "[Ipv4RttRouting] DoDispose: ENTER" << std::endl;

        std::cout << "[Ipv4RttRouting] flowletTable size=" << m_flowletTable.size() << std::endl;

        std::map<BidirectionalFlowKey, Flowlet*>::iterator itr;

        uint32_t deletedFlowlets = 0;

        for (itr = m_flowletTable.begin(); itr != m_flowletTable.end(); ++itr)
        {
            delete itr->second;
            deletedFlowlets++;
        }

        std::cout << "[Ipv4RttRouting] deleted Flowlets=" << deletedFlowlets << std::endl;

        m_flowletTable.clear();

        std::cout << "[Ipv4RttRouting] cleared flowletTable" << std::endl;

        // m_flowToPort.clear ();

        std::cout << "[Ipv4RttRouting] clearing RTT and state maps" << std::endl;

        std::cout << "[Ipv4RttRouting] portRttMap size=" << m_portRttMap.size()
                  << " baseRttMap size=" << m_baseRttMap.size() << std::endl;

        m_portRttMap.clear();
        m_baseRttMap.clear();

        std::cout << "[Ipv4RttRouting] packetTable size=" << m_packetTable.size() << std::endl;

        m_packetTable.clear();

        std::cout << "[Ipv4RttRouting] reverseInfo size=" << m_reverseInfo.size() << std::endl;

        m_reverseInfo.clear();

        m_ipv4 = 0;

        std::cout << "[Ipv4RttRouting] calling base DoDispose" << std::endl;

        Ipv4RoutingProtocol::DoDispose();

        std::cout << "[Ipv4RttRouting] DoDispose: EXIT" << std::endl;
    }
}

} // namespace ns3
