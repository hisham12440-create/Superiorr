#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>

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

#include "filters/presets.h"
TEST(Filters, ColourControlsClampAndRoundTrip) {
    Settings settings;
    ASSERT_TRUE(FromJson(R"({"contrast":9,"saturation":-1,"brightness":-9,"gamma":0,"split_preview":true})", settings));
    EXPECT_FLOAT_EQ(settings.contrast, 2);
    EXPECT_FLOAT_EQ(settings.saturation, 0);
    EXPECT_FLOAT_EQ(settings.brightness, -.25f);
    EXPECT_FLOAT_EQ(settings.gamma, .5f);
    Settings copy;
    ASSERT_TRUE(FromJson(ToJson(settings), copy));
    EXPECT_FLOAT_EQ(copy.contrast, settings.contrast);
    EXPECT_FLOAT_EQ(copy.saturation, settings.saturation);
    EXPECT_FLOAT_EQ(copy.brightness, settings.brightness);
    EXPECT_FLOAT_EQ(copy.gamma, settings.gamma);
    EXPECT_TRUE(copy.splitPreview);
}
TEST(Filters, PresetsPreserveSessionAndLanguagePreferences) {
    for (int i = 0; i < 5; ++i) {
        Settings settings;
        settings.language = "ar";
        settings.comfortMode = 2;
        settings.frameGen = false;
        settings.splitPreview = true;
        filters::Apply(static_cast<filters::Preset>(i), settings);
        EXPECT_EQ(settings.language, "ar");
        EXPECT_EQ(settings.comfortMode, 2);
        EXPECT_FALSE(settings.frameGen);
        EXPECT_TRUE(settings.splitPreview);
        EXPECT_GE(settings.gamma, .5f);
        EXPECT_LE(settings.gamma, 2);
        Settings copy;
        ASSERT_TRUE(FromJson(ToJson(settings), copy));
        EXPECT_FLOAT_EQ(settings.contrast, copy.contrast);
    }
}

#include "core/profiles.h"
#include "core/files.h"
#include <atomic>
#include <fstream>

