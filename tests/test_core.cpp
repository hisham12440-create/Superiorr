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
