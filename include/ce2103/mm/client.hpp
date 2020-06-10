#ifndef CE2103_MM_CLIENT_HPP
#define CE2103_MM_CLIENT_HPP

#include <mutex>
#include <string>
#include <utility>
#include <cstddef>
#include <optional>
#include <string_view>

#include "ce2103/network.hpp"

#include "ce2103/mm/gc.hpp"
#include "ce2103/mm/session.hpp"

namespace ce2103::mm
{
	class client_session : public session
	{
		public:
			client_session(socket client_socket, std::string_view secret);

			bool finalize();

			std::optional<std::size_t> allocate
			(
				std::size_t part_size, std::size_t parts, std::size_t remainder
			);

			bool lift(std::size_t id);

			std::optional<drop_result> drop(std::size_t id);

			std::optional<std::string> fetch(std::size_t id);

			bool overwrite(std::size_t id, std::string_view contents);

		private:
			mutable std::mutex mutex;

			bool expect_empty();

			template<typename T>
			std::optional<T> expect_value();
	};

	class remote_manager : public memory_manager
	{
		private:
			struct private_t {};

		public:
			static bool initialize(socket client_socket, std::string_view secret);

			static remote_manager& get_instance();

			remote_manager(private_t, socket client_socket, std::string_view secret);

			virtual inline at get_locality() const noexcept final override
			{
				return at::remote;
			}

			virtual void lift(std::size_t id) final override;

			virtual drop_result drop(std::size_t id) final override;

			virtual void probe(const void* address, bool for_write = false) final override;

			virtual void evict(std::size_t id) final override;

		private:
			client_session client;
			void*          trap_base;

			[[noreturn]]
			static void throw_network_failure();

			virtual std::pair<std::size_t, void*> allocate(std::size_t size) final override;

			void install_trap_region();

			void* allocation_base_for(std::size_t id) noexcept;

			std::size_t get_part_size() const noexcept;

			void wipe(std::size_t id, std::size_t size);
	};
}

#endif
