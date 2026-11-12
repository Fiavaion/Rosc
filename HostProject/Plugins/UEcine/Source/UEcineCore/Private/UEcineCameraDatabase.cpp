// Copyright 2026 Fiavaion. All rights reserved.

#include "UEcineCameraDatabase.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UEcineJsonEntry.h"
#include "UEcineLog.h"
#include "UEcineOptics.h"

namespace
{
	FString FormatNumber(double Value)
	{
		return FString::SanitizeFloat(Value, 0);
	}
}

FString FUEcineCameraBody::GetDisplayName() const
{
	return Manufacturer + TEXT(" ") + Model;
}

const FUEcineRecordingFormat* FUEcineCameraBody::FindFormat(const FString& FormatId) const
{
	return Formats.FindByPredicate([&FormatId](const FUEcineRecordingFormat& Format) { return Format.Id == FormatId; });
}

FString FUEcineLens::GetDisplayName() const
{
	const FString Focal = IsZoom()
		? FString::Printf(TEXT("%s-%smm"), *FormatNumber(FocalLengthMm), *FormatNumber(MaxFocalLengthMm))
		: FString::Printf(TEXT("%smm"), *FormatNumber(FocalLengthMm));
	return FString::Printf(TEXT("%s T%s"), *Focal, *FormatNumber(TStop));
}

FString FUEcineLensKit::GetDisplayName() const
{
	return Manufacturer + TEXT(" ") + Name;
}

bool FUEcineLensKit::CoversFormat(const FUEcineRecordingFormat& Format) const
{
	// Published image circles and sensor sizes are rounded, so allow a small margin.
	const double ToleranceMm = 0.1;
	return ImageCircleMm + ToleranceMm >= UEcine::Optics::SensorDiagonalMm(Format.SensorWidthMm, Format.SensorHeightMm);
}

const FUEcineCameraDatabase& FUEcineCameraDatabase::Get()
{
	static const FUEcineCameraDatabase Database = []
	{
		FUEcineCameraDatabase Loaded;
		TArray<FString> Errors;

		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UEcine"));
		if (ensure(Plugin.IsValid()))
		{
			Loaded.AddFromDirectory(Plugin->GetBaseDir() / TEXT("Resources") / TEXT("Database"), Errors);
		}
		Loaded.AddFromDirectory(FPaths::ProjectConfigDir() / TEXT("UEcine") / TEXT("Database"), Errors);

		for (const FString& Error : Errors)
		{
			UE_LOG(LogUEcine, Warning, TEXT("Camera database: %s"), *Error);
		}
		UE_LOG(LogUEcine, Log, TEXT("Camera database loaded: %d camera bodies, %d lens kits."), Loaded.Bodies.Num(), Loaded.LensKits.Num());
		return Loaded;
	}();
	return Database;
}

