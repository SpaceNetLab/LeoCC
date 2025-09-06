/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <limits>
#include <iostream>
#include "delay_queue.hh"
#include "timestamp.hh"

using namespace std;

DelayQueue::DelayQueue(uint64_t interval_ms, const string &filename )
    : base_timestamp_(),
    interval_ms_(interval_ms),
    packet_queue_(),
    delays_ms_() {
    // align with varying link capacity
    base_timestamp_ = std::stoull(getenv("BASE_TIMESTAMP"));

    ifstream trace_file( filename );

    if ( not trace_file.good() ) {
        throw runtime_error( filename + ": error opening for reading" );
    }

    string line;

    while ( trace_file.good() and getline( trace_file, line ) ) {
        if ( line.empty() ) {
            throw runtime_error( filename + ": invalid empty line" );
        }

        const uint64_t ms = stoi( line );

        delays_ms_.emplace_back( ms );
    }
    if ( delays_ms_.empty() ) {
        throw runtime_error( filename + ": no valid timestamps found" );
    }
}

bool DelayQueue::cmp::operator()(pair<uint64_t, string> &a, pair<uint64_t, string> &b) {
    return a.first > b.first;
}

void DelayQueue::read_packet( const string & contents )
{
    uint64_t now = raw_timestamp();
    unsigned delay_idx = (now - base_timestamp_) / interval_ms_ % delays_ms_.size();
    packet_queue_.emplace( now + delays_ms_[delay_idx], contents );
}

void DelayQueue::write_packets( FileDescriptor & fd )
{
    while ( (!packet_queue_.empty())
            && (packet_queue_.top().first <= raw_timestamp()) ) {
        fd.write( packet_queue_.top().second );
        packet_queue_.pop();
    }
}

unsigned int DelayQueue::wait_time( void ) const
{
    if ( packet_queue_.empty() ) {
        return numeric_limits<uint16_t>::max();
    }

    const auto now = raw_timestamp();
    if ( packet_queue_.top().first <= now ) {
        return 0;
    } else {
        return packet_queue_.top().first - now;
    }

}
