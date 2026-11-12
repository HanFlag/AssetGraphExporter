// Asset Graph Exporter

#include "AGESummary.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Named (not anonymous) so these helpers cannot clash with other files in a unity build.
namespace AGE::SummaryDetail
{
	using FJsonArray = TArray<TSharedPtr<FJsonValue>>;

	/** Summaries never print more than this many characters of one inline settings object. */
	constexpr int32 MaxInlineSettingsLength = 160;
	/** Expressions deeper than this are cut with "..." (also guards against cycles). */
	constexpr int32 MaxExpressionDepth = 24;
	/** Exec nodes no entry reaches, named at the end of a graph's flow; the rest are only counted. */
	constexpr int32 MaxUnreachedNames = 5;

	const FJsonArray& EmptyArray()
	{
		static const FJsonArray Empty;
		return Empty;
	}

	FString StringField(const FJsonObject& Object, const TCHAR* Key)
	{
		FString Value;
		Object.TryGetStringField(Key, Value);
		return Value;
	}

	bool BoolField(const FJsonObject& Object, const TCHAR* Key, bool bDefault)
	{
		bool bValue = bDefault;
		Object.TryGetBoolField(Key, bValue);
		return bValue;
	}

	const FJsonArray& ArrayField(const FJsonObject& Object, const TCHAR* Key)
	{
		const FJsonArray* Array = nullptr;
		return Object.TryGetArrayField(Key, Array) && Array ? *Array : EmptyArray();
	}

	const FJsonObject* ObjectField(const FJsonObject& Object, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Child = nullptr;
		return Object.TryGetObjectField(Key, Child) && Child && Child->IsValid() ? Child->Get() : nullptr;
	}

