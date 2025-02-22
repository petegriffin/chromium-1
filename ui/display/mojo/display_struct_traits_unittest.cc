// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/macros.h"
#include "mojo/public/cpp/base/file_path_mojom_traits.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/display/display.h"
#include "ui/display/display_layout.h"
#include "ui/display/mojo/display_layout_struct_traits.h"
#include "ui/display/mojo/display_mode_struct_traits.h"
#include "ui/display/mojo/display_snapshot_struct_traits.h"
#include "ui/display/mojo/display_struct_traits.h"
#include "ui/display/mojo/gamma_ramp_rgb_entry_struct_traits.h"
#include "ui/display/types/display_constants.h"
#include "ui/display/types/display_mode.h"
#include "ui/display/types/display_snapshot.h"
#include "ui/display/types/gamma_ramp_rgb_entry.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace display {
namespace {

constexpr int64_t kDisplayId1 = 123;
constexpr int64_t kDisplayId2 = 456;
constexpr int64_t kDisplayId3 = 789;

void CheckDisplaysEqual(const Display& input, const Display& output) {
  EXPECT_NE(&input, &output);  // Make sure they aren't the same object.
  EXPECT_EQ(input.id(), output.id());
  EXPECT_EQ(input.bounds(), output.bounds());
  EXPECT_EQ(input.GetSizeInPixel(), output.GetSizeInPixel());
  EXPECT_EQ(input.work_area(), output.work_area());
  EXPECT_EQ(input.device_scale_factor(), output.device_scale_factor());
  EXPECT_EQ(input.rotation(), output.rotation());
  EXPECT_EQ(input.touch_support(), output.touch_support());
  EXPECT_EQ(input.accelerometer_support(), output.accelerometer_support());
  EXPECT_EQ(input.maximum_cursor_size(), output.maximum_cursor_size());
  EXPECT_EQ(input.color_depth(), output.color_depth());
  EXPECT_EQ(input.depth_per_component(), output.depth_per_component());
  EXPECT_EQ(input.is_monochrome(), output.is_monochrome());
}

void CheckDisplayLayoutsEqual(const DisplayLayout& input,
                              const DisplayLayout& output) {
  EXPECT_NE(&input, &output);  // Make sure they aren't the same object.
  EXPECT_EQ(input.placement_list, output.placement_list);
  EXPECT_EQ(input.default_unified, output.default_unified);
  EXPECT_EQ(input.primary_id, output.primary_id);
}

void CheckDisplayModesEqual(const DisplayMode* input,
                            const DisplayMode* output) {
  // DisplaySnapshot can have null DisplayModes, so if |input| is null then
  // |output| should be null too.
  if (input == nullptr && output == nullptr)
    return;

  EXPECT_NE(input, output);  // Make sure they aren't the same object.
  EXPECT_EQ(input->size(), output->size());
  EXPECT_EQ(input->is_interlaced(), output->is_interlaced());
  EXPECT_EQ(input->refresh_rate(), output->refresh_rate());
}

void CheckDisplaySnapShotMojoEqual(const DisplaySnapshot& input,
                                   const DisplaySnapshot& output) {
  // We want to test each component individually to make sure each data member
  // was correctly serialized and deserialized.
  EXPECT_NE(&input, &output);  // Make sure they aren't the same object.
  EXPECT_EQ(input.display_id(), output.display_id());
  EXPECT_EQ(input.origin(), output.origin());
  EXPECT_EQ(input.physical_size(), output.physical_size());
  EXPECT_EQ(input.type(), output.type());
  EXPECT_EQ(input.is_aspect_preserving_scaling(),
            output.is_aspect_preserving_scaling());
  EXPECT_EQ(input.has_overscan(), output.has_overscan());
  EXPECT_EQ(input.has_color_correction_matrix(),
            output.has_color_correction_matrix());
  EXPECT_EQ(input.color_correction_in_linear_space(),
            output.color_correction_in_linear_space());
  EXPECT_EQ(input.display_name(), output.display_name());
  EXPECT_EQ(input.sys_path(), output.sys_path());
  EXPECT_EQ(input.product_code(), output.product_code());
  EXPECT_EQ(input.modes().size(), output.modes().size());

  for (size_t i = 0; i < input.modes().size(); i++)
    CheckDisplayModesEqual(input.modes()[i].get(), output.modes()[i].get());

  EXPECT_EQ(input.edid(), output.edid());

  CheckDisplayModesEqual(input.current_mode(), output.current_mode());
  CheckDisplayModesEqual(input.native_mode(), output.native_mode());

  EXPECT_EQ(input.maximum_cursor_size(), output.maximum_cursor_size());
}

// Test StructTrait serialization and deserialization for copyable type. |input|
// will be serialized and then deserialized into |output|.
template <class MojomType, class Type>
void SerializeAndDeserialize(const Type& input, Type* output) {
  MojomType::Deserialize(MojomType::Serialize(&input), output);
}

// Test StructTrait serialization and deserialization for move only type.
// |input| will be serialized and then deserialized into |output|.
template <class MojomType, class Type>
void SerializeAndDeserialize(Type&& input, Type* output) {
  MojomType::Deserialize(MojomType::Serialize(&input), output);
}
}  // namespace

