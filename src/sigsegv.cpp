/* Implements page fault management in userspace by handling SIGSEGV.
 * This depends on virtual memory overcommitting by the kernel, which
 * is on by default but can be disabled. Makes use of anonymous
 * sparse files and some x86-specific and Linux-specific :features.
 */

#include <tuple>
#include <mutex>
#include <string>
#include <chrono>
#include <thread>
#include <cerrno>
#include <cstddef>
#include <cassert>
#include <optional>
#include <exception>
#include <functional>
#include <string_view>
#include <system_error>
#include <condition_variable>

#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "ce2103/mm/gc.hpp"
#include "ce2103/mm/error.hpp"
#include "ce2103/mm/client.hpp"
 
using ce2103::mm::client_session;

namespace
{
	//! System page size. Always 4KiB on x86.
	const std::size_t PAGE_SIZE = ::sysconf(_SC_PAGESIZE);

	//! 256MiB/1TiB of virtual address space for 32-bit/64-bit platforms.
	constexpr auto REGION_SIZE = sizeof(char) << (16 + 3 * sizeof(void*));

	//! See Intel SDM vol. 3, section 4.7, figure 4-12, bit 1 (W/R)
	constexpr auto WRITE_FAULT_BIT = 1 << 1;

	//! Result of a request made to the fault handler
	enum class result
	{
		success,
		uncaught,
		fetch_failure,
		mapping_failure
	};

	//! Operation type for a fault handler request
	enum class operation
	{
		begin_read,  //!< Prepare for a read operation at the specified page
		begin_write, //!< Prepare for a write at the specified page
		terminate,   //!< Terminate the fault handler thread
		wipe,        //!< Assume a page as containing all-zeros and make it writable
		evict        //!< Flush a pending writeback operation
	};

	//! In case of a fault handling failure, throws the result code
	[[noreturn]]
	void throw_result(result which);

	/*!
	 * \brief Handles page faults, probe/evict hints and creates the
	 *        illusion of "remote virtual memory".
	 */
	class fault_handler
	{
		public:
			//! Free resources associated to the trap region
			~fault_handler();

			void* install(client_session& client);

			result process(operation action, const void* address, std::size_t limit = 0);

		private:
			struct transaction
			{
				operation   type;
				void*       address;
				std::size_t limit;
				result      response;

				inline transaction(operation type, void* address, std::size_t limit) noexcept
				: type{type}, address{address}, limit{limit}
				{}
			};

			//! Current active transaction
			transaction* request = nullptr;

			//! Mutex for thread synchronization
			mutable std::mutex mutex;

			//! Used to wake-up the handler thread on request
			std::condition_variable transition;

			//! Signal handlers are very limited, so a separate thread is needed
			std::thread handler_thread;

			//! Trap region base
			void* base;

			//! Sparse file which is mapped in the trap region
			int landing_fd;

			//! Session through which to perform remote memory operations
			client_session* client;

			//! Main loop of the handler thread
			void main_loop();

			/*!
			 * \brief Reduces rights and resources of an active page.
			 *
			 * \param page             active page
			 * \param invalidate       whether the page should be invalidated
			 * \param writeback_length non-zero to cause a writeback of that
			 *                         length; zero to not invoke a writeback
			 *
			 * \return result::success, otherwise an error
			 */
			result release(void* page, bool invalidate, std::size_t writeback_length);

			/*!
			 * \brief Require the creation or promotion of a virtual
			 *        memory mapping.
			 *
			 * \param page     target virtual page
			 * \param fetch    whether to fetch this page from the server
			 * \param writable whether the page should be made writable
			 *
			 * If fetch is false and the page is not already mapped, it will
			 * be wiped. If it is already mapped, it might only be made writable
			 * depending on the corresponding parameter.
			 *
			 * \return pair of result::success (otherwise an error) and
			 *         the length of the new mapping (0 if the mapping
			 *         has not changed or a wipe was requested).
			 */
			std::pair<result, std::size_t> require(void* page, bool fetch, bool writable);

