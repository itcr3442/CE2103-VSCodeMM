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

	template<typename T>
	class unsafe_ptr : public VSPtr<T>
	{
		friend class allocator<T>;

		using VSPtr<T>::VSPtr;

		public:
			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			static inline unsafe_ptr pointer_to(T& object) noexcept
			{
				return unsafe_ptr{&object, 0, at::any};
			}

			inline unsafe_ptr() noexcept
			: unsafe_ptr{nullptr, 0, at::any}
			{}

			/* implicit */ inline unsafe_ptr(T* data) noexcept
			: unsafe_ptr{data, 0, at::any}
			{}

			unsafe_ptr(const unsafe_ptr& other) = default;

			unsafe_ptr(unsafe_ptr&& other) = default;

			unsafe_ptr& operator=(const unsafe_ptr& other) = default;

			unsafe_ptr& operator=(unsafe_ptr&& other) = default;

			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			inline unsafe_ptr& operator++() noexcept
			{
				return *(++this->data, this);
			}

			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			inline unsafe_ptr operator++(int) noexcept
			{
				return this->template clone_with<unsafe_ptr>(this->data++);
			}

			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			inline unsafe_ptr& operator--() noexcept
			{
				return *(--this->data, this);
			}

			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			inline unsafe_ptr operator--(int) noexcept
			{
				return this->template clone_with<unsafe_ptr>(this->data--);
			}

			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			inline unsafe_ptr operator+(std::ptrdiff_t offset) const noexcept
			{
				return this->template clone_with<unsafe_ptr>(this->data + offset);
			}

			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			inline unsafe_ptr operator-(std::ptrdiff_t offset) const noexcept
			{
				return this->template clone_with<unsafe_ptr>(this->data - offset);
			}

			template<typename = std::enable_if_t<!std::is_void_v<T>>>
			inline std::ptrdiff_t operator-(const unsafe_ptr& other) const noexcept
			{
				return this->data - other.data;
			}

			/* implicit */ inline operator T*() const noexcept
			{
				return this->access();
			}
	};

	template<typename T>
	class allocator
	{
		public:
			using value_type = T;

			using pointer = unsafe_ptr<T>;

			inline allocator() noexcept = default;

			template<typename U>
			inline allocator(const allocator<U>&) noexcept
			{}

			unsafe_ptr<T> allocate(std::size_t count);

			inline void deallocate(const unsafe_ptr<T>&, std::size_t) noexcept
			{}

			inline bool operator==(const allocator&) const noexcept
			{
				return true;
			}

			inline bool operator!=(const allocator&) const noexcept
			{
				return false;
			}
	};
}

namespace std
{
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
