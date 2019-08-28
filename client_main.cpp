#include "client/client.h"
#include <iostream>

void handler_packet_dynamic( std::vector< char > buffer, forceinline::socket::async_client* client ) {
	forceinline::socket::packet_dynamic packet( buffer );
	auto data = packet.get_packet( );

	printf( "%i | %i:\n", data.container_length, data.some_container.size( ) );
	for ( auto& v : data.some_container )
		printf( "%i\n", v );

	printf( "\n" );
}

int main( ) {
	try {
		forceinline::socket::async_client client( "localhost", "1337" );
		client.set_packet_handler( forceinline::socket::packet_id::dynamic, handler_packet_dynamic );
		
		client.connect( );
		while ( client.is_connected( ) ) {
			std::cin.get( );
			forceinline::socket::packet_dynamic_t packet_data;

			srand( rand( ) );
			auto to_insert = ( rand( ) % 5 ) + 1;

			for ( int i = 0; i < to_insert; i++ )
				packet_data.some_container.push_back( rand( ) % 256 );

			printf( "Container data [%i]:\n", to_insert );

			for ( auto& v: packet_data.some_container )
				printf( "%i\n", v );

			printf( "--------------\n" );

			forceinline::socket::packet_dynamic packet( packet_data );
			client.send_packet( &packet );
		}

		client.disconnect( );
	} catch ( const std::exception& e ) {
		std::cout << e.what( ) << std::endl;
		std::cin.get( );
	}

	return 0;
}
