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
	//! Indicates memory locality.
	enum class at
	{
		any, // Refers to the default locality
		local,
		remote
	};
}

namespace ce2103::mm::_detail
{
	//! Variant of debug_log() which transmits objects' IDs and localitis
	template<typename... PairTypes>
	void memory_debug_log(const char* operation, std::size_t id, at locality, PairTypes&&... pairs);
}

namespace ce2103::mm
{
	/*!
	 * \brief Allocation header present before all managed objects.
	 *        It includes important metadata, such as object count and
	 *        type information.
	 */
	class allocation
	{
		friend class memory_manager;

		public:
			//! Finishes allocation setup by indicating the total object count.
			inline void set_initialized(std::size_t count) noexcept
			{
				this->count = count;
			}

			//! Retrieves RTTI about the stored object.
			inline const std::type_info& get_type() const noexcept
			{
				return this->payload_type.rtti;
			}

			/*!
			 * \brief Returns the size of the whole allocation, including the
			 *        header and all objects.
			 */
			inline std::size_t get_total_size() const noexcept
			{
				return   sizeof(allocation) + this->payload_type.padding
				       + this->payload_type.size * this->count;
			}

			//! Returns the address of the first object in the allocation
			inline void* get_payload_base() const noexcept
			{
				return   const_cast<char*>(reinterpret_cast<const char*>(this))
				       + sizeof(allocation) + this->payload_type.padding;
			}

			//! Generates a human-readable representation of the allocation's objects.
			std::string make_representation();

		private:
			//! Holds metadata about a concrete, non-array type
			struct type final
			{
				//! Type RTTI
				const std::type_info& rtti;

				//! Type's destructor
				void (*const destructor)(void* object);

				//! sizeof(T)
				std::size_t size;

				//! Alignment padding between end-of-header and start-of-object
				std::size_t padding;

				void (*const represent)(void* object, std::size_t count, std::string& output);

				//! Constructs a type metadata record
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

			const type& payload_type; //!< Type information of the allocation payload
			std::size_t count = 0;    //!< Number of objects in the allocation

			//! Constructs an allocation header with the given type information
			inline allocation(const type& payload_type) noexcept
			: payload_type{payload_type}
			{}

			//! Invokes the payload destructor on all non trivially-destructible objects
			void destroy_all();
	};

	/*!
	 * \brief Communicates the object state after a drop operation.
	 *
	 * 'Reduced' indicates a refcount greater than one, 'hanging'
	 * a refcount of one, and 'lost' one of zero.
	 */
	enum class drop_result
	{
		reduced,
		hanging,
		lost
	};

	/*!
	 * \brief Subclasses of memory_manager provide an interface to
	 *        operate over allocations, such as creating them and
	 *        manipulating their reference counts.
	 */
	class memory_manager
	{
		public:
			//! Retrives the default manager for a given locality
			static memory_manager& get_default(at storage) noexcept;

			/*!
			 * \brief Creates a new allocation.
			 *
			 * \tparam T non-array type of the allocation's payload objects
			 *
			 * \parma count        number of contiguous objects
			 * \param always_array whether to treat this allocation
			 *                     as multiobject even if count <= 1
			 *
			 *  \return tuple containing the allocation ID, a pointer to the
			 *          allocation header, and a pointer to the first
			 *          (uninitialized) object
			 */
			template<typename T>
			std::tuple<std::size_t, allocation*, T*> allocate_of
			(
				std::size_t count, bool always_array = false
			);

			//! Increments the given allocation's reference count.
			void lift(std::size_t id);

			/*!
			 * \brief Decrements the reference count of the allocation.
			 *        If the count reaches zero, the ID is invalidated
			 *        an memory is disposed-of in a manager-specified manner.
			 *
			 * \return Indication of whether the refcount after the operation
			 *         is one, zero, or neither.
			 */
			drop_result drop(std::size_t id);

			//! Hints the end of a write operation. This might flush internal caches.
			void evict(std::size_t id);

			//! Returns this manager's locality
			virtual at get_locality() const noexcept = 0;

			/*!
			 * \brief Hints of a read or write operation in the near-future .
			 *        This might provide stronger resilience guarantees in case
			 *        of bus errors. Managers must still be able to handle any
			 *        operation without this indication, nonetheless.
			 *
			 *  \param address   an address somewhere in the allocation that is
			 *                   about to be accessed
			 *  \param for_write whether to hint a read or a write operation
			 */
			virtual inline void probe
			(
				[[maybe_unused]] const void* address,
				[[maybe_unused]] bool for_write = false
			)
			{}

			//! Determines the allocation header from an ID.
			virtual allocation& get_base_of(std::size_t id) = 0;

		protected:
			//! Protected exposition of allocation::destroy_all()
			inline void dispose(allocation& resource)
			{
				resource.destroy_all();
			}

		private:
			//! Reserves an ID for the given amount of bytes.
			virtual std::size_t allocate(std::size_t size) = 0;

			//! Manager-specific fragment of the lift operation.
			virtual void do_lift(std::size_t id) = 0;

			//! Manager-specific fragment of the drop operation.
			virtual drop_result do_drop(std::size_t id) = 0;

			//! Manager-specific fragment of the evict operation.
			virtual inline void do_evict([[maybe_unused]] std::size_t id)
			{}
	};

	//! A local mananger which frees memory periodically
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
		void* first_object = base->get_payload_base();

		_detail::memory_debug_log
		(
			"alloc", id, this->get_locality(), "type", demangle(base->get_type()),
			"address", first_object
		);

		return std::make_tuple(id, base, static_cast<T*>(first_object));
	}
}

#endif