	const FJsonObject* AsObject(const TSharedPtr<FJsonValue>& Value)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		return Value.IsValid() && Value->TryGetObject(Object) && Object && Object->IsValid() ? Object->Get() : nullptr;
	}

	FString Condensed(const TSharedPtr<FJsonValue>& Value)
	{
		FString Output;
		if (Value.IsValid())
		{
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
			FJsonSerializer::Serialize(Value, FString(), Writer);
		}
		return Output;
	}

	/** Extra settings written next to an item, e.g. a state's "properties". Long ones are only counted. */
	FString InlineSettings(const FJsonObject& Owner, const TCHAR* Key)
	{
		const TSharedPtr<FJsonValue> Value = Owner.TryGetField(Key);
		const FJsonObject* Object = AsObject(Value);
		if (!Object || Object->Values.Num() == 0)
		{
			return FString();
		}
		const FString Text = Condensed(Value);
		return Text.Len() <= MaxInlineSettingsLength ? TEXT(" ") + Text : FString::Printf(TEXT(" {%d settings}"), Object->Values.Num());
	}

	/** "/Script/Module.Type" -> "Type", "/Game/Folder/Asset.Asset_C" -> "Asset_C" */
	FString ShortName(const FString& Path)
	{
		int32 Index = INDEX_NONE;
		if (Path.FindLastChar(TEXT('.'), Index))
		{
			return Path.Mid(Index + 1);
		}
		if (Path.FindLastChar(TEXT('/'), Index))
		{
			return Path.Mid(Index + 1);
		}
		return Path;
	}

	/** Node titles can span several lines ("Look At\nBone: Head_M"). */
	FString OneLine(FString Text)
	{
		Text.ReplaceInline(TEXT("\r"), TEXT(""));
		Text.ReplaceInline(TEXT("\n"), TEXT(" "));
		return Text.TrimStartAndEnd();
	}

	/** "8.000000" -> "8", "0.250000" -> "0.25"; anything that is not a plain number is returned unchanged. */
	FString Literal(const FString& Value)
	{
		if (!Value.IsEmpty() && FCString::IsNumeric(*Value))
		{
			return FString::SanitizeFloat(FCString::Atod(*Value), 0);
		}
		return Value;
	}

	FString AssetPath(const FJsonObject& Export)
	{
		const FJsonObject* Asset = ObjectField(Export, TEXT("asset"));
		return Asset ? StringField(*Asset, TEXT("path")) : FString();
	}

	// ---- Pins and nodes of an exported graph ----

	const FJsonObject* PinType(const FJsonObject& Pin)
	{
		return ObjectField(Pin, TEXT("type"));
	}

	FString PinCategory(const FJsonObject& Pin)
	{
		const FJsonObject* Type = PinType(Pin);
		return Type ? StringField(*Type, TEXT("category")) : FString();
	}

	bool IsInput(const FJsonObject& Pin)
	{
		return StringField(Pin, TEXT("dir")) == TEXT("in");
	}

	bool IsExec(const FJsonObject& Pin)
	{
		return PinCategory(Pin) == TEXT("exec");
	}

	bool IsHidden(const FJsonObject& Pin)
	{
		return BoolField(Pin, TEXT("hidden"), false);
	}

	/** Pose pins carry FPoseLink / FComponentSpacePoseLink. */
	bool IsPosePin(const FJsonObject& Pin)
	{
		const FJsonObject* Type = PinType(Pin);
		return Type && StringField(*Type, TEXT("category")) == TEXT("struct") && StringField(*Type, TEXT("object")).Contains(TEXT("PoseLink"));
	}

	FString PinLabel(const FJsonObject& Pin)
	{
		const FString DisplayName = StringField(Pin, TEXT("displayName"));
		return DisplayName.IsEmpty() ? StringField(Pin, TEXT("name")) : DisplayName;
	}

	const FJsonObject* FirstLink(const FJsonObject& Pin)
	{
		const FJsonArray& Links = ArrayField(Pin, TEXT("links"));
		return Links.Num() > 0 ? AsObject(Links[0]) : nullptr;
	}

	TArray<const FJsonObject*> Pins(const FJsonObject& Node)
	{
		TArray<const FJsonObject*> Result;
		for (const TSharedPtr<FJsonValue>& Value : ArrayField(Node, TEXT("pins")))
		{
			if (const FJsonObject* Pin = AsObject(Value))
			{
				Result.Add(Pin);
			}
		}
		return Result;
	}

	const FJsonObject* FindPin(const FJsonObject& Node, const TCHAR* Name)
	{
		for (const FJsonObject* Pin : Pins(Node))
		{
			if (StringField(*Pin, TEXT("name")) == Name)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	const FJsonObject* FindPinById(const FJsonObject& Node, const FString& Id)
	{
		if (Id.IsEmpty())
		{
			return nullptr;
		}
		for (const FJsonObject* Pin : Pins(Node))
		{
			if (StringField(*Pin, TEXT("id")) == Id)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/** Visible exec output pins in pin order. */
	TArray<const FJsonObject*> ExecOutputs(const FJsonObject& Node)
	{
		TArray<const FJsonObject*> Result;
		for (const FJsonObject* Pin : Pins(Node))
		{
			if (IsExec(*Pin) && !IsInput(*Pin) && !IsHidden(*Pin))
			{
				Result.Add(Pin);
			}
		}
		return Result;
	}

	/** Nodes on the exec line (function calls with exec pins, Set, macros, Branch). Pure nodes have no exec pins. */
	bool IsImpure(const FJsonObject& Node)
	{
		for (const FJsonObject* Pin : Pins(Node))
		{
			if (IsExec(*Pin))
			{
				return true;
			}
		}
		return false;
	}

	/** Where execution starts: events, custom events, function and macro entries (exec outputs but no exec input). */
	bool IsEntry(const FJsonObject& Node)
	{
		for (const FJsonObject* Pin : Pins(Node))
		{
			if (IsExec(*Pin) && IsInput(*Pin))
			{
				return false;
			}
		}
		return ExecOutputs(Node).Num() > 0;
	}

	/** MemberName of the node's "VariableReference" or "FunctionReference" property. */
	FString MemberName(const FJsonObject& Node, const TCHAR* ReferenceKey)
	{
		const FJsonObject* Properties = ObjectField(Node, TEXT("properties"));
		const FJsonObject* Reference = Properties ? ObjectField(*Properties, ReferenceKey) : nullptr;
		return Reference ? StringField(*Reference, TEXT("MemberName")) : FString();
	}

	/** " (disabled)" for a disabled node, " (DevelopmentOnly)" for other non-default states. */
	FString StateSuffix(const FJsonObject& Node)
	{
		const FString State = StringField(Node, TEXT("enabledState"));
		if (State.IsEmpty())
		{
			return FString();
		}
		return State == TEXT("Disabled") ? FString(TEXT(" (disabled)")) : FString::Printf(TEXT(" (%s)"), *State);
	}

	FString TypeText(const FJsonObject* Type)
	{
		if (!Type)
		{
			return FString();
		}
		auto Terminal = [](const FString& Category, const FString& SubCategory, const FString& Object)
		{
			if (!Object.IsEmpty())
			{
				return ShortName(Object);
			}
			return Category == TEXT("real") && !SubCategory.IsEmpty() ? SubCategory : Category;
		};
		FString Text = Terminal(StringField(*Type, TEXT("category")), StringField(*Type, TEXT("subCategory")), StringField(*Type, TEXT("object")));
		const FString Container = StringField(*Type, TEXT("container"));
		if (Container == TEXT("array") || Container == TEXT("set"))
		{
			Text = FString::Printf(TEXT("%s<%s>"), *Container, *Text);
		}
		else if (Container == TEXT("map"))
		{
			const FJsonObject* ValueType = ObjectField(*Type, TEXT("valueType"));
			const FString ValueText = ValueType ? Terminal(StringField(*ValueType, TEXT("category")), StringField(*ValueType, TEXT("subCategory")), StringField(*ValueType, TEXT("object"))) : FString(TEXT("?"));
			Text = FString::Printf(TEXT("map<%s, %s>"), *Text, *ValueText);
		}
		return Text;
	}

	/** One exported graph with its nodes indexed by guid. */
	struct FGraphView
	{
		explicit FGraphView(const FJsonObject& InGraph)
			: Graph(InGraph)
		{
			for (const TSharedPtr<FJsonValue>& Value : ArrayField(Graph, TEXT("nodes")))
			{
				if (const FJsonObject* Node = AsObject(Value))
				{
					NodesByGuid.Add(StringField(*Node, TEXT("guid")), Node);
					Nodes.Add(Node);
				}
			}
		}

		const FJsonObject* Find(const FString& Guid) const
		{
			const FJsonObject* const* Found = NodesByGuid.Find(Guid);
			return Found ? *Found : nullptr;
		}

		const FJsonObject* FindByClass(const TCHAR* Class) const
		{
			for (const FJsonObject* Node : Nodes)
			{
				if (StringField(*Node, TEXT("class")) == Class)
				{
					return Node;
				}
			}
			return nullptr;
		}

		const FJsonObject& Graph;
		TArray<const FJsonObject*> Nodes;
		TMap<FString, const FJsonObject*> NodesByGuid;
	};

	/** Binary and unary operators from KismetMathLibrary functions and promotable operator nodes, keyed by the name before '_'. */
	const TCHAR* OperatorSymbol(const FString& Operation)
	{
		static const TMap<FString, const TCHAR*> Symbols =
		{
			{ TEXT("Less"), TEXT("<") }, { TEXT("Greater"), TEXT(">") },
			{ TEXT("LessEqual"), TEXT("<=") }, { TEXT("GreaterEqual"), TEXT(">=") },
			{ TEXT("EqualEqual"), TEXT("==") }, { TEXT("NotEqual"), TEXT("!=") },
			{ TEXT("Add"), TEXT("+") }, { TEXT("Subtract"), TEXT("-") },
			{ TEXT("Multiply"), TEXT("*") }, { TEXT("Divide"), TEXT("/") }, { TEXT("Percent"), TEXT("%") },
			{ TEXT("BooleanAND"), TEXT("AND") }, { TEXT("BooleanOR"), TEXT("OR") }, { TEXT("BooleanXOR"), TEXT("XOR") },
			{ TEXT("BooleanNAND"), TEXT("NAND") }, { TEXT("BooleanNOR"), TEXT("NOR") },
			{ TEXT("Not"), TEXT("NOT") },
		};
		const TCHAR* const* Found = Symbols.Find(Operation);
		return Found ? *Found : nullptr;
	}

	/** Writes pure node networks (transition rules, values feeding anim nodes, Set nodes) as one-line expressions. */
	class FExpressionWriter
	{
	public:
		explicit FExpressionWriter(const FGraphView& InView)
			: View(InView)
		{
		}

		/** The value reaching an input pin: the expression it is linked to, or its default. */
		FString InputValue(const FJsonObject& Pin, int32 Depth = 0) const
		{
			if (const FJsonObject* Link = FirstLink(Pin))
			{
				const FJsonObject* Source = View.Find(StringField(*Link, TEXT("node")));
				return Source ? NodeValue(*Source, StringField(*Link, TEXT("pinName")), Depth + 1) : FString(TEXT("?"));
			}
			return DefaultLiteral(Pin);
		}

		/** "Target." when the node's self pin is wired to something, otherwise empty. */
		FString Target(const FJsonObject& Node, int32 Depth = 0) const
		{
			const FJsonObject* SelfPin = FindPin(Node, TEXT("self"));
			return SelfPin && FirstLink(*SelfPin) ? InputValue(*SelfPin, Depth) + TEXT(".") : FString();
		}

		/** Values of the visible data inputs other than self, in pin order. */
		TArray<FString> Arguments(const FJsonObject& Node, int32 Depth = 0) const
		{
			TArray<FString> Result;
			for (const FJsonObject* Pin : Pins(Node))
			{
				if (IsInput(*Pin) && !IsExec(*Pin) && !IsHidden(*Pin) && StringField(*Pin, TEXT("name")) != TEXT("self"))
				{
					Result.Add(InputValue(*Pin, Depth));
				}
			}
			return Result;
		}

		/** The called function's name, or the node title for nodes that call none (macros, Sequence, ...). */
		static FString CallName(const FJsonObject& Node)
		{
			const FString Function = MemberName(Node, TEXT("FunctionReference"));
			return Function.IsEmpty() ? OneLine(StringField(Node, TEXT("title"))) : Function;
		}

	private:
		static FString DefaultLiteral(const FJsonObject& Pin)
		{
			const FString Value = StringField(Pin, TEXT("default"));
			const FString Category = PinCategory(Pin);
			if (Category == TEXT("string") || Category == TEXT("name") || Category == TEXT("text"))
			{
				return FString::Printf(TEXT("\"%s\""), *Value);
			}
			if (Category == TEXT("object") || Category == TEXT("class") || Category == TEXT("softobject") || Category == TEXT("softclass") || Category == TEXT("interface"))
			{
				return Value.IsEmpty() || Value == TEXT("None") ? FString(TEXT("None")) : ShortName(Value);
			}
			if (Category == TEXT("bool"))
			{
				return Value.IsEmpty() ? FString(TEXT("false")) : Value.ToLower();
			}
			return Value.IsEmpty() ? FString(TEXT("?")) : Literal(Value);
		}

		FString NodeValue(const FJsonObject& Node, const FString& OutputPin, int32 Depth) const
		{
			if (Depth > MaxExpressionDepth)
			{
				return TEXT("...");
			}
			// A split struct output ("ReturnValue_Yaw") is its parent pin's value plus the member: "....Yaw".
			if (const FJsonObject* Pin = FindPin(Node, *OutputPin))
			{
				if (const FJsonObject* Parent = FindPinById(Node, StringField(*Pin, TEXT("parentPin"))))
				{
					const FString ParentName = StringField(*Parent, TEXT("name"));
					FString Member = OutputPin;
					Member.RemoveFromStart(ParentName + TEXT("_"));
					return NodeValue(Node, ParentName, Depth + 1) + TEXT(".") + Member;
				}
			}

			const FString Class = StringField(Node, TEXT("class"));
			if (Class == TEXT("K2Node_Self"))
			{
				return TEXT("self");
			}
			if (Class == TEXT("K2Node_Knot"))
			{
				// Reroute node: the value passes through.
				for (const FJsonObject* Pin : Pins(Node))
				{
					if (IsInput(*Pin))
					{
						return InputValue(*Pin, Depth);
					}
				}
				return TEXT("?");
			}
			if (Class == TEXT("K2Node_VariableGet"))
			{
				const FString Name = MemberName(Node, TEXT("VariableReference"));
				return Target(Node, Depth) + (Name.IsEmpty() ? OutputPin : Name);
			}
			if (IsEntry(Node))
			{
				// Event and function parameters are used by name.
				return OutputPin;
			}
			if (IsImpure(Node))
			{
				// Result of a node on the exec line: the call is written on its own line, so it is only referred to here.
				return CallName(Node) + TEXT(".") + OutputPin;
			}

			const TArray<FString> Args = Arguments(Node, Depth);
			int32 DataOutputs = 0;
			for (const FJsonObject* Pin : Pins(Node))
			{
				if (!IsExec(*Pin) && !IsInput(*Pin))
				{
					++DataOutputs;
				}
			}

			FString Function;
			FString FunctionOwner;
			const FJsonObject* Properties = ObjectField(Node, TEXT("properties"));
			if (const FJsonObject* Reference = Properties ? ObjectField(*Properties, TEXT("FunctionReference")) : nullptr)
			{
				Function = StringField(*Reference, TEXT("MemberName"));
				FunctionOwner = StringField(*Reference, TEXT("MemberParent"));
			}
			FString Operation = Properties ? StringField(*Properties, TEXT("OperationName")) : FString();
			if (Operation.IsEmpty() && FunctionOwner.EndsWith(TEXT("KismetMathLibrary")))
			{
				Function.Split(TEXT("_"), &Operation, nullptr);
				if (Operation.IsEmpty())
				{
					Operation = Function;
				}
			}

			FString Text;
			if (const TCHAR* Symbol = OperatorSymbol(Operation))
			{
				if (Args.Num() == 1)
				{
					Text = FString::Printf(TEXT("%s %s"), Symbol, *Args[0]);
				}
				else
				{
					Text = TEXT("(") + FString::Join(Args, *FString::Printf(TEXT(" %s "), Symbol)) + TEXT(")");
				}
			}
			else
			{
				Text = FString::Printf(TEXT("%s%s(%s)"), *Target(Node, Depth), *CallName(Node), *FString::Join(Args, TEXT(", ")));
			}
			// Nodes with several outputs (break struct, out parameters) say which one is used.
			if (DataOutputs > 1 && OutputPin != TEXT("ReturnValue"))
			{
				Text += TEXT(".") + OutputPin;
			}
			return Text;
		}

		const FGraphView& View;
	};

	/**
	 * Writes a Blueprint graph (EventGraph, function, macro, collapsed graph) as its execution flow: from each entry node,
	 * one line per node along the exec wires. Set nodes read "Var = expression". Nodes with several exec outputs
	 * (Branch, Sequence, Is Valid, loops, Switch) list each connected output in [brackets], indented, with what runs from it.
	 * Joining a node already written gives a reference to it instead of repeating it (also ends exec loops).
	 */
	class FFlowWriter
	{
	public:
		explicit FFlowWriter(const FGraphView& InView)
			: View(InView)
			, Expressions(InView)
		{
		}

		/** Lines indented by two spaces. Empty when the graph has no entry node (disabled placeholder events do not count). */
		TArray<FString> Write()
		{
			for (const FJsonObject* Node : View.Nodes)
			{
				if (!IsEntry(*Node) || BoolField(*Node, TEXT("ghost"), false))
				{
					continue;
				}
				TArray<FString> Parameters;
				for (const FJsonObject* Pin : Pins(*Node))
				{
					if (!IsInput(*Pin) && !IsExec(*Pin) && !IsHidden(*Pin) && PinCategory(*Pin) != TEXT("delegate"))
					{
						Parameters.Add(StringField(*Pin, TEXT("name")));
					}
				}
				FString Head = OneLine(StringField(*Node, TEXT("title")));
				if (Parameters.Num() > 0)
				{
					Head += TEXT("(") + FString::Join(Parameters, TEXT(", ")) + TEXT(")");
				}
				Lines.Add(TEXT("  ") + Head + StateSuffix(*Node) + TEXT(":"));
				const int32 HeadIndex = Lines.Num() - 1;

				const TArray<const FJsonObject*> Outputs = ExecOutputs(*Node);
				if (Outputs.Num() == 1)
				{
					WriteChain(*Outputs[0], TEXT("    "), 0);
				}
				else
				{
					for (const FJsonObject* Pin : Outputs)
					{
						if (FirstLink(*Pin))
						{
							Lines.Add(TEXT("    [") + PinLabel(*Pin) + TEXT("]"));
							WriteChain(*Pin, TEXT("      "), 0);
						}
					}
				}
				if (Lines.Num() == HeadIndex + 1)
				{
					Lines[HeadIndex] += TEXT(" (nothing connected)");
				}
			}

			// Exec nodes no entry leads to never run: often leftovers worth knowing about.
			TArray<FString> Unreached;
			for (const FJsonObject* Node : View.Nodes)
			{
				if (IsImpure(*Node) && !IsEntry(*Node) && StringField(*Node, TEXT("class")) != TEXT("K2Node_Knot") && !Written.Contains(StringField(*Node, TEXT("guid"))))
				{
					Unreached.Add(OneLine(StringField(*Node, TEXT("title"))));
				}
			}
			if (Unreached.Num() > 0)
			{
				const int32 Shown = FMath::Min(Unreached.Num(), MaxUnreachedNames);
				const TArray<FString> Names(Unreached.GetData(), Shown);
				FString Line = FString::Printf(TEXT("  not reached from an entry (%d): %s"), Unreached.Num(), *FString::Join(Names, TEXT(", ")));
				if (Unreached.Num() > Shown)
				{
					Line += TEXT(" ...");
				}
				Lines.Add(Line);
			}
			return Lines;
		}

	private:
		FString Statement(const FJsonObject& Node) const
		{
			const FString Class = StringField(Node, TEXT("class"));
			FString Text;
			if (Class == TEXT("K2Node_VariableSet"))
			{
				const FString Name = MemberName(Node, TEXT("VariableReference"));
				TArray<const FJsonObject*> Values;
				for (const FJsonObject* Pin : Pins(Node))
				{
					if (IsInput(*Pin) && !IsExec(*Pin) && !IsHidden(*Pin) && StringField(*Pin, TEXT("name")) != TEXT("self"))
					{
						Values.Add(Pin);
					}
				}
				FString Value;
				if (Values.Num() == 1)
				{
					Value = Expressions.InputValue(*Values[0]);
				}
				else
				{
					// A split struct pin ("LookAtTarget_X", "_Y", "_Z"): (X=..., Y=..., Z=...)
					TArray<FString> Parts;
					for (const FJsonObject* Pin : Values)
					{
						FString Member = StringField(*Pin, TEXT("name"));
						Member.RemoveFromStart(Name + TEXT("_"));
						Parts.Add(Member + TEXT("=") + Expressions.InputValue(*Pin));
					}
					Value = TEXT("(") + FString::Join(Parts, TEXT(", ")) + TEXT(")");
				}
				Text = Expressions.Target(Node) + Name + TEXT(" = ") + Value;
			}
			else if (Class == TEXT("K2Node_IfThenElse"))
			{
				const FJsonObject* Condition = FindPin(Node, TEXT("Condition"));
				Text = TEXT("if ") + (Condition ? Expressions.InputValue(*Condition) : FString(TEXT("?")));
			}
			else if (Class == TEXT("K2Node_FunctionResult"))
			{
				TArray<FString> Parts;
				for (const FJsonObject* Pin : Pins(Node))
				{
					if (IsInput(*Pin) && !IsExec(*Pin) && !IsHidden(*Pin))
					{
						Parts.Add(PinLabel(*Pin) + TEXT("=") + Expressions.InputValue(*Pin));
					}
				}
				Text = Parts.IsEmpty() ? FString(TEXT("return")) : TEXT("return ") + FString::Join(Parts, TEXT(", "));
			}
			else
			{
				const TArray<FString> Args = Expressions.Arguments(Node);
				Text = Expressions.Target(Node) + FExpressionWriter::CallName(Node);
				// Function calls always get parentheses; flow nodes without inputs (Sequence) read better without.
				if (Args.Num() > 0 || !MemberName(Node, TEXT("FunctionReference")).IsEmpty())
				{
					Text += TEXT("(") + FString::Join(Args, TEXT(", ")) + TEXT(")");
				}
			}
			return Text + StateSuffix(Node);
		}

		/** Writes what runs from an exec output pin, one line per node, until the chain ends or splits. */
		void WriteChain(const FJsonObject& FromPin, const FString& Indent, int32 Depth)
		{
			TSet<FString> KnotsSeen;
			const FJsonObject* Link = FirstLink(FromPin);
			while (Link)
			{
				const FJsonObject* Node = View.Find(StringField(*Link, TEXT("node")));
				if (!Node)
				{
					Lines.Add(Indent + TEXT("?"));
					return;
				}
				const FString Guid = StringField(*Node, TEXT("guid"));
				const TArray<const FJsonObject*> Outputs = ExecOutputs(*Node);
				if (StringField(*Node, TEXT("class")) == TEXT("K2Node_Knot"))
				{
					// Reroute node: follow it (a loop made only of reroutes ends here).
					bool bAlreadySeen = false;
					KnotsSeen.Add(Guid, &bAlreadySeen);
					Link = !bAlreadySeen && Outputs.Num() > 0 ? FirstLink(*Outputs[0]) : nullptr;
					continue;
				}
				if (Depth > MaxExpressionDepth)
				{
					Lines.Add(Indent + TEXT("..."));
					return;
				}
				if (Written.Contains(Guid))
				{
					Lines.Add(Indent + FString::Printf(TEXT("-> continues at \"%s\" above"), *Statement(*Node)));
					return;
				}
				Written.Add(Guid);
				Lines.Add(Indent + Statement(*Node));
				if (Outputs.Num() == 1)
				{
					Link = FirstLink(*Outputs[0]);
					continue;
				}
				for (const FJsonObject* Pin : Outputs)
				{
					if (FirstLink(*Pin))
					{
						Lines.Add(Indent + TEXT("  [") + PinLabel(*Pin) + TEXT("]"));
						WriteChain(*Pin, Indent + TEXT("    "), Depth + 1);
					}
				}
				return;
			}
		}

		const FGraphView& View;
		FExpressionWriter Expressions;
		TArray<FString> Lines;
		/** Guids of the exec nodes written so far, across all entries of the graph. */
		TSet<FString> Written;
	};

	FString ValueText(const TSharedPtr<FJsonValue>& Value);

	/** "(0,-1,0)" for an object of one to four numbers (a vector, rotator or color), otherwise empty. */
	FString NumberTuple(const FJsonObject& Object)
	{
		if (Object.Values.Num() == 0 || Object.Values.Num() > 4)
		{
			return FString();
		}
		TArray<FString> Numbers;
		for (const auto& Pair : Object.Values)
		{
			if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Number)
			{
				return FString();
			}
			Numbers.Add(FString::SanitizeFloat(Pair.Value->AsNumber(), 0));
		}
		return TEXT("(") + FString::Join(Numbers, TEXT(",")) + TEXT(")");
	}

	/** Settings as "Path=value": nested structs as dotted paths, number groups as tuples. */
	void FlattenSettings(const FString& Prefix, const FJsonObject& Object, TArray<TPair<FString, FString>>& OutSettings)
	{
		for (const auto& Pair : Object.Values)
		{
			const FString Key(*Pair.Key);
			if (Key.StartsWith(TEXT("$")))
			{
				continue;
			}
			const FString Path = Prefix.IsEmpty() ? Key : Prefix + TEXT(".") + Key;
			if (const FJsonObject* Child = AsObject(Pair.Value))
			{
				const FString Tuple = NumberTuple(*Child);
				if (Tuple.IsEmpty())
				{
					FlattenSettings(Path, *Child, OutSettings);
				}
				else
				{
					OutSettings.Emplace(Path, Tuple);
				}
				continue;
			}
			OutSettings.Emplace(Path, ValueText(Pair.Value));
		}
	}

	/**
	 * " [LookAt_Axis.Axis=(0,-1,0), LookAtClamp=30]": the anim node's own settings that differ from the defaults ("properties.Node").
	 * Left out: values fed by a pin or a binding (Skipped, written as arguments already) and texts the title already shows
	 * (bone names, asset names). Long lists end with "+N more".
	 */
	FString NodeSettings(const FJsonObject& Node, const FString& Title, const TSet<FString>& Skipped)
	{
		const FJsonObject* Properties = ObjectField(Node, TEXT("properties"));
		const FJsonObject* Settings = Properties ? ObjectField(*Properties, TEXT("Node")) : nullptr;
		if (!Settings)
		{
			return FString();
		}
		TArray<TPair<FString, FString>> Flat;
		FlattenSettings(FString(), *Settings, Flat);

		TArray<FString> Parts;
		int32 Length = 0;
		int32 More = 0;
		for (const TPair<FString, FString>& Setting : Flat)
		{
			FString Root = Setting.Key;
			Setting.Key.Split(TEXT("."), &Root, nullptr);
			const bool bTextInTitle = !Setting.Value.IsEmpty() && !Setting.Value.StartsWith(TEXT("(")) && !FCString::IsNumeric(*Setting.Value)
				&& Setting.Value != TEXT("true") && Setting.Value != TEXT("false") && Title.Contains(Setting.Value);
			if (Skipped.Contains(Root) || bTextInTitle)
			{
				continue;
			}
			const FString Part = Setting.Key + TEXT("=") + Setting.Value;
			if (Parts.Num() > 0 && Length + Part.Len() > MaxInlineSettingsLength)
			{
				++More;
				continue;
			}
			Parts.Add(Part);
			Length += Part.Len() + 2;
		}
		if (Parts.IsEmpty())
		{
			return FString();
		}
		FString Text = TEXT(" [") + FString::Join(Parts, TEXT(", "));
		if (More > 0)
		{
			Text += FString::Printf(TEXT(", +%d more"), More);
		}
		return Text + TEXT("]");
	}

	/** Writes anim graph pose flow: "Slot 'DefaultSlot' <- Main State Machine". */
	class FPoseWriter
	{
	public:
		explicit FPoseWriter(const FGraphView& InView)
			: View(InView)
			, Expressions(InView)
		{
		}

		/** The poses flowing into Node's pose input pins (Node itself is not written). */
		FString Inputs(const FJsonObject& Node, int32 Depth = 0) const
		{
			TArray<TPair<FString, const FJsonObject*>> Sources;
			for (const FJsonObject* Pin : Pins(Node))
			{
				if (IsInput(*Pin) && IsPosePin(*Pin))
				{
					if (const FJsonObject* Link = FirstLink(*Pin))
					{
						if (const FJsonObject* Source = View.Find(StringField(*Link, TEXT("node"))))
						{
							Sources.Emplace(PinLabel(*Pin), Source);
						}
					}
				}
			}
			if (Sources.Num() == 1)
			{
				return Pose(*Sources[0].Value, Depth + 1);
			}
			TArray<FString> Parts;
			for (const TPair<FString, const FJsonObject*>& Source : Sources)
			{
				Parts.Add(FString::Printf(TEXT("%s: %s"), *Source.Key, *Pose(*Source.Value, Depth + 1)));
			}
			return Parts.IsEmpty() ? FString() : TEXT("{") + FString::Join(Parts, TEXT("; ")) + TEXT("}");
		}

	private:
		FString Pose(const FJsonObject& Node, int32 Depth) const
		{
			const FString Text = Label(Node);
			if (Depth > MaxExpressionDepth)
			{
				return Text + TEXT(" <- ...");
			}
			const FString Upstream = Inputs(Node, Depth);
			return Upstream.IsEmpty() ? Text : Text + TEXT(" <- ") + Upstream;
		}

		/** Node title plus the values that come from elsewhere (linked non-pose pins, property-access bindings), then its own settings. */
		FString Label(const FJsonObject& Node) const
		{
			TArray<FString> Arguments;
			TSet<FString> FedProperties;
			for (const FJsonObject* Pin : Pins(Node))
			{
				if (IsInput(*Pin) && !IsPosePin(*Pin) && !IsExec(*Pin) && FirstLink(*Pin))
				{
					Arguments.Add(FString::Printf(TEXT("%s=%s"), *PinLabel(*Pin), *Expressions.InputValue(*Pin)));
					FedProperties.Add(StringField(*Pin, TEXT("name")));
				}
			}
			for (const TSharedPtr<FJsonValue>& Value : ArrayField(Node, TEXT("animBindings")))
			{
				if (const FJsonObject* Binding = AsObject(Value))
				{
					if (BoolField(*Binding, TEXT("bound"), true))
					{
						FString Path = StringField(*Binding, TEXT("pathText"));
						if (Path.IsEmpty())
						{
							Path = StringField(*Binding, TEXT("path"));
						}
						Arguments.Add(FString::Printf(TEXT("%s=@%s"), *StringField(*Binding, TEXT("property")), *Path));
						FedProperties.Add(StringField(*Binding, TEXT("property")));
					}
				}
			}
			const FString Title = OneLine(StringField(Node, TEXT("title")));
			const FString Settings = NodeSettings(Node, Title, FedProperties);
			return (Arguments.IsEmpty() ? Title : FString::Printf(TEXT("%s(%s)"), *Title, *FString::Join(Arguments, TEXT(", ")))) + Settings;
		}

		const FGraphView& View;
		FExpressionWriter Expressions;
	};

	// ---- StateTree ----

	/** "Track Player.Distance to Player < 1,000.0 [StateTreeCompareFloatCondition]" */
	FString StateTreeNodeText(const FJsonObject& Node, bool bIsTask)
	{
		FString Text = StringField(Node, TEXT("description"));
		if (Text.IsEmpty())
		{
			Text = StringField(Node, TEXT("name"));
		}
		Text = OneLine(Text) + FString::Printf(TEXT(" [%s]"), *ShortName(StringField(Node, TEXT("type"))));
		if (const FJsonObject* NodeData = ObjectField(Node, TEXT("node")))
		{
			if (!BoolField(*NodeData, TEXT("bTaskEnabled"), true) || !BoolField(*NodeData, TEXT("bConditionEnabled"), true))
			{
				Text += TEXT(" (disabled)");
			}
			if (bIsTask && !BoolField(*NodeData, TEXT("bConsideredForCompletion"), true))
			{
				Text += TEXT(" (not for completion)");
			}
		}
		return Text;
	}

	/** Conditions joined with their operands; ExpressionIndent becomes parentheses: "A AND (B OR C)". */
	FString StateTreeConditions(const FJsonArray& Conditions)
	{
		FString Text;
		int32 PreviousIndent = 0;
		for (int32 Index = 0; Index < Conditions.Num(); ++Index)
		{
			const FJsonObject* Condition = AsObject(Conditions[Index]);
			if (!Condition)
			{
				continue;
			}
			int32 Indent = 0;
			Condition->TryGetNumberField(TEXT("indent"), Indent);
			Indent = FMath::Max(Indent, 0);
			if (Index > 0)
			{
				for (int32 Close = Indent; Close < PreviousIndent; ++Close)
				{
					Text += TEXT(")");
				}
				FString Operand = StringField(*Condition, TEXT("operand"));
				Text += FString::Printf(TEXT(" %s "), *(Operand.IsEmpty() ? FString(TEXT("AND")) : Operand.ToUpper()));
			}
			for (int32 Open = PreviousIndent; Open < Indent; ++Open)
			{
				Text += TEXT("(");
			}
			FString Description = StringField(*Condition, TEXT("description"));
			Text += OneLine(Description.IsEmpty() ? StringField(*Condition, TEXT("name")) : Description);
			PreviousIndent = Indent;
		}
		for (int32 Close = 0; Close < PreviousIndent; ++Close)
		{
			Text += TEXT(")");
		}
		return Text;
	}

	FString StateLinkText(const FJsonObject* Link)
	{
		if (!Link)
		{
			return TEXT("?");
		}
		const FString Type = StringField(*Link, TEXT("type"));
		const FString State = StringField(*Link, TEXT("state"));
		return Type == TEXT("GotoState") || Type.IsEmpty() ? (State.IsEmpty() ? FString(TEXT("?")) : State) : Type;
	}

	void WriteStateTreeTransition(const FJsonObject& Transition, const FString& Indent, TArray<FString>& Lines)
	{
		FString Line = FString::Printf(TEXT("%s-> %s  on %s"), *Indent, *StateLinkText(ObjectField(Transition, TEXT("target"))), *StringField(Transition, TEXT("trigger")));
		const FJsonArray& Conditions = ArrayField(Transition, TEXT("conditions"));
		if (Conditions.Num() > 0)
		{
			Line += TEXT(" if ") + StateTreeConditions(Conditions);
		}
		const FString Priority = StringField(Transition, TEXT("priority"));
		if (!Priority.IsEmpty() && Priority != TEXT("Normal"))
		{
			Line += TEXT(" priority=") + Priority;
		}
		if (const FJsonObject* Delay = ObjectField(Transition, TEXT("delay")))
		{
			double Duration = 0.0;
			double Variance = 0.0;
			Delay->TryGetNumberField(TEXT("duration"), Duration);
			Delay->TryGetNumberField(TEXT("randomVariance"), Variance);
			Line += FString::Printf(TEXT(" delay=%ss"), *FString::SanitizeFloat(Duration, 0));
			if (Variance != 0.0)
			{
				Line += FString::Printf(TEXT("+-%ss"), *FString::SanitizeFloat(Variance, 0));
			}
		}
		if (!BoolField(Transition, TEXT("enabled"), true))
		{
			Line += TEXT(" (disabled)");
		}
		Line += InlineSettings(Transition, TEXT("properties"));
		Lines.Add(Line);
	}

	void WriteStateTreeState(const FJsonObject& State, int32 Depth, TArray<FString>& Lines)
	{
		const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
		FString Head = Indent + StringField(State, TEXT("name"));
		const FString Type = StringField(State, TEXT("type"));
		if (Type == TEXT("Linked"))
		{
			Head += TEXT(" <Linked -> ") + StateLinkText(ObjectField(State, TEXT("linkedSubtree"))) + TEXT(">");
		}
		else if (Type == TEXT("LinkedAsset"))
		{
			Head += TEXT(" <LinkedAsset -> ") + ShortName(StringField(State, TEXT("linkedAsset"))) + TEXT(">");
		}
		else if (!Type.IsEmpty() && Type != TEXT("State"))
		{
			Head += TEXT(" <") + Type + TEXT(">");
		}
		if (!BoolField(State, TEXT("enabled"), true))
		{
			Head += TEXT(" (disabled)");
		}
		const FString Selection = StringField(State, TEXT("selectionBehavior"));
		if (!Selection.IsEmpty() && Selection != TEXT("TrySelectChildrenInOrder"))
		{
			Head += TEXT(" select=") + Selection;
		}
		const FString Completion = StringField(State, TEXT("tasksCompletion"));
		if (!Completion.IsEmpty() && Completion != TEXT("Any"))
		{
			Head += TEXT(" complete=") + Completion;
		}
		if (const FJsonObject* Parameters = ObjectField(State, TEXT("parameters")))
		{
			Head += InlineSettings(*Parameters, TEXT("values"));
		}
		Head += InlineSettings(State, TEXT("properties"));
		Lines.Add(Head);

		const FString Inner = Indent + TEXT("  ");
		const FJsonArray& EnterConditions = ArrayField(State, TEXT("enterConditions"));
		if (EnterConditions.Num() > 0)
		{
			Lines.Add(Inner + TEXT("if: ") + StateTreeConditions(EnterConditions));
		}
		for (const TSharedPtr<FJsonValue>& Value : ArrayField(State, TEXT("considerations")))
		{
			if (const FJsonObject* Consideration = AsObject(Value))
			{
				Lines.Add(Inner + TEXT("utility: ") + StateTreeNodeText(*Consideration, false));
			}
		}
		for (const TSharedPtr<FJsonValue>& Value : ArrayField(State, TEXT("tasks")))
		{
			if (const FJsonObject* Task = AsObject(Value))
			{
				Lines.Add(Inner + TEXT("task: ") + StateTreeNodeText(*Task, true));
			}
		}
		if (const FJsonObject* SingleTask = ObjectField(State, TEXT("singleTask")))
		{
			Lines.Add(Inner + TEXT("task: ") + StateTreeNodeText(*SingleTask, true));
		}
		for (const TSharedPtr<FJsonValue>& Value : ArrayField(State, TEXT("transitions")))
		{
			if (const FJsonObject* Transition = AsObject(Value))
			{
				WriteStateTreeTransition(*Transition, Inner, Lines);
			}
		}
		for (const TSharedPtr<FJsonValue>& Value : ArrayField(State, TEXT("children")))
		{
			if (const FJsonObject* Child = AsObject(Value))
			{
				WriteStateTreeState(*Child, Depth + 1, Lines);
			}
		}
	}

	FString JoinNodes(const FJsonArray& Nodes, bool bIsTask)
	{
		TArray<FString> Parts;
		for (const TSharedPtr<FJsonValue>& Value : Nodes)
		{
			if (const FJsonObject* Node = AsObject(Value))
			{
				Parts.Add(StateTreeNodeText(*Node, bIsTask));
			}
		}
		return FString::Join(Parts, TEXT("; "));
	}

	FString BindingEnd(const FJsonObject* End)
	{
		if (!End)
		{
			return TEXT("?");
		}
		FString Owner = StringField(*End, TEXT("owner"));
		if (Owner.IsEmpty())
		{
			Owner = StringField(*End, TEXT("id"));
		}
		const FString Path = StringField(*End, TEXT("path"));
		// An empty path binds the whole struct.
		return Path.IsEmpty() ? Owner : FString::Printf(TEXT("%s.%s"), *Owner, *Path);
	}

	// ---- AnimBlueprint ----

	void CollectGraphs(const FJsonObject& Graph, TArray<const FJsonObject*>& OutGraphs)
	{
		OutGraphs.Add(&Graph);
		for (const TSharedPtr<FJsonValue>& Value : ArrayField(Graph, TEXT("subGraphs")))
		{
			if (const FJsonObject* SubGraph = AsObject(Value))
			{
				CollectGraphs(*SubGraph, OutGraphs);
			}
		}
	}

	/** A value from the export as short text: numbers trimmed, object paths shortened, long structs and arrays only counted. */
	FString ValueText(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return TEXT("?");
		}
		switch (Value->Type)
		{
		case EJson::Null:
			return TEXT("None");
		case EJson::String:
		{
			const FString Text = Value->AsString();
			return Text.StartsWith(TEXT("/")) ? ShortName(Text) : Text;
		}
		case EJson::Number:
			return FString::SanitizeFloat(Value->AsNumber(), 0);
		case EJson::Boolean:
			return Value->AsBool() ? TEXT("true") : TEXT("false");
		case EJson::Array:
		{
			const FString Text = Condensed(Value);
			return Text.Len() <= MaxInlineSettingsLength ? Text : FString::Printf(TEXT("[%d items]"), Value->AsArray().Num());
		}
		case EJson::Object:
		{
			const FString Text = Condensed(Value);
			return Text.Len() <= MaxInlineSettingsLength ? Text : FString::Printf(TEXT("{%d settings}"), Value->AsObject()->Values.Num());
		}
		default:
			return TEXT("?");
		}
	}

	/** "variables: Name:type=default, ..." from a "blueprint" section. */
	void WriteVariables(const FJsonObject& Blueprint, TArray<FString>& Lines)
	{
		TArray<FString> Variables;
		for (const TSharedPtr<FJsonValue>& Value : ArrayField(Blueprint, TEXT("variables")))
		{
			if (const FJsonObject* Variable = AsObject(Value))
			{
				FString Text = FString::Printf(TEXT("%s:%s"), *StringField(*Variable, TEXT("name")), *TypeText(ObjectField(*Variable, TEXT("type"))));
				const TSharedPtr<FJsonValue> Default = Variable->TryGetField(TEXT("default"));
				if (Default.IsValid())
				{
					Text += TEXT("=") + ValueText(Default);
				}
				Variables.Add(Text);
			}
		}
		if (Variables.Num() > 0)
		{
			Lines.Add(TEXT("variables: ") + FString::Join(Variables, TEXT(", ")));
		}
	}

	void WriteGraphFlow(const FJsonObject& Graph, TArray<FString>& Lines)
	{
		const FGraphView View(Graph);
		FFlowWriter Flow(View);
		const TArray<FString> Body = Flow.Write();
		Lines.Add(FString::Printf(TEXT("%s (%s):%s"), *StringField(Graph, TEXT("name")), *StringField(Graph, TEXT("kind")), Body.IsEmpty() ? TEXT(" (empty)") : TEXT("")));
		Lines.Append(Body);
	}

	/**
	 * Execution flow of every EventGraph, function and macro graph, and of the collapsed graphs inside them.
	 * AnimGraphs are skipped (the AnimBP overview writes their pose flow); any other top-level graph is only listed.
	 */
	void WriteFlowGraphs(const FJsonObject& Blueprint, TArray<FString>& Lines)
	{
		TArray<FString> OtherGraphs;
		for (const TSharedPtr<FJsonValue>& Value : ArrayField(Blueprint, TEXT("graphs")))
		{
			const FJsonObject* Graph = AsObject(Value);
			if (!Graph || StringField(*Graph, TEXT("class")) == TEXT("AnimationGraph"))
			{
				continue;
			}
			const FString Kind = StringField(*Graph, TEXT("kind"));
			if (Kind == TEXT("Ubergraph") || Kind == TEXT("Function") || Kind == TEXT("Macro"))
			{
				TArray<const FJsonObject*> Graphs;
				CollectGraphs(*Graph, Graphs);
				for (const FJsonObject* FlowGraph : Graphs)
				{
					WriteGraphFlow(*FlowGraph, Lines);
				}
			}
			else
			{
				OtherGraphs.Add(FString::Printf(TEXT("%s (%s, %d nodes)"), *StringField(*Graph, TEXT("name")), *Kind, ArrayField(*Graph, TEXT("nodes")).Num()));
			}
		}
		if (OtherGraphs.Num() > 0)
		{
			Lines.Add(TEXT("other graphs (not summarized): ") + FString::Join(OtherGraphs, TEXT(", ")));
		}
	}

	/** Graphs created inside a node (state, transition rule, conduit) keyed by that node's guid. */
	using FGraphsByOwner = TMultiMap<FString, const FJsonObject*>;

	const FJsonObject* OwnedGraph(const FGraphsByOwner& GraphsByOwner, const FString& OwnerGuid, const TCHAR* GraphClass)
	{
		TArray<const FJsonObject*> Owned;
		GraphsByOwner.MultiFind(OwnerGuid, Owned, true);
		for (const FJsonObject* Graph : Owned)
		{
			if (StringField(*Graph, TEXT("class")) == GraphClass)
			{
				return Graph;
			}
		}
		return nullptr;
	}

	/** The rule of a transition or conduit: the expression feeding the result node's "can enter" pin. */
	FString RuleExpression(const FJsonObject* RuleGraph)
	{
		if (!RuleGraph)
		{
			return TEXT("?");
		}
		const FGraphView View(*RuleGraph);
		const FJsonObject* Result = View.FindByClass(TEXT("AnimGraphNode_TransitionResult"));
		const FJsonObject* Pin = Result ? FindPin(*Result, TEXT("bCanEnterTransition")) : nullptr;
		return Pin ? FExpressionWriter(View).InputValue(*Pin) : FString(TEXT("?"));
	}

	void WriteStateMachine(const FJsonObject& Graph, const FGraphsByOwner& GraphsByOwner, TArray<FString>& Lines)
	{
		const FGraphView View(Graph);
		Lines.Add(TEXT("StateMachine ") + StringField(Graph, TEXT("name")));

		TMap<FString, FString> StateNames;
		for (const FJsonObject* Node : View.Nodes)
		{
			const FString Class = StringField(*Node, TEXT("class"));
			if (Class == TEXT("AnimStateEntryNode"))
			{
				StateNames.Add(StringField(*Node, TEXT("guid")), TEXT("entry"));
			}
			else if (Class == TEXT("AnimStateNode") || Class == TEXT("AnimStateConduitNode") || Class == TEXT("AnimStateAliasNode"))
			{
				StateNames.Add(StringField(*Node, TEXT("guid")), OneLine(StringField(*Node, TEXT("title"))));
			}
		}
		auto NameOf = [&StateNames](const FJsonObject* Link)
		{
			const FString* Name = Link ? StateNames.Find(StringField(*Link, TEXT("node"))) : nullptr;
			return Name ? *Name : FString(TEXT("?"));
		};

		for (const FJsonObject* Node : View.Nodes)
		{
			const FString Class = StringField(*Node, TEXT("class"));
			const FString Guid = StringField(*Node, TEXT("guid"));
			const FString Name = OneLine(StringField(*Node, TEXT("title")));
			if (Class == TEXT("AnimStateEntryNode"))
			{
				for (const FJsonObject* Pin : Pins(*Node))
				{
					for (const TSharedPtr<FJsonValue>& Link : ArrayField(*Pin, TEXT("links")))
					{
						Lines.Add(TEXT("  entry -> ") + NameOf(AsObject(Link)));
					}
				}
			}
			else if (Class == TEXT("AnimStateNode"))
			{
				FString Body = TEXT("(no graph)");
				if (const FJsonObject* StateGraph = OwnedGraph(GraphsByOwner, Guid, TEXT("AnimationStateGraph")))
				{
					const FGraphView StateView(*StateGraph);
					const FJsonObject* Result = StateView.FindByClass(TEXT("AnimGraphNode_StateResult"));
					Body = Result ? FPoseWriter(StateView).Inputs(*Result) : FString();
					if (Body.IsEmpty())
					{
						Body = TEXT("(no pose)");
					}
				}
				Lines.Add(FString::Printf(TEXT("  state %s: %s"), *Name, *Body));
			}
			else if (Class == TEXT("AnimStateConduitNode"))
			{
				Lines.Add(FString::Printf(TEXT("  conduit %s if %s"), *Name, *RuleExpression(OwnedGraph(GraphsByOwner, Guid, TEXT("AnimationTransitionGraph")))));
			}
			else if (Class == TEXT("AnimStateAliasNode"))
			{
				const FJsonObject* Properties = ObjectField(*Node, TEXT("properties"));
				const bool bGlobal = Properties && BoolField(*Properties, TEXT("bGlobalAlias"), false);
				Lines.Add(FString::Printf(TEXT("  alias %s%s"), *Name, bGlobal ? TEXT(" (any state)") : TEXT("")));
			}
		}

		for (const FJsonObject* Node : View.Nodes)
		{
			if (StringField(*Node, TEXT("class")) != TEXT("AnimStateTransitionNode"))
			{
				continue;
			}
			FString From = TEXT("?");
			FString To = TEXT("?");
			for (const FJsonObject* Pin : Pins(*Node))
			{
				if (const FJsonObject* Link = FirstLink(*Pin))
				{
					(IsInput(*Pin) ? From : To) = NameOf(Link);
				}
			}

			const FJsonObject* Properties = ObjectField(*Node, TEXT("properties"));
			FString Rule;
			TArray<FString> Extras;
			if (Properties && BoolField(*Properties, TEXT("bAutomaticRuleBasedOnSequencePlayerInState"), false))
			{
				Rule = TEXT("auto (source animation ends)");
			}
			else
			{
				Rule = RuleExpression(OwnedGraph(GraphsByOwner, StringField(*Node, TEXT("guid")), TEXT("AnimationTransitionGraph")));
			}
			if (Properties)
			{
				double Number = 0.0;
				if (Properties->TryGetNumberField(TEXT("CrossfadeDuration"), Number))
				{
					Extras.Add(FString::Printf(TEXT("blend %ss"), *FString::SanitizeFloat(Number, 0)));
				}
				if (Properties->TryGetNumberField(TEXT("PriorityOrder"), Number))
				{
					Extras.Add(FString::Printf(TEXT("priority %s"), *FString::SanitizeFloat(Number, 0)));
				}
				for (const TCHAR* Key : { TEXT("LogicType"), TEXT("BlendMode") })
				{
					const FString Value = StringField(*Properties, Key);
					if (!Value.IsEmpty())
					{
						Extras.Add(FString::Printf(TEXT("%s=%s"), Key, *Value));
					}
				}
				if (BoolField(*Properties, TEXT("Bidirectional"), false))
				{
					Extras.Add(TEXT("two-way"));
				}
				// First, so a transition that is never taken does not read as a live one.
				if (BoolField(*Properties, TEXT("bDisabled"), false))
				{
					Extras.Insert(TEXT("disabled"), 0);
				}
			}
			FString Line = FString::Printf(TEXT("  %s -> %s if %s"), *From, *To, *Rule);
			if (Extras.Num() > 0)
			{
				Line += TEXT("  [") + FString::Join(Extras, TEXT(", ")) + TEXT("]");
			}
			Lines.Add(Line);
		}
	}

	// ---- Niagara ----

	/** Longest custom HLSL shown inline in a module line. */
	constexpr int32 MaxInlineHlslLength = 100;
	/** Names of inputs left at their default, shown at the end of a module line before "+N more". */
	constexpr int32 MaxInlineDefaults = 6;
	/** Renderers get one line each, so they may show more of their settings. */
	constexpr int32 MaxRendererSettingsLength = 300;

	/** "/Script/Niagara.NiagaraMeshRendererProperties" -> "Mesh" (Suffix "RendererProperties"), "...NiagaraDataInterfaceGrid3DCollection" -> "Grid3DCollection" */
	FString NiagaraClassLabel(const FString& ClassPath, const TCHAR* Suffix)
	{
		FString Name = ShortName(ClassPath);
		Name.RemoveFromStart(TEXT("NiagaraDataInterface"));
		Name.RemoveFromStart(TEXT("Niagara"));
		if (Suffix)
		{
			Name.RemoveFromEnd(Suffix);
		}
		return Name;
	}

	/** An owned object's settings without "$class"/"$name": inline when short, otherwise only counted. */
	FString ObjectSettings(const FJsonObject& Object, int32 MaxLength = MaxInlineSettingsLength)
	{
		const TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
		for (const auto& Pair : Object.Values)
		{
			const FString Key(*Pair.Key);
			if (!Key.StartsWith(TEXT("$")))
			{
				Settings->SetField(Key, Pair.Value);
			}
		}
		if (Settings->Values.Num() == 0)
		{
			return FString();
		}
		const FString Text = Condensed(MakeShared<FJsonValueObject>(Settings));
		return Text.Len() <= MaxLength ? Text : FString::Printf(TEXT("{%d settings}"), Settings->Values.Num());
	}

	/** Custom HLSL on one line, with runs of whitespace collapsed. */
	FString CollapseWhitespace(const FString& Text)
	{
		FString Result;
		Result.Reserve(Text.Len());
		bool bPendingSpace = false;
		for (const TCHAR Char : Text)
		{
			if (FChar::IsWhitespace(Char))
			{
				bPendingSpace = !Result.IsEmpty();
				continue;
			}
			if (bPendingSpace)
			{
				Result.AppendChar(TEXT(' '));
				bPendingSpace = false;
			}
			Result.AppendChar(Char);
		}
		return Result;
	}

	FString NiagaraModuleText(const FJsonObject& Module, bool bModule);

	/** "Name=value", where the value is a constant, a linked parameter, a dynamic input, a data interface or an object. */
	FString NiagaraInputText(const FJsonObject& Input)
	{
		FString Value;
		if (Input.TryGetStringField(TEXT("displayName"), Value))
		{
			// Enum values: the name the editor shows (e.g. "Self") rather than NewEnumerator1.
			Value = Literal(Value);
		}
		else if (Input.TryGetStringField(TEXT("value"), Value))
		{
			Value = Literal(Value);
		}
		else if (Input.TryGetStringField(TEXT("linked"), Value))
		{
		}
		else if (const FJsonObject* Dynamic = ObjectField(Input, TEXT("dynamic")))
		{
			Value = NiagaraModuleText(*Dynamic, false);
			const FString Output = StringField(*Dynamic, TEXT("output"));
			if (!Output.IsEmpty())
			{
				Value += TEXT(".") + Output;
			}
		}
		else if (const FJsonObject* DataInterface = ObjectField(Input, TEXT("dataInterface")))
		{
			Value = NiagaraClassLabel(StringField(*DataInterface, TEXT("$class")), nullptr) + ObjectSettings(*DataInterface);
		}
		else if (Input.TryGetStringField(TEXT("object"), Value))
		{
			Value = ShortName(Value);
		}
		else if (Input.TryGetStringField(TEXT("linkedNode"), Value))
		{
			Value = TEXT("<") + Value + TEXT(">");
		}
		else
		{
			Value = TEXT("?");
		}
		FString Text = StringField(Input, TEXT("name")) + TEXT("=") + Value;
		if (BoolField(Input, TEXT("inactive"), false))
		{
			Text += TEXT(" (inactive: hidden by a switch)");
		}
		return Text;
	}

	/**
	 * "Module(Switch=x, Input=value, ...)"; dynamic inputs use the same form inside the parentheses.
	 * Only modules (not dynamic inputs) list the inputs left at their default, to keep lines short.
	 */
	FString NiagaraModuleText(const FJsonObject& Module, bool bModule)
	{
		TArray<FString> Parts;
		const FString Kind = StringField(Module, TEXT("kind"));
		if (Kind == TEXT("customHlsl"))
		{
			FString Hlsl = CollapseWhitespace(StringField(Module, TEXT("hlsl")));
			if (Hlsl.Len() > MaxInlineHlslLength)
			{
				Hlsl = Hlsl.Left(MaxInlineHlslLength) + TEXT("...");
			}
			Parts.Add(TEXT("hlsl: ") + Hlsl);
		}
		for (const TCHAR* Key : { TEXT("switches"), TEXT("inputs") })
		{
			for (const TSharedPtr<FJsonValue>& Value : ArrayField(Module, Key))
			{
				if (const FJsonObject* Input = AsObject(Value))
				{
					Parts.Add(NiagaraInputText(*Input));
				}
			}
		}

		const FJsonArray& Defaults = ArrayField(Module, TEXT("defaults"));
		if (bModule && Defaults.Num() > 0)
		{
			TArray<FString> Names;
			for (int32 Index = 0; Index < Defaults.Num() && Index < MaxInlineDefaults; ++Index)
			{
				Names.Add(Defaults[Index]->AsString());
			}
			FString DefaultsText = TEXT("default: ") + FString::Join(Names, TEXT(", "));
			if (Defaults.Num() > MaxInlineDefaults)
			{
				DefaultsText += FString::Printf(TEXT(" +%d more"), Defaults.Num() - MaxInlineDefaults);
			}
			Parts.Add(TEXT("{") + DefaultsText + TEXT("}"));
		}

		FString Text = StringField(Module, TEXT("name"));
		if (Kind == TEXT("scratchPad"))
		{
			Text += TEXT(" [scratch pad]");
		}
		if (Parts.Num() > 0)
		{
			Text += TEXT("(") + FString::Join(Parts, TEXT(", ")) + TEXT(")");
		}
		if (!BoolField(Module, TEXT("enabled"), true))
		{
			Text = TEXT("(off) ") + Text;
		}
		return Text;
	}

	FString NiagaraStackTitle(const FJsonObject& Stack)
	{
		const FString Usage = StringField(Stack, TEXT("usage"));
		const FString Name = StringField(Stack, TEXT("name"));
		FString Title;
		if (Usage == TEXT("Event"))
		{
			Title = TEXT("event ") + Name;
			const FString Source = StringField(Stack, TEXT("sourceEmitter"));
			if (!Source.IsEmpty())
			{
				Title += TEXT(" from ") + Source;
			}
		}
		else if (Usage == TEXT("SimulationStage"))
		{
			Title = TEXT("stage ") + Name;
			TArray<FString> Details;
			const FString Source = StringField(Stack, TEXT("iterationSource"));
			if (!Source.IsEmpty())
			{
				const FString DataInterface = StringField(Stack, TEXT("dataInterface"));
				Details.Add(DataInterface.IsEmpty() ? Source : Source + TEXT(" ") + DataInterface);
			}
			const FString Iterations = StringField(Stack, TEXT("iterations"));
			if (!Iterations.IsEmpty() && Iterations != TEXT("1"))
			{
				Details.Add(TEXT("iterations=") + Iterations);
			}
			if (Details.Num() > 0)
			{
				Title += TEXT(" [") + FString::Join(Details, TEXT(", ")) + TEXT("]");
			}
		}
		else
		{
			// "ParticleUpdate" -> "particle update"
			for (int32 Index = 0; Index < Usage.Len(); ++Index)
			{
				if (Index > 0 && FChar::IsUpper(Usage[Index]))
				{
					Title.AppendChar(TEXT(' '));
				}
				Title.AppendChar(FChar::ToLower(Usage[Index]));
			}
		}
		if (!BoolField(Stack, TEXT("enabled"), true))
		{
			Title = TEXT("(off) ") + Title;
		}
		return Title;
	}

	/**
	 * Header line plus one line per module. Empty stacks are skipped unless bShowEmpty (simulation stages always show).
	 * bBrief: one line for the stack, module names only.
	 */
	void WriteNiagaraStack(const FJsonObject& Stack, const FString& Indent, bool bShowEmpty, TArray<FString>& Lines, bool bBrief = false)
	{
		const FJsonArray& Modules = ArrayField(Stack, TEXT("modules"));
		const FString Error = StringField(Stack, TEXT("error"));
		if (Modules.Num() == 0 && Error.IsEmpty() && !bShowEmpty)
		{
			return;
		}
		FString Title = Indent + NiagaraStackTitle(Stack) + TEXT(":");
		if (!Error.IsEmpty())
		{
			Title += TEXT(" (") + Error + TEXT(")");
		}
		else if (Modules.Num() == 0)
		{
			Title += TEXT(" (no modules)");
		}
		if (bBrief)
		{
			TArray<FString> Names;
			for (const TSharedPtr<FJsonValue>& Value : Modules)
			{
				if (const FJsonObject* Module = AsObject(Value))
				{
					Names.Add((BoolField(*Module, TEXT("enabled"), true) ? TEXT("") : TEXT("(off) ")) + StringField(*Module, TEXT("name")));
				}
			}
			Lines.Add(Names.IsEmpty() ? Title : Title + TEXT(" ") + FString::Join(Names, TEXT(", ")));
			return;
		}
		Lines.Add(Title);
		for (const TSharedPtr<FJsonValue>& Value : Modules)
		{
			if (const FJsonObject* Module = AsObject(Value))
			{
				Lines.Add(Indent + TEXT("  ") + NiagaraModuleText(*Module, true));
			}
		}
	}

	/** bBrief: the header, each stack on one line and the renderer kinds; no settings or module inputs. */
	void WriteNiagaraEmitter(const FJsonObject& Emitter, TArray<FString>& Lines, bool bBrief = false)
	{
		FString Header = TEXT("emitter ") + StringField(Emitter, TEXT("name"));
		for (const TCHAR* Key : { TEXT("mode"), TEXT("simTarget") })
		{
			const FString Value = StringField(Emitter, Key);
			if (!Value.IsEmpty())
			{
				Header += TEXT("  ") + Value;
			}
		}
		const FString Parent = StringField(Emitter, TEXT("parent"));
		if (!Parent.IsEmpty())
		{
			Header += TEXT("  parent=") + ShortName(Parent);
		}
		if (!BoolField(Emitter, TEXT("enabled"), true))
		{
			Header += TEXT("  (off)");
		}
		const FString Error = StringField(Emitter, TEXT("error"));
		if (!Error.IsEmpty())
		{
			Header += TEXT("  (") + Error + TEXT(")");
		}
		Lines.Add(Header);

		if (!bBrief)
		{
			if (const FJsonObject* Properties = ObjectField(Emitter, TEXT("properties")))
			{
				Lines.Add(TEXT("  properties: ") + ObjectSettings(*Properties));
			}
			const FString Settings = InlineSettings(Emitter, TEXT("settings"));
			if (!Settings.IsEmpty())
			{
				Lines.Add(TEXT("  settings:") + Settings);
			}
		}
		for (const TSharedPtr<FJsonValue>& Value : ArrayField(Emitter, TEXT("stacks")))
		{
			if (const FJsonObject* Stack = AsObject(Value))
			{
				WriteNiagaraStack(*Stack, TEXT("  "), StringField(*Stack, TEXT("usage")) == TEXT("SimulationStage"), Lines, bBrief);
			}
		}
		if (bBrief)
		{
			TArray<FString> Renderers;
			for (const TSharedPtr<FJsonValue>& Value : ArrayField(Emitter, TEXT("renderers")))
			{
				if (const FJsonObject* Renderer = AsObject(Value))
				{
					Renderers.Add(NiagaraClassLabel(StringField(*Renderer, TEXT("$class")), TEXT("RendererProperties")) + (BoolField(*Renderer, TEXT("bIsEnabled"), true) ? TEXT("") : TEXT(" (off)")));
				}
			}
			if (Renderers.Num() > 0)
			{
				Lines.Add(TEXT("  renderers: ") + FString::Join(Renderers, TEXT(", ")));
			}
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : ArrayField(Emitter, TEXT("renderers")))
		{
			if (const FJsonObject* Renderer = AsObject(Value))
			{
				FString Line = TEXT("  renderer ") + NiagaraClassLabel(StringField(*Renderer, TEXT("$class")), TEXT("RendererProperties"));
				if (!BoolField(*Renderer, TEXT("bIsEnabled"), true))
				{
					Line += TEXT(" (off)");
				}
				const FString RendererSettings = ObjectSettings(*Renderer, MaxRendererSettingsLength);
				if (!RendererSettings.IsEmpty())
				{
					Line += TEXT(": ") + RendererSettings;
				}
				Lines.Add(Line);
			}
		}
	}
}

FString AGE::Summary::StateTreeOverview(const FJsonObject& Export)
{
	using namespace AGE::SummaryDetail;
	const FJsonObject* Tree = ObjectField(Export, TEXT("stateTree"));
	if (!Tree)
	{
		return FString();
	}

	TArray<FString> Lines;
	FString Header = TEXT("StateTree ") + AssetPath(Export);
	if (const FJsonObject* Schema = ObjectField(*Tree, TEXT("schema")))
	{
		Header += TEXT("  schema=") + ShortName(StringField(*Schema, TEXT("$class")));
	}
	Lines.Add(Header);
	if (const FJsonObject* Parameters = ObjectField(*Tree, TEXT("parameters")))
	{
		const FString Values = InlineSettings(*Parameters, TEXT("values"));
		if (!Values.IsEmpty())
		{
			Lines.Add(TEXT("parameters:") + Values);
		}
	}
	const FJsonArray& Evaluators = ArrayField(*Tree, TEXT("evaluators"));
	if (Evaluators.Num() > 0)
	{
		Lines.Add(TEXT("evaluators: ") + JoinNodes(Evaluators, false));
	}
	const FJsonArray& GlobalTasks = ArrayField(*Tree, TEXT("globalTasks"));
	if (GlobalTasks.Num() > 0)
	{
		FString Line = TEXT("global tasks: ") + JoinNodes(GlobalTasks, true);
		const FString Completion = StringField(*Tree, TEXT("globalTasksCompletion"));
		if (!Completion.IsEmpty() && Completion != TEXT("Any"))
		{
			Line += TEXT(" complete=") + Completion;
		}
		Lines.Add(Line);
	}
	for (const TSharedPtr<FJsonValue>& Value : ArrayField(*Tree, TEXT("states")))
	{
		if (const FJsonObject* State = AsObject(Value))
		{
			WriteStateTreeState(*State, 0, Lines);
		}
	}
	Lines.Add(FString::Printf(TEXT("bindings: %d"), ArrayField(*Tree, TEXT("bindings")).Num()));
	return FString::Join(Lines, TEXT("\n"));
}

FString AGE::Summary::StateTreeBindings(const FJsonObject& Export, const FString& Filter)
{
	using namespace AGE::SummaryDetail;
	const FJsonObject* Tree = ObjectField(Export, TEXT("stateTree"));
	if (!Tree)
	{
		return FString();
	}

	const FJsonArray& Bindings = ArrayField(*Tree, TEXT("bindings"));
	TArray<FString> Lines;
	for (const TSharedPtr<FJsonValue>& Value : Bindings)
	{
		const FJsonObject* Binding = AsObject(Value);
		if (!Binding)
		{
			continue;
		}
		FString Line = FString::Printf(TEXT("%s -> %s"), *BindingEnd(ObjectField(*Binding, TEXT("source"))), *BindingEnd(ObjectField(*Binding, TEXT("target"))));
		if (const FJsonObject* Function = ObjectField(*Binding, TEXT("function")))
		{
			Line += FString::Printf(TEXT("  [function %s]"), *ShortName(StringField(*Function, TEXT("$type"))));
		}
		if (Filter.IsEmpty() || Line.Contains(Filter, ESearchCase::IgnoreCase))
		{
			Lines.Add(Line);
		}
	}
	FString Header = FString::Printf(TEXT("Bindings of %s: "), *AssetPath(Export));
	Header += Filter.IsEmpty()
		? FString::Printf(TEXT("%d"), Bindings.Num())
		: FString::Printf(TEXT("%d of %d match \"%s\""), Lines.Num(), Bindings.Num(), *Filter);
	Lines.Insert(Header, 0);
	return FString::Join(Lines, TEXT("\n"));
}

FString AGE::Summary::AnimBlueprintOverview(const FJsonObject& Export)
{
	using namespace AGE::SummaryDetail;
	const FJsonObject* Blueprint = ObjectField(Export, TEXT("blueprint"));
	if (!Blueprint)
	{
		return FString();
	}

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("AnimBlueprint %s  parent=%s"), *AssetPath(Export), *ShortName(StringField(*Blueprint, TEXT("parentClass")))));

	WriteVariables(*Blueprint, Lines);

	TArray<const FJsonObject*> AllGraphs;
	for (const TSharedPtr<FJsonValue>& Value : ArrayField(*Blueprint, TEXT("graphs")))
	{
		if (const FJsonObject* Graph = AsObject(Value))
		{
			CollectGraphs(*Graph, AllGraphs);
		}
	}
	FGraphsByOwner GraphsByOwner;
	for (const FJsonObject* Graph : AllGraphs)
	{
		const FString Owner = StringField(*Graph, TEXT("ownerNode"));
		if (!Owner.IsEmpty())
		{
			GraphsByOwner.Add(Owner, Graph);
		}
	}

	for (const FJsonObject* Graph : AllGraphs)
	{
		if (StringField(*Graph, TEXT("class")) == TEXT("AnimationGraph"))
		{
			const FGraphView View(*Graph);
			const FJsonObject* Root = View.FindByClass(TEXT("AnimGraphNode_Root"));
			const FString Flow = Root ? FPoseWriter(View).Inputs(*Root) : FString();
			Lines.Add(FString::Printf(TEXT("AnimGraph %s: %s"), *StringField(*Graph, TEXT("name")), Flow.IsEmpty() ? TEXT("(no pose)") : *Flow));
		}
	}
	for (const FJsonObject* Graph : AllGraphs)
	{
		if (StringField(*Graph, TEXT("class")) == TEXT("AnimationStateMachineGraph"))
		{
			WriteStateMachine(*Graph, GraphsByOwner, Lines);
		}
	}

	WriteFlowGraphs(*Blueprint, Lines);
	return FString::Join(Lines, TEXT("\n"));
}

