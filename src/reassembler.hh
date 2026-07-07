#pragma once

#include "byte_stream.hh"

#include <cstdint>
#include <map>
#include <string>

class Reassembler
{
public:
  /*
   * Insert a new substring to be reassembled into a ByteStream.
   *   `first_index`: the index of the first byte of the substring
   *   `data`: the substring itself
   *   `is_last_substring`: this substring represents the end of the stream
   *   `output`: a mutable reference to the Writer
   *
   * The Reassembler's job is to reassemble the indexed substrings (possibly out-of-order
   * and possibly overlapping) back into the original ByteStream. As soon as the Reassembler
   * learns the next byte in the stream, it should write it to the output.
   *
   * If the Reassembler learns about bytes that fit within the stream's available capacity
   * but can't yet be written (because earlier bytes remain unknown), it should store them
   * internally until the gaps are filled in.
   *
   * The Reassembler should discard any bytes that lie beyond the stream's available capacity
   * (i.e., bytes that couldn't be written even if earlier gaps get filled in).
   *
   * The Reassembler should close the stream after writing the last byte.
   */
  void insert( uint64_t first_index, std::string data, bool is_last_substring, Writer& output );

  // How many bytes are stored in the Reassembler itself?
  uint64_t bytes_pending() const;

private:
  // Index of the next byte the stream is waiting for (i.e. bytes_pushed to output so far).
  uint64_t next_index_ { 0 };
  // Out-of-order bytes buffered for later, keyed by their absolute stream index. Segments are
  // stored non-overlapping and non-adjacent-overlapping (each key maps to a contiguous chunk).
  std::map<uint64_t, std::string> pending_ {};
  uint64_t bytes_pending_ { 0 };  // total bytes currently held in pending_
  bool eof_seen_ { false };       // have we learned the index of the last byte?
  uint64_t eof_index_ { 0 };      // absolute index one past the last byte of the stream

  // Push any buffered bytes that are now contiguous with next_index_ into the output.
  void flush( Writer& output );
};
