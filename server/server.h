#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <string>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <functional>

#include "../packet/packet_base.h"

#pragma comment (lib, "Ws2_32.lib")

namespace forceinline::remote {
	class async_server;
	typedef void( *packet_handler_server_fn )( async_server* server, SOCKET from, const std::vector< char >& data, std::uint8_t flags  );

	class async_server {
	public:
		async_server( std::string_view port );
		~async_server( );

		void start( );
		void close( );

		bool is_running( );

		void set_packet_handler( std::uint16_t packet_id, packet_handler_server_fn handler );

		void send_packet( SOCKET to, packets::packet_base::base_packet* packet );
		bool send_packet( SOCKET to, packets::packet_base::base_packet* packet, std::function< bool( SOCKET from, const std::vector< char >& buffer, const std::uint8_t flags ) > handler, std::chrono::milliseconds timeout = std::chrono::milliseconds( 250 ) );

	private:
		void send_packet_internal( SOCKET to, packets::packet_base::base_packet* packet, std::uint8_t packet_flags );

		void accept( );
		void receive( );
		void process_packets( );

		void close_client_connection( SOCKET client );

		std::uint8_t generate_packet_identifier( SOCKET to );
		void remove_packet_identifier( SOCKET to, std::uint8_t identifier );

		bool m_running = false;
		int m_bytes_received = 0;

		SOCKET m_server_socket = 0;

	#ifdef WIN32
		WSADATA m_wsa_data = { };
	#endif // WIN32

		std::thread m_accept_thread, m_receive_thread, m_process_thread;

		std::string m_port = "";

		std::mutex m_send_mtx, m_client_mtx, m_process_mtx, m_custom_mtx;

		const std::uint16_t m_buffer_size = 4096;

		std::vector< SOCKET > m_connected_clients = { };
		std::unordered_map< SOCKET, std::vector< char > > m_packet_queue = { };

		struct custom_process_info_t {
			custom_process_info_t( std::uint8_t identifier ) : packet_identifier( identifier ) { }

			std::uint8_t packet_identifier = 0;
			std::vector< char > packet_data = { };
		};

		std::unordered_map< SOCKET, std::vector< std::uint8_t > > m_packet_identifiers = { };
		std::unordered_map< SOCKET, std::vector< custom_process_info_t > > m_custom_process_queue = { };

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

		std::unordered_map< int, packet_handler_server_fn > m_packet_handlers = { };
	};
} // namespace forceinline::remote