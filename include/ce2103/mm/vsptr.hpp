#ifndef CE2103_MM_VSPTR_HPP
#define CE2103_MM_VSPTR_HPP

#include <ostream>
#include <utility>
#include <cstddef>
#include <climits>
#include <typeinfo>
#include <algorithm>
#include <functional>
#include <type_traits>

#include "ce2103/mm/gc.hpp"

namespace ce2103::mm
{
	template<typename T>
	class VSPtr;

	template<typename T>
	std::ostream& operator<<(std::ostream& stream, const VSPtr<T>& pointer);
}

namespace ce2103::mm::_detail
{
	template<typename T, template<class> class Derived>
	class ptr_base
	{
		template<typename, template<class> class>
		friend class ptr_base;

		template<typename>
		friend class mm::VSPtr;

		public:
			template<typename U>
			static inline Derived<T> from_cast(const Derived<U>& source)
			{
				static_assert
				(
					std::conditional<false, std::void_t<U>, std::false_type>::value,
					"All casts into this type are ill-formed"
				);

				return std::declval<Derived<T>>();
			}

			inline ptr_base() noexcept
			: storage{at::any}
			{}

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

			template<typename U>
			explicit inline operator Derived<U>() const
			{
				return Derived<U>::from_cast(static_cast<const Derived<T>&>(*this));
			}

		protected:
			using element_type = T;

			template<typename U, typename... ArgumentTypes>
			static Derived<T> create
			(
				std::size_t count, bool always_array, at storage, ArgumentTypes&&... arguments
			);

			template<typename U, typename... ArgumentTypes>
			static inline Derived<T> create
			(
				std::size_t count, bool always_array, ArgumentTypes&&... arguments
			)
			{
				return create<U>
				(
					count, always_array, at::any, std::forward<ArgumentTypes>(arguments)...
				);
			}

			T* data = nullptr;

			std::size_t id : sizeof(std::size_t) * CHAR_BIT - 2;

			/*!
			 * at::any indicates no owner.
			 * Note: This triggers a compiler bug in GCC < 9.3.0.
			 */
			at storage : 2;

			inline ptr_base(T* data, std::size_t id, at storage) noexcept
			: data{data}, id{id}, storage{storage}
			{}

			inline memory_manager* get_owner() const
			{
				return this->storage != at::any
				     ? &memory_manager::get_default(this->storage) : nullptr;
			}

			T* access(bool for_write = false) const;

			template<typename U, template<class> class OtherDerived>
			Derived<T>& initialize(const ptr_base<U, OtherDerived>& other);

			template<typename U, template<class> class OtherDerived>
			Derived<T>& initialize(ptr_base<U, OtherDerived>&& other) noexcept;

			template<typename U, template<class> class OtherDerived>
			Derived<T>& assign(const ptr_base<U, OtherDerived>& other);

			template<typename U, template<class> class OtherDerived>
			Derived<T>& assign(ptr_base<U, OtherDerived>&& other) noexcept;

			template<class PointerType>
			PointerType clone_with(typename PointerType::element_type* new_data) const;
	};
}

namespace ce2103::mm
{
	template<typename T>
	class VSPtr : public _detail::ptr_base<T, VSPtr>
	{
		using _detail::ptr_base<T, VSPtr>::ptr_base;

		friend std::ostream& mm::operator<< <T>(std::ostream&, const VSPtr<T>&);

		public:
			class dereferenced
			{
				friend class VSPtr<T>;

				public:
					dereferenced(const dereferenced& other) noexcept = delete;
					dereferenced(dereferenced&& other) noexcept = default;

					dereferenced& operator=(const dereferenced& other) = delete;
					dereferenced& operator=(dereferenced&& other) = delete;

					template<typename U, typename = std::enable_if_t<std::is_assignable_v<T&, U&&>>>
					const dereferenced&& operator=(U&& value) const &&;

					inline VSPtr<T> operator&() const &&
					{
						return this->pointer;
					}

					/* implicit */ inline operator T&() const && noexcept
					{
						return *this->pointer.data;
					}

				private:
					const VSPtr<T>& pointer;

					inline dereferenced(const VSPtr<T>& pointer)
					: pointer{pointer}
					{}
			};

			template<typename... ArgumentTypes>
			static inline VSPtr New(ArgumentTypes&&... arguments)
			{
				return VSPtr::template create<T>
				(
					1, false, std::forward<ArgumentTypes>(arguments)...
				);
			}

			template<typename U>
			static VSPtr from_cast(const VSPtr<U>& source);

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

			inline dereferenced operator*() const
			{
				return dereferenced{(this->access(), *this)};
			}

			inline dereferenced operator&() const
			{
				return **this;
			}

			template<typename FieldType, typename BoundT = std::enable_if_t
			<
				std::is_class_v<std::conditional<true, T, std::void_t<FieldType>>>, T
			>>
			VSPtr<FieldType> operator->*(FieldType BoundT::*member) const;
	};
}

namespace ce2103::mm::_detail
{
	template<typename T>
	using array_ptr = VSPtr<T[]>;
}

namespace ce2103::mm
{
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
}

namespace ce2103::mm::_detail
{
	template<typename ReturnType, typename... ParameterTypes>
	struct function_base
	{
		using type = ReturnType(ParameterTypes...);

		virtual ReturnType operator()(ParameterTypes... parameters) = 0;
	};

