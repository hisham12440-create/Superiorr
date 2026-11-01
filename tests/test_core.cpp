#include <gtest/gtest.h>

#include "core/config.h"
#include "core/oled_curve.h"
#include "core/pacing.h"

using namespace superior;

TEST(Config, ClampForcesValidRanges) {
    Settings s;
    s.language = "xx";
    s.oledLevel = 9;
    s.oledIntensity = 5.0f;
    s.sharpness = -2.0f;
    s.Clamp();
    EXPECT_EQ(s.language, "en");
    EXPECT_EQ(s.oledLevel, 3);
    EXPECT_FLOAT_EQ(s.oledIntensity, 1.0f);
    EXPECT_FLOAT_EQ(s.sharpness, 0.0f);
}

TEST(Config, JsonRoundTrip) {
    Settings a;
    a.language = "ar";
    a.oledLevel = 2;
    a.oledIntensity = 0.7f;
    a.sharpness = 0.25f;
    a.frameGen = false;
    a.antiAlias = false;
    a.vibrance = 0.6f;
    Settings b;
    ASSERT_TRUE(FromJson(ToJson(a), b));
    EXPECT_EQ(b.language, "ar");
    EXPECT_EQ(b.oledLevel, 2);
    EXPECT_FLOAT_EQ(b.oledIntensity, 0.7f);
    EXPECT_FLOAT_EQ(b.sharpness, 0.25f);
    EXPECT_FALSE(b.frameGen);
    EXPECT_FALSE(b.antiAlias);
    EXPECT_FLOAT_EQ(b.vibrance, 0.6f);
}

TEST(Config, BadJsonIsRejectedAndDefaultsKept) {
    Settings s;
    EXPECT_FALSE(FromJson("{not json", s));
    EXPECT_EQ(s.oledLevel, 0);
}

TEST(Config, OutOfRangeValuesAreClampedOnLoad) {
    Settings s;
    ASSERT_TRUE(FromJson(R"({"oled_level":42,"sharpness":9,"vibrance":-3})", s));
    EXPECT_EQ(s.oledLevel, 3);
    EXPECT_FLOAT_EQ(s.sharpness, 1.0f);
    EXPECT_FLOAT_EQ(s.vibrance, 0.0f);
}

TEST(Pacing, GenerateOnlyWhenDisplayIsAboutTwiceTheGameFps) {
    EXPECT_TRUE(ShouldGenerate(33.3, 16.67, 30));   // 30 fps on 60 Hz
    EXPECT_TRUE(ShouldGenerate(16.67, 8.33, 30));   // 60 fps on 120 Hz
    EXPECT_FALSE(ShouldGenerate(16.67, 16.67, 30)); // 60 fps on 60 Hz: nothing to add
    EXPECT_FALSE(ShouldGenerate(50.0, 16.67, 30));  // 20 fps on 60 Hz: x3 needed, not supported
    EXPECT_FALSE(ShouldGenerate(33.3, 16.67, 3));   // not enough samples yet
}

TEST(Pacing, RecommendedCapIsHalfRefresh) {
    EXPECT_EQ(RecommendedGameCap(144.0), 72);
    EXPECT_EQ(RecommendedGameCap(60.0), 30);
}

TEST(Pacing, SceneCutAndStaticDetection) {
    EXPECT_TRUE(IsSceneCut(600, 1000));
    EXPECT_FALSE(IsSceneCut(100, 1000));
    EXPECT_TRUE(IsSceneCut(0, 0));
    EXPECT_TRUE(IsStaticFrame(1000, 1000));
    EXPECT_FALSE(IsStaticFrame(900, 1000));
}

TEST(Pacing, EmaConvergesToSteadyInterval) {
    IntervalEma e;
    for (int i = 0; i < 200; ++i) e.Add(33.3);
    EXPECT_NEAR(e.Value(), 33.3, 0.01);
    EXPECT_EQ(e.Count(), 200);
}

TEST(Oled, CurveIsMonotonicAndNeverClips) {
    for (int level = 1; level <= 3; ++level) {
        for (float a : {0.0f, 0.25f, 0.5f, 1.0f}) {
            float prev = -1.0f;
            for (int i = 0; i <= 4096; ++i) {
                const float y = static_cast<float>(i) / 4096.0f;
                const float o = OledCurve(y, level, a);
                EXPECT_GE(o, prev - 1e-6f) << "level " << level << " a " << a << " y " << y;
                EXPECT_GE(o, 0.0f);
                EXPECT_LE(o, 1.0f + 1e-6f);
                prev = o;
            }
            EXPECT_NEAR(OledCurve(0.0f, level, a), 0.0f, 1e-6f);
            EXPECT_NEAR(OledCurve(1.0f, level, a), 1.0f, 1e-5f);
        }
    }
}

