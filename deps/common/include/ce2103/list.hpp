#ifndef CE2103_LIST_HPP
#define CE2103_LIST_HPP

#include <memory>
#include <cstddef>
#include <utility>
#include <optional>
#include <iterator>

namespace ce2103
{
	/*!
	 * \brief Singly-linked list.
	 *
	 * \tparam T         element type
	 * \tparam Allocator dynamic storage allocator
	 */
	template<typename T, class Allocator = std::allocator<T>>
	class linked_list
	{
		private:
			// Forward declaration used by 'class iterator' below
			struct node;

			//! Used to manage dynamic node storage
			using node_allocator = typename std::allocator_traits<Allocator>
			                     ::template rebind_alloc<node>;

			//! See above
			using node_allocator_traits = std::allocator_traits<node_allocator>;

			//! Fancy node*
			using node_pointer = typename node_allocator_traits::pointer;

		public:
			//! Immutable list iterator
			class const_iterator
			{
				friend class linked_list<T, Allocator>;

				public:
					//! Iterator requirements
					using difference_type = std::ptrdiff_t;

					//! Iterator requirements
					using value_type = const T;

					//! Iterator requirements
					using pointer = typename std::allocator_traits<Allocator>::const_pointer;

					//! Iterator requirements
					using reference = const T&;

					//! Iterator requirements
					using iterator_category = std::input_iterator_tag;

					//! Compares two iterators for equality
					inline bool operator==(const const_iterator& other) const noexcept
					{
						return this->current == other.current;
					}

					//! Compares two iterators for inequality
					inline bool operator!=(const const_iterator& other) const noexcept
					{
						return this->current != other.current;
					}

					//! Moves the iterator to the next element
					const_iterator& operator++() noexcept;

					//! Moves the iterator to the next element, returns previous state
					const_iterator operator++(int) noexcept;

					//! Accesses the element referred to by this iterator
					inline const T& operator*() const noexcept
					{
						return this->current->data;
					}

					//! Accesses a member of the referenced element.
					inline const T* operator->() const noexcept
					{
						return std::addressof(**this);
					}

				private:
					node_pointer current;

					//! Constructs an iterator given the initial node
					explicit inline const_iterator(node_pointer current) noexcept
					: current{std::move(current)}
					{}
			};

			//! Mutable list iterator
			class iterator : private const_iterator
			{
				friend class linked_list<T, Allocator>;

				using const_iterator::const_iterator;

				public:
					//! Iterator requirements
					using difference_type = std::ptrdiff_t;

					//! Iterator requirements
					using value_type = T;

					//! Iterator requirements
					using pointer = typename std::allocator_traits<Allocator>::pointer;

					//! Iterator requirements
					using reference = T&;

					//! Iterator requirements
					using iterator_category = std::input_iterator_tag;

					//! Compares two iterators for equality
					inline bool operator==(const iterator& other) const noexcept
					{
						return this->const_iterator::operator==(other);
					}

					//! Compares two iterators for inequality
					inline bool operator!=(const iterator& other) const noexcept
					{
						return this->const_iterator::operator!=(other);
					}

					//! Moves the iterator to the next element
					inline iterator& operator++() noexcept
					{
						return static_cast<iterator&>(this->const_iterator::operator++());
					}

					//! Moves the iterator to the next element, returns previous state
					inline iterator operator++(int) noexcept
					{
						return iterator{this->const_iterator::operator++(42)};
					}

					//! Accesses the element referred to by this iterator
					inline T& operator*() const noexcept
					{
						return const_cast<T&>(this->const_iterator::operator*());
					}

					//! Accesses a member of the referenced element.
					inline T* operator->() const noexcept
					{
						return const_cast<T*>(this->const_iterator::operator->());
					}

				private:
					//! Constructs an iterator out of its fake const variant
					explicit inline iterator(const_iterator as_const)
					: const_iterator{std::move(as_const)}
					{}
			};

			//! Constructs an empty list
			linked_list() noexcept = default;

			linked_list(const linked_list& other) = delete;

			//! Constructs a list by moving ownership out of other list
			linked_list(linked_list&& other) noexcept;

			//! Destroys the list, clearing it beforehand
			inline ~linked_list() noexcept
			{
				this->clear();
			}

			linked_list& operator=(const linked_list& other) = delete;

			//! Replaces the list by move
			linked_list& operator=(linked_list&& other) noexcept;

			//! Retrieves a mutable iterator to the beginning of the list
			inline iterator begin() noexcept
			{
				return iterator{const_cast<const linked_list*>(this)->begin()};
			}

			//! Retrieves an immutable iterator to the beginning of the list
			inline const_iterator begin() const noexcept
			{
				return const_iterator{this->head};
			}

			//! Retrieves a mutable iterator to one-past-the-end of the list
			inline iterator end() noexcept
			{
				return iterator{const_cast<const linked_list*>(this)->end()};
			}

			//! Retrieves an immutable iterator to one-past-the-end of the list
			inline const_iterator end() const noexcept
			{
				return const_iterator{nullptr};
			}

			//! Retrieves the current list size
			inline std::size_t get_size() const noexcept
			{
				return this->storage.size;
			}

			//! Clears the list
			void clear() noexcept;

			/*!
			 * \brief Allocates and appends a new node at the end of the list.
			 *
			 * \param data contents of the new node
			 *
			 * \return reference to the data of the new node
			 */
			T& append(T data);

			/*!
			 * \brief Allocates and appends a new node at the start of the list.
			 *
			 * \param data contents of the new node
			 *
			 * \return reference to the data of the new node
			 */
			T& prepend(T data);

