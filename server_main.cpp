#include <iostream>
#include "server/server.h"
#include "packet/packet.h"

namespace remote = forceinline::remote;
namespace packets = remote::packets;

int main( ) {
	try {
		remote::async_server server( "1337" );

		// Set the packet handlers beforehand
		server.set_packet_handler( packets::packet_id::text_one, [ ]( remote::async_server* server, SOCKET from, const std::vector< char >& buffer, std::uint8_t flags ) {
			packets::text_packet< packets::packet_id::text_one > packet( buffer, flags );
			
			std::cout << "[1] Client says: " << packet( ).some_string << std::endl;

			// Set a response
			packet( ).some_string = "Hello from server :)";

			// Send a response
			server->send_packet( from, &packet );
		} );

		server.set_packet_handler( packets::packet_id::text_two, [ ]( remote::async_server* server, SOCKET from, const std::vector< char >& buffer, std::uint8_t flags ) {
			// Note the different packet id: we will send a different packet as a response.
			packets::text_packet< packets::packet_id::text_two > packet( buffer, flags );

			std::cout << "[2] Client says: " << packet( ).some_string << std::endl;

			// Construct a response packet
			auto response_data = packets::packet_random_num_t( { 0,  int( GetTickCount( ) ), 0 } );
			packets::simple_packet< packets::packet_random_num_t, packets::packet_id::random_numbers > response_packet( response_data, flags );

			// Send the response
			server->send_packet( from, &response_packet );
		} );

		server.start( );

		while ( server.is_running( ) ) {
			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
		}

		server.close( );
	} catch ( const std::exception& e ) {
		std::cout << e.what( ) << std::endl;
		std::cin.get( );
		return 1;
	}

	return 0;
}

