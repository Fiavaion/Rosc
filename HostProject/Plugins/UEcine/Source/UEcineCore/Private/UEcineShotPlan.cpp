// Copyright 2026 Fiavaion. All rights reserved.

#include "UEcineShotPlan.h"

#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UEcineCameraDatabase.h"

namespace
{
	/** What a field of a written step holds. */
	enum class EPlanFieldType : uint8
	{
		Text,
		Number,
		WholeNumber,
		YesOrNo,
		ShotSize
	};

	struct FPlanField
	{
		const TCHAR* Name;
		EPlanFieldType Type;
		/** What the field means, written for the language model that fills it in. */
		const TCHAR* Meaning;
	};

	struct FPlanStepKind
	{
		EUEcineShotStep Kind;
		const TCHAR* Id;
		const TCHAR* Meaning;
		TArray<FPlanField> Fields;
	};

	/** Every kind of step: the schema is written from this list and the reader checks against it. */
	const TArray<FPlanStepKind>& GetPlanStepKinds()
	{
		using enum EPlanFieldType;
		static const TArray<FPlanStepKind> Kinds =
		{
			{ EUEcineShotStep::ChooseCamera, TEXT("choose_camera"),
				TEXT("Choose the camera body and its recording format. Use only when the request asks for a different camera or format."),
				{
					{ TEXT("bodyId"), Text, TEXT("The id of a body in the equipment list.") },
					{ TEXT("formatId"), Text, TEXT("The id of one of that body's formats.") },
				} },
			{ EUEcineShotStep::ChooseLens, TEXT("choose_lens"),
				TEXT("Put on a lens. Use when the request names a lens, a focal length or a field of view, or when the shot size asked for needs a different lens."),
				{
					{ TEXT("lensKitId"), Text, TEXT("The id of a lens kit in the equipment list. Keep the camera's own kit unless the request asks for another.") },
					{ TEXT("focalLengthMm"), Number, TEXT("The focal length wanted. The lens of the kit nearest to it is put on, so choose one the kit has.") },
				} },
			{ EUEcineShotStep::SetExposure, TEXT("set_exposure"),
				TEXT("Set the exposure. Give all four values, repeating the camera's own value for any that should not change."),
				{
					{ TEXT("tStop"), Number, TEXT("The T-stop, such as 2.8. A lower number is a shallower depth of field.") },
					{ TEXT("iso"), WholeNumber, TEXT("The ISO, such as 800.") },
					{ TEXT("shutterAngleDeg"), Number, TEXT("The shutter angle, from 1 to 360. 180 is normal motion blur.") },
					{ TEXT("ndStops"), Number, TEXT("Stops of neutral density in front of the lens: 0 for none, 1 for ND 0.3, 2 for ND 0.6 and so on.") },
				} },
			{ EUEcineShotStep::PutOnSupport, TEXT("put_on_support"),
				TEXT("Put the camera on a support, which is stood up under the camera where it is. Leave this out when the camera is already on the right support."),
				{
					{ TEXT("supportId"), Text, TEXT("The id of a support in the equipment list.") },
				} },
			{ EUEcineShotStep::MoveToMark, TEXT("move_to_mark"),
				TEXT("Move the rig from where it stands to a new mark. The first of these in a plan marks where the rig stands now as the start of the move, in place of any marks it had. Each value says how far that axis goes from where it is, not where it ends up. Give 0 for an axis that stays still, that the support does not have, or that is fixed during a shot."),
				{
					{ TEXT("travelM"), Number, TEXT("Along the track or the slider rail; forwards for Steadicam, handheld and drone. Forwards is the way the camera looked when the support was stood up. Metres; negative is backwards.") },
					{ TEXT("sidewaysM"), Number, TEXT("To the right, for Steadicam, handheld and drone. Metres; negative is left.") },
					{ TEXT("liftM"), Number, TEXT("Up: the dolly's column, the lens height of a Steadicam or handheld camera, a drone's altitude. Metres; negative is down. A jib or crane raises the camera with boomDeg.") },
					{ TEXT("swingDeg"), Number, TEXT("The arm of a jib or crane swinging to the right. Degrees; negative is left.") },
					{ TEXT("boomDeg"), Number, TEXT("The arm of a jib or crane rising. Degrees; negative is down.") },
					{ TEXT("extensionM"), Number, TEXT("The arm of a telescoping crane reaching out. Metres; negative is in.") },
					{ TEXT("panDeg"), Number, TEXT("The head panning right. Degrees; negative is left.") },
					{ TEXT("tiltDeg"), Number, TEXT("The head tilting up. Degrees; negative is down.") },
					{ TEXT("rollDeg"), Number, TEXT("The head rolling clockwise. Degrees; negative is anticlockwise.") },
					{ TEXT("durationSeconds"), Number, TEXT("How long the move takes. Allow enough for the support's top speeds in the equipment list.") },
					{ TEXT("easeIn"), Number, TEXT("The share of the move spent getting up to speed, from 0 to 1. 0.25 is a feathered start.") },
					{ TEXT("easeOut"), Number, TEXT("The share of the move spent coming to rest, from 0 to 1. easeIn and easeOut together must not pass 1.") },
				} },
			{ EUEcineShotStep::FocusOn, TEXT("focus_on"),
				TEXT("Focus on something in the level."),
				{
					{ TEXT("subject"), Text, TEXT("The name of a subject in the set, exactly as it is written there.") },
					{ TEXT("follow"), YesOrNo, TEXT("true to keep the focus on the subject as it and the camera move; false to set the focus once.") },
				} },
			{ EUEcineShotStep::FocusAt, TEXT("focus_at"),
				TEXT("Set the focus to a distance."),
				{
					{ TEXT("distanceM"), Number, TEXT("Metres from the camera.") },
				} },
			{ EUEcineShotStep::AddShot, TEXT("add_shot"),
				TEXT("Add a shot to the sequence, seen through this camera, and fill in its line of the shot list. Put it before bake_move so that the move is written into the new shot."),
				{
					{ TEXT("scene"), Text, TEXT("The scene's number or name; empty when the request gives none.") },
					{ TEXT("size"), ShotSize, TEXT("How much of the subject the shot holds.") },
					{ TEXT("move"), Text, TEXT("The move in a few words for the shot list, such as \"Push in\" or \"Crane up\"; empty for a camera that stays still.") },
					{ TEXT("notes"), Text, TEXT("Notes for the shot list; empty when there are none.") },
				} },
			{ EUEcineShotStep::BakeMove, TEXT("bake_move"),
				TEXT("Write the move through the marks into the sequence as keys, starting at the playhead. Use it when the plan has a move."),
				{} },
		};
		return Kinds;
	}