	template<typename TargetType, typename ReturnType, typename... ParameterTypes>
	struct function final : function_base<ReturnType, ParameterTypes...>
	{
		TargetType target;

		inline function(TargetType target)
		: target{std::move(target)}
		{}

		virtual inline ReturnType operator()(ParameterTypes... arguments) final override
		{
			return std::invoke(this->target, std::forward<ParameterTypes>(arguments)...);
		}
	};

	template<class FunctionType> 
	using function_ptr = VSPtr<typename FunctionType::type>;
}

namespace ce2103::mm
{
	template<typename ReturnType, typename... ParameterTypes>
	class VSPtr<ReturnType(ParameterTypes...)> : public _detail::ptr_base
	<
		_detail::function_base<ReturnType, ParameterTypes...>, _detail::function_ptr
	>
	{
		private:
			using base = _detail::ptr_base
			<
				_detail::function_base<ReturnType, ParameterTypes...>,
				_detail::function_ptr
			>;

		using base::base;

		public:
			template<typename TargetType>
			static inline VSPtr New(TargetType target)
			{
				return New(at::any, std::move(target));
			}

			template<typename TargetType, typename = std::enable_if_t<std::is_convertible_v
			<
				std::invoke_result_t<TargetType, ParameterTypes...>, ReturnType
			>>>
			static inline VSPtr New(at storage, TargetType target)
			{
				using type = _detail::function<TargetType, ReturnType, ParameterTypes...>;
				return VSPtr::template create<type>(1, false, storage, std::move(target));
			}

			VSPtr(const VSPtr& other) = default;

			VSPtr(VSPtr&& other) = default;

			VSPtr& operator=(const VSPtr& other) = default;

			VSPtr& operator=(VSPtr&& other) = default;

			inline ReturnType operator()(ParameterTypes... arguments)
			{
				return (*this->access())(std::forward<ParameterTypes>(arguments)...);
			}
	};
}

namespace ce2103::mm::_detail
{
	[[noreturn]]
	void throw_null_dereference();

	[[noreturn]]
	void throw_out_of_bounds();
}

namespace ce2103::mm
{
	template<typename T, template<class> class Derived>
	Derived<T>& _detail::ptr_base<T, Derived>::operator=(std::nullptr_t) noexcept
	{
		if(auto *owner = this->get_owner(); owner != nullptr)
		{
			owner->drop(this->id);
			this->storage = at::any;
		}

		this->data = nullptr;
		return static_cast<Derived<T>&>(*this);
	}

	template<typename T, template<class> class Derived>
	template<typename U, typename... ArgumentTypes>
	Derived<T> _detail::ptr_base<T, Derived>::create
	(
		std::size_t count, bool always_array, at storage, ArgumentTypes&&... arguments
	)
	{
		auto& owner = memory_manager::get_default(storage);
		if(storage == at::any)
		{
			storage = owner.get_locality();
		}

		auto [id, resource, data] = owner.allocate_of<U>(count, always_array);
		for(U* element = data; element < data + count; ++element)
		{
			new(element) U(std::forward<ArgumentTypes>(arguments)...);
		}

		resource->set_initialized(count);
		return Derived<T>{data, id, storage};
	}

	template<typename T, template<class> class Derived>
	T* _detail::ptr_base<T, Derived>::access(bool for_write) const
	{
		if(*this == nullptr)
		{
			_detail::throw_null_dereference();
		} else if(auto* owner = this->get_owner(); owner != nullptr)
		{
			owner->probe(this->data, for_write);
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
		this->storage = other.storage;

		if(auto* owner = this->get_owner(); owner != nullptr)
		{
			owner->lift(this->id);
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
		this->storage = other.storage;

		other.data = nullptr;
		other.storage = at::any;

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
	PointerType _detail::ptr_base<T, Derived>::clone_with
	(
		typename PointerType::element_type* new_data
	) const
	{
		if(auto* owner = this->get_owner(); owner != nullptr)
		{
			owner->lift(this->id);
		}

		return PointerType{new_data, this->id, this->storage};
	}

	template<typename T>
	std::ostream& operator<<(std::ostream& stream, const VSPtr<T>& pointer)
	{
		//TODO: Nice output
		return stream << pointer.data;
	}

	template<typename T>
	template<typename U, typename>
	const typename VSPtr<T>::dereferenced&& VSPtr<T>::dereferenced::operator=(U&& value) const &&
	{
		auto* owner = this->pointer.get_owner();
		if(owner != nullptr)
		{
			owner->probe(this->pointer.data, true);
		}

		*this->pointer.data = std::forward<U>(value);
		if(owner != nullptr)
		{
			owner->evict(this->pointer.id);
		}

		return std::move(*this);
	}

	template<typename T>
	template<typename U>
	VSPtr<T> VSPtr<T>::from_cast(const VSPtr<U>& source)
	{
		static_assert(std::is_polymorphic_v<U> && std::is_class_v<T>);
		return source.template clone_with<VSPtr<T>>(&dynamic_cast<T&>(*source.operator->()));
	}

	template<typename T>
	template<typename FieldType, typename BoundT>
	VSPtr<FieldType> VSPtr<T>::operator->*(FieldType BoundT::*member) const
	{
		static_assert(std::is_same_v<BoundT, T>);
		if(*this == nullptr || member == nullptr)
		{
			_detail::throw_null_dereference();
		}

		return this->template clone_with<VSPtr<FieldType>>(&(this->data->*member));
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
