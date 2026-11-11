#include "Misc/AutomationTest.h"
#include "UEcineUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineUnitsFormatTest, "UEcine.Core.Units.Format", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineUnitsFormatTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::Units;

	TestEqual(TEXT("Whole numbers have no decimals"), *FormatNumber(180.0, 1).ToString(), TEXT("180"));
	TestEqual(TEXT("Decimals are kept up to the limit"), *FormatNumber(23.976, 3).ToString(), TEXT("23.976"));
	TestEqual(TEXT("Decimals beyond the limit are rounded"), *FormatNumber(2.84, 1).ToString(), TEXT("2.8"));
	TestEqual(TEXT("Large numbers are not grouped"), *FormatNumber(12800.0, 0).ToString(), TEXT("12800"));

	TestEqual(TEXT("Under a metre is centimetres"), *FormatDistance(350.0, EUEcineDistanceUnits::Metric).ToString(), TEXT("35 cm"));
	TestEqual(TEXT("Centimetres keep one decimal"), *FormatDistance(225.0, EUEcineDistanceUnits::Metric).ToString(), TEXT("22.5 cm"));
	TestEqual(TEXT("Metres under ten keep two decimals"), *FormatDistance(3048.0, EUEcineDistanceUnits::Metric).ToString(), TEXT("3.05 m"));
	TestEqual(TEXT("Metres from ten keep one decimal"), *FormatDistance(29811.9, EUEcineDistanceUnits::Metric).ToString(), TEXT("29.8 m"));

	TestEqual(TEXT("Under a foot is inches"), *FormatDistance(228.6, EUEcineDistanceUnits::Imperial).ToString(), TEXT("9\""));
	TestEqual(TEXT("Feet and inches"), *FormatDistance(1371.6, EUEcineDistanceUnits::Imperial).ToString(), TEXT("4' 6\""));
	TestEqual(TEXT("Whole feet show zero inches"), *FormatDistance(3048.0, EUEcineDistanceUnits::Imperial).ToString(), TEXT("10' 0\""));
	TestEqual(TEXT("Inches that round up to twelve carry into feet"), *FormatDistance(3040.0, EUEcineDistanceUnits::Imperial).ToString(), TEXT("10' 0\""));
	TestEqual(TEXT("A hundred feet and over is whole feet"), *FormatDistance(36576.0, EUEcineDistanceUnits::Imperial).ToString(), TEXT("120'"));

	// Rig positions can be behind or below their starting point.
	TestEqual(TEXT("A negative distance keeps its unit"), *FormatDistance(-5000.0, EUEcineDistanceUnits::Metric).ToString(), TEXT("-5 m"));
	TestEqual(TEXT("A negative imperial distance"), *FormatDistance(-1371.6, EUEcineDistanceUnits::Imperial).ToString(), TEXT("-4' 6\""));
	TestEqual(TEXT("Nothing is not negative"), *FormatDistance(-0.2, EUEcineDistanceUnits::Metric).ToString(), TEXT("0 cm"));

	return true;
}

#endif
