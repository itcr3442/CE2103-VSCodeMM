#ifndef CE2103_MM_SESSION_HPP
#define CE2103_MM_SESSION_HPP

#include <string>
#include <utility>
#include <variant>
#include <cstddef>
#include <optional>
#include <string_view>

#include "nlohmann/json.hpp"

#include "ce2103/network.hpp"

namespace ce2103::mm
{
	/*!
	 * \brief Represents an active TCP session which communicates
	 *        one JSON value per line.
	 */
	class session
	{
		public:
			//! Whether the session has been closed.
			inline bool is_lost() const noexcept
			{
				return !this->peer;
			}

		protected:
			//! Produces a compact JSON representation of an octet stream.
			static nlohmann::json serialize_octets(std::string_view input);

			/*!
			 * \brief For a valid input produced by serialize_octets(), determines
			 *        the total space required for deserialization.
			 */
			static std::optional<std::size_t> deserialized_size
			(
				const nlohmann::json& input
			) noexcept;

			/*!
			 * \brief Deserializes a representation produced by serialize_octets().
			 *
			 * \param input  representation to deserialize
			 * \param output output buffer
			 * \param bytes  expected bytes to deserialize; fails if this differs
			 *
			 * \return whether the operation was successful
			 */
			static bool deserialize_octets
			(
				const nlohmann::json& input, char* output, std::size_t bytes
			) noexcept;

			//! Constructs a session from a given socket.
			explicit inline session(socket peer) noexcept
			: peer{std::move(peer)}
			{}

			//! Serializes a JSON value and sends it as a single line.
			void send(nlohmann::json data);

			//! Attempts to read a single line and deserialize it as JSON.
			std::optional<nlohmann::json> receive();

			//! Forces immediate session termination.
			inline void discard() noexcept
			{
				this->peer = std::nullopt;
			}

		private:
			std::optional<socket> peer; //!< The session's socket
	};
}

#endif
