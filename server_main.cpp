#include "server/server.h"
#include <iostream>

void handler_packet_dynamic( SOCKET from, std::vector< char > buffer, forceinline::socket::async_server* server ) {
	forceinline::socket::packet_dynamic packet( buffer );
	auto data = packet.get_packet( );

	printf( "%i | %i\n", data.container_length, data.some_container.size( ) );
	for ( auto& v : data.some_container )
		printf( "%i\n", v );

	printf( "\n" );

	server->send_packet( from, &packet );
}

int main( ) {
	try {
		forceinline::socket::async_server server( "1337" );
		server.set_packet_handler( forceinline::socket::packet_id::dynamic, handler_packet_dynamic );

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