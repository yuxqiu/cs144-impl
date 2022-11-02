#include "tcp_sender.hh"

#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <cstddef>
#include <cstdint>
#include <random>

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : isn_(fixed_isn.value_or(WrappingInt32{random_device()()})), timer_{retx_timeout}, stream_{capacity} {}

size_t TCPSender::bytes_in_flight() const { return next_seqno_ - seqno_acked_; }

void TCPSender::push_segment(const TCPSegment &seg, const uint64_t start) {
    segments_out_.push(seg);
    buffer_.emplace_back(start, seg);
}

uint64_t TCPSender::assemble_segment(const uint64_t start) {
    TCPSegment seg;

    // fill in the seqno
    auto &header = seg.header();
    header.seqno = wrap(start, isn_);

    uint64_t max_segment_size = max<uint64_t>(1, window_size_) - (start - seqno_acked_);

    // fill syn if needed
    if (start == 0) {
        header.syn = true;
        --max_segment_size;
    }

    // fill fin if needed and possible (remaining can be consumed)
    const uint64_t payload_size_with_fin = min<uint64_t>(max_segment_size - 1, TCPConfig::MAX_PAYLOAD_SIZE);
    const size_t buffer_size = stream_.buffer_size();

    if (!sent_fin_ && stream_.input_ended() && buffer_size <= payload_size_with_fin) {
        header.fin = true;
        sent_fin_ = true;
        --max_segment_size;
    }

    // fill the payload
    max_segment_size = min<uint64_t>({max_segment_size, buffer_size, TCPConfig::MAX_PAYLOAD_SIZE});
    seg.payload() = stream_.read(max_segment_size);

    // trim empty packets
    max_segment_size = seg.length_in_sequence_space();
    if (max_segment_size != 0)
        push_segment(seg, start);

    return start + max_segment_size;
}

void TCPSender::fill_window() {
    const uint64_t right = seqno_acked_ + max<uint64_t>(1, window_size_);
    while (next_seqno_ < right) {
        next_seqno_ = assemble_segment(next_seqno_);
        if (stream_.buffer_empty())
            break;
    }

    if (!timer_.running() && !buffer_.empty()) {
        timer_.reset();
        timer_.start();
    }
}

void TCPSender::clear_buffer() {
    while (!buffer_.empty() &&
           buffer_.front().first + buffer_.front().second.length_in_sequence_space() <= seqno_acked_) {
        buffer_.pop_front();
    }

    // Stop timer if there is no outstanding segment
    if (buffer_.empty()) {
        timer_.stop();
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    const uint64_t abs_ackno = unwrap(ackno, isn_, seqno_acked_);

    // drop if abs_ackno is >= what we have sent
    if (abs_ackno > next_seqno_)
        return;

    // update seqno_acked and timer only if receiving new ack
    if (abs_ackno > seqno_acked_) {
        seqno_acked_ = abs_ackno;
        consecutive_retransmissions_ = 0;
        timer_.reset();
        timer_.start();
    }

    // update window_size
    window_size_ = window_size;

    // remove data already sent
    clear_buffer();
}

// Precondition:
//  1. At least one element in map
//  2. The timer is running
void TCPSender::retransmit() {
    // restart the timer with double rto and increase the consecutive_retransmissions_
    if (window_size_ != 0) {
        timer_.twice();
        ++consecutive_retransmissions_;
    }
    timer_.start();

    // retransmit
    segments_out_.push(buffer_.front().second);
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    timer_.tick(ms_since_last_tick);
    if (timer_.expire()) {
        retransmit();
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return consecutive_retransmissions_; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(next_seqno_, isn_);
    segments_out_.emplace(move(seg));
}
