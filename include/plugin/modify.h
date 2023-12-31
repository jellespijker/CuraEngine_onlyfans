#ifndef PLUGIN_MODIFY_H
#define PLUGIN_MODIFY_H

#include "plugin/broadcast.h"
#include "plugin/metadata.h"
#include "plugin/settings.h"

#include <boost/asio/awaitable.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/tokenize.hpp>
#include <spdlog/spdlog.h>

#include <ctre.hpp>
#include <cura/plugins/slots/postprocess/v0/modify.grpc.pb.h>

#if __has_include(<coroutine>)

#include <coroutine>

#elif __has_include(<experimental/coroutine>)
#include <experimental/coroutine>
#define USE_EXPERIMENTAL_COROUTINE
#endif

#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>

namespace plugin::onlyfans
{

std::string filterLines(std::string_view layer)
{
    constexpr auto pattern = ctll::fixed_string{ "^(;|M106|M107|M123|M710).*$" };

    // Split the input string to lines
    const std::regex rx{ R"(.*\n?)" };
    auto lines = layer | ranges::views::tokenize(rx);

    // Filter the lines that match the pattern
    auto matching_lines = lines
                        | ranges::views::filter(
                              [&](const std::string& line)
                              {
                                  return ctre::match<pattern>(line);
                              });

    std::string result;
    for (const auto& matched_line : matching_lines)
    {
        result += matched_line;
    }
    return result;
}

template<class T, class Rsp, class Req>
struct Generate
{
    using service_t = std::shared_ptr<T>;
    service_t generate_service{ std::make_shared<T>() };
    Broadcast::shared_settings_t settings{ std::make_shared<Broadcast::settings_t>() };
    std::shared_ptr<Metadata> metadata{ std::make_shared<Metadata>() };

    boost::asio::awaitable<void> run()
    {
        while (true)
        {
            grpc::ServerContext server_context;

            cura::plugins::slots::postprocess::v0::modify::CallRequest request;
            grpc::ServerAsyncResponseWriter<Rsp> writer{ &server_context };
            co_await agrpc::request(&T::RequestCall, *generate_service, server_context, request, writer, boost::asio::use_awaitable);

            Rsp response;
            auto client_metadata = getUuid(server_context);
            const auto& layer = request.gcode_word();

            grpc::Status status = grpc::Status::OK;
            try
            {
                response.set_gcode_word(filterLines(layer));
            }
            catch (const std::exception& e)
            {
                spdlog::error("Error: {}", e.what());
                status = grpc::Status(grpc::StatusCode::INTERNAL, static_cast<std::string>(e.what()));
            }

            if (! status.ok())
            {
                co_await agrpc::finish_with_error(writer, status, boost::asio::use_awaitable);
                continue;
            }
            co_await agrpc::finish(writer, response, status, boost::asio::use_awaitable);
        }
    }
};

} // namespace plugin::onlyfans

#endif // PLUGIN_MODIFY_H
