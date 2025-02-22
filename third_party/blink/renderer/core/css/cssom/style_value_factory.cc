// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/cssom/style_value_factory.h"

#include "third_party/blink/renderer/core/css/css_custom_ident_value.h"
#include "third_party/blink/renderer/core/css/css_custom_property_declaration.h"
#include "third_party/blink/renderer/core/css/css_identifier_value.h"
#include "third_party/blink/renderer/core/css/css_image_value.h"
#include "third_party/blink/renderer/core/css/css_property_name.h"
#include "third_party/blink/renderer/core/css/css_value.h"
#include "third_party/blink/renderer/core/css/css_value_pair.h"
#include "third_party/blink/renderer/core/css/css_variable_reference_value.h"
#include "third_party/blink/renderer/core/css/cssom/css_keyword_value.h"
#include "third_party/blink/renderer/core/css/cssom/css_numeric_value.h"
#include "third_party/blink/renderer/core/css/cssom/css_position_value.h"
#include "third_party/blink/renderer/core/css/cssom/css_style_value.h"
#include "third_party/blink/renderer/core/css/cssom/css_style_variable_reference_value.h"
#include "third_party/blink/renderer/core/css/cssom/css_transform_value.h"
#include "third_party/blink/renderer/core/css/cssom/css_unparsed_value.h"
#include "third_party/blink/renderer/core/css/cssom/css_unsupported_style_value.h"
#include "third_party/blink/renderer/core/css/cssom/css_url_image_value.h"
#include "third_party/blink/renderer/core/css/cssom/cssom_types.h"
#include "third_party/blink/renderer/core/css/parser/css_property_parser.h"
#include "third_party/blink/renderer/core/css/parser/css_tokenizer.h"
#include "third_party/blink/renderer/core/css/parser/css_variable_parser.h"
#include "third_party/blink/renderer/core/css/properties/css_property.h"
#include "third_party/blink/renderer/core/css/property_registration.h"
#include "third_party/blink/renderer/core/style_property_shorthand.h"

