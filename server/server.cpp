#include "server.h"
#include <algorithm>

namespace forceinline::remote {
	async_server::async_server( std::string_view port ) {
		if ( port.empty( ) )
			throw std::invalid_argument( "async_server::async_server: port argument empty" );

		m_port = port;
	}

	async_server::~async_server( ) {
		close( );
		m_packet_handlers.clear( );
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

		// Create a socket
		m_server_socket = ::socket( result->ai_family, result->ai_socktype, result->ai_protocol );

		if ( m_server_socket == INVALID_SOCKET )
			throw std::exception( "" );

		// Bind the socket
		if ( bind( m_server_socket, result->ai_addr, int( result->ai_addrlen ) ) == SOCKET_ERROR )
			throw std::exception( "" );

		// Listen on the socket
		if ( listen( m_server_socket, SOMAXCONN ) == SOCKET_ERROR )
			throw std::exception( "" );

		freeaddrinfo( result );

		// Mark the server as running
		m_running = true;

		// Start our threads
		m_accept_thread = std::thread( &async_server::accept, this );
		m_receive_thread = std::thread( &async_server::receive, this );
		m_process_thread = std::thread( &async_server::process_packets, this );
	}

	void async_server::close( ) {
		if ( !m_running || !m_server_socket )
			return;

		// Shut our socket down
		closesocket( m_server_socket );

		// Let the threads know we're not running anymore
		m_running = false;

		// Wait for our threads to finish
		m_accept_thread.join( );
		m_receive_thread.join( );
		m_process_thread.join( );

		// Shut down the connection
		for ( auto& client_socket : m_connected_clients ) {
			shutdown( client_socket, SD_SEND );
			closesocket( client_socket );
		}

		// Clear the packet queue
		m_packet_queue.clear( );

		// Erase all our clients
		m_connected_clients.clear( );

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
		// Return if our packet is invalid or we have no one to send our packet to
		if ( !to || !packet )
			return;

		// Lock the mutex
		std::unique_lock lock( m_send_mtx );

		// Grab our packet size and ID
		auto packet_id = packet->id( );
		auto packet_size = packet->size( );

		// Allocate a buffer so into which we copy our packet data
		std::vector< char > packet_buffer( packet_size + 2 * sizeof std::uint16_t );

		// Set packet ID and packet length
		memcpy( packet_buffer.data( ), &packet_id, sizeof std::uint16_t );
		memcpy( packet_buffer.data( ) + sizeof std::uint16_t, &packet_size, sizeof std::uint16_t );

		// Finally copy our packet data into the buffer
		memcpy( packet_buffer.data( ) + 2 * sizeof std::uint16_t, packet->data( ), packet_size );

		// Try to send our buffer
		std::size_t bytes_sent = 0, total_bytes_sent = 0;
		do {
			bytes_sent = send( to, packet_buffer.data( ) + total_bytes_sent, packet_buffer.size( ) - total_bytes_sent, NULL );
			total_bytes_sent += bytes_sent;
		} while ( bytes_sent > 0 && total_bytes_sent < packet_buffer.size( ) );

		// An error occurred, remove the client
		if ( bytes_sent <= 0 )
			close_client_connection( to );
	}

	/*
		This function is used for when you ask for a packet. See the client's
		implementation for an explanation.
	*/
	void async_client::request_packet( SOCKET to, std::uint16_t packet_id ) {
		std::vector< char > packet_buffer( 2 * sizeof std::uint16_t );

		memset( packet_buffer.data( ), 0, packet_buffer.size( ) );
		memcpy( packet_buffer.data( ), &packet_id, sizeof std::uint16_t );

		// Lock the send function for other threads until we're done
		std::unique_lock lock( m_send_mtx );

		// Try to send our buffer
		std::size_t bytes_sent = 0, total_bytes_sent = 0;
		do {
			bytes_sent = send( to, packet_buffer.data( ) + total_bytes_sent, packet_buffer.size( ) - total_bytes_sent, NULL );
			total_bytes_sent += bytes_sent;
		} while ( bytes_sent > 0 && total_bytes_sent < packet_buffer.size( ) );

		// An error occurred, remove the client
		if ( bytes_sent <= 0 )
			close_client_connection( to );
	}

