#pragma once
#include <vector>

/*
	IMPORTANT NOTE:
	It is recommended to share this file between server and client. This means,
	you should link to this exact file in both of your projects for both the
	client and server to have the same packet information at all times.

	Example packet layout (x = 1 byte)
	[
		xx		type: uint16, specifies packet id
		xx		type: uint16, specifies packet data length
		xx...	type: uint8[], byte array with length of above mentioned length
	]
*/

namespace forceinline::remote {
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

		void set_buffer( std::vector< char >& buffer ) {
			m_bytes_read = 0;
			m_buffer = buffer;
		}

		char* data( ) {
			return m_buffer.data( );
		}

		std::uint16_t length( ) {
			return m_buffer.size( );
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
		// This method returns a pointer to the raw data of our packet
		virtual char* data( ) = 0;

		// This method returns the size of our packet
		virtual std::uint16_t size( ) = 0;

		// This method converts our buffer into usable data
		virtual void read( std::vector< char >& buffer ) = 0;

		// This method returns our packet ID
		virtual std::uint16_t id( ) = 0;
	};

	template < typename T, std::uint16_t pkt_id = -1 >
	class base_dynamic_packet : public base_packet {
	public:
		base_dynamic_packet( ) { }

		virtual std::uint16_t size( ) {
			return m_buffer.length( );
		}

		virtual std::uint16_t id( ) {
			return pkt_id;
		}

		// Used to access our packet data
		T& operator()( ) {
			return m_packet_data;
		}

		void set_packet_data( const T& packet_data ) {
			m_buffer.clear( );
			m_packet_data = packet_data;
		}

		virtual char* data( ) { return nullptr; }
		virtual void read( std::vector< char >& buffer ) { }

	protected:
		T m_packet_data = { };
		data_buffer m_buffer = { };
	};

	/*
		This is an enum which defines the packet IDs. These will be used later on for identification when the
		server receives a packet.

		Keep one thing in mind however; a packet ID shall never be 0 or lower as ID 0 is reserved for disconnect.
	*/

	enum packet_id : std::uint16_t {
		disconnect = 0, // Do NOT change this!
		simple,
		dynamic
	};

	/*
		Example packets:
		First one is a simple packet which can be implemented with the below given class.

		The second packet has items of varying size. It shows an example of
		how such a packet could be sent and specifically read.

		----------------------------------------------------------------------------------

		First we define structs that our packets will use. For ease of use, all structs
		and classes shall be prefixed with "packet_". If you don't like that, choose a
		different naming scheme or none at all, your choice.

		The first struct is a simple, static size packet. Its size will never change,
		which means we can just use the provided class to use it later on.

		The second struct features an std::vector. Its size varies, therefore we have
		to send additional information to our server/client so it knows how to read the
		data.

		----------------------------------------------------------------------------------

		Here we are implementing the basic packet.

		First we define a struct which does not contain any dynamic sized items.
	*/

	// Use this class when sending a packet which always has a static size. Example use will be shown below.
	template < typename T, const std::uint16_t pkt_id >
	class simple_packet : public base_packet {
	public:
		// Constructor for receiving
		simple_packet( std::vector< char >& packet_data ) {
			read( packet_data );
		}

		// Constructor for sending
		simple_packet( T packet_data ) : m_packet_data( packet_data ) { }

		virtual char* data( ) {
			return reinterpret_cast< char* >( &m_packet_data );
		}

		virtual std::uint16_t size( ) {
			return sizeof T;
		}

		virtual void read( std::vector< char >& buffer ) {
			if ( buffer.size( ) < this->size( ) )
				return;

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

	struct packet_simple_t {
		std::uint32_t some_number = 0;
		float some_float = 0.f;
		char some_array[ 3 ] = { };
	};

	/*
		Example usage:
		auto packet = remote::simple_packet< remote::packet_simple_t, remote::packet_id::simple >( my_struct );

		Now we will implement the dynamic packet. As an example I have chosen an std::vector because it is a
		container that I like to use.
	*/

	struct packet_dynamic_t {
		std::vector< std::uint8_t > some_container = { };
	};

	/*
		Here our class will be implemented. Because every dynamic packet will be different,
		we have to create a class for each one.

		If you have similar packets (for example a simple text stream), remember that if you
		use a typedef you have to send another form of packet ID to distinguish between them.

		Alternatively, you could make a new class which inherits from your text stream class,
		in which you simply override the virtual get_id method which will make it call a different
		packet handler.
	*/

	class dynamic_packet : public base_dynamic_packet< packet_dynamic_t, packet_id::dynamic > {
	public:
		dynamic_packet( packet_dynamic_t packet_data ) {
			m_packet_data = packet_data;
			this->data( ); // So the packet size gets set
		}

		dynamic_packet( std::vector< char >& packet_data ) { read( packet_data ); }

		// We only have to override the .data( ) and .read( ) methods
		virtual char* data( ) {
			// Write into our buffer if we haven't done it yet
			if ( !m_buffer.filled( ) ) {
				m_buffer.write_array< std::uint8_t >( m_packet_data.some_container );
				m_buffer.set_filled( true );
			}

			// Return the data
			return m_buffer.data( );
		}

		virtual void read( std::vector< char >& buffer ) {
			m_buffer.set_buffer( buffer );
			m_packet_data.some_container = m_buffer.read_array< std::uint8_t >( );
		}
	};
}