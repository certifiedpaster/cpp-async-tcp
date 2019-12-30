#include "server/server.h"
#include <iostream>

#define print_var(x) (printf("%s: %s\n", #x, std::to_string(x).data()))

namespace remote = forceinline::remote;

void handler_packet_simple( SOCKET from, std::vector< char > buffer, remote::async_server* server ) {
	remote::simple_packet< remote::packet_simple_t, remote::packet_id::simple > packet( buffer );
	
	print_var( packet( ).some_float );
	print_var( packet( ).some_number );
	print_var( packet( ).some_array[ 1 ] );
	printf( "\n" );

	server->send_packet( from, &packet );
}

void handler_packet_dynamic( SOCKET from, std::vector< char > buffer, remote::async_server* server ) {
	remote::dynamic_packet packet( buffer );

	print_var( packet( ).some_container[ 0 ] );
	print_var( packet( ).some_container[ 1 ] );
	print_var( packet( ).some_container[ 2 ] );
	printf( "\n" );

	server->send_packet( from, &packet );
}

int main( ) {
	try {
		remote::async_server server( "1337" );
		server.set_packet_handler( remote::packet_id::simple, handler_packet_simple );
		server.set_packet_handler( remote::packet_id::dynamic, handler_packet_dynamic );

		server.start( );
		while ( server.is_running( ) ) {
			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
		}

		server.close( );
	} catch ( const std::exception& e ) {
		std::cout << e.what( ) << std::endl;
		std::cin.get( );
	}

	return 0;
}