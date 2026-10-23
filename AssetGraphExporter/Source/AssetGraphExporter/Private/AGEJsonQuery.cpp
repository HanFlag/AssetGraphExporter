// Asset Graph Exporter

#include "AGEJsonQuery.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Named (not anonymous) so these helpers cannot clash with other files in a unity build.
namespace AGE::JsonQueryDetail
{
	using FJsonArray = TArray<TSharedPtr<FJsonValue>>;

	/** Longest string value shown in an outline line. */
	constexpr int32 MaxPreviewLength = 80;

	const FJsonArray* AsArray(const TSharedPtr<FJsonValue>& Value)
	{
		const FJsonArray* Array = nullptr;
		return Value.IsValid() && Value->TryGetArray(Array) ? Array : nullptr;
	}

	const FJsonObject* AsObject(const TSharedPtr<FJsonValue>& Value)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		return Value.IsValid() && Value->TryGetObject(Object) && Object && Object->IsValid() ? Object->Get() : nullptr;
	}

	FString ItemName(const TSharedPtr<FJsonValue>& Item)
	{
		if (const FJsonObject* Object = AsObject(Item))
		{
			for (const TCHAR* Key : { TEXT("name"), TEXT("path"), TEXT("title") })
			{
				FString Name;
				if (Object->TryGetStringField(Key, Name) && !Name.IsEmpty())
				{
					return Name;
				}
			}
		}
		return FString();
	}

	FString KeyList(const FJsonObject& Object)
	{
		TArray<FString> Keys;
		for (const auto& Pair : Object.Values)
		{
			Keys.Add(FString(*Pair.Key));
		}
		return FString::Join(Keys, TEXT(", "));
	}

	FString SizeText(int32 Chars)
	{
		return Chars >= 1000 ? FString::Printf(TEXT("%.1fK chars"), Chars / 1000.0) : FString::Printf(TEXT("%d chars"), Chars);
	}

	FString Describe(const TSharedPtr<FJsonValue>& Value)
	{
		if (const FJsonObject* Object = AsObject(Value))
		{
			return FString::Printf(TEXT("object, %d keys, %s"), Object->Values.Num(), *SizeText(AGE::JsonQuery::ToCompactString(Value).Len()));
		}
		if (const FJsonArray* Array = AsArray(Value))
		{
			return FString::Printf(TEXT("array, %d items, %s"), Array->Num(), *SizeText(AGE::JsonQuery::ToCompactString(Value).Len()));
		}
		const FString Text = AGE::JsonQuery::ToCompactString(Value);
		return Text.Len() <= MaxPreviewLength ? Text : Text.Left(MaxPreviewLength) + FString::Printf(TEXT("... (%s)"), *SizeText(Text.Len()));
	}
}

