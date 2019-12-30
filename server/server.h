#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <string>
#include <thread>
#include <mutex>
#include <unordered_map>

#include "../packet/packet.h"

#pragma comment (lib, "Ws2_32.lib")

namespace forceinline::remote {
	class async_server;
	typedef void( *packet_handler_server_fn )( SOCKET from, std::vector< char > data, async_server* server );

	class async_server {
	public:
		async_server( std::string_view port );
		~async_server( );

		void start( );
		void close( );

		bool is_running( );

		void set_packet_handler( std::uint16_t packet_id, packet_handler_server_fn handler );

		void send_packet( SOCKET to, base_packet* packet );
		void request_packet( SOCKET to, std::uint16_t packet_id );

	private:
		void accept( );
		void receive( );
		void process_packets( );

		void close_client_connection( SOCKET client );

		bool m_running = false;
		int m_bytes_received = 0;

		SOCKET m_server_socket = 0;
		WSADATA m_wsa_data = { };

		std::thread m_accept_thread, m_receive_thread, m_process_thread;

		std::string m_port = "";

		std::mutex m_send_mtx, m_client_mtx, m_process_mtx;

		const std::uint16_t m_buffer_size = 4096;

		std::vector< SOCKET > m_connected_clients = { };
		std::unordered_map< SOCKET, std::vector< char > > m_packet_queue = { };

		std::unordered_map< int, packet_handler_server_fn > m_packet_handlers = { };
	};
}