#pragma once
#include <vector>
#include <string>
#include "packet_base.h"

/*
	IMPORTANT NOTE:
	It is recommended to share this file between server and client. This means,
	you should link to this exact file in both of your projects for both the
	client and server to have the same packet information at all times.
*/

namespace forceinline::remote::packets {
	/*
		This is an enum which defines the packet IDs. These will be used later on for identification when the
		server receives a packet.

		Keep one thing in mind however; a packet ID shall never be 0 or lower as ID 0 is reserved for disconnect.
	*/

	enum packet_id : std::uint16_t {
		disconnect = 0, // Do NOT change this! (TODO: implement disconnect)
		simple,
		text_one,
		text_two,
		random_numbers
	};

	/*
		Example packets:
		First one is a simple packet which can be implemented with the below given class.

		The second packet has items of varying size. It shows an example of
		how such a packet could be sent and specifically read.

		----------------------------------------------------------------------------------

		First we define structs that our packets will use. For ease of use, all structs
		shall be prefixed with "packet_".

		The first struct is a simple, static size packet. Its size will never change,
		which means we can just use the provided class to use it later on.

		The second struct features a string. Its length varies, therefore we have
		to send additional information to our server/client so it knows how to read the
		data.

		----------------------------------------------------------------------------------

		Here we are implementing the basic packet.

		First we define a struct which does not contain any dynamically sized items.
	*/

	struct packet_simple_t {
		std::uint32_t some_number = 0;
		float some_float = 0.f;
		char some_array[ 3 ] = { };
	};

	/*
		Example usage:
		namespace packets = forceinline::remote::packets;
		auto packet = packets::simple_packet< packets::packet_simple_t, packets::packet_id::simple >( my_struct );

		Now we will implement the dynamic packet. As an example I have chosen a string because you will
		have to send one sooner or later.
	*/

	/*
		Creating a struct for a string isn't really necessary, but this is done to show how you would
		work with packets normally. If you're using a single variable, you could just make it a member
		of the packet class.
	*/

	struct packet_text_t {
		std::string some_string = "";
	};

	struct packet_random_num_t {
		int numbers[ 3 ] = { };
	};

	/*
		Here our class will be implemented. Because every dynamic packet will be different,
		we have to create a class for each one.

		If you have similar packets (for example a simple text stream, like in this case), remember
		that you can still template the class like shown below.
	*/

	template < std::uint16_t pkt_id >
	class text_packet : public packet_base::base_dynamic_packet< packet_text_t, pkt_id > {
	public:
		text_packet( packet_text_t packet_data, std::uint8_t flags = 0 ) {
			this->m_flags = flags;
			this->m_packet_data = packet_data;
		}
		
		text_packet( const std::vector< char >& packet_data, std::uint8_t flags ) {
			// Always initialize the flags!
			this->m_flags = flags;
			read( packet_data ); 
		}

		// We only have to override the .read( ) and .fill_buffer( ) methods
		virtual void read( const std::vector< char >& buffer ) {
			// Always call the base function first!
			this->base_dynamic_packet::read( buffer );

			auto& data = this->m_packet_data;
			
			// Read the text as an array from the buffer (returns std::vector so we have to assign it to the string)
			auto text_data = this->m_buffer.read_array< char >( );

			// Assign the string the data
			data.some_string.assign( text_data.data( ), text_data.size( ) );
		}

	private:
		virtual void fill_buffer( ) {
			// Only fill our buffer once
			if ( this->m_buffer.filled( ) )
				return;

			auto& data = this->m_packet_data;

			// Write the string into our buffer
			auto text_data = std::vector< char >( data.some_string.begin( ), data.some_string.end( ) );
			this->m_buffer.write_array< char >( text_data );
		}
	};

	/* 
		Later on you'd use it like so:

		auto packet = packets::text_packet< packets::packet_id::text >( { "Hello from packet!" } );
	*/
}