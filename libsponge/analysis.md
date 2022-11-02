## Why it's hard to debug

1. Cannot run test cases independently
2. Program is both a server and a client
3. The program is multithreaded, and our implementation is encapsulated

## Where is the bug?

### Identify the Output of the test script

It's a `sha256sum` of some file data. So, it means that the file size of the input file doesn't match that of the output file.

### Helper 1: Output the file size

By adding `echo $(ls -l $TEST_IN_FILE | cut -d ' ' -f5) $(ls -l $TEST_OUT_FILE | cut -d ' ' -f5) $(ls -l $TEST_OUT2_FILE | cut -d ' ' -f5)`, we can output the filesize of three test files. We can find that the input file size is always >= than the output file size.

This leads to a guess: there is something wrong with the ACK system. Maybe the ackno is not correctly sent?

### Helper 2: Change the Debugging Print

The default debugging output is hard to read as we are running a server and a client, and both of them are printing to the terminal. So, we need to add a little bit decoration on the Debugging output (the source ip), and format the output a little bit to make it less likely output things from client/server on the same line.

### Guess 1: the ackno is not correctly sent?

The first guess is the ackno is not correctly sent, but the receiver actually gets the data. However, this is not correct as it becomes clear that the total number of bytes written to the receiver's `stream_out` is less than the size of the actual data.

However, during the investigation, we find something very interesting. The amount of data the receiver doesn't get is equal to the size of the last segment from the sender. (We get this by printing the number of bytes left in sender's `TCPSennder's stream`).

Thus, is it possible that these bytes are not successfully assembled by reassembler?

### Guess 2: reassembler problem?

To investigate whether the problem is because of the reassembler, we try to print the unassembled byte in the reassembler. It seems that there are actually some bytes not assembled in reassembler. This seems to confirm our guess.

However, as we delve deeper into the reason why the reassmbler didn't assemble our bytes, we find something is interesting. We first try to print the `next_` inside reassembler vs. the given `index`. We soon find that when receiving the last segment, the `index` is incremented by 1 for no reason.

As the output is a little bit messy, we write down the following analysis:

>Sender: 0, 1 => ACK with seqno 2
>
>Receiver: 129688, 129688
>
>1. Sender behaves like this because I send ACK to it
>2. The seqno is 2 as it's equal to index + 1
>3. It's 2 because we have sent a SYN/ACK, and FIN to the Sender who firstly tries to connect us
>    - 3.1 it sends SYN => we send SYN/ACK => it sends ACK => we send FIN
>4. Now, every ACK we send is 2 because no other segment is sent
>5. So, the sender get this strange number because of the receiver side
>
>So, as receiver now gets: 130688, 130689; we know it's because of sender.
>
>But if FIN is sent with payload correctly: if FIN is sent with the last payload (which is less than the largest payload size), we could only get `next_ == index`. Thus, the only possibility is that we send FIN wrongly: we send it before the last segment, and the last segment is therefore assigned with a wrong `seqno`.

### Guess 3: sender problem?

So, we step back to the `TCPSender`, and finally find one suspicious condition check:

```c++
uint64_t max_segment_size = max<uint64_t>(1, window_size_) - (start - seqno_acked_);

// some codes here

if (!sent_fin_ && stream_.input_ended() && stream_.buffer_size() < max_segment_size) {
  // fill the packet with fin
}
```

The problem is that we could put FIN into segment even if this inequality is true: `max_segment_size > stream.buffer_size() > TCPConfig::MAX_PAYLOAD_SIZE`.

However, by this inequality, we apparently cannot put all the content in the buffer to the segment. Thus, we send a segment with some payload and a FIN, and leave some additional contents inside the buffer. Then, this FIN causes seqno to increase by 1 without actually delivering the one byte data to the reassembler, which again causes the reassembler fail to assemble the data.