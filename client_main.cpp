#include "client/client.h"
#include <iostream>

#define print_var(x) (printf("%s: %s\n", #x, std::to_string(x).data()))

namespace remote = forceinline::remote;

int pkt = 0;
void handler_packet_simple( std::vector< char > buffer, remote::async_client* client ) {
	remote::simple_packet< remote::packet_simple_t, remote::packet_id::simple > packet( buffer );
	
	print_var( packet( ).some_float );
	print_var( packet( ).some_number );
	print_var( packet( ).some_array[ 1 ] );
	printf( "\n" );
}


void handler_packet_dynamic( std::vector< char > buffer, remote::async_client* client ) {
	remote::dynamic_packet packet( buffer );

	print_var( packet( ).some_container[ 2 ] );
	printf( "\n" );
}

int main( ) {
	try {
		remote::async_client client( "localhost", "1337" );
		client.set_packet_handler( remote::packet_id::simple, handler_packet_simple );
		
		client.connect( );
		if ( client.is_connected( ) ) {
			for ( int i = 1; i <= 10; i++ ) {
				remote::packet_dynamic_t data;
				data.some_container.push_back( 0 );
				data.some_container.push_back( i );
				data.some_container.push_back( 2 );

				remote::dynamic_packet packet( data );
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
