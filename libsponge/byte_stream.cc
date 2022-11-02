#include "byte_stream.hh"

#include <algorithm>
#include <cstddef>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

using namespace std;

ByteStream::ByteStream(const size_t capacity) : rb_(capacity) {}

size_t ByteStream::write(const string &data) {
    const size_t size = min(data.size(), rb_.remaining_capacity());
    rb_.push_back_n(data, size);
    total_write_ += size;
    return size;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const { return rb_.peek_front_n(len); }

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    rb_.pop_front_n(len);
    total_read_ += len;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    const auto ans = peek_output(len);
    pop_output(len);
    return ans;
}

void ByteStream::end_input() { eof_input_ = true; }

bool ByteStream::input_ended() const { return eof_input_; }

size_t ByteStream::buffer_size() const { return rb_.size(); }

bool ByteStream::buffer_empty() const { return rb_.empty(); }

bool ByteStream::eof() const { return input_ended() && total_read_ == total_write_; }

size_t ByteStream::bytes_written() const { return total_write_; }

size_t ByteStream::bytes_read() const { return total_read_; }

size_t ByteStream::remaining_capacity() const { return rb_.remaining_capacity(); }
