/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef TIMESTAMP_HH
#define TIMESTAMP_HH

#include <cstdint>

uint64_t timestamp( void );
uint64_t raw_timestamp( void );
uint64_t raw_timestamp_us( void );
uint64_t initial_timestamp( void );

#endif /* TIMESTAMP_HH */
