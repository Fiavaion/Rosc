// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"
#include "RoscCameraDatabase.h"
#include "RoscCameraPackage.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** A small database with one Super 35 body and two kits: full-coverage primes with a zoom, and anamorphics. */
	FRoscCameraDatabase MakeTestDatabase(FAutomationTestBase& Test)
	{
		const FString Json = TEXT(R"({
			"schema": 1,
			"bodies": [
				{ "id": "body", "manufacturer": "Acme", "model": "S35", "baseIso": [500],
				  "formats": [
					{ "id": "wide", "name": "Wide", "sensorMm": [28.0, 19.0], "resolution": [4480, 3040] },
					{ "id": "large", "name": "Large", "sensorMm": [36.0, 24.0], "resolution": [5760, 3840] } ] }
			],
			"lensKits": [
				{ "id": "primes", "manufacturer": "Acme", "name": "Primes", "imageCircleMm": 35.0, "minApertureTStop": 16, "irisBlades": 9,
				  "lenses": [
					{ "focalMm": 25, "tStop": 1.5, "closeFocusM": 0.3 },
					{ "focalMm": 35, "tStop": 1.5, "closeFocusM": 0.35 },
					{ "focalMm": 50, "tStop": 2.0, "closeFocusM": 0.45 },
					{ "focalMm": 70, "zoomToMm": 200, "tStop": 2.8, "closeFocusM": 1.2 } ] },
				{ "id": "scope", "manufacturer": "Acme", "name": "Scope", "squeeze": 2.0, "imageCircleMm": 35.0,
				  "lenses": [ { "focalMm": 40, "tStop": 2.3, "closeFocusM": 0.8 }, { "focalMm": 75, "tStop": 2.3, "closeFocusM": 1.0 } ] }
			]
		})");

		FRoscCameraDatabase Database;
		TArray<FString> Errors;
		Database.AddFromJson(Json, TEXT("test.json"), Errors);
		for (const FString& Error : Errors)
		{
			Test.AddError(Error);
		}
		return Database;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscCameraPackageSelectionTest, "Rosc.Core.CameraPackage.Selection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscCameraPackageSelectionTest::RunTest(const FString& Parameters)
{
	const FRoscCameraDatabase Database = MakeTestDatabase(*this);
	const FRoscCameraBody& Body = *Database.FindBody(TEXT("body"));
	const FRoscLensKit& Primes = *Database.FindLensKit(TEXT("primes"));
	const FRoscLensKit& Scope = *Database.FindLensKit(TEXT("scope"));

	FRoscCameraPackage Package;
	Package.SelectBody(Body);
	TestEqual(TEXT("Choosing a body takes its first format"), *Package.FormatId, TEXT("wide"));
	TestEqual(TEXT("Choosing a body takes its native ISO"), Package.Iso, 500);

	// The default package is at 35 mm, T2.8.
	Package.SelectLensKit(Primes);
	TestEqual(TEXT("Choosing a kit picks the lens at the current focal length"), Package.LensIndex, 1);
	TestEqual(TEXT("Focal length stays at 35 mm"), Package.FocalLengthMm, 35.0f);
	TestEqual(TEXT("T-stop within the lens range is kept"), Package.TStop, 2.8f);

	// Swapping to the anamorphic kit at 35 mm: 40 mm is the closest lens.
	Package.SelectLensKit(Scope);
	TestEqual(TEXT("The closest anamorphic lens is the 40 mm"), Package.FocalLengthMm, 40.0f);

	// Opening up to T1.5 on the 35 mm, then swapping to the T2.0 50 mm, closes the iris to T2.0.
	Package.SelectLensKit(Primes);
	Package.TStop = 1.5f;
	Package.SelectLens(Primes, 2);
	TestEqual(TEXT("Swapping to a slower lens raises the T-stop to its widest aperture"), Package.TStop, 2.0f);
	TestEqual(TEXT("Swapping primes changes the focal length"), Package.FocalLengthMm, 50.0f);

	// Going to the zoom from 50 mm lands at its wide end; a focal length inside its range is kept.
	Package.SelectLens(Primes, 3);
	TestEqual(TEXT("A focal length below the zoom range moves to its wide end"), Package.FocalLengthMm, 70.0f);
	Package.FocalLengthMm = 135.0f;
	Package.SelectLensKit(Primes);
	TestEqual(TEXT("A focal length inside the zoom range selects the zoom"), Package.LensIndex, 3);
	TestEqual(TEXT("and keeps the focal length"), Package.FocalLengthMm, 135.0f);

	Package.SelectLens(Primes, 99);
	TestEqual(TEXT("An out-of-range lens index is ignored"), Package.LensIndex, 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscCameraPackageResolveTest, "Rosc.Core.CameraPackage.Resolve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscCameraPackageResolveTest::RunTest(const FString& Parameters)
{
	const FRoscCameraDatabase Database = MakeTestDatabase(*this);

	FRoscCameraPackage Package;
	Package.SelectBody(*Database.FindBody(TEXT("body")));
	Package.SelectLensKit(*Database.FindLensKit(TEXT("scope")));
	Package.ShutterAngleDeg = 172.8f;
	Package.FramesPerSecond = 24.0f;
	Package.NdDensity = 0.9f;
	Package.ExposureCompensationStops = 1.0f;
	Package.bPhysicalExposure = true;
	Package.TStop = 1.0f; // wider than the lens can open

	FRoscResolvedCamera Camera;
	FText Error;
	if (!TestTrue(TEXT("A complete package resolves"), Rosc::ResolveCameraPackage(Database, Package, Camera, Error)))
	{
		AddError(Error.ToString());
		return false;
	}

	TestEqual(TEXT("Sensor width"), Camera.SensorWidthMm, 28.0);
	TestEqual(TEXT("Sensor height"), Camera.SensorHeightMm, 19.0);
	TestEqual(TEXT("Squeeze comes from the kit"), Camera.Squeeze, 2.0);
	TestEqual(TEXT("A prime's focal range is a single focal length"), Camera.MinFocalLengthMm, Camera.MaxFocalLengthMm);
	TestEqual(TEXT("Focal length"), Camera.FocalLengthMm, 40.0);
	TestEqual(TEXT("A T-stop wider than the lens is limited to its widest aperture"), Camera.TStop, 2.3);
	TestEqual(TEXT("Minimum aperture defaults to T22"), Camera.MinApertureTStop, 22.0);
	TestEqual(TEXT("Close focus is converted to millimetres"), Camera.CloseFocusMm, 800.0);
	TestEqual(TEXT("Unknown iris blade count is zero"), Camera.IrisBlades, 0);
	TestEqual(TEXT("172.8 degrees at 24 fps is 1/50 s"), Camera.ShutterSpeedReciprocal, 50.0, 1e-4);
	TestEqual(TEXT("Motion blur amount is the open fraction of the shutter"), Camera.MotionBlurAmount, 0.48, 1e-6);
	TestEqual(TEXT("ISO"), Camera.Iso, 500);
	TestEqual(TEXT("Exposure bias is compensation minus three stops of ND"), Camera.ExposureBiasStops, 1.0 - 2.98974, 1e-4);
	TestEqual(TEXT("A covered format gives no warnings"), Camera.Warnings.Num(), 0);

	// The 35 mm image circle does not cover the 43.3 mm diagonal of the large format.
	Package.FormatId = TEXT("large");
	TestTrue(TEXT("An uncovered format still resolves"), Rosc::ResolveCameraPackage(Database, Package, Camera, Error));
	TestEqual(TEXT("An uncovered format warns about vignetting"), Camera.Warnings.Num(), 1);

	// A zoom keeps its range and limits the focal length to it.
	Package.FormatId = TEXT("wide");
	Package.SelectLensKit(*Database.FindLensKit(TEXT("primes")));
	Package.LensIndex = 3;
	Package.FocalLengthMm = 500.0f;
	TestTrue(TEXT("A zoom resolves"), Rosc::ResolveCameraPackage(Database, Package, Camera, Error));
	TestEqual(TEXT("Zoom wide end"), Camera.MinFocalLengthMm, 70.0);
	TestEqual(TEXT("Zoom long end"), Camera.MaxFocalLengthMm, 200.0);
	TestEqual(TEXT("A focal length past the long end is limited to it"), Camera.FocalLengthMm, 200.0);
	TestEqual(TEXT("Iris blade count comes from the kit"), Camera.IrisBlades, 9);

	// Unknown ids fail with a message naming the id.
	FRoscCameraPackage Unknown = Package;
	Unknown.BodyId = TEXT("missing-body");
	TestFalse(TEXT("An unknown body fails"), Rosc::ResolveCameraPackage(Database, Unknown, Camera, Error));
	TestTrue(TEXT("The error names the body"), Error.ToString().Contains(TEXT("missing-body")));

	Unknown = Package;
	Unknown.FormatId = TEXT("missing-format");
	TestFalse(TEXT("An unknown format fails"), Rosc::ResolveCameraPackage(Database, Unknown, Camera, Error));
	TestTrue(TEXT("The error names the format"), Error.ToString().Contains(TEXT("missing-format")));

	Unknown = Package;
	Unknown.LensKitId = TEXT("missing-kit");
	TestFalse(TEXT("An unknown lens kit fails"), Rosc::ResolveCameraPackage(Database, Unknown, Camera, Error));
	TestTrue(TEXT("The error names the kit"), Error.ToString().Contains(TEXT("missing-kit")));

	Unknown = Package;
	Unknown.LensIndex = 42;
	TestFalse(TEXT("A lens index outside the kit fails"), Rosc::ResolveCameraPackage(Database, Unknown, Camera, Error));

	return true;
}

#endif
