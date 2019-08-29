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
		if ( m_receive_thread.joinable( ) )
			m_receive_thread.join( );

		m_buffer.clear( );
		
		disconnect( );
	}

	void async_client::connect( ) {
		if ( m_connected )
			throw std::exception( "async_client::connect: already connected" );

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
	}

	void async_client::disconnect( ) {
		if ( !m_connected || !m_socket )
			return;
		
		shutdown( m_socket, SD_SEND );
		closesocket( m_socket );
		
		m_connected = false;
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

		//Lock the mutex
		m_mtx.lock( );

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
			bytes_sent = send( m_socket, packet_buffer.data( ) + total_bytes_sent, packet_buffer.size( ) - total_bytes_sent, NULL );
			total_bytes_sent += bytes_sent;
		} while ( bytes_sent > 0 && total_bytes_sent < packet_buffer.size( ) );

		//An error occurred, disconnect from server
		if ( bytes_sent <= 0 )
			disconnect( );

		//Unlock our mutex so the next packet can be sent
		m_mtx.unlock( );
	}

	//fixme: receive ALL packets, not just the expected ones
	void async_client::receive( ) {
		assert( m_connected );

		auto receive = [ & ]( std::uint16_t size, char* buffer ) {		
			int received = 0, total_received = 0;

			//Try to receive as much as we requested
			do {
				received = recv( m_socket, buffer + total_received, size - total_received, NULL );
				total_received += received;
			} while ( received > 0 && total_received < size );

			//Did we receive what we requested?
			return total_received == size;
		};

		do {	
			//Clear and prepare the buffer
			m_buffer.clear( );
			m_buffer.resize( m_packet_size );

			//Check how many bytes we have received
			m_bytes_received = recv( m_socket, m_buffer.data( ), m_buffer.size( ), NULL );

			//We haven't received enough information about our packet, try to get more
			if ( m_bytes_received < 4 ) {
				//We failed to receive enough bytes, disconnect as an error has occurred
				if ( !receive( 4 - m_bytes_received, m_buffer.data( ) + m_bytes_received ) )
					break;
			} 

			//Get the information about our packet
			std::uint32_t packet_information = *reinterpret_cast< std::uint32_t* >( m_buffer.data( ) );

			//Packet ID is in the first 2 bytes while its size is in the following 2
			std::uint16_t packet_id = packet_information & 0xFFFF;
			std::uint16_t packet_size = ( packet_information >> 16 ) & 0xFFFF;

			//Add the first 4 bytes to our packet size
			std::uint16_t total_packet_size = packet_size + 2 * sizeof std::uint16_t;

			//The packet is larger than our buffer for it
			if ( total_packet_size > m_buffer.size( ) ) {
				//Resize the buffer accordingly
				m_buffer.insert( m_buffer.end( ), total_packet_size - m_buffer.size( ), 0 );
			}

			//We haven't received the whole packet, keep listening
			if ( m_bytes_received < packet_size ) {
				//Failed to receive whole packet, error -> disconnect
				if ( !receive( packet_size - m_bytes_received, m_buffer.data( ) + m_bytes_received ) )
					break;
			}		

			//Does a handler exist for the packet?
			if ( m_packet_handlers.find( packet_id ) != m_packet_handlers.end( ) ) {
				//Remove the first four bytes from our buffer as we only pass the packet data to our handler
				m_buffer.erase( m_buffer.begin( ), m_buffer.begin( ) + 4 );

				//Copy the buffer and call the packet handler
				m_packet_handlers[ packet_id ]( m_buffer, this );
			}

		} while ( m_connected && m_bytes_received > 0 );

		//An error occurred or the connection was closed by the server, disconnect
		disconnect( );
	}
}