	const TCHAR* StepKindField = TEXT("step");

	struct FPlanAxis
	{
		EUEcineRigAxis Axis;
		const TCHAR* Id;
	};

	TConstArrayView<FPlanAxis> GetPlanAxes()
	{
		static const FPlanAxis Axes[] =
		{
			{ EUEcineRigAxis::Travel, TEXT("travelM") },
			{ EUEcineRigAxis::Traverse, TEXT("sidewaysM") },
			{ EUEcineRigAxis::Lift, TEXT("liftM") },
			{ EUEcineRigAxis::Swing, TEXT("swingDeg") },
			{ EUEcineRigAxis::Boom, TEXT("boomDeg") },
			{ EUEcineRigAxis::Extension, TEXT("extensionM") },
			{ EUEcineRigAxis::Pan, TEXT("panDeg") },
			{ EUEcineRigAxis::Tilt, TEXT("tiltDeg") },
			{ EUEcineRigAxis::Roll, TEXT("rollDeg") },
		};
		return Axes;
	}

	/** A plan length or angle in the units rigs keep: centimetres and degrees. */
	double FromPlanUnits(EUEcineRigAxis Axis, double Value)
	{
		return UEcine::Rig::IsDistanceAxis(Axis) ? Value * 100.0 : Value;
	}

