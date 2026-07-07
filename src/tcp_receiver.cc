#include "tcp_receiver.hh"

#include <algorithm>
#include <cstdint>

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message, Reassembler& reassembler, Writer& inbound_stream )
{
  // The connection begins with the SYN segment, which carries the Initial Sequence Number.
  if ( message.SYN ) {
    isn_ = message.seqno;
  }

  // Ignore any segment that arrives before the SYN establishes the zero point.
  if ( not isn_.has_value() ) {
    return;
  }

  // Absolute sequence number of the segment's seqno. The SYN flag occupies absolute seqno 0,
  // so the first byte of the stream (stream index 0) is at absolute seqno 1.
  // Use the next expected byte (bytes_pushed + 1) as the unwrap checkpoint.
  const uint64_t checkpoint = inbound_stream.bytes_pushed() + 1;
  const uint64_t seg_abs_seqno = message.seqno.unwrap( isn_.value(), checkpoint );

  // Stream index of the first payload byte. A SYN present in this segment means the payload starts
  // one absolute seqno later; otherwise the payload's absolute seqno maps directly to index abs-1.
  const uint64_t stream_index = message.SYN ? 0 : seg_abs_seqno - 1;

  reassembler.insert( stream_index, std::move( message.payload ), message.FIN, inbound_stream );
}

TCPReceiverMessage TCPReceiver::send( const Writer& inbound_stream ) const
{
  TCPReceiverMessage msg;

  // Window size: how many more bytes the stream can accept, capped at the 16-bit field max.
  msg.window_size = static_cast<uint16_t>( min( inbound_stream.available_capacity(), static_cast<uint64_t>( UINT16_MAX ) ) );

  // The ackno is only defined once we have the ISN.
  if ( isn_.has_value() ) {
    // Absolute seqno of the next byte we need = 1 (for SYN) + bytes reassembled so far,
    // plus 1 more if the stream has been fully received and closed (accounting for the FIN).
    uint64_t ack_abs = 1 + inbound_stream.bytes_pushed();
    if ( inbound_stream.is_closed() ) {
      ack_abs += 1; // FIN occupies one sequence number after the last byte.
    }
    msg.ackno = Wrap32::wrap( ack_abs, isn_.value() );
  }

  return msg;
}
