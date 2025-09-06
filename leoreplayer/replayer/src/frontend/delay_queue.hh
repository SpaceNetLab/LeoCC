/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef DELAY_QUEUE_HH
#define DELAY_QUEUE_HH

#include <queue>
#include <cstdint>
#include <string>
#include <fstream>

#include "file_descriptor.hh"
#include "ezio.hh"

class DelayQueue
{
private:
    struct cmp {
        bool operator()(std::pair<uint64_t, std::string> &a, std::pair<uint64_t, std::string> &b);
    };
    uint64_t base_timestamp_;
    uint64_t interval_ms_;
    std::priority_queue<
        std::pair<uint64_t, std::string>,
        std::vector<std::pair<uint64_t, std::string>>,
        DelayQueue::cmp > packet_queue_;
    std::vector<uint64_t> delays_ms_;

public:
    DelayQueue(uint64_t interval_ms, const std::string & filename);

    void read_packet( const std::string & contents );

    void write_packets( FileDescriptor & fd );

    unsigned int wait_time( void ) const;

    bool pending_output( void ) const { return wait_time() <= 0; }

    static bool finished( void ) { return false; }
};

#endif /* DELAY_QUEUE_HH */
