#ifndef TRANSPORT_PROXYGEN_REQUEST_H
#define TRANSPORT_PROXYGEN_REQUEST_H

#include <string>
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

			class request
			{
			public:
				using ptr = websocketpp::lib::shared_ptr<request>;

			public:
				explicit request() {}
				explicit request(::proxygen::HTTPMessage* m);

				size_t consume(char const * buf, size_t len);

				const std::string& get_method() const;
				const std::string& get_header(const std::string& key) const;
				const std::string& get_uri() const;
				const std::string& get_version() const;

				bool get_header_as_plist(const std::string& key, websocketpp::http::parameter_list& out) const;

				std::string raw() const {return "";}

				bool ready() const {return true;}

				void remove_header(const std::string& key);
				void replace_header(const std::string& key, const std::string& val);

				/**
				 * Utilisé seulement pour les websockets côté client
				 */
				void append_header(const std::string& key, const std::string& val);

				void set_method(const std::string& method);
				void set_uri(const std::string& uri);
				void set_version(const std::string& version);

			public:
				::proxygen::HTTPMessage* m{nullptr};

			private:
				mutable std::string httpVersion;
			};

		}
	}
}

#endif