TEST(Oled, LevelZeroIsIdentityAndDarksGetDarker) {
    EXPECT_FLOAT_EQ(OledCurve(0.3f, 0, 1.0f), 0.3f);
    EXPECT_LT(OledCurve(0.05f, 3, 1.0f), 0.05f);
}

#include "core/timing.h"

TEST(Timing, MonotonicAndResettable) {
    SteadyTimer timer;
    const double first = timer.ElapsedMs();
    EXPECT_GE(first, 0.0);
    EXPECT_GE(timer.ElapsedMs(), first);
    timer.Reset();
    EXPECT_GE(timer.ElapsedMs(), 0.0);
}

TEST(Config, WrongTypesLeaveExistingSettingsUntouched) {
    Settings settings;
    settings.sharpness = 0.7f;
    EXPECT_FALSE(FromJson(R"({"language":"ar","sharpness":"wrong"})", settings));
    EXPECT_EQ(settings.language, "en");
    EXPECT_FLOAT_EQ(settings.sharpness, 0.7f);
}

TEST(Pacing, ResetRemovesPreviousSessionHistory) {
    IntervalEma ema;
    for (int i = 0; i < 20; ++i) ema.Add(33.3);
    ema.Reset();
    EXPECT_EQ(ema.Count(), 0);
    EXPECT_DOUBLE_EQ(ema.Value(), 0);
    ema.Add(-1);
    ema.Add(0);
    EXPECT_EQ(ema.Count(), 0);
}

#include "hardware/policy.h"
using namespace superior::hardware;
TEST(Hardware, TierBoundaries) {
    EXPECT_EQ(SelectTier(0), Tier::Low);
    EXPECT_EQ(SelectTier(4 * 1024 * MiB), Tier::Low);
    EXPECT_EQ(SelectTier(6 * 1024 * MiB), Tier::SixGB);
    EXPECT_EQ(SelectTier(8 * 1024 * MiB), Tier::EightGB);
    EXPECT_EQ(SelectTier(12 * 1024 * MiB), Tier::High);
}
TEST(Hardware, BudgetSaturatesWithoutUnsignedUnderflow) {
    EXPECT_EQ(AvailableBudget(100 * MiB, 200 * MiB, Tier::High), 0);
    EXPECT_EQ(AvailableBudget(400 * MiB, 0, Tier::High), 0);
    EXPECT_EQ(AvailableBudget(600 * MiB, 100 * MiB, Tier::High), 100 * MiB);
    EXPECT_EQ(AvailableBudget(8000 * MiB, 0, Tier::Low), 256 * MiB);
}
TEST(Comfort, ModeBudgetTable) {
    EXPECT_DOUBLE_EQ(BudgetMs(Mode::Comfort, 16), 4);
    EXPECT_DOUBLE_EQ(BudgetMs(Mode::Balanced, 10), 4);
    EXPECT_DOUBLE_EQ(BudgetMs(Mode::Max, 10), 6.5);
    EXPECT_DOUBLE_EQ(BudgetMs(Mode::Max, -1), 0);
}
TEST(Comfort, ImmediateBackoffAndFiveSecondRecovery) {
    Governor governor;
    governor.Update(5, 4, 0);
    EXPECT_FALSE(governor.Enabled());
    governor.Update(2, 4, 100);
    governor.Update(2, 4, 5099);
    EXPECT_FALSE(governor.Enabled());
    governor.Update(2, 4, 5100);
    EXPECT_TRUE(governor.Enabled());
    governor.Update(8, 4, 5200);
    governor.Update(2, 4, 5300);
    governor.Update(3, 4, 9000); // Lost stable headroom.
    governor.Update(2, 4, 10000);
    governor.Update(2, 4, 14999);
    EXPECT_FALSE(governor.Enabled());
}
TEST(Config, ComfortModeIsPersistedAndClamped) {
    Settings settings;
    EXPECT_EQ(settings.comfortMode, 0);
    EXPECT_TRUE(FromJson(R"({"comfort_mode":99})", settings));
    EXPECT_EQ(settings.comfortMode, 2);
    Settings roundTrip;
    EXPECT_TRUE(FromJson(ToJson(settings), roundTrip));
    EXPECT_EQ(roundTrip.comfortMode, 2);
}
TEST(Hardware, PowerPolicyNeverLoosensComfort) {
    EXPECT_EQ(EffectiveMode(Mode::Comfort, true, false), Mode::Comfort);
    EXPECT_EQ(EffectiveMode(Mode::Max, true, false), Mode::Balanced);
    EXPECT_EQ(EffectiveMode(Mode::Max, false, true), Mode::Balanced);
    EXPECT_EQ(EffectiveMode(Mode::Max, false, false), Mode::Max);
    EXPECT_TRUE(PowerAllowsGeneration(false, false));
    EXPECT_FALSE(PowerAllowsGeneration(true, false));
    EXPECT_FALSE(PowerAllowsGeneration(false, true));
}
