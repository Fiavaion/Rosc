// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeatures.h"
#include "HAL/PlatformTime.h"
#include "ILiveLinkClient.h"
#include "ILiveLinkSource.h"
#include "LiveLinkSourceSettings.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

/** A device for the tests that operate a support: a Live Link source whose every move the test makes. */
namespace Rosc::DeviceTests
{
	inline const FName TestDevice(TEXT("RoscTestDevice"));

	inline ILiveLinkClient* GetLiveLinkClient()
	{
		IModularFeatures& Features = IModularFeatures::Get();
		return Features.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName)
			? &Features.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName)
			: nullptr;
	}

	class FTestDeviceSource : public ILiveLinkSource
	{
	public:
		virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override
		{
			Client = InClient;
			Guid = InSourceGuid;
		}
		virtual bool IsSourceStillValid() const override { return true; }
		virtual bool RequestSourceShutdown() override { return true; }
		virtual FText GetSourceType() const override { return FText::FromString(TEXT("Rosc test device")); }
		virtual FText GetSourceMachineName() const override { return FText::FromString(TEXT("Test")); }
		virtual FText GetSourceStatus() const override { return FText::FromString(TEXT("Moving")); }

		/**
		 * Adds a test device to Live Link, listed as TestDevice. The engine's clock does not move
		 * inside a test, so the device is read at its latest frame.
		 */
		static TSharedRef<FTestDeviceSource> Add(ILiveLinkClient& LiveLink)
		{
			const TSharedRef<FTestDeviceSource> Source = MakeShared<FTestDeviceSource>();
			const FGuid SourceGuid = LiveLink.AddSource(Source);
			if (ULiveLinkSourceSettings* Settings = LiveLink.GetSourceSettings(SourceGuid))
			{
				Settings->Mode = ELiveLinkSourceMode::Latest;
			}
			FLiveLinkStaticDataStruct StaticData(FLiveLinkTransformStaticData::StaticStruct());
			LiveLink.PushSubjectStaticData_AnyThread(FLiveLinkSubjectKey(SourceGuid, TestDevice), ULiveLinkTransformRole::StaticClass(), MoveTemp(StaticData));
			LiveLink.ForceTick();
			return Source;
		}

		/** Takes the device out of Live Link. */
		void Remove()
		{
			if (Client != nullptr)
			{
				Client->RemoveSource(Guid);
				Client->ForceTick();
				Client = nullptr;
			}
		}

		/** Holds the device so, and has Live Link take it in at once. */
		void Hold(const FRotator& Rotation, const FVector& Location = FVector::ZeroVector)
		{
			FLiveLinkFrameDataStruct Frame(FLiveLinkTransformFrameData::StaticStruct());
			Frame.Cast<FLiveLinkTransformFrameData>()->Transform = FTransform(Rotation, Location);
			Frame.GetBaseData()->WorldTime = FLiveLinkWorldTime(FPlatformTime::Seconds());
			Client->PushSubjectFrameData_AnyThread(FLiveLinkSubjectKey(Guid, TestDevice), MoveTemp(Frame));
			Client->ForceTick();
		}

	private:
		ILiveLinkClient* Client = nullptr;
		FGuid Guid;
	};
}

#endif
