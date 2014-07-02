/*
 * Copyright (C) 2014 Canonical, Ltd.
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 3 of the GNU Lesser General Public License as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Pete Woods <pete.woods@canonical.com>
 */

#include <youtube/api/channel.h>
#include <youtube/api/client.h>
#include <youtube/api/playlist.h>

#include <core/net/error.h>
#include <core/net/http/content_type.h>
#include <core/net/http/response.h>
#include <json/json.h>

#include <iostream>
#include <unordered_map>

namespace http = core::net::http;
namespace json = Json;
namespace net = core::net;

using namespace youtube::api;
using namespace std;

namespace {
static unordered_map<string, function<Resource::Ptr(const json::Value &)>> TYPES =
        { { "youtube#videoCategory", [](const json::Value &value) {
            return make_shared<VideoCategory>(value);
        } }, { "youtube#video", [](const json::Value &value) {
            return make_shared<Video>(value);
        } }, { "youtube#channel", [](const json::Value &value) {
            return make_shared<Channel>(value);
        } }, { "youtube#playlist", [](const json::Value &value) {
            return make_shared<Playlist>(value);
        } } };

template<typename T>
static deque<shared_ptr<T>> get_typed_list(const string &filter,
        const json::Value &root) {
    deque<shared_ptr<T>> results;
    json::Value data = root["items"];
    for (json::ArrayIndex index = 0; index < data.size(); ++index) {
        json::Value item = data[index];

        string kind = item["kind"].asString();
        if (kind == "youtube#searchResult") {
            kind = item["id"]["kind"].asString();
        }

        if (kind == filter) {
            results.emplace_back(make_shared<T>(item));
        }
    }
    return results;
}

static Client::ResourceList get_list(const json::Value &root) {
    Client::ResourceList results;
    json::Value data = root["items"];
    for (json::ArrayIndex index = 0; index < data.size(); ++index) {
        json::Value item = data[index];
        string kind = item["kind"].asString();
        if (kind == "youtube#searchResult") {
            kind = item["id"]["kind"].asString();
        }
        const auto f = TYPES.find(kind);
        if (f == TYPES.cend()) {
            cerr << "Couldn't create type: " << kind << endl;
            cerr << item.toStyledString() << endl;
            cerr << "------------------" << endl;
        } else {
            results.emplace_back(f->second(item));
        }
    }
    return results;
}

static string make_uri(const string host, const deque<string> &endpoints,
        const vector<pair<string, string>> &querys,
        shared_ptr<http::Client> client) {
    string uri = host;
    for (const string &endpoint : endpoints) {
        uri.append("/" + endpoint);
    }
    bool first = true;
    for (auto it : querys) {
        if (first) {
            uri.append("?");
            first = false;
        } else {
            uri.append("&");
        }
        uri.append(client->url_escape(it.first));
        uri.append("=");
        uri.append(client->url_escape(it.second));
    }
    return uri;
}

}

class Client::Priv {
public:
    Priv(Config::Ptr config, int cardinality, const string& locale) :
            client_(http::make_client()), worker_ { [this]() {client_->run();} }, config_(
                    config), cardinality_(cardinality), locale_(locale), cancelled_(
            false) {
    }

