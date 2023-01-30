#include "Toolkit/AssetGeneration/AssetGenerationUtil.h"
#include "Dom/JsonObject.h"
#include "Engine/MemberReference.h"
#include "Toolkit/ObjectHierarchySerializer.h"
#include "EdGraphSchema_K2.h"
#include "CoreUObject.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

void GetPropertyCategoryInfo(const TSharedPtr<FJsonObject> PropertyObject, FName& OutCategory, FName& OutSubCategory, UObject*& OutSubCategoryObject, bool& bOutIsWeakPointer, class UObjectHierarchySerializer* ObjectSerializer);

bool HasAllPropertyFlags(EPropertyFlags PropertyFlags, EPropertyFlags FlagsToCheck) {
	return (PropertyFlags & FlagsToCheck) == FlagsToCheck;
}

FString GetPropertyFriendlyName(const FString& PropertyName) {
	int32 UnderscoreIndex = INDEX_NONE;
	int32 UnderscoreNum = 0;

	for (int32 i = PropertyName.Len() - 1; i >= 0; i--) {
		if (PropertyName[i] == '_') {
			UnderscoreIndex = i;
			UnderscoreNum++;
			if (UnderscoreIndex == 2)
				break;
		}
	}

	check(UnderscoreIndex != INDEX_NONE);
	return PropertyName.Mid(0, UnderscoreIndex);
}

bool FAssetGenerationUtil::PopulateStructVariable(const TSharedPtr<FJsonObject>& PropertyObject, UObjectHierarchySerializer* ObjectSerializer, FStructVariableDescription& OutStructVariable) {
	FEdGraphPinType VariablePinType{};
	ConvertPropertyObjectToGraphPinType(PropertyObject, VariablePinType, ObjectSerializer);

	//Setup basic information from splitting variable name
	const FString FullVariableName = PropertyObject->GetStringField(TEXT("ObjectName"));
	OutStructVariable.VarName = FName(*FullVariableName);
	OutStructVariable.VarGuid = FStructureEditorUtils::GetGuidFromPropertyName(*FullVariableName);
	OutStructVariable.FriendlyName = GetPropertyFriendlyName(FullVariableName);

	//Carry type information over to the struct variable description
	OutStructVariable.Category = VariablePinType.PinCategory;
	OutStructVariable.SubCategory = VariablePinType.PinSubCategory;
	OutStructVariable.SubCategoryObject = VariablePinType.PinSubCategoryObject.Get();
	OutStructVariable.PinValueType = VariablePinType.PinValueType;
	OutStructVariable.ContainerType = VariablePinType.ContainerType;

	//Set flags that can be deduced from compiled property flags
	const EPropertyFlags PropertyFlags = (EPropertyFlags)FCString::Atoi64(*PropertyObject->GetStringField(TEXT("PropertyFlags")));
	OutStructVariable.bEnableSaveGame = HasAllPropertyFlags(PropertyFlags, CPF_SaveGame);
	OutStructVariable.bDontEditOnInstance = HasAllPropertyFlags(PropertyFlags, CPF_DisableEditOnInstance);

	return true;
}

static TMap<FString, UClass*> GetUPropertyMappings() {
	TMap<FString, UClass*> Mappings;

	TArray<UClass*> DerivedClasses;
	GetDerivedClasses(UProperty::StaticClass(), DerivedClasses);
	for (auto Derived : DerivedClasses) {
		Mappings.Add(Derived->GetName(), Derived);
	}

	return Mappings;
}

