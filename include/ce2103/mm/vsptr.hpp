#ifndef CE2103_MM_VSPTR_HPP
#define CE2103_MM_VSPTR_HPP

#include <utility>
#include <cstddef>
#include <algorithm>
#include <functional>
#include <type_traits>

#include "ce2103/mm/gc.hpp"

namespace ce2103::mm
{
	namespace _detail
	{
		template<typename T, template<class> class Derived>
		class ptr_base
		{
			template<typename, template<class> class>
			friend class ptr_base;

			public:
				ptr_base() noexcept = default;

				inline ptr_base(const ptr_base& other) noexcept
				{
					this->initialize(other);
				}

				inline ptr_base(ptr_base&& other) noexcept
				{
					this->initialize(std::move(other));
				}

				/* implicit */ inline ptr_base(std::nullptr_t) noexcept
				: ptr_base{}
				{}

				inline ~ptr_base()
				{
					*this = nullptr;
				}

				inline ptr_base& operator=(const ptr_base& other) noexcept
				{
					return this->assign(other);
				}

				inline ptr_base& operator=(ptr_base&& other) noexcept
				{
					return this->assign(std::move(other));
				}

				Derived<T>& operator=(std::nullptr_t) noexcept;

				inline bool operator==(std::nullptr_t) const noexcept
				{
					return this->data == nullptr;
				}

				template<typename U,
				         template<class> class OtherDerived,
				         typename = std::enable_if_t<std::is_convertible_v<U*, T*>
				                                  || std::is_convertible_v<T*, U*>>>
				inline bool operator==(const ptr_base<U, OtherDerived>& other) const noexcept
				{
					return this->data == other.data;
				}

				inline bool operator!=(std::nullptr_t) const noexcept
				{
					return this->data != nullptr;
				}

				template<typename U,
				         template<class> class OtherDerived,
				         typename = std::enable_if_t<std::is_convertible_v<U*, T*>
				                                  || std::is_convertible_v<T*, U*>>>
				inline bool operator!=(const ptr_base<U, OtherDerived>& other) const noexcept
				{
					return this->data != other.data;
				}

				explicit inline operator bool() const noexcept
				{
					return this->data != nullptr;
				}

			protected:
				template<typename U, typename... ArgumentTypes>
				static Derived<T> create
				(
					std::size_t count, bool always_aray, ArgumentTypes&&... arguments
				);

				T*              data = nullptr;
				std::size_t     id;
				memory_manager* owner = nullptr;

				inline ptr_base(T* data, std::size_t id, memory_manager* owner) noexcept
				: data{data}, id{id}, owner{owner}
				{}

				T* access() const;

				template<typename U, template<class> class OtherDerived>
				Derived<T>& initialize(const ptr_base<U, OtherDerived>& other);

				template<typename U, template<class> class OtherDerived>
				Derived<T>& initialize(ptr_base<U, OtherDerived>&& other) noexcept;

				template<typename U, template<class> class OtherDerived>
				Derived<T>& assign(const ptr_base<U, OtherDerived>& other);

				template<typename U, template<class> class OtherDerived>
				Derived<T>& assign(ptr_base<U, OtherDerived>&& other) noexcept;

				template<class PointerType>
				PointerType clone_with(T* new_data) const;
		};
	}

	template<typename T>
	class VSPtr : public _detail::ptr_base<T, VSPtr>
	{
		using _detail::ptr_base<T, VSPtr>::ptr_base;

		public:
			template<typename... ArgumentTypes>
			static inline VSPtr New(ArgumentTypes&&... arguments)
			{
				return VSPtr::template create<T>
				(
					1, false, std::forward<ArgumentTypes>(arguments)...
				);
			}

			VSPtr(const VSPtr& other) = default;

			template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
			inline VSPtr(const VSPtr<U>& other) noexcept
			{
				this->initialize(other);
			}

			VSPtr(VSPtr&& other) = default;

