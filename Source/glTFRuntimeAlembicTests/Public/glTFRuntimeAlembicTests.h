// Copyright 2025 - Roberto De Ioris

#pragma once

#include "Modules/ModuleManager.h"
#include "glTFRuntimeAsset.h"

namespace glTFRuntimeAlembic
{
	namespace Tests
	{
		struct FFixture
		{
			FFixture(const FString& Filename);

			TArray64<uint8> Blob;
		};
	}
};

class FglTFRuntimeAlembicTestsModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
