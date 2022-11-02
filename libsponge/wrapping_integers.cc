#include "wrapping_integers.hh"

#include <cstdint>
#include <iostream>

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    constexpr uint64_t m = static_cast<uint64_t>(UINT32_MAX) + 1;
    return WrappingInt32{static_cast<uint32_t>(((n % m) + static_cast<uint64_t>(isn.raw_value())) % m)};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    constexpr uint64_t m = static_cast<uint64_t>(UINT32_MAX) + 1;
    const uint64_t steps = (static_cast<uint64_t>(n.raw_value()) + m - static_cast<uint64_t>(isn.raw_value())) % m;
    const uint64_t prev = (checkpoint - steps) / m, next = prev + 1;
    const uint64_t from_prev = steps + prev * m, from_next = steps + next * m;
    const uint64_t diff_prev = checkpoint > from_prev ? checkpoint - from_prev : from_prev - checkpoint;
    const uint64_t diff_next = checkpoint > from_next ? checkpoint - from_next : from_next - checkpoint;
    return diff_prev < diff_next ? from_prev : from_next;
}
