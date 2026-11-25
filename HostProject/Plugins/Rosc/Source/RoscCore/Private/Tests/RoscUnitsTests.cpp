// Copyright 2026 Fiavaion. All rights reserved.

#include "Misc/AutomationTest.h"
#include "RoscUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscUnitsFormatTest, "Rosc.Core.Units.Format", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscUnitsFormatTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::Units;

	TestEqual(TEXT("Whole numbers have no decimals"), *FormatNumber(180.0, 1).ToString(), TEXT("180"));
	TestEqual(TEXT("Decimals are kept up to the limit"), *FormatNumber(23.976, 3).ToString(), TEXT("23.976"));
	TestEqual(TEXT("Decimals beyond the limit are rounded"), *FormatNumber(2.84, 1).ToString(), TEXT("2.8"));
	TestEqual(TEXT("Large numbers are not grouped"), *FormatNumber(12800.0, 0).ToString(), TEXT("12800"));

	TestEqual(TEXT("Under a metre is centimetres"), *FormatDistance(350.0, ERoscDistanceUnits::Metric).ToString(), TEXT("35 cm"));
	TestEqual(TEXT("Centimetres keep one decimal"), *FormatDistance(225.0, ERoscDistanceUnits::Metric).ToString(), TEXT("22.5 cm"));
	TestEqual(TEXT("Metres under ten keep two decimals"), *FormatDistance(3048.0, ERoscDistanceUnits::Metric).ToString(), TEXT("3.05 m"));
	TestEqual(TEXT("Metres from ten keep one decimal"), *FormatDistance(29811.9, ERoscDistanceUnits::Metric).ToString(), TEXT("29.8 m"));

	TestEqual(TEXT("Under a foot is inches"), *FormatDistance(228.6, ERoscDistanceUnits::Imperial).ToString(), TEXT("9\""));
	TestEqual(TEXT("Feet and inches"), *FormatDistance(1371.6, ERoscDistanceUnits::Imperial).ToString(), TEXT("4' 6\""));
	TestEqual(TEXT("Whole feet show zero inches"), *FormatDistance(3048.0, ERoscDistanceUnits::Imperial).ToString(), TEXT("10' 0\""));
	TestEqual(TEXT("Inches that round up to twelve carry into feet"), *FormatDistance(3040.0, ERoscDistanceUnits::Imperial).ToString(), TEXT("10' 0\""));
	TestEqual(TEXT("A hundred feet and over is whole feet"), *FormatDistance(36576.0, ERoscDistanceUnits::Imperial).ToString(), TEXT("120'"));

	// Rig positions can be behind or below their starting point.
	TestEqual(TEXT("A negative distance keeps its unit"), *FormatDistance(-5000.0, ERoscDistanceUnits::Metric).ToString(), TEXT("-5 m"));
	TestEqual(TEXT("A negative imperial distance"), *FormatDistance(-1371.6, ERoscDistanceUnits::Imperial).ToString(), TEXT("-4' 6\""));
	TestEqual(TEXT("Nothing is not negative"), *FormatDistance(-0.2, ERoscDistanceUnits::Metric).ToString(), TEXT("0 cm"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscUnitsParseTest, "Rosc.Core.Units.Parse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscUnitsParseTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::Units;
	const ERoscDistanceUnits Metric = ERoscDistanceUnits::Metric;
	const ERoscDistanceUnits Imperial = ERoscDistanceUnits::Imperial;

	const auto TestParse = [this](const TCHAR* Text, ERoscDistanceUnits Units, double ExpectedMillimetres)
	{
		const TOptional<double> Parsed = ParseDistance(Text, Units);
		if (TestTrue(FString::Printf(TEXT("'%s' is a distance"), Text), Parsed.IsSet()))
		{
			TestEqual(FString::Printf(TEXT("'%s' in millimetres"), Text), Parsed.GetValue(), ExpectedMillimetres, 1.e-6);
		}
	};

	TestParse(TEXT("3.2 m"), Metric, 3200.0);
	TestParse(TEXT("45 cm"), Metric, 450.0);
	TestParse(TEXT("45cm"), Metric, 450.0);
	TestParse(TEXT("1 m 20 cm"), Metric, 1200.0);
	TestParse(TEXT("12 mm"), Metric, 12.0);
	TestParse(TEXT("2.5"), Metric, 2500.0);
	TestParse(TEXT(" -5 m "), Metric, -5000.0);

	TestParse(TEXT("10' 6\""), Imperial, 3200.4);
	TestParse(TEXT("10'6\""), Imperial, 3200.4);
	TestParse(TEXT("10'"), Imperial, 3048.0);
	TestParse(TEXT("9\""), Imperial, 228.6);
	TestParse(TEXT("4' 6"), Imperial, 1371.6);
	TestParse(TEXT("10 ft 6 in"), Imperial, 3200.4);
	TestParse(TEXT("10"), Imperial, 3048.0);
	TestParse(TEXT("-4' 6\""), Imperial, -1371.6);

	// A unit that is written wins over the setting.
	TestParse(TEXT("3 m"), Imperial, 3000.0);
	TestParse(TEXT("10'"), Metric, 3048.0);

	// What is shown can be read back.
	for (const double Millimetres : { 0.0, 350.0, 3050.0, 29800.0, -5000.0 })
	{
		TestParse(*FormatDistance(Millimetres, Metric).ToString(), Metric, Millimetres);
	}
	for (const double Millimetres : { 228.6, 1371.6, 3048.0, 36576.0, -1371.6 })
	{
		TestParse(*FormatDistance(Millimetres, Imperial).ToString(), Imperial, Millimetres);
	}

	for (const TCHAR* NotADistance : { TEXT(""), TEXT("-"), TEXT("far"), TEXT("3 furlongs"), TEXT("3 m and a bit"), TEXT("3 4"), TEXT("nan"), TEXT("m"), TEXT("."), TEXT("1.2.3 m") })
	{
		TestFalse(FString::Printf(TEXT("'%s' is not a distance"), NotADistance), ParseDistance(NotADistance, Metric).IsSet());
	}
	return true;
}

#endif
