// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/session_cleanup_cookie_store.h"

#include <list>
#include <memory>

#include "base/bind.h"
#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/memory/ref_counted.h"
#include "base/sequenced_task_runner.h"
#include "base/task/post_task.h"
#include "base/values.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_util.h"
#include "net/extras/sqlite/cookie_crypto_delegate.h"
#include "net/log/net_log.h"
#include "url/gurl.h"

namespace network {

namespace {

std::unique_ptr<base::Value> CookieStoreOriginFiltered(
    const std::string& origin,
    bool is_https,
    net::NetLogCaptureMode capture_mode) {
  if (!capture_mode.include_cookies_and_credentials())
    return nullptr;
  auto dict = std::make_unique<base::DictionaryValue>();
  dict->SetString("origin", origin);
  dict->SetBoolean("is_https", is_https);
  return dict;
}

}  // namespace

SessionCleanupCookieStore::SessionCleanupCookieStore(
    const scoped_refptr<net::SQLitePersistentCookieStore>& cookie_store)
    : persistent_store_(cookie_store) {}

SessionCleanupCookieStore::~SessionCleanupCookieStore() {
  net_log_.AddEvent(
      net::NetLogEventType::COOKIE_PERSISTENT_STORE_CLOSED,
      net::NetLog::StringCallback("type", "SessionCleanupCookieStore"));
}

void SessionCleanupCookieStore::DeleteSessionCookies(
    DeleteCookiePredicate delete_cookie_predicate) {
  using CookieOrigin = net::SQLitePersistentCookieStore::CookieOrigin;
  if (force_keep_session_state_ || !delete_cookie_predicate)
    return;

  std::list<CookieOrigin> session_only_cookies;
  for (const auto& entry : cookies_per_origin_) {
    if (entry.second == 0) {
      continue;
    }
    const CookieOrigin& cookie = entry.first;
    const GURL url(
        net::cookie_util::CookieOriginToURL(cookie.first, cookie.second));
    if (!url.is_valid() ||
        !delete_cookie_predicate.Run(cookie.first, cookie.second)) {
      continue;
    }
    net_log_.AddEvent(
        net::NetLogEventType::COOKIE_PERSISTENT_STORE_ORIGIN_FILTERED,
        base::BindRepeating(&CookieStoreOriginFiltered, cookie.first,
                            cookie.second));
    session_only_cookies.push_back(cookie);
  }

  persistent_store_->DeleteAllInList(session_only_cookies);
}

void SessionCleanupCookieStore::Load(const LoadedCallback& loaded_callback,
                                     const net::NetLogWithSource& net_log) {
  net_log_ = net_log;
  persistent_store_->Load(
      base::BindRepeating(&SessionCleanupCookieStore::OnLoad, this,
                          loaded_callback),
      net_log);
}

void SessionCleanupCookieStore::LoadCookiesForKey(
    const std::string& key,
    const LoadedCallback& loaded_callback) {
  persistent_store_->LoadCookiesForKey(
      key, base::BindRepeating(&SessionCleanupCookieStore::OnLoad, this,
                               loaded_callback));
}

void SessionCleanupCookieStore::AddCookie(const net::CanonicalCookie& cc) {
  net::SQLitePersistentCookieStore::CookieOrigin origin(cc.Domain(),
                                                        cc.IsSecure());
  ++cookies_per_origin_[origin];
  persistent_store_->AddCookie(cc);
}

void SessionCleanupCookieStore::UpdateCookieAccessTime(
    const net::CanonicalCookie& cc) {
  persistent_store_->UpdateCookieAccessTime(cc);
}

void SessionCleanupCookieStore::DeleteCookie(const net::CanonicalCookie& cc) {
  net::SQLitePersistentCookieStore::CookieOrigin origin(cc.Domain(),
                                                        cc.IsSecure());
  DCHECK_GE(cookies_per_origin_[origin], 1U);
  --cookies_per_origin_[origin];
  persistent_store_->DeleteCookie(cc);
}

void SessionCleanupCookieStore::SetForceKeepSessionState() {
  force_keep_session_state_ = true;
}

void SessionCleanupCookieStore::SetBeforeCommitCallback(
    base::RepeatingClosure callback) {
  persistent_store_->SetBeforeCommitCallback(std::move(callback));
}

void SessionCleanupCookieStore::Flush(base::OnceClosure callback) {
  persistent_store_->Flush(std::move(callback));
}

void SessionCleanupCookieStore::OnLoad(
    const LoadedCallback& loaded_callback,
    std::vector<std::unique_ptr<net::CanonicalCookie>> cookies) {
  for (const auto& cookie : cookies) {
    net::SQLitePersistentCookieStore::CookieOrigin origin(cookie->Domain(),
                                                          cookie->IsSecure());
    ++cookies_per_origin_[origin];
  }

  loaded_callback.Run(std::move(cookies));
}

}  // namespace network
