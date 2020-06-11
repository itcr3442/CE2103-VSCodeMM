#ifndef CE2103_HASH_MAP_HPP
#define CE2103_HASH_MAP_HPP

#include <ratio>
#include <memory>
#include <utility>
#include <cstddef>
#include <iterator>
#include <optional>
#include <functional>
#include <type_traits>

#include "ce2103/avl.hpp"
#include "ce2103/hash.hpp"

namespace ce2103
{
	/*!
	 * \brief Key-value map based on a hash table.
	 *
	 * \tparam K         key type
	 * \tparam V         value type
	 * \tparam Hash      hash function
	 * \tparam Allocator dynamic storage allocator
	 */
	template<typename K, typename V, class Hash = standard_hash_adapter<murmur3>,
	         class Allocator = std::allocator<std::pair<const K, V>>>
	class hash_map
	{
		private:
			//! Per-bucket container
			using bucket_type = avl_tree<K, V, Allocator>;

		public:
			//! Immutable hash map iterator
			class const_iterator
			{
				friend class hash_map<K, V, Hash, Allocator>;

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

					//! Dereferences the iterator.
					inline const auto& operator*() const noexcept
					{
						return *this->bucket_iterator;
					}

					//! Dereferences the iterator by member access.
					inline const auto* operator->() const noexcept
					{
						return &**this;
					}

					//! Advances the iterator.
					const_iterator& operator++() noexcept;

					//! Advances the iterator, post-incrementally.
					const_iterator operator++(int) noexcept;

					//! Compares two iterators for equality.
					inline bool operator==(const const_iterator& other) const noexcept
					{
						return this->bucket_iterator == other.bucket_iterator;
					}

					//! Compares two iterators for inequality.
					inline bool operator!=(const const_iterator& other) const noexcept
					{
						return this->bucket_iterator != other.bucket_iterator;
					}

				private:
					const hash_map* map;    //!< Container to iterate
					std::size_t     bucket; //!< Current bucket

					//! Per-bucket iterator
					typename bucket_type::iterator bucket_iterator;

					//! Constructs an iterator by member values.
					inline const_iterator
					(
						const hash_map* map, std::size_t bucket,
						typename bucket_type::iterator bucket_iterator
					)
					: map{map}, bucket{bucket}, bucket_iterator{bucket_iterator}
					{}

					//! Performs a step of iterator increment.
					void advance() noexcept;
			};

			//! Hash map iterator.
			class iterator : private const_iterator
			{
				friend class hash_map<K, V, Hash, Allocator>;

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

					//! Dereferences the iterator.
					inline auto& operator*() const noexcept
					{
						return const_cast<value_type&>(this->const_iterator::operator*());
					}

					//! Dereferences the iterator by member access.
					inline auto* operator->() const noexcept
					{
						return const_cast<value_type*>(this->const_iterator::operator*());
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

					//! Compares two iterators for equality.
					inline bool operator==(const iterator& other) const noexcept
					{
						return this->const_iterator::operator==(other);
					}

					//! Compares two iterators for inequality.
					inline bool operator!=(const iterator& other) const noexcept
					{
						return this->const_iterator::operator!=(other);
					}

				private:
					//! Constructs an iterator from its fake const variant
					inline iterator(const_iterator as_const)
					: const_iterator{std::move(as_const)}
					{}
			};

			//! Constructs an empty hash map.
			hash_map() = default;

			hash_map(const hash_map& other) = delete;

			//! Move-constructs a map.
			hash_map(hash_map&& other) noexcept;

			//! Destroys the map and all its elements.
			inline ~hash_map() noexcept
			{
				this->clear();
			}

			//! Moves a map to another, clearing the first.
			hash_map& operator=(hash_map&& other);

			// Returns a mutable iterator to the start of the map's (unordered) elements.
			inline iterator begin() noexcept
			{
				return iterator{const_cast<const hash_map*>(this)->begin()};
			}

			// Returns an immutable iterator to the start of the map's (unordered) elements.
			const_iterator begin() const noexcept;

			//! Returns a mmutable iterator to the end of the map.
			inline iterator end() noexcept
			{
				return iterator{const_cast<const hash_map*>(this)->end()};
			}

