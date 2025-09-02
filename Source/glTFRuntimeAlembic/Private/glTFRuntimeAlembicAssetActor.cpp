// Copyright 2025 - Roberto De Ioris


#include "glTFRuntimeAlembicAssetActor.h"
#include "glTFRuntimeABCFunctionLibrary.h"
#include "glTFRuntimeGeomCacheComponent.h"
#include "glTFRuntimeGeomCacheFuncLibrary.h"
#include "GroomComponent.h"
#include "glTFRuntimeGeometryCacheTrack.h"

// Sets default values
AglTFRuntimeAlembicAssetActor::AglTFRuntimeAlembicAssetActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	AssetRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ABC"));
	RootComponent = AssetRoot;
}

// Called when the game starts or when spawned
void AglTFRuntimeAlembicAssetActor::BeginPlay()
{
	Super::BeginPlay();

	if (!Asset)
	{
		return;
	}

	TSharedPtr<glTFRuntimeAlembic::FObject> RootObject = glTFRuntimeAlembic::ParseArchive(Asset->GetParser()->GetBlob());
	if (!RootObject)
	{
		Asset->GetParser()->AddError("AglTFRuntimeAlembicAssetActor::BeginPlay()", "Invalid Alembic archive");
		return;
	}

	ProcessObject(AssetRoot, RootObject.ToSharedRef());

	ReceiveOnScenesLoaded();
}

// Called every frame
void AglTFRuntimeAlembicAssetActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AglTFRuntimeAlembicAssetActor::ProcessObject(USceneComponent* Component, TSharedRef<glTFRuntimeAlembic::FObject> Object)
{
	Component->ComponentTags.Add(FName(FString::Printf(TEXT("glTFRuntimeAlembic::Object::Name::%s"), *Object->Name)));
	Component->ComponentTags.Add(FName(FString::Printf(TEXT("glTFRuntimeAlembic::Object::Path::%s"), *Object->Path)));

	for (const TPair<FString, FString>& Pair : Object->Metadata)
	{
		Component->ComponentTags.Add(FName(FString::Printf(TEXT("glTFRuntimeAlembic::Object::Metadata::%s=%s"), *Pair.Key, *Pair.Value)));
	}

	if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
	{
		FglTFRuntimeMeshLOD LOD;
		if (UglTFRuntimeABCFunctionLibrary::LoadAlembicObjectAsRuntimeLOD(Asset, Object->Path, SampleIndex, LOD, StaticMeshConfig.MaterialsConfig))
		{
			UStaticMesh* StaticMesh = Asset->LoadStaticMeshFromRuntimeLODs({ LOD }, StaticMeshConfig);
			if (StaticMesh)
			{
				StaticMeshComponent->SetStaticMesh(StaticMesh);
			}
		}
	}
	else if (UglTFRuntimeGeomCacheComponent* GeomCacheComponent = Cast<UglTFRuntimeGeomCacheComponent>(Component))
	{
		// retrieve the number of frames from .geom/P
		TSharedPtr<glTFRuntimeAlembic::FArrayProperty> PositionsProperty = Object->FindArrayProperty(".geom/P");
		if (PositionsProperty)
		{
			const uint32 NumFrames = PositionsProperty->NextSampleIndex;
			TArray<FglTFRuntimeGeometryCacheFrame> Frames;
			Frames.AddDefaulted(NumFrames);
			for (uint32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
			{
				Frames[FrameIndex].Time = (1.0 / 24) * FrameIndex;
				if (!UglTFRuntimeABCFunctionLibrary::LoadAlembicObjectAsRuntimeLOD(Asset, Object->Path, FrameIndex, Frames[FrameIndex].Mesh, StaticMeshConfig.MaterialsConfig))
				{
					UE_LOG(LogGLTFRuntime, Warning, TEXT("Unable to load sample %u from %s"), FrameIndex, *Object->Path);
				}
			}
			UglTFRuntimeGeometryCacheTrack* Track = UglTFRuntimeGeomCacheFuncLibrary::LoadRuntimeTrackFromGeometryCacheFrames(Frames);
			if (Track)
			{
				UGeometryCache* GeometryCache = UglTFRuntimeGeomCacheFuncLibrary::LoadGeometryCacheFromRuntimeTracks({ Track });
				if (GeometryCache)
				{
					GeomCacheComponent->SetGeometryCache(GeometryCache);
				}
			}
		}
	}
	else if (UGroomComponent* GroomComponent = Cast<UGroomComponent>(Component))
	{
		UGroomAsset* GroomAsset = UglTFRuntimeABCFunctionLibrary::LoadGroomFromAlembicObject(Asset, Object->Path);
		if (!GroomAsset)
		{
			UE_LOG(LogGLTFRuntime, Warning, TEXT("Unable to load groom from %s"), *Object->Path);
		}
		else
		{
			GroomComponent->SetGroomAsset(GroomAsset);
		}
	}
	else
	{
		if (TSharedPtr<glTFRuntimeAlembic::FScalarProperty> MatrixOpsProperty = Object->FindScalarProperty(".xform/.ops"))
		{
			if (TSharedPtr<glTFRuntimeAlembic::FScalarProperty> MatrixValsProperty = Object->FindScalarProperty(".xform/.vals"))
			{
				uint32 MatrixOpsPropertyTrueSampleIndex;
				if (!MatrixOpsProperty->GetSampleTrueIndex(SampleIndex, MatrixOpsPropertyTrueSampleIndex))
				{
					return;
				}
				uint32 MatrixValsPropertyTrueSampleIndex;
				if (!MatrixValsProperty->GetSampleTrueIndex(SampleIndex, MatrixValsPropertyTrueSampleIndex))
				{
					return;
				}

				FMatrix Matrix;
				if (glTFRuntimeAlembic::BuildMatrix(MatrixOpsPropertyTrueSampleIndex, MatrixOpsProperty.ToSharedRef(), MatrixValsPropertyTrueSampleIndex, MatrixValsProperty.ToSharedRef(), Matrix))
				{
					Component->SetRelativeTransform(Asset->GetParser()->TransformTransform(FTransform(Matrix)));
				}
			}
		}
	}

	for (const TSharedRef<glTFRuntimeAlembic::FObject>& Child : Object->Children)
	{
		USceneComponent* ChildComponent = nullptr;

		if (Child->GetSchema() == "AbcGeom_PolyMesh_v1")
		{
			if (bUseGeometryCache)
			{
				ChildComponent = NewObject<UglTFRuntimeGeomCacheComponent>(this, MakeUniqueObjectName(this, UglTFRuntimeGeomCacheComponent::StaticClass(), *Child->Name));
			}
			else
			{
				ChildComponent = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), *Child->Name));
			}
		}
		else if (Child->GetSchema() == "AbcGeom_Curve_v2")
		{
			ChildComponent = NewObject<UGroomComponent>(this, MakeUniqueObjectName(this, UGroomComponent::StaticClass(), *Child->Name));
		}
		else
		{
			ChildComponent = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), *Child->Name));
		}

		ChildComponent->SetupAttachment(Component);
		ChildComponent->RegisterComponent();
		AddInstanceComponent(ChildComponent);

		ProcessObject(ChildComponent, Child);
	}
}

void AglTFRuntimeAlembicAssetActor::ReceiveOnStaticMeshComponentCreated_Implementation(UStaticMeshComponent* StaticMeshComponent)
{

}

void AglTFRuntimeAlembicAssetActor::ReceiveOnScenesLoaded_Implementation()
{

}