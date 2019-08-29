#include "client.h"
#include <cassert>

namespace forceinline::socket {
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

		if ( WSAStartup( MAKEWORD( 2, 2 ), &m_wsa_data ) != 0 )
			throw std::exception( "async_client::connect: WSAStartup call failed" );

		struct addrinfo hints, * result;
		ZeroMemory( &hints, sizeof( hints ) );

		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		if ( getaddrinfo( m_ip.data( ), m_port.data( ), &hints, &result ) != 0 )
			throw std::exception( "async_client::connect: getaddrinfo call failed" );

		m_socket = ::socket( result->ai_family, result->ai_socktype, result->ai_protocol );

		if ( m_socket == INVALID_SOCKET )
			throw std::exception( "async_client::connect: socket creation failed" );

		if ( ::connect( m_socket, result->ai_addr, int( result->ai_addrlen ) ) == SOCKET_ERROR )
			throw std::exception( "async_client::connect: failed to connect to host" );

		freeaddrinfo( result );

		m_connected = true;
		m_receive_thread = std::thread( &async_client::receive, this );
		m_process_thread = std::thread( &async_client::process_packets, this );
	}

	void async_client::disconnect( ) {
		m_connected = false;

		//Wait for our threads to finish
		if ( m_receive_thread.joinable( ) )
			m_receive_thread.join( );

		if ( m_process_thread.joinable( ) )
			m_process_thread.join( );

		//Tell the server we disconnected
		if ( m_socket ) {
			shutdown( m_socket, SD_SEND );
			closesocket( m_socket );
			m_socket = NULL;
		}

		WSACleanup( );
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

	void async_client::send_packet( base_packet* packet ) {
		//Return if our packet is invalid
		if ( !packet )
			return;

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

		//Lock the send function for other threads until we're done
		std::unique_lock lock( m_send_mtx );

		//Try to send our buffer
		std::size_t bytes_sent = 0, total_bytes_sent = 0;
		do {
			bytes_sent = send( m_socket, packet_buffer.data( ) + total_bytes_sent, packet_buffer.size( ) - total_bytes_sent, NULL );
			total_bytes_sent += bytes_sent;
		} while ( bytes_sent > 0 && total_bytes_sent < packet_buffer.size( ) );

		//An error occurred, disconnect from server
		if ( bytes_sent <= 0 )
			m_connected = false;
	}

	void async_client::receive( ) {
		//Create a temporary buffer
		std::vector< char > temporary_buffer = std::vector< char >( m_buffer_size );

		//Loop and receive
		do {
			//Check how many bytes we have received
			int bytes_received = recv( m_socket, temporary_buffer.data( ), m_buffer_size, NULL );

			//An error occurred, break out and disconnect
			if ( bytes_received <= 0 )
				break;
			
			//Lock the process mutex
			std::unique_lock lock( m_process_mtx );

			//Copy the received bytes into our queue
			m_packet_queue.insert( m_packet_queue.end( ), temporary_buffer.data( ), temporary_buffer.data( ) + bytes_received );

		} while ( m_connected );

		//Clear the temporary buffer and disconnect
		temporary_buffer.clear( );
		m_connected = false;
	}

	void async_client::process_packets( ) {
		while ( m_connected ) {
			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
			std::unique_lock lock( m_process_mtx );

			//Check if we have at least a packet header stored
			if ( m_packet_queue.size( ) < 2 * sizeof std::uint16_t )
				continue;

			//We have something to process
			do {
				//Get the information about our packet
				std::uint32_t packet_information = *reinterpret_cast< std::uint32_t* >( m_packet_queue.data( ) );

				//Packet ID is in the first 2 bytes while its size is in the following 2
				std::uint16_t packet_id = packet_information & 0xFFFF;
				std::uint16_t packet_size = ( packet_information >> 16 );

				//Add the first 4 bytes to our packet size
				std::uint16_t total_packet_size = packet_size + 2 * sizeof std::uint16_t;

				//Do we have a whole packet stored?
				if ( m_packet_queue.size( ) < total_packet_size )
					break;

				//Does our packet have a handler?
				if ( m_packet_handlers.find( packet_id ) != m_packet_handlers.end( ) ) {
					//Erase the packet header
					m_packet_queue.erase( m_packet_queue.begin( ), m_packet_queue.begin( ) + 2 * sizeof std::uint16_t );

					//Call the handler
					m_packet_handlers[ packet_id ]( m_packet_queue, this );

					//Remove the packet from our queue
					m_packet_queue.erase( m_packet_queue.begin( ), m_packet_queue.begin( ) + packet_size );
				} else {
					//Packet is invalid/has no handler, erase it
					m_packet_queue.erase( m_packet_queue.begin( ), m_packet_queue.begin( ) + total_packet_size );
				}
			} while ( m_packet_queue.size( ) > 2 * sizeof std::uint16_t );
		}
	}
}