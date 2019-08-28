#include "server.h"
#include <algorithm>

namespace forceinline::socket {
	async_server::async_server( std::string_view port ) {
		if ( port.empty( ) )
			throw std::invalid_argument( "async_server::async_server: port argument empty" );

		m_port = port;
	}

	async_server::~async_server( ) {
		close( );
		WSACleanup( );
	}

	void async_server::start( ) {
		if ( m_running )
			throw std::exception( "async_server::start: already running" );

		if ( WSAStartup( MAKEWORD( 2, 2 ), &m_wsa_data ) != 0 )
			throw std::exception( "async_server::start: WSAStartup call failed" );

		struct addrinfo* result, hints;	
		ZeroMemory( &hints, sizeof( hints ) );
		
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = AI_PASSIVE;

		if ( getaddrinfo( NULL, m_port.data( ), &hints, &result ) != 0 )
			throw std::exception( "" );

		m_server_socket = ::socket( result->ai_family, result->ai_socktype, result->ai_protocol );

		if ( m_server_socket == INVALID_SOCKET )
			throw std::exception( "" );

		if ( bind( m_server_socket, result->ai_addr, int( result->ai_addrlen ) ) == SOCKET_ERROR )
			throw std::exception( "" );

		if ( listen( m_server_socket, SOMAXCONN ) == SOCKET_ERROR )
			throw std::exception( "" );

		freeaddrinfo( result );

		m_running = true;

		//Start our threads for accepting clients receiving data
		m_accept_thread = std::thread( &async_server::accept, this );
		m_receive_thread = std::thread( &async_server::receive, this );
	}

	void async_server::close( ) {
		if ( !m_running || !m_server_socket )
			throw std::exception( "async_server::close: attempted to close server while it wasn't running" );
	
		//Lock our client mutex so other threads cannot access the clients
		m_client_mtx.lock( );

		//Shut down the connection
		for ( auto& client_socket : m_connected_clients ) {
			shutdown( client_socket, SD_SEND );
			closesocket( client_socket );
		}

		//Erase all our clients
		m_connected_clients.clear( );

		//Unlock the mutex for other threads
		m_client_mtx.unlock( );

		m_running = false;
		WSACleanup( );
	}
		 
	bool async_server::is_running( ) {
		return m_running;
	}
		 
	void async_server::set_packet_handler( std::uint16_t packet_id, packet_handler_server_fn handler ) {
		if ( handler )
			m_packet_handlers[ packet_id ] = handler;
		else
			m_packet_handlers.erase( packet_id );
	}
		 
	void async_server::send_packet( SOCKET to, base_packet* packet ) {
		//Return if our packet is invalid or we have no one to send our packet to
		if ( !to || !packet )
			return;

		//Lock the mutex
		m_send_mtx.lock( );

		//Grab our packet size and ID
		auto packet_id = packet->id( );
		auto packet_size = packet->size( );

		//Allocate a buffer so into which we copy our packet data
		std::vector< char > packet_buffer( packet_size + 2 * sizeof std::uint16_t );

		//Set packet ID and packet length
		memcpy( packet_buffer.data( ), &packet_id, sizeof std::uint16_t );
		memcpy( packet_buffer.data( ) + sizeof std::uint16_t, &packet_size, sizeof std::uint16_t );

		//Finally copy our packet data into the buffer
		memcpy( packet_buffer.data( ) + 2 * sizeof std::uint16_t, packet->data( ), packet_size );

		//Try to send our buffer
		std::size_t bytes_sent = 0, total_bytes_sent = 0;
		do {
			bytes_sent = send( to, packet_buffer.data( ) + total_bytes_sent, packet_buffer.size( ) - total_bytes_sent, NULL );
			total_bytes_sent += bytes_sent;
		} while ( bytes_sent > 0 && total_bytes_sent < packet_buffer.size( ) );

		//An error occurred, remove the client
		if ( bytes_sent <= 0 )
			close_client_connection( to );

		//Unlock our mutex so the next packet can be sent
		m_send_mtx.unlock( );
	}

