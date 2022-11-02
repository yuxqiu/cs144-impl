#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "ethernet_header.hh"
#include "ipv4_datagram.hh"
#include "parser.hh"

#include <cstdint>
#include <iostream>
#include <optional>

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : ethernet_address_(ethernet_address), ip_address_(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(ethernet_address_) << " and IP address "
         << ip_address.ip() << "\n";
}

EthernetFrame NetworkInterface::generate_ip_frame(const EthernetAddress &dst, const IPv4Datagram &payload) {
    EthernetFrame eframe;
    eframe.payload() = payload.serialize();
    auto &header = eframe.header();
    header.dst = dst;
    header.src = ethernet_address_;
    header.type = EthernetHeader::TYPE_IPv4;
    return eframe;
}

EthernetFrame NetworkInterface::generate_arp_frame(const EthernetAddress &dst,
                                                   const uint32_t ip_address,
                                                   const uint16_t opcode) {
    ARPMessage msg;
    msg.opcode = opcode;
    msg.sender_ip_address = ip_address_.ipv4_numeric();
    msg.sender_ethernet_address = ethernet_address_;
    msg.target_ethernet_address = dst;
    msg.target_ip_address = ip_address;

    EthernetFrame eframe;
    eframe.payload() = msg.serialize();
    auto &header = eframe.header();
    header.dst = dst == EMPTY_ADDRESS ? ETHERNET_BROADCAST : dst;
    header.src = ethernet_address_;
    header.type = EthernetHeader::TYPE_ARP;
    return eframe;
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
//
//! Note: Address is the IP address of next router or The host ip (which is equal to ip in dgram)
//! The datagram's dst is not necessarily equal to this ip. (However, we are approaching it because of
//! Longest Prefix Match.)
//! This interface is probably connected to a subnet that are closer to this ip.
//
//! We can assume empty address will only paired with 32 prefix length:
//? only in this case, we are sure the dst of data gram is the host.
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    // Send datagram directly if find non-expiring ip->ethernet mapping
    const auto it = map_.find(next_hop_ip);
    if (it != map_.end() && ms_since_last_tick_ - it->second.second < IP_TO_ETHERNET_LIMIT) {
        frames_out_.emplace(generate_ip_frame(it->second.first, dgram));
        return;
    }

    // Send ARP if never send it before or expiring
    const auto already_sent_it = already_sent_arp_.find(next_hop_ip);
    if (already_sent_it == already_sent_arp_.end() || ms_since_last_tick_ - already_sent_it->second >= ARP_TIME_LIMIT) {
        frames_out_.emplace(generate_arp_frame(EMPTY_ADDRESS, next_hop_ip, ARPMessage::OPCODE_REQUEST));
        already_sent_arp_.emplace(next_hop_ip, ms_since_last_tick_);
    }

    // Buffer the IPv4 datagram
    buffer_[next_hop_ip].emplace_back(dgram);
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    const auto &header = frame.header();
    if (header.dst != ethernet_address_ && header.dst != ETHERNET_BROADCAST) {
        return nullopt;
    }

    if (header.type == EthernetHeader::TYPE_IPv4) {
        IPv4Datagram dgram;
        if (dgram.parse(frame.payload()) == ParseResult::NoError) {
            return dgram;
        }
    } else if (header.type == EthernetHeader::TYPE_ARP) {
        ARPMessage msg;
        if (msg.parse(frame.payload()) == ParseResult::NoError) {
            map_.emplace(msg.sender_ip_address, pair{msg.sender_ethernet_address, ms_since_last_tick_});

            // Send buffered ipv4 datagram
            const auto it = buffer_.find(msg.sender_ip_address);
            if (it != buffer_.end()) {
                for (const auto &dgram : it->second) {
                    frames_out_.emplace(generate_ip_frame(msg.sender_ethernet_address, dgram));
                }
                buffer_.erase(it);
            }

            // If target ip address matches => send a reply
            if (msg.target_ethernet_address == EMPTY_ADDRESS && msg.target_ip_address == ip_address_.ipv4_numeric()) {
                frames_out_.emplace(
                    generate_arp_frame(msg.sender_ethernet_address, msg.sender_ip_address, ARPMessage::OPCODE_REPLY));
            }
        }
    }

    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) { ms_since_last_tick_ += ms_since_last_tick; }