void FAssetGenerationUtil::GetPropertyDependencies(const TSharedPtr<FJsonObject> PropertyObject, UObjectHierarchySerializer* ObjectSerializer, TArray<FString>& OutDependencies) {
	static TMap<FString, UClass*> Mappings = GetUPropertyMappings();
	const FString ObjectClass = PropertyObject->GetStringField(TEXT("ObjectClass"));

	UClass* FieldClass = Mappings.FindChecked(ObjectClass);
	if (ObjectClass == "MapProperty") {

		const int32 ValueProperty = PropertyObject->GetIntegerField(TEXT("ValueProp"));
		const int32 KeyProperty = PropertyObject->GetIntegerField(TEXT("KeyProp"));

		ObjectSerializer->CollectObjectPackages(ValueProperty, OutDependencies);
		ObjectSerializer->CollectObjectPackages(KeyProperty, OutDependencies);

	}
	else if (ObjectClass == "SetProperty") {

		const int32 ElementType = PropertyObject->GetIntegerField(TEXT("ElementType"));
		ObjectSerializer->CollectObjectPackages(ElementType, OutDependencies);

	}
	else if (FieldClass->IsChildOf(UArrayProperty::StaticClass())) {

		const int32 InnerProperty = PropertyObject->GetIntegerField(TEXT("Inner"));
		ObjectSerializer->CollectObjectPackages(InnerProperty, OutDependencies);

	}
	else {
		if (FieldClass->IsChildOf(UInterfaceProperty::StaticClass())) {
			const int32 InterfaceClassIndex = PropertyObject->GetIntegerField(TEXT("InterfaceClass"));
			ObjectSerializer->CollectObjectPackages(InterfaceClassIndex, OutDependencies);

		}
		else if (FieldClass->IsChildOf(UClassProperty::StaticClass()) || FieldClass->IsChildOf(USoftClassProperty::StaticClass())) {
			const int32 MetaClassIndex = PropertyObject->GetIntegerField(TEXT("MetaClass"));
			ObjectSerializer->CollectObjectPackages(MetaClassIndex, OutDependencies);

		}
		else if (FieldClass->IsChildOf(UStructProperty::StaticClass())) {
			const int32 StructObjectIndex = PropertyObject->GetIntegerField(TEXT("Struct"));
			ObjectSerializer->CollectObjectPackages(StructObjectIndex, OutDependencies);

		}
		else if (FieldClass->IsChildOf(UByteProperty::StaticClass()) || FieldClass->IsChildOf(UEnumProperty::StaticClass())) {
			const int32 EnumObjectIndex = PropertyObject->GetIntegerField(TEXT("Enum"));
			ObjectSerializer->CollectObjectPackages(EnumObjectIndex, OutDependencies);

		}
		else if (FieldClass->IsChildOf(UObjectPropertyBase::StaticClass())) {

			const int32 ObjectClassIndex = PropertyObject->GetIntegerField(TEXT("PropertyClass"));
			ObjectSerializer->CollectObjectPackages(ObjectClassIndex, OutDependencies);
		}
	}

}

FDeserializedProperty::FDeserializedProperty(const TSharedPtr<FJsonObject>& Object, UObjectHierarchySerializer* ObjectSerializer) {
	check(Object->GetStringField(TEXT("FieldKind")) == TEXT("Property"));

	this->PropertyName = FName(*Object->GetStringField(TEXT("ObjectName")));
	this->PropertyFlags = (EPropertyFlags)FCString::Atoi64(*Object->GetStringField(TEXT("PropertyFlags")));
	this->ArrayDim = Object->GetIntegerField(TEXT("ArrayDim"));

	this->RepNotifyFunc = FName(*Object->GetStringField(TEXT("RepNotifyFunc")));
	this->BlueprintReplicationCondition = (ELifetimeCondition)Object->GetIntegerField(TEXT("BlueprintReplicationCondition"));
	FAssetGenerationUtil::ConvertPropertyObjectToGraphPinType(Object, this->GraphPinType, ObjectSerializer);
}

