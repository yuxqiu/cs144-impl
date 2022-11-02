#include "tcp_receiver.hh"

#include "wrapping_integers.hh"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>

using namespace std;

TCPReceiver::TCPReceiver(const size_t capacity) : capacity_(capacity) {}

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const auto &header = seg.header();
    const auto &payload = seg.payload();
    if (header.syn) {
        isn_ = header.seqno;
        has_isn_ = true;
    }

    if (!has_isn_)
        return;

    if (header.fin && !sent_fin_) {
        has_fin_ = true;
    }

    auto &out = reassembler_.stream_out();
    const size_t start = header.syn ? unwrap(header.seqno + 1, isn_, ackno_) : unwrap(header.seqno, isn_, ackno_);
    const size_t before_assemble = out.buffer_size();
    reassembler_.push_substring(payload.copy(), start - 1, header.fin);
    ackno_ += out.buffer_size() - before_assemble;

    if (has_fin_ && !sent_fin_ && out.input_ended()) {
        ++ackno_;
        sent_fin_ = true;
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (has_isn_) {
        return wrap(ackno_, isn_);
    }
    return nullopt;
}

size_t TCPReceiver::window_size() const { return capacity_ - reassembler_.stream_out().buffer_size(); }
