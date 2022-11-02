#include "router.hh"

#include "address.hh"
#include "ethernet_frame.hh"

#include <cstdint>
#include <iostream>

using namespace std;

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";
    if (!prefix_length) {
        table_.emplace(pair{0, 0}, pair{interface_num, move(next_hop)});
        return;
    }

    const uint32_t ip{(static_cast<uint32_t>(~0) << (32 - prefix_length)) & route_prefix};
    table_.emplace(pair{ip, prefix_length}, pair{interface_num, move(next_hop)});
}

//! \param[in] dgram The datagram to be routed
//
//! When next NetworkInterface receives packet:
//!   - if it's a router, it collects the packet and route it again
//!   - if it's a host, it delivers the packet to the application
void Router::route_one_datagram(InternetDatagram &dgram) {
    if (dgram.header().ttl == 0 || --dgram.header().ttl == 0)
        return;

    const uint32_t ip{dgram.header().dst};
    {
        int8_t prefix_length{32};
        for (uint32_t mask{~static_cast<uint32_t>(0)}; prefix_length >= 0; mask <<= 1, --prefix_length) {
            const auto it = table_.find({mask & ip, prefix_length});
            if (it != table_.end()) {
                auto &interface = Router::interface(it->second.first);
                interface.send_datagram(dgram,
                                        it->second.second.value_or(Address::from_ipv4_numeric(dgram.header().dst)));
                break;
            }
        }
    }
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : interfaces_) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
