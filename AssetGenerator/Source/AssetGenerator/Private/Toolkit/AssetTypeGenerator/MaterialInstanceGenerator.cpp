#include "Toolkit/AssetTypeGenerator/MaterialInstanceGenerator.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionStaticComponentMaskParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Toolkit/ObjectHierarchySerializer.h"
#include "Toolkit/PropertySerializer.h"
#include "Toolkit/AssetDumping/AssetTypeSerializerMacros.h"
#include "StaticParameterSet.h"
#include "Toolkit/AssetTypeGenerator/MaterialGenerator.h"

void UMaterialInstanceGenerator::PostInitializeAssetGenerator() {
	UPropertySerializer* Serializer = GetPropertySerializer();

	//transient data, generated by the editor automatically and can be different
	DISABLE_SERIALIZATION_RAW(UMaterialInstanceConstant, "TextureStreamingData");
	DISABLE_SERIALIZATION_RAW(UMaterialInstanceConstant, "CachedReferencedTextures");
	DISABLE_SERIALIZATION_RAW(UMaterialInstanceConstant, "CachedLayerParameters");

	//not deserialized or compared directly, need special handling to make sure parameters in question exist
	DISABLE_SERIALIZATION_RAW(UMaterialInstanceConstant, "bHasStaticPermutationResource");
	DISABLE_SERIALIZATION_RAW(UMaterialInstanceConstant, "StaticParameters");

	//resolved by the editor by looking at parameter names and iterating material graph
	DISABLE_SERIALIZATION(FScalarParameterValue, ExpressionGUID);
	DISABLE_SERIALIZATION(FVectorParameterValue, ExpressionGUID);
	DISABLE_SERIALIZATION(FTextureParameterValue, ExpressionGUID);
	DISABLE_SERIALIZATION(FFontParameterValue, ExpressionGUID);
	//	DISABLE_SERIALIZATION(FRuntimeVirtualTextureParameterValue, ExpressionGUID);

		//do not deserialize material layers, we do not regenerate materials functions anyway
	DISABLE_SERIALIZATION(FStaticParameterSet, MaterialLayersParameters);
	DISABLE_SERIALIZATION(FStaticParameterSet, TerrainLayerWeightParameters);

	//deserialize manually to avoid cyclic dependencies
	DISABLE_SERIALIZATION_RAW(UMaterialInterface, "AssetUserData");
	this->AssetUserDataProperty = UMaterialInterface::StaticClass()->FindPropertyByName(TEXT("AssetUserData"));
}

UClass* UMaterialInstanceGenerator::GetAssetObjectClass() const {
	return UMaterialInstanceConstant::StaticClass();
}

FStaticParameterSet UMaterialInstanceGenerator::GetStaticParameterOverrides() const {
	const TSharedPtr<FJsonObject> AssetData = GetAssetData();
	const TSharedPtr<FJsonObject> AssetObjectProperties = AssetData->GetObjectField(TEXT("AssetObjectData"));
	const TSharedPtr<FJsonObject> StaticParametersObject = AssetObjectProperties->GetObjectField(TEXT("StaticParameters"));

	FStaticParameterSet StaticParameterOverrides;
	GetPropertySerializer()->DeserializeStruct(FStaticParameterSet::StaticStruct(), StaticParametersObject.ToSharedRef(), &StaticParameterOverrides);
	return StaticParameterOverrides;
}

