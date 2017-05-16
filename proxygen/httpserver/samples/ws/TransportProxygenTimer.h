#ifndef TRANSPORT_PROXYGEN_TIMER_H
#define TRANSPORT_PROXYGEN_TIMER_H

#include <folly/io/async/HHWheelTimer.h>
#include <websocketpp/http/constants.hpp>

namespace websocketpp
{
	namespace transport
	{
		namespace proxygen
		{

			class timer : public folly::HHWheelTimer::Callback
			{
			public:
				timer(websocketpp::transport::timer_handler handler) : handler(handler) {}

				void timeoutExpired() noexcept override {handler(websocketpp::lib::error_code{});}
				void callbackCanceled() noexcept override {}

				void cancel() noexcept {cancelTimeout();}

			private:
				websocketpp::transport::timer_handler handler;
			};

		}
	}
}

#endif

