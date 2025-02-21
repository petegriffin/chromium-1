// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/search/arc/arc_app_reinstall_app_result.h"

#include <utility>

#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"

namespace app_list {

namespace {
constexpr char kPlayStoreAppUrlPrefix[] =
    "https://play.google.com/store/apps/details?id=";

// We choose a default app reinstallation relevance; This ranks app reinstall
// app result as a top result typically.
constexpr float kAppReinstallRelevance = 0.7;

}  // namespace

ArcAppReinstallAppResult::ArcAppReinstallAppResult(
    const arc::mojom::AppReinstallCandidatePtr& mojom_data,
    const gfx::ImageSkia& skia_icon,
    Observer* observer)
    : observer_(observer) {
  DCHECK(observer_);
  set_id(kPlayStoreAppUrlPrefix + mojom_data->package_name);
  SetResultType(ash::SearchResultType::kPlayStoreReinstallApp);
  SetTitle(base::UTF8ToUTF16(mojom_data->title));
  SetDisplayType(ash::SearchResultDisplayType::kRecommendation);
  set_relevance(kAppReinstallRelevance);
  SetNotifyVisibilityChange(true);
  SetIcon(skia_icon);
  SetChipIcon(skia_icon);
  SetNotifyVisibilityChange(true);

  if (mojom_data->star_rating != 0.0f) {
    SetRating(mojom_data->star_rating);
  }
}

ArcAppReinstallAppResult::~ArcAppReinstallAppResult() = default;

void ArcAppReinstallAppResult::Open(int /*event_flags*/) {
  RecordAction(base::UserMetricsAction("ArcAppReinstall_Clicked"));
  arc::LaunchPlayStoreWithUrl(id());
  observer_->OnOpened(id());
}

void ArcAppReinstallAppResult::OnVisibilityChanged(bool visibility) {
  ChromeSearchResult::OnVisibilityChanged(visibility);
  observer_->OnVisibilityChanged(id(), visibility);
}

}  // namespace app_list
