#include "client/client.h"
#include <iostream>

int pkt = 0;
void handler_packet_simple( std::vector< char > buffer, forceinline::socket::async_client* client ) {
	forceinline::socket::packet_simple packet( buffer );
	printf( "%i | %i\n", ++pkt, packet.m_data.some_number );
}

int main( ) {
	try {
		forceinline::socket::async_client client( "localhost", "1337" );
		client.set_packet_handler( forceinline::socket::packet_id::simple, handler_packet_simple );
		
		client.connect( );
		if ( client.is_connected( ) ) {
			for ( int i = 1; i <= 250; i++ ) {
				forceinline::socket::packet_simple_t packet_data;

				packet_data.some_float = 123.456f;
				packet_data.some_number = i;
				memset( packet_data.some_array, 2, 3 );

				forceinline::socket::packet_simple packet( packet_data );
				client.send_packet( &packet );
			}

			while ( client.is_connected( ) ) {
				std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
			}
		}
	} catch ( const std::exception& e ) {
		std::cout << e.what( ) << std::endl;
		std::cin.get( );
	}

	std::cin.get( );
	return 0;
}
