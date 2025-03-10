#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraphPin.h"

class ASSETDUMPER_API UPropertyTypeHelper {
public:
    static FEdGraphPinType DeserializeGraphPinType(const TSharedRef<FJsonObject>& PinJson, UClass* SelfScope);
    static TSharedRef<FJsonObject> SerializeGraphPinType(const FEdGraphPinType& GraphPinType, UClass* SelfScope);
    static bool ConvertPropertyToPinType(const UProperty* Property, FEdGraphPinType& OutType);
};
