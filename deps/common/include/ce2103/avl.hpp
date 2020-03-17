#ifndef CE2103_AVL_HPP
#define CE2103_AVL_HPP

#include <memory>
#include <utility>
#include <cstddef>
#include <iterator>
#include <optional>
#include <algorithm>
#include <type_traits>

namespace ce2103
{
	/*!
	 * \brief Self-balancing AVL trees.
	 *
	 * \tparam K         key type
	 * \tparam V         value type
	 * \tparam Allocator dynamic storage allocator
	 */
	template<typename K, typename V,
	         class Allocator = std::allocator<std::pair<const K, V>>>
	class avl_tree
	{
		public:
			//! Immutable AVL tree iterator.
			class const_iterator
			{
				friend class avl_tree<K, V, Allocator>;

				public:
					//! Iterator requirements
					using difference_type = std::ptrdiff_t;

					//! Iterator requirements
					using value_type = const std::pair<const K, V>;

					//! Iterator requirements
					using pointer = typename std::allocator_traits<Allocator>::const_pointer;

					//! Iterator requirements
					using reference = value_type&;

					//! Iterator requirements
					using iterator_category = std::input_iterator_tag;

					//! Constructs an invalid iterator
					const_iterator() noexcept = default;

					//! Dereferences the iterator.
					inline const auto& operator*() const noexcept
					{
						return *this->current;
					}

					//! Dereferences the iterator for member access.
					inline const auto* operator->() const noexcept
					{
						return &**this;
					}

					//! Advances the iterator.
					const_iterator& operator++() noexcept;

					//! Advances the iterator, post-incrementally.
					const_iterator operator++(int) noexcept;

					//! Compares iterators for equality.
					inline bool operator==(const const_iterator& other) const noexcept
					{
						return this->tree == other.tree && this->current == other.current;
					}

					//! Compares iterators for equality.
					inline bool operator!=(const const_iterator& other) const noexcept
					{
						return this->tree != other.tree || this->current != other.current;
					}

				private:
					//!< Iteration container
					const avl_tree* tree = nullptr;

					//!< Currently iterated element
					value_type* current = nullptr;

					//! Constructs an iterator by member values.
					inline const_iterator(const avl_tree* tree, value_type* current) noexcept
					: tree{tree}, current{current}
					{}
			};

			//! Mutable AVL tree iterator
			class iterator : private const_iterator
			{
				friend class avl_tree<K, V, Allocator>;

				public:
					//! Iterator requirements
					using difference_type = std::ptrdiff_t;

					//! Iterator requirements
					using value_type = std::remove_const_t<typename const_iterator::value_type>;

					//! Iterator requirements
					using pointer = typename std::allocator_traits<Allocator>::pointer;

					//! Iterator requirements
					using reference = value_type&;

					//! Iterator requirements
					using iterator_category = std::input_iterator_tag;

					//! Constructs an invalid iterator
					iterator() noexcept = default;

					//! Dereferences the iterator.
					inline auto& operator*() const noexcept
					{
						return const_cast<value_type&>(this->const_iterator::operator*());
					}

					//! Dereferences the iterator for member access.
					inline auto* operator->() const noexcept
					{
						return const_cast<value_type&>(this->const_iterator::operator->());
					}

					//! Advances the iterator.
					inline iterator& operator++() noexcept
					{
						return static_cast<iterator&>(this->const_iterator::operator++());
					}

					//! Advances the iterator, post-incrementally.
					inline iterator operator++(int) noexcept
					{
						return iterator{this->const_iterator::operator++(42)};
					}

					//! Compares iterators for equality.
					inline bool operator==(const iterator& other) const noexcept
					{
						return this->const_iterator::operator==(other);
					}

					//! Compares iterators for equality.
					inline bool operator!=(const iterator& other) const noexcept
					{
						return this->const_iterator::operator!=(other);
					}

				private:
					//! Constructs an iterator from its fake const variant
					explicit inline iterator(const_iterator as_const)
					: const_iterator{std::move(as_const)}
					{}
			};

			//! Constructs an empty tree.
			avl_tree() noexcept = default;

			avl_tree(const avl_tree& other) = delete;

			//! Constructs a tree by moving from other tree.
			inline avl_tree(avl_tree&& other) noexcept
			: storage{std::move(other.storage)}, root{std::move(other.root)}
			{
				other.storage.size = 0;
				other.root = nullptr;
			}

			//! Destroys a tree.
			inline ~avl_tree() noexcept
			{
				this->clear();
			}

			avl_tree& operator=(const avl_tree& other) = delete;

			//! Replaces this tree with another one, by move.
			avl_tree& operator=(avl_tree&& other) noexcept;

