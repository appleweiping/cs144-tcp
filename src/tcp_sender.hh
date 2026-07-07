#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <cstdint>
#include <optional>
#include <queue>

class TCPSender
{
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;

  // --- outbound sequencing state ---
  uint64_t next_seqno_ { 0 };        // absolute seqno of the next byte/flag to send
  uint64_t ackno_abs_ { 0 };         // absolute seqno the receiver has acknowledged up to
  uint16_t window_size_ { 1 };       // last window advertised by the receiver (starts at 1 to send SYN)
  bool syn_sent_ { false };          // have we sent the SYN?
  bool fin_sent_ { false };          // have we sent the FIN?

  // Segments waiting to be handed to maybe_send(), and segments sent but not yet acknowledged.
  std::queue<TCPSenderMessage> segments_out_ {};
  std::queue<TCPSenderMessage> outstanding_ {};
  uint64_t bytes_in_flight_ { 0 };

  // --- retransmission timer state ---
  uint64_t current_RTO_ms_ { 0 };            // current retransmission timeout (doubles on expiry)
  uint64_t time_since_last_send_ { 0 };      // ms elapsed since the timer (re)started
  bool timer_running_ { false };
  uint64_t consecutive_retransmissions_ { 0 };

public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( uint64_t initial_RTO_ms, std::optional<Wrap32> fixed_isn );

  /* Push bytes from the outbound stream */
  void push( Reader& outbound_stream );

  /* Send a TCPSenderMessage if needed (or empty optional otherwise) */
  std::optional<TCPSenderMessage> maybe_send();

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage send_empty_message() const;

  /* Receive an act on a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called. */
  void tick( uint64_t ms_since_last_tick );

  /* Accessors for use in testing */
  uint64_t sequence_numbers_in_flight() const;  // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // How many consecutive *re*transmissions have happened?
};
