// Copyright 2025 - Roberto De Ioris


#include "glTFRuntimeAlembicAssetActor.h"
#include "glTFRuntimeABCFunctionLibrary.h"

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

	for (const TSharedRef<glTFRuntimeAlembic::FObject>& Child : Object->Children)
	{
		USceneComponent* ChildComponent = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), *Child->Name));
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