			template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
			inline VSPtr(VSPtr<U>&& other) noexcept
			{
				this->initialize(std::move(other));
			}

			VSPtr& operator=(const VSPtr& other) = default;

			template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
			inline VSPtr& operator=(const VSPtr<U>& other) noexcept
			{
				return this->assign(other);
			}

			VSPtr& operator=(VSPtr&& other) = default;

			template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
			inline VSPtr& operator=(VSPtr<U>&& other) noexcept
			{
				return this->assign(std::move(other));
			}

			template<typename U, typename = std::enable_if_t<std::is_assignable_v<T&, U&&>>>
			inline VSPtr& operator=(U&& value)
			{
				return *(**this = std::forward<U>(value), this);
			}

			inline T* operator->() const
			{
				return this->access();
			}

			inline T& operator*() const
			{
				return *this->access();
			}

			inline T& operator&() const
			{
				return **this;
			}
	};

	namespace _detail
	{
		template<typename T>
		using array_ptr = VSPtr<T[]>;
	}

	template<typename T>
	class VSPtr<T[]> : public _detail::ptr_base<T, _detail::array_ptr>
	{
		private:
			using base = _detail::ptr_base<T, _detail::array_ptr>;

		template<typename U>
		friend class VSPtr;

		using base::base;

		public:
			template<typename... ArgumentTypes>
			static inline VSPtr New(std::size_t count, ArgumentTypes&&... arguments)
			{
				return VSPtr::template create<T>
				(
					count, true, std::forward<ArgumentTypes>(arguments)...
				).of_size(count);
			}

			VSPtr(const VSPtr& other) = default;

			VSPtr(VSPtr&& other) = default;

			VSPtr& operator=(const VSPtr& other) = default;

			VSPtr& operator=(VSPtr&& other) = default;

			inline std::size_t get_size() const noexcept
			{
				return this->size;
			}

			VSPtr slice(std::size_t start, std::size_t size) const;

			T& operator[](std::ptrdiff_t index) const;

			inline T* begin() const
			{
				return this->access();
			}

			inline T* end() const
			{
				return this->access() + this->size;
			}

			inline VSPtr<T> operator+(std::ptrdiff_t offset) const
			{
				return this->template clone_with<VSPtr<T>>(&(*this)[offset]);
			}

			inline VSPtr<T> operator-(std::ptrdiff_t offset) const
			{
				return this->template clone_with<VSPtr<T>>(&(*this)[-offset]);
			}

			std::ptrdiff_t operator-(const VSPtr& other) const;

			inline operator VSPtr<const T[]>() const noexcept
			{
				return this->template clone_with<VSPtr<const T[]>>(this->data).of_size(this->size);
			}

			inline bool operator==(const VSPtr<const T[]>& other) const noexcept
			{
				return this->base::operator==(other) && this->size == other.size;
			}

			inline bool operator!=(const VSPtr<const T[]>& other) const noexcept
			{
				return this->base::operator!=(other) || this->size != other.size;
			}

		private:
			std::size_t size = 0;

			inline VSPtr of_size(std::size_t size) && noexcept
			{
				this->size = size;
				return std::move(*this);
			}
	};

	namespace _detail
	{
		[[noreturn]]
		void throw_null_dereference();

		[[noreturn]]
		void throw_out_of_bounds();
	}

	template<typename T, template<class> class Derived>
	Derived<T>& _detail::ptr_base<T, Derived>::operator=(std::nullptr_t) noexcept
	{
		if(this->owner != nullptr)
		{
			this->owner->drop(this->id);
			this->owner = nullptr;
		}

		this->data = nullptr;
		return static_cast<Derived<T>&>(*this);
	}