FDeserializedFunction::FDeserializedFunction(const TSharedPtr<FJsonObject>& Object, UObjectHierarchySerializer* ObjectSerializer, bool bDeserializeOnlySignatureProperties) {
	check(Object->GetStringField(TEXT("FieldKind")) == TEXT("Function"));

	const FString FunctionNameString = Object->GetStringField(TEXT("ObjectName"));
	this->FunctionName = FName(*FunctionNameString);
	this->FunctionFlags = (EFunctionFlags)FCString::Atoi64(*Object->GetStringField(TEXT("FunctionFlags")));

	const TArray<TSharedPtr<FJsonValue>> FunctionProperties = Object->GetArrayField(TEXT("ChildProperties"));

	for (int32 j = 0; j < FunctionProperties.Num(); j++) {
		const TSharedPtr<FJsonObject> FunctionProperty = FunctionProperties[j]->AsObject();
		if (bDeserializeOnlySignatureProperties && !FAssetGenerationUtil::IsFunctionSignatureRelevantProperty(FunctionProperty)) {
			continue;
		}

		FDeserializedProperty DeserializedProperty(FunctionProperty, ObjectSerializer);

		if (DeserializedProperty.HasAnyPropertyFlags(CPF_ReturnParm)) {
			this->ReturnValue = DeserializedProperty;
		}
		if (DeserializedProperty.HasAnyPropertyFlags(CPF_Parm)) {
			this->Parameters.Add(DeserializedProperty);
		}
		this->AllProperties.Add(MoveTemp(DeserializedProperty));
	}

	const TArray<TSharedPtr<FJsonValue>> Script = Object->GetArrayField(TEXT("Script"));
	const FString UbergraphFunctionName = UEdGraphSchema_K2::FN_ExecuteUbergraphBase.ToString();
	this->bIsCallingIntoUbergraph = false;

	for (int32 i = 0; i < Script.Num(); i++) {
		const TSharedPtr<FJsonObject> StatementObject = Script[i]->AsObject();
		const FString InstName = StatementObject->GetStringField(TEXT("Inst"));

		if (InstName == TEXT("LocalFinalFunction")) {
			const FString FunctionName = StatementObject->GetStringField(TEXT("Function"));

			if (FunctionName.StartsWith(UbergraphFunctionName)) {
				this->bIsCallingIntoUbergraph = true;
			}
		}
	}

	this->bIsUberGraphFunction = FunctionNameString.StartsWith(UEdGraphSchema_K2::FN_ExecuteUbergraphBase.ToString());
	this->bIsDelegateSignatureFunction = FunctionNameString.EndsWith(HEADER_GENERATED_DELEGATE_SIGNATURE_SUFFIX);
}

void FAssetGenerationUtil::ConvertPropertyObjectToGraphPinType(const TSharedPtr<FJsonObject> PropertyObject, FEdGraphPinType& OutPinType, class UObjectHierarchySerializer* ObjectSerializer) {
	static TMap<FString, UClass*> Mappings = GetUPropertyMappings();

	OutPinType.PinSubCategory = NAME_None;
	const FString ObjectClass = PropertyObject->GetStringField(TEXT("ObjectClass"));
	const FString ObjectName = PropertyObject->GetStringField(TEXT("ObjectName"));
	const EPropertyFlags PropertyFlags = (EPropertyFlags)FCString::Atoi64(*PropertyObject->GetStringField(TEXT("PropertyFlags")));

	UClass* FieldClass = Mappings.FindChecked(ObjectClass);
	TSharedPtr<FJsonObject> ResultProperty = PropertyObject;

	OutPinType.ContainerType = EPinContainerType::None;

	if (FieldClass->IsChildOf(UMapProperty::StaticClass())) {

		//const TSharedPtr<FJsonObject> ValueProperty = PropertyObject->GetObjectField(TEXT("ValueProp"));
		//ResultProperty = PropertyObject->GetObjectField(TEXT("KeyProp"));

		UObject* SubCategoryObject = nullptr;
		bool bIsWeakPtr = false;
		FEdGraphTerminalType& PinValueType = OutPinType.PinValueType;

		//GetPropertyCategoryInfo(ValueProperty, PinValueType.TerminalCategory, PinValueType.TerminalSubCategory, SubCategoryObject, bIsWeakPtr, ObjectSerializer);
		PinValueType.TerminalSubCategoryObject = SubCategoryObject;
		OutPinType.ContainerType = EPinContainerType::Map;

	}
	else if (FieldClass->IsChildOf(USetProperty::StaticClass())) {
		//const TSharedPtr<FJsonObject> ElementType = PropertyObject->GetObjectField(TEXT("ElementType"));
		//ResultProperty = ElementType;
		OutPinType.ContainerType = EPinContainerType::Set;
	}
	else if (FieldClass->IsChildOf(UArrayProperty::StaticClass())) {

		//const TSharedPtr<FJsonObject> InnerProperty = PropertyObject->GetObjectField(TEXT("Inner"));
		//ResultProperty = InnerProperty;
		OutPinType.ContainerType = EPinContainerType::Array;

	}

	OutPinType.bIsReference = HasAllPropertyFlags(PropertyFlags, CPF_OutParm | CPF_ReferenceParm);
	OutPinType.bIsConst = HasAllPropertyFlags(PropertyFlags, CPF_ConstParm);

	// LIES!!!!
	//We actually do not want to set SignatureFunction on delegates, they need special handling in blueprints
	//because that function in fact will not even exist without delegate in first place
	if (FieldClass->IsChildOf(UMulticastDelegateProperty::StaticClass())) {
		int32 Index = PropertyObject->GetIntegerField("SignatureFunction");
		UField* SignatureFunction = Cast<UField>(ObjectSerializer->DeserializeObject(Index));
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
		FMemberReference::FillSimpleMemberReference<UFunction>(SignatureFunction, OutPinType.PinSubCategoryMemberReference);
	}
	else if (FieldClass->IsChildOf(UDelegateProperty::StaticClass())) {
		int32 Index = PropertyObject->GetIntegerField("SignatureFunction");
		UField* SignatureFunction = Cast<UField>(ObjectSerializer->DeserializeObject(Index));
		FMemberReference::FillSimpleMemberReference<UFunction>(SignatureFunction, OutPinType.PinSubCategoryMemberReference);
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Delegate;
	}
	else {
		UObject* SubCategoryObject = nullptr;
		bool bIsWeakPointer = false;
		GetPropertyCategoryInfo(ResultProperty, OutPinType.PinCategory, OutPinType.PinSubCategory, SubCategoryObject, bIsWeakPointer, ObjectSerializer);
		OutPinType.bIsWeakPointer = bIsWeakPointer;
		OutPinType.PinSubCategoryObject = SubCategoryObject;
	}

}