	TSharedRef<FJsonObject> MakeSchemaField(const TCHAR* Type, const TCHAR* Meaning)
	{
		const TSharedRef<FJsonObject> Field = MakeShared<FJsonObject>();
		Field->SetStringField(TEXT("type"), Type);
		Field->SetStringField(TEXT("description"), Meaning);
		return Field;
	}

	TSharedRef<FJsonObject> MakeSchemaObject(const TSharedRef<FJsonObject>& Properties, const TArray<FString>& Required)
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("type"), TEXT("object"));
		Object->SetObjectField(TEXT("properties"), Properties);
		TArray<TSharedPtr<FJsonValue>> RequiredValues;
		for (const FString& Name : Required)
		{
			RequiredValues.Add(MakeShared<FJsonValueString>(Name));
		}
		Object->SetArrayField(TEXT("required"), RequiredValues);
		Object->SetBoolField(TEXT("additionalProperties"), false);
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> MakeTextValues(TConstArrayView<const TCHAR*> Texts)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const TCHAR* Text : Texts)
		{
			Values.Add(MakeShared<FJsonValueString>(Text));
		}
		return Values;
	}

	FString WriteJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Object, Writer);
		return Text;
	}

	FString BuildJsonSchema()
	{
		TArray<TSharedPtr<FJsonValue>> StepSchemas;
		for (const FPlanStepKind& Kind : GetPlanStepKinds())
		{
			const TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
			TArray<FString> Required = { StepKindField };

			const TSharedRef<FJsonObject> KindField = MakeSchemaField(TEXT("string"), Kind.Meaning);
			KindField->SetArrayField(TEXT("enum"), MakeTextValues({ Kind.Id }));
			Properties->SetObjectField(StepKindField, KindField);

			for (const FPlanField& Field : Kind.Fields)
			{
				TSharedPtr<FJsonObject> FieldSchema;
				switch (Field.Type)
				{
				case EPlanFieldType::Text: FieldSchema = MakeSchemaField(TEXT("string"), Field.Meaning); break;
				case EPlanFieldType::Number: FieldSchema = MakeSchemaField(TEXT("number"), Field.Meaning); break;
				case EPlanFieldType::WholeNumber: FieldSchema = MakeSchemaField(TEXT("integer"), Field.Meaning); break;
				case EPlanFieldType::YesOrNo: FieldSchema = MakeSchemaField(TEXT("boolean"), Field.Meaning); break;
				case EPlanFieldType::ShotSize:
				{
					FieldSchema = MakeSchemaField(TEXT("string"), Field.Meaning);
					TArray<const TCHAR*> SizeIds;
					for (const EUEcineShotSize Size : UEcine::Shots::GetShotSizes())
					{
						SizeIds.Add(UEcine::ShotPlan::GetShotSizeId(Size));
					}
					FieldSchema->SetArrayField(TEXT("enum"), MakeTextValues(SizeIds));
					break;
				}
				}
				Properties->SetObjectField(Field.Name, FieldSchema);
				Required.Add(Field.Name);
			}
			StepSchemas.Add(MakeShared<FJsonValueObject>(MakeSchemaObject(Properties, Required)));
		}

		const TSharedRef<FJsonObject> StepChoice = MakeShared<FJsonObject>();
		StepChoice->SetArrayField(TEXT("anyOf"), StepSchemas);
		const TSharedRef<FJsonObject> Steps = MakeSchemaField(TEXT("array"), TEXT("The steps, in the order they are carried out. Empty when nothing the request asks for can be done with these steps."));
		Steps->SetObjectField(TEXT("items"), StepChoice);

		const TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		Properties->SetObjectField(TEXT("summary"), MakeSchemaField(TEXT("string"),
			TEXT("What the plan does, in one to three sentences for the director, in film language. Say what you decided where the request left it open, and say plainly what was asked for that these steps cannot do.")));
		Properties->SetObjectField(TEXT("steps"), Steps);
		return WriteJson(MakeSchemaObject(Properties, { TEXT("summary"), TEXT("steps") }));
	}

	/** Reads the fields of one written step, having found that each is there and of the right type. */
	class FPlanStepReader
	{
	public:
		FPlanStepReader(const FJsonObject& InObject, const FString& InPath, FString& InError)
			: Object(InObject), Path(InPath), Error(InError)
		{
		}

		bool IsValid() const { return Error.IsEmpty(); }

		FString Text(const TCHAR* Field) const { return Object.GetStringField(Field); }
		double Number(const TCHAR* Field) const { return Object.GetNumberField(Field); }
		bool YesOrNo(const TCHAR* Field) const { return Object.GetBoolField(Field); }

		FString NonEmptyText(const TCHAR* Field)
		{
			const FString Value = Text(Field);
			if (Value.IsEmpty())
			{
				Fail(Field, TEXT("must not be empty"));
			}
			return Value;
		}

		double Positive(const TCHAR* Field)
		{
			const double Value = Number(Field);
			if (Value <= 0.0)
			{
				Fail(Field, TEXT("must be greater than zero"));
			}
			return Value;
		}

		double Between(const TCHAR* Field, double Low, double High)
		{
			const double Value = Number(Field);
			if (Value < Low || Value > High)
			{
				Fail(Field, *FString::Printf(TEXT("must be from %g to %g"), Low, High));
			}
			return Value;
		}

		void Fail(const TCHAR* Field, const TCHAR* Problem)
		{
			if (Error.IsEmpty())
			{
				Error = FString::Printf(TEXT("%s: \"%s\" %s."), *Path, Field, Problem);
			}
		}

	private:
		const FJsonObject& Object;
		FString Path;
		FString& Error;
	};

	bool HasFieldOfType(const FJsonObject& Object, const FPlanField& Field)
	{
		const TSharedPtr<FJsonValue> Value = Object.TryGetField(Field.Name);
		if (!Value.IsValid())
		{
			return false;
		}
		double Number = 0.0;
		switch (Field.Type)
		{
		case EPlanFieldType::Text:
		case EPlanFieldType::ShotSize:
			return Value->Type == EJson::String;
		case EPlanFieldType::Number:
			return Value->Type == EJson::Number && FMath::IsFinite(Value->AsNumber());
		case EPlanFieldType::WholeNumber:
			return Value->Type == EJson::Number && Value->TryGetNumber(Number) && Number == FMath::RoundToDouble(Number) && FMath::Abs(Number) < MAX_int32;
		case EPlanFieldType::YesOrNo:
			return Value->Type == EJson::Boolean;
		}
		return false;
	}

	const TCHAR* DescribeFieldType(EPlanFieldType Type)
	{
		switch (Type)
		{
		case EPlanFieldType::Number: return TEXT("is required and must be a number");
		case EPlanFieldType::WholeNumber: return TEXT("is required and must be a whole number");
		case EPlanFieldType::YesOrNo: return TEXT("is required and must be true or false");
		default: return TEXT("is required and must be text");
		}
	}

	bool ReadStep(const FJsonObject& Object, const FString& Path, FUEcineShotStep& OutStep, FString& OutError)
	{
		FString KindId;
		Object.TryGetStringField(StepKindField, KindId);
		const FPlanStepKind* Kind = GetPlanStepKinds().FindByPredicate([&KindId](const FPlanStepKind& Each) { return KindId == Each.Id; });
		if (Kind == nullptr)
		{
			OutError = FString::Printf(TEXT("%s: \"%s\" must name a kind of step; \"%s\" is not one."), *Path, StepKindField, *KindId);
			return false;
		}

		const FString StepPath = FString::Printf(TEXT("%s (%s)"), *Path, Kind->Id);
		FPlanStepReader Reader(Object, StepPath, OutError);
		for (const FPlanField& Field : Kind->Fields)
		{
			if (!HasFieldOfType(Object, Field))
			{
				Reader.Fail(Field.Name, DescribeFieldType(Field.Type));
				return false;
			}
		}

		OutStep.Kind = Kind->Kind;
		switch (Kind->Kind)
		{
		case EUEcineShotStep::ChooseCamera:
			OutStep.BodyId = Reader.NonEmptyText(TEXT("bodyId"));
			OutStep.FormatId = Reader.NonEmptyText(TEXT("formatId"));
			break;

		case EUEcineShotStep::ChooseLens:
			OutStep.LensKitId = Reader.NonEmptyText(TEXT("lensKitId"));
			OutStep.FocalLengthMm = Reader.Positive(TEXT("focalLengthMm"));
			break;

		case EUEcineShotStep::SetExposure:
			OutStep.TStop = Reader.Positive(TEXT("tStop"));
			OutStep.Iso = static_cast<int32>(Reader.Positive(TEXT("iso")));
			OutStep.ShutterAngleDeg = Reader.Between(TEXT("shutterAngleDeg"), 1.0, 360.0);
			OutStep.NdStops = Reader.Between(TEXT("ndStops"), 0.0, 20.0);
			break;

		case EUEcineShotStep::PutOnSupport:
			OutStep.SupportId = Reader.NonEmptyText(TEXT("supportId"));
			break;

		case EUEcineShotStep::MoveToMark:
			for (const FPlanAxis& Axis : GetPlanAxes())
			{
				OutStep.Change.Set(Axis.Axis, FromPlanUnits(Axis.Axis, Reader.Number(Axis.Id)));
			}
			OutStep.Timing.DurationSeconds = Reader.Positive(TEXT("durationSeconds"));
			OutStep.Timing.EaseInFraction = Reader.Between(TEXT("easeIn"), 0.0, 1.0);
			OutStep.Timing.EaseOutFraction = Reader.Between(TEXT("easeOut"), 0.0, 1.0);
			if (OutStep.Timing.EaseInFraction + OutStep.Timing.EaseOutFraction > 1.0 + UE_KINDA_SMALL_NUMBER)
			{
				Reader.Fail(TEXT("easeOut"), TEXT("and \"easeIn\" together must not pass 1"));
			}
			break;

		case EUEcineShotStep::FocusOn:
			OutStep.Subject = Reader.NonEmptyText(TEXT("subject"));
			OutStep.bFollow = Reader.YesOrNo(TEXT("follow"));
			break;

		case EUEcineShotStep::FocusAt:
			OutStep.FocusDistanceCm = Reader.Positive(TEXT("distanceM")) * 100.0;
			break;

		case EUEcineShotStep::AddShot:
		{
			OutStep.Scene = Reader.Text(TEXT("scene"));
			OutStep.Move = Reader.Text(TEXT("move"));
			OutStep.Notes = Reader.Text(TEXT("notes"));
			const FString SizeId = Reader.Text(TEXT("size"));
			const TConstArrayView<EUEcineShotSize> Sizes = UEcine::Shots::GetShotSizes();
			const EUEcineShotSize* Size = Sizes.FindByPredicate([&SizeId](EUEcineShotSize Each) { return SizeId == UEcine::ShotPlan::GetShotSizeId(Each); });
			if (Size == nullptr)
			{
				Reader.Fail(TEXT("size"), TEXT("must be a shot size"));
				break;
			}
			OutStep.Size = *Size;
			break;
		}

		case EUEcineShotStep::BakeMove:
			break;
		}
		return Reader.IsValid();
	}
}