			//! Returns a mutable iterator to the start of this tree.
			inline iterator begin() noexcept
			{
				return iterator{const_cast<const avl_tree*>(this)->begin()};
			}

			//! Returns an mutable iterator to the start of this tree.
			const_iterator begin() const noexcept;

			//! Returns a mutable iterator past-the-end of this tree.
			inline iterator end() noexcept
			{
				return iterator{const_cast<const avl_tree*>(this)->end()};
			}

			//! Returns a immutable iterator past-the-end of this tree.
			inline const_iterator end() const noexcept
			{
				return const_iterator{this, nullptr};
			}

			//! Retrieves the number of elements in the tree
			inline std::size_t get_size() const noexcept
			{
				return this->storage.size;
			}

			//! Empties the tree.
			void clear() noexcept;

			/*!
			 * \brief Inserts a key-value pair into the tree,
			 *        replacing any existing matching node.
			 *
			 * \return reference to the data of the inserted node
			 */
			V& insert(K key, V value);

			/*!
			 * \brief Removes a node by key.
			 *
			 * \return data of the removed node, if found
			 */
			std::optional<V> remove(const K& key) noexcept;

			/*!
			 * \brief Searches a node by key, mutably.
			 *
			 * \return address of the node's data, if found, otherwise a null pointer
			 */
			inline V* search(const K& key) noexcept
			{
				return const_cast<V*>(const_cast<const avl_tree*>(this)->search(key));
			}

			/*!
			 * \brief Searches a node by key, immutably.
			 *
			 * \return address of the node's data, if found, otherwise a null pointer
			 */
			const V* search(const K& key) const noexcept;

			/*!
			 * \brief Searches a node by key, creating it if not found.
			 *
			 * \return node's data
			 */
			V& operator[](const K& key);

			//! Returns the key of the tree root, if the tree is not empty.
			const K* get_root_key() const noexcept;

			/*!
			 * \brief Unlinks a node from another tree and inserts
			 *        it into this tree, without performing any
			 *        allocation or deallocation.
			 *
			 * \return address of the node value, if found and spliced
			 */
			V* splice_by_key(avl_tree& other, const K& key) noexcept;

		private:
			//! Binary tree node
			struct node
			{
				//! Allocator used to reserve dynamic node storage
				using allocator = typename std::allocator_traits<Allocator>
				                ::template rebind_alloc<node>;

				//! Allocator traits for tree nodes
				using allocator_traits = std::allocator_traits<allocator>;

				//! Fancy allocator-aware node*
				using pointer = typename allocator_traits::pointer;

				//! Key-value pair
				std::pair<const K, V> data;

				pointer left   = nullptr; //!< Left subtree
				pointer right  = nullptr; //!< Right subtree
				int     height = 1;       //!< Total subtree height 

				//! Determines the total height of a child subtree.
				static inline int height_of(pointer child) noexcept
				{
					return child != nullptr ? child->height : 0;
				}

				//! Determines the AVL balance factor (right-positive) of a subtree.
				static inline int balance_factor_of(pointer base) noexcept
				{
					return base != nullptr ? height_of(base->right) - height_of(base->left) : 0;
				}

				//! Constructs a new tree node.
				inline node(K key, V value)
				: data{std::move(key), std::move(value)}
				{}

				//! Retrieves the node's key.
				inline const K& key() const noexcept
				{
					return this->data.first;
				}

				//! Retrieves the node's value, mutably.
				inline V& value() noexcept
				{
					return this->data.second;
				}

				//! Retrieves the node's value, immutably.
				inline const V& value() const noexcept
				{
					return this->data.second;
				}

				/*!
				 * \brief Rebalances a subtree.
				 *
				 * \return reference to the new subtree root
				 */
				node& rebalance() noexcept;

				/*!
				 * \brief Rotates a subtree to the left, preserving 
				 *        the binary tree preconditions.
				 *
				 * \return reference to the new subtree root
				 */
				node& rotate_left() noexcept;

				/*!
				 * \brief Rotates a subtree to the right, preserving 
				 *        the binary tree preconditions.
				 *
				 * \return reference to the new subtree root
				 */
				node& rotate_right() noexcept;

				/*!
				 * \brief Performs a step in a tree walk.
				 *
				 * \return next element in the walk, if any
				 */
				std::pair<const K, V>* walk(const K& last_key) noexcept;
			};

			//! I'm not going to write typename everywhere
			using node_pointer = typename node::pointer;

			//! Empty-base optimization
			struct : node::allocator
			{
				std::size_t size = 0; //!< Number of elements in tree
			} storage;

