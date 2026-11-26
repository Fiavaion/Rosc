// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscCam.h"

#include "Containers/StringConv.h"
#include "Math/RotationMatrix.h"

namespace
{
	void AppendUint32(TArray<uint8>& Out, uint32 Value)
	{
		for (int32 Byte = 0; Byte < 4; ++Byte)
		{
			Out.Add(static_cast<uint8>(Value >> (8 * Byte)));
		}
	}

	uint32 ReadUint32(const uint8* Bytes)
	{
		return uint32(Bytes[0]) | (uint32(Bytes[1]) << 8) | (uint32(Bytes[2]) << 16) | (uint32(Bytes[3]) << 24);
	}

	template <typename T>
	void AppendValue(TArray<uint8>& Out, T Value)
	{
		// Every platform Rosc and RoscCam run on is little-endian, as the protocol is.
		const uint8* Bytes = reinterpret_cast<const uint8*>(&Value);
		Out.Append(Bytes, sizeof(T));
	}

	template <typename T>
	T ReadValue(const uint8* Bytes)
	{
		T Value;
		FMemory::Memcpy(&Value, Bytes, sizeof(T));
		return Value;
	}

	/** A vector of the device's world frame (right-handed, z up) in Unreal's (left-handed, z up). */
	FVector ToUnreal(const FVector& Device)
	{
		return FVector(Device.Y, Device.X, Device.Z);
	}
}

void Rosc::Cam::AppendMessage(TArray<uint8>& Out, EMessage Type, TConstArrayView<uint8> Body)
{
	AppendUint32(Out, static_cast<uint32>(Body.Num() + 1));
	Out.Add(static_cast<uint8>(Type));
	Out.Append(Body.GetData(), Body.Num());
}

void Rosc::Cam::AppendMessage(TArray<uint8>& Out, EMessage Type, const FString& Text)
{
	const FTCHARToUTF8 Utf8(*Text);
	AppendMessage(Out, Type, TConstArrayView<uint8>(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()));
}

void Rosc::Cam::FMessageReader::Add(TConstArrayView<uint8> Bytes)
{
	Pending.Append(Bytes.GetData(), Bytes.Num());
}

bool Rosc::Cam::FMessageReader::Next(EMessage& OutType, TArray<uint8>& OutBody)
{
	if (bBroken || Pending.Num() < 4)
	{
		return false;
	}
	const uint32 Length = ReadUint32(Pending.GetData());
	if (Length == 0 || Length > static_cast<uint32>(MaxMessageBytes))
	{
		bBroken = true;
		return false;
	}
	if (Pending.Num() < 4 + static_cast<int32>(Length))
	{
		return false;
	}
	OutType = static_cast<EMessage>(Pending[4]);
	OutBody = TArray<uint8>(Pending.GetData() + 5, static_cast<int32>(Length) - 1);
	Pending.RemoveAt(0, 4 + static_cast<int32>(Length), EAllowShrinking::No);
	return true;
}

FString Rosc::Cam::ToText(TConstArrayView<uint8> Utf8)
{
	const FUTF8ToTCHAR Text(reinterpret_cast<const UTF8CHAR*>(Utf8.GetData()), Utf8.Num());
	return FString::ConstructFromPtrSize(Text.Get(), Text.Length());
}

TArray<uint8> Rosc::Cam::WriteMotion(const FMotion& Motion)
{
	TArray<uint8> Body;
	AppendValue<double>(Body, Motion.Seconds);
	AppendValue<float>(Body, static_cast<float>(Motion.Rotation.W));
	AppendValue<float>(Body, static_cast<float>(Motion.Rotation.X));
	AppendValue<float>(Body, static_cast<float>(Motion.Rotation.Y));
	AppendValue<float>(Body, static_cast<float>(Motion.Rotation.Z));
	AppendValue<float>(Body, static_cast<float>(Motion.Position.X));
	AppendValue<float>(Body, static_cast<float>(Motion.Position.Y));
	AppendValue<float>(Body, static_cast<float>(Motion.Position.Z));
	AppendValue<uint32>(Body, Motion.bPositionTracked ? 1u : 0u);
	return Body;
}

bool Rosc::Cam::ReadMotion(TConstArrayView<uint8> Body, FMotion& OutMotion)
{
	if (Body.Num() != MotionBytes)
	{
		return false;
	}
	const uint8* Bytes = Body.GetData();
	OutMotion.Seconds = ReadValue<double>(Bytes);
	const float W = ReadValue<float>(Bytes + 8);
	const float X = ReadValue<float>(Bytes + 12);
	const float Y = ReadValue<float>(Bytes + 16);
	const float Z = ReadValue<float>(Bytes + 20);
	FQuat Rotation(X, Y, Z, W);
	if (!Rotation.ContainsNaN() && Rotation.SizeSquared() > UE_SMALL_NUMBER)
	{
		Rotation.Normalize();
	}
	else
	{
		return false;
	}
	OutMotion.Rotation = Rotation;
	OutMotion.Position = FVector(ReadValue<float>(Bytes + 24), ReadValue<float>(Bytes + 28), ReadValue<float>(Bytes + 32));
	OutMotion.bPositionTracked = (ReadValue<uint32>(Bytes + 36) & 1u) != 0 && !OutMotion.Position.ContainsNaN();
	return true;
}

FTransform Rosc::Cam::ToCameraTransform(const FMotion& Motion)
{
	// Held in landscape with the top to the left: the device's x points up, its y to the left,
	// and the camera looks out of the back, along -z.
	const FVector Forward = ToUnreal(Motion.Rotation.RotateVector(FVector(0.0, 0.0, -1.0)));
	const FVector Up = ToUnreal(Motion.Rotation.RotateVector(FVector(1.0, 0.0, 0.0)));
	const FRotator Rotation = FRotationMatrix::MakeFromXZ(Forward, Up).Rotator();
	const FVector Location = Motion.bPositionTracked ? ToUnreal(Motion.Position) * 100.0 : FVector::ZeroVector;
	return FTransform(Rotation, Location);
}

FString Rosc::Cam::MakePairingCode(FRandomStream& Random)
{
	return FString::Printf(TEXT("%04d"), Random.RandRange(0, 9999));
}
