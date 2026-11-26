// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"
#include "RoscAIModels.h"
#include "RoscVision.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/**
	 * What a point-map model would say of a floor, a wall and a post seen with a given field of
	 * view: true positions, then scaled and pushed along the lens axis as such maps are.
	 */
	void MakeVisionTestPoints(int32 Width, int32 Height, double FieldOfViewDeg, double Scale, double Shift, TArray<float>& OutPoints, TArray<float>& OutMask)
	{
		const double FocalPixels = Width / (2.0 * FMath::Tan(FMath::DegreesToRadians(FieldOfViewDeg) / 2.0));
		OutPoints.SetNumUninitialized(Width * Height * 3);
		OutMask.SetNumUninitialized(Width * Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const FVector3d Ray((X + 0.5 - Width / 2.0) / FocalPixels, (Y + 0.5 - Height / 2.0) / FocalPixels, 1.0);
				// A floor 1.5 below the camera and a wall 12 away behind it. A post 5 away fills the left
				// third of the picture: a long lens sees no floor, and a flat wall alone fixes no lens.
				const double Behind = X < Width / 3 ? 5.0 : 12.0;
				const double Distance = Ray.Y > 0.0 ? FMath::Min(1.5 / Ray.Y, Behind) : Behind;
				const FVector3d Point = Ray * Distance;
				const int32 Index = Y * Width + X;
				OutPoints[Index * 3] = static_cast<float>(Point.X * Scale);
				OutPoints[Index * 3 + 1] = static_cast<float>(Point.Y * Scale);
				OutPoints[Index * 3 + 2] = static_cast<float>(Point.Z * Scale - Shift);
				OutMask[Index] = 1.0f;
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscVisionFieldOfViewTest, "Rosc.Core.Vision.FieldOfView", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscVisionFieldOfViewTest::RunTest(const FString& Parameters)
{
	TArray<float> Points;
	TArray<float> Mask;

	// A 32 mm lens on Super 35 sees 42.5 degrees; a 14 mm sees 83.3; a 135 mm sees 10.5.
	for (const double FieldOfView : { 42.5, 83.3, 10.5 })
	{
		MakeVisionTestPoints(320, 180, FieldOfView, 1.0, 0.0, Points, Mask);
		TOptional<double> Found = Rosc::Vision::FieldOfViewFromPoints(Points, Mask, 320, 180);
		TestTrue(TEXT("True points give a field of view"), Found.IsSet());
		TestEqual(*FString::Printf(TEXT("True points seen with %.1f degrees"), FieldOfView), Found.Get(0.0), FieldOfView, 0.1);

		MakeVisionTestPoints(320, 180, FieldOfView, 0.2, 0.15, Points, Mask);
		Found = Rosc::Vision::FieldOfViewFromPoints(Points, Mask, 320, 180);
		TestEqual(*FString::Printf(TEXT("Scaled and shifted points seen with %.1f degrees"), FieldOfView), Found.Get(0.0), FieldOfView, 0.2);
	}

	// A tall picture is judged across its width like any other.
	MakeVisionTestPoints(180, 320, 35.0, 3.0, -0.5, Points, Mask);
	TestEqual(TEXT("A tall picture seen with 35 degrees across"), Rosc::Vision::FieldOfViewFromPoints(Points, Mask, 180, 320).Get(0.0), 35.0, 0.2);

	// Points that are not to be trusted are left out; with none left there is no answer.
	MakeVisionTestPoints(320, 180, 42.5, 1.0, 0.0, Points, Mask);
	for (int32 Index = 0; Index < Mask.Num() / 2; ++Index)
	{
		Mask[Index] = 0.0f;
		Points[Index * 3 + 2] = -1000.0f;
	}
	TestEqual(TEXT("Half the picture masked off"), Rosc::Vision::FieldOfViewFromPoints(Points, Mask, 320, 180).Get(0.0), 42.5, 0.2);
	for (float& Value : Mask)
	{
		Value = 0.0f;
	}
	TestFalse(TEXT("Nothing to judge from gives no answer"), Rosc::Vision::FieldOfViewFromPoints(Points, Mask, 320, 180).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscVisionGreyTest, "Rosc.Core.Vision.Grey", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscVisionGreyTest::RunTest(const FString& Parameters)
{
	const TArray<uint16> Depth = Rosc::Vision::NearnessToGrey16({ 2.0f, 4.0f, 3.0f, 2.5f });
	TestEqual(TEXT("The farthest is black"), static_cast<int32>(Depth[0]), 0);
	TestEqual(TEXT("The nearest is white"), static_cast<int32>(Depth[1]), 65535);
	TestEqual(TEXT("Half way is mid grey"), static_cast<int32>(Depth[2]), 32768);
	TestEqual(TEXT("A quarter of the way"), static_cast<int32>(Depth[3]), 16384);
	TestEqual(TEXT("A flat picture is black, not a fault"), static_cast<int32>(Rosc::Vision::NearnessToGrey16({ 1.0f, 1.0f })[1]), 0);

	const TArray<uint8> Matte = Rosc::Vision::LogitsToGrey8({ -20.0f, 0.0f, 20.0f });
	TestEqual(TEXT("Surely not the subject is black"), static_cast<int32>(Matte[0]), 0);
	TestEqual(TEXT("Undecided is mid grey"), static_cast<int32>(Matte[1]), 128);
	TestEqual(TEXT("Surely the subject is white"), static_cast<int32>(Matte[2]), 255);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscAIModelListTest, "Rosc.Core.AIModels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscAIModelListTest::RunTest(const FString& Parameters)
{
	// The list that ships: one model for each task, each with a licence, a checked download and a recipe.
	const FRoscAIModelList& Shipped = FRoscAIModelList::Get();
	for (const ERoscAITask Task : { ERoscAITask::Depth, ERoscAITask::Matte, ERoscAITask::FocalLength })
	{
		const FRoscAIModel* Model = Shipped.FindForTask(Task);
		if (TestNotNull(*FString::Printf(TEXT("The shipped list has a model for %s"), Rosc::AI::GetTaskId(Task)), Model))
		{
			TestTrue(TEXT("Its licence allows commercial use"), Model->Licence == TEXT("Apache-2.0") || Model->Licence == TEXT("MIT"));
			TestTrue(TEXT("Its licence page is named"), Model->LicencePage.StartsWith(TEXT("https://")));
			TestEqual(TEXT("Its hash is a SHA-256"), Model->Sha256.Len(), 64);
		}
	}

	const FRoscAIModel* Depth = Shipped.FindForTask(ERoscAITask::Depth);
	if (Depth != nullptr)
	{
		TestTrue(TEXT("A 16:9 picture goes to the depth model at 924 x 518"), Depth->Input.GetSizeFor(1920, 1080) == FIntPoint(924, 518));
		TestTrue(TEXT("A 2.39:1 picture keeps its shape, in multiples of 14"), Depth->Input.GetSizeFor(4096, 1716) == FIntPoint(1232, 518));
		TestTrue(TEXT("A tall picture has its short side across"), Depth->Input.GetSizeFor(1080, 1920) == FIntPoint(518, 924));
	}
	const FRoscAIModel* Matte = Shipped.FindForTask(ERoscAITask::Matte);
	if (Matte != nullptr)
	{
		TestTrue(TEXT("The matte model takes one size"), Matte->Input.GetSizeFor(1920, 1080) == FIntPoint(1024, 1024));
	}

	// Faults are named with the file and the field, and the model is left out.
	const FString Faulty = TEXT(R"({ "models": [
		{ "id": "a", "task": "depth", "name": "A", "licence": "MIT", "licencePage": "https://example.com/a",
		  "url": "http://example.com/a.onnx", "file": "../a.onnx", "bytes": 10, "sha256": "abc",
		  "input": { "name": "in", "shortSide": 518, "mean": [0, 0, 0], "std": [1, 0, 1] }, "output": "out" },
		{ "id": "b", "task": "paint", "name": "B", "licence": "MIT", "licencePage": "https://example.com/b",
		  "url": "https://example.com/b.onnx", "file": "b.onnx", "bytes": 10,
		  "sha256": "0000000000000000000000000000000000000000000000000000000000000000",
		  "input": { "name": "in", "width": 64, "height": 64, "mean": [0, 0, 0], "std": [1, 1, 1] }, "output": "out" },
		{ "id": "c", "task": "matte", "name": "C", "licence": "MIT", "licencePage": "https://example.com/c",
		  "url": "https://example.com/c.onnx", "file": "c.onnx", "bytes": 10,
		  "sha256": "0000000000000000000000000000000000000000000000000000000000000000",
		  "input": { "name": "in", "width": 64, "height": 64, "mean": [0, 0, 0], "std": [1, 1, 1] }, "output": "out" } ] })");
	FRoscAIModelList List;
	TArray<FString> Errors;
	TestFalse(TEXT("A list with faults says so"), List.AddFromJson(Faulty, TEXT("test.json"), Errors));
	TestEqual(TEXT("The model without faults is kept"), List.GetModels().Num(), 1);
	const auto HasError = [&Errors](const TCHAR* Text) { return Errors.ContainsByPredicate([Text](const FString& Error) { return Error.Contains(Text); }); };
	TestTrue(TEXT("A download that is not https is refused"), HasError(TEXT("test.json models[0]: \"url\" must start with https://")));
	TestTrue(TEXT("A file name that leaves the folder is refused"), HasError(TEXT("test.json models[0]: \"file\" must be a file name without a folder")));
	TestTrue(TEXT("A short hash is refused"), HasError(TEXT("test.json models[0]: \"sha256\" must be 64 hexadecimal digits")));
	TestTrue(TEXT("A deviation of zero is refused"), HasError(TEXT("test.json models[0] input: \"std\" must be three numbers greater than zero")));
	TestTrue(TEXT("An unknown task is refused"), HasError(TEXT("test.json models[1]: \"task\" must be depth, matte or focal")));
	return true;
}

#endif
