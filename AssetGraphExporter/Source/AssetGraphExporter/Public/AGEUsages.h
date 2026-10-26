// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UClass;

/** A C++ class, or one of its UFUNCTIONs or UPROPERTYs, to look for in exported assets. */
struct ASSETGRAPHEXPORTER_API FAGEUsageSymbol
{
	/** The class named in the query. */
	const UClass* Class = nullptr;
	/** The class that declares the member: the named class or one of its parents. The named class for a class query. */
	const UClass* DeclaringClass = nullptr;
	/** None for a class query. */
	FName Member;
	/** function, property or class. */
	FString Kind;
	/** Module package of the declaring class, e.g. /Script/GameplayAbilities. Only assets that depend on it can use the symbol. */
	FName ModulePackage;

	/** e.g. "GameplayAbility.GetAvatarActorFromActorInfo  [function, declared in /Script/GameplayAbilities.GameplayAbility]" */
	FString Describe() const;
};

namespace AGE::Usages
{
	/** Class::Member, Class.Member, Class, or /Script/Module.Class with an optional ::Member. U/A prefixes are accepted. C++ classes only. */
	ASSETGRAPHEXPORTER_API bool ResolveSymbol(const FString& Text, FAGEUsageSymbol& OutSymbol, FString& OutError);

	/**
	 * Uses of the symbol in one exported asset, one line each.
	 * Members: Blueprint graph nodes whose function, variable, event or delegate reference names the member on the declaring class or a
	 * subclass (calls on self use the Blueprint's native parent class). Classes: every string in the JSON that names the class path.
	 */
	ASSETGRAPHEXPORTER_API TArray<FString> FindInExport(const FJsonObject& Export, const FAGEUsageSymbol& Symbol);
}
