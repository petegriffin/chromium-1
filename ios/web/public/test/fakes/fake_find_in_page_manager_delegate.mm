// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/public/test/fakes/fake_find_in_page_manager_delegate.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace web {

FakeFindInPageManagerDelegate::State::State() = default;

FakeFindInPageManagerDelegate::State::~State() = default;

FakeFindInPageManagerDelegate::FakeFindInPageManagerDelegate() = default;

FakeFindInPageManagerDelegate::~FakeFindInPageManagerDelegate() = default;

void FakeFindInPageManagerDelegate::DidCountMatches(WebState* web_state,
                                                    int match_count,
                                                    NSString* query) {
  delegate_state_ = std::make_unique<State>();
  delegate_state_->web_state = web_state;
  delegate_state_->match_count = match_count;
  delegate_state_->query = query;
}

void FakeFindInPageManagerDelegate::DidHighlightMatch(WebState* web_state,
                                                      int index) {}

}  // namespace web