			/*!
			 * \brief Removes a node given its data's address.
			 *
			 * \param to_delete reference of the data of the node to delete
			 *
			 * \return the data of the deleted node, if found
			 */
			std::optional<T> remove_by_address(T& to_delete) noexcept;

			/*!
			 * \brief Removes the first node matching a predicate.
			 *
			 * \tparam F predicate type
			 *
			 * \param predicate discriminator functor
			 *
			 * \return the data of the deleted node, if found
			 */
			template<typename F>
			std::optional<T> remove_by_predicate(F&& predicate) noexcept;

		private:
			//! List node
			struct node
			{
				T            data;
				node_pointer next;

				//! Constructs a list node given data and next node.
				inline node(T data, node_pointer next)
				: data{std::move(data)}, next{std::move(next)}
				{}
			};

			//! Empty-base optimization
			struct : node_allocator
			{
				std::size_t size = 0; //!< Count of nodes in the list
			} storage;

			node_pointer head = nullptr; //!< List head node
			node_pointer tail = nullptr; //!< List tail node

			/*!
			 * \brief Remove a list element given its node and predecessor.
			 *
			 * \param previous predecessor node
			 * \param current  node to delete
			 *
			 * \return the data of the deleted node, if the later is valid
			 */
			std::optional<T> do_remove(node_pointer previous, node_pointer current) noexcept;

			//! Reserves a new node using the allocator
			node_pointer new_node(T data, node_pointer next = nullptr);

			//! Deletes a node using the allocator
			void delete_node(node_pointer node);
	};

	template<typename T, typename Allocator>
	auto linked_list<T, Allocator>::const_iterator::operator++() noexcept
		-> const_iterator&
	{
		this->current = this->current->next;
		return *this;
	}

	template<typename T, typename Allocator>
	auto linked_list<T, Allocator>::const_iterator::operator++(int) noexcept
		-> const_iterator
	{
		const_iterator copy = *this;
		++*this;

		return copy;
	}

	template<typename T, typename Allocator>
	auto linked_list<T, Allocator>::operator=(linked_list&& other) noexcept
		-> linked_list&
	{
		this->clear();

		this->head = std::move(other.head);
		this->tail = std::move(other.tail);
		this->storage.size = other.size;

		other.head = other.tail = nullptr;
		other.size = 0;

		return *this;
	}

	template<typename T, typename Allocator>
	void linked_list<T, Allocator>::clear() noexcept
	{
		node_pointer current = this->head;
		while(current != nullptr)
		{
			node_pointer next = current->next;

			this->delete_node(std::move(current));
			current = next;
		}

		this->head = this->tail = nullptr;
		this->storage.size = 0;
	}

	template<typename T, typename Allocator>
	linked_list<T, Allocator>::linked_list(linked_list&& other) noexcept
	: head{std::move(other.head)}, tail{std::move(other.tail)},
	  storage{std::move(other.storage)}
	{
		other.head = other.tail = nullptr;
		other.storage.size = 0;
	}

	template<typename T, typename Allocator>
	T& linked_list<T, Allocator>::append(T data)
	{
		node_pointer new_tail = this->new_node(std::move(data));
		if(this->tail != nullptr)
		{
			this->tail->next = new_tail;
		} else
		{
			this->head = new_tail;
		}

		this->tail = new_tail;
		++this->storage.size;

		return new_tail->data;
	}

	template<typename T, typename Allocator>
	T& linked_list<T, Allocator>::prepend(T data)
	{
		this->head = this->new_node(std::move(data), this->head);
		if(this->tail == nullptr)
		{
			this->tail = this->head;
		}

		++this->storage.size;
		return this->head->data;
	}

	template<typename T, typename Allocator>
	std::optional<T> linked_list<T, Allocator>::remove_by_address(T& to_delete) noexcept
	{
		return this->remove_by_predicate([&to_delete](auto& node)
		{
			return &node == &to_delete;
		});
	}

	template<typename T, typename Allocator>
	template<typename F>
	std::optional<T> linked_list<T, Allocator>::remove_by_predicate(F&& predicate) noexcept
	{
		node_pointer previous = nullptr;
		node_pointer current = this->head;

		while(current != nullptr && !predicate(current->data))
		{
			previous = current;
			current = current->next;
		}

		return this->do_remove(previous, current);
	}

	template<typename T, typename Allocator>
	std::optional<T> linked_list<T, Allocator>::do_remove
	(
		node_pointer previous, node_pointer current
	) noexcept
	{
		if(current == nullptr)
		{
			return std::nullopt;
		}

		T data{std::move(current->data)};

		if(previous == nullptr)
		{
			this->head = current->next;
		} else
		{
			previous->next = current->next;
		}

		if(this->tail == current)
		{
			this->tail = current->next;
		}

		this->delete_node(std::move(current));
		--this->storage.size;

		return std::move(data);
	}

	template<typename T, typename Allocator>
	auto linked_list<T, Allocator>::new_node(T data, node_pointer next)
		-> node_pointer
	{
		node_pointer node = node_allocator_traits::allocate(this->storage, 1);
		node_allocator_traits::construct(this->storage, &*node, std::move(data), next);

		return node;
	}

	template<typename T, typename Allocator>
	void linked_list<T, Allocator>::delete_node(node_pointer node)
	{
		node_allocator_traits::destroy(this->storage, &*node);
		node_allocator_traits::deallocate(this->storage, std::move(node), 1);
	}
}

#endif
