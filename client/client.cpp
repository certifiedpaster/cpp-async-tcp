#include "client.h"
#include <functional>
#include <algorithm>

namespace forceinline::remote {
	async_client::async_client( std::string_view ip, std::string_view port ) {
		if ( ip.empty( ) )
			throw std::invalid_argument( "async_client::async_client: ip argument is empty" );

		if ( port.empty( ) )
			throw std::invalid_argument( "async_client::async_client: port argument is empty" );

		m_ip = ip;
		m_port = port;
	}

	async_client::~async_client( ) {
		disconnect( );
		m_packet_handlers.clear( );
	}

	void async_client::connect( ) {
		if ( m_connected )
			return;

	#ifdef WIN32
		if ( WSAStartup( MAKEWORD( 2, 2 ), &m_wsa_data ) != 0 )
			throw std::exception( "async_client::connect: WSAStartup call failed" );
	#endif // WIN32

		struct addrinfo hints, * result;
		ZeroMemory( &hints, sizeof( hints ) );

		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		if ( getaddrinfo( m_ip.data( ), m_port.data( ), &hints, &result ) != 0 )
			throw std::exception( "async_client::connect: getaddrinfo call failed" );

		// Create a socket
		m_socket = ::socket( result->ai_family, result->ai_socktype, result->ai_protocol );

		if ( m_socket == INVALID_SOCKET )
			throw std::exception( "async_client::connect: socket creation failed" );

		// Connect to the server
		if ( ::connect( m_socket, result->ai_addr, int( result->ai_addrlen ) ) == SOCKET_ERROR )
			throw std::exception( "async_client::connect: failed to connect to host" );

		freeaddrinfo( result );

		// Mark the client as connected
		m_connected = true;
		m_receive_thread = std::thread( &async_client::receive, this );
		m_process_thread = std::thread( &async_client::process_packets, this );
	}

	void async_client::disconnect( ) {
		m_connected = false;

		// Wait for our threads to finish
		if ( m_receive_thread.joinable( ) )
			m_receive_thread.join( );
		
		if ( m_process_thread.joinable( ) )
			m_process_thread.join( );

		// Tell the server we disconnected
		if ( m_socket ) {
			shutdown( m_socket, SD_SEND );
			closesocket( m_socket );
			m_socket = NULL;
		}
		
	#ifdef WIN32
		WSACleanup( );
	#endif // WIN32
	}

	bool async_client::is_connected( ) {
		return m_connected;
	}

	void async_client::set_packet_handler( std::uint16_t packet_id, packet_handler_client_fn handler ) {
		if ( handler )
			m_packet_handlers[ packet_id ] = handler;
		else
			m_packet_handlers.erase( packet_id );
	}

	void async_client::send_packet( packets::packet_base::base_packet* packet ) {
		if ( !packet )
			return;

		send_packet_internal( packet, packet->flags( ) );
	}

	/*
		This function is used for when you want to handle a packet response differently
		than the standard handler, or if you want to deal with the response in the thread
		you're calling this function from. For further clarification, see the example files.

		When using this function, it does NOT call the assigned packet id's handler but the
		function that has been passed in the handler argument.

		timeout_seconds is the maximum time this function will wait for a response packet.
		If a response does not arrive in time, it will return false.
	*/
	bool async_client::send_packet( packets::packet_base::base_packet* packet, std::function< bool( const std::vector< char >&, const std::uint8_t ) > handler, std::chrono::milliseconds timeout ) {
		// Latest packet handler. In range of 1-127
		std::uint8_t packet_identifier = generate_packet_identifier( );

		// Send the packet with according flags
		send_packet_internal( packet, packet_identifier );
		
		auto now = [ ]( ) {
			return std::chrono::high_resolution_clock::now( );
		};

		// Wait for the packet to arrive within timeout limit		
		auto time_sent = now( );
		bool handler_result = false;
		do {
			m_custom_mtx.lock( );

			// See if we have a response packet queued
			auto it = std::find_if( m_custom_process_queue.begin( ), m_custom_process_queue.end( ), [ packet_identifier ]( const custom_process_info_t& info ) {
				return info.packet_identifier == packet_identifier;
			} );

			// Call the handler if a packet has been found
			if ( it != m_custom_process_queue.end( ) ) {
				handler_result = handler( it->packet_data, it->packet_identifier | 0b10000000 );
				m_custom_process_queue.erase( it );
				break;
			}
			
			m_custom_mtx.unlock( );
			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
		} while ( std::chrono::duration_cast< std::chrono::milliseconds >( now( ) - time_sent ) <= timeout );

		m_custom_mtx.unlock( );
		remove_packet_identifier( packet_identifier );
		return handler_result;
	}