	void async_server::accept( ) {
		while ( m_running ) {
			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );

			//Accept an incoming connection
			SOCKET client = ::accept( m_server_socket, NULL, NULL );

			//Error check
			if ( client == INVALID_SOCKET )
				continue;

			//Lock our mutex so other threads cannot access it
			m_client_mtx.lock( );
			
			//Add the client to our list
			m_connected_clients.push_back( client );

			//Remove the client from our list
			m_client_mtx.unlock( );
		}
	}

	void async_server::receive( ) {
		while ( m_running ) {
			m_client_mtx.lock( );

			//Loop over all our connected clients
			for ( auto& client : m_connected_clients ) {
				//Query the queued bytes for a socket
				unsigned long queued_bytes = 0;
				ioctlsocket( client, FIONREAD, &queued_bytes );

				//Does our socket have any bytes queued?
				if ( queued_bytes > 0 ) {
					//Receive the bytes; close connection on error
					if ( !receive_from_client( client ) )
						close_client_connection( client );
				}
			}

			m_client_mtx.unlock( );
		}
	}

	//fixme: read whole buffer, not just the first packet in it
	//todo: add is alive packet to detect closed connections
	bool async_server::receive_from_client( SOCKET from ) {
		auto receive = [ & ]( SOCKET from, std::uint16_t size, char* buffer ) {
			int received = 0, total_received = 0;

			//Try to receive as much as we requested
			do {
				received = recv( from, buffer + total_received, size - total_received, NULL );
				total_received += received;
			} while ( received > 0 && total_received < size );

			//Did we receive what we requested?
			return total_received == size;
		};

		//Clear and prepare the buffer
		m_buffer.clear( );
		m_buffer.resize( m_packet_size );

		//Check how many bytes we have received
		int bytes_received = recv( from, m_buffer.data( ), m_buffer.size( ), NULL );

		//We haven't received enough information about our packet, try to get more
		if ( bytes_received < 4 ) {
			//We failed to receive enough bytes, disconnect as an error has occurred
			if ( !receive( from, 4 - bytes_received, m_buffer.data( ) + bytes_received ) )
				return false;
		}

		//Get the information about our packet
		std::uint32_t packet_information = *reinterpret_cast< std::uint32_t* >( m_buffer.data( ) );

		//Packet ID is in the first 2 bytes while its size is in the following 2
		std::uint16_t packet_id = packet_information & 0xFFFF;
		std::uint16_t packet_size = packet_information & 0xFFFF0000;

		//Add the first 4 bytes to our packet size
		std::uint16_t total_packet_size = packet_size + 2 * sizeof std::uint16_t;

		//The packet is larger than our buffer for it
		if ( total_packet_size > m_buffer.size( ) ) {
			//Resize the buffer accordingly
			m_buffer.insert( m_buffer.end( ), total_packet_size - m_buffer.size( ), 0 );
		}

		//We haven't received the whole packet, keep listening
		if ( bytes_received < packet_size ) {
			//Failed to receive whole packet, error -> disconnect
			if ( !receive( from, packet_size - bytes_received, m_buffer.data( ) + bytes_received ) )
				return false;
		}

		//Does a handler exist for the packet?
		if ( m_packet_handlers.find( packet_id ) != m_packet_handlers.end( ) ) {
			//Remove the first four bytes from our buffer as we only pass the packet data to our handler
			m_buffer.erase( m_buffer.begin( ), m_buffer.begin( ) + 4 );

			//Copy the buffer and call the packet handler
			m_packet_handlers[ packet_id ]( from, m_buffer, this );
		}

		return true;
	}

	void async_server::close_client_connection( SOCKET client ) {
		m_client_mtx.lock( );

		//Find our clients position
		auto it = std::find( m_connected_clients.begin( ), m_connected_clients.end( ), client );

		//Is our client in our list?
		if ( it != m_connected_clients.end( ) ) {
			//Shut the connection down
			shutdown( *it, SD_SEND );
			closesocket( *it );

			//Remove our client
			m_connected_clients.erase( it );
		}

		m_client_mtx.unlock( );
	}
}