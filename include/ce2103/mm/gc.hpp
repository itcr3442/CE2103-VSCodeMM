#ifndef CE2103_MM_GC_HPP
#define CE2103_MM_GC_HPP

#include <tuple>
#include <mutex>
#include <thread>
#include <string>
#include <utility>
#include <cstddef>
#include <typeinfo>
#include <type_traits>
#include <condition_variable>

#include "ce2103/hash_map.hpp"

namespace ce2103::mm
{
	class allocation
	{
		friend class memory_manager;

		public:
			inline void set_initialized(std::size_t count) noexcept
			{
				this->count = count;
			}

			inline const char* get_mangled_type_name() const noexcept
			{
				return this->type.name();
			}

		private:
			const std::type_info& type;
			void (*destructor)(void* object);
			std::size_t padding;
			std::size_t size;
			std::size_t count = 0;

			inline allocation
			(
				const std::type_info& type, void (*destructor)(void*),
				std::size_t padding, std::size_t size
			) noexcept
			: type{type}, destructor{destructor}, padding{padding}, size{size}
			{}

			void destroy_all();
	};

	class memory_manager
	{
		public:
			static memory_manager& get_default() noexcept;

			template<typename T>
			std::tuple<std::size_t, allocation*, T*> allocate_of
			(
				std::size_t count, bool always_array = false
			);

			virtual std::size_t lift(std::size_t id) = 0;
			virtual std::size_t drop(std::size_t id) = 0;

			inline virtual void probe([[maybe_unused]] const void* address)
			{}

		protected:
			inline void dispose(allocation& resource)
			{
				resource.destroy_all();
			}

		private:
			virtual std::pair<std::size_t, void*> allocate(std::size_t size) = 0;
	};

	class garbage_collector : public memory_manager
	{
		public:
			/*!
			 * \brief Returns a reference to the singleton instance,
			 *        creating it if necessary.
			 */
			static garbage_collector& get_instance();

			/*!
			 * \brief Increments the reference count of the given object.
			 *
			 * \return Altered reference count
			 */
			virtual std::size_t lift(std::size_t id) final override;

			/*!
			 * \brief Decrements the reference count of the given object.
			 *        If the count reaches zero, the object will be collected
			 *        in the future by the GC.
			 *
			 * \return Altered reference count
			 */
			virtual std::size_t drop(std::size_t id) final override;

		private:
			//! Map of ID-(refcount, allocation header) pairs for each allocation.
			hash_map<std::size_t, std::pair<std::size_t, allocation*>> allocations;

			//! Tentative ID for the next allocation.
			std::size_t next_id = 0;

			//! Used to guarantee thread-safety.
			mutable std::mutex mutex;

			//! Main GC loop thread.
			std::thread thread;

			//! Used to explicitly wake the GC outside of its period.
			std::condition_variable wakeup;

			//! Initializes the GC thread on construction.
			garbage_collector();

			//! Terminates the GC.
			~garbage_collector();

			/*!
			 * \brief Allocates a new memory region owned by the GC.
			 *
			 * \param size region size, in bytes
			 *
			 * \return ID-base pair
			 */
			virtual std::pair<std::size_t, void*> allocate(std::size_t size) final override;

			//! Main GC loop.
			void main_loop();
	};

	template<typename T>
	std::tuple<std::size_t, allocation*, T*> memory_manager::allocate_of
	(
		std::size_t count, bool always_array
	)
	{
		constexpr auto padding = (alignof(T) - alignof(allocation) % alignof(T)) % alignof(T);
		constexpr auto header_size = sizeof(allocation) + padding;

		auto [id, base] = this->allocate(header_size + sizeof(T) * count);

		void (*destructor)(void* object) = nullptr;
		if constexpr(!std::is_trivially_destructible_v<T>)
		{
			destructor = [](void* object)
			{
				static_cast<T*>(object)->~T();
			};
		}

		this->probe(base);

		auto& type = count == 1 && !always_array ? typeid(T) : typeid(T[]);
		new(base) allocation{type, destructor, padding, sizeof(T)};

		auto* first_element = reinterpret_cast<T*>(static_cast<char*>(base) + header_size);
		return std::make_tuple(id, static_cast<allocation*>(base), first_element);
	}
}

#endif
