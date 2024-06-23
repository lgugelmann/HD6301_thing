#include "graphics_state.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eight_bit {
namespace {

class GraphicsStateForTest : public GraphicsState {
 public:
  using GraphicsState::SetBgColor;
  using GraphicsState::SetFgColor;
};

TEST(GraphicsStateTest, SetFgColorIsReturnedByGetForegroundColorIndex) {
  GraphicsStateForTest graphics_state;

  graphics_state.SetFgColor(0, 0x39);
  EXPECT_EQ(graphics_state.GetForegroundColor(0), 0x39);

  graphics_state.SetFgColor(12, 0x39);
  EXPECT_EQ(graphics_state.GetForegroundColor(12), 0x39);
}

TEST(GraphicsStateTest, SetBgColorIsReturnedByGetBackgroundColorIndex) {
  GraphicsStateForTest graphics_state;
  graphics_state.SetBgColor(12, 0x12);
  EXPECT_EQ(graphics_state.GetBackgroundColor(12), 0x12);
}

}  // namespace

}  // namespace eight_bit