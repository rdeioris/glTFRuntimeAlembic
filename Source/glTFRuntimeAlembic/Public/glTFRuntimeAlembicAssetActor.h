// Copyright 2025 - Roberto De Ioris

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeABC.h"
#include "glTFRuntimeAlembicAssetActor.generated.h"


UCLASS()
class GLTFRUNTIMEALEMBIC_API AglTFRuntimeAlembicAssetActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AglTFRuntimeAlembicAssetActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	void ProcessObject(USceneComponent* Component, TSharedRef<glTFRuntimeAlembic::FObject> Object);

	int32 TrueSampleIndex = 0;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime|Alembic")
	UglTFRuntimeAsset* Asset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime|Alembic")
	FglTFRuntimeStaticMeshConfig StaticMeshConfig;

	UFUNCTION(BlueprintNativeEvent, Category = "glTFRuntime|Alembic", meta = (DisplayName = "On StaticMeshComponent Created"))
	void ReceiveOnStaticMeshComponentCreated(UStaticMeshComponent* StaticMeshComponent);

	UFUNCTION(BlueprintNativeEvent, Category = "glTFRuntime|Alembic", meta = (DisplayName = "On Scenes Loaded"))
	void ReceiveOnScenesLoaded();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime|Alembic")
	int32 SampleIndex = 0;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"), Category = "glTFRuntime|Alembic")
	USceneComponent* AssetRoot;

};