			//! Returns a (page number, offset) pair if the address is in the trap region
			auto get_position_of(void* page) const noexcept
				-> std::optional<std::pair<std::size_t, std::size_t>>;
	} handler;

	/*!
	 * \brief Signal handler for SIGSEGV. The function's signature is compatible
	 *        with what ::sigaction() requires.
	 *
	 * \param signal_info kernel-provided signal state
	 * \param context     kernel-provided thread context and CPU registers
	 */
	void handle_segmentation_fault(int, ::siginfo_t* signal_info, void* context) noexcept;

	[[noreturn]]
	void throw_result(result which)
	{
		using ce2103::mm::error_code;

		error_code error;
		switch(which)
		{
			case result::fetch_failure:
				error = error_code::network_failure;
				break;

			case result::mapping_failure:
				error = error_code::memory_error;
				break;

			default:
				error = error_code::unknown;
				break;
		}

		throw std::system_error{error};
	}

	fault_handler::~fault_handler()
	{
		if(this->handler_thread.joinable())
		{
			if(auto result = this->process(operation::terminate, nullptr);
			   result != result::success)
			{
				throw_result(result);
			}

			this->handler_thread.join();

			::munmap(this->base, REGION_SIZE);
			::close(landing_fd);

			this->client->finalize();
		}
	}

	void* fault_handler::install(client_session& client)
	{
		std::lock_guard lock{this->mutex};

		assert(!this->handler_thread.joinable());

		struct ::sigaction action = {};
		action.sa_flags = SA_SIGINFO;
		action.sa_sigaction = &handle_segmentation_fault;

		this->landing_fd = -1;
		this->base = MAP_FAILED;

		/* In order:
		 *   - Sets the SIGSEGV handler
		 *   - Creates the anonymous sparse file
		 *   - Maps the trap region into the virtual address space
		 *   - Starts the handler thread
		 */
		if(::sigaction(SIGSEGV, &action, nullptr) != -1
		&& (this->landing_fd = ::memfd_create("landing", MFD_CLOEXEC)) != -1
		&& ::ftruncate(this->landing_fd, REGION_SIZE) != -1
		&& MAP_FAILED != (this->base = ::mmap
		(
			nullptr, REGION_SIZE, PROT_NONE,
			MAP_SHARED | MAP_NORESERVE, this->landing_fd, 0
		)))
		{
			this->client = &client;
			this->handler_thread = std::thread{&fault_handler::main_loop, this};

			return this->base;
		}

		// If something failed, undo the previous operations

		int old_errno = errno;

		if(this->base != MAP_FAILED)
		{
			::munmap(this->base, REGION_SIZE);
		}

		if(this->landing_fd != -1)
		{
			::close(this->landing_fd);
		}

		action.sa_handler = SIG_DFL;
		::sigaction(SIGSEGV, &action, nullptr);

		throw std::system_error{old_errno, std::system_category()};
	}

	result fault_handler::process(operation action, const void* address, std::size_t limit)
	{
		std::unique_lock lock{this->mutex};

		this->transition.wait(lock, [this]
		{
			return this->request == nullptr;
		});

		transaction request{action, const_cast<void*>(address), limit};
		this->request = &request;
		this->transition.notify_all();

		this->transition.wait(lock, [&, this]
		{
			return this->request != &request;
		});

		return request.response;
	}