const TCHAR* UEcine::ShotPlan::GetStepId(EUEcineShotStep Kind)
{
	const FPlanStepKind* Found = GetPlanStepKinds().FindByPredicate([Kind](const FPlanStepKind& Each) { return Each.Kind == Kind; });
	check(Found != nullptr);
	return Found->Id;
}

const TCHAR* UEcine::ShotPlan::GetShotSizeId(EUEcineShotSize Size)
{
	switch (Size)
	{
	case EUEcineShotSize::Unset: return TEXT("not_set");
	case EUEcineShotSize::ExtremeWide: return TEXT("extreme_wide");
	case EUEcineShotSize::Wide: return TEXT("wide");
	case EUEcineShotSize::Full: return TEXT("full");
	case EUEcineShotSize::MediumWide: return TEXT("medium_wide");
	case EUEcineShotSize::Medium: return TEXT("medium");
	case EUEcineShotSize::MediumCloseUp: return TEXT("medium_close_up");
	case EUEcineShotSize::CloseUp: return TEXT("close_up");
	case EUEcineShotSize::ExtremeCloseUp: return TEXT("extreme_close_up");
	case EUEcineShotSize::Insert: return TEXT("insert");
	case EUEcineShotSize::OverTheShoulder: return TEXT("over_the_shoulder");
	case EUEcineShotSize::PointOfView: return TEXT("point_of_view");
	case EUEcineShotSize::TwoShot: return TEXT("two_shot");
	}
	checkNoEntry();
	return TEXT("");
}