TEST(DisplayStructTraitsTest, DefaultDisplayValues) {
  Display input(5);

  Display output;
  SerializeAndDeserialize<mojom::Display>(input, &output);

  CheckDisplaysEqual(input, output);
}

TEST(DisplayStructTraitsTest, SetAllDisplayValues) {
  const gfx::Rect bounds(100, 200, 500, 600);
  const gfx::Rect work_area(150, 250, 400, 500);
  const gfx::Size maximum_cursor_size(64, 64);

  Display input(246345234, bounds);
  input.set_work_area(work_area);
  input.set_device_scale_factor(2.0f);
  input.set_rotation(Display::ROTATE_270);
  input.set_touch_support(Display::TouchSupport::AVAILABLE);
  input.set_accelerometer_support(Display::AccelerometerSupport::UNAVAILABLE);
  input.set_maximum_cursor_size(maximum_cursor_size);
  input.set_color_depth(input.color_depth() + 1);
  input.set_depth_per_component(input.depth_per_component() + 1);
  input.set_is_monochrome(!input.is_monochrome());

  Display output;
  SerializeAndDeserialize<mojom::Display>(input, &output);

  CheckDisplaysEqual(input, output);
}

TEST(DisplayStructTraitsTest, DefaultDisplayMode) {
  std::unique_ptr<DisplayMode> input =
      std::make_unique<DisplayMode>(gfx::Size(1024, 768), true, 61.0);

  std::unique_ptr<DisplayMode> output;
  SerializeAndDeserialize<mojom::DisplayMode>(input->Clone(), &output);

  CheckDisplayModesEqual(input.get(), output.get());
}

TEST(DisplayStructTraitsTest, DisplayPlacementFlushAtTop) {
  DisplayPlacement input;
  input.display_id = kDisplayId1;
  input.parent_display_id = kDisplayId2;
  input.position = DisplayPlacement::TOP;
  input.offset = 0;
  input.offset_reference = DisplayPlacement::TOP_LEFT;

  DisplayPlacement output;
  SerializeAndDeserialize<mojom::DisplayPlacement>(input, &output);

  EXPECT_EQ(input, output);
}

TEST(DisplayStructTraitsTest, DisplayPlacementWithOffset) {
  DisplayPlacement input;
  input.display_id = kDisplayId1;
  input.parent_display_id = kDisplayId2;
  input.position = DisplayPlacement::BOTTOM;
  input.offset = -100;
  input.offset_reference = DisplayPlacement::BOTTOM_RIGHT;

  DisplayPlacement output;
  SerializeAndDeserialize<mojom::DisplayPlacement>(input, &output);

  EXPECT_EQ(input, output);
}

TEST(DisplayStructTraitsTest, DisplayLayoutTwoExtended) {
  DisplayPlacement placement;
  placement.display_id = kDisplayId1;
  placement.parent_display_id = kDisplayId2;
  placement.position = DisplayPlacement::RIGHT;
  placement.offset = 0;
  placement.offset_reference = DisplayPlacement::TOP_LEFT;

  auto input = std::make_unique<DisplayLayout>();
  input->placement_list.push_back(placement);
  input->primary_id = kDisplayId2;
  input->default_unified = true;

  std::unique_ptr<DisplayLayout> output;
  SerializeAndDeserialize<mojom::DisplayLayout>(input->Copy(), &output);

  CheckDisplayLayoutsEqual(*input, *output);
}

TEST(DisplayStructTraitsTest, DisplayLayoutThreeExtended) {
  DisplayPlacement placement1;
  placement1.display_id = kDisplayId2;
  placement1.parent_display_id = kDisplayId1;
  placement1.position = DisplayPlacement::LEFT;
  placement1.offset = 0;
  placement1.offset_reference = DisplayPlacement::TOP_LEFT;

  DisplayPlacement placement2;
  placement2.display_id = kDisplayId3;
  placement2.parent_display_id = kDisplayId1;
  placement2.position = DisplayPlacement::RIGHT;
  placement2.offset = -100;
  placement2.offset_reference = DisplayPlacement::BOTTOM_RIGHT;

  auto input = std::make_unique<DisplayLayout>();
  input->placement_list.push_back(placement1);
  input->placement_list.push_back(placement2);
  input->primary_id = kDisplayId1;
  input->default_unified = false;

  std::unique_ptr<DisplayLayout> output;
  SerializeAndDeserialize<mojom::DisplayLayout>(input->Copy(), &output);

  CheckDisplayLayoutsEqual(*input, *output);
}

