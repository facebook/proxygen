#ifndef TRANSPORT_PROXYGEN_CONNECTION_H
#define TRANSPORT_PROXYGEN_CONNECTION_H

#include <folly/SocketAddress.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <websocketpp/transport/base/connection.hpp>

namespace websocketpp
{
	namespace transport
	{
		namespace proxygen
		{

			template <typename config> class endpoint;

			template <typename config>
			class connection_interface
			{
			public:
				using timer_type = typename config::timer_type;
				using timer_ptr = typename config::timer_ptr;

			public:
				virtual void init(websocketpp::transport::init_handler handler) = 0;
				virtual void async_read_at_least(size_t num_bytes, char *buf, size_t len, websocketpp::transport::read_handler handler) = 0;
				virtual void async_shutdown(websocketpp::transport::shutdown_handler handler) = 0;
				virtual void async_write(std::vector<websocketpp::transport::buffer> & bufs, websocketpp::transport::write_handler handler) = 0;
				virtual void dispatch(typename websocketpp::transport::dispatch_handler) = 0;
				virtual std::string get_remote_endpoint() = 0;
				virtual timer_ptr set_timer(long duration, websocketpp::transport::timer_handler handler) = 0;
			};

			template <typename config>
			class connection : public config::socket_type::socket_con_type
			{
			public:
				using type = connection<config>;
				using ptr = lib::shared_ptr<type>;

				using socket_con_type = typename config::socket_type::socket_con_type;
				using socket_con_ptr = typename socket_con_type::ptr;
				using alog_type = typename config::alog_type;
				using elog_type = typename config::elog_type;

				using request_type = typename config::request_type;
				using request_ptr = typename request_type::ptr;
				using response_type = typename config::response_type;
				using response_ptr = typename response_type::ptr;

				using timer_ptr = typename config::timer_ptr;

				friend class endpoint<config>;

			public:
				connection(bool&, alog_type&, elog_type&) {}

				void init(websocketpp::transport::init_handler handler)
				{
					impl->init(handler);
				}

				void async_read_at_least(size_t num_bytes, char *buf, size_t len, websocketpp::transport::read_handler handler)
				{
					impl->async_read_at_least(num_bytes, buf, len, handler);
				}

				void async_shutdown(websocketpp::transport::shutdown_handler handler)
				{
					impl->async_shutdown(handler);
				}

				void async_write(const char* buf, size_t len, websocketpp::transport::write_handler handler)
				{
					std::vector<websocketpp::transport::buffer> bufs{websocketpp::transport::buffer{buf, len}};
					impl->async_write(bufs, handler);
				}

				void async_write(std::vector<websocketpp::transport::buffer> & bufs, websocketpp::transport::write_handler handler)
				{
					impl->async_write(bufs, handler);
				}

				void dispatch(typename websocketpp::transport::dispatch_handler handler)
				{
					impl->dispatch(handler);
				}

				std::string get_remote_endpoint()
				{
					return impl->get_remote_endpoint();
				}

				ptr get_shared()
				{
					return websocketpp::lib::static_pointer_cast<type>(socket_con_type::get_shared());
				}

				void set_handle(websocketpp::connection_hdl hdl);

				timer_ptr set_timer(long duration, websocketpp::transport::timer_handler handler)
				{
					return impl->set_timer(duration, handler);
				}

				bool is_secure() const
				{
					return false;
				}

			public:
				connection_interface<config>* impl;
			};
		}
	}
}

#endif

