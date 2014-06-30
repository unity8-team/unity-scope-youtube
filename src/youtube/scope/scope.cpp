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

#include <youtube/scope/scope.h>
#include <youtube/scope/query.h>
#include <youtube/scope/preview.h>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

#include <iostream>
#include <sstream>
#include <fstream>

namespace sc = unity::scopes;
using namespace std;
using namespace boost;
using namespace youtube::scope;
using namespace youtube::api;

void Scope::start(string const&, sc::RegistryProxy const&) {
    config_ = make_shared<Config>();

    if (getenv("YOUTUBE_SCOPE_APIROOT")) {
        config_->apiroot = getenv("YOUTUBE_SCOPE_APIROOT");
    }

    SimpleOAuth oauth("google");
    SimpleOAuth::AuthData auth_data;
    if (getenv("YOUTUBE_SCOPE_IGNORE_ACCOUNTS") == nullptr) {
        auth_data = oauth.auth_data();
    }
    if (auth_data.access_token.empty()) {
        cerr << "Youtube scope is unauthenticated" << endl;
    } else {
        cerr << "Youtube scope is authenticated" << endl;
        config_->authenticated = true;
    }

    config_->access_token = auth_data.access_token;
    config_->client_id = auth_data.client_id;
    config_->client_secret = auth_data.client_secret;
}

void Scope::stop() {
}

sc::SearchQueryBase::UPtr Scope::search(const sc::CannedQuery &query,
        const sc::SearchMetadata &metadata) {
    return sc::SearchQueryBase::UPtr(new Query(query, metadata, config_));
}

sc::PreviewQueryBase::UPtr Scope::preview(sc::Result const& result,
        sc::ActionMetadata const& metadata) {
    return sc::PreviewQueryBase::UPtr(new Preview(result, metadata));
}

//class Activation: public sc::ActivationQueryBase {
//public:
//    Activation(sc::Result const& result, sc::ActionMetadata const& metadata) :
//            sc::ActivationQueryBase(result, metadata) {
//        cerr << "Hello" << endl;
//    }
//};
//
//sc::ActivationQueryBase::UPtr Scope::activate(sc::Result const& result,
//        sc::ActionMetadata const& metadata) {
//    return sc::ActivationQueryBase::UPtr(new Activation(result, metadata));
//}

#define EXPORT __attribute__ ((visibility ("default")))

extern "C" {

EXPORT
unity::scopes::ScopeBase*
// cppcheck-suppress unusedFunction
UNITY_SCOPE_CREATE_FUNCTION() {
    return new Scope();
}

EXPORT
void
// cppcheck-suppress unusedFunction
UNITY_SCOPE_DESTROY_FUNCTION(unity::scopes::ScopeBase* scope_base) {
    delete scope_base;
}

}