			//! Returns an immutable iterator to the end of the map.
			const_iterator end() const noexcept;

			//! Empties the map.
			void clear() noexcept;

			/*!
			 * \brief Inserts the given key-value pair, replacing
			 *        the value if the key is already inserted.
			 *
			 * \return reference to the inserted value
			 */
			V& insert(K key, V value);

			/*!
			 * \brief Removes an element by key.
			 *
			 * \return value object of the returned element, if found
			 */
			std::optional<V> remove(const K& key) noexcept;

			/*!
			 * \brief Searches by key, mutably.
			 *
			 * \return address of the value object, if found
			 */
			inline V* search(const K& key) noexcept
			{
				return const_cast<V*>(const_cast<const hash_map*>(this)->search(key));
			}

			/*!
			 * \brief Searches by key, immutably.
			 *
			 * \return address of the value object, if found
			 */
			const V* search(const K& key) const noexcept;

			/*!
			 * \brief Searches by key, creating the element if not found.
			 *
			 * \return value object for the given key
			 */
			V& operator[](const K& key);

			//! Retrieves the number of elements in the map.
			inline std::size_t get_size() const noexcept
			{
				return this->storage.usage;
			}

		private:
			//! Used to determine when to rehash.
			using load_limit = std::ratio<7, 8>;

			//! Real allocator
			using bucket_allocator = typename std::allocator_traits<Allocator>
			                      :: template rebind_alloc<bucket_type>;

			//! Real allocator traits
			using bucket_allocator_traits = std::allocator_traits<bucket_allocator>;

			//! Fancy bucket_type*
			using bucket_pointer = typename bucket_allocator_traits::pointer;

			//! Initial table size order, used once a first element is inserted.
			static constexpr std::size_t INITIAL_ORDER = 4;

			//! Computes the hash index for the given key with the given order.
			static std::size_t index_for(const K& key, std::size_t order) noexcept;

			//! Empty-base optimization
			struct : bucket_allocator
			{
				std::size_t usage = 0; //!< Current number of elements in the map
			} storage;

			std::size_t    order = 0;       //!< Power-of-two order of the table size
			bucket_pointer table = nullptr; //!< Hash table

			//! Recreates the hash table with a new order constraint.
			void rehash(std::size_t new_order);

			//! Deletes the current table using the allocator
			void delete_table();
	};

	template<typename K, typename V, class Hash, class Allocator>
	auto hash_map<K, V, Hash, Allocator>::const_iterator::operator++() noexcept
		-> const_iterator&
	{
		++this->bucket_iterator;
		this->advance();

		return *this;
	}

	template<typename K, typename V, class Hash, class Allocator>
	auto hash_map<K, V, Hash, Allocator>::const_iterator::operator++(int) noexcept
		-> const_iterator
	{
		const_iterator copy = *this;
		++*this;

		return copy;
	}

	template<typename K, typename V, class Hash, class Allocator>
	void hash_map<K, V, Hash, Allocator>::const_iterator::advance() noexcept
	{
		while(this->bucket_iterator == this->map->table[this->bucket].end()
		   && this->bucket != (sizeof(char) << this->map->order) - 1)
		{
			this->bucket_iterator = this->map->table[++this->bucket].begin();
		}
	}

	template<typename K, typename V, class Hash, class Allocator>
	hash_map<K, V, Hash, Allocator>::hash_map(hash_map&& other) noexcept
	: storage{std::move(other.storage)}, order{other.order}, table{other.table}
	{
		other.storage.usage = other.order = 0;
		other.table = nullptr;
	}

	template<typename K, typename V, class Hash, class Allocator>
	auto hash_map<K, V, Hash, Allocator>::operator=(hash_map&& other)
		-> hash_map&
	{
		this->clear();

		this->storage = other.storage;
		this->order = other.order;
		this->table = std::move(other.table);

		other.storage.usage = other.order = 0;
		other.table = nullptr;

		return *this;
	}

