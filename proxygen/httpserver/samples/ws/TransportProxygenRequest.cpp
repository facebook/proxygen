#include "TransportProxygenRequest.h"

#include <proxygen/lib/http/HTTPMessage.h>
#include <websocketpp/http/parser.hpp>

namespace websocketpp
{
	namespace transport
	{
		namespace proxygen
		{

			request::request(::proxygen::HTTPMessage* m) : m(m)
			{}

			size_t
			request::consume(char const * buf, size_t len)
			{
				return len;
			}

			const std::string&
			request::get_method() const
			{
				return m->getMethodString();
			}

			const std::string&
			request::get_header(const std::string& key) const
			{
				return m->getHeaders().getSingleOrEmpty(key);
			}

			const std::string&
			request::get_uri() const
			{
				return m->getURL();
			}

			const std::string&
			request::get_version() const
			{
				httpVersion = std::string{"HTTP/"} + m->getVersionString();
				return httpVersion;
			}

			bool
			request::get_header_as_plist(const std::string& key, websocketpp::http::parameter_list& out) const
			{
				bool result{false};
				const auto& value = m->getHeaders().getSingleOrEmpty(key);
				if (value.length() > 0) {
					auto it = websocketpp::http::parser::extract_parameters(value.begin(), value.end(), out);
					result = it == value.begin();
				}
				return result;
			}

			void
			request::remove_header(const std::string& key)
			{
				m->getHeaders().remove(key);
			}

			void
			request::replace_header(const std::string& key, const std::string& val)
			{
				m->getHeaders().set(key, val);
			}

			void
			request::append_header(const std::string& key, const std::string& val)
			{
				m->getHeaders().add(key, val);
			}

			void
			request::set_method(const std::string& method)
			{
				m->setMethod(method);
			}

			void
			request::set_uri(const std::string& uri)
			{
				m->setURL(uri);
			}

			void
			request::set_version(const std::string& version)
			{
				m->setVersionString(version);
			}

		}
	}
}

