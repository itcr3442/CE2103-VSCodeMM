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

	//! Pretty-formats a VSPtr<T> to a standard output stream
	template<typename T>
	std::ostream& operator<<(std::ostream& stream, const VSPtr<T>& pointer);
}

namespace ce2103::mm::_detail
{
	/*!
	 * \brief Functional base of all VSPtr<T> specializations.
	 *
	 * \tparam T       pointed-to type
	 * \tparam Derived Derived<T> inherits from ptr_base<T, Derived> (CRTP)
	 */
	template<typename T, template<class> class Derived>
	class ptr_base
	{
		// Friendship needed because of cloning and casting operations
		template<typename, template<class> class>
		friend class ptr_base;

		// Same reason as for 'friend class ptr_base'
		template<typename>
		friend class mm::VSPtr;

		public:
			//! Default from_cast() implementation; disallows all casts into this type
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

			//! Constructs a pointer initialized to nullptr.
			inline ptr_base() noexcept
			: storage{at::any}
			{}

			//! Constructs a new reference (if not nullptr) to the same object.
			inline ptr_base(const ptr_base& other) noexcept
			{
				this->initialize(other);
			}

			//! Move-constructs a reference, leaving 'other' as nullptr.
			inline ptr_base(ptr_base&& other) noexcept
			{
				this->initialize(std::move(other));
			}

			//! Constructs a pointer initialized to nullptr.
			/* implicit */ inline ptr_base(std::nullptr_t) noexcept
			: ptr_base{}
			{}

			//! Destructs this pointer, dropping a reference if not nullptr.
			inline ~ptr_base()
			{
				*this = nullptr;
			}

			/*!
			 * \brief Drops this reference if applicable, then creates a new
			 *        reference to the source object if not nullptr.
			 */
			inline ptr_base& operator=(const ptr_base& other) noexcept
			{
				return this->assign(other);
			}

			//! Drops this reference if applicable, then 
			inline ptr_base& operator=(ptr_base&& other) noexcept
			{
				return this->assign(std::move(other));
			}

			//! Drops this reference, if the pointer is not nullptr already.
			Derived<T>& operator=(std::nullptr_t) noexcept;

			//! Tests whether this is a null pointer.
			inline bool operator==(std::nullptr_t) const noexcept
			{
				return this->data == nullptr;
			}

			//! Compares two pointers of comparable raw pointer types for equality.
			template<typename U,
					 template<class> class OtherDerived,
					 typename = std::enable_if_t<std::is_convertible_v<U*, T*>
											  || std::is_convertible_v<T*, U*>>>
			inline bool operator==(const ptr_base<U, OtherDerived>& other) const noexcept
			{
				return this->data == other.data;
			}

			//! Tests whether this is not a null pointer.
			inline bool operator!=(std::nullptr_t) const noexcept
			{
				return this->data != nullptr;
			}

			//! Compares two pointers of comparable raw pointer types for inequality.
			template<typename U,
					 template<class> class OtherDerived,
					 typename = std::enable_if_t<std::is_convertible_v<U*, T*>
											  || std::is_convertible_v<T*, U*>>>
			inline bool operator!=(const ptr_base<U, OtherDerived>& other) const noexcept
			{
				return this->data != other.data;
			}

			//! Tests whether this is pointer is not nullptr.
			explicit inline operator bool() const noexcept
			{
				return this->data != nullptr;
			}

			/*!
			 * \brief Explicitly casts betwen pointer types.
			 *
			 * This allows type-safe and memory-safe downcasts and
			 * sidecasts in polymorphic type hierarchies. std::bad_cast
			 * will be thrown if the object isn't actually of the target type.
			 */
			template<typename U>
			explicit inline operator Derived<U>() const
			{
				return Derived<U>::from_cast(static_cast<const Derived<T>&>(*this));
			}

		protected:
			using element_type = T;

			/*!
			 * \brief Allocates and initializes memory, creating a
			 *        sole Derived<T> to the new object(s).
			 *
			 * \tparam U non-array, concrete element type; possibly distinct from T
			 *
			 * \param count        number of objects in the allocation
			 * \param always_array whether RTTI should consider this allocation's
			 *                     payload as an array even if count <= 1
			 * \param storage      memory locality
			 * \param arguments    arguments to each object's constructor
			 */
			template<typename U, typename... ArgumentTypes>
			static Derived<T> create
			(
				std::size_t count, bool always_array, at storage, ArgumentTypes&&... arguments
			);

			//! Same as the other overload, but implicity uses at::any as locality
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

			//! Underlying raw pointer
			T* data = nullptr;

			//! Allocation ID
			std::size_t id : sizeof(std::size_t) * CHAR_BIT - 2;

