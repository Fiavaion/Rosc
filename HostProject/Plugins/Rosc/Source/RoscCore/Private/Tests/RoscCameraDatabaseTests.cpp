// Copyright 2026 Fiavaion. All rights reserved.

#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "RoscCameraDatabase.h"
#include "RoscOptics.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscDatabaseParseTest, "Rosc.Core.Database.Parse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscDatabaseParseTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"schema": 1,
		"bodies": [
			{ "id": "test-body", "manufacturer": "Acme", "model": "One", "mount": "PL", "baseIso": [800, 3200],
			  "formats": [
				{ "id": "open-gate", "name": "Open Gate", "sensorMm": [36.0, 24.0], "resolution": [6000, 4000] },
				{ "id": "film", "name": "No fixed resolution", "sensorMm": [24.89, 18.66] } ] }
		],
		"lensKits": [
			{ "id": "test-kit", "manufacturer": "Acme", "name": "Primes", "squeeze": 2.0, "imageCircleMm": 31.5, "irisBlades": 11,
			  "lenses": [
				{ "focalMm": 75, "tStop": 2.0, "closeFocusM": 0.9 },
				{ "focalMm": 24, "zoomToMm": 70, "tStop": 2.8, "closeFocusM": 0.6 },
				{ "focalMm": 35, "tStop": 1.8, "closeFocusM": 0.35 } ] }
		]
	})");

	FRoscCameraDatabase Database;
	TArray<FString> Errors;
	TestTrue(TEXT("A valid file loads without errors"), Database.AddFromJson(Json, TEXT("test.json"), Errors));
	TestEqual(TEXT("No errors reported"), Errors.Num(), 0);

	const FRoscCameraBody* Body = Database.FindBody(TEXT("test-body"));
	if (!TestNotNull(TEXT("The body is found by id"), Body))
	{
		return false;
	}
	TestEqual(TEXT("Display name"), *Body->GetDisplayName(), TEXT("Acme One"));
	TestEqual(TEXT("Base ISO count"), Body->BaseIsos.Num(), 2);
	TestEqual(TEXT("Format count"), Body->Formats.Num(), 2);

	const FRoscRecordingFormat* OpenGate = Body->FindFormat(TEXT("open-gate"));
	if (!TestNotNull(TEXT("The format is found by id"), OpenGate))
	{
		return false;
	}
	TestEqual(TEXT("Sensor width"), OpenGate->SensorWidthMm, 36.0);
	TestEqual(TEXT("Sensor height"), OpenGate->SensorHeightMm, 24.0);
	TestTrue(TEXT("Resolution"), OpenGate->Resolution == FIntPoint(6000, 4000));
	TestTrue(TEXT("A format without a resolution reads as zero"), Body->FindFormat(TEXT("film"))->Resolution == FIntPoint::ZeroValue);

	const FRoscLensKit* Kit = Database.FindLensKit(TEXT("test-kit"));
	if (!TestNotNull(TEXT("The lens kit is found by id"), Kit))
	{
		return false;
	}
	TestTrue(TEXT("A squeeze above 1 is anamorphic"), Kit->IsAnamorphic());
	TestEqual(TEXT("Minimum aperture defaults to T22"), Kit->MinApertureTStop, 22.0);
	TestEqual(TEXT("Lenses are sorted by focal length"), Kit->Lenses[0].FocalLengthMm, 24.0);
	TestTrue(TEXT("A lens with zoomToMm is a zoom"), Kit->Lenses[0].IsZoom());
	TestEqual(TEXT("Zoom display name"), *Kit->Lenses[0].GetDisplayName(), TEXT("24-70mm T2.8"));
	TestFalse(TEXT("A lens without zoomToMm is a prime"), Kit->Lenses[1].IsZoom());
	TestEqual(TEXT("Prime display name"), *Kit->Lenses[1].GetDisplayName(), TEXT("35mm T1.8"));

	// A 31.5 mm image circle covers Super 35 (31.1 mm diagonal) but not full frame (43.3 mm).
	TestTrue(TEXT("The kit covers a Super 35 sized format"), Kit->CoversFormat(*Body->FindFormat(TEXT("film"))));
	TestFalse(TEXT("The kit does not cover full frame"), Kit->CoversFormat(*OpenGate));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscDatabaseValidationTest, "Rosc.Core.Database.Validation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscDatabaseValidationTest::RunTest(const FString& Parameters)
{
	{
		FRoscCameraDatabase Database;
		TArray<FString> Errors;
		TestFalse(TEXT("Malformed JSON is rejected"), Database.AddFromJson(TEXT("{ not json"), TEXT("broken.json"), Errors));
		TestTrue(TEXT("The error names the file"), Errors.Num() == 1 && Errors[0].Contains(TEXT("broken.json")));
	}
	{
		FRoscCameraDatabase Database;
		TArray<FString> Errors;
		TestFalse(TEXT("An unknown schema version is rejected"), Database.AddFromJson(TEXT(R"({ "schema": 99, "bodies": [] })"), TEXT("future.json"), Errors));
		TestTrue(TEXT("The error names the schema field"), Errors.Num() == 1 && Errors[0].Contains(TEXT("schema")));
	}
	{
		// One good body, one with a bad sensor size, one lens kit with a lens missing its T-stop.
		const FString Json = TEXT(R"({
			"schema": 1,
			"bodies": [
				{ "id": "good", "manufacturer": "Acme", "model": "Good", "formats": [ { "id": "a", "name": "A", "sensorMm": [36.0, 24.0] } ] },
				{ "id": "bad", "manufacturer": "Acme", "model": "Bad", "formats": [ { "id": "a", "name": "A", "sensorMm": [36.0, -1.0] } ] }
			],
			"lensKits": [
				{ "id": "kit", "manufacturer": "Acme", "name": "Kit", "imageCircleMm": 46.0, "lenses": [ { "focalMm": 35, "closeFocusM": 0.3 } ] }
			]
		})");

		FRoscCameraDatabase Database;
		TArray<FString> Errors;
		TestFalse(TEXT("A file with invalid entries reports failure"), Database.AddFromJson(Json, TEXT("mixed.json"), Errors));
		TestNotNull(TEXT("The valid body is still added"), Database.FindBody(TEXT("good")));
		TestNull(TEXT("The invalid body is skipped"), Database.FindBody(TEXT("bad")));
		TestNull(TEXT("The invalid lens kit is skipped"), Database.FindLensKit(TEXT("kit")));
		TestEqual(TEXT("One error per invalid field"), Errors.Num(), 2);
		if (Errors.Num() == 2)
		{
			TestTrue(TEXT("The sensor error gives the path and field"), Errors[0].Contains(TEXT("mixed.json bodies[1] formats[0]")) && Errors[0].Contains(TEXT("sensorMm")));
			TestTrue(TEXT("The lens error gives the path and field"), Errors[1].Contains(TEXT("mixed.json lensKits[0] lenses[0]")) && Errors[1].Contains(TEXT("tStop")));
		}

		// Loading the same body id again is reported, and the first entry is kept.
		Errors.Reset();
		const FString Duplicate = TEXT(R"({ "schema": 1, "bodies": [ { "id": "good", "manufacturer": "Other", "model": "Copy", "formats": [ { "id": "a", "name": "A", "sensorMm": [10.0, 10.0] } ] } ] })");
		TestFalse(TEXT("A duplicate body id is rejected"), Database.AddFromJson(Duplicate, TEXT("duplicate.json"), Errors));
		TestEqual(TEXT("The original body is kept"), *Database.FindBody(TEXT("good"))->Manufacturer, TEXT("Acme"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscDatabaseShippedTest, "Rosc.Core.Database.Shipped", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscDatabaseShippedTest::RunTest(const FString& Parameters)
{
	// Load the shipped files into a fresh database so that every validation error fails this test.
	FRoscCameraDatabase Database;
	TArray<FString> Errors;
	Database.AddFromDirectory(IPluginManager::Get().FindPlugin(TEXT("Rosc"))->GetBaseDir() / TEXT("Resources") / TEXT("Database"), Errors);
	for (const FString& Error : Errors)
	{
		AddError(Error);
	}
	TestTrue(TEXT("The shipped database has camera bodies"), Database.GetBodies().Num() > 0);
	TestTrue(TEXT("The shipped database has lens kits"), Database.GetLensKits().Num() > 0);

	for (const FRoscCameraBody& Body : Database.GetBodies())
	{
		for (const FRoscRecordingFormat& Format : Body.Formats)
		{
			// Digital formats: the sensor aspect ratio must agree with the photosite count.
			if (Format.Resolution.X > 0)
			{
				const double SensorAspect = Format.SensorWidthMm / Format.SensorHeightMm;
				const double PixelAspect = static_cast<double>(Format.Resolution.X) / Format.Resolution.Y;
				TestEqual(*FString::Printf(TEXT("%s %s: sensor aspect matches resolution aspect"), *Body.Id, *Format.Id), SensorAspect, PixelAspect, 0.02);
			}
			const double Diagonal = Rosc::Optics::SensorDiagonalMm(Format.SensorWidthMm, Format.SensorHeightMm);
			TestTrue(*FString::Printf(TEXT("%s %s: sensor diagonal is plausible (5 to 100 mm)"), *Body.Id, *Format.Id), Diagonal > 5.0 && Diagonal < 100.0);
		}
	}

	for (const FRoscLensKit& Kit : Database.GetLensKits())
	{
		TestTrue(*FString::Printf(TEXT("%s: squeeze is between 1 and 2"), *Kit.Id), Kit.Squeeze >= 1.0 && Kit.Squeeze <= 2.0);
		for (const FRoscLens& Lens : Kit.Lenses)
		{
			TestTrue(*FString::Printf(TEXT("%s %s: widest T-stop is faster than the minimum aperture"), *Kit.Id, *Lens.GetDisplayName()), Lens.TStop < Kit.MinApertureTStop);
			TestTrue(*FString::Printf(TEXT("%s %s: close focus is plausible (0.05 to 5 m)"), *Kit.Id, *Lens.GetDisplayName()), Lens.CloseFocusM > 0.05 && Lens.CloseFocusM < 5.0);
		}
	}
	return true;
}

#endif