bool FUEcineCameraDatabase::AddFromJson(const FString& JsonText, const FString& SourceName, TArray<FString>& OutErrors)
{
	const int32 ErrorCountBefore = OutErrors.Num();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutErrors.Add(FString::Printf(TEXT("%s: not valid JSON (%s)."), *SourceName, *Reader->GetErrorMessage()));
		return false;
	}

	int32 Schema = 0;
	if (!Root->TryGetNumberField(TEXT("schema"), Schema) || Schema != SchemaVersion)
	{
		OutErrors.Add(FString::Printf(TEXT("%s: \"schema\" must be %d."), *SourceName, SchemaVersion));
		return false;
	}

	FUEcineJsonEntryReader RootReader(Root, SourceName, OutErrors);

	const TArray<TSharedPtr<FJsonObject>> BodyObjects = RootReader.ObjectArray(TEXT("bodies"), false);
	for (int32 BodyIndex = 0; BodyIndex < BodyObjects.Num(); ++BodyIndex)
	{
		const FString BodyPath = FString::Printf(TEXT("%s bodies[%d]"), *SourceName, BodyIndex);
		FUEcineJsonEntryReader BodyReader(BodyObjects[BodyIndex], BodyPath, OutErrors);

		FUEcineCameraBody Body;
		Body.Id = BodyReader.RequiredString(TEXT("id"));
		Body.Manufacturer = BodyReader.RequiredString(TEXT("manufacturer"));
		Body.Model = BodyReader.RequiredString(TEXT("model"));
		Body.Mount = BodyReader.OptionalString(TEXT("mount"));
		Body.BaseIsos = BodyReader.OptionalPositiveIntArray(TEXT("baseIso"));

		const TArray<TSharedPtr<FJsonObject>> FormatObjects = BodyReader.ObjectArray(TEXT("formats"), true);
		for (int32 FormatIndex = 0; FormatIndex < FormatObjects.Num(); ++FormatIndex)
		{
			FUEcineJsonEntryReader FormatReader(FormatObjects[FormatIndex], FString::Printf(TEXT("%s formats[%d]"), *BodyPath, FormatIndex), OutErrors);

			FUEcineRecordingFormat Format;
			Format.Id = FormatReader.RequiredString(TEXT("id"));
			Format.Name = FormatReader.RequiredString(TEXT("name"));
			FormatReader.RequiredPositivePair(TEXT("sensorMm"), Format.SensorWidthMm, Format.SensorHeightMm);
			if (FormatObjects[FormatIndex]->HasField(TEXT("resolution")))
			{
				double Width = 0.0;
				double Height = 0.0;
				if (FormatReader.RequiredPositivePair(TEXT("resolution"), Width, Height))
				{
					Format.Resolution = FIntPoint(FMath::RoundToInt32(Width), FMath::RoundToInt32(Height));
				}
			}
			if (FormatReader.IsValid() && Body.FindFormat(Format.Id) != nullptr)
			{
				FormatReader.Fail(TEXT("id"), *FString::Printf(TEXT("\"%s\" is used by another format of this camera"), *Format.Id));
			}

			if (FormatReader.IsValid())
			{
				Body.Formats.Add(MoveTemp(Format));
			}
			else
			{
				BodyReader.MarkInvalid();
			}
		}

		if (BodyReader.IsValid() && FindBody(Body.Id) != nullptr)
		{
			BodyReader.Fail(TEXT("id"), *FString::Printf(TEXT("\"%s\" is already used by another camera body"), *Body.Id));
		}
		if (BodyReader.IsValid())
		{
			Bodies.Add(MoveTemp(Body));
		}
	}

	const TArray<TSharedPtr<FJsonObject>> KitObjects = RootReader.ObjectArray(TEXT("lensKits"), false);
	for (int32 KitIndex = 0; KitIndex < KitObjects.Num(); ++KitIndex)
	{
		const FString KitPath = FString::Printf(TEXT("%s lensKits[%d]"), *SourceName, KitIndex);
		FUEcineJsonEntryReader KitReader(KitObjects[KitIndex], KitPath, OutErrors);

		FUEcineLensKit Kit;
		Kit.Id = KitReader.RequiredString(TEXT("id"));
		Kit.Manufacturer = KitReader.RequiredString(TEXT("manufacturer"));
		Kit.Name = KitReader.RequiredString(TEXT("name"));
		Kit.Squeeze = KitReader.OptionalAtLeast(TEXT("squeeze"), 1.0, 1.0);
		Kit.ImageCircleMm = KitReader.RequiredPositive(TEXT("imageCircleMm"));
		Kit.MinApertureTStop = KitReader.OptionalAtLeast(TEXT("minApertureTStop"), 1.0, 22.0);
		Kit.IrisBlades = FMath::RoundToInt32(KitReader.OptionalAtLeast(TEXT("irisBlades"), 3.0, 0.0));

		const TArray<TSharedPtr<FJsonObject>> LensObjects = KitReader.ObjectArray(TEXT("lenses"), true);
		for (int32 LensIndex = 0; LensIndex < LensObjects.Num(); ++LensIndex)
		{
			FUEcineJsonEntryReader LensReader(LensObjects[LensIndex], FString::Printf(TEXT("%s lenses[%d]"), *KitPath, LensIndex), OutErrors);

			FUEcineLens Lens;
			Lens.FocalLengthMm = LensReader.RequiredPositive(TEXT("focalMm"));
			Lens.MaxFocalLengthMm = LensReader.OptionalAtLeast(TEXT("zoomToMm"), Lens.FocalLengthMm, Lens.FocalLengthMm);
			Lens.TStop = LensReader.RequiredPositive(TEXT("tStop"));
			Lens.CloseFocusM = LensReader.RequiredPositive(TEXT("closeFocusM"));

			if (LensReader.IsValid())
			{
				Kit.Lenses.Add(Lens);
			}
			else
			{
				KitReader.MarkInvalid();
			}
		}

		if (KitReader.IsValid() && FindLensKit(Kit.Id) != nullptr)
		{
			KitReader.Fail(TEXT("id"), *FString::Printf(TEXT("\"%s\" is already used by another lens kit"), *Kit.Id));
		}
		if (KitReader.IsValid())
		{
			Kit.Lenses.Sort([](const FUEcineLens& A, const FUEcineLens& B) { return A.FocalLengthMm < B.FocalLengthMm; });
			LensKits.Add(MoveTemp(Kit));
		}
	}

	return OutErrors.Num() == ErrorCountBefore;
}

void FUEcineCameraDatabase::AddFromDirectory(const FString& Directory, TArray<FString>& OutErrors)
{
	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(FileNames, *(Directory / TEXT("*.json")), true, false);
	FileNames.Sort();

	for (const FString& FileName : FileNames)
	{
		FString JsonText;
		if (FFileHelper::LoadFileToString(JsonText, *(Directory / FileName)))
		{
			AddFromJson(JsonText, FileName, OutErrors);
		}
		else
		{
			OutErrors.Add(FString::Printf(TEXT("%s: the file could not be read."), *FileName));
		}
	}
}

const FUEcineCameraBody* FUEcineCameraDatabase::FindBody(const FString& BodyId) const
{
	return Bodies.FindByPredicate([&BodyId](const FUEcineCameraBody& Body) { return Body.Id == BodyId; });
}

const FUEcineLensKit* FUEcineCameraDatabase::FindLensKit(const FString& KitId) const
{
	return LensKits.FindByPredicate([&KitId](const FUEcineLensKit& Kit) { return Kit.Id == KitId; });
}
