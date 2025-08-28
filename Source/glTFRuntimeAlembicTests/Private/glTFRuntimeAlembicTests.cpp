// Copyright 2025 - Roberto De Ioris

#include "glTFRuntimeAlembicTests.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

#define LOCTEXT_NAMESPACE "FglTFRuntimeAlembicTestsModule"

void FglTFRuntimeAlembicTestsModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FglTFRuntimeAlembicTestsModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

glTFRuntimeAlembic::Tests::FFixture::FFixture(const FString& Filename)
{
	const FString PluginDir = IPluginManager::Get().FindPlugin(TEXT("glTFRuntimeAlembic"))->GetBaseDir();
	const FString FixturePath = FPaths::Combine(PluginDir, TEXT("Source/glTFRuntimeAlembicTests/Private/Fixtures"), Filename);
	FFileHelper::LoadFileToArray(Blob, *FixturePath);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FglTFRuntimeAlembicTestsModule, glTFRuntimeAlembicTests)