	void async_server::accept( ) {
		while ( m_running ) {
			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );

			// Accept an incoming connection
			SOCKET client = ::accept( m_server_socket, NULL, NULL );

			// Error check
			if ( client == INVALID_SOCKET )
				continue;

			// Lock mutex
			std::unique_lock lock( m_client_mtx );

			// Add the client to our list
			m_connected_clients.push_back( client );
		}
	}

	void async_server::receive( ) {
		// Create a temporary buffer
		std::vector< char > temporary_buffer( m_buffer_size );

		while ( m_running ) {
			std::unique_lock cl_lock( m_client_mtx );

			// Loop over all our connected clients
			for ( auto& client : m_connected_clients ) {
				// Query the queued bytes for a socket
				unsigned long queued_bytes = 0;
				ioctlsocket( client, FIONREAD, &queued_bytes );

				// Does our connection have any bytes queued?
				if ( queued_bytes > 0 ) {
					// Receive the bytes
					int received = recv( client, temporary_buffer.data( ), m_buffer_size, NULL );

					// Did we have an error?
					if ( received <= 0 ) {
						close_client_connection( client );
						continue;
					}

					// Lock mutex
					std::unique_lock pp_lock( m_process_mtx );

					// Get the client buffer and fill it with the data we received
					auto& packet_buffer = m_packet_queue[ client ];
					packet_buffer.insert( packet_buffer.end( ), temporary_buffer.data( ), temporary_buffer.data( ) + received );
				}
			}
		}

		//Clear our buffer
		temporary_buffer.clear( );
	}

	void async_server::process_packets( ) {
		while ( m_running ) {
			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
			std::unique_lock lock( m_process_mtx );

			// Process any packets
			for ( auto& to_process : m_packet_queue ) {
				auto& packet_buffer = to_process.second;

				// Check if we have at least a packet header stored
				if ( packet_buffer.size( ) < 2 * sizeof std::uint16_t )
					continue;

				// We have something to process, get the information about our packet
				std::uint32_t packet_information = *reinterpret_cast< std::uint32_t* >( packet_buffer.data( ) );

				// Packet ID is in the first 2 bytes while its size is in the following 2
				std::uint16_t packet_id = packet_information & 0xFFFF;
				std::uint16_t packet_size = ( packet_information >> 16 );

				// Add the first 4 bytes to our packet size
				std::uint16_t total_packet_size = packet_size + 2 * sizeof std::uint16_t;

				// Do we have a whole packet stored?
				if ( packet_buffer.size( ) < total_packet_size )
					continue;

				// Does our packet have a handler?
				if ( m_packet_handlers.find( packet_id ) != m_packet_handlers.end( ) ) {
					// Erase the packet header
					packet_buffer.erase( packet_buffer.begin( ), packet_buffer.begin( ) + 2 * sizeof std::uint16_t );

					// Call the handler
					m_packet_handlers[ packet_id ]( to_process.first /* Client socket */, packet_buffer, this );

					// Remove the packet from our queue
					packet_buffer.erase( packet_buffer.begin( ), packet_buffer.begin( ) + packet_size );
				} else {
					// Packet is invalid/has no handler, erase it
					packet_buffer.erase( packet_buffer.begin( ), packet_buffer.begin( ) + total_packet_size );
				}
			}
		}
	}

	void async_server::close_client_connection( SOCKET client ) {
		std::unique_lock cl_lock( m_client_mtx );
		std::unique_lock pp_lock( m_process_mtx );

		// Find our clients position
		auto queue_it = m_packet_queue.find( client );
		auto conn_it = std::find( m_connected_clients.begin( ), m_connected_clients.end( ), client );

		if ( queue_it != m_packet_queue.end( ) )
			m_packet_queue.erase( queue_it );

		// Is our client in our list?
		if ( conn_it == m_connected_clients.end( ) )
			return;

		// Shut the connection down
		shutdown( *conn_it, SD_SEND );
		closesocket( *conn_it );

		// Remove our client
		m_connected_clients.erase( conn_it );
	}
}