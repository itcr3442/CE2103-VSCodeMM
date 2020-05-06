/* Implements page fault management in userspace by handling SIGSEGV.
 * This depends on virtual memory overcommitting by the kernel, which
 * is on by default but can be disabled. This also uses anonymous
 * sparse files.
 */

#include <tuple>
#include <mutex>
#include <string>
#include <chrono>
#include <thread>
#include <cerrno>
#include <csetjmp>
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
	const std::size_t PAGE_SIZE = ::sysconf(_SC_PAGESIZE);

	//! 256MiB/1TiB of virtual address space for 32-bit/64-bit platforms.
	constexpr auto REGION_SIZE = sizeof(char) << (16 + 3 * sizeof(void*));

	constexpr auto WRITE_FAULT_BIT = 1 << 1;

	enum class result
	{
		// 0 isn't used in order to disambiguate setjmp()
		success = 1,
		uncaught,
		fetch_failure,
		mapping_failure
	};

	enum class operation
	{
		begin_read,
		begin_write,
		terminate,
		wipe,
		evict
	};

	void throw_result(result which);

	class fault_handler
	{
		public:
			~fault_handler();

			void* install(client_session& client);

			result process(operation action, void* address, std::size_t limit = 0);

			void evict(void* page);

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

			transaction* request = nullptr;

			mutable std::mutex mutex;

			std::condition_variable transition;

			std::thread handler_thread;

			void* base;

			int landing_fd;

			client_session* client;

			void main_loop();

			result release(void* page, bool invalidate, std::size_t writeback_length);

			std::pair<result, std::size_t> require(void* page, bool fetch, bool writable);

			auto get_position_of(void* page) const noexcept
				-> std::optional<std::pair<std::size_t, std::size_t>>;
	} handler;

	void handle_segmentation_fault(int, ::siginfo_t* signal_info, void* context) noexcept;

	thread_local std::jmp_buf* probe_checkpoint = nullptr;

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

	result fault_handler::process(operation action, void* address, std::size_t limit)
	{
		std::unique_lock lock{this->mutex};

		this->transition.wait(lock, [this]
		{
			return this->request == nullptr;
		});

		transaction request{action, address, limit};
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
		auto delayed_result = result::success;

		while(!terminate)
		{
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

			void* page = !requested || terminate ? nullptr : reinterpret_cast<void*>
			(
				  reinterpret_cast<std::uintptr_t>(this->request->address)
				& ~(PAGE_SIZE - 1) 
			);

			bool invalidate = evict == (active == page) || wipe || page == nullptr;
			if(active != nullptr)
			{
				std::size_t writeback_length = writeback ? length : 0;

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
				if(delayed_result != result::success || terminate || evict)
				{
					response = delayed_result;
					delayed_result = result::success;
				} else
				{
					bool writable = !invalidate && writeback;
					bool begin_write = wipe || this->request->type == operation::begin_write;

					std::size_t new_length;
					std::tie(response, new_length) = this->require
					(
						page, invalidate && !wipe, begin_write && !writable
					);

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

				this->request = nullptr;
				this->transition.notify_all();
			}
		}
	}

	result fault_handler::release(void* page, bool invalidate, std::size_t writeback_length)
	{
		if(writeback_length > 0)
		{
			if(::mprotect(page, PAGE_SIZE, invalidate ? PROT_NONE : PROT_READ) == -1)
			{
				return result::mapping_failure;
			}

			std::string contents;
			contents.resize(writeback_length);

			auto position = this->get_position_of(page);
			assert(position);

			auto [id, page_offset] = *position;

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

			if(::mmap(this->base, REGION_SIZE, PROT_NONE,
			          MMAP_FLAGS, this->landing_fd, 0) != this->base
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

			auto [id, page_offset] = *position;
			auto contents = this->client->fetch(id);

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
				auto action = was_write ? operation::begin_write : operation::begin_read;
				if(auto response = handler.process(action, signal_info->si_addr);
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
					if(probe_checkpoint != nullptr)
					{
						std::longjmp(*probe_checkpoint, static_cast<int>(response));
					} else
					{
						terminate("=== Non-probing remote memory operation failed ===\n");
					}
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
	void remote_manager::probe(const void* address)
	{
		std::jmp_buf checkpoint;

		int response = setjmp(checkpoint);
		if(response == 0)
		{
			probe_checkpoint = &checkpoint;

			[[maybe_unused]]
			char canary = *static_cast<volatile const char*>(address);
		}

		probe_checkpoint = nullptr;
		if(response > 0 && response != static_cast<int>(result::success))
		{
			throw_result(static_cast<result>(response));
		}
	}

	void remote_manager::install_trap_region()
	{
		this->trap_base = handler.install(this->client);
	}

	void* remote_manager::allocation_base_for(std::size_t id) noexcept
	{
		return static_cast<char*>(this->trap_base) + PAGE_SIZE * id;
	}

	std::size_t remote_manager::get_part_size() const noexcept
	{
		return PAGE_SIZE;
	}

	void remote_manager::wipe(std::size_t id, std::size_t size)
	{
		void* address = this->allocation_base_for(id);
		if(auto result = handler.process(operation::wipe, address, size);
		   result != result::success)
		{
			throw_result(result);
		}
	}

	void remote_manager::evict(std::size_t id)
	{
		void* address = this->allocation_base_for(id);
		if(auto result = handler.process(operation::evict, address);
		   result != result::success)
		{
			throw_result(result);
		}
	}
}
