#include <new>
#include <mutex>
#include <thread>
#include <chrono>
#include <utility>
#include <cassert>
#include <cstddef>
#include <iostream>

#include "ce2103/mm/gc.hpp"

namespace ce2103::mm
{
	void allocation::destroy_all()
	{
		char* element_base = reinterpret_cast<char*>(this) + sizeof(allocation)
		                   + this->payload_type.padding;

		if(this->payload_type.destructor != nullptr)
		{
			for(std::size_t i = 0; i < this->count; ++i)
			{
				this->payload_type.destructor(element_base);
				element_base += this->payload_type.size;
			}
		}
	}

	garbage_collector& garbage_collector::get_instance()
	{
		//! Per the Standard, initialization will occur on the first call.
		static garbage_collector gc;
		return gc;
	}

	std::size_t garbage_collector::lift(std::size_t id)
	{
		std::lock_guard lock{this->mutex};

		auto* pair = this->allocations.search(id);
		return pair != nullptr ? ++pair->first : 0;
	}

	std::size_t garbage_collector::drop(std::size_t id)
	{
		std::lock_guard lock{this->mutex};

		if(auto* pair = this->allocations.search(id); pair != nullptr)
		{
			auto& reference_count = pair->first;

			/* If pair->first == 0, aborts in debug builds but doesn't
			 * underflow in release builds, resorting to saturation
			 * arithmetic instead.
			 */
			assert(reference_count > 0);
			if(reference_count > 0)
			{
				return --reference_count;
			}
		}

		return 0;
	}

	void garbage_collector::require_contiguous_ids(std::size_t ids) noexcept
	{
		std::size_t test_from = this->next_id;

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

	std::pair<std::size_t, void*> garbage_collector::allocate(std::size_t size)
	{
		void* base = ::operator new(size);

		std::lock_guard lock{this->mutex};

		std::size_t id;
		do
		{
			id = this->next_id++;
		} while(this->allocations.search(id) != nullptr);

		this->allocations.insert(id, std::make_pair(1, static_cast<allocation*>(base)));
		return std::make_pair(id, base);
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

			this->wakeup.notify_one();
		}

		terminated.join();
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

						dispose(*header);
						::operator delete(header);

						lock.lock();

						modified = true;
						break;
					}
				}
			} while(modified);
		} while(!is_last_run);

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

				auto type_name = header->get_demangled_type_name();
				std::cerr << " to [" << id << ": " << type_name << "]\n";
			}

			std::cerr << "=== Memory has been leaked ===\n";
		}
	}
}
