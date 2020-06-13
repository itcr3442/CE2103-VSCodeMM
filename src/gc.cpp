#include <new>
#include <mutex>
#include <thread>
#include <chrono>
#include <utility>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <typeinfo>
#include <stdexcept>

#include "ce2103/rtti.hpp"

#include "ce2103/mm/gc.hpp"
#include "ce2103/mm/debug.hpp"

using ce2103::mm::at;

namespace ce2103::mm
{
	void allocation::destroy_all()
	{
		auto* element_base = static_cast<char*>(this->get_payload_base());
		if(this->payload_type.destructor != nullptr)
		{
			for(std::size_t i = 0; i < this->count; ++i)
			{
				this->payload_type.destructor(element_base);
				element_base += this->payload_type.size;
			}
		}
	}

	std::string allocation::make_representation()
	{
		std::string output;
		this->payload_type.represent(this->get_payload_base(), this->count, output);

		return output;
	}

	void memory_manager::lift(std::size_t id)
	{
		this->do_lift(id);
		_detail::memory_debug_log("lift", id, this->get_locality());
	}

	drop_result memory_manager::drop(std::size_t id)
	{
		auto result = this->do_drop(id);
		_detail::memory_debug_log("drop", id, this->get_locality());

		return result;
	}

	void memory_manager::evict(std::size_t id)
	{
		auto representation = this->get_base_of(id).make_representation();
		_detail::memory_debug_log("write", id, this->get_locality(), "value", std::move(representation));

		this->do_evict(id);
	}

	garbage_collector& garbage_collector::get_instance()
	{
		//! Per the Standard, initialization will occur on the first call.
		static garbage_collector gc;
		return gc;
	}

	void garbage_collector::require_contiguous_ids(std::size_t ids) noexcept
	{
		std::size_t test_from = this->next_id;

		// Check that the expected range is completely free
		bool found = false;
		while(!found)
		{
			found = true;
			for(std::size_t candidate = test_from; candidate < test_from + ids; ++candidate)
			{
				if(this->allocations.search(candidate) != nullptr)
				{
					found = false;
					test_from = candidate + 1;

					break;
				}
			}
		}

		this->next_id = test_from;
	}

	allocation& garbage_collector::get_base_of(std::size_t id)
	{
		std::lock_guard lock{this->mutex};

		auto* count_base = this->allocations.search(id);
		if(count_base == nullptr)
		{
			throw std::invalid_argument{"ID is unassigned"};
		}

		[[maybe_unused]]
		auto [count, base] = *count_base;

		return *base;
	}

	garbage_collector::garbage_collector()
	{
		std::lock_guard lock{this->mutex};
		this->thread = std::thread{&garbage_collector::main_loop, this};
	}

	garbage_collector::~garbage_collector()
	{
		std::thread terminated;
		{
			std::lock_guard lock{this->mutex};
			terminated = std::move(this->thread);

			// Wake-up the GC thread immediately
			this->wakeup.notify_one();
		}

		terminated.join();
	}

	std::size_t garbage_collector::allocate(std::size_t size, const std::type_info&)
	{
		void* base = ::operator new(size);

		std::lock_guard lock{this->mutex};

		std::size_t id;
		do
		{
			id = this->next_id++;
		} while(this->allocations.search(id) != nullptr);

		this->allocations.insert(id, std::make_pair(1, static_cast<allocation*>(base)));
		return id;
	}

	void garbage_collector::do_lift(std::size_t id)
	{
		std::lock_guard lock{this->mutex};

		auto* pair = this->allocations.search(id);
		assert(pair != nullptr);

		++pair->first;
	}

	drop_result garbage_collector::do_drop(std::size_t id)
	{
		std::lock_guard lock{this->mutex};

		auto* pair = this->allocations.search(id);
		assert(pair != nullptr && pair->first > 0);

		switch(--pair->first)
		{
			case 1:
				return drop_result::hanging;

			case 0:
				return drop_result::lost;

			default:
				return drop_result::reduced;
		}
	}

	void garbage_collector::main_loop()
	{
		constexpr std::chrono::seconds GC_PERIOD{5};

		std::unique_lock lock{this->mutex};
		bool is_last_run;

		do
		{
			//! Non-joinability indicates GC termination.
			is_last_run = this->wakeup.wait_for(lock, GC_PERIOD, [this]
			{
				return !this->thread.joinable();
			});

			bool modified;
			do
			{
				modified = false;
				for(const auto& [id, pair] : this->allocations)
				{
					auto [count, header] = pair;
					if(count == 0)
					{
						this->allocations.remove(id);

						/* Otherwise could deadlock (eg, if a VSPtr<T> is destroyed,
						 * therefore calling this->drop()).
						 */
						lock.unlock();

						// Destroy the objects and then the free the allocation
						dispose(*header);
						::operator delete(header);

						lock.lock();

						modified = true;
						break;
					}
				}
			} while(modified);
		} while(!is_last_run);

		// By design, unattended circular references might cause leaks
		if(this->allocations.get_size() > 0)
		{
			std::cerr << "=== These allocations have stale references at GC termination ===\n";

			for(const auto& [id, pair] : this->allocations)
			{
				auto [count, header] = pair;

				std::cerr << "  - " << count << " reference";
				if(count > 1)
				{
					std::cerr << 's';
				}

				std::cerr << " to [" << id << ": " << demangle(header->get_type()) << "]\n";
			}

			std::cerr << "=== Memory has been leaked ===\n";
		}
	}
}
