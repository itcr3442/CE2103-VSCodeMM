#ifndef CE2103_MM_SESSION_HPP
#define CE2103_MM_SESSION_HPP

#include <string>
#include <utility>
#include <variant>
#include <cstddef>
#include <optional>

#include "nlohmann/json.hpp"

#include "ce2103/network.hpp"

namespace ce2103::mm
{
	class session
	{
		public:
			inline bool is_lost() const noexcept
			{
				return !this->peer;
			}

		protected:
			static void serialize_octets(std::string& input);

			static bool deserialize_octets(std::string& input, std::size_t bytes) noexcept;

			explicit inline session(socket peer) noexcept
			: peer{std::move(peer)}
			{}

			void send(nlohmann::json data);

			std::optional<nlohmann::json> receive();

			inline void discard() noexcept
			{
				this->peer = std::nullopt;
			}

		private:
			std::optional<socket> peer;
	};
}

#endif
