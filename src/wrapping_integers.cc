#include "wrapping_integers.hh"

#include <cstdint>

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  // The low 32 bits of n, offset from the zero point. Truncation to uint32_t performs the mod 2^32.
  return Wrap32 { zero_point.raw_value_ + static_cast<uint32_t>( n ) };
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // Offset of this seqno from the zero point, as a 32-bit value in [0, 2^32).
  const uint32_t offset = raw_value_ - zero_point.raw_value_;

  // Candidate absolute seqno formed by pasting `offset` onto the checkpoint's high bits.
  constexpr uint64_t TWO_32 = 1UL << 32;
  const uint64_t base = checkpoint & ~( TWO_32 - 1 ); // checkpoint with low 32 bits cleared
  const uint64_t candidate = base + offset;

  // Consider the candidate in the same 2^32 block, and the neighbouring blocks, and pick the one
  // closest to the checkpoint. Guard against underflow when candidate < 2^32.
  uint64_t best = candidate;
  auto closer = [&]( uint64_t c ) {
    const uint64_t d_c = c > checkpoint ? c - checkpoint : checkpoint - c;
    const uint64_t d_best = best > checkpoint ? best - checkpoint : checkpoint - best;
    if ( d_c < d_best ) {
      best = c;
    }
  };

  if ( candidate >= TWO_32 ) {
    closer( candidate - TWO_32 );
  }
  closer( candidate + TWO_32 );

  return best;
}
