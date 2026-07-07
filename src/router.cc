#include "router.hh"

#include <iostream>
#include <limits>
#include <optional>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  routes_.push_back( RouteEntry { route_prefix, prefix_length, next_hop, interface_num } );
}

void Router::route()
{
  // Pull every waiting datagram out of every interface and forward it.
  for ( auto& iface : interfaces_ ) {
    while ( auto dgram = iface.maybe_receive() ) {
      route_one( std::move( dgram.value() ) );
    }
  }
}

void Router::route_one( InternetDatagram dgram )
{
  const uint32_t dst = dgram.header.dst;

  // Longest-prefix match: find the applicable route with the greatest prefix_length.
  const RouteEntry* best = nullptr;
  for ( const auto& entry : routes_ ) {
    // A prefix of length 0 matches everything; otherwise compare the high `prefix_length` bits.
    // Shifting a uint32_t by 32 is undefined, so treat length 0 as an unconditional match.
    const bool matches
      = entry.prefix_length == 0
        || ( ( dst >> ( 32 - entry.prefix_length ) ) == ( entry.prefix >> ( 32 - entry.prefix_length ) ) );
    if ( matches && ( best == nullptr || entry.prefix_length > best->prefix_length ) ) {
      best = &entry;
    }
  }

  // No matching route: drop the datagram.
  if ( best == nullptr ) {
    return;
  }

  // Drop datagrams whose TTL has expired (or would expire on decrement).
  if ( dgram.header.ttl <= 1 ) {
    return;
  }
  dgram.header.ttl -= 1;
  dgram.header.compute_checksum(); // TTL changed, so the header checksum must be recomputed.

  // The next hop is either the explicit gateway or, for a directly-attached network, the datagram's
  // own destination address.
  const Address next_hop = best->next_hop.has_value() ? best->next_hop.value()
                                                      : Address::from_ipv4_numeric( dst );

  interface( best->interface_num ).send_datagram( dgram, next_hop );
}
