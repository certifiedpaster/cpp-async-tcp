#include "server/server.h"
#include <iostream>

int pkt = 0;
void handler_packet_simple( SOCKET from, std::vector< char > buffer, forceinline::remote::async_server* server ) {
	forceinline::remote::packet_simple packet( buffer );
	printf( "%i | %i\n", ++pkt, packet.m_data.some_number );
	server->send_packet( from, &packet );
}

int main( ) {
	try {
		forceinline::remote::async_server server( "1337" );
		server.set_packet_handler( forceinline::remote::packet_id::simple, handler_packet_simple );

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