void UMaterialInstanceGenerator::PopulateStageDependencies(TArray<FPackageDependency>& AssetDependencies) const {
	if (GetAssetData()->GetBoolField(TEXT("SkipDependecies"))) return;
	if (GetCurrentStage() == EAssetGenerationStage::CONSTRUCTION) {
		const TSharedPtr<FJsonObject> AssetData = GetAssetData();
		const TSharedPtr<FJsonObject> AssetObjectProperties = AssetData->GetObjectField(TEXT("AssetObjectData"));

		const TArray<TSharedPtr<FJsonValue>> AssetUserDataObjects = AssetObjectProperties->GetArrayField(TEXT("AssetUserData"));
		const TArray<TSharedPtr<FJsonValue>> ReferencedObjects = AssetObjectProperties->GetArrayField(TEXT("$ReferencedObjects"));

		TArray<int32> AssetUserDataObjectIndices;
		for (const TSharedPtr<FJsonValue>& ObjectIndexValue : AssetUserDataObjects) {
			AssetUserDataObjectIndices.Add((int32)ObjectIndexValue->AsNumber());
		}

		TArray<FString> ReferencedPackages;
		for (const TSharedPtr<FJsonValue>& ObjectIndexValue : ReferencedObjects) {
			const int32 ObjectIndex = (int32)ObjectIndexValue->AsNumber();

			if (!AssetUserDataObjectIndices.Contains(ObjectIndex)) {
				GetObjectSerializer()->CollectObjectPackages(ObjectIndex, ReferencedPackages);
			}
		}

		for (const FString& DependencyPackageName : ReferencedPackages) {
			AssetDependencies.Add(FPackageDependency{ *DependencyPackageName, EAssetGenerationStage::CDO_FINALIZATION });
		}
	}

	if (GetCurrentStage() == EAssetGenerationStage::PRE_FINSHED) {
		TArray<FString> ReferencedPackages;
		const TSharedPtr<FJsonObject> AssetData = GetAssetData();
		const TSharedPtr<FJsonObject> AssetObjectProperties = AssetData->GetObjectField(TEXT("AssetObjectData"));

		const TArray<TSharedPtr<FJsonValue>> AssetUserDataObjects = AssetObjectProperties->GetArrayField(TEXT("AssetUserData"));

		for (const TSharedPtr<FJsonValue>& AssetObjectValue : AssetUserDataObjects) {
			const int32 ObjectIndex = (int32)AssetObjectValue->AsNumber();
			GetObjectSerializer()->CollectObjectPackages(ObjectIndex, ReferencedPackages);
		}
		for (const FString& PackageName : ReferencedPackages) {
			AssetDependencies.Add(FPackageDependency{ *PackageName, EAssetGenerationStage::CDO_FINALIZATION });
		}
	}
}

void UMaterialInstanceGenerator::PreFinishAssetGeneration() {
	UMaterialInterface* Asset = GetAsset<UMaterialInterface>();
	const TSharedPtr<FJsonObject> AssetObjectProperties = GetAssetData()->GetObjectField(TEXT("AssetObjectData"));

	void* AssetUserData = AssetUserDataProperty->ContainerPtrToValuePtr<void>(Asset);
	const TSharedPtr<FJsonValue> AssetUserDataJson = AssetObjectProperties->GetField<EJson::Array>(TEXT("AssetUserData"));

	GetPropertySerializer()->DeserializePropertyValue(AssetUserDataProperty, AssetUserDataJson.ToSharedRef(), AssetUserData);
}

void EnsureStaticSwitchNodesPresent(UMaterial* Material, const FStaticParameterSet& StaticParameters) {
	TArray<FName> ExistingParameters;

	for (UMaterialExpression* Expression : Material->Expressions) {
		if (UMaterialExpressionStaticSwitchParameter* StaticSwitchParameter = Cast<UMaterialExpressionStaticSwitchParameter>(Expression)) {
			ExistingParameters.Add(StaticSwitchParameter->ParameterName);
		}
		if (UMaterialExpressionStaticBoolParameter* StaticBoolParameter = Cast<UMaterialExpressionStaticBoolParameter>(Expression)) {
			ExistingParameters.Add(StaticBoolParameter->ParameterName);
		}
		if (UMaterialExpressionStaticComponentMaskParameter* StaticMaskParameter = Cast<UMaterialExpressionStaticComponentMaskParameter>(Expression)) {
			ExistingParameters.Add(StaticMaskParameter->ParameterName);
		}
	}

	bool bMaterialGraphChanged = false;

	for (const FStaticSwitchParameter& StaticSwitchParameter : StaticParameters.StaticSwitchParameters) {
		if (!ExistingParameters.Contains(StaticSwitchParameter.ParameterInfo.Name)) {
			UMaterialExpressionStaticSwitchParameter* Parameter = UMaterialGenerator::SpawnMaterialExpression<UMaterialExpressionStaticSwitchParameter>(Material);
			Parameter->SetParameterName(StaticSwitchParameter.ParameterInfo.Name);
			bMaterialGraphChanged = true;
		}
	}

	for (const FStaticComponentMaskParameter& StaticComponentMaskParameter : StaticParameters.StaticComponentMaskParameters) {
		if (!ExistingParameters.Contains(StaticComponentMaskParameter.ParameterInfo.Name)) {
			UMaterialExpressionStaticComponentMaskParameter* Parameter = UMaterialGenerator::SpawnMaterialExpression<UMaterialExpressionStaticComponentMaskParameter>(Material);
			Parameter->SetParameterName(StaticComponentMaskParameter.ParameterInfo.Name);
			bMaterialGraphChanged = true;
		}
	}

	if (bMaterialGraphChanged) {
		UMaterialGenerator::ConnectBasicParameterPinsIfPossible(Material, TEXT("Static Parameters Added from MaterialInstance"));
		UMaterialGenerator::ForceMaterialCompilation(Material);
	}
}

