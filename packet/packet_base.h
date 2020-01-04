#pragma once

/*
	Example packet layout( x = 1 byte )
	[
		xx		type : uint16, specifies packet id
		xx		type : uint16, specifies packet data length
		x		type : uint8, specifies packet flags
		xx...	type : uint8[ ], byte array with length of above mentioned length
	]
*/

namespace forceinline::remote::packets {
	namespace packet_base {
		// A dynamic data buffer.
		class data_buffer {
		public:
			template < typename T >
			void write( T data ) {
				m_buffer.insert( m_buffer.end( ), reinterpret_cast< char* >( &data ), reinterpret_cast< char* >( &data ) + sizeof T );
			}

			template < typename T >
			void write_array( const std::vector< T >& data_array ) {
				write< std::size_t >( data_array.size( ) );
				m_buffer.insert( m_buffer.end( ), data_array.data( ), data_array.data( ) + data_array.size( ) * sizeof T );
			}

			template < typename T >
			T read( ) {
				auto data = *reinterpret_cast< T* >( m_buffer.data( ) + m_bytes_read );
				m_bytes_read += sizeof( T );
				return data;
			}

			template < typename T >
			std::vector< T > read_array( ) {
				auto length = read< std::size_t >( );

				std::vector< T > data_array( length );
				memcpy( data_array.data( ), m_buffer.data( ) + m_bytes_read, length * sizeof T );
				m_bytes_read += length * sizeof T;

				return data_array;
			}

			void clear( ) {
				m_buffer.clear( );
				m_bytes_read = 0;
				m_filled = false;
			}

			void set( const std::vector< char >& buffer ) {
				m_bytes_read = 0;
				m_buffer = buffer;
			}

			char* data( ) {
				return m_buffer.data( );
			}

			std::uint16_t length( ) {
				return m_buffer.size( ) & 0xFFFF;
			}

			bool filled( ) {
				return m_filled;
			}

			void set_filled( bool filled ) {
				m_filled = filled;
			}

		private:
			bool m_filled = false;
			std::size_t m_bytes_read = 0;
			std::vector< char > m_buffer = { };
		};

		class base_packet {
		public:
			base_packet( ) { }
			base_packet( std::uint8_t flags ) : m_flags( flags ) { }

			// This method returns a pointer to the raw data of our packet
			virtual char* data( ) = 0;

			// This method returns the size of our packet
			virtual std::uint16_t size( ) = 0;

			// This method converts our buffer into usable data
			virtual void read( const std::vector< char >& buffer ) = 0;

			// This method returns our packet ID
			virtual std::uint16_t id( ) = 0;

			// This method returns the packet flags
			std::uint8_t flags( ) {
				return m_flags;
			}

		protected:
			std::uint8_t m_flags = 0;
		};

		// Use this class as a template for dynamic packets
		template < typename T, std::uint16_t pkt_id >
		class base_dynamic_packet : public base_packet {
		public:
			base_dynamic_packet( ) { }
			base_dynamic_packet( std::uint8_t flags ) : base_packet( flags ) { }

			virtual std::uint16_t size( ) {
				// Clear buffer in case of old data
				m_buffer.clear( );

				// Fill it with the new data
				fill_buffer( );

				return m_buffer.length( );
			}

			virtual std::uint16_t id( ) {
				return pkt_id;
			}

			// Used to access our packet data
			T& operator()( ) {
				return m_packet_data;
			}

			virtual char* data( ) {
				m_buffer.clear( );

				fill_buffer( );

				return m_buffer.data( ); 
			}

			virtual void read( const std::vector< char >& buffer ) {
				m_buffer.set( buffer );
			}

			// This function will (re-)fill the buffer if necessary
			virtual void fill_buffer( ) = 0;

		protected:
			T m_packet_data = { };
			data_buffer m_buffer = { };
		};
	} // namespace packet_base

	// Use this class when sending a packet which always has a static size. Example use is shown in packet.h
	template < typename T, const std::uint16_t pkt_id >
	class simple_packet : public packet_base::base_packet {
	public:
		// Constructor for receiving
		simple_packet( const std::vector< char >& packet_data, std::uint8_t flags ) : base_packet( flags ) {
			read( packet_data );
		}

		// Constructor for sending
		simple_packet( T packet_data, std::uint8_t flags = 0 ) {
			this->m_flags = flags;
			memcpy( &m_packet_data, &packet_data, sizeof T );
		}

		virtual char* data( ) {
			return reinterpret_cast< char* >( &m_packet_data );
		}

		virtual std::uint16_t size( ) {
			return sizeof T;
		}

		virtual void read( const std::vector< char >& buffer ) {
			if ( buffer.size( ) < this->size( ) )
				return;

			// Copy the data
			memcpy( &m_packet_data, buffer.data( ), this->size( ) );
		}

		virtual std::uint16_t id( ) {
			return pkt_id;
		}

		// Used to access our packet data
		T& operator()( ) {
			return m_packet_data;
		}

	private:
		T m_packet_data = { };
	};
} // namespace forceinline::remote::packets