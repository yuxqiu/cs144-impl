#include "stream_reassembler.hh"

#include <cstddef>
#include <string_view>

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : capacity_{capacity} {}

// Assemble the data into the queue
// Reflect the adjustment made via index and len
// The interval is [)
// use string view to rewrite
void StreamReassembler::assemble(const string &input, const size_t index) {
    const size_t upper = next_ + (capacity_ - output_.buffer_size());
    const string_view &data{input};
    if (!data.size() || next_ >= index + data.size() || index >= upper)
        return;

    // adjust the size
    const size_t start = max(next_, index), offset = start - index;  // offset is relative indexing
    const size_t len = min(data.size() - offset, upper - start);
    const pair<size_t, size_t> range{start, start + len};  // absolute range

    // adjust the position
    auto prev = data_.before_begin();
    auto it = data_.begin();
    while (it != data_.end() && range.first > it->first.second) {
        prev = it++;
    }

    // if no overlapping intervals
    if (it == data_.end() || range.second < it->first.first) {
        size_ += len;
        data_.insert_after(prev, {range, input.substr(offset, len)});
        return;
    }

    const size_t mini = min(it->first.first, range.first);
    const size_t maxj = max(it->first.second, range.second);

    // Merge the stored string
    size_t keep = 0;  // number of bytes to be "kept" from data
    if (mini == it->first.first) {
        // For intervals like this
        // --------------------
        //              ---------------
        if (maxj != it->first.second) {
            const size_t bypass = (it->first.second - range.first);
            keep = len - bypass;
            it->second += data.substr(offset + bypass, keep);
        }
    } else {
        // For intervals like this
        //    --------------------
        // ---------------
        if (maxj == it->first.second) {
            keep = it->first.first - range.first;
            it->second = move(input.substr(offset, keep) += move(it->second));
        } else {
            // For intervals like this
            //    --------------------
            // ------------------------------
            keep = len - it->second.size();
            it->second = data.substr(offset, len);
        }
    }
    size_ += keep;

    it->first.first = mini;
    it->first.second = maxj;

    // Remove overlapping fields
    for (prev = it++; it != data_.end() && prev->first.second >= it->first.second; ++it, data_.erase_after(prev)) {
        size_ -= it->second.size();
    }
    if (it != data_.end() && it->first.first <= prev->first.second) {
        const size_t bypass = prev->first.second - it->first.first;
        size_ -= bypass;
        prev->second.append(string_view{it->second}.substr(bypass));
        prev->first.second = it->first.second;
        data_.erase_after(prev);
    }
}

void StreamReassembler::check_end_input(const size_t index, const bool eof) {
    if (eof) {
        eof_ = true;
        eof_num_ = index;
    }
    if (eof_ && eof_num_ == next_) {
        output_.end_input();
    }
}

void StreamReassembler::output() {
    if (data_.empty() || next_ != data_.front().first.first)
        return;

    next_ = data_.front().first.second;
    size_ -= data_.front().second.size();

    output_.write(data_.front().second);
    data_.pop_front();
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const uint64_t index, const bool eof) {
    assemble(data, index);
    output();
    check_end_input(index + data.size(), eof);
}

size_t StreamReassembler::unassembled_bytes() const { return size_; }

bool StreamReassembler::empty() const { return eof_ && eof_num_ == next_; }
