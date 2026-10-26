// Asset Graph Exporter

#include "AGEUsages.h"
#include "AGEAssetQuery.h"
#include "Dom/JsonValue.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

// Named (not anonymous) so these helpers cannot clash with other files in a unity build.
namespace AGE::UsagesDetail
{
	/** Node properties that name a member: FMemberReference values written by the Blueprint handler. */
	const TCHAR* const ReferenceFields[] = { TEXT("FunctionReference"), TEXT("VariableReference"), TEXT("EventReference"), TEXT("DelegateReference") };

	/** Top-level keys that describe the file, not the asset (every Blueprint's "asset.class" names /Script/Engine.Blueprint). */
	const TCHAR* const HeaderFields[] = { TEXT("schemaVersion"), TEXT("exporter"), TEXT("engineVersion"), TEXT("asset"), TEXT("handler") };

	FString FirstLine(const FString& Text)
	{
		FString Line = Text;
		int32 Break = INDEX_NONE;
		if (Line.FindChar(TEXT('\n'), Break))
		{
			Line.LeftInline(Break);
		}
		return Line;
	}

	/** True when Text contains Path as a whole name: not followed by a letter, digit or underscore (GameplayAbility vs GameplayAbilityTask). */
	bool NamesPath(const FString& Text, const FString& Path)
	{
		for (int32 Start = Text.Find(Path, ESearchCase::CaseSensitive); Start != INDEX_NONE;
			Start = Text.Find(Path, ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 1))
		{
			const int32 After = Start + Path.Len();
			if (After >= Text.Len() || !(FChar::IsAlnum(Text[After]) || Text[After] == TEXT('_')))
			{
				return true;
			}
		}
		return false;
	}

	/** Walks one export and collects lines. Graph and node are the innermost ones around the current value. */
	struct FWalker
	{
		const FAGEUsageSymbol& Symbol;
		const UClass* NativeParent = nullptr;
		FString ClassPath;
		TArray<FString> Lines;

		/** "EventGraph: CallFunction 'Get Avatar Actor from Actor Info'" for a node. */
		FString NodeText(const FJsonObject& Node, const FString& Graph) const
		{
			FString Class, Title;
			Node.TryGetStringField(TEXT("class"), Class);
			Node.TryGetStringField(TEXT("title"), Title);
			Class.RemoveFromStart(TEXT("K2Node_"));
			return FString::Printf(TEXT("%s: %s '%s'"), *Graph, *Class, *FirstLine(Title));
		}

		/** The member reference of this node names the symbol's member on the declaring class or a subclass. */
		bool NodeUsesMember(const FJsonObject& Node) const
		{
			const TSharedPtr<FJsonObject>* Properties = nullptr;
			if (!Node.TryGetObjectField(TEXT("properties"), Properties))
			{
				return false;
			}
			for (const TCHAR* Field : ReferenceFields)
			{
				const TSharedPtr<FJsonObject>* Reference = nullptr;
				FString MemberName;
				if (!(*Properties)->TryGetObjectField(Field, Reference) || !(*Reference)->TryGetStringField(TEXT("MemberName"), MemberName)
					|| FName(*MemberName) != Symbol.Member || (*Reference)->HasField(TEXT("MemberScope")))
				{
					continue;
				}
				FString ParentPath;
				bool bSelf = false;
				const UClass* Owner = nullptr;
				if ((*Reference)->TryGetStringField(TEXT("MemberParent"), ParentPath))
				{
					Owner = FindObject<UClass>(nullptr, *ParentPath);
				}
				else if ((*Reference)->TryGetBoolField(TEXT("bSelfContext"), bSelf) && bSelf)
				{
					Owner = NativeParent;
				}
				if (Owner && Owner->IsChildOf(Symbol.DeclaringClass))
				{
					return true;
				}
			}
			return false;
		}

		void Walk(const TSharedPtr<FJsonValue>& Value, const FString& Path, const FString& Graph, const FString& Node)
		{
			if (!Value.IsValid())
			{
				return;
			}
			switch (Value->Type)
			{
			case EJson::String:
				if (Symbol.Member.IsNone() && NamesPath(Value->AsString(), ClassPath))
				{
					Lines.AddUnique(Node.IsEmpty() ? Path : Node);
				}
				break;
			case EJson::Array:
			{
				const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
				for (int32 Index = 0; Index < Items.Num(); ++Index)
				{
					// [Name] where the item has a name, as age_get_asset's select reads it; [n] otherwise.
					FString Name;
					const TSharedPtr<FJsonObject>* Item = nullptr;
					const FString Step = Items[Index]->TryGetObject(Item) && (*Item)->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty()
						? FString::Printf(TEXT("[%s]"), *Name) : FString::Printf(TEXT("[%d]"), Index);
					Walk(Items[Index], Path + Step, Graph, Node);
				}
				break;
			}
			case EJson::Object:
			{
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				FString InnerGraph = Graph;
				FString InnerNode = Node;
				FString Name;
				if (Object->HasTypedField<EJson::Array>(TEXT("nodes")) && Object->TryGetStringField(TEXT("name"), Name))
				{
					InnerGraph = Name;
				}
				else if (Object->HasField(TEXT("guid")) && Object->HasField(TEXT("pins")))
				{
					// The path tells nodes with the same title apart and can be passed to age_get_asset's select.
					InnerNode = NodeText(*Object, Graph) + TEXT("  (") + Path + TEXT(")");
					if (!Symbol.Member.IsNone() && NodeUsesMember(*Object))
					{
						Lines.AddUnique(InnerNode);
					}
				}
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
				{
					Walk(Field.Value, Path.IsEmpty() ? Field.Key : Path + TEXT(".") + Field.Key, InnerGraph, InnerNode);
				}
				break;
			}
			default:
				break;
			}
		}
	};
}

