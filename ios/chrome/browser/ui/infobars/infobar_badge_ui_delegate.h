// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_INFOBARS_INFOBAR_BADGE_UI_DELEGATE_H_
#define IOS_CHROME_BROWSER_UI_INFOBARS_INFOBAR_BADGE_UI_DELEGATE_H_

#import <UIKit/UIKit.h>

// Delegate that handles any followup actions to Infobar UI events.
@protocol InfobarBadgeUIDelegate

// Called whenever an InfobarModal was presented.
- (void)infobarModalWasPresented;

// Called whenever an InfobarModal was dismissed.
- (void)infobarModalWasDismissed;

// Called whenever an Infobar accept/confirm button was tapped. It is
// triggered by either the banner or modal button.
- (void)infobarWasAccepted;

@end

#endif  // IOS_CHROME_BROWSER_UI_INFOBARS_INFOBAR_BADGE_UI_DELEGATE_H_