    ~Priv() {
        client_->stop();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    std::shared_ptr<core::net::http::Client> client_;

    std::thread worker_;

    Config::Ptr config_;

    int cardinality_;

    std::string locale_;

    std::atomic<bool> cancelled_;

    void get(const deque<string> &path,
            const vector<pair<string, string>> &parameters,
            http::Request::Handler &handler) {
        cancelled_ = false;

        http::Request::Configuration configuration;
        vector<pair<string, string>> complete_parameters(parameters);
        if (config_->authenticated) {
            configuration.header.add("Authorization",
                    "Bearer " + config_->access_token);
        } else {
            complete_parameters.emplace_back("key", config_->api_key);
        }
        configuration.uri = make_uri(config_->apiroot, path,
                complete_parameters, client_);
        configuration.header.add("Accept", config_->accept);
        configuration.header.add("User-Agent", config_->user_agent);

        auto request = client_->head(configuration);
        request->async_execute(handler);
    }

    http::Request::Progress::Next progress_report(
            const http::Request::Progress&) {
        return cancelled_ ?
                http::Request::Progress::Next::abort_operation :
                http::Request::Progress::Next::continue_operation;
    }

    template<typename T>
    future<T> async_get(const deque<string> &path,
            const vector<pair<string, string>> &parameters,
            const function<T(const json::Value &root)> &func) {
        auto prom = make_shared<promise<T>>();

        http::Request::Handler handler;
        handler.on_progress(
                bind(&Client::Priv::progress_report, this, placeholders::_1));
        handler.on_error([prom](const net::Error& e)
        {
            prom->set_exception(make_exception_ptr(e));
        });
        handler.on_response(
                [prom,func](const http::Response& response)
                {
                    json::Value root;
                    json::Reader reader;
                    reader.parse(response.body, root);

                    if (response.status != http::Status::ok) {
                        prom->set_exception(make_exception_ptr(domain_error(root["error"].asString())));
                    } else {
                        prom->set_value(func(root));
                    }
                });

        get(path, parameters, handler);

        return prom->get_future();
    }
};

Client::Client(Config::Ptr config, int cardinality, const string& locale) :
        p(new Priv(config, cardinality, locale)) {
}

future<Client::ResourceList> Client::search(const string &query) {
    return p->async_get<ResourceList>( { "youtube", "v3", "search" }, { {
            "part", "snippet" }, { "maxResults", "10" }, { "q", query } },
            [](const json::Value &root) {
                return get_list(root);
            });
}

future<Client::VideoCategoryList> Client::video_categories() {
    // FIXME Get the real country code
    string country_code = "US";
    return p->async_get<VideoCategoryList>(
            { "youtube", "v3", "videoCategories" }, { { "part", "snippet" }, {
                    "regionCode", country_code }, { "h1", p->locale_ } },
            [](const json::Value &root) {
                return get_typed_list<VideoCategory>("youtube#videoCategory", root);
            });
}

future<Client::GuideCategoryList> Client::guide_categories() {
    // FIXME Get the real country code
    string country_code = "US";
    return p->async_get<GuideCategoryList>(
            { "youtube", "v3", "guideCategories" }, { { "part", "snippet" }, {
                    "regionCode", country_code }, { "h1", p->locale_ } },
            [](const json::Value &root) {
                return get_typed_list<GuideCategory>("youtube#guideCategory", root);
            });
}

future<Client::ChannelList> Client::category_channels(
        const string &categoryId) {
    return p->async_get<ChannelList>( { "youtube", "v3", "channels" }, { {
            "part", "snippet" }, { "categoryId", categoryId } },
            [](const json::Value &root) {
                return get_typed_list<Channel>("youtube#channel", root);
            });
}

future<Client::ChannelSectionList> Client::channel_sections(
        const string &channelId, int maxResults) {
    return p->async_get<ChannelSectionList>( { "youtube", "v3",
            "channelSections" }, { { "part", "contentDetails" }, { "channelId",
            channelId }, { "maxResults", to_string(maxResults) } },
            [](const json::Value &root) {
                return get_typed_list<ChannelSection>("youtube#channelSection", root);
            });
}

future<Client::VideoList> Client::channel_videos(const string &channelId) {
    return p->async_get<VideoList>( { "youtube", "v3", "search" }, { { "part",
            "snippet" }, { "type", "video" }, { "order", "viewCount" }, {
            "channelId", channelId } }, [](const json::Value &root) {
        return get_typed_list<Video>("youtube#video", root);
    });
}

future<Client::PlaylistItemList> Client::playlist_items(
        const string &playlistId) {
    return p->async_get<PlaylistItemList>( { "youtube", "v3", "playlistItems" },
            { { "part", "snippet,contentDetails" }, { "playlistId", playlistId } },
            [](const json::Value &root) {
                return get_typed_list<PlaylistItem>("youtube#playlistItem", root);
            });
}

future<Client::ResourceList> Client::feed() {
    return p->async_get<ResourceList>( { "youtube", "v3", "videos" }, { {
            "part", "snippet" }, { "chart", "mostPopular" } },
            [](const json::Value &root) {
                return get_list(root);
            });
}

void Client::cancel() {
    p->cancelled_ = true;
}

Config::Ptr Client::config() {
    return p->config_;
}
