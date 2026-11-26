// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/**
 * The RoscCam protocol (ADR-014, docs/rosccam-protocol.md): the messages between the RoscCam app
 * on a phone or tablet and the editor, and the turn from the device's motion to a camera's.
 */
namespace Rosc::Cam
{
	inline constexpr int32 ProtocolVersion = 1;
	inline constexpr int32 DiscoveryPort = 47820;
	inline constexpr int32 ServerPort = 47821;
	/** A message longer than this closes the connection. */
	inline constexpr int32 MaxMessageBytes = 8 * 1024 * 1024;
	/** The size of a Motion message's body. */
	inline constexpr int32 MotionBytes = 40;

	enum class EMessage : uint8
	{
		Hello = 0x01,
		Welcome = 0x02,
		Pair = 0x03,
		Paired = 0x04,
		Picture = 0x10,
		Motion = 0x20,
		Control = 0x30,
		State = 0x31,
		Result = 0x32,
	};

	/** Adds one message to a buffer to send: its length, its type and its body. */
	ROSCCORE_API void AppendMessage(TArray<uint8>& Out, EMessage Type, TConstArrayView<uint8> Body);

	/** The same with a text body, sent as UTF-8. */
	ROSCCORE_API void AppendMessage(TArray<uint8>& Out, EMessage Type, const FString& Text);

	/** Takes in bytes as they arrive, in pieces of any size, and hands back whole messages. */
	class ROSCCORE_API FMessageReader
	{
	public:
		void Add(TConstArrayView<uint8> Bytes);

		/** The next whole message, if one has arrived. False when none has, or the stream is broken. */
		bool Next(EMessage& OutType, TArray<uint8>& OutBody);

		/** True when a message was too long or empty: the connection is to be closed. */
		bool IsBroken() const { return bBroken; }

	private:
		TArray<uint8> Pending;
		bool bBroken = false;
	};

	/** UTF-8 bytes as text. */
	ROSCCORE_API FString ToText(TConstArrayView<uint8> Utf8);

	/** One reading of the device. */
	struct FMotion
	{
		/** The device's clock, in seconds. */
		double Seconds = 0.0;
		/** The device's rotation in its world frame: right-handed, z up. */
		FQuat Rotation = FQuat::Identity;
		/** The device's position in its world frame, in metres. */
		FVector Position = FVector::ZeroVector;
		bool bPositionTracked = false;
	};

	ROSCCORE_API TArray<uint8> WriteMotion(const FMotion& Motion);

	/** False when the body is not a Motion message's. */
	ROSCCORE_API bool ReadMotion(TConstArrayView<uint8> Body, FMotion& OutMotion);

	/**
	 * Where a camera held as the device is would be, and which way it would look, in Unreal's
	 * frame: the device held in landscape with its top to the left, looking out of its back.
	 * Unreal's frame is left-handed, so the device's x and y swap; positions turn into centimetres.
	 * Only changes from one reading to another mean anything: the device's heading is arbitrary.
	 */
	ROSCCORE_API FTransform ToCameraTransform(const FMotion& Motion);

	/** A four-digit code for pairing a device, such as "4821". */
	ROSCCORE_API FString MakePairingCode(FRandomStream& Random);
}