TSharedPtr<FJsonValue> AGE::JsonQuery::Select(const TSharedPtr<FJsonValue>& Root, const FString& Path, FString& OutError)
{
	using namespace AGE::JsonQueryDetail;
	TSharedPtr<FJsonValue> Current = Root;
	FString Walked;
	auto Where = [&Walked]() { return Walked.IsEmpty() ? FString(TEXT("(root)")) : Walked; };

	int32 Index = 0;
	const int32 Length = Path.Len();
	while (Index < Length && Current.IsValid())
	{
		if (FChar::IsWhitespace(Path[Index]) || Path[Index] == TEXT('.'))
		{
			++Index;
			continue;
		}

		if (Path[Index] == TEXT('['))
		{
			const int32 Close = Path.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
			if (Close == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("Missing ']' in \"%s\"."), *Path);
				return nullptr;
			}
			const FString Token = Path.Mid(Index + 1, Close - Index - 1).TrimStartAndEnd();
			Index = Close + 1;

			const FJsonArray* Array = AsArray(Current);
			if (!Array)
			{
				OutError = FString::Printf(TEXT("'%s' is not an array, so [%s] cannot be used on it."), *Where(), *Token);
				return nullptr;
			}
			TSharedPtr<FJsonValue> Next;
			if (!Token.IsEmpty() && Token.IsNumeric())
			{
				const int32 Item = FCString::Atoi(*Token);
				if (!Array->IsValidIndex(Item))
				{
					OutError = FString::Printf(TEXT("Index %d is out of range: '%s' has %d items."), Item, *Where(), Array->Num());
					return nullptr;
				}
				Next = (*Array)[Item];
			}
			else
			{
				for (const TSharedPtr<FJsonValue>& Item : *Array)
				{
					if (ItemName(Item) == Token)
					{
						Next = Item;
						break;
					}
				}
				if (!Next.IsValid())
				{
					OutError = FString::Printf(TEXT("No item named \"%s\" in '%s' (matched against name, path, title)."), *Token, *Where());
					return nullptr;
				}
			}
			Walked += TEXT("[") + Token + TEXT("]");
			Current = Next;
			continue;
		}

		int32 End = Index;
		while (End < Length && Path[End] != TEXT('.') && Path[End] != TEXT('['))
		{
			++End;
		}
		const FString Key = Path.Mid(Index, End - Index).TrimStartAndEnd();
		Index = End;

		const FJsonObject* Object = AsObject(Current);
		if (!Object)
		{
			OutError = FString::Printf(TEXT("'%s' is not an object, so it has no key '%s'."), *Where(), *Key);
			return nullptr;
		}
		TSharedPtr<FJsonValue> Next = Object->TryGetField(Key);
		if (!Next.IsValid())
		{
			OutError = FString::Printf(TEXT("'%s' has no key '%s'. Keys: %s"), *Where(), *Key, *KeyList(*Object));
			return nullptr;
		}
		Walked += (Walked.IsEmpty() ? FString() : FString(TEXT("."))) + Key;
		Current = Next;
	}
	return Current;
}

FString AGE::JsonQuery::ToCompactString(const TSharedPtr<FJsonValue>& Value)
{
	using namespace AGE::JsonQueryDetail;
	if (!Value.IsValid())
	{
		return TEXT("null");
	}
	FString Output;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	if (Value->Type == EJson::Object || Value->Type == EJson::Array)
	{
		FJsonSerializer::Serialize(Value, FString(), Writer);
		return Output;
	}
	// Scalars are written inside a one-item array, then the brackets are removed.
	FJsonSerializer::Serialize(TArray<TSharedPtr<FJsonValue>>{ Value }, Writer);
	return Output.Len() >= 2 ? Output.Mid(1, Output.Len() - 2) : Output;
}

FString AGE::JsonQuery::Outline(const TSharedPtr<FJsonValue>& Value, const FString& Path, int32 MaxItems)
{
	using namespace AGE::JsonQueryDetail;
	const FString Base = Path.TrimStartAndEnd();
	TArray<FString> Lines;
	if (const FJsonObject* Object = AsObject(Value))
	{
		Lines.Add(FString::Printf(TEXT("%s: %s"), Base.IsEmpty() ? TEXT("(root)") : *Base, *Describe(Value)));
		for (const auto& Pair : Object->Values)
		{
			const FString Key(*Pair.Key);
			Lines.Add(FString::Printf(TEXT("  %s  %s"), *(Base.IsEmpty() ? Key : Base + TEXT(".") + Key), *Describe(Pair.Value)));
		}
	}
	else if (const FJsonArray* Array = AsArray(Value))
	{
		Lines.Add(FString::Printf(TEXT("%s: %s"), Base.IsEmpty() ? TEXT("(root)") : *Base, *Describe(Value)));
		const int32 Shown = FMath::Min(Array->Num(), FMath::Max(MaxItems, 1));
		for (int32 Item = 0; Item < Shown; ++Item)
		{
			const FString Name = ItemName((*Array)[Item]);
			Lines.Add(FString::Printf(TEXT("  %s[%d]%s  %s"), *Base, Item, Name.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" \"%s\""), *Name), *Describe((*Array)[Item])));
		}
		if (Array->Num() > Shown)
		{
			Lines.Add(FString::Printf(TEXT("  ... %d more items"), Array->Num() - Shown));
		}
	}
	else
	{
		Lines.Add(FString::Printf(TEXT("%s: %s"), Base.IsEmpty() ? TEXT("(root)") : *Base, *Describe(Value)));
	}
	return FString::Join(Lines, TEXT("\n"));
}