TEST(DisplayStructTraitsTest, BasicGammaRampRGBEntry) {
  const GammaRampRGBEntry input{259, 81, 16};

  GammaRampRGBEntry output;
  SerializeAndDeserialize<mojom::GammaRampRGBEntry>(input, &output);

  EXPECT_EQ(input.r, output.r);
  EXPECT_EQ(input.g, output.g);
  EXPECT_EQ(input.b, output.b);
}

// One display mode, current and native mode nullptr.
TEST(DisplayStructTraitsTest, DisplaySnapshotCurrentAndNativeModesNull) {
  // Prepare sample input with random values.
  const int64_t display_id = 7;
  const gfx::Point origin(1, 2);
  const gfx::Size physical_size(5, 9);
  const gfx::Size maximum_cursor_size(3, 5);
  const DisplayConnectionType type = DISPLAY_CONNECTION_TYPE_DISPLAYPORT;
  const bool is_aspect_preserving_scaling = true;
  const bool has_overscan = true;
  const bool has_color_correction_matrix = true;
  const bool color_correction_in_linear_space = true;
  const gfx::ColorSpace display_color_space = gfx::ColorSpace::CreateREC709();
  const std::string display_name("whatever display_name");
  const base::FilePath sys_path = base::FilePath::FromUTF8Unsafe("a/cb");
  const int64_t product_code = 19;
  const int32_t year_of_manufacture = 1776;

  const DisplayMode display_mode(gfx::Size(13, 11), true, 40.0f);

  DisplaySnapshot::DisplayModeList modes;
  modes.push_back(display_mode.Clone());

  const DisplayMode* current_mode = nullptr;
  const DisplayMode* native_mode = nullptr;
  const std::vector<uint8_t> edid = {1};
  const bool has_associated_crtc = true;

  std::unique_ptr<DisplaySnapshot> input = std::make_unique<DisplaySnapshot>(
      display_id, origin, physical_size, type, is_aspect_preserving_scaling,
      has_overscan, has_color_correction_matrix,
      color_correction_in_linear_space, display_color_space, display_name,
      sys_path, std::move(modes), edid, current_mode, native_mode, product_code,
      year_of_manufacture, maximum_cursor_size, has_associated_crtc);

  std::unique_ptr<DisplaySnapshot> output;
  SerializeAndDeserialize<mojom::DisplaySnapshot>(input->Clone(), &output);

  CheckDisplaySnapShotMojoEqual(*input, *output);
}

// One display mode that is the native mode and no current mode.
TEST(DisplayStructTraitsTest, DisplaySnapshotCurrentModeNull) {
  // Prepare sample input with random values.
  const int64_t display_id = 6;
  const gfx::Point origin(11, 32);
  const gfx::Size physical_size(55, 49);
  const gfx::Size maximum_cursor_size(13, 95);
  const DisplayConnectionType type = DISPLAY_CONNECTION_TYPE_VGA;
  const bool is_aspect_preserving_scaling = true;
  const bool has_overscan = true;
  const bool has_color_correction_matrix = true;
  const bool color_correction_in_linear_space = true;
  const gfx::ColorSpace display_color_space = gfx::ColorSpace::CreateREC709();
  const std::string display_name("whatever display_name");
  const base::FilePath sys_path = base::FilePath::FromUTF8Unsafe("z/b");
  const int64_t product_code = 9;
  const int32_t year_of_manufacture = 1776;

  const DisplayMode display_mode(gfx::Size(13, 11), true, 50.0f);

  DisplaySnapshot::DisplayModeList modes;
  modes.push_back(display_mode.Clone());

  const DisplayMode* current_mode = nullptr;
  const DisplayMode* native_mode = modes[0].get();
  const std::vector<uint8_t> edid = {1};
  const bool has_associated_crtc = true;

  std::unique_ptr<DisplaySnapshot> input = std::make_unique<DisplaySnapshot>(
      display_id, origin, physical_size, type, is_aspect_preserving_scaling,
      has_overscan, has_color_correction_matrix,
      color_correction_in_linear_space, display_color_space, display_name,
      sys_path, std::move(modes), edid, current_mode, native_mode, product_code,
      year_of_manufacture, maximum_cursor_size, has_associated_crtc);

  std::unique_ptr<DisplaySnapshot> output;
  SerializeAndDeserialize<mojom::DisplaySnapshot>(input->Clone(), &output);

  CheckDisplaySnapShotMojoEqual(*input, *output);
}

