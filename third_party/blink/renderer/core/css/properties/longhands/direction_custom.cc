// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/properties/longhands/direction.h"

namespace blink {
namespace css_longhand {

const CSSValue* Direction::CSSValueFromComputedStyleInternal(
    const ComputedStyle& style,
    const SVGComputedStyle&,
    const LayoutObject*,
    Node*,
    bool allow_visited_style) const {
  return CSSIdentifierValue::Create(style.Direction());
}

void Direction::ApplyValue(StyleResolverState& state,
                           const CSSValue& value) const {
  state.Style()->SetDirection(
      To<CSSIdentifierValue>(value).ConvertTo<TextDirection>());
}

}  // namespace css_longhand
}  // namespace blink
