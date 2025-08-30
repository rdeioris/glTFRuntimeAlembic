// Copyright 2025 - Roberto De Ioris

#if WITH_DEV_AUTOMATION_TESTS
#include "glTFRuntimeAlembicTests.h"
#include "glTFRuntimeABC.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeAlembicTests_Archive_BlenderDefault, "glTFRuntime.Alembic.UnitTests.Archive.BlenderDefault", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeAlembicTests_Archive_BlenderDefault::RunTest(const FString& Parameters)
{
	glTFRuntimeAlembic::Tests::FFixture Fixture("blender_default.abc");

	TSharedPtr<glTFRuntimeAlembic::IOgawaNode> Root = glTFRuntimeAlembic::ParseOgawaBlob(Fixture.Blob);

	TSharedPtr<glTFRuntimeAlembic::FObject> RootObject = glTFRuntimeAlembic::ParseArchive(Root->Group().ToSharedRef());

	TestEqual("RootObject->Name == \"ABC\"", RootObject->Name, "ABC");

	TestEqual("RootObject->GetChildrenNames() == [\"Camera\", \"Cube\", \"Light\"]", RootObject->GetChildrenNames(), { "Cube", "Camera", "Light" });

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeAlembicTests_Archive_BlenderDefaultFind, "glTFRuntime.Alembic.UnitTests.Archive.BlenderDefaultFind", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeAlembicTests_Archive_BlenderDefaultFind::RunTest(const FString& Parameters)
{
	glTFRuntimeAlembic::Tests::FFixture Fixture("blender_default.abc");

	TSharedPtr<glTFRuntimeAlembic::IOgawaNode> Root = glTFRuntimeAlembic::ParseOgawaBlob(Fixture.Blob);

	TSharedPtr<glTFRuntimeAlembic::FObject> RootObject = glTFRuntimeAlembic::ParseArchive(Root->Group().ToSharedRef());

	TestTrue("RootObject->Find(\"/Cube/Cube\") != nullptr", RootObject->Find("/Cube/Cube") != nullptr);

	TestTrue("RootObject->Find(\"/Cube/BrokenCube\") == nullptr", RootObject->Find("/Cube/BrokenCube") == nullptr);

	TestTrue("RootObject->Find(\"Cube\") != nullptr", RootObject->Find("Cube") != nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeAlembicTests_Archive_FileMetadata, "glTFRuntime.Alembic.UnitTests.Archive.FileMetadata", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeAlembicTests_Archive_FileMetadata::RunTest(const FString& Parameters)
{
	glTFRuntimeAlembic::Tests::FFixture Fixture("blender_default.abc");

	TSharedPtr<glTFRuntimeAlembic::FObject> RootObject = glTFRuntimeAlembic::ParseArchive(Fixture.Blob);

	TestEqual("RootObject->Metadata.Num() == 6", RootObject->Metadata.Num(), 6);

	TestEqual("RootObject->Metadata[\"blender_version\"] == nullptr", RootObject->Metadata["blender_version"], "v4.5.1 LTS");

	return true;
}

#endif