			node_pointer root = nullptr; //!< Tree root

			/*!
			 * \brief Inserts a key-value pair into the tree,
			 *        replacing any existing matching node.
			 *        The optional parameter to_insert specifies
			 *        an already created node to insert.
			 *
			 * \return reference to the data of the inserted node
			 */
			V& insert(K* key, V* value, node_pointer to_insert);

			/*!
			 * \brief Unlinks a node by key.
			 *
			 * \return unlinked node, if found
			 */
			node_pointer unlink(const K& key) noexcept;

			//! Reserve a new node using the allocator
			node_pointer new_node(K key, V value);

			//! Delete a node using the allocator
			void delete_node(node_pointer node);
	};

	template<typename K, typename V, class Allocator>
	auto avl_tree<K, V, Allocator>::const_iterator::operator++() noexcept
		-> const_iterator&
	{
		this->current = this->tree->root->walk(this->current->first);
		return *this;
	}

	template<typename K, typename V, class Allocator>
	auto avl_tree<K, V, Allocator>::const_iterator::operator++(int) noexcept
		-> const_iterator
	{
		const_iterator copy = *this;
		++*this;

		return copy;
	}

	template<typename K, typename V, class Allocator>
	auto avl_tree<K, V, Allocator>::begin() const noexcept
		-> const_iterator
	{
		node_pointer least = this->root;
		if(least != nullptr)
		{
			while(least->left != nullptr)
			{
				least = least->left;
			}
		}

		return const_iterator{this, least != nullptr ? &least->data : nullptr};
	}

	template<typename K, typename V, class Allocator>
	auto avl_tree<K, V, Allocator>::operator=(avl_tree&& other) noexcept
		-> avl_tree&
	{
		this->clear();

		this->storage = std::move(other.storage);
		this->root = std::move(other.root);

		other.storage.size = 0;
		other.root = nullptr;

		return *this;
	}

	template<typename K, typename V, class Allocator>
	void avl_tree<K, V, Allocator>::clear() noexcept
	{
		auto do_clear = [this](auto& self, node_pointer current) noexcept -> void
		{
			if(current != nullptr)
			{
				node_pointer left = current->left;
				node_pointer right = current->right;

				this->delete_node(std::move(current));

				self(self, left);
				self(self, right);
			}
		};

		do_clear(do_clear, this->root);
		this->storage.size = 0;
	}

	template<typename K, typename V, class Allocator>
	V& avl_tree<K, V, Allocator>::insert(K key, V value)
	{
		return this->insert(&key, &value, nullptr);
	}

	template<typename K, typename V, class Allocator>
	std::optional<V> avl_tree<K, V, Allocator>::remove(const K& key) noexcept
	{
		node_pointer unlinked = this->unlink(key);
		if(unlinked == nullptr)
		{
			return std::nullopt;
		}

		V value{std::move(unlinked->value())};
		this->delete_node(std::move(unlinked));

		return value;
	}

	template<typename K, typename V, class Allocator>
	const V* avl_tree<K, V, Allocator>::search(const K& key) const noexcept
	{
		node_pointer current = this->root;
		while(current != nullptr)
		{
			if(key == current->key())
			{
				return &current->value();
			} else if(key < current->key())
			{
				current = current->left;
			} else
			{
				current = current->right;
			}
		}

		return nullptr;
	}

	template<typename K, typename V, class Allocator>
	V& avl_tree<K, V, Allocator>::operator[](const K& key)
	{
		if(auto* value = this->search(key); value != nullptr)
		{
			return *value;
		}

		return this->insert(key, V());
	}

	template<typename K, typename V, class Allocator>
	const K* avl_tree<K, V, Allocator>::get_root_key() const noexcept
	{
		return this->root != nullptr ? &this->root->key() : nullptr;
	}

	template<typename K, typename V, class Allocator>
	V* avl_tree<K, V, Allocator>::splice_by_key(avl_tree& other, const K& key) noexcept
	{
		node_pointer unlinked = other.unlink(key);
		if(unlinked == nullptr)
		{
			return nullptr;
		}

		return &this->insert(nullptr, nullptr, unlinked);
	}

	template<typename K, typename V, class Allocator>
	auto avl_tree<K, V, Allocator>::node::rebalance() noexcept
		-> node&
	{
		this->height = 1 + std::max(height_of(this->left), height_of(this->right));

		int factor = balance_factor_of(this);
		if(factor > 1)
		{
			if(balance_factor_of(this->right) < 0)
			{
				this->right = &this->right->rotate_right();
			}

			return this->rotate_left();
		} else if(factor < -1)
		{
			if(balance_factor_of(this->left) > 0)
			{
				this->left = &this->left->rotate_left();
			}
			
			return this->rotate_right();
		}

		return *this;
	}

