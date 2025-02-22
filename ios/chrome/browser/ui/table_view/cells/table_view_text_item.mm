// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/table_view/cells/table_view_text_item.h"

#include "base/mac/foundation_util.h"
#import "ios/chrome/browser/ui/table_view/cells/table_view_cells_constants.h"
#import "ios/chrome/browser/ui/table_view/chrome_table_view_styler.h"
#import "ios/chrome/browser/ui/util/uikit_ui_util.h"
#include "ios/chrome/grit/ios_strings.h"
#include "ui/base/l10n/l10n_util_mac.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

#pragma mark - TableViewTextItem

@implementation TableViewTextItem

- (instancetype)initWithType:(NSInteger)type {
  self = [super initWithType:type];
  if (self) {
    self.cellClass = [TableViewTextCell class];
    _enabled = YES;
  }
  return self;
}

- (void)configureCell:(TableViewCell*)tableCell
           withStyler:(ChromeTableViewStyler*)styler {
  [super configureCell:tableCell withStyler:styler];
  TableViewTextCell* cell =
      base::mac::ObjCCastStrict<TableViewTextCell>(tableCell);
  // TODO(crbug.com/894791): set isAccessibilityElement = YES in TableViewItem.
  cell.isAccessibilityElement = YES;

  if (self.masked) {
    cell.textLabel.text = kMaskedPassword;
    cell.accessibilityLabel =
        l10n_util::GetNSString(IDS_IOS_SETTINGS_PASSWORD_HIDDEN_LABEL);
  } else {
    cell.textLabel.text = self.text;
    cell.accessibilityLabel =
        self.accessibilityLabel ? self.accessibilityLabel : self.text;
  }

  // Decide cell.textLabel.backgroundColor in order:
  //   1. styler.cellBackgroundColor;
  //   2. styler.tableViewBackgroundColor.
  cell.textLabel.backgroundColor = styler.cellBackgroundColor
                                       ? styler.cellBackgroundColor
                                       : styler.tableViewBackgroundColor;

  // Decide cell.textLabel.textColor in order:
  //   1. this.textColor;
  //   2. styler.cellTitleColor;
  //   3. kTableViewTextLabelColorLightGrey.
  if (self.textColor) {
    cell.textLabel.textColor = self.textColor;
  } else if (styler.cellTitleColor) {
    cell.textLabel.textColor = styler.cellTitleColor;
  } else {
    cell.textLabel.textColor =
        UIColorFromRGB(kTableViewTextLabelColorLightGrey);
  }
  cell.textLabel.textAlignment =
      self.textAlignment ? self.textAlignment : NSTextAlignmentNatural;

  cell.userInteractionEnabled = self.enabled;
}

@end

#pragma mark - TableViewTextCell

@implementation TableViewTextCell
@synthesize textLabel = _textLabel;
@synthesize checked = _checked;

- (instancetype)initWithStyle:(UITableViewCellStyle)style
              reuseIdentifier:(NSString*)reuseIdentifier {
  self = [super initWithStyle:style reuseIdentifier:reuseIdentifier];
  if (self) {
    // Text Label, set font sizes using dynamic type.
    _textLabel = [[UILabel alloc] init];
    _textLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _textLabel.numberOfLines = 0;
    _textLabel.lineBreakMode = NSLineBreakByWordWrapping;
    _textLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    _textLabel.isAccessibilityElement = NO;

    // Add subviews to View Hierarchy.
    [self.contentView addSubview:_textLabel];

    // Set and activate constraints.
    [NSLayoutConstraint activateConstraints:@[
      // Title Label Constraints.
      [_textLabel.leadingAnchor
          constraintEqualToAnchor:self.contentView.leadingAnchor
                         constant:kTableViewHorizontalSpacing],
      [_textLabel.topAnchor
          constraintEqualToAnchor:self.contentView.topAnchor
                         constant:kTableViewOneLabelCellVerticalSpacing],
      [_textLabel.bottomAnchor
          constraintEqualToAnchor:self.contentView.bottomAnchor
                         constant:-kTableViewOneLabelCellVerticalSpacing],
      [_textLabel.trailingAnchor
          constraintEqualToAnchor:self.contentView.trailingAnchor
                         constant:-kTableViewHorizontalSpacing]
    ]];
  }
  return self;
}

- (void)setChecked:(BOOL)checked {
  if (checked) {
    self.accessoryView = [[UIImageView alloc]
        initWithImage:[UIImage imageNamed:@"bookmark_blue_check"]];
  } else {
    self.accessoryView = nil;
  }
  _checked = checked;
}

- (void)prepareForReuse {
  [super prepareForReuse];
  self.checked = NO;
  self.userInteractionEnabled = YES;
}

@end
