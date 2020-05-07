#include <string>
#include <cerrno>
#include <cstdio>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <utility>
#include <string_view>
#include <system_error>

#include <netdb.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "ce2103/network.hpp"

namespace
{
	[[noreturn]]
	void throw_errno()
	{
		throw std::system_error{errno, std::system_category()};
	}
}

namespace ce2103
{
	std::optional<ip_endpoint> ip_endpoint::try_from(std::string_view address) noexcept
	{
		auto delimiter = address.rfind(':');
		if(delimiter == std::string_view::npos)
		{
			return std::nullopt;
		}

		// IPv4 1.2.3.4 vs IPv6 [::1]
		bool is_ipv4 = delimiter == 0 || address[0] != '[';

		struct ::addrinfo addrinfo = {};
		addrinfo.ai_socktype = SOCK_STREAM;
		addrinfo.ai_flags = AI_NUMERICHOST;
		addrinfo.ai_family = is_ipv4 ? AF_INET : AF_INET6;

		const char* service = &address[delimiter + 1];
		std::string host{is_ipv4 ? address.substr(0, delimiter) : address.substr(1, delimiter - 2)};

		struct ::addrinfo* result;
		if(::getaddrinfo(host.data(), service, &addrinfo, &result) != 0)
		{
			return std::nullopt;
		}

		auto endpoint = is_ipv4
		              ? ip_endpoint{*reinterpret_cast<struct ::sockaddr_in*>(result->ai_addr)}
		              : ip_endpoint{*reinterpret_cast<struct ::sockaddr_in6*>(result->ai_addr)};

		::freeaddrinfo(result);
		return endpoint;
	}

	socket::socket(socket&& other) noexcept
	: descriptor{other.descriptor}, buffer{other.buffer},
	  buffer_base{other.buffer_base}, buffer_usage{other.buffer_usage}
	{
		other.descriptor = -1;
		other.buffer = other.buffer_base = nullptr;
		other.buffer_usage = 0;
	}

	socket& socket::operator=(socket&& other) noexcept
	{
		this->close();

		this->descriptor = other.descriptor;
		this->buffer = other.buffer;
		this->buffer_base = other.buffer_base;
		this->buffer_usage = other.buffer_usage;

		other.descriptor = -1;
		other.buffer = other.buffer_base = nullptr;
		other.buffer_usage = 0;

		return *this;
	}

	void socket::close() noexcept
	{
		if(this->descriptor >= 0)
		{
			::close(this->descriptor);
			if(this->buffer != nullptr)
			{
				delete[] this->buffer;

				this->buffer_base = this->buffer = nullptr;
				this->buffer_usage = 0;
			}

			this->descriptor = -1;
		}
	}

	bool socket::bind(const ip_endpoint& endpoint, bool passive) noexcept
	{
		bool succeeded = this->expect_descriptor(endpoint.is_ipv4())
		              && !::bind(this->descriptor, &endpoint.as_sockaddr(),
		                         endpoint.get_sockaddr_size());

		if(succeeded)
		{
			if(passive)
			{
				succeeded = !::listen(this->descriptor, 42);
			} else
			{
				this->expect_buffer();
			}
		}

		return succeeded;
	}

	bool socket::connect(const ip_endpoint& endpoint) noexcept
	{
		bool result = this->expect_descriptor(endpoint.is_ipv4())
		           && !::connect(this->descriptor, &endpoint.as_sockaddr(),
		                         endpoint.get_sockaddr_size());

		if(result)
		{
			this->expect_buffer();
		}

		return result;
	}

	std::optional<socket> socket::accept() noexcept
	{
		int client_descriptor = ::accept(this->descriptor, nullptr, nullptr);
		if(client_descriptor < 0)
		{
			return std::nullopt;
		}

		return socket{client_descriptor};
	}

	bool socket::expect_descriptor(bool is_ipv4) noexcept
	{
		if(this->descriptor < 0)
		{
			this->descriptor = ::socket(is_ipv4 ? AF_INET : AF_INET6, SOCK_STREAM, 0);
		}

		return this->descriptor >= 0;
	}

