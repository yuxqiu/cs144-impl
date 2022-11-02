#ifndef SPONGE_LIBSPONGE_TCP_RECEIVER_HH
#define SPONGE_LIBSPONGE_TCP_RECEIVER_HH

#include "byte_stream.hh"
#include "stream_reassembler.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <cstdint>
#include <optional>

//! \brief The "receiver" part of a TCP implementation.

//! Receives and reassembles segments into a ByteStream, and computes
//! the acknowledgment number and window size to advertise back to the
//! remote TCPSender.
class TCPReceiver {
    //! The maximum number of bytes we'll store.
    size_t capacity_;

    //! Our data structure for re-assembling bytes.
    StreamReassembler reassembler_{capacity_};

    // ISN and ackno
    WrappingInt32 isn_{0};
    uint64_t ackno_{1};
    bool has_isn_{false};

    // FIN
    bool has_fin_{false};
    bool sent_fin_{false};

  public:
    //! \brief Construct a TCP receiver
    //!
    //! \param capacity the maximum number of bytes that the receiver will
    //!                 store in its buffers at any give time.
    TCPReceiver(const size_t capacity);

    //! \name Accessors to provide feedback to the remote TCPSender
    //!@{

    //! \brief The ackno that should be sent to the peer
    //! \returns empty if no SYN has been received
    //!
    //! This is the beginning of the receiver's window, or in other words, the sequence number
    //! of the first byte in the stream that the receiver hasn't received.
    std::optional<WrappingInt32> ackno() const;

    //! \brief The window size that should be sent to the peer
    //!
    //! Operationally: the capacity minus the number of bytes that the
    //! TCPReceiver is holding in its byte stream (those that have been
    //! reassembled, but not consumed).
    //!
    //! Formally: the difference between (a) the sequence number of
    //! the first byte that falls after the window (and will not be
    //! accepted by the receiver) and (b) the sequence number of the
    //! beginning of the window (the ackno).
    //!
    //! The operational design is quite clever as it reflects an idea:
    //! if we are waiting and receiving a lot of bytes but we could not
    //! reassemble, we should inform sender the decrease of the windows size,
    //! which makes the sender more likely to send the byte(s) we are missing.
    size_t window_size() const;
    //!@}

    //! \brief number of bytes stored but not yet reassembled
    size_t unassembled_bytes() const { return reassembler_.unassembled_bytes(); }

    //! \brief handle an inbound segment
    void segment_received(const TCPSegment &seg);

    //! \name "Output" interface for the reader
    //!@{
    ByteStream &stream_out() { return reassembler_.stream_out(); }
    const ByteStream &stream_out() const { return reassembler_.stream_out(); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_RECEIVER_HH