const TCHAR* UEcine::ShotPlan::GetAxisId(EUEcineRigAxis Axis)
{
	const FPlanAxis* Found = GetPlanAxes().FindByPredicate([Axis](const FPlanAxis& Each) { return Each.Axis == Axis; });
	check(Found != nullptr);
	return Found->Id;
}

double UEcine::ShotPlan::ToPlanUnits(EUEcineRigAxis Axis, double Value)
{
	return UEcine::Rig::IsDistanceAxis(Axis) ? Value / 100.0 : Value;
}

const FString& UEcine::ShotPlan::GetJsonSchema()
{
	static const FString Schema = BuildJsonSchema();
	return Schema;
}

bool UEcine::ShotPlan::Parse(const FString& JsonText, FUEcineShotPlan& OutPlan, FString& OutError)
{
	OutPlan = FUEcineShotPlan();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("The plan is not valid JSON (%s)."), *Reader->GetErrorMessage());
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* StepValues = nullptr;
	if (!Root->TryGetStringField(TEXT("summary"), OutPlan.Summary) || !Root->TryGetArrayField(TEXT("steps"), StepValues))
	{
		OutError = TEXT("The plan must have a \"summary\" and a list of \"steps\".");
		return false;
	}

	for (int32 Index = 0; Index < StepValues->Num(); ++Index)
	{
		const FString Path = FString::Printf(TEXT("Step %d"), Index + 1);
		const TSharedPtr<FJsonObject>* StepObject = nullptr;
		if (!(*StepValues)[Index]->TryGetObject(StepObject))
		{
			OutError = Path + TEXT(" is not a step.");
		}
		else
		{
			ReadStep(**StepObject, Path, OutPlan.Steps.AddDefaulted_GetRef(), OutError);
		}
		if (!OutError.IsEmpty())
		{
			OutPlan = FUEcineShotPlan();
			return false;
		}
	}
	return true;
}

