// Asset Graph Exporter

#include "AGEJsonDiff.h"
#include "AGEJsonQuery.h"
#include "Dom/JsonObject.h"

// Named (not anonymous) so these helpers cannot clash with other files in a unity build.
namespace AGE::JsonDiffDetail
{
	using AGE::JsonDiff::FChange;

	/** Longest value text in a change line. */
	constexpr int32 MaxValueChars = 120;
	/** Largest line-diff table (lines before x lines after). */
	constexpr int64 MaxLineCells = 4000000;

	FString Shorten(const FString& Text)
	{
		return Text.Len() <= MaxValueChars ? Text : Text.Left(MaxValueChars - 3) + TEXT("...");
	}

	/** One line for a value: graph nodes by class and title, named objects by name, containers by size, the rest as JSON. */
	FString Describe(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return FString();
		}
		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			FString Class, Title, Name;
			if (Object->TryGetStringField(TEXT("class"), Class) && Object->TryGetStringField(TEXT("title"), Title))
			{
				int32 Break = INDEX_NONE;
				if (Title.FindChar(TEXT('\n'), Break))
				{
					Title.LeftInline(Break);
				}
				Class.RemoveFromStart(TEXT("K2Node_"));
				return Shorten(FString::Printf(TEXT("%s '%s'"), *Class, *Title));
			}
			if (Object->TryGetStringField(TEXT("name"), Name))
			{
				return Shorten(FString::Printf(TEXT("{name: %s, %d fields}"), *Name, Object->Values.Num()));
			}
			return FString::Printf(TEXT("{%d fields}"), Object->Values.Num());
		}
		if (Value->Type == EJson::Array)
		{
			return FString::Printf(TEXT("[%d items]"), Value->AsArray().Num());
		}
		return Shorten(AGE::JsonQuery::ToCompactString(Value));
	}

	/** The key items of an array are paired by: "guid" when every item is an object with one, else "name" likewise, else none. */
	FString PairingKey(const TArray<TSharedPtr<FJsonValue>>& Items)
	{
		for (const TCHAR* Key : { TEXT("guid"), TEXT("name") })
		{
			TSet<FString> Seen;
			bool bAll = !Items.IsEmpty();
			for (const TSharedPtr<FJsonValue>& Item : Items)
			{
				FString Id;
				const TSharedPtr<FJsonObject>* Object = nullptr;
				bool bDuplicate = false;
				if (!Item->TryGetObject(Object) || !(*Object)->TryGetStringField(Key, Id))
				{
					bAll = false;
					break;
				}
				Seen.Add(Id, &bDuplicate);
				if (bDuplicate)
				{
					bAll = false;
					break;
				}
			}
			if (bAll)
			{
				return Key;
			}
		}
		return FString();
	}

	FString ItemId(const TSharedPtr<FJsonValue>& Item, const FString& Key)
	{
		return Item->AsObject()->GetStringField(Key);
	}

	/** [Name] when the item has a name (what select matches), else [index]. */
	FString Step(const TSharedPtr<FJsonValue>& Item, int32 Index)
	{
		FString Name;
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Item->TryGetObject(Object) && (*Object)->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
		{
			return FString::Printf(TEXT("[%s]"), *Name);
		}
		return FString::Printf(TEXT("[%d]"), Index);
	}

	void Add(TArray<FChange>& Changes, const TCHAR* Kind, const FString& Path, const TSharedPtr<FJsonValue>& Before, const TSharedPtr<FJsonValue>& After)
	{
		Changes.Add({ Kind, Path, Describe(Before), Describe(After) });
	}

	void Compare(const TSharedPtr<FJsonValue>& Before, const TSharedPtr<FJsonValue>& After, const FString& Path, TArray<FChange>& Changes)
	{
		if (Before->Type != After->Type || (Before->Type != EJson::Object && Before->Type != EJson::Array))
		{
			if (AGE::JsonQuery::ToCompactString(Before) != AGE::JsonQuery::ToCompactString(After))
			{
				Add(Changes, TEXT("changed"), Path, Before, After);
			}
			return;
		}

		const FString Prefix = Path.IsEmpty() ? FString() : Path + TEXT(".");
		if (Before->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Old = Before->AsObject();
			const TSharedPtr<FJsonObject> New = After->AsObject();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Old->Values)
			{
				const TSharedPtr<FJsonValue> Other = New->TryGetField(Field.Key);
				if (Other.IsValid())
				{
					Compare(Field.Value, Other, Prefix + Field.Key, Changes);
				}
				else
				{
					Add(Changes, TEXT("removed"), Prefix + Field.Key, Field.Value, nullptr);
				}
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : New->Values)
			{
				if (!Old->HasField(Field.Key))
				{
					Add(Changes, TEXT("added"), Prefix + Field.Key, nullptr, Field.Value);
				}
			}
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>& Old = Before->AsArray();
		const TArray<TSharedPtr<FJsonValue>>& New = After->AsArray();
		const FString OldKey = PairingKey(Old);
		const FString Key = OldKey == PairingKey(New) ? OldKey : FString();
		if (Key.IsEmpty())
		{
			for (int32 Index = 0; Index < FMath::Max(Old.Num(), New.Num()); ++Index)
			{
				if (Index >= New.Num())
				{
					Add(Changes, TEXT("removed"), Path + Step(Old[Index], Index), Old[Index], nullptr);
				}
				else if (Index >= Old.Num())
				{
					Add(Changes, TEXT("added"), Path + Step(New[Index], Index), nullptr, New[Index]);
				}
				else
				{
					Compare(Old[Index], New[Index], Path + Step(New[Index], Index), Changes);
				}
			}
			return;
		}

		TMap<FString, int32> NewIndex;
		for (int32 Index = 0; Index < New.Num(); ++Index)
		{
			NewIndex.Add(ItemId(New[Index], Key), Index);
		}
		TSet<FString> OldIds;
		for (int32 Index = 0; Index < Old.Num(); ++Index)
		{
			const FString Id = ItemId(Old[Index], Key);
			OldIds.Add(Id);
			if (const int32* Match = NewIndex.Find(Id))
			{
				Compare(Old[Index], New[*Match], Path + Step(New[*Match], *Match), Changes);
			}
			else
			{
				Add(Changes, TEXT("removed"), Path + Step(Old[Index], Index), Old[Index], nullptr);
			}
		}
		for (int32 Index = 0; Index < New.Num(); ++Index)
		{
			if (!OldIds.Contains(ItemId(New[Index], Key)))
			{
				Add(Changes, TEXT("added"), Path + Step(New[Index], Index), nullptr, New[Index]);
			}
		}
	}
}