namespace blink {

namespace {

// Reify and return a CSSStyleValue, if |value| can be reified without the
// context of a CSS property.
CSSStyleValue* CreateStyleValueWithoutProperty(const CSSValue& value) {
  if (value.IsCSSWideKeyword())
    return CSSKeywordValue::FromCSSValue(value);
  if (auto* variable_reference_value =
          DynamicTo<CSSVariableReferenceValue>(value))
    return CSSUnparsedValue::FromCSSValue(*variable_reference_value);
  if (auto* custom_prop_declaration =
          DynamicTo<CSSCustomPropertyDeclaration>(value)) {
    return CSSUnparsedValue::FromCSSValue(*custom_prop_declaration);
  }
  return nullptr;
}

CSSStyleValue* CreateStyleValue(const CSSValue& value) {
  if (IsA<CSSIdentifierValue>(value) || IsA<CSSCustomIdentValue>(value))
    return CSSKeywordValue::FromCSSValue(value);
  if (auto* primitive_value = DynamicTo<CSSPrimitiveValue>(value))
    return CSSNumericValue::FromCSSValue(*primitive_value);
  if (auto* image_value = DynamicTo<CSSImageValue>(value)) {
    return CSSURLImageValue::FromCSSValue(*image_value->Clone());
  }
  return nullptr;
}

CSSStyleValue* CreateStyleValueWithPropertyInternal(CSSPropertyID property_id,
                                                    const CSSValue& value) {
  // FIXME: We should enforce/document what the possible CSSValue structures
  // are for each property.
  switch (property_id) {
    case CSSPropertyBorderBottomLeftRadius:
    case CSSPropertyBorderBottomRightRadius:
    case CSSPropertyBorderTopLeftRadius:
    case CSSPropertyBorderTopRightRadius: {
      // border-radius-* are always stored as pairs, but when both values are
      // the same, we should reify as a single value.
      if (const auto* pair = DynamicTo<CSSValuePair>(value)) {
        if (pair->First() == pair->Second() && !pair->KeepIdenticalValues()) {
          return CreateStyleValue(pair->First());
        }
      }
      return nullptr;
    }
    case CSSPropertyCaretColor: {
      // caret-color also supports 'auto'
      auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
      if (identifier_value && identifier_value->GetValueID() == CSSValueAuto)
        return CSSKeywordValue::Create("auto");
      FALLTHROUGH;
    }
    case CSSPropertyBackgroundColor:
    case CSSPropertyBorderBottomColor:
    case CSSPropertyBorderLeftColor:
    case CSSPropertyBorderRightColor:
    case CSSPropertyBorderTopColor:
    case CSSPropertyColor:
    case CSSPropertyColumnRuleColor:
    case CSSPropertyFloodColor:
    case CSSPropertyLightingColor:
    case CSSPropertyOutlineColor:
    case CSSPropertyStopColor:
    case CSSPropertyTextDecorationColor:
    case CSSPropertyWebkitTextEmphasisColor: {
      // Only 'currentcolor' is supported.
      auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
      if (identifier_value &&
          identifier_value->GetValueID() == CSSValueCurrentcolor)
        return CSSKeywordValue::Create("currentcolor");
      return CSSUnsupportedStyleValue::Create(CSSPropertyName(property_id),
                                              value);
    }
    case CSSPropertyContain: {
      if (value.IsIdentifierValue())
        return CreateStyleValue(value);

      // Only single values are supported in level 1.
      const auto& value_list = To<CSSValueList>(value);
      if (value_list.length() == 1U)
        return CreateStyleValue(value_list.Item(0));
      return nullptr;
    }
    case CSSPropertyFontVariantEastAsian:
    case CSSPropertyFontVariantLigatures:
    case CSSPropertyFontVariantNumeric: {
      // Only single keywords are supported in level 1.
      if (const auto* value_list = DynamicTo<CSSValueList>(value)) {
        if (value_list->length() != 1U)
          return nullptr;
        return CreateStyleValue(value_list->Item(0));
      }
      return CreateStyleValue(value);
    }
    case CSSPropertyGridAutoFlow: {
      const auto& value_list = To<CSSValueList>(value);
      // Only single keywords are supported in level 1.
      if (value_list.length() == 1U)
        return CreateStyleValue(value_list.Item(0));
      return nullptr;
    }
    case CSSPropertyTransform:
      return CSSTransformValue::FromCSSValue(value);
    case CSSPropertyOffsetAnchor:
    case CSSPropertyOffsetPosition:
      // offset-anchor and offset-position can be 'auto'
      if (value.IsIdentifierValue())
        return CreateStyleValue(value);
      FALLTHROUGH;
    case CSSPropertyObjectPosition:
    case CSSPropertyPerspectiveOrigin:
    case CSSPropertyTransformOrigin:
      return CSSPositionValue::FromCSSValue(value);
    case CSSPropertyOffsetRotate: {
      const auto& value_list = To<CSSValueList>(value);
      // Only single keywords are supported in level 1.
      if (value_list.length() == 1U)
        return CreateStyleValue(value_list.Item(0));
      return nullptr;
    }
    case CSSPropertyAlignItems: {
      // Computed align-items is a ValueList of either length 1 or 2.
      // Typed OM level 1 can't support "pairs", so we only return
      // a Typed OM object for length 1 lists.
      if (const auto* value_list = DynamicTo<CSSValueList>(value)) {
        if (value_list->length() != 1U)
          return nullptr;
        return CreateStyleValue(value_list->Item(0));
      }
      return CreateStyleValue(value);
    }
    case CSSPropertyTextDecorationLine: {
      if (value.IsIdentifierValue())
        return CreateStyleValue(value);

      const auto& value_list = To<CSSValueList>(value);
      // Only single keywords are supported in level 1.
      if (value_list.length() == 1U)
        return CreateStyleValue(value_list.Item(0));
      return nullptr;
    }
    case CSSPropertyTextIndent: {
      if (value.IsIdentifierValue())
        return CreateStyleValue(value);

      const auto& value_list = To<CSSValueList>(value);
      // Only single values are supported in level 1.
      if (value_list.length() == 1U)
        return CreateStyleValue(value_list.Item(0));
      return nullptr;
    }
    case CSSPropertyTransitionProperty:
    case CSSPropertyTouchAction: {
      const auto& value_list = To<CSSValueList>(value);
      // Only single values are supported in level 1.
      if (value_list.length() == 1U)
        return CreateStyleValue(value_list.Item(0));
      return nullptr;
    }
    case CSSPropertyWillChange: {
      // Only 'auto' is supported, which can be stored as an identifier or list.
      if (value.IsIdentifierValue())
        return CreateStyleValue(value);

      const auto& value_list = To<CSSValueList>(value);
      if (value_list.length() == 1U) {
        const auto* ident = DynamicTo<CSSIdentifierValue>(value_list.Item(0));
        if (ident && ident->GetValueID() == CSSValueAuto)
          return CreateStyleValue(value_list.Item(0));
      }
      return nullptr;
    }
    default:
      // TODO(meade): Implement other properties.
      break;
  }
  return nullptr;
}

CSSStyleValue* CreateStyleValueWithProperty(CSSPropertyID property_id,
                                            const CSSValue& value) {
  DCHECK_NE(property_id, CSSPropertyInvalid);

  if (CSSStyleValue* style_value = CreateStyleValueWithoutProperty(value))
    return style_value;

  if (!CSSOMTypes::IsPropertySupported(property_id)) {
    DCHECK_NE(property_id, CSSPropertyVariable);
    return CSSUnsupportedStyleValue::Create(CSSPropertyName(property_id),
                                            value);
  }

  CSSStyleValue* style_value =
      CreateStyleValueWithPropertyInternal(property_id, value);
  if (style_value)
    return style_value;
  return CreateStyleValue(value);
}

CSSStyleValueVector UnsupportedCSSValue(const CSSPropertyName& name,
                                        const CSSValue& value) {
  CSSStyleValueVector style_value_vector;
  style_value_vector.push_back(CSSUnsupportedStyleValue::Create(name, value));
  return style_value_vector;
}

}  // namespace

CSSStyleValueVector StyleValueFactory::FromString(
    CSSPropertyID property_id,
    const AtomicString& custom_property_name,
    const PropertyRegistration* registration,
    const String& css_text,
    const CSSParserContext* parser_context) {
  DCHECK_NE(property_id, CSSPropertyInvalid);
  DCHECK_EQ(property_id == CSSPropertyVariable, !custom_property_name.IsNull());
  CSSTokenizer tokenizer(css_text);
  const auto tokens = tokenizer.TokenizeToEOF();
  const CSSParserTokenRange range(tokens);

  HeapVector<CSSPropertyValue, 256> parsed_properties;
  if (property_id != CSSPropertyVariable &&
      CSSPropertyParser::ParseValue(property_id, false, range, parser_context,
                                    parsed_properties,
                                    StyleRule::RuleType::kStyle)) {
    if (parsed_properties.size() == 1) {
      const auto result = StyleValueFactory::CssValueToStyleValueVector(
          CSSPropertyName(parsed_properties[0].Id()),
          *parsed_properties[0].Value());
      // TODO(801935): Handle list-valued properties.
      if (result.size() == 1U)
        result[0]->SetCSSText(css_text);

      return result;
    }

    // Shorthands are not yet supported.
    CSSStyleValueVector result;
    result.push_back(CSSUnsupportedStyleValue::Create(
        CSSPropertyName(property_id), css_text));
    return result;
  }

  if (property_id == CSSPropertyVariable && registration) {
    const bool is_animation_tainted = false;
    const CSSValue* value = registration->Syntax().Parse(tokens, parser_context,
                                                         is_animation_tainted);
    if (!value)
      return CSSStyleValueVector();

    return StyleValueFactory::CssValueToStyleValueVector(
        CSSPropertyName(custom_property_name), *value);
  }

  if ((property_id == CSSPropertyVariable && !tokens.IsEmpty()) ||
      CSSVariableParser::ContainsValidVariableReferences(range)) {
    const auto variable_data = CSSVariableData::Create(
        range, false /* is_animation_tainted */,
        false /* needs variable resolution */, parser_context->BaseURL(),
        parser_context->Charset());
    CSSStyleValueVector values;
    values.push_back(CSSUnparsedValue::FromCSSVariableData(*variable_data));
    return values;
  }

  return CSSStyleValueVector();
}

CSSStyleValue* StyleValueFactory::CssValueToStyleValue(
    const CSSPropertyName& name,
    const CSSValue& css_value) {
  DCHECK(!CSSProperty::Get(name.Id()).IsRepeated());
  CSSStyleValue* style_value =
      CreateStyleValueWithProperty(name.Id(), css_value);
  if (!style_value)
    return CSSUnsupportedStyleValue::Create(name, css_value);
  return style_value;
}

CSSStyleValueVector StyleValueFactory::CoerceStyleValuesOrStrings(
    const CSSProperty& property,
    const AtomicString& custom_property_name,
    const PropertyRegistration* registration,
    const HeapVector<CSSStyleValueOrString>& values,
    const ExecutionContext& execution_context) {
  const CSSParserContext* parser_context = nullptr;

  CSSStyleValueVector style_values;
  for (const auto& value : values) {
    if (value.IsCSSStyleValue()) {
      if (!value.GetAsCSSStyleValue())
        return CSSStyleValueVector();
      style_values.push_back(*value.GetAsCSSStyleValue());
    } else {
      DCHECK(value.IsString());
      if (!parser_context)
        parser_context = CSSParserContext::Create(execution_context);

      const auto subvalues = StyleValueFactory::FromString(
          property.PropertyID(), custom_property_name, registration,
          value.GetAsString(), parser_context);
      if (subvalues.IsEmpty())
        return CSSStyleValueVector();

      DCHECK(!subvalues.Contains(nullptr));
      style_values.AppendVector(subvalues);
    }
  }
  return style_values;
}

CSSStyleValueVector StyleValueFactory::CssValueToStyleValueVector(
    const CSSPropertyName& name,
    const CSSValue& css_value) {
  CSSStyleValueVector style_value_vector;

  CSSPropertyID property_id = name.Id();
  CSSStyleValue* style_value =
      CreateStyleValueWithProperty(property_id, css_value);
  if (style_value) {
    style_value_vector.push_back(style_value);
    return style_value_vector;
  }

  // We assume list-valued properties are always stored as a list.
  const auto* css_value_list = DynamicTo<CSSValueList>(css_value);
  if (!css_value_list ||
      // TODO(andruud): Custom properties claim to not be repeated, even though
      // they may be. Therefore we must ignore "IsRepeated" for custom
      // properties.
      (!CSSProperty::Get(property_id).IsRepeated() &&
       property_id != CSSPropertyVariable) ||
      // Note: CSSTransformComponent is parsed as CSSFunctionValue, which is a
      // CSSValueList. We do not yet support such CSSFunctionValues, however.
      // TODO(andruud): Make CSSTransformComponent a subclass of CSSStyleValue,
      // once TypedOM spec is updated.
      // https://github.com/w3c/css-houdini-drafts/issues/290
      (property_id == CSSPropertyVariable &&
       CSSTransformComponent::FromCSSValue(css_value))) {
    return UnsupportedCSSValue(name, css_value);
  }

  for (const CSSValue* inner_value : *css_value_list) {
    style_value = CreateStyleValueWithProperty(property_id, *inner_value);
    if (!style_value)
      return UnsupportedCSSValue(name, css_value);
    style_value_vector.push_back(style_value);
  }
  return style_value_vector;
}

CSSStyleValueVector StyleValueFactory::CssValueToStyleValueVector(
    const CSSValue& css_value) {
  CSSStyleValueVector style_value_vector;

  if (CSSStyleValue* value = CreateStyleValueWithoutProperty(css_value))
    style_value_vector.push_back(value);
  else
    style_value_vector.push_back(CSSUnsupportedStyleValue::Create(css_value));

  return style_value_vector;
}

}  // namespace blink
