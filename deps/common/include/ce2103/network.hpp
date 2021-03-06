#ifndef CE2103_NETWORK_HPP
#define CE2103_NETWORK_HPP

#include <string>
#include <variant>
#include <utility>
#include <cstdarg>
#include <optional>
#include <type_traits>
#include <string_view>

#include <sys/socket.h>
#include <netinet/in.h>

#include "ce2103/hash_map.hpp"

namespace ce2103
{
	//! IPv4/IPv6 endpoint, consisting of an IP address and a port number.
	class ip_endpoint
	{
		public:
			/*!
			 * \brief Constructs an endpoint out of an address string.
			 *
			 * \param address endpoint address string
			 *
			 * \return newly created endpoint, if the input is parseable
			 */
			static std::optional<ip_endpoint> try_from
			(
				std::string_view address
			) noexcept;

			/*!
			 * \brief Gets the sockaddr structure, situable for the Berkeley sockets API.
			 *
			 * \return associated sockaddr structure
			 */
			inline const auto& as_sockaddr() const noexcept
			{
				return reinterpret_cast<const struct ::sockaddr&>(this->sockaddr);
			}

			/*!
			 * \brief Performs 'sizeof' over the particular, IP-version dependent
			 *        sockaddr structure.
			 *
			 * \return size in bytes of the sockaddr structure
			 */
			inline ::socklen_t get_sockaddr_size() const noexcept
			{
				return this->_is_ipv4 ? sizeof this->sockaddr.as_ipv4
				                      : sizeof this->sockaddr.as_ipv6;
			}

			/*!
			 * \brief Indicates if this is an IPv4 endpoint.
			 *
			 * \return whether this is an IPv4 endpoint.
			 */
			inline bool is_ipv4() const noexcept
			{
				return this->_is_ipv4;
			}

			/*!
			 * \brief Indicates if this is an IPv6 endpoint.
			 *
			 * \return whether this is an IPv6 endpoint.
			 */
			inline bool is_ipv6() const noexcept
			{
				return !this->_is_ipv4;
			}

		private:
			//! Discriminator for the sockaddr union.
			bool _is_ipv4;

			//! Union between both posible sockaddr variants.
			union
			{
				struct ::sockaddr_in  as_ipv4; 
				struct ::sockaddr_in6 as_ipv6;
			} sockaddr;

			//! Constructs an IPv4 endpoint out of a sockaddr.
			inline ip_endpoint(const struct ::sockaddr_in& as_ipv4) noexcept
			: _is_ipv4{true}
			{
				this->sockaddr.as_ipv4 = as_ipv4;
			}

			//! Constructs an IPv6 endpoint out of a sockaddr.
			inline ip_endpoint(const struct ::sockaddr_in6& as_ipv6) noexcept
			: _is_ipv4{false}
			{
				this->sockaddr.as_ipv6 = as_ipv6;
			}
	};

	//! Represents a network connection or connection acceptor.
	class socket final
	{
		public:
			//! Constructs an unspecified socket;
			socket() noexcept = default;

			socket(const socket& other) = delete;

			//! Moves a socket.
			socket(socket&& other) noexcept;

			//! Destroys a socket, closing it if necessary.
			inline ~socket() noexcept
			{
				this->close();
			}

			socket& operator=(const socket& other) = delete;

			//! Replaces this socket object by move.
			socket& operator=(socket&& other) noexcept;

			//! Closes the socket, interrupting any existing connection.
			void close() noexcept;

			/*!
			 * \brief Binds this socket to a particular address. Corner cases
			 *        are OS-dependent.
			 *
			 * \param endpoint the address to bind to
			 * \param passive  whether to bind in passive or active mode
			 *
			 * \return whether the socket was successfully bound
			 */
			bool bind(const ip_endpoint& endpoint, bool passive) noexcept;

			/*!
			 * \brief Attempts to connect the socket to a given endpoint.
			 *
			 * \param endpoint the remote endpoint to connect to
			 *
			 * \return whether the connection was successful
			 */
			bool connect(const ip_endpoint& endpoint) noexcept;

			/*!
			 * \brief Attempts to accept a new connection from a passive socket.
			 *        This blocks until such a connection or some error occurs.
			 *
			 * \return accepted client socket, if no error occurs
			 */
			std::optional<socket> accept() noexcept;

			/*!
			 * \brief Attempts to read a line of text from the socket. Blocks
			 *        until a line is read, EOF is found or an error occurs.
			 *
			 * \param line output where to store the text line, cleared on each call
			 *
			 * \return whether a line was successfully read
			 */
			bool read_line(std::string& line);

