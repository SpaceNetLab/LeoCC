/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <vector>
#include <string>

#include "delay_queue.hh"
#include "util.hh"
#include "exception.hh"
#include "timestamp.hh"
#include "ezio.hh"
#include "packetshell.cc"

using namespace std;

size_t count_env_vars(char **env) {
    size_t count = 0;
    while (env[count] != nullptr) {
        count++;
    }
    return count;
}

char **add_env_var(char **original_env, const std::string &new_var) {
    size_t original_count = count_env_vars(original_env);
    char **new_env = new char*[original_count + 2];

    for (size_t i = 0; i < original_count; ++i) {
        new_env[i] = original_env[i];
    }

    new_env[original_count] = strdup(new_var.c_str());
    new_env[original_count + 1] = nullptr;

    return new_env;
}

int main( int argc, char *argv[] )
{
    try {
        const bool passthrough_until_signal = getenv( "MAHIMAHI_PASSTHROUGH_UNTIL_SIGNAL" );

        const uint64_t base_timestamp = initial_timestamp();
        std::string timestamp_str = std::to_string(base_timestamp);
        std::string env_var = "BASE_TIMESTAMP=" + timestamp_str;

        char **original_env = environ;
        char ** const user_environment = add_env_var(original_env, env_var);

        environ = nullptr;

        check_requirements( argc, argv );

        if ( argc < 2 ) {
            throw runtime_error( "Usage: " + string( argv[ 0 ] ) 
                + " interval-milliseconds delay-tracefile [command...]" );
        }

        const uint64_t interval_ms = myatoi( argv[ 1 ] );
        const string delay_filename = argv[2];

        vector< string > command;

        if ( argc == 3 ) {
            command.push_back( shell_path() );
        } else {
            for ( int i = 3; i < argc; i++ ) {
                command.push_back( argv[ i ] );
            }
        }

        PacketShell<DelayQueue> delay_shell_app( "delay", user_environment, passthrough_until_signal );

        delay_shell_app.start_uplink( "[delay " + to_string( interval_ms ) + " ms-int] ",
                                      command,
                                      interval_ms, delay_filename );
        delay_shell_app.start_downlink( interval_ms, delay_filename );
        return delay_shell_app.wait_for_exit();
    } catch ( const exception & e ) {
        print_exception( e );
        return EXIT_FAILURE;
    }
}
