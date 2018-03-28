#ifndef TRANSPORT_PROXYGEN_RESPONSE_H
#define TRANSPORT_PROXYGEN_RESPONSE_H

#include <string>
#include <proxygen/lib/http/HTTPMessage.h>
#include <websocketpp/common/memory.hpp>
#include <websocketpp/http/constants.hpp>

namespace proxygen
{
	class HTTPMessage;
}

namespace websocketpp
{
	namespace transport
	{
		namespace proxygen
		{

			class response
			{
			public:
				using ptr = websocketpp::lib::shared_ptr<response>;

			public:
				explicit response() {}

				size_t consume(char const * buf, size_t len);
				const std::string& get_body() const;

				void remove_header(const std::string& key);
				void replace_header(const std::string& key, const std::string& val);
				void append_header(const std::string& key, const std::string& val);
				void set_status(websocketpp::http::status_code::value code);
				void set_status(websocketpp::http::status_code::value code, const std::string& msg);
				void set_version(std::string const & version);

				/**
				 * Utilisé seulement pour les websockets côté client
				 */
				const std::string& get_header(const std::string& key) const;
				bool headers_ready() const {return true;}

				bool get_header_as_plist(const std::string& key, websocketpp::http::parameter_list& out) const;

				websocketpp::http::status_code::value get_status_code() const;
				std::string raw() const;

			public:
				::proxygen::HTTPMessage m;
			};

		}
	}
}

#endif

