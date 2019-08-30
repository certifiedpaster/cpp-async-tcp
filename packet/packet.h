#pragma once
#include <vector>

/*	
	Example packet layout (x = 1 byte)
	[	
		xx		type: uint16, specifies packet id
		xx		type: uint16, specifies packet data length
		xx...	type: uint8[], byte array with length of above mentioned byte
	]
*/

#define default_packet_constructors( packet_name, packet_struct, packet_id ) \
packet_name( std::vector< char >& _data ) {		 							 \
	m_packet_id = packet_id;			 					                 \
	read( _data );		 									  				 \
}															  				 \
															  				 \
packet_name( const packet_struct& _struct ) {				  				 \
	m_packet_id = packet_id;								  				 \
	m_data = _struct;										  				 \
}

#define create_packet_basic( packet_name, packet_struct, packet_id )	  \
class packet_name : public base_packet {				 				  \
public:																 	  \
	default_packet_constructors( packet_name, packet_struct, packet_id ); \
																		  \
	virtual char* data( ) {						  						  \
		return m_raw_data;												  \
	}																 	  \
																	 	  \
	virtual std::uint16_t size( ) {									 	  \
		return sizeof packet_struct;									  \
	}																 	  \
																	 	  \
	virtual void read( std::vector< char >& buffer ) {		 			  \
		if ( !buffer.data( ) || buffer.empty( ) )					 	  \
			return;													 	  \
																		  \
		memcpy( m_raw_data, buffer.data( ), sizeof m_data );		 	  \
	}																 	  \
																	 	  \
	union {															 	  \
		packet_struct	m_data;										 	  \
		char			m_raw_data[ sizeof packet_struct ];				  \
	};																	  \
};																		

namespace forceinline::socket {
	class base_packet {
	public:
		//This method returns a pointer to the raw data of our packet
		virtual char* data( ) = 0;

		//This method returns the size of our packet
		virtual std::uint16_t size( ) = 0;
		
		//This method converts our buffer into usable data
		virtual void read( std::vector< char >& buffer ) = 0;

		//This method returns our packet ID
		std::uint16_t id( ) {
			return m_packet_id;
		}

	protected:
		std::uint16_t m_packet_id = 0;
	};

	/*
		This is an enum which defines the IDs of our packets. You don't have to use
		one, however it is easier to utilize the IDs when setting packet handlers
		later on.

		Keep one thing in mind however; a packet ID shall never be 0 or lower.
	*/

	enum packet_id : std::uint16_t {
		simple = 1,
		dynamic
	};

	/*
		Example packets:
		First one is a simple packet which can be implemented with the given macro.

		The second packet has items of varying size. It shows an example of
		how such a packet could be send and specifically read.

		----------------------------------------------------------------------------------

		First we define structs that our packets will use. For ease of use, all structs
		and classes shall be prefixed with "packet_". If you don't like that, choose a 
		different naming scheme or none at all, your choice.

		The first struct is a simple, static size packet. Its size will never change,
		which means we can just use the provided macro to implement it later on.

		The second struct features an std::vector. Its size varies, therefore we have
		to send additional information to our server/client so it knows how to read the
		data.

		----------------------------------------------------------------------------------

		Here we are implementing the basic packet.
		
		First we define a struct which does not contain any dynamic sized items.
	*/

	struct packet_simple_t {
		std::uint32_t some_number = 0;
		float some_float = 0.f;
		char some_array[ 3 ] = { };
	};

	/*
		Because we have a basic packet with a static size, we can use the above provided
		macro to implement the class that will handle sending and receiving it.
	*/

	create_packet_basic( packet_simple, packet_simple_t, packet_id::simple );

	/*
		Now we will implement the dynamic packet. As an example I have chosen an
		std::vector because it is a container that I like to use.

		I decided to prefix the container data with the container size, so our receiving
		end knows how much data to copy from the buffer into its container.
	*/

	struct packet_dynamic_t {
		std::uint32_t container_length = 0;
		std::vector< std::uint8_t > some_container = { };
	};

	/*
		Here our class will be implemented. Because every dynamic packet will be different,
		we have to 
	*/

	class packet_dynamic : public base_packet {
	public:
		default_packet_constructors( packet_dynamic, packet_dynamic_t, packet_id::dynamic );

		~packet_dynamic( ) {
			if ( m_raw_data )
				delete[ ] m_raw_data;
		}

		virtual char* data( ) {
			//Create a buffer, remember to deallocate it in our deconstructor
			if ( !m_raw_data )
				m_raw_data = new char[ size( ) ];

			m_data.container_length = m_data.some_container.size( );

			//Copy the size of the container and its data into the buffer
			memcpy( m_raw_data, &m_data.container_length, sizeof std::uint32_t );
			memcpy( m_raw_data + sizeof std::uint32_t, m_data.some_container.data( ), m_data.some_container.size( ) * sizeof std::uint8_t );

			return m_raw_data;
		}

		virtual std::uint16_t size( ) {
			//Calculate size just once as our data will never change
			if ( !m_packet_size ) {
				//Add all items together
				m_packet_size += sizeof std::uint32_t;
				m_packet_size += m_data.some_container.size( ) * sizeof std::uint8_t;
			}
			
			//Return packet size
			return m_packet_size;
		}

		virtual void read( std::vector< char >& buffer ) {
			//See how many elements our container has
			m_data.container_length = *reinterpret_cast< std::uint32_t* >( buffer.data( ) );
			
			//Resize our container accordingly
			m_data.some_container.resize( m_data.container_length, 0 );
			
			//Copy the container data into our container
			memcpy( m_data.some_container.data( ), buffer.data( ) + sizeof std::uint32_t, m_data.container_length * sizeof std::uint8_t );
		}

		packet_dynamic_t get_packet( ) {
			//Return a copy of our packet
			return m_data;
		}

	private:
		packet_dynamic_t m_data = { };
		char* m_raw_data = nullptr;

		std::uint16_t m_packet_size = 0;
	};
}