// Multiple display modes, both native and current mode set.
TEST(DisplayStructTraitsTest, DisplaySnapshotExternal) {
  // Prepare sample input from external display.
  const int64_t display_id = 9834293210466051;
  const gfx::Point origin(0, 1760);
  const gfx::Size physical_size(520, 320);
  const gfx::Size maximum_cursor_size(4, 5);
  const DisplayConnectionType type = DISPLAY_CONNECTION_TYPE_HDMI;
  const bool is_aspect_preserving_scaling = false;
  const bool has_overscan = false;
  const bool has_color_correction_matrix = false;
  const bool color_correction_in_linear_space = false;
  const std::string display_name("HP Z24i");
  const gfx::ColorSpace display_color_space = gfx::ColorSpace::CreateSRGB();
  const base::FilePath sys_path = base::FilePath::FromUTF8Unsafe("a/cb");
  const int64_t product_code = 139;
  const int32_t year_of_manufacture = 2018;

  const DisplayMode display_mode(gfx::Size(1024, 768), false, 60.0f);
  const DisplayMode display_current_mode(gfx::Size(1440, 900), false, 59.89f);
  const DisplayMode display_native_mode(gfx::Size(1920, 1200), false, 59.89f);

  DisplaySnapshot::DisplayModeList modes;
  modes.push_back(display_mode.Clone());
  modes.push_back(display_current_mode.Clone());
  modes.push_back(display_native_mode.Clone());

  const DisplayMode* current_mode = modes[1].get();
  const DisplayMode* native_mode = modes[2].get();
  const std::vector<uint8_t> edid = {2, 3, 4, 5};
  const bool has_associated_crtc = true;

  std::unique_ptr<DisplaySnapshot> input = std::make_unique<DisplaySnapshot>(
      display_id, origin, physical_size, type, is_aspect_preserving_scaling,
      has_overscan, has_color_correction_matrix,
      color_correction_in_linear_space, display_color_space, display_name,
      sys_path, std::move(modes), edid, current_mode, native_mode, product_code,
      year_of_manufacture, maximum_cursor_size, has_associated_crtc);

  std::unique_ptr<DisplaySnapshot> output;
  SerializeAndDeserialize<mojom::DisplaySnapshot>(input->Clone(), &output);

  CheckDisplaySnapShotMojoEqual(*input, *output);
}

TEST(DisplayStructTraitsTest, DisplaySnapshotInternal) {
  // Prepare sample input from Pixel's internal display.
  const int64_t display_id = 13761487533244416;
  const gfx::Point origin(0, 0);
  const gfx::Size physical_size(270, 180);
  const gfx::Size maximum_cursor_size(64, 64);
  const DisplayConnectionType type = DISPLAY_CONNECTION_TYPE_INTERNAL;
  const bool is_aspect_preserving_scaling = true;
  const bool has_overscan = false;
  const bool has_color_correction_matrix = false;
  const bool color_correction_in_linear_space = false;
  const gfx::ColorSpace display_color_space =
      gfx::ColorSpace::CreateDisplayP3D65();
  const std::string display_name("");
  const base::FilePath sys_path;
  const int64_t product_code = 139;
  const int32_t year_of_manufacture = 2018;

  const DisplayMode display_mode(gfx::Size(2560, 1700), false, 95.96f);

  DisplaySnapshot::DisplayModeList modes;
  modes.push_back(display_mode.Clone());

  const DisplayMode* current_mode = modes[0].get();
  const DisplayMode* native_mode = modes[0].get();
  const std::vector<uint8_t> edid = {2, 3};
  const bool has_associated_crtc = true;

  std::unique_ptr<DisplaySnapshot> input = std::make_unique<DisplaySnapshot>(
      display_id, origin, physical_size, type, is_aspect_preserving_scaling,
      has_overscan, has_color_correction_matrix,
      color_correction_in_linear_space, display_color_space, display_name,
      sys_path, std::move(modes), edid, current_mode, native_mode, product_code,
      year_of_manufacture, maximum_cursor_size, has_associated_crtc);

  std::unique_ptr<DisplaySnapshot> output;
  SerializeAndDeserialize<mojom::DisplaySnapshot>(input->Clone(), &output);

  CheckDisplaySnapShotMojoEqual(*input, *output);
}

}  // namespace display