void GetPropertyCategoryInfo(const TSharedPtr<FJsonObject> PropertyObject, FName& OutCategory, FName& OutSubCategory, UObject*& OutSubCategoryObject, bool& bOutIsWeakPointer, class UObjectHierarchySerializer* ObjectSerializer) {
	static TMap<FString, UClass*> Mappings = GetUPropertyMappings();

	const FString ObjectClass = PropertyObject->GetStringField(TEXT("ObjectClass"));
	const FString ObjectName = PropertyObject->GetStringField(TEXT("ObjectName"));

	UClass* FieldClass = Mappings.FindChecked(ObjectClass);

	if (FieldClass->IsChildOf(UInterfaceProperty::StaticClass())) {
		OutCategory = UEdGraphSchema_K2::PC_Interface;
		const int32 InterfaceClassIndex = PropertyObject->GetIntegerField(TEXT("InterfaceClass"));
		OutSubCategoryObject = ObjectSerializer->DeserializeObject(InterfaceClassIndex);

	}
	else if (FieldClass->IsChildOf(UClassProperty::StaticClass())) {
		OutCategory = UEdGraphSchema_K2::PC_Class;
		const int32 MetaClassIndex = PropertyObject->GetIntegerField(TEXT("MetaClass"));
		OutSubCategoryObject = ObjectSerializer->DeserializeObject(MetaClassIndex);

	}
	else if (FieldClass->IsChildOf(USoftClassProperty::StaticClass())) {
		OutCategory = UEdGraphSchema_K2::PC_SoftClass;
		const int32 MetaClassIndex = PropertyObject->GetIntegerField(TEXT("MetaClass"));
		OutSubCategoryObject = ObjectSerializer->DeserializeObject(MetaClassIndex);

	}
	else if (FieldClass->IsChildOf(UObjectPropertyBase::StaticClass())) {
		OutCategory = UEdGraphSchema_K2::PC_Object;
		const int32 ObjectClassIndex = PropertyObject->GetIntegerField(TEXT("PropertyClass"));
		OutSubCategoryObject = ObjectSerializer->DeserializeObject(ObjectClassIndex);
		bOutIsWeakPointer = FieldClass->IsChildOf(UWeakObjectProperty::StaticClass());

	}
	else if (FieldClass->IsChildOf(UStructProperty::StaticClass())) {
		OutCategory = UEdGraphSchema_K2::PC_Struct;
		const int32 StructObjectIndex = PropertyObject->GetIntegerField(TEXT("Struct"));
		auto Object = ObjectSerializer->DeserializeObject(StructObjectIndex);

		if (Object) {
			UScriptStruct* Struct = CastChecked<UScriptStruct>(Object);
			OutSubCategoryObject = Struct;

			//Match IsTypeCompatibleWithProperty and erase REINST_ structs here:
			if (UUserDefinedStruct* UDS = Cast<UUserDefinedStruct>(Struct)) {
				UUserDefinedStruct* RealStruct = UDS->PrimaryStruct.Get();
				if (RealStruct) {
					OutSubCategoryObject = RealStruct;
				}
			}
		}
		else {
			UE_LOG(LogTemp, Warning, TEXT("Skip this asset: %s"), *ObjectName);
			OutCategory = UEdGraphSchema_K2::PC_Float;
		}


	}
	else if (FieldClass->IsChildOf(UFloatProperty::StaticClass())) {
		OutCategory = UEdGraphSchema_K2::PC_Float;

	}
	else if (FieldClass->IsChildOf(UInt64Property::StaticClass())) {
		OutCategory = UEdGraphSchema_K2::PC_Int64;

	}
	else if (FieldClass->IsChildOf(UIntProperty::StaticClass())) {
		OutCategory = UEdGraphSchema_K2::PC_Int;

	}
	else if (FieldClass->IsChildOf(UByteProperty::StaticClass())) {
		OutCategory = UEdGraphSchema_K2::PC_Byte;
		const int32 EnumObjectIndex = PropertyObject->GetIntegerField(TEXT("Enum"));
		OutSubCategoryObject = ObjectSerializer->DeserializeObject(EnumObjectIndex);

	}
	else if (FieldClass->IsChildOf(UEnumProperty::StaticClass())) {
		OutCategory = UEdGraphSchema_K2::PC_Byte;
		const int32 EnumObjectIndex = PropertyObject->GetIntegerField(TEXT("Enum"));
		OutSubCategoryObject = ObjectSerializer->DeserializeObject(EnumObjectIndex);

	}
	else if (FieldClass->IsChildOf(UNameProperty::StaticClass())) {
		OutCategory = UEdGraphSchema_K2::PC_Name;

	}
	else if (FieldClass->IsChildOf(UBoolProperty::StaticClass())) {
		OutCategory = UEdGraphSchema_K2::PC_Boolean;

	}
	else if (FieldClass->IsChildOf(UStrProperty::StaticClass())) {
		OutCategory = UEdGraphSchema_K2::PC_String;

	}
	else if (FieldClass->IsChildOf(UTextProperty::StaticClass())) {
		OutCategory = UEdGraphSchema_K2::PC_Text;

	}
	else {
		OutCategory = TEXT("bad_type");
	}
}

bool FAssetGenerationUtil::AreStructDescriptionsEqual(const FStructVariableDescription& A, const FStructVariableDescription& B) {
	return A.VarName == B.VarName &&
		A.VarGuid == B.VarGuid &&
		A.FriendlyName == B.FriendlyName &&
		A.Category == B.Category &&
		A.SubCategory == B.SubCategory &&
		A.SubCategoryObject == B.SubCategoryObject &&
		A.PinValueType == B.PinValueType &&
		A.ContainerType == B.ContainerType &&
		A.bDontEditOnInstance == B.bDontEditOnInstance &&
		A.bEnableSaveGame == B.bEnableSaveGame &&
		A.bEnable3dWidget == B.bEnable3dWidget &&
		A.bEnableMultiLineText == B.bEnableMultiLineText &&
		A.ToolTip == B.ToolTip;
}

bool FAssetGenerationUtil::IsFunctionSignatureRelevantProperty(const TSharedPtr<FJsonObject>& PropertyObject) {
	EPropertyFlags PropertyFlags = (EPropertyFlags)FCString::Atoi64(*PropertyObject->GetStringField(TEXT("PropertyFlags")));
	return (PropertyFlags & (CPF_Parm | CPF_OutParm | CPF_ReturnParm)) != 0;
}

