#include "TransportProxygenResponse.h"

#include <websocketpp/http/parser.hpp>

namespace websocketpp
{
	namespace transport
	{
		namespace proxygen
		{

			size_t
			response::consume(char const * buf, size_t len)
			{
				return len;
			}

			const std::string&
			response::get_body() const
			{
				static std::string emptyBody = "";
				return emptyBody;
			}

			void
			response::remove_header(const std::string& key)
			{
				m.getHeaders().remove(key);
			}

			void
			response::replace_header(const std::string& key, const std::string& val)
			{
				m.getHeaders().set(key, val);
			}

			void
			response::append_header(const std::string& key, const std::string& val)
			{
				m.getHeaders().add(key, val);
			}

			const std::string&
			response::get_header(const std::string& key) const
			{
				return m.getHeaders().getSingleOrEmpty(key);
			}

			bool
			response::get_header_as_plist(const std::string& key, websocketpp::http::parameter_list& out) const
			{
				bool result{false};
				const auto& value = m.getHeaders().getSingleOrEmpty(key);
				if (value.length() > 0) {
					auto it = websocketpp::http::parser::extract_parameters(value.begin(), value.end(), out);
					result = it == value.begin();
				}
				return result;
			}

			websocketpp::http::status_code::value
			response::get_status_code() const
			{
				return (websocketpp::http::status_code::value)m.getStatusCode();
			}

			void
			response::set_status(websocketpp::http::status_code::value code)
			{
				m.setStatusCode((uint16_t)code);
				m.setStatusMessage(m.getDefaultReason((uint16_t)code));
			}

			void
			response::set_status(websocketpp::http::status_code::value code, const std::string& msg)
			{
				m.setStatusCode((uint16_t)code);
				m.setStatusMessage(msg);
			}

			void
			response::set_version(std::string const & version)
			{
				m.setHTTPVersion(1, 1);
			}

			std::string
			response::raw() const
			{
				return std::string{};
			}

		}
	}
}