	template<typename K, typename V, class Allocator>
	auto avl_tree<K, V, Allocator>::node::rotate_left() noexcept
		-> node&
	{
		node_pointer inner = this->right;

		this->right = inner->left;
		inner->left = this;

		return *inner;
	}

	template<typename K, typename V, class Allocator>
	auto avl_tree<K, V, Allocator>::node::rotate_right() noexcept
		-> node&
	{
		node_pointer inner = this->left;

		this->left = inner->right;
		inner->right = this;

		return *inner;
	}

	template<typename K, typename V, class Allocator>
	std::pair<const K, V>* avl_tree<K, V, Allocator>::node::walk(const K& last_key) noexcept
	{
		if(last_key < this->key())
		{
			auto* next = this->left != nullptr ? this->left->walk(last_key) : nullptr;
			return next ?: &this->data;
		} else
		{
			return this->right != nullptr ? this->right->walk(last_key) : nullptr;
		}
	}

	template<typename K, typename V, class Allocator>
	V& avl_tree<K, V, Allocator>::insert(K* key, V* value, node_pointer to_insert)
	{
		const auto* effective_key = to_insert != nullptr ? &to_insert->key() : key;

		auto do_insert = [&, this](auto& self, node_pointer current) mutable
			-> std::pair<node_pointer, V*>
		{
			if(current == nullptr)
			{
				node_pointer new_node = to_insert;
				if(new_node == nullptr)
				{
					new_node = this->new_node(std::move(*key), std::move(*value));
				} else
				{
					new_node->left = new_node->right = nullptr;
				}

				++this->storage.size;
				return std::make_pair(new_node, &new_node->value());
			} else if(*effective_key == current->key())
			{
				current->value() = std::move(*value);
				if(to_insert != nullptr)
				{
					this->delete_node(std::move(to_insert));
				}

				return std::make_pair(current, &current->value());
			}

			node_pointer* next_branch = *effective_key < current->key()
			                          ? std::addressof(current->left)
									  : std::addressof(current->right);

			auto [new_branch, created] = self(self, *next_branch);
			*next_branch = new_branch;

			return std::make_pair(&current->rebalance(), created);
		};

		auto [new_root, created] = do_insert(do_insert, this->root);

		this->root = new_root;
		return *created;
	}

	template<typename K, typename V, class Allocator>
	auto avl_tree<K, V, Allocator>::unlink(const K& key) noexcept
		-> node_pointer
	{
		auto do_unlink = [this](auto& self, node_pointer current, const K& key) mutable
			-> std::optional<std::pair<node_pointer, node_pointer>>
		{
			if(current == nullptr)
			{
				return std::nullopt;
			}

			node_pointer to_delete;
			node_pointer to_balance;

			if(key == current->key())
			{
				to_delete = current;

				if(current->left == nullptr || current->right == nullptr)
				{
					node_pointer successor = current->left == nullptr
					                       ? current->right : current->left;

					return std::make_pair(successor, to_delete);
				}

				node_pointer least = current->right;
				while(least->left != nullptr)
				{
					least = least->left;
				}

				to_balance = this->new_node(least->key(), std::move(least->value()));
				to_balance->left = current->left;
				to_balance->right = self(self, current->right, least->key())->first;

				this->delete_node(std::move(least));
			} else
			{
				to_balance = current;

				node_pointer* next_branch = key < current->key()
				                          ? std::addressof(current->left)
										  : std::addressof(current->right);

				if(auto result = self(self, *next_branch, key))
				{
					*next_branch = result->first;
					to_delete = result->second;
				} else
				{
					return std::nullopt;
				}
			}

			return std::make_pair(&to_balance->rebalance(), to_delete);
		};

		if(auto result = do_unlink(do_unlink, this->root, key))
		{
			this->root = result->first;
			--this->storage.size;

			return result->second;
		}

		return nullptr;
	}

	template<typename K, typename V, class Allocator>
	auto avl_tree<K, V, Allocator>::new_node(K key, V value)
		-> node_pointer
	{
		using traits = typename node::allocator_traits;

		auto pointer = traits::allocate(this->storage, 1);
		traits::construct(this->storage, &*pointer, std::move(key), std::move(value));

		return pointer;
	}

	template<typename K, typename V, class Allocator>
	void avl_tree<K, V, Allocator>::delete_node(node_pointer node)
	{
		using traits = typename node::allocator_traits;

		traits::destroy(this->storage, &*node);
		traits::deallocate(this->storage, std::move(node), 1);
	}
}

#endif
