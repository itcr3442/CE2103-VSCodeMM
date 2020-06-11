#ifndef CE2103_MM_ALLOCATOR_HPP
#define CE2103_MM_ALLOCATOR_HPP

#include <utility>
#include <cstddef>
#include <iterator>
#include <type_traits>

#include "ce2103/mm/gc.hpp"
#include "ce2103/mm/vsptr.hpp"

namespace ce2103::mm
{
	template<typename T>
	class allocator;

	//! Unchecked variant of VSPtr<T>, for use by allocator<T>
	template<typename T>
	class unsafe_ptr : public VSPtr<T>
	{
		friend class allocator<T>;

		using VSPtr<T>::VSPtr;

		public:
			//! Required by the Standard; constructs a unsafe_ptr to an arbitrary address
			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			static inline unsafe_ptr pointer_to(T& object) noexcept
			{
				return unsafe_ptr{&object, 0, at::any};
			}

			//! Constructs a null pointer
			inline unsafe_ptr() noexcept
			: unsafe_ptr{nullptr, 0, at::any}
			{}

			//! Constructs a pointer to an arbitrary address
			/* implicit */ inline unsafe_ptr(T* data) noexcept
			: unsafe_ptr{data, 0, at::any}
			{}

			//! Copy-constructs a pointer
			unsafe_ptr(const unsafe_ptr& other) = default;

			//! Move-constructs a pointer
			unsafe_ptr(unsafe_ptr&& other) = default;

			//! Copy-assigns a pointer
			unsafe_ptr& operator=(const unsafe_ptr& other) = default;

			//! Move-assigns a pointer
			unsafe_ptr& operator=(unsafe_ptr&& other) = default;

			//! Performs pointer arithmetic
			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			inline unsafe_ptr& operator++() noexcept
			{
				return *(++this->data, this);
			}

			//! Performs pointer arithmetic
			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			inline unsafe_ptr operator++(int) noexcept
			{
				return this->template clone_with<unsafe_ptr>(this->data++);
			}

			//! Performs pointer arithmetic
			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			inline unsafe_ptr& operator--() noexcept
			{
				return *(--this->data, this);
			}

			//! Performs pointer arithmetic
			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			inline unsafe_ptr operator--(int) noexcept
			{
				return this->template clone_with<unsafe_ptr>(this->data--);
			}

			//! Performs pointer arithmetic
			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			inline unsafe_ptr operator+(std::ptrdiff_t offset) const noexcept
			{
				return this->template clone_with<unsafe_ptr>(this->data + offset);
			}

			//! Performs pointer arithmetic
			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			inline unsafe_ptr operator-(std::ptrdiff_t offset) const noexcept
			{
				return this->template clone_with<unsafe_ptr>(this->data - offset);
			}

			//! Performs pointer arithmetic
			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			inline std::ptrdiff_t operator-(const unsafe_ptr& other) const noexcept
			{
				return this->data - other.data;
			}

			//! Dereferences a possibly unmanaged pointer
			inline T& operator*() const
			{
				return *this->access();
			}

			//! Casts to a raw pointer, implicitly
			/* implicit */ inline operator T*() const
			{
				return &**this;
			}
	};

	/*!
	 * \brief Standard-compliant implementation of the Allocator concept,
	 *        based on VSPtr<T>.
	 *
	 * \tparam T element type
	 */
	template<typename T>
	class allocator
	{
		public:
			using value_type = T;

			using pointer = unsafe_ptr<T>;

			//! Constructs a new allocator
			inline allocator() noexcept = default;

			//! Copy-constructs an allocator
			template<typename U>
			inline allocator(const allocator<U>&) noexcept
			{}

			//! Allocates a contiguous memory region of the given array size
			unsafe_ptr<T> allocate(std::size_t count);

			//! Managed by the GC
			inline void deallocate(const unsafe_ptr<T>&, std::size_t) noexcept
			{}

			//! Compares two allocators for equality
			inline bool operator==(const allocator&) const noexcept
			{
				return true;
			}

			//! Compares two allocators for inequality
			inline bool operator!=(const allocator&) const noexcept
			{
				return false;
			}
	};
}

namespace std
{
	//! Required by the Standard
	template<typename T>
	struct iterator_traits<ce2103::mm::unsafe_ptr<T>>
	{
		using difference_type = std::ptrdiff_t;

		using value_type = std::remove_cv_t<T>;

		using pointer = ce2103::mm::unsafe_ptr<T>;

		using reference = T&;

		using iterator_category = std::random_access_iterator_tag;
	};
}

namespace ce2103::mm
{
	template<typename T>
	unsafe_ptr<T> allocator<T>::allocate(std::size_t count)
	{
		auto& owner = memory_manager::get_default(at::any);
		auto [id, resource, base] = owner.allocate_of<T>(count);

		return unsafe_ptr<T>{base, id, owner.get_locality()};
	}
}

#endif