	void fault_handler::main_loop()
	{
		constexpr std::chrono::milliseconds WRITEBACK_TIMEOUT{5};

		auto is_pending = [this]
		{
			return this->request != nullptr;
		};

		std::unique_lock lock{this->mutex};

		void* active = nullptr;
		bool writeback = false;
		std::size_t length = 0;

		bool terminate = false;

		// An error condition might need to be propagated to the next request
		auto delayed_result = result::success;

		while(!terminate)
		{
			// Wait for either a request or a writeback timeout
			bool requested = true;
			if(writeback)
			{
				requested = this->transition.wait_for(lock, WRITEBACK_TIMEOUT, is_pending);
			} else
			{
				this->transition.wait(lock, is_pending);
			}

			bool wipe = false;
			bool evict = false;

			if(requested)
			{
				// Set flags from the request block
				switch(this->request->type)
				{
					case operation::wipe:
						wipe = true;
						break;

					case operation::evict:
						evict = true;
						break;

					case operation::terminate:
						terminate = true;
						break;

					default:
						break;
				}
			}

			/* Manipulate bits of this->request->address to
			 * determine the base of the virtual page in which
			 * it is located. Normally, this will just reset
			 * the lower 12 (2^12 bytes = 4KiB) bits of the address.
			 */
			void* page = !requested || terminate ? nullptr : reinterpret_cast<void*>
			(
				  reinterpret_cast<std::uintptr_t>(this->request->address)
				& ~(PAGE_SIZE - 1) 
			);

			// The current active page must be invalidated under any of these conditions
			bool invalidate = evict == (active == page) || wipe || page == nullptr;

			if(active != nullptr)
			{
				std::size_t writeback_length = writeback ? length : 0;

				/* If the active page is writable, writes back and makes it
				 * read-only. Conditionally, it also invalidates the page.
				 */
				auto release_result = this->release(active, invalidate, writeback_length);
				if(delayed_result == result::success)
				{
					delayed_result = release_result;
				}

				writeback = false;
				if(invalidate)
				{
					active = nullptr;
				}
			}

			if(requested)
			{
				auto& response = this->request->response;

				// Delayed results are forwarded here, and terminate/evict break here.
				if(delayed_result != result::success || terminate || evict)
				{
					response = delayed_result;
					delayed_result = result::success;
				} else
				{
					/* Whether the current page is writeable and whether
					 * the next (possibly the same) active page should bhe
					 * made writable.
					 */
					bool writable = !invalidate && writeback;
					bool begin_write = wipe || this->request->type == operation::begin_write;

					/* Either setup a new active page or grant more
					 * rights to the current one.
					 */
					std::size_t new_length;
					std::tie(response, new_length) = this->require
					(
						page, invalidate && !wipe, begin_write && !writable
					);

					// This occurs on wipes and on promotions of the current active page
					if(new_length == 0)
					{
						new_length = wipe ? this->request->limit : length;
					}

					if(response == result::success)
					{
						active = page;
						length = new_length;
						writeback = writable || begin_write;
					}
				}

				// Request is done, allow the next one in
				this->request = nullptr;
				this->transition.notify_all();
			}
		}
	}

	result fault_handler::release(void* page, bool invalidate, std::size_t writeback_length)
	{
		if(writeback_length > 0)
		{
			//! Disable writing
			if(::mprotect(page, PAGE_SIZE, invalidate ? PROT_NONE : PROT_READ) == -1)
			{
				return result::mapping_failure;
			}

			// Prepare the writeback buffer
			std::string contents;
			contents.resize(writeback_length);

			auto position = this->get_position_of(page);
			assert(position);

			// Remote object ID == page number
			auto [id, page_offset] = *position;

			/* Since the page is/was a mapping of the sparse file,
			 * the contents to writeback can be read from this file.
			 */
			if(::lseek(this->landing_fd, page_offset, SEEK_SET) == -1
			|| ::read(this->landing_fd, &contents[0], writeback_length)
			   != static_cast<::ssize_t>(writeback_length)
			|| !client->overwrite(id, contents))
			{
				return result::fetch_failure;
			}
		}

		if(invalidate)
		{
			constexpr auto MMAP_FLAGS = MAP_FIXED | MAP_SHARED | MAP_NORESERVE;
			constexpr auto FALLOCATE_FLAGS = FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE;

			// mmap() recreates the whole trap region, invalidating the package
			if(::mmap(this->base, REGION_SIZE, PROT_NONE,
					  MMAP_FLAGS, this->landing_fd, 0) != this->base
			// fallocate() punches a hole in the sparse file where the page was stored
			|| ::fallocate(this->landing_fd, FALLOCATE_FLAGS, 0, REGION_SIZE) == -1)
			{
				return result::mapping_failure;
			}
		}

		return result::success;
	}