FString AGE::Summary::BlueprintOverview(const FJsonObject& Export)
{
	using namespace AGE::SummaryDetail;
	const FJsonObject* Blueprint = ObjectField(Export, TEXT("blueprint"));
	if (!Blueprint)
	{
		return FString();
	}

	TArray<FString> Lines;
	const FString Parent = ShortName(StringField(*Blueprint, TEXT("parentClass")));
	FString Header = FString::Printf(TEXT("Blueprint %s  parent=%s"), *AssetPath(Export), *Parent);
	const FString NativeParent = ShortName(StringField(*Blueprint, TEXT("nativeParentClass")));
	if (!NativeParent.IsEmpty() && NativeParent != Parent)
	{
		Header += TEXT("  native=") + NativeParent;
	}
	const FString Type = StringField(*Blueprint, TEXT("type"));
	if (!Type.IsEmpty() && Type != TEXT("BPTYPE_Normal"))
	{
		Header += TEXT("  type=") + Type;
	}
	Lines.Add(Header);

	TArray<FString> Interfaces;
	for (const TSharedPtr<FJsonValue>& Value : ArrayField(*Blueprint, TEXT("interfaces")))
	{
		Interfaces.Add(ShortName(Value->AsString()));
	}
	if (Interfaces.Num() > 0)
	{
		Lines.Add(TEXT("interfaces: ") + FString::Join(Interfaces, TEXT(", ")));
	}

	WriteVariables(*Blueprint, Lines);

	// "Mesh:StaticMeshComponent <- DefaultSceneRoot @hand_r {settings}"
	TArray<FString> Components;
	for (const TSharedPtr<FJsonValue>& Value : ArrayField(*Blueprint, TEXT("components")))
	{
		if (const FJsonObject* Component = AsObject(Value))
		{
			FString Text = FString::Printf(TEXT("%s:%s"), *StringField(*Component, TEXT("name")), *ShortName(StringField(*Component, TEXT("class"))));
			const FString ComponentParent = StringField(*Component, TEXT("parent"));
			if (!ComponentParent.IsEmpty())
			{
				Text += TEXT(" <- ") + ComponentParent;
			}
			const FString Socket = StringField(*Component, TEXT("attachSocket"));
			if (!Socket.IsEmpty())
			{
				Text += TEXT(" @") + Socket;
			}
			Components.Add(Text + InlineSettings(*Component, TEXT("properties")));
		}
	}
	if (Components.Num() > 0)
	{
		Lines.Add(TEXT("components: ") + FString::Join(Components, TEXT(", ")));
	}

	// Class Defaults values other than this Blueprint's own variables (those are shown with the variables).
	if (const FJsonObject* ClassDefaults = ObjectField(*Blueprint, TEXT("classDefaults")))
	{
		TSet<FString> VariableNames;
		for (const TSharedPtr<FJsonValue>& Value : ArrayField(*Blueprint, TEXT("variables")))
		{
			if (const FJsonObject* Variable = AsObject(Value))
			{
				VariableNames.Add(StringField(*Variable, TEXT("name")));
			}
		}
		TArray<FString> Defaults;
		for (const auto& Pair : ClassDefaults->Values)
		{
			const FString Key(*Pair.Key);
			if (!VariableNames.Contains(Key))
			{
				Defaults.Add(Key + TEXT("=") + ValueText(Pair.Value));
			}
		}
		if (Defaults.Num() > 0)
		{
			Lines.Add(TEXT("class defaults: ") + FString::Join(Defaults, TEXT(", ")));
		}
	}

	WriteFlowGraphs(*Blueprint, Lines);
	return FString::Join(Lines, TEXT("\n"));
}