			/*!
			 * \brief Memory locality.
			 *
			 * at::any indicates no owner.
			 * Note: This triggers a compiler bug in GCC < 9.3.0.
			 */
			at storage : 2;

			//! Constructs a ptr_base by parts.
			inline ptr_base(T* data, std::size_t id, at storage) noexcept
			: data{data}, id{id}, storage{storage}
			{}

			//! Determines the associated manager from locality.
			inline memory_manager* get_owner() const
			{
				return this->storage != at::any
				     ? &memory_manager::get_default(this->storage) : nullptr;
			}

			/*!
			 * \brief Begins a dereference operation. The pointer
			 *        is checked for not being nullptr, and the
			 *        manager is informed of a mmeory access in
			 *        the near future.
			 *
			 * \param for_write whether the near operation is most likely a write
			 *
			 * \return this->data
			 */
			T* access(bool for_write = false) const;

			//! Generalization of the copy-constructor.
			template<typename U, template<class> class OtherDerived>
			Derived<T>& initialize(const ptr_base<U, OtherDerived>& other);

			//! Generalization of the move-constructor.
			template<typename U, template<class> class OtherDerived>
			Derived<T>& initialize(ptr_base<U, OtherDerived>&& other) noexcept;

			//! Generalization of the copy-assignment operator.
			template<typename U, template<class> class OtherDerived>
			Derived<T>& assign(const ptr_base<U, OtherDerived>& other);

			//! Generalization of the move-assignment operator.
			template<typename U, template<class> class OtherDerived>
			Derived<T>& assign(ptr_base<U, OtherDerived>&& other) noexcept;

			/*!
			 * \brief Clones this pointer as one of a potentially distinct
			 *        type and with a reference to a distinct (sub)object,
			 *        but still being a reference to the same allocation
			 *        (ID is preserved).
			 *
			 * This permits, for example, to obtain a VSPtr<T> by indeixng a VSPtr<T[]>.
			 */
			template<class PointerType>
			PointerType clone_with(typename PointerType::element_type* new_data) const;
	};
}

namespace ce2103::mm
{
	/*!
	 * \brief A smart, managed pointer for fundamental and composite types.
	 *
	 * \tparam T element type
	 */
	template<typename T>
	class VSPtr : public _detail::ptr_base<T, VSPtr>
	{
		using _detail::ptr_base<T, VSPtr>::ptr_base;

		friend std::ostream& mm::operator<< <T>(std::ostream&, const VSPtr<T>&);

		public:
			//! Proxy object for pointer dereference
			class dereferenced
			{
				friend class VSPtr<T>;

				public:
					dereferenced(const dereferenced& other) noexcept = delete;

					//! Move-constructs a dereference proxy
					dereferenced(dereferenced&& other) noexcept = default;

					dereferenced& operator=(const dereferenced& other) = delete;
					dereferenced& operator=(dereferenced&& other) = delete;

					/*!
					 * \brief Implements *ptr = value. Controls the processes
					 *        of ordered debug messages and allocation eviction.
					 */
					template<typename U, typename = std::enable_if_t<std::is_assignable_v<T&, U&&>>>
					const dereferenced&& operator=(U&& value) const &&;

					//! Preserves the identity '&*ptr == ptr'
					inline VSPtr<T> operator&() const &&
					{
						return this->pointer;
					}

					//! Implicit cast to raw reference, for transparency.
					/* implicit */ inline operator T&() const && noexcept
					{
						return *this->pointer.data;
					}

				private:
					const VSPtr<T>& pointer; //!< Dereferenced pointer

					//! Constructs a dereference proxy
					inline dereferenced(const VSPtr<T>& pointer)
					: pointer{pointer}
					{}
			};

			//! Creates a new pointer out of newly allocated memory. See ptr_base::create().
			template<typename... ArgumentTypes>
			static inline VSPtr New(ArgumentTypes&&... arguments)
			{
				return VSPtr::template create<T>
				(
					1, false, std::forward<ArgumentTypes>(arguments)...
				);
			}

			/*!
			 * \brief Implements type-safe dynamic casting from VSPtr&lt;U&gt;
			 *        to VSPtr&lt;T&gt;. Throws on invalid casts.
			 */
			template<typename U>
			static VSPtr from_cast(const VSPtr<U>& source);

			//! Copy-constructs a pointer
			VSPtr(const VSPtr& other) = default;

			//! Implicit casting through copy-construction (such as upcasting)
			template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
			inline VSPtr(const VSPtr<U>& other) noexcept
			{
				this->initialize(other);
			}

			//! Move-constructs a pointer
			VSPtr(VSPtr&& other) = default;

			//! Implicit casting through move-construction (such as upcasting)
			template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
			inline VSPtr(VSPtr<U>&& other) noexcept
			{
				this->initialize(std::move(other));
			}

