#include "reassembler.hh"

#include <algorithm>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring, Writer& output )
{
  // Record where the stream ends, if this substring carries the end-of-stream marker.
  if ( is_last_substring ) {
    eof_seen_ = true;
    eof_index_ = first_index + data.size();
  }

  // The acceptable window is [next_index_, next_index_ + available_capacity).
  // available_capacity() already accounts for bytes buffered in the output ByteStream, so the
  // Reassembler may only hold bytes that can eventually fit within the remaining window.
  const uint64_t window_start = next_index_;
  const uint64_t window_end = next_index_ + output.available_capacity();

  // Clamp the incoming substring to the acceptable window.
  uint64_t begin = first_index;
  uint64_t end = first_index + data.size();
  begin = max( begin, window_start );
  end = min( end, window_end );

  if ( begin < end ) {
    data = data.substr( begin - first_index, end - begin );

    // Merge with any overlapping or adjacent segments already buffered so that pending_ stays a
    // set of disjoint, coalesced chunks. First absorb segments that start at/after `begin`.
    auto it = pending_.lower_bound( begin );
    // Also consider the segment immediately before `begin`, which may overlap into our range.
    if ( it != pending_.begin() ) {
      auto prev = std::prev( it );
      if ( prev->first + prev->second.size() >= begin ) {
        it = prev;
      }
    }

    while ( it != pending_.end() && it->first <= end ) {
      const uint64_t seg_begin = it->first;
      const uint64_t seg_end = it->first + it->second.size();
      if ( seg_end < begin ) { // strictly before, no overlap/adjacency
        ++it;
        continue;
      }
      // Overlapping or touching: fold this segment into [begin, end).
      const uint64_t new_begin = min( begin, seg_begin );
      const uint64_t new_end = max( end, seg_end );
      string merged( new_end - new_begin, '\0' );
      // Copy the existing segment first, then our data on top (identical where they overlap).
      merged.replace( seg_begin - new_begin, it->second.size(), it->second );
      merged.replace( begin - new_begin, data.size(), data );
      begin = new_begin;
      end = new_end;
      data = std::move( merged );
      bytes_pending_ -= it->second.size();
      it = pending_.erase( it );
    }

    pending_.emplace( begin, std::move( data ) );
    bytes_pending_ += end - begin;
  }

  flush( output );
}

void Reassembler::flush( Writer& output )
{
  // Write out every buffered chunk that begins exactly at the next expected index.
  auto it = pending_.begin();
  while ( it != pending_.end() && it->first == next_index_ ) {
    const string& chunk = it->second;
    const uint64_t before = output.bytes_pushed();
    output.push( chunk );
    const uint64_t written = output.bytes_pushed() - before;
    next_index_ += written;
    bytes_pending_ -= written;
    if ( written < chunk.size() ) {
      // Output is full; keep the unwritten tail buffered at its correct index.
      string tail = chunk.substr( written );
      pending_.erase( it );
      pending_.emplace( next_index_, std::move( tail ) );
      break;
    }
    it = pending_.erase( it );
  }

  // If we've delivered everything up to the recorded end of stream, close the output.
  if ( eof_seen_ && next_index_ >= eof_index_ ) {
    output.close();
  }
}

uint64_t Reassembler::bytes_pending() const
{
  return bytes_pending_;
}
