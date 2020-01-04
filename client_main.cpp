#include <iostream>
#include "client/client.h"
#include "packet/packet.h"

namespace remote = forceinline::remote;
namespace packets = remote::packets;

int main( ) {
	try {
		remote::async_client client( "localhost", "1337" );

		/*
			Set the handlers before connecting (you can do it later too, but it's better
			to set them first so you don't miss any packets.
		*/

		client.set_packet_handler( packets::packet_id::text_one, [ ]( remote::async_client* client, const std::vector< char >& buffer, const std::uint8_t flags ) {
			// Read the packet (server replies with the same packet)
			remote::packets::text_packet< packets::packet_id::text_one > response( buffer, flags );

			std::cout << "Hello from static packet handler! Server says: " << response( ).some_string << std::endl;
		} );

		// Connect after we're finished
		client.connect( );

		// First we send a packet normally (this means the packet handler, if given, will be called)
		std::cout << "Sending packet normally." << std::endl;

		packets::text_packet< packets::packet_id::text_one > packet_1( { "Hello from dynamic packet 1!" } );
		client.send_packet( &packet_1 );

		// Now we send it with a custom handler
		std::cout << "Sending 10 packets with a custom handler." << std::endl;
		
		for ( int i = 0; i < 10; i++ ) {
			packets::text_packet< packets::packet_id::text_two > packet_2( { "Hello from dynamic packet 2!" } );
			bool result = client.send_packet( &packet_2, [ ]( const std::vector< char >& buffer, const std::uint8_t flags ) {
				std::cout << "Hello from custom handler!" << std::endl;

				/*
					Read the packet (server responds with a different packet!)
					Note: since we are sending just 'one' same-size item, we don't need a struct specifically
					for this packet.
				*/
				packets::simple_packet< packets::packet_random_num_t, packets::packet_id::random_numbers > response( buffer, flags );
				return response( ).numbers[ 1 ] % 2 == 0; // Return something random based on the servers response.
			}, std::chrono::seconds( 10 ) );

			// Do something with the result
			std::cout << "Result " << i + 1 << " was " << ( result ? "true." : "false." ) << std::endl;

			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
		}

		// Disconnect after we're done.
		client.disconnect( );
	} catch ( const std::exception& e ) {
		std::cout << e.what( ) << std::endl;
		std::cin.get( );
		return 1;
	}
	
	return 0;
}