	template<typename K, typename V, class Hash, class Allocator>
	auto hash_map<K, V, Hash, Allocator>::begin() const noexcept
		-> const_iterator
	{
		if(this->table == nullptr)
		{
			return const_iterator{this, 0, {}};
		}

		const_iterator begin_iterator{this, 0, this->table[0].begin()};
		begin_iterator.advance();

		return begin_iterator;
	}

	template<typename K, typename V, class Hash, class Allocator>
	auto hash_map<K, V, Hash, Allocator>::end() const noexcept
		-> const_iterator
	{
		if(this->table == nullptr)
		{
			return const_iterator{this, 0, {}};
		}

		auto last = (sizeof(char) << this->order) - 1;
		return const_iterator{this, last, this->table[last].end()};
	}

	template<typename K, typename V, class Hash, class Allocator>
	void hash_map<K, V, Hash, Allocator>::clear() noexcept
	{
		if(this->table != nullptr)
		{
			this->delete_table();
			this->storage.usage = this->order = 0;
			this->table = nullptr;
		}
	}

	template<typename K, typename V, class Hash, class Allocator>
	V& hash_map<K, V, Hash, Allocator>::insert(K key, V value)
	{
		bool exceeded_ratio = this->storage.usage * static_cast<std::size_t>(load_limit::den)
		                    > static_cast<std::size_t>(load_limit::num) << this->order;

		if(this->order == 0 || exceeded_ratio)
		{
			this->rehash(this->order > 0 ? this->order + 1 : INITIAL_ORDER);
		}

		V& inserted = this->table[index_for(key, this->order)].insert
		(
			std::move(key), std::move(value)
		);

		++this->storage.usage;
		return inserted;
	}

	template<typename K, typename V, class Hash, class Allocator>
	std::optional<V> hash_map<K, V, Hash, Allocator>::remove(const K& key) noexcept
	{
		if(this->table == nullptr)
		{
			return std::nullopt;
		}

		auto removed = this->table[index_for(key, this->order)].remove(key);
		if(removed)
		{
			--this->storage.usage;
		}

		return removed;
	}

	template<typename K, typename V, class Hash, class Allocator>
	const V* hash_map<K, V, Hash, Allocator>::search(const K& key) const noexcept
	{
		if(this->table == nullptr)
		{
			return nullptr;
		}

		return this->table[index_for(key, this->order)].search(key);
	}

	template<typename K, typename V, class Hash, class Allocator>
	V& hash_map<K, V, Hash, Allocator>::operator[](const K& key)
	{
		if(this->table == nullptr)
		{
			this->rehash(INITIAL_ORDER);
		}

		return this->table[index_for(key, this->order)][key];
	}

	template<typename K, typename V, class Hash, class Allocator>
	std::size_t hash_map<K, V, Hash, Allocator>::index_for
	(
		const K& key, std::size_t order
	) noexcept
	{
		return Hash{}(key) & ((sizeof(char) << order) - 1);
	}

	template<typename K, typename V, class Hash, class Allocator>
	void hash_map<K, V, Hash, Allocator>::rehash(std::size_t new_order)
	{
		auto new_size = sizeof(char) << new_order;

		auto new_table = bucket_allocator_traits::allocate(this->storage, new_size);
		for(std::size_t i = 0; i < new_size; ++i)
		{
			bucket_allocator_traits::construct(this->storage, &new_table[i]);
		}

		if(this->table != nullptr)
		{
			for(std::size_t i = 0; i < sizeof(char) << this->order; ++i)
			{
				auto& old_bucket = this->table[i];
				while(const auto* root_key = old_bucket.get_root_key())
				{
					new_table[index_for(*root_key, new_order)].splice_by_key
					(
						old_bucket, *root_key
					);
				}
			}

			this->delete_table();
		}

		this->table = new_table;
		this->order = new_order;
	}

	template<typename K, typename V, class Hash, class Allocator>
	void hash_map<K, V, Hash, Allocator>::delete_table()
	{
		auto size = sizeof(char) << this->order;
		for(std::size_t i = 0; i < size; ++i)
		{
			bucket_allocator_traits::destroy(this->storage, &this->table[size - i - 1]);
		}

		bucket_allocator_traits::deallocate(this->storage, std::move(this->table), size);
	}
}

#endif
