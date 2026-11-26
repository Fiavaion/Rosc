// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "Math/RotationMatrix.h"
#include "Misc/AutomationTest.h"
#include "RoscCam.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** The device held upright in landscape, top to the left, facing north (+y of its world). */
	FQuat FacingNorth()
	{
		// Columns: where the device's x (up), y (left, so west) and z (towards the operator, so south) point.
		const FMatrix Device(FPlane(0.0, 0.0, 1.0, 0.0), FPlane(-1.0, 0.0, 0.0, 0.0), FPlane(0.0, -1.0, 0.0, 0.0), FPlane(0.0, 0.0, 0.0, 1.0));
		return FQuat(Device);
	}

	/** A right-handed turn of the device's world about an axis, applied after the hold. */
	FQuat Turned(const FQuat& Hold, const FVector& Axis, double Degrees)
	{
		return FQuat(Axis, FMath::DegreesToRadians(Degrees)) * Hold;
	}

	Rosc::Cam::FMotion Motion(const FQuat& Rotation)
	{
		Rosc::Cam::FMotion Result;
		Result.Rotation = Rotation;
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscCamMessagesTest, "Rosc.Core.Cam.Messages", EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FRoscCamMessagesTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::Cam;

	TArray<uint8> Stream;
	AppendMessage(Stream, EMessage::Hello, FString(TEXT("{ \"device\": \"Pixel 8 — test\" }")));
	const TArray<uint8> Picture = { 0xFF, 0xD8, 0x00, 0xFF, 0xD9 };
	AppendMessage(Stream, EMessage::Picture, Picture);
	// The hello's 30 characters are 32 bytes of UTF-8: the dash takes three.
	TestEqual(TEXT("each message is its length, its type and its body"), Stream.Num(), 4 + 1 + 32 + 4 + 1 + 5);

	// Delivered one byte at a time, as a slow network might.
	FMessageReader Reader;
	TArray<TPair<EMessage, TArray<uint8>>> Read;
	for (const uint8 Byte : Stream)
	{
		Reader.Add(MakeArrayView(&Byte, 1));
		EMessage Type;
		TArray<uint8> Body;
		while (Reader.Next(Type, Body))
		{
			Read.Emplace(Type, Body);
		}
	}
	if (TestEqual(TEXT("both messages are read back"), Read.Num(), 2))
	{
		TestEqual(TEXT("the first is the hello"), Read[0].Key, EMessage::Hello);
		TestEqual(TEXT("its text comes back whole, accents and all"), ToText(Read[0].Value), FString(TEXT("{ \"device\": \"Pixel 8 — test\" }")));
		TestEqual(TEXT("the second is the picture"), Read[1].Key, EMessage::Picture);
		TestTrue(TEXT("with its bytes"), Read[1].Value == Picture);
	}

	// Both at once, then half of a third.
	FMessageReader Together;
	TArray<uint8> More = Stream;
	AppendMessage(More, EMessage::Control, FString(TEXT("{\"action\":\"zero\"}")));
	Together.Add(TConstArrayView<uint8>(More.GetData(), More.Num() - 4));
	EMessage Type;
	TArray<uint8> Body;
	int32 Count = 0;
	while (Together.Next(Type, Body))
	{
		++Count;
	}
	TestEqual(TEXT("whole messages are read and a half one waits"), Count, 2);
	Together.Add(TConstArrayView<uint8>(More.GetData() + More.Num() - 4, 4));
	TestTrue(TEXT("the rest of it completes it"), Together.Next(Type, Body) && Type == EMessage::Control);

	FMessageReader TooLong;
	const TArray<uint8> Huge = { 0xFF, 0xFF, 0xFF, 0x7F, 0x10 };
	TooLong.Add(Huge);
	TestFalse(TEXT("a message longer than allowed is not read"), TooLong.Next(Type, Body));
	TestTrue(TEXT("and breaks the stream"), TooLong.IsBroken());

	FMotion Sent;
	Sent.Seconds = 12.5;
	Sent.Rotation = FQuat(FVector(1.0, 2.0, 3.0).GetSafeNormal(), 0.7);
	Sent.Position = FVector(0.25, -1.5, 1.6);
	Sent.bPositionTracked = true;
	const TArray<uint8> MotionBody = WriteMotion(Sent);
	TestEqual(TEXT("a motion is 40 bytes"), MotionBody.Num(), MotionBytes);
	FMotion Got;
	if (TestTrue(TEXT("and is read back"), ReadMotion(MotionBody, Got)))
	{
		TestEqual(TEXT("its time"), Got.Seconds, 12.5);
		TestTrue(TEXT("its rotation"), Got.Rotation.Equals(Sent.Rotation, 1.e-6));
		TestTrue(TEXT("its position"), Got.Position.Equals(Sent.Position, 1.e-6));
		TestTrue(TEXT("and that the position is tracked"), Got.bPositionTracked);
	}
	TestFalse(TEXT("a motion of the wrong size is refused"), ReadMotion(TConstArrayView<uint8>(MotionBody.GetData(), 39), Got));
	TArray<uint8> Zero = MotionBody;
	FMemory::Memzero(Zero.GetData() + 8, 16);
	TestFalse(TEXT("a motion with no rotation is refused"), ReadMotion(Zero, Got));

	FRandomStream Random(7);
	const FString Code = MakePairingCode(Random);
	TestTrue(*FString::Printf(TEXT("a pairing code is four digits (%s)"), *Code), Code.Len() == 4 && Code.IsNumeric());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscCamCameraTest, "Rosc.Core.Cam.Camera", EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FRoscCamCameraTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::Cam;

	const FRotator Level = ToCameraTransform(Motion(FacingNorth())).Rotator();
	TestEqual(TEXT("held upright, the camera is level"), Level.Pitch, 0.0, 1.e-6);
	TestEqual(TEXT("with no roll"), Level.Roll, 0.0, 1.e-6);

	const FVector Up(0.0, 0.0, 1.0);
	const FRotator Right = ToCameraTransform(Motion(Turned(FacingNorth(), Up, -30.0))).Rotator();
	TestEqual(TEXT("turning the device 30° to the right pans the camera 30° to the right"), FRotator::NormalizeAxis(Right.Yaw - Level.Yaw), 30.0, 1.e-4);
	TestEqual(TEXT("and does not tilt it"), Right.Pitch, 0.0, 1.e-4);

	// The camera's right is east, +x of the device's world.
	const FRotator Tilted = ToCameraTransform(Motion(Turned(FacingNorth(), FVector(1.0, 0.0, 0.0), 10.0))).Rotator();
	TestEqual(TEXT("tipping the device's back up tilts the camera up 10°"), Tilted.Pitch, 10.0, 1.e-4);
	TestEqual(TEXT("and does not pan it"), FRotator::NormalizeAxis(Tilted.Yaw - Level.Yaw), 0.0, 1.e-4);

	// Looking north, the camera looks along the device world's +y.
	const FRotator Rolled = ToCameraTransform(Motion(Turned(FacingNorth(), FVector(0.0, 1.0, 0.0), 5.0))).Rotator();
	TestEqual(TEXT("leaning the device clockwise as the operator sees it rolls the camera 5° clockwise"), Rolled.Roll, 5.0, 1.e-4);
	TestEqual(TEXT("and does not tilt it"), Rolled.Pitch, 0.0, 1.e-4);

	const FRotator Flat = ToCameraTransform(Motion(FQuat::Identity)).Rotator();
	TestEqual(TEXT("lying face up, the camera looks at the floor"), Flat.Pitch, -90.0, 1.e-4);

	FMotion Walking = Motion(FacingNorth());
	Walking.Position = FVector(0.5, 2.0, 1.6);
	Walking.bPositionTracked = true;
	const FVector Location = ToCameraTransform(Walking).GetLocation();
	TestTrue(*FString::Printf(TEXT("2 m north and 0.5 m east is 200 cm along x and 50 cm along y (%s)"), *Location.ToString()), Location.Equals(FVector(200.0, 50.0, 160.0), 1.e-3));
	Walking.bPositionTracked = false;
	TestTrue(TEXT("an untracked position is not used"), ToCameraTransform(Walking).GetLocation().IsZero());
	return true;
}

#endif
