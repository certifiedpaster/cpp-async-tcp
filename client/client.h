#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>

#include <string>
#include <thread>
#include <array>
#include <functional>
#include <unordered_map>
#include <mutex>

#include "../packet/packet.h"

#pragma comment (lib, "Ws2_32.lib")

namespace forceinline::remote {
	class async_client;
	typedef void( *packet_handler_client_fn )( async_client* client, const std::vector< char >& data, const std::uint8_t flags );

	class async_client {
	public:
		async_client( std::string_view ip, std::string_view port );
		~async_client( );

		void connect( );
		void disconnect( );
		
		bool is_connected( );

		void set_packet_handler( std::uint16_t packet_id, packet_handler_client_fn handler );

		void send_packet( packets::packet_base::base_packet* packet );
		bool send_packet( packets::packet_base::base_packet* packet, std::function< bool( const std::vector< char >& buffer, const std::uint8_t flags ) > handler, std::chrono::milliseconds timeout = std::chrono::milliseconds( 250 ) );

	private:
		void send_packet_internal( packets::packet_base::base_packet* packet, std::uint8_t packet_flags );

		void receive( );
		void process_packets( );

		std::uint8_t generate_packet_identifier( );
		void remove_packet_identifier( std::uint8_t identifier );

		bool m_connected = false;

		SOCKET m_socket = 0;
	
	#ifdef WIN32
		WSADATA m_wsa_data = { };
	#endif // WIN32

		std::thread m_receive_thread, m_process_thread;

		std::string m_ip = "", m_port = "";

		std::mutex m_send_mtx, m_process_mtx, m_custom_mtx;

		const std::uint16_t m_buffer_size = 4096;
		std::vector< char > m_packet_queue = { };
		
		struct custom_process_info_t {
			custom_process_info_t( std::uint8_t identifier ) : packet_identifier( identifier ) { }

			std::uint8_t packet_identifier = 0;
			std::vector< char > packet_data = { };
		};

		struct packet_header_t {
			packet_header_t( packets::packet_base::base_packet* packet ) {
				if ( !packet )
					return;

				packet_id = packet->id( );
				packet_size = packet->size( );
				packet_flags = packet->flags( );
			}

			std::uint16_t packet_id = 0;
			std::uint16_t packet_size = 0;
			std::uint8_t packet_flags = 0;
		};

		std::vector< std::uint8_t > m_packet_identifiers = { };
		std::vector< custom_process_info_t > m_custom_process_queue = { };

		std::unordered_map< int, packet_handler_client_fn > m_packet_handlers = { };
	};
} // namespace forceinline::remote