			/*!
			 * \brief Writes a string to the socket.
			 *
			 * \param output string to write
			 */
			void write(std::string_view output);

			/*!
			 * \brief Writes formatted output to the socket, vprintf-style.
			 *        Additional arguments are required as per the format string.
			 *
			 * \param format    output format string
			 * \param arguments variable argument list
			 */
			void write_variadic(const char* format, va_list arguments);

			/*!
			 * \brief Writes formatted output to the socket, printf-style.
			 *        Additional arguments are required as per the format string.
			 *
			 * \param format output format string
			 */
			void write_formatted(const char* format, ...);

			//! Retrieves the socket's file descriptor, or a negative integer if inactive
			inline int get_descriptor() const noexcept
			{
				return this->descriptor;
			}

		private:
			static constexpr std::size_t BUFFER_SIZE = 0x100;

			/*!
			 * \brief OS file descriptor which represents the socket, or a negative
			 *        integer if there is no file descriptor currently associated.
			 */
			int descriptor = -1;

			char*       buffer       = nullptr; //!< Buffer for reading
			char*       buffer_base  = nullptr; //!< Current buffer base
			std::size_t buffer_usage = 0;       //!< Buffer usage in bytes

			/*!
			 * \brief Constructs a socket given its file descriptor.
			 *
			 * \param descriptor file descriptor
			 */
			inline socket(int descriptor) noexcept
			: descriptor{descriptor}
			{
				this->expect_buffer();
			}

			/*!
			 * \brief If the socket object is not associated with a file descriptor,
			 *        creates a new one for the given address family. Otherwise is a no-op.
			 *
			 * \param is_ipv4 whether the requested family is IPv4 or IPv6
			 *
			 * \return whether there was already an associated file descriptor,
			 *         and otherwise whether socket creation succeeded
			 */
			bool expect_descriptor(bool is_ipv4) noexcept;

			//! Allocates the per-socket buffer, if there is none.
			void expect_buffer();
	};

	template<typename AcceptorType>
	class reactor;
}

namespace ce2103::_detail
{
	//! epoll(7)-based reactor
	class reactor_base
	{
		public:
			//! Move-constructs a reactor
			inline reactor_base(reactor_base&& other) noexcept
			: listen_socket{std::move(other.listen_socket)},
			  epoll_descriptor{other.epoll_descriptor}
			{
				other.epoll_descriptor = -1;
			}

			//! Destroys the reactor
			~reactor_base();

			//! Moves a reactor into another one
			reactor_base& operator=(reactor_base&& other) noexcept;

		protected:
			//! Constructs a reactor given its listening socket
			reactor_base(socket listen_socket);

			/*!
			 * \brief Waits for an event.
			 *
			 * \return Either a descriptor with ready input data or the
			 *         the socket of a newly accepted connection.
			 */
			std::variant<int, socket> wait();

			//! Registers tee descriptor into the epoll set
			void watch(int descriptor);

			//! Removes the descriptor from the epoll set
			void forget(int descriptor);

		private:
			socket listen_socket;    //!< Passive socket for new sessions
			int    epoll_descriptor; //!< Handle for epoll syscalls
	};
}

namespace ce2103
{
	//! A network input traffic multiplexer, for concurrent server sessions.
	template<typename AcceptorType>
	class reactor : private _detail::reactor_base
	{
		private:
			//! Deducted from the acceptor type
			using session_type = std::invoke_result_t<AcceptorType, socket>;

		public:
			//! Constructs a reactor from the given acceptor and listen socket
			inline reactor(socket listen_socket, AcceptorType acceptor)
			: _detail::reactor_base{std::move(listen_socket)}, acceptor{std::move(acceptor)}
			{}

			//! Enters the main loop
			void run();

		private:
			AcceptorType acceptor; //!< Session generator

			//! Map of fds to active sessions
			hash_map<int, session_type> sessions;
	};

	template<typename AcceptorType>
	void reactor<AcceptorType>::run()
	{
		while(true)
		{
			auto result = this->wait();
			if(auto* descriptor = std::get_if<int>(&result); descriptor != nullptr)
			{
				if(auto* session = this->sessions.search(*descriptor);
				   session != nullptr && !session->on_input())
				{
					this->forget(*descriptor);
					this->sessions.remove(*descriptor);
				}
			} else if(auto* new_socket = std::get_if<socket>(&result); new_socket != nullptr)
			{
				int descriptor = new_socket->get_descriptor();

				this->sessions.insert(descriptor, this->acceptor(std::move(*new_socket)));
				this->watch(descriptor);
			}
		}
	}
}

#endif
