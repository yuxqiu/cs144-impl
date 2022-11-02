#include "tcp_connection.hh"

#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <cstddef>
#include <cstdlib>
#include <iostream>

using namespace std;

TCPConnection::TCPConnection(const TCPConfig &cfg) : cfg_{cfg} {}

void TCPConnection::send_segment_with_info() {
    for (auto &out = sender_.segments_out(); !out.empty(); out.pop()) {
        auto &segment = out.front();
        auto &header = segment.header();
        if (receiver_.ackno().has_value()) {
            header.ack = true;
            header.ackno = receiver_.ackno().value();
        }
        header.win = receiver_.window_size();
        segments_out_.push(move(segment));
    }
}

/* A Duff Device Implementation
- No significant improvement as this is not the bottleneck

void TCPConnection::send_segment_with_info() {
    auto &out = sender_.segments_out();
    if (out.empty())
        return;

    constexpr size_t unroll_factor = 8;
    auto process_segment = [&out, this]() {
        auto &segment = out.front();
        auto &header = segment.header();
        if (receiver_.ackno().has_value()) {
            header.ack = true;
            header.ackno = receiver_.ackno().value();
        }
        header.win = receiver_.window_size();
        segments_out_.push(move(segment));
        out.pop();
    };

    long long n = (out.size() + unroll_factor - 1) / (unroll_factor);
    switch (out.size() % unroll_factor) {
        case 0:
            do {
                process_segment();
                [[fallthrough]];
                case 7:
                    process_segment();
                    [[fallthrough]];
                case 6:
                    process_segment();
                    [[fallthrough]];
                case 5:
                    process_segment();
                    [[fallthrough]];
                case 4:
                    process_segment();
                    [[fallthrough]];
                case 3:
                    process_segment();
                    [[fallthrough]];
                case 2:
                    process_segment();
                    [[fallthrough]];
                case 1:
                    process_segment();
            } while (--n > 0);
    }
}

*/

void TCPConnection::send_rst() {
    sender_.send_empty_segment();
    sender_.segments_out().back().header().rst = true;
    send_segment_with_info();
}

void TCPConnection::dirty_abort() {
    sender_.stream_in().set_error();
    receiver_.stream_out().set_error();
    linger_after_streams_finish_ = false;
}

void TCPConnection::abort() { linger_after_streams_finish_ = false; }

size_t TCPConnection::remaining_outbound_capacity() const { return sender_.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return sender_.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return receiver_.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return time_passed; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    // if received rst
    const auto &header = seg.header();

    if (header.rst) {
        dirty_abort();
        return;
    }

    // Processing incoming seg
    receiver_.segment_received(seg);

    // Guard: Actions below require SYN or SYN/ACK is received
    if (!receiver_.ackno().has_value())
        return;
    // Guard =================================================

    // Update ackno if we have one
    if (header.ack) {
        sender_.ack_received(header.ackno, header.win);

        // check if we can send packets now
        sender_.fill_window();
    } else if (header.syn) {    // Deal with SYN request with no ACK
        sender_.fill_window();  // generate a SYN with seqno
    }

    // Deal with fin segment
    if (header.fin) {
        // remote send fin first
        if (!sender_.stream_in().eof()) {
            linger_after_streams_finish_ = false;
        }
        // if local send fin first, we ack the remote fin (remote has already closed the byte stream)
    }

    // Reply to keep-alive requests
    const size_t seg_length = seg.length_in_sequence_space();
    if (seg_length == 0 && header.seqno == receiver_.ackno().value() - 1) {
        sender_.send_empty_segment();
    }

    // If no actual seqno is acquired
    if (seg_length != 0 && sender_.segments_out().empty()) {
        sender_.send_empty_segment();
    }

    // Clear senders packets
    send_segment_with_info();

    // Reset timer
    time_passed = 0;
}

// check active condition
bool TCPConnection::active() const {
    return linger_after_streams_finish_ ||
           ((sender_.bytes_in_flight() || !sender_.stream_in().eof()) && !sender_.stream_in().error()) ||
           (!receiver_.stream_out().eof() && !receiver_.stream_out().error());
}

size_t TCPConnection::write(const string &data) {
    if (sender_.stream_in().input_ended())
        return 0;

    // Calculate how much data is sent
    const size_t send_successfully = min(sender_.stream_in().remaining_capacity(), data.size());

    // Write data
    sender_.stream_in().write(data);
    sender_.fill_window();
    send_segment_with_info();

    return send_successfully;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    time_passed += ms_since_last_tick;

    sender_.tick(ms_since_last_tick);

    if (sender_.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        sender_.segments_out() = {};
        send_rst();
        dirty_abort();
    }

    // lingering and wait for more than 10 * rt_timeout
    if (sender_.stream_in().eof() && receiver_.stream_out().eof() && linger_after_streams_finish_ &&
        time_passed >= 10 * cfg_.rt_timeout) {
        abort();
    }

    send_segment_with_info();
}

void TCPConnection::end_input_stream() {
    if (sender_.stream_in().input_ended())
        return;

    sender_.stream_in().end_input();
    sender_.fill_window();
    send_segment_with_info();
}

void TCPConnection::connect() {
    sender_.fill_window();
    send_segment_with_info();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            send_rst();
            dirty_abort();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