void UMaterialInstanceGenerator::PopulateSimpleAssetWithData(UObject* Asset) {
	Super::PopulateSimpleAssetWithData(Asset);
	UMaterialInstanceConstant* MaterialInstance = CastChecked<UMaterialInstanceConstant>(Asset);

	if (!MaterialInstance->Parent) {
		UE_LOG(LogAssetGenerator, Error, TEXT("Failed to deserialize Parent Material for MaterialInstance %s. Falling back to default material"), *MaterialInstance->GetPathName());
		MaterialInstance->Parent = UMaterial::GetDefaultMaterial(EMaterialDomain::MD_Surface);
		return;
	}

	UMaterial* ParentMaterial = MaterialInstance->GetMaterial();
	const FStaticParameterSet StaticParameterOverrides = GetStaticParameterOverrides();

	EnsureStaticSwitchNodesPresent(ParentMaterial, StaticParameterOverrides);
	MaterialInstance->UpdateStaticPermutation(StaticParameterOverrides);

	//Regenerate ExpressionGUID values on updated parameter values by calling UpdateParameters()
	//but since it's protected we're calling post load instead, even though it does a little bit more than that, but who cares
	MaterialInstance->PostLoad();
}

bool operator==(const FStaticSwitchParameter& a, const FStaticSwitchParameter& b) {
	return a.bOverride == b.bOverride && a.ExpressionGUID == b.ExpressionGUID && a.ParameterInfo == b.ParameterInfo && a.Value == b.Value;
}

bool operator==(const FStaticComponentMaskParameter& a, const FStaticComponentMaskParameter& b) {
	return a.A == b.A && a.B == b.B && a.bOverride == b.bOverride && a.ExpressionGUID == b.ExpressionGUID && a.G == b.G && a.ParameterInfo == b.ParameterInfo && a.R == b.R;
}

bool UMaterialInstanceGenerator::IsSimpleAssetUpToDate(UObject* Asset) const {
	UMaterialInstanceConstant* MaterialInstance = CastChecked<UMaterialInstanceConstant>(Asset);
	const TSharedPtr<FJsonObject> AssetObjectProperties = GetAssetData()->GetObjectField(TEXT("AssetObjectData"));

	if (!Super::IsSimpleAssetUpToDate(Asset)) {
		return false;
	}

	const void* AssetUserData = AssetUserDataProperty->ContainerPtrToValuePtr<void>(Asset);
	const TSharedPtr<FJsonValue> AssetUserDataJson = AssetObjectProperties->GetField<EJson::Array>(TEXT("AssetUserData"));

	if (!GetPropertySerializer()->ComparePropertyValues(AssetUserDataProperty, AssetUserDataJson.ToSharedRef(), AssetUserData)) {
		return false;
	}

	const FStaticParameterSet StaticParameterOverrides = GetStaticParameterOverrides();
	const FStaticParameterSet& ExistingStaticParameters = MaterialInstance->GetStaticParameters();

	return StaticParameterOverrides.StaticSwitchParameters == ExistingStaticParameters.StaticSwitchParameters &&
		StaticParameterOverrides.StaticComponentMaskParameters == ExistingStaticParameters.StaticComponentMaskParameters;
}

FName UMaterialInstanceGenerator::GetAssetClass() {
	return UMaterialInstanceConstant::StaticClass()->GetFName();
}