	std::pair<result, std::size_t> fault_handler::require(void* page, bool fetch, bool writable)
	{
		std::size_t length = 0;
		if(fetch)
		{
			auto position = this->get_position_of(page);
			if(!position)
			{
				return std::make_pair(result::uncaught, 0);
			}

			// Actually retrieve the page contents from the server
			auto [id, page_offset] = *position;
			auto contents = this->client->fetch(id);

			/* Write the page contents into the corresponding part
			 * of the sparse file, thereby writing it into the
			 * virtual mapping.
			 */
			if(!contents
			|| (length = contents->length()) > PAGE_SIZE
			|| ::lseek(this->landing_fd, page_offset, SEEK_SET) == -1
			|| ::write(this->landing_fd, contents->data(), length)
			   != static_cast<::ssize_t>(length))
			{
				return std::make_pair(result::fetch_failure, 0);
			}
		}

		if(fetch || writable)
		{
			// Enable or increase access rights of the page
			int protection = PROT_READ | (writable ? PROT_WRITE : 0);
			if(::mprotect(page, PAGE_SIZE, protection) == -1)
			{
				return std::make_pair(result::mapping_failure, 0);
			}
		}

		return std::make_pair(result::success, length);
	}

	auto fault_handler::get_position_of(void* page) const noexcept
		-> std::optional<std::pair<std::size_t, std::size_t>>
	{
		auto difference = static_cast<char*>(page) - static_cast<char*>(this->base);
		if(difference >= 0 && difference < static_cast<std::ptrdiff_t>(REGION_SIZE))
		{
			auto offset = static_cast<std::size_t>(difference);
			return std::make_pair(offset / PAGE_SIZE, offset);
		}

		return std::nullopt;
	}

	void handle_segmentation_fault(int, ::siginfo_t* signal_info, void* context) noexcept
	{
		auto terminate = [](std::string_view last_words) noexcept
		{
			::write(STDERR_FILENO, last_words.data(), last_words.length());
			std::terminate();

			__builtin_unreachable();
		};

		// Stack unwinding through a signal handler triggers undefined behavior
		try
		{
			bool was_write = static_cast<::ucontext_t*>
			(
				context
			)->uc_mcontext.gregs[REG_ERR] & WRITE_FAULT_BIT;

			if(signal_info->si_code == SEGV_ACCERR)
			{
				auto type = was_write ? operation::begin_write : operation::begin_read;
				if(auto response = handler.process(type, signal_info->si_addr);
				   response == result::uncaught)
				{
					/* If control reaches this point, this is a true segmentation
					 * fault. The following sequence removes this handler and
					 * crashes the process by triggering another fault on return.
					 */
					struct ::sigaction action;
					action.sa_handler = SIG_DFL;

					::sigaction(SIGSEGV, &action, nullptr);
				} else if(response != result::success)
				{
					terminate("=== Unchecked remote memory operation failed ===\n");
				}
			}
		} catch(...)
		{
			terminate("=== Uncaught exception reached the SIGSEGV handler ===\n");
		}
	}
}

namespace ce2103::mm
{
	void remote_manager::probe(const void* address, bool for_write)
	{
		auto type = for_write ? operation::begin_write : operation::begin_read;
		if(auto result = handler.process(type, address);
		   result != result::success)
		{
			throw_result(result);
		}
	}

	allocation& remote_manager::get_base_of(std::size_t id)
	{
		return *reinterpret_cast<allocation*>(static_cast<char*>(this->trap_base) + PAGE_SIZE * id);
	}

	void remote_manager::do_evict(std::size_t id)
	{
		void* address = &this->get_base_of(id);
		if(auto result = handler.process(operation::evict, address);
		   result != result::success)
		{
			throw_result(result);
		}
	}

	void remote_manager::install_trap_region()
	{
		this->trap_base = handler.install(this->client);
	}

	std::size_t remote_manager::get_part_size() const noexcept
	{
		return PAGE_SIZE;
	}

	void remote_manager::wipe(std::size_t id, std::size_t size)
	{
		void* address = &this->get_base_of(id);
		if(auto result = handler.process(operation::wipe, address, size);
		   result != result::success)
		{
			throw_result(result);
		}
	}
}
