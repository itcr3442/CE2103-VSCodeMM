#ifndef CE2103_MM_CLIENT_HPP
#define CE2103_MM_CLIENT_HPP

#include <mutex>
#include <string>
#include <utility>
#include <cstddef>
#include <optional>
#include <typeinfo>
#include <string_view>

#include "ce2103/network.hpp"

#include "ce2103/mm/gc.hpp"
#include "ce2103/mm/session.hpp"

namespace ce2103::mm
{
	//! Client-side remote memory session
	class client_session : public session
	{
		public:
			/*!
			 * \brief Constructs a client session out of a socket and
			 *        the authorization secret. is_lost() will return
			 *        true afterwards if authorization or connection fails.
			 */
			client_session(socket client_socket, std::string_view secret);

			/*!
			 * \brief Terminates the session. Returns whether this was done
			 *        cleanly and without any leaks.
			 */
			bool finalize();

			/*!
			 * \brief Requests a new piecewise allocation to the server.
			 *        A number of sequential IDs are allocated according
			 *        to the part-remainder relations.
			 *
			 * If part_size or parts is zero, only the remainder is considered.
			 * If remainder is zero, only the evenly sized parts are considered.
			 *
			 * \param part_size size of each non-remainder part
			 * \param parts     number of part_size parts
			 * \param remainder size of the last allocation
			 * \param type      object type name
			 *
			 * \return first allocation ID, if successful
			 */
			std::optional<std::size_t> allocate
			(
				std::size_t part_size, std::size_t parts, std::size_t remainder, const char* type
			);

			//! Attempts to increment the reference count of a remote object.
			bool lift(std::size_t id);

			//! Attempts to decrement the reference count of a remote object.
			std::optional<drop_result> drop(std::size_t id);

			//! Attempts to retrieve the raw contents of a remote object.
			std::optional<std::string> fetch(std::size_t id);

			/*!
			 * \brief Sends a message which instructs to overwrite the
			 *        contents of a remote allocation.
			 *
			 * \return whether the operation succeeded
			 */
			bool overwrite(std::size_t id, std::string_view contents);

		private:
			mutable std::mutex mutex; //!< Mutex for multithread synchronization

			//! Receives a message and returns true if and only if it is '{}'
			bool expect_empty();

			//! Expects a message of the form '{..., "value": <a T>, ...}'
			template<typename T>
			std::optional<T> expect_value();
	};

	/*!
	 * \brief A manager which forwards requests to a network server.
	 *        Userspace virtual memory manipulation is used to do this
	 *        in a transparent manner.
	 */
	class remote_manager : public memory_manager
	{
		private:
			// Workaround for a libstdc++ limitation
			struct private_t {};

		public:
			/*!
			 * \brief Attempts to setup a properly established session
			 *        from the given socket and authorization secret.
			 *
			 * \return whether initialization succeeded
			 */
			static bool initialize(socket client_socket, std::string_view secret);

			//! Returns the quasi-singleton instance.
			static remote_manager& get_instance();

			//! Constructs 
			remote_manager(private_t, socket client_socket, std::string_view secret);

			//! Determines the manager's locality as being remote
			virtual inline at get_locality() const noexcept final override
			{
				return at::remote;
			}

			//! Hints of a read or write to the given address in the near future.
			virtual void probe(const void* address, bool for_write = false) final override;

			//! Returns the allocation header for a given ID.
			virtual allocation& get_base_of(std::size_t id) final override;

		private:
			client_session client;    //!< Active session
			void*          trap_base; //!< Start of the virtual trap region

			//! Throws a netwok error
			[[noreturn]]
			static void throw_network_failure();

			//! Reserves an ID for a new allocation of the given size.
			virtual std::size_t allocate
			(
				std::size_t size, const std::type_info& type
			) final override;

			//! Increments the reference count of an allocation/
			virtual void do_lift(std::size_t id) final override;

			//! Decrements the reference count of an allocation/
			virtual drop_result do_drop(std::size_t id) final override;

			//! Hints the end of a write operation.
			virtual void do_evict(std::size_t id) final override;

			/*!
			 * \brief Seizes a (very large) region of virtual address space
			 *        for remote memory.
			 */
			void install_trap_region();

			//! Returns the size of a part (virtual page).
			std::size_t get_part_size() const noexcept;

			/*!
			 * \brief Speculates the given allocation to contain
			 *        only the given amount of zero bytes, which is a
			 *        local-only operation. This optimizes away an
			 *        unnecessary read during the initialization of
			 *        allocated objects.
			 */
			void wipe(std::size_t id, std::size_t size);
	};
}

#endif