	void socket::expect_buffer()
	{
		if(this->buffer == nullptr)
		{
			this->buffer_base = this->buffer = new char[BUFFER_SIZE];
			this->buffer_usage = 0;
		}
	}

	bool socket::read_line(std::string& line)
	{
		if(this->buffer == nullptr)
		{
			return false;
		}

		line.erase();

		bool found = false;
		while(true)
		{
			char* terminator = static_cast<char*>
			(
				std::memchr(this->buffer_base, '\n', this->buffer_usage)
			);

			char* end = terminator ?: this->buffer_base + this->buffer_usage;

			line += std::string_view
			{
				this->buffer_base, static_cast<std::size_t>(end - this->buffer_base)
			};

			this->buffer_usage -= end - this->buffer_base;
			this->buffer_base = end;

			if(terminator != nullptr)
			{
				--this->buffer_usage;
				++this->buffer_base;

				found = true;
				break;
			}

			auto bytes = ::read(this->descriptor, this->buffer, BUFFER_SIZE);
			if(bytes == 0)
			{
				break;
			} else if(bytes < 0)
			{
				this->close();
				return false;
			}

			this->buffer_base = buffer;
			this->buffer_usage = bytes;
		}

		return found || !line.empty();
	}

	void socket::write(std::string_view output)
	{
		if(::write(this->descriptor, output.data(), output.length()) < 0)
		{
			this->close();
		}
	}

	void socket::write_variadic(const char* format, va_list arguments)
	{
		std::va_list test_arguments;

		va_copy(test_arguments, arguments);
		std::size_t buffer_size = 1 + std::vsnprintf(nullptr, 0, format, test_arguments);
		va_end(test_arguments);

		char* buffer = new char[buffer_size];

		std::vsnprintf(buffer, buffer_size, format, arguments);
		if(::write(this->descriptor, buffer, buffer_size - 1) < 0)
		{
			this->close();
		}

		delete[] buffer;
	}

	void socket::write_formatted(const char* format, ...)
	{
		std::va_list arguments;

		va_start(arguments, format);
		this->write_variadic(format, arguments);
		va_end(arguments);
	}

	_detail::reactor_base::~reactor_base()
	{
		if(this->epoll_descriptor >= 0)
		{
			::close(this->epoll_descriptor);
		}
	}

	_detail::reactor_base& _detail::reactor_base::operator=(reactor_base&& other) noexcept
	{
		if(this->epoll_descriptor >= 0)
		{
			::close(this->epoll_descriptor);
		}

		this->listen_socket = std::move(other.listen_socket);
		this->epoll_descriptor = other.epoll_descriptor;

		other.epoll_descriptor = -1;
		return *this;
	}

	_detail::reactor_base::reactor_base(socket listen_socket)
	: listen_socket{std::move(listen_socket)},
	  epoll_descriptor{::epoll_create1(EPOLL_CLOEXEC)}
	{
		if(this->epoll_descriptor < 0)
		{
			throw_errno();
		}

		this->watch(this->listen_socket.get_descriptor());
	}

	std::variant<int, socket> _detail::reactor_base::wait()
	{
		struct ::epoll_event event;
		if(::epoll_wait(this->epoll_descriptor, &event, 1, -1) != 1)
		{
			throw_errno();
		}

		int descriptor = event.data.fd;
		if(descriptor == this->listen_socket.get_descriptor())
		{
			auto new_socket = this->listen_socket.accept();
			if(!new_socket)
			{
				throw_errno();
			}

			return std::move(*new_socket);
		}

		return descriptor;
	}

	void _detail::reactor_base::watch(int descriptor)
	{
		struct ::epoll_event event;
		event.events = EPOLLIN | EPOLLRDHUP;
		event.data.fd = descriptor;

		if(::epoll_ctl(this->epoll_descriptor, EPOLL_CTL_ADD, descriptor, &event) != 0)
		{
			throw_errno();
		}
	}

	void _detail::reactor_base::forget(int descriptor)
	{
		if(::epoll_ctl(this->epoll_descriptor, EPOLL_CTL_DEL, descriptor, nullptr) != 0
		&& errno != EBADF) // The descriptor might already be closed
		{
			throw_errno();
		}
	}
}
