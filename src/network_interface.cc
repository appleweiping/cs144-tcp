#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

using namespace std;

// ethernet_address: Ethernet (what ARP calls "hardware") address of the interface
// ip_address: IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( const EthernetAddress& ethernet_address, const Address& ip_address )
  : ethernet_address_( ethernet_address ), ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ ) << " and IP address "
       << ip_address.ip() << "\n";
}

// Build an Ethernet frame around a serialized payload and queue it for transmission.
void NetworkInterface::enqueue_frame( const EthernetAddress& dst, uint16_t type, vector<Buffer> payload )
{
  EthernetFrame frame;
  frame.header.dst = dst;
  frame.header.src = ethernet_address_;
  frame.header.type = type;
  frame.payload = std::move( payload );
  frames_out_.push( std::move( frame ) );
}

// dgram: the IPv4 datagram to be sent
// next_hop: the IP address of the interface to send it to (typically a router or default gateway, but
// may also be another host if directly connected to the same network as the destination)
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  const uint32_t next_hop_ip = next_hop.ipv4_numeric();

  // If we already know the next hop's Ethernet address, encapsulate and send immediately.
  auto entry = arp_table_.find( next_hop_ip );
  if ( entry != arp_table_.end() ) {
    enqueue_frame( entry->second.eth, EthernetHeader::TYPE_IPv4, serialize( dgram ) );
    return;
  }

  // Otherwise queue the datagram until we learn the mapping via ARP.
  waiting_datagrams_[next_hop_ip].push_back( dgram );

  // Send an ARP request for the next hop, but not more than once every ARP_REQUEST_INTERVAL_MS
  // (to avoid flooding the network with duplicate requests).
  if ( pending_arp_.find( next_hop_ip ) == pending_arp_.end() ) {
    ARPMessage request;
    request.opcode = ARPMessage::OPCODE_REQUEST;
    request.sender_ethernet_address = ethernet_address_;
    request.sender_ip_address = ip_address_.ipv4_numeric();
    request.target_ethernet_address = {}; // unknown -- that's what we're asking for
    request.target_ip_address = next_hop_ip;

    enqueue_frame( ETHERNET_BROADCAST, EthernetHeader::TYPE_ARP, serialize( request ) );
    pending_arp_.emplace( next_hop_ip, 0 );
  }
}

// frame: the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  // Ignore frames not addressed to us (and not broadcast).
  if ( frame.header.dst != ethernet_address_ && frame.header.dst != ETHERNET_BROADCAST ) {
    return {};
  }

  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    InternetDatagram dgram;
    if ( parse( dgram, frame.payload ) ) {
      return dgram;
    }
    return {};
  }

  if ( frame.header.type == EthernetHeader::TYPE_ARP ) {
    ARPMessage arp;
    if ( not parse( arp, frame.payload ) ) {
      return {};
    }

    // Learn the sender's IP->Ethernet mapping from any ARP message we can see.
    arp_table_[arp.sender_ip_address] = ArpEntry { arp.sender_ethernet_address, 0 };

    // Any datagrams that were waiting for this IP can now be sent.
    auto waiting = waiting_datagrams_.find( arp.sender_ip_address );
    if ( waiting != waiting_datagrams_.end() ) {
      for ( const auto& dgram : waiting->second ) {
        enqueue_frame( arp.sender_ethernet_address, EthernetHeader::TYPE_IPv4, serialize( dgram ) );
      }
      waiting_datagrams_.erase( waiting );
    }
    pending_arp_.erase( arp.sender_ip_address );

    // If this is a request for our IP address, reply with our Ethernet address.
    if ( arp.opcode == ARPMessage::OPCODE_REQUEST && arp.target_ip_address == ip_address_.ipv4_numeric() ) {
      ARPMessage reply;
      reply.opcode = ARPMessage::OPCODE_REPLY;
      reply.sender_ethernet_address = ethernet_address_;
      reply.sender_ip_address = ip_address_.ipv4_numeric();
      reply.target_ethernet_address = arp.sender_ethernet_address;
      reply.target_ip_address = arp.sender_ip_address;

      enqueue_frame( arp.sender_ethernet_address, EthernetHeader::TYPE_ARP, serialize( reply ) );
    }
  }

  return {};
}

// ms_since_last_tick: the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  // Expire learned ARP mappings older than ARP_MAPPING_TTL_MS.
  for ( auto it = arp_table_.begin(); it != arp_table_.end(); ) {
    it->second.age_ms += ms_since_last_tick;
    if ( it->second.age_ms >= ARP_MAPPING_TTL_MS ) {
      it = arp_table_.erase( it );
    } else {
      ++it;
    }
  }

  // Allow a fresh ARP request once the previous one has been outstanding for the interval.
  for ( auto it = pending_arp_.begin(); it != pending_arp_.end(); ) {
    it->second += ms_since_last_tick;
    if ( it->second >= ARP_REQUEST_INTERVAL_MS ) {
      it = pending_arp_.erase( it );
    } else {
      ++it;
    }
  }
}

optional<EthernetFrame> NetworkInterface::maybe_send()
{
  if ( frames_out_.empty() ) {
    return {};
  }
  EthernetFrame frame = std::move( frames_out_.front() );
  frames_out_.pop();
  return frame;
}
