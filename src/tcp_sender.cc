#include "tcp_sender.hh"
#include "tcp_config.hh"

#include <algorithm>
#include <random>

using namespace std;

/* TCPSender constructor (uses a random ISN if none given) */
TCPSender::TCPSender( uint64_t initial_RTO_ms, optional<Wrap32> fixed_isn )
  : isn_( fixed_isn.value_or( Wrap32 { random_device()() } ) )
  , initial_RTO_ms_( initial_RTO_ms )
  , current_RTO_ms_( initial_RTO_ms )
{}

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return bytes_in_flight_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return consecutive_retransmissions_;
}

optional<TCPSenderMessage> TCPSender::maybe_send()
{
  if ( segments_out_.empty() ) {
    return {};
  }
  TCPSenderMessage msg = std::move( segments_out_.front() );
  segments_out_.pop();
  return msg;
}

void TCPSender::push( Reader& outbound_stream )
{
  // Treat a zero window as 1 so that we always probe with a single byte/flag; this lets the peer
  // re-advertise a real window and prevents deadlock.
  const uint64_t effective_window = window_size_ == 0 ? 1 : window_size_;

  // Keep sending segments while the window has room for more outstanding sequence numbers and
  // there is still something (SYN, data, or FIN) left to transmit.
  while ( effective_window > bytes_in_flight_ ) {
    // If the FIN has already been sent, there is nothing more this connection will ever send.
    if ( fin_sent_ ) {
      break;
    }

    TCPSenderMessage msg;
    msg.seqno = Wrap32::wrap( next_seqno_, isn_ );

    // The SYN flag opens the stream and occupies the first sequence number.
    if ( not syn_sent_ ) {
      msg.SYN = true;
      syn_sent_ = true;
    }

    // How many sequence numbers may this segment occupy, given the window and the SYN we may
    // have just placed? Cap the payload at MAX_PAYLOAD_SIZE.
    const uint64_t room = effective_window - bytes_in_flight_ - msg.sequence_length();
    const uint64_t payload_size = min( { room, static_cast<uint64_t>( TCPConfig::MAX_PAYLOAD_SIZE ),
                                         outbound_stream.bytes_buffered() } );

    string payload;
    read( outbound_stream, payload_size, payload );
    msg.payload = std::move( payload );

    // Attach the FIN if the stream is finished, we haven't sent it yet, and there is room for one
    // more sequence number in the window after the SYN (if any) and the payload we just read.
    if ( outbound_stream.is_finished() and not fin_sent_
         and bytes_in_flight_ + msg.sequence_length() + 1 <= effective_window ) {
      msg.FIN = true;
      fin_sent_ = true;
    }

    // Nothing to send (no SYN, no payload, no FIN) -- stop.
    if ( msg.sequence_length() == 0 ) {
      break;
    }

    // Ship the segment: queue it for output, track it as outstanding, advance seqno, start timer.
    next_seqno_ += msg.sequence_length();
    bytes_in_flight_ += msg.sequence_length();
    outstanding_.push( msg );
    segments_out_.push( std::move( msg ) );

    if ( not timer_running_ ) {
      timer_running_ = true;
      time_since_last_send_ = 0;
    }
  }
}

TCPSenderMessage TCPSender::send_empty_message() const
{
  TCPSenderMessage msg;
  msg.seqno = Wrap32::wrap( next_seqno_, isn_ );
  return msg; // no SYN/FIN/payload -- zero sequence length
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  window_size_ = msg.window_size;

  // A segment with no ackno (peer hasn't seen our SYN yet) only updates the window.
  if ( not msg.ackno.has_value() ) {
    return;
  }

  const uint64_t ack_abs = msg.ackno.value().unwrap( isn_, next_seqno_ );

  // Ignore acks for data we haven't even sent yet (impossible/invalid ackno).
  if ( ack_abs > next_seqno_ ) {
    return;
  }

  bool something_acked = false;

  // Remove any outstanding segment that is now fully acknowledged.
  while ( not outstanding_.empty() ) {
    const TCPSenderMessage& seg = outstanding_.front();
    const uint64_t seg_seqno = seg.seqno.unwrap( isn_, ackno_abs_ );
    if ( seg_seqno + seg.sequence_length() <= ack_abs ) {
      bytes_in_flight_ -= seg.sequence_length();
      outstanding_.pop();
      something_acked = true;
    } else {
      break;
    }
  }

  if ( ack_abs > ackno_abs_ ) {
    ackno_abs_ = ack_abs;
  }

  if ( something_acked ) {
    // New data was acknowledged: reset RTO to its initial value and clear the backoff counter.
    current_RTO_ms_ = initial_RTO_ms_;
    consecutive_retransmissions_ = 0;
    // Restart the timer only if unacknowledged data remains.
    if ( outstanding_.empty() ) {
      timer_running_ = false;
    } else {
      time_since_last_send_ = 0;
    }
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick )
{
  if ( not timer_running_ ) {
    return;
  }

  time_since_last_send_ += ms_since_last_tick;

  if ( time_since_last_send_ < current_RTO_ms_ ) {
    return;
  }

  // Timer expired. Retransmit the earliest outstanding segment.
  if ( not outstanding_.empty() ) {
    segments_out_.push( outstanding_.front() );

    // If the receiver's window is nonzero, treat this as a real loss: back off (double the RTO)
    // and count the consecutive retransmission. When the window is zero we're probing and must
    // not exponentially back off (the peer may simply have no space right now).
    if ( window_size_ > 0 ) {
      consecutive_retransmissions_ += 1;
      current_RTO_ms_ *= 2;
    }
  }

  // Restart the timer.
  time_since_last_send_ = 0;
}
