// Copyright 2025 - Roberto De Ioris

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeABC.h"
#include "glTFRuntimeABCFunctionLibrary.generated.h"

/**
 *
 */
UCLASS()
class GLTFRUNTIMEALEMBIC_API UglTFRuntimeABCFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MaterialsConfig", AutoCreateRefTerm = "StaticMeshMaterialsConfig"), Category = "glTFRuntime|Alembic")
	static bool LoadAlembicObjectAsRuntimeLOD(UglTFRuntimeAsset* Asset, const FString& ObjectPath, const int32 SampleIndex, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& StaticMeshMaterialsConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MaterialsConfig", AutoCreateRefTerm = "StaticMeshMaterialsConfig,SkeletalMeshMaterialsConfig"), Category = "glTFRuntime|Alembic")
	static class UGroomAsset* LoadGroomFromAlembicObject(UglTFRuntimeAsset* Asset, const FString& ObjectPath);

};