			//! Copy-assigns a pointer
			VSPtr& operator=(const VSPtr& other) = default;

			//! Implicit casting through copy-assignment (such as upcasting)
			template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
			inline VSPtr& operator=(const VSPtr<U>& other) noexcept
			{
				return this->assign(other);
			}

			//! Move-assigns a pointer
			VSPtr& operator=(VSPtr&& other) = default;

			//! Implicit casting through move-assignment (such as upcasting)
			template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
			inline VSPtr& operator=(VSPtr<U>&& other) noexcept
			{
				return this->assign(std::move(other));
			}

			//! Equivalent to '*ptr = std::forward<U>(value)'
			template<typename U, typename = std::enable_if_t<std::is_assignable_v<T&, U&&>>>
			inline VSPtr& operator=(U&& value)
			{
				return *(**this = std::forward<U>(value), this);
			}

			//! Performs dereference-to-member
			inline T* operator->() const
			{
				return this->access();
			}

			//! Dereferences the pointer. The returned object is a proxy.
			inline dereferenced operator*() const
			{
				return dereferenced{(this->access(), *this)};
			}

			//! Dereferences the pointer
			inline dereferenced operator&() const
			{
				return **this;
			}

			/*!
			 * \brief Performs VSPtr-aware member-pointer dereference.
			 *        This allows to derive a VSPtr<F> from a VSPtr<C>,
			 *        where C is a class which as a field of type F.
			 */
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
	//! Specialization for arrays
	template<typename T>
	class VSPtr<T[]> : public _detail::ptr_base<T, _detail::array_ptr>
	{
		private:
			using base = _detail::ptr_base<T, _detail::array_ptr>;

		template<typename U>
		friend class VSPtr;

		using base::base;

		public:
			//! Allocates and initializes a new array. See ptr_base::create().
			template<typename... ArgumentTypes>
			static inline VSPtr New(std::size_t count, ArgumentTypes&&... arguments)
			{
				return VSPtr::template create<T>
				(
					count, true, std::forward<ArgumentTypes>(arguments)...
				).of_size(count);
			}

			//! Copy-constructs a pointer-to-array
			VSPtr(const VSPtr& other) = default;

			//! Move-constructs a pointer-to-array
			VSPtr(VSPtr&& other) = default;

			//! Copy-assigns a pointer-to-array
			VSPtr& operator=(const VSPtr& other) = default;

			//! Move-assigns a pointer-to-array
			VSPtr& operator=(VSPtr&& other) = default;

			//! Retrieves the slice size
			inline std::size_t get_size() const noexcept
			{
				return this->size;
			}

			//! Creates a zero-copy slice of this array.
			VSPtr slice(std::size_t start, std::size_t size) const;

			//! Indexes the array. Throws an exception if out-of-bounds.
			T& operator[](std::ptrdiff_t index) const;

			//! Returns an iterator to the start of the array.
			inline T* begin() const
			{
				return this->access();
			}

			//! Returns an iterator to one-past-the-end of the array.
			inline T* end() const
			{
				return this->access() + this->size;
			}

			//! Performs safe pointer arithmetic
			inline VSPtr<T> operator+(std::ptrdiff_t offset) const
			{
				return this->template clone_with<VSPtr<T>>(&(*this)[offset]);
			}

			//! Performs safe pointer arithmetic
			inline VSPtr<T> operator-(std::ptrdiff_t offset) const
			{
				return this->template clone_with<VSPtr<T>>(&(*this)[-offset]);
			}

			//! Performs safe pointer arithmetic
			std::ptrdiff_t operator-(const VSPtr& other) const;

			//! Casts to a pointer to array-of-const
			inline operator VSPtr<const T[]>() const noexcept
			{
				return this->template clone_with<VSPtr<const T[]>>(this->data).of_size(this->size);
			}

			//! Compares two pointers-to-array for equality
			inline bool operator==(const VSPtr<const T[]>& other) const noexcept
			{
				return this->base::operator==(other) && this->size == other.size;
			}

			//! Compares two pointers-to-array for inequality
			inline bool operator!=(const VSPtr<const T[]>& other) const noexcept
			{
				return this->base::operator!=(other) || this->size != other.size;
			}

		private:
			std::size_t size = 0; //!< Array size

			//! Changes this->size for rvalue *this
			inline VSPtr of_size(std::size_t size) && noexcept
			{
				this->size = size;
				return std::move(*this);
			}
	};
}

namespace ce2103::mm::_detail
{
	//! Invoked when a null VSPtr<T> is dereferenced
	[[noreturn]]
	void throw_null_dereference();

	//! Invoked upon an out-of-bounds access of a VSPtr<T[]>
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
		owner.evict(id);

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