FString AGE::Summary::NiagaraOverview(const FJsonObject& Export, const FString& EmitterName, bool bBrief)
{
	using namespace AGE::SummaryDetail;
	const FJsonObject* Niagara = ObjectField(Export, TEXT("niagara"));
	if (!Niagara)
	{
		return FString();
	}

	TArray<FString> Lines;
	Lines.Add(TEXT("NiagaraSystem ") + AssetPath(Export));
	const bool bOneEmitter = !EmitterName.IsEmpty();
	const FString Settings = InlineSettings(Export, TEXT("properties"));
	if (!Settings.IsEmpty() && !bBrief && !bOneEmitter)
	{
		Lines.Add(TEXT("settings:") + Settings);
	}

	TArray<FString> UserParameters;
	for (const TSharedPtr<FJsonValue>& Value : ArrayField(*Niagara, TEXT("userParameters")))
	{
		if (const FJsonObject* Parameter = AsObject(Value))
		{
			FString Name = StringField(*Parameter, TEXT("name"));
			Name.RemoveFromStart(TEXT("User."));
			FString Text = Name + TEXT(":") + StringField(*Parameter, TEXT("type"));
			FString ParameterValue;
			if (Parameter->TryGetStringField(TEXT("displayName"), ParameterValue) || Parameter->TryGetStringField(TEXT("value"), ParameterValue))
			{
				Text += TEXT("=") + Literal(ParameterValue);
			}
			else if (Parameter->TryGetStringField(TEXT("object"), ParameterValue))
			{
				Text += TEXT("=") + ShortName(ParameterValue);
			}
			else if (const FJsonObject* DataInterface = ObjectField(*Parameter, TEXT("dataInterface")))
			{
				Text += ObjectSettings(*DataInterface);
			}
			UserParameters.Add(Text);
		}
	}
	if (UserParameters.Num() > 0)
	{
		Lines.Add(TEXT("user: ") + FString::Join(UserParameters, TEXT(", ")));
	}

	if (bOneEmitter)
	{
		TArray<FString> Names;
		for (const TSharedPtr<FJsonValue>& Value : ArrayField(*Niagara, TEXT("emitters")))
		{
			if (const FJsonObject* Emitter = AsObject(Value))
			{
				Names.Add(StringField(*Emitter, TEXT("name")));
				if (Names.Last() == EmitterName)
				{
					WriteNiagaraEmitter(*Emitter, Lines);
					return FString::Join(Lines, TEXT("\n"));
				}
			}
		}
		Lines.Add(FString::Printf(TEXT("no emitter named '%s' (names are exact). Emitters: %s"), *EmitterName, *FString::Join(Names, TEXT(", "))));
		return FString::Join(Lines, TEXT("\n"));
	}

	if (const FJsonObject* SystemJson = ObjectField(*Niagara, TEXT("system")))
	{
		for (const TSharedPtr<FJsonValue>& Value : ArrayField(*SystemJson, TEXT("stacks")))
		{
			if (const FJsonObject* Stack = AsObject(Value))
			{
				WriteNiagaraStack(*Stack, FString(), false, Lines, bBrief);
			}
		}
	}
	for (const TSharedPtr<FJsonValue>& Value : ArrayField(*Niagara, TEXT("emitters")))
	{
		if (const FJsonObject* Emitter = AsObject(Value))
		{
			WriteNiagaraEmitter(*Emitter, Lines, bBrief);
		}
	}
	if (bBrief)
	{
		Lines.Add(TEXT("This is the outline: module names only. For settings and the inputs of every module call again with emitter set to the name of one emitter, or full=true for the whole system."));
		return FString::Join(Lines, TEXT("\n"));
	}

	TArray<FString> Graphs;
	for (const TSharedPtr<FJsonValue>& Value : ArrayField(*Niagara, TEXT("graphs")))
	{
		if (const FJsonObject* Graph = AsObject(Value))
		{
			Graphs.Add(FString::Printf(TEXT("%s (%s, %d nodes)"), *StringField(*Graph, TEXT("name")), *StringField(*Graph, TEXT("kind")), ArrayField(*Graph, TEXT("nodes")).Num()));
		}
	}
	if (Graphs.Num() > 0)
	{
		Lines.Add(TEXT("graphs (age_get_asset with select niagara.graphs[Name]): ") + FString::Join(Graphs, TEXT(", ")));
	}
	return FString::Join(Lines, TEXT("\n"));
}