TArray<AGE::JsonDiff::FChange> AGE::JsonDiff::Compare(const TSharedPtr<FJsonValue>& Before, const TSharedPtr<FJsonValue>& After)
{
	TArray<FChange> Changes;
	if (Before.IsValid() && After.IsValid())
	{
		AGE::JsonDiffDetail::Compare(Before, After, FString(), Changes);
	}
	return Changes;
}

TArray<FString> AGE::JsonDiff::DiffLines(const TArray<FString>& Before, const TArray<FString>& After, int32 Context)
{
	const int32 OldCount = Before.Num();
	const int32 NewCount = After.Num();
	if (int64(OldCount + 1) * int64(NewCount + 1) > AGE::JsonDiffDetail::MaxLineCells)
	{
		return { FString(TEXT("(too long to compare line by line)")) };
	}

	// Longest common subsequence table from the end, then a forward walk that emits the edit script.
	TArray<int32> Table;
	Table.SetNumZeroed((OldCount + 1) * (NewCount + 1));
	auto At = [&Table, NewCount](int32 Old, int32 New) -> int32& { return Table[Old * (NewCount + 1) + New]; };
	for (int32 Old = OldCount - 1; Old >= 0; --Old)
	{
		for (int32 New = NewCount - 1; New >= 0; --New)
		{
			At(Old, New) = Before[Old] == After[New] ? At(Old + 1, New + 1) + 1 : FMath::Max(At(Old + 1, New), At(Old, New + 1));
		}
	}

	struct FOp { TCHAR Mark; const FString* Line; };
	TArray<FOp> Ops;
	int32 Old = 0;
	int32 New = 0;
	while (Old < OldCount || New < NewCount)
	{
		if (Old < OldCount && New < NewCount && Before[Old] == After[New])
		{
			Ops.Add({ TEXT(' '), &Before[Old] });
			++Old;
			++New;
		}
		// Removed lines before added ones where both fit, so a changed line reads "- old" then "+ new".
		else if (Old < OldCount && (New == NewCount || At(Old + 1, New) >= At(Old, New + 1)))
		{
			Ops.Add({ TEXT('-'), &Before[Old] });
			++Old;
		}
		else
		{
			Ops.Add({ TEXT('+'), &After[New] });
			++New;
		}
	}

	// Keep changes and Context unchanged lines around them.
	TArray<bool> Keep;
	Keep.SetNumZeroed(Ops.Num());
	for (int32 Index = 0; Index < Ops.Num(); ++Index)
	{
		if (Ops[Index].Mark != TEXT(' '))
		{
			for (int32 Near = FMath::Max(0, Index - Context); Near <= FMath::Min(Ops.Num() - 1, Index + Context); ++Near)
			{
				Keep[Near] = true;
			}
		}
	}
	TArray<FString> Lines;
	bool bSkipped = false;
	for (int32 Index = 0; Index < Ops.Num(); ++Index)
	{
		if (!Keep[Index])
		{
			bSkipped = true;
			continue;
		}
		if (bSkipped && !Lines.IsEmpty())
		{
			Lines.Add(TEXT("  ..."));
		}
		bSkipped = false;
		Lines.Add(FString::Printf(TEXT("%c %s"), Ops[Index].Mark, **Ops[Index].Line));
	}
	return Lines;
}