	template<typename T, template<class> class Derived>
	template<typename U, typename... ArgumentTypes>
	Derived<T> _detail::ptr_base<T, Derived>::create
	(
		std::size_t count, bool always_array, ArgumentTypes&&... arguments
	)
	{
		auto& owner = memory_manager::get_default();
		auto [id, resource, data] = owner.allocate_of<U>(count, always_array);

		if constexpr(!std::is_trivially_constructible_v<U, ArgumentTypes...>)
		{
			for(U* element = data; element < data + count; ++element)
			{
				new(element) U(std::forward<ArgumentTypes>(arguments)...);
			}
		}

		if constexpr(!std::is_trivially_destructible_v<U>)
		{
			resource->set_initialized(count);
		}

		return Derived<T>{data, id, &owner};
	}

	template<typename T, template<class> class Derived>
	T* _detail::ptr_base<T, Derived>::access() const
	{
		if(*this == nullptr)
		{
			_detail::throw_null_dereference();
		} else if(this->owner != nullptr)
		{
			this->owner->probe(this->data);
		}

		return this->data;
	}

	template<typename T, template<class> class Derived>
	template<typename U, template<class> class OtherDerived>
	Derived<T>& _detail::ptr_base<T, Derived>::initialize
	(
		const ptr_base<U, OtherDerived>& other
	)
	{
		this->data = other.data;
		this->id = other.id;
		this->owner = other.owner;

		if(this->owner != nullptr)
		{
			this->owner->lift(this->id);
		}

		return static_cast<Derived<T>&>(*this);
	}

	template<typename T, template<class> class Derived>
	template<typename U, template<class> class OtherDerived>
	Derived<T>& _detail::ptr_base<T, Derived>::initialize
	(
		ptr_base<U, OtherDerived>&& other
	) noexcept
	{
		this->data = other.data;
		this->id = other.id;
		this->owner = other.owner;

		other.data = nullptr;
		other.owner = nullptr;

		return static_cast<Derived<T>&>(*this);
	}

	template<typename T, template<class> class Derived>
	template<typename U, template<class> class OtherDerived>
	Derived<T>& _detail::ptr_base<T, Derived>::assign
	(
		const ptr_base<U, OtherDerived>& other
	)
	{
		if(&other != this)
		{
			*this = nullptr;
			this->initialize(other);
		}

		return static_cast<Derived<T>&>(*this);
	}

	template<typename T, template<class> class Derived>
	template<typename U, template<class> class OtherDerived>
	Derived<T>& _detail::ptr_base<T, Derived>::assign
	(
		ptr_base<U, OtherDerived>&& other
	) noexcept
	{
		if(&other != this)
		{
			*this = nullptr;
			this->initialize(std::move(other));
		}

		return static_cast<Derived<T>&>(*this);
	}

	template<typename T, template<class> class Derived>
	template<class PointerType>
	PointerType _detail::ptr_base<T, Derived>::clone_with(T* new_data) const
	{
		if(this->owner != nullptr)
		{
			this->owner->lift(this->id);
		}

		return PointerType{new_data, this->id, this->owner};
	}

	template<typename T>
	VSPtr<T[]> VSPtr<T[]>::slice(std::size_t start, std::size_t size) const
	{
		if(*this == nullptr)
		{
			_detail::throw_null_dereference();
		}

		start = std::min(start, this->size);
		size = std::min(size, this->size - start);

		return this->template clone_with<VSPtr>(this->data + start).of_size(size);
	}

	template<typename T>
	T& VSPtr<T[]>::operator[](std::ptrdiff_t index) const
	{
		if(index < 0 || index >= static_cast<std::ptrdiff_t>(this->size))
		{
			_detail::throw_out_of_bounds();
		}

		return this->access()[index];
	}

	template<typename T>
	std::ptrdiff_t VSPtr<T[]>::operator-(const VSPtr& other) const
	{
		if(*this == nullptr || other == nullptr)
		{
			_detail::throw_null_dereference();
		} else if(this->id != other.id)
		{
			_detail::throw_out_of_bounds();
		}

		return this->data - other.data;
	}
}

#endif
