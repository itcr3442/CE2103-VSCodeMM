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
				return this->payload_type.rtti.name();
			}

			std::string get_demangled_type_name() const;

			inline std::size_t get_total_size() const noexcept
			{
				return   sizeof(allocation) + this->payload_type.padding
				       + this->payload_type.size * this->count;
			}

		private:
			struct type final
			{
				const std::type_info& rtti;

				void (*const destructor)(void* object);

				std::size_t size;

				std::size_t padding;

				inline constexpr type
				(
					const std::type_info& rtti, void (*const destructor)(void*),
					std::size_t size, std::size_t padding
				) noexcept
				: rtti{rtti}, destructor{destructor}, size{size}, padding{padding}
				{}
			};

			const type& payload_type;
			std::size_t count = 0;

			inline allocation(const type& payload_type) noexcept
			: payload_type{payload_type}
			{}

			void destroy_all();
	};

	enum class at
	{
		any,
		local,
		remote
	};

	enum class drop_result
	{
		reduced,
		hanging,
		lost
	};

	class memory_manager
	{
		public:
			static memory_manager& get_default(at storage) noexcept;

			template<typename T>
			std::tuple<std::size_t, allocation*, T*> allocate_of
			(
				std::size_t count, bool always_array = false
			);

			virtual at get_locality() const noexcept = 0;

			virtual void lift(std::size_t id) = 0;

			virtual drop_result drop(std::size_t id) = 0;

			virtual inline void probe
			(
				[[maybe_unused]] const void* address,
				[[maybe_unused]] bool for_write = false
			)
			{}

			virtual void evict(std::size_t id) = 0;

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

			//! Indicates that this is a local memory manager
			virtual inline at get_locality() const noexcept final override
			{
				return at::local;
			}

			/*!
			 * \brief Increments the reference count of the given object.
			 *
			 * \return Altered reference count
			 */
			virtual void lift(std::size_t id) final override;

			/*!
			 * \brief Decrements the reference count of the given object.
			 *        If the count reaches zero, the object will be collected
			 *        in the future by the GC.
			 *
			 * \return Result of the drop operation
			 */
			virtual drop_result drop(std::size_t id) final override;

			//! Hints the end of a write operation.
			virtual void evict(std::size_t id) final override;

			/*!
			 * \brief Enforces that, if no other operation is performed,
			 *        the following given number of allocations will be
			 *        contiguous and ordered in the ID namespace.
			 */
			void require_contiguous_ids(std::size_t ids) noexcept;

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

		constexpr void (*destructor)(void* object)
			= !std::is_trivially_destructible_v<T>
			? static_cast<void(*)(void*)>([](void* object)
			{
				static_cast<T*>(object)->~T();
			})
			: nullptr;

		using type = allocation::type;

		static constexpr type type_of_single{typeid(T), destructor, sizeof(T), padding};
		static constexpr type type_of_array{typeid(T[]), destructor, sizeof(T), padding};

		auto [id, base] = this->allocate(header_size + sizeof(T) * count);
		new(base) allocation{count == 1 && !always_array ? type_of_single : type_of_array};

		auto* first_element = reinterpret_cast<T*>(static_cast<char*>(base) + header_size);
		return std::make_tuple(id, static_cast<allocation*>(base), first_element);
	}
}

#endif