	void async_client::send_packet_internal( packets::packet_base::base_packet* packet, std::uint8_t packet_flags ) {
		// Return if our packet is invalid
		if ( !packet )
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

		// Lock the send function for other threads until we're done
		std::lock_guard lock( m_send_mtx );

		// Try to send our buffer
		std::size_t bytes_sent = 0, total_bytes_sent = 0;
		do {
			bytes_sent = send( m_socket, packet_buffer.data( ) + total_bytes_sent, packet_buffer.size( ) - total_bytes_sent, NULL );
			total_bytes_sent += bytes_sent;
		} while ( bytes_sent > 0 && total_bytes_sent < packet_buffer.size( ) );

		// An error occurred, disconnect from server
		if ( bytes_sent <= 0 )
			m_connected = false;
	}

	void async_client::receive( ) {
		// Create a temporary buffer
		std::vector< char > temporary_buffer = std::vector< char >( m_buffer_size );

		// Loop and receive
		do {
			// Check how many bytes we have received
			int bytes_received = recv( m_socket, temporary_buffer.data( ), m_buffer_size, NULL );

			// An error occurred, break out and disconnect
			if ( bytes_received <= 0 )
				break;

			// Lock the process mutex
			std::lock_guard lock( m_process_mtx );

			// Copy the received bytes into our queue
			m_packet_queue.insert( m_packet_queue.end( ), temporary_buffer.data( ), temporary_buffer.data( ) + bytes_received );

		} while ( m_connected );

		// Clear the temporary buffer and disconnect
		temporary_buffer.clear( );
		m_connected = false;
	}

	void async_client::process_packets( ) {
		while ( m_connected ) {
			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
			std::lock_guard lock( m_process_mtx );

			// Check if we have at least a packet header stored
			if ( m_packet_queue.size( ) < sizeof packet_header_t )
				continue;

			// We have something to process, get the information about our packet
			auto header = *reinterpret_cast< packet_header_t* >( m_packet_queue.data( ) );

			// Add the first 4 bytes to our packet size
			std::uint16_t total_packet_size = sizeof packet_header_t + header.packet_size;

			// Do we have a whole packet stored?
			if ( m_packet_queue.size( ) < total_packet_size )
				continue;

			// Does our packet have a handler?
			if ( m_packet_handlers.find( header.packet_id ) != m_packet_handlers.end( ) || header.packet_flags & 0b10000000 /* Custom handler */ ) {
				// Erase the packet header but keep the flags
				m_packet_queue.erase( m_packet_queue.begin( ), m_packet_queue.begin( ) + sizeof packet_header_t );

				// Add the packet to custom processing queue if marked as one
				if ( header.packet_flags & 0b10000000 ) {
					// Create an info structure
					custom_process_info_t info( header.packet_flags & 0x7F /* Extract the packet identifier */ );
					
					// Copy the packet buffer
					info.packet_data = std::vector< char >( m_packet_queue.begin( ), m_packet_queue.begin( ) + header.packet_size );

					m_custom_mtx.lock( );

					// Add it to the queue
					m_custom_process_queue.push_back( info );

					m_custom_mtx.unlock( );
				} else {
					if ( header.packet_flags & 0x7F )
						header.packet_flags |= 0b10000000;

					// Call the packet handler
					m_packet_handlers[ header.packet_id ]( this, m_packet_queue, header.packet_flags );
				}

				// Remove the packet from our queue
				m_packet_queue.erase( m_packet_queue.begin( ), m_packet_queue.begin( ) + header.packet_size );
			} else {
				// Packet is invalid/has no handler, erase it
				m_packet_queue.erase( m_packet_queue.begin( ), m_packet_queue.begin( ) + total_packet_size );
			}
		}
	}

	std::uint8_t async_client::generate_packet_identifier( ) {
		// TODO: Don't assign packet identifiers if next one would be 1

		if ( m_packet_identifiers.size( ) > 0 ) { 
			std::uint8_t last_identifier = m_packet_identifiers.back( );
			std::uint8_t next_identifier = ( ++last_identifier % 127 ) + 1; // 1-127
			m_packet_identifiers.push_back( next_identifier );
			
			return next_identifier;
		} else {
			m_packet_identifiers.push_back( 1 );
			return 1;
		}
	}

	void async_client::remove_packet_identifier( std::uint8_t identifier ) {
		m_packet_identifiers.erase( std::remove_if( m_packet_identifiers.begin( ), m_packet_identifiers.end( ), [ identifier ]( std::uint8_t id ) {
			return id == identifier;
		} ) );
	}
} // namespace forceinline::remote