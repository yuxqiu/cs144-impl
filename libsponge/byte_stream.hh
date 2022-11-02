#ifndef SPONGE_LIBSPONGE_BYTE_STREAM_HH
#define SPONGE_LIBSPONGE_BYTE_STREAM_HH

#include <cstddef>
#include <string>
#include <vector>

//! \brief An in-order byte stream.

//! Bytes are written on the "input" side and read from the "output"
//! side.  The byte stream is finite: the writer can end the input,
//! and then no more bytes can be written.
class ByteStream {
  private:
    class RingBuffer {
      public:
        size_t front_{0}, size_{0}, capacity_;
        std::string queue = std::string(capacity_, 0);

        RingBuffer(const size_t k) : capacity_(k) {}

        void push_back_n(const std::string &s, const size_t length) {
            const size_t start = (front_ + size_) % capacity_;
            const size_t len_to_end = std::min(length, capacity_ - start);
            queue.replace(start, len_to_end, s, 0, len_to_end);
            queue.replace(0, length - len_to_end, s, len_to_end, length - len_to_end);
            size_ += length;
        }

        void pop_front_n(const size_t n) {
            front_ = (front_ + n) % capacity_;
            size_ -= n;
        }

        std::string peek_front_n(const size_t length) const {
            const size_t len_to_end = std::min(length, capacity_ - front_);
            std::string s{queue.substr(front_, len_to_end)};
            s.reserve(length);
            s += queue.substr(0, length - len_to_end);
            return s;
        }

        bool empty() const { return !size_; }

        bool full() const { return size_ == capacity_; }

        size_t size() const { return size_; }
        size_t remaining_capacity() const { return capacity_ - size_; }
        size_t capacity() const { return capacity_; }
    };

    RingBuffer rb_;
    size_t total_read_{0}, total_write_{0};
    bool error_{false}, eof_input_{false};  //!< Flag indicating that the stream suffered an error.

  public:
    //! Construct a stream with room for `capacity` bytes.
    ByteStream(const size_t capacity);

    //! \name "Input" interface for the writer
    //!@{

    //! Write a string of bytes into the stream. Write as many
    //! as will fit, and return how many were written.
    //! \returns the number of bytes accepted into the stream
    size_t write(const std::string &data);

    //! \returns the number of additional bytes that the stream has space for
    size_t remaining_capacity() const;

    //! Signal that the byte stream has reached its ending
    void end_input();

    //! Indicate that the stream suffered an error.
    void set_error() { error_ = true; }
    //!@}

    //! \name "Output" interface for the reader
    //!@{

    //! Peek at next "len" bytes of the stream
    //! \returns a string
    std::string peek_output(const size_t len) const;

    //! Remove bytes from the buffer
    void pop_output(const size_t len);

    //! Read (i.e., copy and then pop) the next "len" bytes of the stream
    //! \returns a string
    std::string read(const size_t len);

    //! \returns `true` if the stream input has ended
    bool input_ended() const;

    //! \returns `true` if the stream has suffered an error
    bool error() const { return error_; }

    //! \returns the maximum amount that can currently be read from the stream
    size_t buffer_size() const;

    //! \returns `true` if the buffer is empty
    bool buffer_empty() const;

    //! \returns `true` if the output has reached the ending
    bool eof() const;
    //!@}

    //! \name General accounting
    //!@{

    //! Total number of bytes written
    size_t bytes_written() const;

    //! Total number of bytes popped
    size_t bytes_read() const;
    //!@}
};

#endif  // SPONGE_LIBSPONGE_BYTE_STREAM_HH
