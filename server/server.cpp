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

	#ifdef WIN32
		if ( WSAStartup( MAKEWORD( 2, 2 ), &m_wsa_data ) != 0 )
			throw std::exception( "async_server::start: WSAStartup call failed" );
	#endif // WIN32

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
		if ( m_accept_thread.joinable( ) )
			m_accept_thread.join( );

		if ( m_receive_thread.joinable( ) )
			m_receive_thread.join( );
		
		if ( m_process_thread.joinable( ) )
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

	#ifdef WIN32
		WSACleanup( );
	#endif // WIN32
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

	void async_server::send_packet( SOCKET to, packets::packet_base::base_packet* packet ) {
		send_packet_internal( to, packet, packet->flags( ) );
	}

	bool async_server::send_packet( SOCKET to, packets::packet_base::base_packet* packet, std::function< bool( SOCKET, const std::vector< char >&, const std::uint8_t ) > handler, std::chrono::milliseconds timeout ) {
		// Latest packet handler. In range of 1-127
		std::uint8_t packet_identifier = generate_packet_identifier( to );

		// Send the packet with according flags
		send_packet_internal( to, packet, packet_identifier );

		auto now = [ ]( ) {
			return std::chrono::high_resolution_clock::now( );
		};

		// Wait for the packet to arrive within timeout limit		
		auto time_sent = now( );
		bool handler_result = false;
		do {
			m_custom_mtx.lock( );

			auto& queue = m_custom_process_queue[ to ];

			// See if we have a response packet queued
			auto it = std::find_if( queue.begin( ), queue.end( ), [ packet_identifier ]( const custom_process_info_t& info ) {
				return info.packet_identifier == packet_identifier;
			} );

			// Call the handler if a packet has been found
			if ( it != queue.end( ) ) {
				handler_result = handler( to, it->packet_data, it->packet_identifier | 0b10000000 );
				queue.erase( it );
				m_custom_mtx.unlock( );
				break;
			}

			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
		} while ( std::chrono::duration_cast< std::chrono::milliseconds >( now( ) - time_sent ) <= timeout );

		m_custom_mtx.unlock( );
		remove_packet_identifier( to, packet_identifier );
		return handler_result;
	}

	void async_server::send_packet_internal( SOCKET to, packets::packet_base::base_packet* packet, std::uint8_t packet_flags ) {
		// Return if our packet is invalid or we have no one to send our packet to
		if ( !to || !packet )
			return;

		packet_header_t header( packet );

		// Set the packet flags if they don't match
		if ( packet->flags( ) != packet_flags )
			header.packet_flags = packet_flags;

		// Allocate a buffer into which we copy our packet data
		std::vector< char > packet_buffer( sizeof packet_header_t + header.packet_size );

		// Copy the header and packet data into the buffer
		memcpy( packet_buffer.data( ), &header, sizeof packet_header_t );
		memcpy( packet_buffer.data( ) + sizeof packet_header_t, packet->data( ), header.packet_size );

		// Lock the mutex
		std::lock_guard lock( m_send_mtx );

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
			std::lock_guard lock( m_client_mtx );

			// Add the client to our list
			m_connected_clients.push_back( client );
		}
	}

	void async_server::receive( ) {
		// Create a temporary buffer
		std::vector< char > temporary_buffer( m_buffer_size );

		while ( m_running ) {
			std::lock_guard cl_lock( m_client_mtx );

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
					std::lock_guard pp_lock( m_process_mtx );

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
			std::lock_guard lock( m_process_mtx );

			// Process any packets
			for ( auto& to_process : m_packet_queue ) {
				auto& from = to_process.first;
				auto& packet_buffer = to_process.second;		

				// Check if we have at least a packet header stored
				if ( packet_buffer.size( ) < sizeof packet_header_t )
					continue;

				// We have something to process, get the information about our packet
				auto header = *reinterpret_cast< packet_header_t* >( packet_buffer.data( ) );

				// Add the first 4 bytes to our packet size
				std::uint16_t total_packet_size = sizeof packet_header_t + header.packet_size;

				// Do we have a whole packet stored?
				if ( packet_buffer.size( ) < total_packet_size )
					continue;

				// Does our packet have a handler?
				if ( m_packet_handlers.find( header.packet_id ) != m_packet_handlers.end( ) || header.packet_flags & 0b10000000 /* Custom handler */ ) {
					// Erase the packet header
					packet_buffer.erase( packet_buffer.begin( ), packet_buffer.begin( ) + sizeof packet_header_t );

					// Add the packet to custom processing queue if marked as one
					if ( header.packet_flags & 0b10000000 ) {
						// Create an info structure
						custom_process_info_t info( header.packet_flags & 0x7F /* Extract the packet identifier */ );

						// Copy the packet buffer
						info.packet_data = std::vector< char >( packet_buffer.begin( ), packet_buffer.begin( ) + header.packet_size );

						std::lock_guard custom_lock( m_custom_mtx );

						// Add it to the queue
						m_custom_process_queue[ from ].push_back( info );
					} else {
						// If the packet has an identifier, mark it as an answer packet
						if ( header.packet_flags & 0x7F )
							header.packet_flags |= 0b10000000;

						// Call the packet handler
						m_packet_handlers[ header.packet_id ]( this, to_process.first /* from socket */, packet_buffer, header.packet_flags );
					}

					// Remove the packet from our queue
					packet_buffer.erase( packet_buffer.begin( ), packet_buffer.begin( ) + header.packet_size );
				} else {
					// Packet is invalid/has no handler, erase it
					packet_buffer.erase( packet_buffer.begin( ), packet_buffer.begin( ) + total_packet_size );
				}
			}
		}
	}

	void async_server::close_client_connection( SOCKET client ) {
		std::lock_guard cl_lock( m_client_mtx );
		std::lock_guard pp_lock( m_process_mtx );

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

	std::uint8_t async_server::generate_packet_identifier( SOCKET to ) {
		// TODO: Don't assign packet identifiers if next one would be 1
		auto& identifiers = m_packet_identifiers[ to ];

		if ( identifiers.size( ) > 0 ) {
			std::uint8_t last_identifier = identifiers.back( );
			std::uint8_t next_identifier = ( ++last_identifier % 127 ) + 1; // 1-127
			identifiers.push_back( next_identifier );

			return next_identifier;
		} else {
			identifiers.push_back( 1 );
			return 1;
		}
	}

	void async_server::remove_packet_identifier( SOCKET to, std::uint8_t identifier ) {
		auto& identifiers = m_packet_identifiers[ to ];
		identifiers.erase( std::remove_if( identifiers.begin( ), identifiers.end( ), [ identifier ]( std::uint8_t id ) {
			return id == identifier;
		} ) );
	}
} // namespace forceinline::remote