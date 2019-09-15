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
	typedef void( *packet_handler_client_fn )( std::vector< char > data, async_client* client );

	class async_client {
	public:
		async_client( std::string_view ip, std::string_view port );
		~async_client( );

		void connect( );
		void disconnect( );
		
		bool is_connected( );

		void set_packet_handler( std::uint16_t packet_id, packet_handler_client_fn handler );

		void send_packet( base_packet* packet );
		void request_packet( std::uint16_t packet_id );

	private:
		void receive( );
		void process_packets( );

		bool m_connected = false;

		SOCKET m_socket = 0;
		WSADATA m_wsa_data = { };

		std::thread m_receive_thread, m_process_thread;

		std::string m_ip = "", m_port = "";

		std::mutex m_send_mtx, m_process_mtx;

		const std::uint16_t m_buffer_size = 4096;
		std::vector< char > m_packet_queue = { };

		std::unordered_map< int, packet_handler_client_fn > m_packet_handlers = { };
	};
}