FString FAGEUsageSymbol::Describe() const
{
	const FString ClassName = Class ? Class->GetName() : FString();
	const FString Declaring = DeclaringClass ? DeclaringClass->GetPathName() : FString();
	return Member.IsNone()
		? FString::Printf(TEXT("%s  [class %s]"), *ClassName, *Declaring)
		: FString::Printf(TEXT("%s.%s  [%s, declared in %s]"), *ClassName, *Member.ToString(), *Kind, *Declaring);
}

bool AGE::Usages::ResolveSymbol(const FString& Text, FAGEUsageSymbol& OutSymbol, FString& OutError)
{
	const FString Trimmed = Text.TrimStartAndEnd();
	FString ClassText = Trimmed;
	FString MemberText;
	FString Left, Right;
	if (Trimmed.Split(TEXT("::"), &Left, &Right))
	{
		ClassText = Left;
		MemberText = Right;
	}
	// Class.Member, but a /Script/Module.Class path keeps its dot.
	else if (!Trimmed.StartsWith(TEXT("/")) && Trimmed.Split(TEXT("."), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
	{
		ClassText = Left;
		MemberText = Right;
	}
	if (ClassText.IsEmpty())
	{
		OutError = TEXT("'symbol' is required, e.g. GameplayAbility::GetAvatarActorFromActorInfo, ACharacter.JumpMaxCount or UGameplayAbility.");
		return false;
	}

	UClass* Class = AGE::ResolveClass(ClassText);
	if (!Class)
	{
		OutError = FString::Printf(TEXT("No class named %s."), *ClassText);
		return false;
	}
	if (!Class->HasAnyClassFlags(CLASS_Native))
	{
		OutError = FString::Printf(TEXT("%s is a Blueprint class; age_find_usages looks for C++ classes and members. Use age_references for assets."), *Class->GetPathName());
		return false;
	}

	OutSymbol = FAGEUsageSymbol();
	OutSymbol.Class = Class;
	OutSymbol.DeclaringClass = Class;
	OutSymbol.Kind = TEXT("class");
	if (!MemberText.IsEmpty())
	{
		const FName Member(*MemberText);
		if (const UFunction* Function = Class->FindFunctionByName(Member))
		{
			OutSymbol.Member = Function->GetFName();
			OutSymbol.Kind = TEXT("function");
			OutSymbol.DeclaringClass = Function->GetOwnerClass();
		}
		else if (const FProperty* Property = FindFProperty<FProperty>(Class, Member))
		{
			OutSymbol.Member = Property->GetFName();
			OutSymbol.Kind = TEXT("property");
			OutSymbol.DeclaringClass = Property->GetOwnerClass();
		}
		else
		{
			OutError = FString::Printf(TEXT("%s has no UFUNCTION or UPROPERTY named %s (plain C++ functions cannot be used from Blueprints)."), *Class->GetName(), *MemberText);
			return false;
		}
	}
	OutSymbol.ModulePackage = OutSymbol.DeclaringClass->GetOutermost()->GetFName();
	return true;
}

TArray<FString> AGE::Usages::FindInExport(const FJsonObject& Export, const FAGEUsageSymbol& Symbol)
{
	using namespace AGE::UsagesDetail;
	FWalker Walker{ Symbol };
	Walker.ClassPath = Symbol.Class ? Symbol.Class->GetPathName() : FString();

	const TSharedPtr<FJsonObject>* Blueprint = nullptr;
	FString NativeParentPath;
	if (Export.TryGetObjectField(TEXT("blueprint"), Blueprint) && (*Blueprint)->TryGetStringField(TEXT("nativeParentClass"), NativeParentPath))
	{
		Walker.NativeParent = FindObject<UClass>(nullptr, *NativeParentPath);
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Export.Values)
	{
		bool bHeader = false;
		for (const TCHAR* Header : HeaderFields)
		{
			bHeader |= Field.Key == Header;
		}
		if (!bHeader)
		{
			Walker.Walk(Field.Value, Field.Key, FString(), FString());
		}
	}
	return Walker.Lines;
}