TSharedRef<FJsonValue> UEcine::ShotPlan::MakeNumber(double Value, int32 Decimals)
{
	FString Text = FString::Printf(TEXT("%.*f"), Decimals, Value);
	if (Text.Contains(TEXT(".")))
	{
		while (Text.EndsWith(TEXT("0")))
		{
			Text.LeftChopInline(1);
		}
		Text.RemoveFromEnd(TEXT("."));
	}
	if (Text == TEXT("-0"))
	{
		Text = TEXT("0");
	}
	return MakeShared<FJsonValueNumberString>(Text);
}

FString UEcine::ShotPlan::DescribeEquipment(const FUEcineCameraDatabase& Database)
{
	// Rounded as a crew would say them: a model has no use for the fifth decimal of a rail length.
	const auto Round = [](double Value) { return MakeNumber(Value, 2); };

	TArray<TSharedPtr<FJsonValue>> Bodies;
	for (const FUEcineCameraBody& Body : Database.GetBodies())
	{
		TArray<TSharedPtr<FJsonValue>> Formats;
		for (const FUEcineRecordingFormat& Format : Body.Formats)
		{
			const TSharedRef<FJsonObject> FormatObject = MakeShared<FJsonObject>();
			FormatObject->SetStringField(TEXT("id"), Format.Id);
			FormatObject->SetStringField(TEXT("name"), Format.Name);
			const TArray<TSharedPtr<FJsonValue>> SensorSize = { Round(Format.SensorWidthMm), Round(Format.SensorHeightMm) };
			FormatObject->SetArrayField(TEXT("sensorMm"), SensorSize);
			Formats.Add(MakeShared<FJsonValueObject>(FormatObject));
		}
		const TSharedRef<FJsonObject> BodyObject = MakeShared<FJsonObject>();
		BodyObject->SetStringField(TEXT("id"), Body.Id);
		BodyObject->SetStringField(TEXT("name"), Body.GetDisplayName());
		BodyObject->SetArrayField(TEXT("formats"), Formats);
		Bodies.Add(MakeShared<FJsonValueObject>(BodyObject));
	}

	TArray<TSharedPtr<FJsonValue>> LensKits;
	for (const FUEcineLensKit& Kit : Database.GetLensKits())
	{
		TArray<TSharedPtr<FJsonValue>> Lenses;
		for (const FUEcineLens& Lens : Kit.Lenses)
		{
			Lenses.Add(MakeShared<FJsonValueString>(Lens.GetDisplayName()));
		}
		const TSharedRef<FJsonObject> KitObject = MakeShared<FJsonObject>();
		KitObject->SetStringField(TEXT("id"), Kit.Id);
		KitObject->SetStringField(TEXT("name"), Kit.GetDisplayName());
		if (Kit.IsAnamorphic())
		{
			KitObject->SetField(TEXT("anamorphicSqueeze"), Round(Kit.Squeeze));
		}
		KitObject->SetField(TEXT("imageCircleMm"), Round(Kit.ImageCircleMm));
		KitObject->SetArrayField(TEXT("lenses"), Lenses);
		LensKits.Add(MakeShared<FJsonValueObject>(KitObject));
	}

	// Past this an axis has no end worth telling: a Steadicam goes where its operator walks.
	const double FarthestWorthTelling = 100000.0;
	TArray<TSharedPtr<FJsonValue>> Supports;
	for (const FUEcineRigPreset& Preset : UEcine::Rig::GetPresets())
	{
		const TSharedRef<FJsonObject> Axes = MakeShared<FJsonObject>();
		for (const FPlanAxis& Axis : GetPlanAxes())
		{
			const FUEcineAxisLimits& Limits = Preset.Spec.GetAxis(Axis.Axis);
			if (!Limits.bUsed)
			{
				continue;
			}
			const TSharedRef<FJsonObject> AxisObject = MakeShared<FJsonObject>();
			if (FMath::Abs(Limits.Min) < FarthestWorthTelling && FMath::Abs(Limits.Max) < FarthestWorthTelling)
			{
				AxisObject->SetField(TEXT("from"), Round(ToPlanUnits(Axis.Axis, Limits.Min)));
				AxisObject->SetField(TEXT("to"), Round(ToPlanUnits(Axis.Axis, Limits.Max)));
			}
			if (Limits.IsFixedDuringShot())
			{
				AxisObject->SetBoolField(TEXT("fixedDuringShot"), true);
			}
			else
			{
				AxisObject->SetField(TEXT("topSpeedPerSecond"), Round(ToPlanUnits(Axis.Axis, Limits.MaxSpeed)));
			}
			Axes->SetObjectField(Axis.Id, AxisObject);
		}
		const TSharedRef<FJsonObject> SupportObject = MakeShared<FJsonObject>();
		SupportObject->SetStringField(TEXT("id"), Preset.Id);
		SupportObject->SetStringField(TEXT("name"), Preset.Name.ToString());
		SupportObject->SetObjectField(TEXT("axes"), Axes);
		Supports.Add(MakeShared<FJsonValueObject>(SupportObject));
	}

	const TSharedRef<FJsonObject> Equipment = MakeShared<FJsonObject>();
	Equipment->SetArrayField(TEXT("bodies"), Bodies);
	Equipment->SetArrayField(TEXT("lensKits"), LensKits);
	Equipment->SetArrayField(TEXT("supports"), Supports);
	return WriteJson(Equipment);
}
