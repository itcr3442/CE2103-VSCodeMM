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

#include "ce2103/rtti.hpp"
#include "ce2103/hash_map.hpp"

#include "ce2103/mm/debug.hpp"

namespace ce2103::mm
{
	enum class at
	{
		any,
		local,
		remote
	};
}

namespace ce2103::mm::_detail
{
	template<typename... PairTypes>
	void memory_debug_log(const char* operation, std::size_t id, at locality, PairTypes&&... pairs);
}

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

			inline const std::type_info& get_type() const noexcept
			{
				return this->payload_type.rtti;
			}

			inline std::size_t get_total_size() const noexcept
			{
				return   sizeof(allocation) + this->payload_type.padding
				       + this->payload_type.size * this->count;
			}

			inline void* get_payload_base() const noexcept
			{
				return   const_cast<char*>(reinterpret_cast<const char*>(this))
				       + sizeof(allocation) + this->payload_type.padding;
			}

			std::string make_representation();

		private:
			struct type final
			{
				const std::type_info& rtti;

				void (*const destructor)(void* object);

				std::size_t size;

				std::size_t padding;

				void (*const represent)(void* object, std::size_t count, std::string& output);

				inline constexpr type
				(
					const std::type_info& rtti, void (*destructor)(void*),
					std::size_t size, std::size_t padding, 
					void (*represent)(void*, std::size_t, std::string&)
				) noexcept
				: rtti{rtti}, destructor{destructor}, size{size},
				  padding{padding}, represent{represent}
				{}
			};

			const type& payload_type;
			std::size_t count = 0;

			inline allocation(const type& payload_type) noexcept
			: payload_type{payload_type}
			{}

			void destroy_all();
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

			void lift(std::size_t id);

			drop_result drop(std::size_t id);

			void evict(std::size_t id);

			virtual at get_locality() const noexcept = 0;

			virtual inline void probe
			(
				[[maybe_unused]] const void* address,
				[[maybe_unused]] bool for_write = false
			)
			{}

			virtual allocation& get_base_of(std::size_t id) = 0;

		protected:
			inline void dispose(allocation& resource)
			{
				resource.destroy_all();
			}

		private:
			virtual std::size_t allocate(std::size_t size) = 0;

			virtual void do_lift(std::size_t id) = 0;

			virtual drop_result do_drop(std::size_t id) = 0;

			virtual inline void do_evict([[maybe_unused]] std::size_t id)
			{}
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

			//! Returns the allocation header for a given ID
			virtual allocation& get_base_of(std::size_t id) final override;

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
			 * \return new ID
			 */
			virtual std::size_t allocate(std::size_t size) final override;

			/*!
			 * \brief Increments the reference count of the given object.
			 *
			 * \return Altered reference count
			 */
			virtual void do_lift(std::size_t id) final override;

			/*!
			 * \brief Decrements the reference count of the given object.
			 *        If the count reaches zero, the object will be collected
			 *        in the future by the GC.
			 *
			 * \return Result of the drop operation
			 */
			virtual drop_result do_drop(std::size_t id) final override;

			//! Main GC loop.
			void main_loop();
	};

	template<typename... PairTypes>
	void _detail::memory_debug_log
	(
		const char* operation, std::size_t id, at locality, PairTypes&&... pairs
	)
	{
		const char* locality_name;
		switch(locality)
		{
			case at::local:
				locality_name = "local";
				break;

			case at::remote:
				locality_name = "remote";
				break;

			default:
				locality_name = "unknown";
				break;
		}

		debug_log
		(
			operation, "id", id, "at", static_cast<std::string>(locality_name),
			std::forward<PairTypes>(pairs)...
		);
	}

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

		constexpr void (*represent_single)(void*, std::size_t, std::string&) = []
		(
			void* object_base, std::size_t, std::string& output
		)
		{
			T& object = *static_cast<T*>(object_base);
			if constexpr(std::is_fundamental_v<T>)
			{
				output.append(std::to_string(object));
			} else if constexpr(std::is_same_v<std::string, std::remove_cv_t<T>>)
			{
				output.push_back('"');
				output.append(object);
				output.push_back('"');
			} else
			{
				output.append("{...}");
			}
		};

		static constexpr allocation::type type_of_single
		{
			typeid(T), destructor, sizeof(T), padding, represent_single
		};

		static constexpr allocation::type type_of_array
		{
			typeid(T[]), destructor, sizeof(T), padding,
			[](void* object, std::size_t count, std::string& output)
			{
				output.push_back('[');
				for(std::size_t i = 0; i < count; ++i)
				{
					if(i > 0)
					{
						output.push_back(',');
					}

					represent_single(static_cast<T*>(object) + i, 1, output);
				}

				output.push_back(']');
			}
		};

		std::size_t id = this->allocate(header_size + sizeof(T) * count);
		allocation* base = &this->get_base_of(id);

		new(base) allocation{count == 1 && !always_array ? type_of_single : type_of_array};
		_detail::memory_debug_log
		(
			"alloc", id, this->get_locality(), "type", demangle(base->get_type())
		);

		return std::make_tuple(id, base, static_cast<T*>(base->get_payload_base()));
	}
}

#endif
