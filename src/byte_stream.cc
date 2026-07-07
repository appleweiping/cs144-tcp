#include "byte_stream.hh"

#include <algorithm>
#include <stdexcept>

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

void Writer::push( string data )
{
  if ( closed_ or error_ ) {
    return;
  }

  const uint64_t to_write = min( data.size(), available_capacity() );
  if ( to_write == 0 ) {
    return;
  }

  if ( to_write < data.size() ) {
    data.resize( to_write );
  }
  buffer_ += data;
  bytes_pushed_ += to_write;
}

void Writer::close()
{
  closed_ = true;
}

void Writer::set_error()
{
  error_ = true;
}

bool Writer::is_closed() const
{
  return closed_;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - buffer_.size();
}

uint64_t Writer::bytes_pushed() const
{
  return bytes_pushed_;
}

string_view Reader::peek() const
{
  return string_view { buffer_ };
}

bool Reader::is_finished() const
{
  return closed_ and buffer_.empty();
}

bool Reader::has_error() const
{
  return error_;
}

void Reader::pop( uint64_t len )
{
  const uint64_t to_pop = min( len, static_cast<uint64_t>( buffer_.size() ) );
  buffer_.erase( 0, to_pop );
  bytes_popped_ += to_pop;
}

uint64_t Reader::bytes_buffered() const
{
  return buffer_.size();
}

uint64_t Reader::bytes_popped() const
{
  return bytes_popped_;
}
