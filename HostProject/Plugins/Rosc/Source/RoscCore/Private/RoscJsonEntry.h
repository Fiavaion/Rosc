// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Reads fields from one JSON object and records what is wrong, with the path to the field. */
class FRoscJsonEntryReader
{
public:
	FRoscJsonEntryReader(const TSharedPtr<FJsonObject>& InObject, const FString& InPath, TArray<FString>& InErrors)
		: Object(InObject), Path(InPath), Errors(InErrors)
	{
	}

	bool IsValid() const { return bValid; }

	FString RequiredString(const TCHAR* Field)
	{
		FString Value;
		if (!Object->TryGetStringField(Field, Value) || Value.IsEmpty())
		{
			Fail(Field, TEXT("is required and must be a non-empty string"));
		}
		return Value;
	}

	FString OptionalString(const TCHAR* Field)
	{
		FString Value;
		Object->TryGetStringField(Field, Value);
		return Value;
	}

	double RequiredPositive(const TCHAR* Field)
	{
		double Value = 0.0;
		if (!Object->TryGetNumberField(Field, Value) || Value <= 0.0)
		{
			Fail(Field, TEXT("is required and must be a number greater than zero"));
		}
		return Value;
	}

	/** Returns Default when the field is absent; fails when present but below Minimum. */
	double OptionalAtLeast(const TCHAR* Field, double Minimum, double Default)
	{
		if (!Object->HasField(Field))
		{
			return Default;
		}
		double Value = 0.0;
		if (!Object->TryGetNumberField(Field, Value) || Value < Minimum)
		{
			Fail(Field, *FString::Printf(TEXT("must be a number of at least %g"), Minimum));
			return Default;
		}
		return Value;
	}

	/** Reads an array of exactly two positive numbers, such as a sensor size. */
	bool RequiredPositivePair(const TCHAR* Field, double& OutFirst, double& OutSecond)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object->TryGetArrayField(Field, Values) || Values->Num() != 2
			|| !(*Values)[0]->TryGetNumber(OutFirst) || !(*Values)[1]->TryGetNumber(OutSecond)
			|| OutFirst <= 0.0 || OutSecond <= 0.0)
		{
			Fail(Field, TEXT("is required and must be two numbers greater than zero"));
			return false;
		}
		return true;
	}

	/** Returns the objects of an array field; non-object elements are reported. */
	TArray<TSharedPtr<FJsonObject>> ObjectArray(const TCHAR* Field, bool bRequireNonEmpty)
	{
		TArray<TSharedPtr<FJsonObject>> Result;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				const TSharedPtr<FJsonObject>* Element = nullptr;
				if (Value->TryGetObject(Element))
				{
					Result.Add(*Element);
				}
				else
				{
					Fail(Field, TEXT("must contain only objects"));
				}
			}
		}
		if (bRequireNonEmpty && Result.IsEmpty())
		{
			Fail(Field, TEXT("is required and must contain at least one entry"));
		}
		return Result;
	}

	TArray<int32> OptionalPositiveIntArray(const TCHAR* Field)
	{
		TArray<int32> Result;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				int32 Number = 0;
				if (Value->TryGetNumber(Number) && Number > 0)
				{
					Result.Add(Number);
				}
				else
				{
					Fail(Field, TEXT("must contain only whole numbers greater than zero"));
				}
			}
		}
		return Result;
	}

	/** Returns an object field, or null with the fault recorded when it is absent. */
	TSharedPtr<FJsonObject> RequiredObject(const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Value = nullptr;
		if (!Object->TryGetObjectField(Field, Value))
		{
			Fail(Field, TEXT("is required and must be an object"));
			return nullptr;
		}
		return *Value;
	}

	/** Reads an array of exactly three numbers, such as one value for each of red, green and blue. */
	bool RequiredTriple(const TCHAR* Field, double& OutFirst, double& OutSecond, double& OutThird)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object->TryGetArrayField(Field, Values) || Values->Num() != 3
			|| !(*Values)[0]->TryGetNumber(OutFirst) || !(*Values)[1]->TryGetNumber(OutSecond) || !(*Values)[2]->TryGetNumber(OutThird))
		{
			Fail(Field, TEXT("is required and must be three numbers"));
			return false;
		}
		return true;
	}

	bool Has(const TCHAR* Field) const { return Object->HasField(Field); }

	void Fail(const TCHAR* Field, const TCHAR* Problem)
	{
		Errors.Add(FString::Printf(TEXT("%s: \"%s\" %s."), *Path, Field, Problem));
		bValid = false;
	}

	void MarkInvalid() { bValid = false; }

private:
	TSharedPtr<FJsonObject> Object;
	FString Path;
	TArray<FString>& Errors;
	bool bValid = true;
};