namespace {
class StorageTest : public ::testing::Test {
protected:
    std::filesystem::path directory;
    void SetUp() override {
        static std::atomic<unsigned> sequence{0};
        directory = std::filesystem::temp_directory_path() /
            ("superior-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             "-" + std::to_string(sequence++));
        std::filesystem::create_directories(directory);
    }
    void TearDown() override {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};
}
TEST(Hotkeys, DefaultsAreValidAndKeysHaveReadableNames) {
    std::string error;
    EXPECT_TRUE(ValidateHotkeys(DefaultHotkeys(), error));
    EXPECT_EQ(KeyName('A'), "A");
    EXPECT_EQ(KeyName(0x26), "Up");
    EXPECT_EQ(KeyName(0x7A), "F11");
    EXPECT_EQ(KeyName(0), "Disabled");
}
TEST(Hotkeys, DuplicateAndUnsupportedBindingsAreRejected) {
    auto keys = DefaultHotkeys();
    std::string error;
    keys[1] = keys[0];
    EXPECT_FALSE(ValidateHotkeys(keys, error));
    EXPECT_FALSE(error.empty());
    keys[1] = {0,0};
    EXPECT_TRUE(ValidateHotkeys(keys, error));
    keys[1] = {'B',0};
    EXPECT_FALSE(ValidateHotkeys(keys, error));
    keys[1] = {0x7B,3}; // Windows reserves F12.
    EXPECT_FALSE(ValidateHotkeys(keys, error));
}
TEST(Hotkeys, JsonRoundTripAndAtomicRejection) {
    Settings original;
    original.hotkeys[0] = {0x70,6};
    original.autoProfiles = false;
    Settings loaded;
    ASSERT_TRUE(FromJson(ToJson(original), loaded));
    EXPECT_EQ(loaded.hotkeys, original.hotkeys);
    EXPECT_FALSE(loaded.autoProfiles);
    const auto before = ToJson(loaded);
    EXPECT_FALSE(FromJson(R"({"language":"ar","hotkeys":[]})", loaded));
    EXPECT_EQ(ToJson(loaded), before);
}
TEST_F(StorageTest, AtomicReplacementAndBoundedRead) {
    const auto path = directory / "settings.json";
    ASSERT_TRUE(WriteTextAtomic(path, "old"));
    ASSERT_TRUE(WriteTextAtomic(path, "new contents"));
    std::string text;
    ASSERT_TRUE(ReadText(path, text));
    EXPECT_EQ(text, "new contents");
    EXPECT_FALSE(ReadText(path, text, 3));
    EXPECT_EQ(text, "new contents");
    EXPECT_FALSE(std::filesystem::exists(directory / "settings.json.tmp"));
    EXPECT_FALSE(WriteTextAtomic(directory / "absent" / "file", "x"));
}
TEST_F(StorageTest, UnicodeSettingsPath) {
    const auto path = directory / std::filesystem::u8path(u8"إعدادات.json");
    Settings original;
    original.language = "ar";
    ASSERT_TRUE(SaveSettings(path.u8string(), original));
    Settings restored;
    ASSERT_TRUE(LoadSettings(path.u8string(), restored));
    EXPECT_EQ(restored.language, "ar");
}
TEST_F(StorageTest, GameProfilesPreserveGlobalPreferencesAndIsolation) {
    Settings game;
    game.sharpness = .8f; game.gamma = 1.2f; game.language = "ar";
    ASSERT_TRUE(SaveGameProfile(directory, "c:/games/a/game.exe", game));
    Settings target;
    target.hotkeys[0] = {0x70,6};
    target.autoProfiles = false;
    target.splitPreview = true;
    ASSERT_TRUE(LoadGameProfile(directory, "c:/games/a/game.exe", target));
    EXPECT_FLOAT_EQ(target.sharpness, .8f);
    EXPECT_FLOAT_EQ(target.gamma, 1.2f);
    EXPECT_EQ(target.language, "en");
    EXPECT_EQ(target.hotkeys[0].key, 0x70u);
    EXPECT_FALSE(target.autoProfiles);
    EXPECT_TRUE(target.splitPreview);
    EXPECT_FALSE(LoadGameProfile(directory, "c:/games/b/game.exe", target));
    EXPECT_NE(ProfileKey("c:/games/a/game.exe"), ProfileKey("c:/games/b/game.exe"));
}
TEST_F(StorageTest, CorruptAndMismatchedProfilesDoNotModifySettings) {
    const std::string identity = "c:/games/game.exe";
    const auto path = directory / (ProfileKey(identity) + ".json");
    Settings target;
    target.sharpness = .9f;
    const auto before = ToJson(target);
    ASSERT_TRUE(WriteTextAtomic(path, "{bad json"));
    EXPECT_FALSE(LoadGameProfile(directory, identity, target));
    EXPECT_EQ(ToJson(target), before);
    ASSERT_TRUE(WriteTextAtomic(path, R"({"version":1,"executable":"wrong","settings":{"sharpness":0}})"));
    EXPECT_FALSE(LoadGameProfile(directory, identity, target));
    EXPECT_EQ(ToJson(target), before);
    EXPECT_FALSE(SaveGameProfile(directory, "", target));
}
TEST_F(StorageTest, FailedReplacementPreservesDestinationAndCleansTemporary) {
    const auto destination = directory / "occupied";
    std::filesystem::create_directory(destination);
    ASSERT_TRUE(WriteTextAtomic(destination / "keep.txt", "original"));
    EXPECT_FALSE(WriteTextAtomic(destination, "replacement"));
    std::string text;
    ASSERT_TRUE(ReadText(destination / "keep.txt", text));
    EXPECT_EQ(text, "original");
    EXPECT_FALSE(std::filesystem::exists(directory / "occupied.tmp"));
}
#include "core/log.h"
TEST_F(StorageTest, UnicodeLogAndRotation) {
    const auto path = directory / std::filesystem::u8path(u8"سجل.log");
    ASSERT_TRUE(Log::Open(path.u8string()));
    const std::string large(1024 * 1024 + 1, 'x');
    Log::Write("TEST", "%s", large.c_str());
    Log::Write("TEST", "after rotation");
    Log::Close();
    auto backup = path;
    backup += ".1";
    EXPECT_TRUE(std::filesystem::exists(backup));
    std::string text;
    ASSERT_TRUE(ReadText(path, text));
    EXPECT_NE(text.find("after rotation"), std::string::npos);
}

#include "interpolate/pacer.h"
#include "benchmark/quality.h"
TEST(Interpolation, MultiplierSelectionRespectsRefreshAndComfortCap) {
    using namespace superior::interpolate;
    EXPECT_EQ(ChooseMultiplier(33.333,16.667,20,2),2);
    EXPECT_EQ(ChooseMultiplier(25,8.333,20,4),3);
    EXPECT_EQ(ChooseMultiplier(33.333,8.333,20,4),4);
    EXPECT_EQ(ChooseMultiplier(33.333,8.333,20,2),1); // Comfort cannot fill x4 cadence.
    EXPECT_EQ(ChooseMultiplier(25,16.667,20,4),1); // Noninteger ratio.
    EXPECT_EQ(ChooseMultiplier(16,8,0,4),1);
    EXPECT_EQ(ChooseMultiplier(std::numeric_limits<double>::infinity(),8,20,4),1);
}
TEST(Interpolation, AllPhasesHaveOneDisplayIntervalSpacing) {
    for(int multiplier=2;multiplier<=4;++multiplier) {
        const auto samples=superior::interpolate::Schedule(multiplier,100,8);
        ASSERT_EQ(samples.size(),static_cast<size_t>(multiplier));
        EXPECT_DOUBLE_EQ(samples.front().dueMs,100);
        EXPECT_FLOAT_EQ(samples.back().phase,1);
        for(size_t i=1;i<samples.size();++i) {
            EXPECT_DOUBLE_EQ(samples[i].dueMs-samples[i-1].dueMs,8);
            EXPECT_GT(samples[i].phase,samples[i-1].phase);
        }
    }
    EXPECT_TRUE(superior::interpolate::Schedule(5,0,8).empty());
}
TEST(Interpolation, OppositeFlowIsConsistent) {
    EXPECT_DOUBLE_EQ(superior::interpolate::ForwardBackwardError(3,4,-3,-4),0);
    EXPECT_DOUBLE_EQ(superior::interpolate::ForwardBackwardError(3,4,0,0),5);
}
TEST(Quality, IdenticalImagesHaveExactKnownMetrics) {
    using namespace superior::benchmark;
    Image a{4,1,{0,0,1,1}};
    const auto m=Compare(a,a,a,a);
    EXPECT_DOUBLE_EQ(m.ssim,1);
    EXPECT_DOUBLE_EQ(m.edgeError,0);
    EXPECT_TRUE(m.exact);
    EXPECT_TRUE(std::isinf(m.psnr));
    EXPECT_TRUE(Pass(m,{}));
}
TEST(Quality, OppositeConstantsHaveAnalyticSsimAndPsnr) {
    using namespace superior::benchmark;
    Image black{8,8,std::vector<float>(64,0)},white{8,8,std::vector<float>(64,1)};
    const auto m=Compare(black,white,black,black);
    EXPECT_NEAR(m.ssim,.0001/1.0001,1e-12);
    EXPECT_DOUBLE_EQ(m.psnr,0);
    EXPECT_FALSE(Pass(m,{}));
}
TEST(Quality, SobelEdgeErrorReferenceAndInvalidInput) {
    using namespace superior::benchmark;
    Image black{4,1,{0,0,0,0}},step{4,1,{0,0,1,1}};
    const auto m=Compare(black,step,black,black);
    EXPECT_DOUBLE_EQ(m.edgeError,.5);
    EXPECT_GT(m.flaggedPercent,0);
    Image invalid{1,1,{std::numeric_limits<float>::quiet_NaN()}};
    EXPECT_THROW(Compare(invalid,invalid,invalid,invalid),std::runtime_error);
    Metrics fake; fake.ssim=1; fake.edgeError=-1;
    EXPECT_FALSE(Pass(fake,{}));
}
TEST_F(StorageTest, PgmRoundTripAndReferenceVerificationExports) {
    using namespace superior::benchmark;
    Image image{8,8,std::vector<float>(64,.5f)};
    WritePgm(directory/"frame.pgm",image);
    const auto restored=ReadPgm(directory/"frame.pgm");
    EXPECT_EQ(restored.width,8);
    EXPECT_NEAR(restored.pixels[0],.5f,1.f/255);
    const auto manifest=directory/"manifest.json",report=directory/"report.json";
    ASSERT_TRUE(WriteTextAtomic(manifest,R"({"frames":[{"truth":"frame.pgm","generated":"frame.pgm","source_a":"frame.pgm","source_b":"frame.pgm"}]})"));
    EXPECT_EQ(Verify(manifest,report),0);
    std::string text;
    ASSERT_TRUE(ReadText(report,text));
    EXPECT_NE(text.find("\"target_achieved\": true"),std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(directory/"report.json.txt"));
    ASSERT_TRUE(WriteTextAtomic(manifest,R"({"frames":[]})"));
    EXPECT_EQ(Verify(manifest,report),1);
    ASSERT_TRUE(ReadText(report,text));
    EXPECT_NE(text.find("\"target_achieved\": false"),std::string::npos);
}
TEST_F(StorageTest, TruncatedAndUnsupportedPgmAreRejected) {
    using namespace superior::benchmark;
    ASSERT_TRUE(WriteTextAtomic(directory/"bad.pgm","P5\n2 2\n255\nx"));
    EXPECT_THROW(ReadPgm(directory/"bad.pgm"),std::runtime_error);
    ASSERT_TRUE(WriteTextAtomic(directory/"bad.pgm","P5\n1 1\n65535\nx"));
    EXPECT_THROW(ReadPgm(directory/"bad.pgm"),std::runtime_error);
}
TEST(Config, GenerationMultiplierRoundTripAndClamp) {
    Settings settings;
    ASSERT_TRUE(FromJson(R"({"max_multiplier":99})",settings));
    EXPECT_EQ(settings.maxMultiplier,4);
    Settings restored;
    ASSERT_TRUE(FromJson(ToJson(settings),restored));
    EXPECT_EQ(restored.maxMultiplier,4);
}
TEST_F(StorageTest, VerificationNeverOverwritesManifestOrImages) {
    using namespace superior::benchmark;
    const std::string source=R"({"frames":[{"truth":"frame.pgm","generated":"frame.pgm","source_a":"frame.pgm","source_b":"frame.pgm"}]})";
    const auto manifest=directory/"manifest.json";
    ASSERT_TRUE(WriteTextAtomic(manifest,source));
    EXPECT_EQ(Verify(manifest,manifest),1);
    std::string text;
    ASSERT_TRUE(ReadText(manifest,text));
    EXPECT_EQ(text,source);
    const auto image=directory/"frame.pgm";
    ASSERT_TRUE(WriteTextAtomic(image,"P2\n1 1\n255\n0"));
    EXPECT_EQ(Verify(manifest,image),1);
    ASSERT_TRUE(ReadText(image,text));
    EXPECT_EQ(text,"P2\n1 1\n255\n0");
}
TEST_F(StorageTest, VerificationProgressAndCancellationAreNotSuccess) {
    using namespace superior::benchmark;
    Image image{8,8,std::vector<float>(64,.5f)};
    WritePgm(directory/"frame.pgm",image);
    const auto manifest=directory/"manifest.json",report=directory/"report.json";
    ASSERT_TRUE(WriteTextAtomic(manifest,R"({"frames":[{"truth":"frame.pgm","generated":"frame.pgm","source_a":"frame.pgm","source_b":"frame.pgm"},{"truth":"frame.pgm","generated":"frame.pgm","source_a":"frame.pgm","source_b":"frame.pgm"}]})"));
    size_t completed=0,total=0;
    VerifyControl control;
    control.progress=[&](size_t done,size_t count) { completed=done; total=count; };
    control.cancelled=[&] { return completed==1; };
    EXPECT_EQ(Verify(manifest,report,control),3);
    EXPECT_EQ(completed,1u);
    EXPECT_EQ(total,2u);
    std::string text;
    ASSERT_TRUE(ReadText(report,text));
    EXPECT_NE(text.find("\"cancelled\": true"),std::string::npos);
    EXPECT_EQ(text.find("\"pass_percent\""),std::string::npos);
    ASSERT_TRUE(ReadText(directory/"report.json.txt",text));
    EXPECT_NE(text.find("did not complete"),std::string::npos);
}
TEST_F(StorageTest, BaselineRegressionFailsEvenWhenConfiguredTargetPasses) {
    using namespace superior::benchmark;
    WritePgm(directory/"black.pgm",Image{8,8,std::vector<float>(64,0)});
    WritePgm(directory/"white.pgm",Image{8,8,std::vector<float>(64,1)});
    const auto manifest=directory/"manifest.json",report=directory/"report.json";
    ASSERT_TRUE(WriteTextAtomic(manifest,R"({"thresholds":{"target_percent":0},"baseline_percent":99,"frames":[{"truth":"black.pgm","generated":"white.pgm","source_a":"black.pgm","source_b":"black.pgm","content_class":"fast-motion"}]})"));
    EXPECT_EQ(Verify(manifest,report),2);
    std::string text;
    ASSERT_TRUE(ReadText(report,text));
    EXPECT_NE(text.find("\"target_achieved\": true"),std::string::npos);
    EXPECT_NE(text.find("\"baseline_met\": false"),std::string::npos);
    EXPECT_NE(text.find("\"accepted\": false"),std::string::npos);
    EXPECT_NE(text.find("\"fast-motion\""),std::string::npos);
}
TEST_F(StorageTest, ClassificationSummariesReflectRealCounts) {
    using namespace superior::benchmark;
    WritePgm(directory/"black.pgm",Image{8,8,std::vector<float>(64,0)});
    const auto manifest=directory/"manifest.json",report=directory/"report.json";
    ASSERT_TRUE(WriteTextAtomic(manifest,R"({"frames":[{"truth":"black.pgm","generated":"black.pgm","source_a":"black.pgm","source_b":"black.pgm","content_class":"hud"}]})"));
    size_t complete=0,total=0;
    VerifyControl control; control.progress=[&](size_t done,size_t count) { complete=done; total=count; };
    EXPECT_EQ(Verify(manifest,report,control),0);
    EXPECT_EQ(complete,1u); EXPECT_EQ(total,1u);
    std::string text;
    ASSERT_TRUE(ReadText(report,text));
    EXPECT_NE(text.find("\"content_classes\""),std::string::npos);
    EXPECT_NE(text.find("\"hud\""),std::string::npos);
    EXPECT_NE(text.find("\"mean_ssim\": 1.0"),std::string::npos);
}
TEST_F(StorageTest, OversizedPgmIsRejectedBeforeAllocation) {
    using namespace superior::benchmark;
    ASSERT_TRUE(WriteTextAtomic(directory/"huge.pgm","P5\n8192 8192\n255\n"));
    EXPECT_THROW(ReadPgm(directory/"huge.pgm"),std::runtime_error);
    ASSERT_TRUE(WriteTextAtomic(directory/"small.pgm","P2\n1 1\n255\n0"));
    EXPECT_ANY_THROW(ReadPgm(directory/"small.pgm",[]{return true;}));
}
TEST(Quality, CompareCanBeCancelledDuringMetrics) {
    using namespace superior::benchmark;
    Image image{32,32,std::vector<float>(1024,.5f)};
    unsigned checks=0;
    EXPECT_ANY_THROW(Compare(image,image,image,image,[&]{ return ++checks>3; }));
}
TEST_F(StorageTest, EarlyCancellationCannotOverwriteUnvalidatedInputs) {
    using namespace superior::benchmark;
    const auto manifest=directory/"manifest.json",input=directory/"frame.pgm";
    ASSERT_TRUE(WriteTextAtomic(manifest,R"({"frames":[{"truth":"frame.pgm","generated":"frame.pgm","source_a":"frame.pgm","source_b":"frame.pgm"}]})"));
    const std::string bytes="P2\n1 1\n255\n0";
    ASSERT_TRUE(WriteTextAtomic(input,bytes));
    VerifyControl control; control.cancelled=[] { return true; };
    EXPECT_EQ(Verify(manifest,input,control),3);
    std::string preserved;
    ASSERT_TRUE(ReadText(input,preserved));
    EXPECT_EQ(preserved,bytes);
}

#include "present/session_stats.h"
TEST(Session, NotAvailableUntilStartedAndRestartsAreClean) {
    using namespace superior::present;
    SessionStats stats;
    EXPECT_FALSE(stats.Snapshot(0).available);
    EXPECT_FALSE(stats.Schedule(false));
    EXPECT_THROW(SessionJson(stats.Snapshot(0)),std::invalid_argument);
    stats.Start(100,16);
    stats.Receive(true); stats.Schedule(true);
    stats.Stop(200);
    stats.Start(300,8);
    const auto s=stats.Snapshot(400);
    EXPECT_EQ(s.received,0u); EXPECT_EQ(s.generated.scheduled,0u);
    EXPECT_DOUBLE_EQ(s.elapsedMs,100);
    EXPECT_TRUE(s.running); EXPECT_TRUE(s.Consistent());
}
TEST(Session, AllPresentOutcomesAreDistinctAndConserved) {
    using namespace superior::present;
    SessionStats stats; stats.Start(0,16);
    for(int i=0;i<4;++i) ASSERT_TRUE(stats.Schedule(true));
    EXPECT_TRUE(stats.Complete(true,Outcome::Submitted,0,0,1));
    EXPECT_TRUE(stats.Complete(true,Outcome::Occluded,16,16,17));
    EXPECT_TRUE(stats.Complete(true,Outcome::OtherStatus,32,32,33));
    EXPECT_TRUE(stats.Complete(true,Outcome::Failed,48,48,49));
    EXPECT_FALSE(stats.Complete(true,Outcome::Submitted,64,64,65));
    const auto s=stats.Snapshot(100);
    EXPECT_EQ(s.generated.scheduled,4u); EXPECT_EQ(s.generated.submitted,1u);
    EXPECT_EQ(s.generated.occluded,1u); EXPECT_EQ(s.generated.otherStatus,1u);
    EXPECT_EQ(s.generated.failed,1u); EXPECT_EQ(s.generated.pending,0u);
    EXPECT_TRUE(s.Consistent());
}
TEST(Session, DiscardReasonsAndStopDrainAllPendingWork) {
    using namespace superior::present;
    SessionStats stats; stats.Start(0,16);
    for(int i=0;i<3;++i) stats.Schedule(true);
    stats.Schedule(false); stats.Schedule(false);
    EXPECT_TRUE(stats.Discard(true,DropReason::NewCapture));
    EXPECT_TRUE(stats.Discard(true,DropReason::Pause));
    EXPECT_TRUE(stats.Discard(false,DropReason::Resize));
    stats.Stop(100);
    const auto s=stats.Snapshot(1000);
    EXPECT_EQ(s.replaced,1u); EXPECT_EQ(s.droppedOnPause,1u); EXPECT_EQ(s.droppedOnResize,1u);
    EXPECT_EQ(s.droppedOnStop,2u); EXPECT_EQ(s.generated.pending+s.real.pending,0u);
    EXPECT_TRUE(s.Consistent());
    EXPECT_FALSE(stats.Discard(false,DropReason::Stop));
    EXPECT_FALSE(stats.Schedule(true));
}
TEST(Session, PausesFreezeActivityAndDoNotBridgeReturnIntervals) {
    using namespace superior::present;
    SessionStats stats; stats.Start(10,16);
    stats.Schedule(false); stats.Complete(false,Outcome::Submitted,20,20,21);
    stats.Pause(true,110);
    auto mid=stats.Snapshot(210);
    EXPECT_DOUBLE_EQ(mid.activeMs,100); EXPECT_DOUBLE_EQ(mid.pausedMs,100);
    EXPECT_FALSE(stats.Schedule(false));
    stats.Receive(false);
    stats.Pause(false,310);
    stats.Receive(true);
    stats.Schedule(false); stats.Complete(false,Outcome::Submitted,320,320,321);
    stats.Stop(1010);
    const auto s=stats.Snapshot(9999);
    EXPECT_DOUBLE_EQ(s.elapsedMs,1000); EXPECT_DOUBLE_EQ(s.pausedMs,200); EXPECT_DOUBLE_EQ(s.activeMs,800);
    EXPECT_EQ(s.returnIntervalMs.samples,0u);
    EXPECT_EQ(s.received,2u); EXPECT_EQ(s.accepted,1u); EXPECT_EQ(s.ignoredWhilePaused,1u);
    EXPECT_TRUE(s.Consistent());
}
TEST(Session, CpuWallTimeAndLatenessAreNotScanoutMeasurements) {
    using namespace superior::present;
    SessionStats stats; stats.Start(0,16);
    stats.Schedule(false); stats.Complete(false,Outcome::Submitted,10,12,15);
    stats.Schedule(true); stats.Complete(true,Outcome::Submitted,26,26,31);
    const auto s=stats.Snapshot(32);
    EXPECT_DOUBLE_EQ(s.callWallMs.Mean(),4); EXPECT_DOUBLE_EQ(s.callWallMs.maximum,5);
    EXPECT_DOUBLE_EQ(s.lateStartMs.Mean(),1); EXPECT_DOUBLE_EQ(s.lateStartMs.maximum,2);
    EXPECT_DOUBLE_EQ(s.returnIntervalMs.Mean(),16);
    const auto json=SessionJson(s);
    EXPECT_NE(json.find("\"scanout_fps\": null"),std::string::npos);
    EXPECT_NE(json.find("\"ground_truth_pass_rate\": null"),std::string::npos);
}
TEST(Session, UnknownOutcomeAndClockRegressionsDoNotCorruptCounters) {
    using namespace superior::present;
    SessionStats stats; stats.Start(100,16); stats.Schedule(false);
    EXPECT_FALSE(stats.Complete(false,static_cast<Outcome>(99),100,100,101));
    EXPECT_EQ(stats.Snapshot(102).real.pending,1u);
    stats.Pause(true,200); stats.Pause(false,150);
    EXPECT_TRUE(stats.Snapshot(150).paused);
    stats.Stop(150); EXPECT_TRUE(stats.Snapshot(250).running);
    stats.Stop(300); EXPECT_TRUE(stats.Snapshot(400).Consistent());
    EXPECT_THROW(stats.Start(std::numeric_limits<double>::quiet_NaN(),16),std::invalid_argument);
}
TEST_F(StorageTest, SessionReportsRejectUnavailableAndInconsistentSnapshots) {
    using namespace superior::present;
    const auto report=directory/"session.json";
    EXPECT_FALSE(SaveSessionReport(report,{}));
    SessionStats stats; stats.Start(0,16); stats.Receive(true); stats.Schedule(false); stats.Stop(20);
    ASSERT_TRUE(SaveSessionReport(report,stats.Snapshot(20)));
    std::string text; ASSERT_TRUE(ReadText(report,text));
    EXPECT_NE(text.find("\"submitted_s_ok\": 0"),std::string::npos);
    EXPECT_NE(text.find("\"render_and_present_call_wall_ms\": null"),std::string::npos);
    auto bad=stats.Snapshot(20); ++bad.real.scheduled;
    EXPECT_FALSE(SaveSessionReport(report,bad));
    std::string after; ASSERT_TRUE(ReadText(report,after)); EXPECT_EQ(after,text);
}
TEST(Session, SyntheticOperationSequenceAlwaysPreservesAccounting) {
    using namespace superior::present;
    SessionStats stats; stats.Start(0,16);
    uint32_t state=12345;
    for(int i=0;i<3000;++i) {
        state=state*1664525u+1013904223u;
        const bool generated=(state & 1)!=0;
        switch((state>>1)%4) {
            case 0: stats.Schedule(generated); break;
            case 1: stats.Complete(generated,Outcome::Submitted,i,i,i+1); break;
            case 2: stats.Discard(generated,DropReason::NewCapture); break;
            case 3: stats.Receive((state & 8)!=0); break;
        }
        ASSERT_TRUE(stats.Snapshot(i+1).Consistent());
    }
    stats.Stop(4000); EXPECT_TRUE(stats.Snapshot(5000).Consistent());
}
TEST(Session, StopWhilePausedFreezesElapsedAndPausedTime) {
    using namespace superior::present;
    SessionStats stats; stats.Start(0,16); stats.Pause(true,10); stats.Stop(30);
    const auto s=stats.Snapshot(1000);
    EXPECT_DOUBLE_EQ(s.elapsedMs,30); EXPECT_DOUBLE_EQ(s.pausedMs,20); EXPECT_DOUBLE_EQ(s.activeMs,10);
    EXPECT_FALSE(s.paused); EXPECT_FALSE(s.running); EXPECT_TRUE(s.Consistent());
}
TEST(Session, NonfiniteMeasurementDoesNotInventTimingSamples) {
    using namespace superior::present;
    SessionStats stats; stats.Start(0,16); stats.Schedule(false);
    const double missing=std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(stats.Complete(false,Outcome::Submitted,missing,missing,missing));
    const auto s=stats.Snapshot(10);
    EXPECT_EQ(s.real.submitted,1u); EXPECT_EQ(s.callWallMs.samples,0u);
    EXPECT_EQ(s.lateStartMs.samples,0u); EXPECT_EQ(s.returnIntervalMs.samples,0u);
    EXPECT_TRUE(s.Consistent());
}

#include "filters/parameters.h"
TEST(Filters, IndependentBypassUsesNeutralValuesWithoutErasingSliders) {
    Settings s;
    s.sharpness=.8f; s.contrast=1.5f; s.saturation=1.6f;
    s.brightness=.2f; s.gamma=1.4f; s.vibrance=.7f; s.temperature=.8f; s.aaStrength=.6f;
    s.sharpnessEnabled=s.contrastEnabled=s.saturationEnabled=false;
    s.brightnessEnabled=s.gammaEnabled=s.vibranceEnabled=s.temperatureEnabled=false;
    s.antiAlias=false;
    const auto p=filters::Effective(s);
    EXPECT_FLOAT_EQ(p.sharpness,0); EXPECT_FLOAT_EQ(p.contrast,1);
    EXPECT_FLOAT_EQ(p.saturation,1); EXPECT_FLOAT_EQ(p.brightness,0);
    EXPECT_FLOAT_EQ(p.gamma,1); EXPECT_FLOAT_EQ(p.vibrance,0);
    EXPECT_FLOAT_EQ(p.temperature,0); EXPECT_FLOAT_EQ(p.aaStrength,0);
    EXPECT_FLOAT_EQ(s.temperature,.8f); EXPECT_FLOAT_EQ(s.contrast,1.5f);
    s.temperatureEnabled=true; s.antiAlias=true;
    EXPECT_FLOAT_EQ(filters::Effective(s).temperature,.8f);
    EXPECT_FLOAT_EQ(filters::Effective(s).aaStrength,.6f);
}
TEST(Filters, NewControlsPersistAndClampWithBackwardCompatibleDefaults) {
    Settings s;
    ASSERT_TRUE(FromJson(R"({"temperature":-9,"aa_strength":4,"temperature_enabled":false,"contrast_enabled":false,"sharpness_enabled":false,"saturation_enabled":false,"brightness_enabled":false,"gamma_enabled":false,"vibrance_enabled":false})",s));
    EXPECT_FLOAT_EQ(s.temperature,-1); EXPECT_FLOAT_EQ(s.aaStrength,1);
    Settings copy; ASSERT_TRUE(FromJson(ToJson(s),copy));
    EXPECT_FALSE(copy.temperatureEnabled); EXPECT_FALSE(copy.contrastEnabled);
    EXPECT_FALSE(copy.sharpnessEnabled); EXPECT_FALSE(copy.saturationEnabled);
    EXPECT_FALSE(copy.brightnessEnabled); EXPECT_FALSE(copy.gammaEnabled); EXPECT_FALSE(copy.vibranceEnabled);
    EXPECT_FLOAT_EQ(copy.temperature,-1); EXPECT_FLOAT_EQ(copy.aaStrength,1);
    Settings legacy; ASSERT_TRUE(FromJson("{}",legacy));
    EXPECT_TRUE(legacy.sharpnessEnabled); EXPECT_TRUE(legacy.temperatureEnabled);
    EXPECT_FLOAT_EQ(legacy.temperature,0); EXPECT_FLOAT_EQ(legacy.aaStrength,1);
}
TEST(Filters, GameSettingsCopyIncludesFilterBypassesAndTemperature) {
    Settings source,destination;
    source.temperature=-.6f; source.aaStrength=.3f;
    source.sharpnessEnabled=source.contrastEnabled=source.saturationEnabled=false;
    source.brightnessEnabled=source.gammaEnabled=source.vibranceEnabled=source.temperatureEnabled=false;
    destination.language="ar";
    CopyGameSettings(destination,source);
    EXPECT_FLOAT_EQ(destination.temperature,-.6f); EXPECT_FLOAT_EQ(destination.aaStrength,.3f);
    EXPECT_FALSE(destination.sharpnessEnabled); EXPECT_FALSE(destination.contrastEnabled);
    EXPECT_FALSE(destination.saturationEnabled); EXPECT_FALSE(destination.brightnessEnabled);
    EXPECT_FALSE(destination.gammaEnabled); EXPECT_FALSE(destination.vibranceEnabled); EXPECT_FALSE(destination.temperatureEnabled);
    EXPECT_EQ(destination.language,"ar");
}
TEST(Filters, ImagePresetsDoNotModifyIndependentOledOrAA) {
    for(int i=0;i<4;++i) {
        Settings s; s.oledLevel=3; s.oledIntensity=.4f; s.antiAlias=false; s.aaStrength=.2f;
        filters::Apply(static_cast<filters::Preset>(i),s);
        EXPECT_EQ(s.oledLevel,3); EXPECT_FLOAT_EQ(s.oledIntensity,.4f);
        EXPECT_FALSE(s.antiAlias); EXPECT_FLOAT_EQ(s.aaStrength,.2f);
    }
}
TEST(Filters, TemperatureReferenceIsNeutralAndWarmCoolHaveOppositeGains) {
    const filters::Rgb input{.4f,.5f,.6f};
    const auto neutral=filters::Temperature(input,0);
    EXPECT_FLOAT_EQ(neutral.r,input.r); EXPECT_FLOAT_EQ(neutral.g,input.g); EXPECT_FLOAT_EQ(neutral.b,input.b);
    const auto warm=filters::Temperature(input,1), cool=filters::Temperature(input,-1);
    EXPECT_GT(warm.r,input.r); EXPECT_LT(warm.b,input.b); EXPECT_FLOAT_EQ(warm.g,input.g);
    EXPECT_LT(cool.r,input.r); EXPECT_GT(cool.b,input.b);
    const auto clipped=filters::Temperature({1,1,1},1);
    EXPECT_FLOAT_EQ(clipped.r,1);
}
TEST(Oled, ZeroIntensityIsExactIdentityAtEveryActiveLevel) {
    for(int level=1;level<=3;++level)
        for(int i=0;i<=1000;++i) {
            const float y=i/1000.f;
            EXPECT_FLOAT_EQ(OledCurve(y,level,0),y);
        }
}

#include "core/settings_bundle.h"
#include <nlohmann/json.hpp>
TEST(Bundle, RoundTripPreservesLocalPreferencesAndCopiesAllPortableValues) {
    Settings source,destination;
    source.temperature=-.7f; source.aaStrength=.4f; source.sharpnessEnabled=false;
    source.oledLevel=3; source.oledIntensity=.25f; source.splitPreview=true;
    source.frameGen=false; source.maxMultiplier=4;
    destination.language="ar"; destination.autoProfiles=false;
    const auto localKeys=destination.hotkeys;
    std::string error;
    ASSERT_TRUE(ImportSettingsBundle(ExportSettingsBundle(source),destination,error)) << error;
    EXPECT_TRUE(error.empty()); EXPECT_EQ(destination.language,"ar"); EXPECT_FALSE(destination.autoProfiles);
    for(size_t i=0;i<localKeys.size();++i) {
        EXPECT_EQ(destination.hotkeys[i].key,localKeys[i].key);
        EXPECT_EQ(destination.hotkeys[i].modifiers,localKeys[i].modifiers);
    }
    EXPECT_FLOAT_EQ(destination.temperature,-.7f); EXPECT_FLOAT_EQ(destination.aaStrength,.4f);
    EXPECT_FALSE(destination.sharpnessEnabled); EXPECT_EQ(destination.oledLevel,3);
    EXPECT_FLOAT_EQ(destination.oledIntensity,.25f); EXPECT_TRUE(destination.splitPreview);
    EXPECT_FALSE(destination.frameGen); EXPECT_EQ(destination.maxMultiplier,4);
}
TEST(Bundle, ExportContainsNoLocalIdentityOrShortcuts) {
    const auto root=nlohmann::json::parse(ExportSettingsBundle(Settings{}));
    EXPECT_EQ(root.at("format"),"superior-settings"); EXPECT_EQ(root.at("version"),2);
    const auto& settings=root.at("settings");
    EXPECT_FALSE(settings.contains("hotkeys")); EXPECT_FALSE(settings.contains("language"));
    EXPECT_FALSE(settings.contains("auto_profiles"));
}
TEST(Bundle, InvalidInputsNeverPartiallyMutateDestination) {
    Settings destination; destination.language="ar"; destination.temperature=.6f;
    const auto original=ToJson(destination);
    auto valid=nlohmann::json::parse(ExportSettingsBundle(Settings{}));
    std::vector<std::string> invalid={"", "[]", "{}", "{bad", std::string(65537,'x')};
    auto v=valid; v["version"]=3; invalid.push_back(v.dump());
    v=valid; v["version"]=1.5; invalid.push_back(v.dump());
    v=valid; v["settings"].erase("contrast"); invalid.push_back(v.dump());
    v=valid; v["settings"]["language"]="en"; invalid.push_back(v.dump());
    v=valid; v["settings"]["temperature"]="warm"; invalid.push_back(v.dump());
    v=valid; v["settings"]["frame_gen"]=1; invalid.push_back(v.dump());
    v=valid; v["settings"]["oled_level"]=1.5; invalid.push_back(v.dump());
    v=valid; v["settings"]["oled_level"]=9223372036854775807LL; invalid.push_back(v.dump());
    v=valid; v["settings"]["gamma"]=nullptr; invalid.push_back(v.dump());
    v=valid; v["extra"]=true; invalid.push_back(v.dump());
    for(const auto& input:invalid) {
        std::string error;
        EXPECT_FALSE(ImportSettingsBundle(input,destination,error));
        EXPECT_FALSE(error.empty()); EXPECT_EQ(ToJson(destination),original);
    }
}
TEST(Bundle, NumericRangesAreClampedAndFailureErrorClearsOnSuccess) {
    auto root=nlohmann::json::parse(ExportSettingsBundle(Settings{}));
    root["settings"]["temperature"]=12; root["settings"]["aa_strength"]=-1;
    Settings destination; std::string error="old error";
    ASSERT_TRUE(ImportSettingsBundle(root.dump(),destination,error));
    EXPECT_TRUE(error.empty()); EXPECT_FLOAT_EQ(destination.temperature,1); EXPECT_FLOAT_EQ(destination.aaStrength,0);
}
TEST_F(StorageTest, BundleFilesRoundTripAndRejectOversizedOrMissingFiles) {
    const auto path=directory/"shared-settings.json";
    Settings source,destination; source.temperature=.75f;
    ASSERT_TRUE(SaveSettingsBundle(path,source));
    std::string error;
    ASSERT_TRUE(LoadSettingsBundle(path,destination,error)); EXPECT_FLOAT_EQ(destination.temperature,.75f);
    const auto before=ToJson(destination);
    EXPECT_FALSE(LoadSettingsBundle(directory/"missing.json",destination,error));
    EXPECT_EQ(ToJson(destination),before);
    ASSERT_TRUE(WriteTextAtomic(path,std::string(65537,'x')));
    EXPECT_FALSE(LoadSettingsBundle(path,destination,error)); EXPECT_EQ(ToJson(destination),before);
}
TEST_F(StorageTest, BundleFailedSaveDoesNotModifyExistingFile) {
    const auto path=directory/"shared.json";
    ASSERT_TRUE(SaveSettingsBundle(path,Settings{}));
    std::string original; ASSERT_TRUE(ReadText(path,original));
    EXPECT_FALSE(SaveSettingsBundle(directory/"missing"/"shared.json",Settings{}));
    std::string after; ASSERT_TRUE(ReadText(path,after)); EXPECT_EQ(original,after);
}

TEST(Bundle, ExcessiveNestingIsRejectedWithoutMutation) {
    Settings destination; const auto before=ToJson(destination); std::string error;
    const std::string nested=std::string(2000,'[')+"0"+std::string(2000,']');
    EXPECT_FALSE(ImportSettingsBundle(nested,destination,error));
    EXPECT_EQ(ToJson(destination),before); EXPECT_FALSE(error.empty());
}

#include "filters/color_lut.h"
TEST(ColorLut, IdentityTrilinearSamplingPreservesGridAndOffGridColors) {
    const auto lut=filters::BuildLook(0);
    for(int i=0;i<=200;++i) {
        const filters::Rgb c{i/200.f,(200-i)/200.f,(i%37)/36.f};
        const auto result=filters::SampleLook(lut,c,1);
        EXPECT_NEAR(result.r,c.r,2e-6f); EXPECT_NEAR(result.g,c.g,2e-6f); EXPECT_NEAR(result.b,c.b,2e-6f);
    }
}
TEST(ColorLut, GridLayoutIsRedFastestWithCorrectCorners) {
    const auto lut=filters::BuildLook(0);
    EXPECT_EQ(filters::LutIndex(1,0,0),1u);
    EXPECT_EQ(filters::LutIndex(0,1,0),17u);
    EXPECT_EQ(filters::LutIndex(0,0,1),289u);
    EXPECT_FLOAT_EQ(lut.front().r,0); EXPECT_FLOAT_EQ(lut.front().g,0); EXPECT_FLOAT_EQ(lut.front().b,0);
    EXPECT_FLOAT_EQ(lut.back().r,1); EXPECT_FLOAT_EQ(lut.back().g,1); EXPECT_FLOAT_EQ(lut.back().b,1);
    EXPECT_FLOAT_EQ(lut[filters::LutIndex(16,0,0)].r,1);
    EXPECT_FLOAT_EQ(lut[filters::LutIndex(16,0,0)].g,0);
}
TEST(ColorLut, EveryLookHasFiniteBoundedVoxelsAndMatchesItsGridReference) {
    for(int look=1;look<=filters::kLookCount;++look) {
        const auto lut=filters::BuildLook(look);
        for(int b=0;b<17;++b) for(int g=0;g<17;++g) for(int r=0;r<17;++r) {
            const auto& voxel=lut[filters::LutIndex(r,g,b)];
            EXPECT_TRUE(std::isfinite(voxel.r)); EXPECT_TRUE(std::isfinite(voxel.g)); EXPECT_TRUE(std::isfinite(voxel.b));
            EXPECT_GE(voxel.r,0); EXPECT_LE(voxel.r,1); EXPECT_GE(voxel.g,0); EXPECT_LE(voxel.g,1);
            EXPECT_GE(voxel.b,0); EXPECT_LE(voxel.b,1); EXPECT_FLOAT_EQ(voxel.a,1);
            const auto expected=filters::LookReference({r/16.f,g/16.f,b/16.f},look);
            EXPECT_FLOAT_EQ(voxel.r,expected.r); EXPECT_FLOAT_EQ(voxel.g,expected.g); EXPECT_FLOAT_EQ(voxel.b,expected.b);
        }
    }
}
TEST(ColorLut, StrengthZeroBypassesAndHalfStrengthBlendsExactly) {
    const auto lut=filters::BuildLook(2);
    const filters::Rgb c{.3f,.4f,.5f};
    const auto bypass=filters::SampleLook(lut,c,0), full=filters::SampleLook(lut,c,1), half=filters::SampleLook(lut,c,.5f);
    EXPECT_FLOAT_EQ(bypass.r,c.r); EXPECT_FLOAT_EQ(bypass.g,c.g); EXPECT_FLOAT_EQ(bypass.b,c.b);
    EXPECT_NEAR(half.r,(c.r+full.r)*.5f,1e-6f); EXPECT_NEAR(half.g,(c.g+full.g)*.5f,1e-6f);
    EXPECT_NEAR(half.b,(c.b+full.b)*.5f,1e-6f);
}
TEST(ColorLut, MonochromeRemovesChromaAndUnknownLookIsRejected) {
    const auto lut=filters::BuildLook(4);
    const auto result=filters::SampleLook(lut,{.83f,.24f,.61f},1);
    EXPECT_NEAR(result.r,result.g,1e-6f); EXPECT_NEAR(result.g,result.b,1e-6f);
    EXPECT_THROW(filters::BuildLook(-1),std::invalid_argument);
    EXPECT_THROW(filters::BuildLook(5),std::invalid_argument);
}
TEST(ColorLut, SettingsRoundTripClampAndGameProfileCopy) {
    Settings s;
    ASSERT_TRUE(FromJson(R"({"color_look":40,"color_look_strength":-3,"color_look_enabled":false})",s));
    EXPECT_EQ(s.colorLook,4); EXPECT_FLOAT_EQ(s.colorLookStrength,0); EXPECT_FALSE(s.colorLookEnabled);
    Settings copy;
    ASSERT_TRUE(FromJson(ToJson(s),copy));
    EXPECT_EQ(copy.colorLook,4); EXPECT_FLOAT_EQ(copy.colorLookStrength,0); EXPECT_FALSE(copy.colorLookEnabled);
    copy=Settings{}; CopyGameSettings(copy,s);
    EXPECT_EQ(copy.colorLook,4); EXPECT_FALSE(copy.colorLookEnabled); EXPECT_FLOAT_EQ(copy.colorLookStrength,0);
    Settings legacy; ASSERT_TRUE(FromJson("{}",legacy)); EXPECT_EQ(legacy.colorLook,0);
}
TEST(Bundle, V2PreservesLookAndV1MigratesToDisabledLook) {
    Settings source,destination; source.colorLook=3; source.colorLookStrength=.6f; source.colorLookEnabled=false;
    std::string error;
    ASSERT_TRUE(ImportSettingsBundle(ExportSettingsBundle(source),destination,error));
    EXPECT_EQ(destination.colorLook,3); EXPECT_FLOAT_EQ(destination.colorLookStrength,.6f); EXPECT_FALSE(destination.colorLookEnabled);
    auto legacy=nlohmann::json::parse(ExportSettingsBundle(source));
    legacy["version"]=1;
    legacy["settings"].erase("color_look"); legacy["settings"].erase("color_look_enabled"); legacy["settings"].erase("color_look_strength");
    ASSERT_TRUE(ImportSettingsBundle(legacy.dump(),destination,error));
    EXPECT_EQ(destination.colorLook,0); EXPECT_FLOAT_EQ(destination.colorLookStrength,1); EXPECT_TRUE(destination.colorLookEnabled);
    legacy["settings"]["color_look"]=2;
    const auto before=ToJson(destination);
    EXPECT_FALSE(ImportSettingsBundle(legacy.dump(),destination,error)); EXPECT_EQ(ToJson(destination),before);
}
TEST(ColorLut, ImagePresetsPreserveIndependentLookSettings) {
    for(int i=0;i<4;++i) {
        Settings s; s.colorLook=2; s.colorLookStrength=.35f; s.colorLookEnabled=false;
        filters::Apply(static_cast<filters::Preset>(i),s);
        EXPECT_EQ(s.colorLook,2); EXPECT_FLOAT_EQ(s.colorLookStrength,.35f); EXPECT_FALSE(s.colorLookEnabled);
    }
}

#include "core/image_comparison.h"
TEST(Comparison, VerticalHorizontalReverseAndDisabledRegions) {
    Settings s; s.splitPreview=true; s.previewPosition=.3f;
    EXPECT_TRUE(PreviewOriginalAt(s,.2f,.8f)); EXPECT_FALSE(PreviewOriginalAt(s,.4f,.1f));
    EXPECT_FALSE(PreviewOriginalAt(s,.3f,.1f));
    s.previewAxis=1;
    EXPECT_FALSE(PreviewOriginalAt(s,.2f,.8f)); EXPECT_TRUE(PreviewOriginalAt(s,.4f,.1f));
    s.previewReverse=true;
    EXPECT_TRUE(PreviewOriginalAt(s,.2f,.8f)); EXPECT_FALSE(PreviewOriginalAt(s,.4f,.1f));
    s.splitPreview=false; EXPECT_FALSE(PreviewOriginalAt(s,.9f,.9f));
}
TEST(Comparison, EndpointPositionsSelectWholeImage) {
    Settings s; s.splitPreview=true; s.previewPosition=0;
    EXPECT_FALSE(PreviewOriginalAt(s,.001f,.5f)); EXPECT_FALSE(PreviewOriginalAt(s,.999f,.5f));
    s.previewPosition=1;
    EXPECT_TRUE(PreviewOriginalAt(s,.001f,.5f)); EXPECT_TRUE(PreviewOriginalAt(s,.999f,.5f));
    s.previewReverse=true; EXPECT_FALSE(PreviewOriginalAt(s,.5f,.5f));
}
TEST(Comparison, LayoutClampRoundTripAndLegacyDefaults) {
    Settings s;
    ASSERT_TRUE(FromJson(R"({"preview_position":9,"preview_axis":5,"preview_reverse":true,"preview_line":false})",s));
    EXPECT_FLOAT_EQ(s.previewPosition,1); EXPECT_EQ(s.previewAxis,1); EXPECT_TRUE(s.previewReverse); EXPECT_FALSE(s.previewLine);
    Settings copy; ASSERT_TRUE(FromJson(ToJson(s),copy));
    EXPECT_FLOAT_EQ(copy.previewPosition,1); EXPECT_EQ(copy.previewAxis,1); EXPECT_TRUE(copy.previewReverse); EXPECT_FALSE(copy.previewLine);
    s.previewPosition=std::numeric_limits<float>::quiet_NaN(); s.Clamp(); EXPECT_FLOAT_EQ(s.previewPosition,.5f);
    Settings legacy; ASSERT_TRUE(FromJson("{}",legacy));
    EXPECT_FLOAT_EQ(legacy.previewPosition,.5f); EXPECT_EQ(legacy.previewAxis,0); EXPECT_FALSE(legacy.previewReverse); EXPECT_TRUE(legacy.previewLine);
}
TEST(Comparison, SlotsAreIndependentCopiesAndEmptySlotsNeverMutateSettings) {
    ImageComparisonSlots slots; Settings source,destination;
    const auto original=ToJson(destination);
    EXPECT_FALSE(slots.Apply(0,destination)); EXPECT_EQ(ToJson(destination),original);
    source.contrast=1.6f; source.colorLook=3; source.oledLevel=2;
    ASSERT_TRUE(slots.Capture(0,source)); source.contrast=.6f;
    ASSERT_TRUE(slots.Capture(1,source)); source.contrast=1;
    ASSERT_TRUE(slots.Apply(0,destination)); EXPECT_FLOAT_EQ(destination.contrast,1.6f);
    EXPECT_EQ(destination.colorLook,3); EXPECT_EQ(destination.oledLevel,2);
    ASSERT_TRUE(slots.Apply(1,destination)); EXPECT_FLOAT_EQ(destination.contrast,.6f);
    EXPECT_FALSE(slots.Capture(2,source)); EXPECT_FALSE(slots.Has(2)); EXPECT_FALSE(slots.Apply(2,destination));
    slots.Clear(); EXPECT_FALSE(slots.Has(0)); EXPECT_FALSE(slots.Has(1));
}
TEST(Comparison, ApplyingSlotPreservesSessionLocalPreferencesAndPreview) {
    ImageComparisonSlots slots; Settings source,destination;
    source.frameGen=true; source.comfortMode=0; source.maxMultiplier=2; source.temperature=.7f; source.antiAlias=false;
    ASSERT_TRUE(slots.Capture(0,source));
    destination.frameGen=false; destination.comfortMode=2; destination.maxMultiplier=4;
    destination.language="ar"; destination.autoProfiles=false; destination.splitPreview=true;
    destination.previewPosition=.25f; destination.previewAxis=1; destination.previewReverse=true; destination.previewLine=false;
    const auto keys=destination.hotkeys;
    ASSERT_TRUE(slots.Apply(0,destination));
    EXPECT_FALSE(destination.frameGen); EXPECT_EQ(destination.comfortMode,2); EXPECT_EQ(destination.maxMultiplier,4);
    EXPECT_EQ(destination.language,"ar"); EXPECT_FALSE(destination.autoProfiles); EXPECT_TRUE(destination.splitPreview);
    EXPECT_FLOAT_EQ(destination.previewPosition,.25f); EXPECT_EQ(destination.previewAxis,1); EXPECT_TRUE(destination.previewReverse); EXPECT_FALSE(destination.previewLine);
    EXPECT_FLOAT_EQ(destination.temperature,.7f); EXPECT_FALSE(destination.antiAlias);
    for(size_t i=0;i<keys.size();++i) {
        EXPECT_EQ(destination.hotkeys[i].key,keys[i].key); EXPECT_EQ(destination.hotkeys[i].modifiers,keys[i].modifiers);
    }
}
TEST(Comparison, GameSettingsCopyDoesNotReplaceLocalComparisonLayout) {
    Settings source,destination;
    destination.previewPosition=.8f; destination.previewAxis=1; destination.previewReverse=true; destination.previewLine=false;
    CopyGameSettings(destination,source);
    EXPECT_FLOAT_EQ(destination.previewPosition,.8f); EXPECT_EQ(destination.previewAxis,1);
    EXPECT_TRUE(destination.previewReverse); EXPECT_FALSE(destination.previewLine);
}
TEST(Bundle, ComparisonLayoutStaysLocalWithoutChangingV2Schema) {
    Settings source,destination; source.previewPosition=.1f; source.previewAxis=1;
    destination.previewPosition=.7f; destination.previewReverse=true; destination.previewLine=false;
    const auto text=ExportSettingsBundle(source);
    const auto values=nlohmann::json::parse(text).at("settings");
    EXPECT_FALSE(values.contains("preview_position")); EXPECT_FALSE(values.contains("preview_axis"));
    EXPECT_FALSE(values.contains("preview_reverse")); EXPECT_FALSE(values.contains("preview_line"));
    std::string error; ASSERT_TRUE(ImportSettingsBundle(text,destination,error));
    EXPECT_FLOAT_EQ(destination.previewPosition,.7f); EXPECT_EQ(destination.previewAxis,0);
    EXPECT_TRUE(destination.previewReverse); EXPECT_FALSE(destination.previewLine);
}

TEST(Comparison, DividerGuideUsesExactlyOneOutputPixelAndHidesAtEndpoints) {
    Settings s; s.splitPreview=true; s.previewPosition=.5f;
    int count=0; for(int x=0;x<1920;++x) if(PreviewGuideAt(s,x,100,1920,1080)) ++count;
    EXPECT_EQ(count,1); EXPECT_TRUE(PreviewGuideAt(s,960,100,1920,1080));
    s.previewAxis=1; count=0;
    for(int y=0;y<1080;++y) if(PreviewGuideAt(s,100,y,1920,1080)) ++count;
    EXPECT_EQ(count,1);
    s.previewPosition=1; EXPECT_FALSE(PreviewGuideAt(s,100,1079,1920,1080));
    s.previewPosition=0; EXPECT_FALSE(PreviewGuideAt(s,100,0,1920,1080));
    s.previewPosition=.5f; s.previewLine=false; EXPECT_FALSE(PreviewGuideAt(s,100,540,1920,1080));
}

#include "core/diagnostics.h"
TEST(Diagnostics, UnknownMeasurementsAreNullNotZeroOrFakeClaims) {
    const auto report=nlohmann::json::parse(DiagnosticsJson({}));
    EXPECT_EQ(report.at("format"),"superior-diagnostics"); EXPECT_EQ(report.at("schema"),1);
    EXPECT_TRUE(report["gpu"]["device_removed_reason_hresult"].is_null());
    EXPECT_TRUE(report["gpu"]["feature_level"].is_null());
    EXPECT_TRUE(report["gpu"]["local_budget_bytes"].is_null());
    EXPECT_TRUE(report["gpu"]["last_completed_group_ms"].is_null());
    EXPECT_TRUE(report["gpu"]["temperature_c"].is_null()); EXPECT_TRUE(report["gpu"]["watts"].is_null());
    EXPECT_TRUE(report["gpu"]["whole_frame_gpu_ms"].is_null()); EXPECT_TRUE(report["power"]["on_battery"].is_null());
    EXPECT_TRUE(report["runtime"]["wgc_supported_probe"].is_null());
    EXPECT_TRUE(report["settings_bundle"].is_null()); EXPECT_TRUE(report["session"].is_null());
    EXPECT_EQ(report["session_status"],"not_requested"); EXPECT_FALSE(report["build_revision"].get<std::string>().empty());
}
TEST(Diagnostics, FalseAndZeroRemainKnownValues) {
    DiagnosticsSnapshot snapshot;
    snapshot.captureSupported=false; snapshot.onBattery=false; snapshot.batterySaver=false;
    snapshot.deviceRemovedReason=0; snapshot.gpuGroupMs=0; snapshot.budgetBytes=0; snapshot.usageBytes=0;
    snapshot.featureLevel=0xb000; snapshot.running=true; snapshot.captureActive=true;
    const auto report=nlohmann::json::parse(DiagnosticsJson(snapshot));
    EXPECT_EQ(report["runtime"]["wgc_supported_probe"],false); EXPECT_EQ(report["gpu"]["device_removed_reason_hresult"],0);
    EXPECT_EQ(report["gpu"]["local_budget_bytes"],0); EXPECT_EQ(report["gpu"]["last_completed_group_ms"],0);
    EXPECT_EQ(report["power"]["on_battery"],false); EXPECT_EQ(report["gpu"]["feature_level"],0xb000);
}
TEST(Diagnostics, NonfiniteOrNegativeTimingIsOmitted) {
    for(double value : {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-1.}) {
        DiagnosticsSnapshot snapshot; snapshot.gpuGroupMs=value;
        const auto report=nlohmann::json::parse(DiagnosticsJson(snapshot));
        EXPECT_TRUE(report["gpu"]["last_completed_group_ms"].is_null());
    }
}
TEST(Diagnostics, OptionalSettingsArePortableAndDoNotExposeLocalBindings) {
    DiagnosticsSnapshot snapshot; snapshot.settings.language="ar"; snapshot.settings.colorLook=3;
    const auto report=nlohmann::json::parse(DiagnosticsJson(snapshot,{true,false}));
    const auto& settings=report["settings_bundle"]["settings"];
    EXPECT_EQ(settings["color_look"],3); EXPECT_FALSE(settings.contains("hotkeys"));
    EXPECT_FALSE(settings.contains("language")); EXPECT_FALSE(settings.contains("preview_position"));
    EXPECT_FALSE(report["privacy"]["uploaded_automatically"].get<bool>());
    EXPECT_FALSE(report["privacy"]["contains_game_paths"].get<bool>());
    EXPECT_FALSE(report["privacy"]["contains_images"].get<bool>());
    EXPECT_FALSE(report["privacy"]["contains_logs"].get<bool>());
}
TEST(Diagnostics, SessionOptInRejectsInvalidSnapshotWithoutLosingReport) {
    DiagnosticsSnapshot snapshot;
    auto report=nlohmann::json::parse(DiagnosticsJson(snapshot,{false,true}));
    EXPECT_EQ(report["session_status"],"unavailable"); EXPECT_TRUE(report["session"].is_null());
    present::SessionStats stats; stats.Start(0,16); snapshot.session=stats.Snapshot(10);
    report=nlohmann::json::parse(DiagnosticsJson(snapshot,{false,true}));
    EXPECT_EQ(report["session_status"],"included"); EXPECT_TRUE(report["session"]["scanout_fps"].is_null());
    ++snapshot.session.real.scheduled;
    report=nlohmann::json::parse(DiagnosticsJson(snapshot,{false,true}));
    EXPECT_EQ(report["session_status"],"invalid_snapshot_omitted"); EXPECT_TRUE(report["session"].is_null());
}
TEST(Diagnostics, RemovedDeviceReasonAndFailureStageArePreserved) {
    DiagnosticsSnapshot snapshot; snapshot.deviceRemovedReason=-2005270523; snapshot.lastFailure=FailureStage::Runtime;
    const auto report=nlohmann::json::parse(DiagnosticsJson(snapshot));
    EXPECT_EQ(report["gpu"]["device_removed_reason_hresult"],-2005270523);
    EXPECT_EQ(report["runtime"]["last_failure_stage"],"runtime");
    EXPECT_STREQ(FailureStageName(FailureStage::StartCapture),"capture_start");
    EXPECT_STREQ(FailureStageName(FailureStage::UserInterface),"ui");
}
TEST_F(StorageTest, DiagnosticReportSaveIsReadableAndFailuresAreReported) {
    DiagnosticsSnapshot snapshot; snapshot.adapter="Synthetic test adapter";
    const auto path=directory/"diagnostics.json";
    ASSERT_TRUE(SaveDiagnosticsReport(path,snapshot));
    std::string text; ASSERT_TRUE(ReadText(path,text));
    EXPECT_EQ(nlohmann::json::parse(text)["gpu"]["adapter"],"Synthetic test adapter");
    EXPECT_FALSE(SaveDiagnosticsReport(directory/"missing"/"diagnostics.json",snapshot));
}

TEST(Performance, EmptyHistoryIsUnavailableAndInvalidSamplesDoNotCreateData) {
    PerformanceHistory history;
    EXPECT_FALSE(history.Describe(GpuScope::CaptureProcessing,0));
    EXPECT_FALSE(history.Add({0,GpuScope::CaptureProcessing,1},0));
    EXPECT_FALSE(history.Add({1,GpuScope::CaptureProcessing,-1},0));
    EXPECT_FALSE(history.Add({1,GpuScope::CaptureProcessing,std::numeric_limits<double>::infinity()},0));
    EXPECT_FALSE(history.Add({1,static_cast<GpuScope>(3),1},0));
    EXPECT_EQ(history.Count(GpuScope::CaptureProcessing),0u);
}
TEST(Performance, ScopeWindowsRemainSeparateAndPercentileUsesNearestRank) {
    PerformanceHistory history;
    for(uint64_t i=1;i<=20;++i) ASSERT_TRUE(history.Add({i,GpuScope::CaptureProcessing,double(i)},double(i)));
    ASSERT_TRUE(history.Add({21,GpuScope::FinalRendering,100},21));
    const auto processing=history.Describe(GpuScope::CaptureProcessing,100);
    ASSERT_TRUE(processing); EXPECT_EQ(processing->count,20u); EXPECT_DOUBLE_EQ(processing->mean,10.5);
    EXPECT_DOUBLE_EQ(processing->p95,19); EXPECT_DOUBLE_EQ(processing->maximum,20); EXPECT_DOUBLE_EQ(processing->ageMs,80);
    const auto final=history.Describe(GpuScope::FinalRendering,100);
    ASSERT_TRUE(final); EXPECT_EQ(final->count,1u); EXPECT_DOUBLE_EQ(final->mean,100);
}
TEST(Performance, RingIsBoundedAndReturnsChronologicalWindow) {
    PerformanceHistory history;
    for(uint64_t i=1;i<=300;++i) ASSERT_TRUE(history.Add({i,GpuScope::FinalRendering,double(i)},double(i)));
    EXPECT_EQ(history.Count(GpuScope::FinalRendering),PerformanceHistory::Capacity);
    EXPECT_EQ(history.At(GpuScope::FinalRendering,0).sequence,173u);
    EXPECT_EQ(history.At(GpuScope::FinalRendering,127).sequence,300u);
    const auto summary=history.Describe(GpuScope::FinalRendering,300);
    ASSERT_TRUE(summary); EXPECT_DOUBLE_EQ(summary->mean,236.5); EXPECT_DOUBLE_EQ(summary->maximum,300);
}
TEST(Performance, AgeIsCollectionAgeAndStaleAfterTwoSeconds) {
    PerformanceHistory history; ASSERT_TRUE(history.Add({1,GpuScope::CaptureProcessing,0},100));
    EXPECT_FALSE(history.Describe(GpuScope::CaptureProcessing,2100)->stale);
    EXPECT_TRUE(history.Describe(GpuScope::CaptureProcessing,2101)->stale);
    EXPECT_DOUBLE_EQ(history.Describe(GpuScope::CaptureProcessing,50)->ageMs,0);
    EXPECT_FALSE(history.Describe(GpuScope::CaptureProcessing,std::numeric_limits<double>::quiet_NaN()));
}
TEST(Performance, OutOfOrderSequenceAndRegressingClockAreRejectedWithoutMutation) {
    PerformanceHistory history; ASSERT_TRUE(history.Add({2,GpuScope::CaptureProcessing,3},100));
    EXPECT_FALSE(history.Add({1,GpuScope::FinalRendering,9},101));
    EXPECT_FALSE(history.Add({3,GpuScope::FinalRendering,9},99));
    EXPECT_EQ(history.Count(GpuScope::FinalRendering),0u);
    history.Clear(); EXPECT_EQ(history.Count(GpuScope::CaptureProcessing),0u);
    EXPECT_TRUE(history.Add({1,GpuScope::FinalRendering,1},0));
}
TEST(Diagnostics, ReportIncludesIndependentGpuWindowsWithExplicitAgeAndStaleness) {
    DiagnosticsSnapshot snapshot; snapshot.collectedNowMs=4000;
    ASSERT_TRUE(snapshot.performance.Add({1,GpuScope::CaptureProcessing,3},1000));
    const auto report=nlohmann::json::parse(DiagnosticsJson(snapshot));
    const auto& windows=report["gpu_group_sample_windows"];
    EXPECT_TRUE(windows["final_rendering"].is_null());
    EXPECT_EQ(windows["capture_processing"]["samples"],1);
    EXPECT_EQ(windows["capture_processing"]["collected_age_ms"],3000);
    EXPECT_EQ(windows["capture_processing"]["stale"],true);
}
