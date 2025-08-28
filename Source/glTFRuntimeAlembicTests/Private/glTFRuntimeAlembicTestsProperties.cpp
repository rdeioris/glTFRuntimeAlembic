// Copyright 2025 - Roberto De Ioris

#if WITH_DEV_AUTOMATION_TESTS
#include "glTFRuntimeAlembicTests.h"
#include "glTFRuntimeABCFunctionLibrary.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeAlembicTests_Properties_Xform, "glTFRuntime.Alembic.UnitTests.Properties.xform", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeAlembicTests_Properties_Xform::RunTest(const FString& Parameters)
{
	glTFRuntimeAlembic::Tests::FFixture Fixture("blender_default.abc");

	TSharedPtr<glTFRuntimeAlembic::IOgawaNode> Root = glTFRuntimeAlembic::ParseOgawaBlob(Fixture.Blob);

	TSharedPtr<glTFRuntimeAlembic::FObject> RootObject = glTFRuntimeAlembic::ParseArchive(Root->Group().ToSharedRef());

	TestTrue("RootObject->GetChild(\"Cube\")->GetProperty(\".xform\") != nullptr", RootObject->GetChild("Cube")->GetProperty(".xform") != nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeAlembicTests_Properties_Geom, "glTFRuntime.Alembic.UnitTests.Properties.geom", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeAlembicTests_Properties_Geom::RunTest(const FString& Parameters)
{
	glTFRuntimeAlembic::Tests::FFixture Fixture("blender_default.abc");

	TSharedPtr<glTFRuntimeAlembic::IOgawaNode> Root = glTFRuntimeAlembic::ParseOgawaBlob(Fixture.Blob);

	TSharedPtr<glTFRuntimeAlembic::FObject> RootObject = glTFRuntimeAlembic::ParseArchive(Root->Group().ToSharedRef());

	TestTrue("RootObject->GetChild(\"Cube\")->GetChild(\"Cube\")->GetProperty(\".geom\") != nullptr", RootObject->GetChild("Cube")->GetChild("Cube")->GetProperty(".geom") != nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeAlembicTests_Properties_Geom_P, "glTFRuntime.Alembic.UnitTests.Properties.geom_P", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeAlembicTests_Properties_Geom_P::RunTest(const FString& Parameters)
{
	glTFRuntimeAlembic::Tests::FFixture Fixture("blender_default.abc");

	TSharedPtr<glTFRuntimeAlembic::IOgawaNode> Root = glTFRuntimeAlembic::ParseOgawaBlob(Fixture.Blob);

	TSharedPtr<glTFRuntimeAlembic::FObject> RootObject = glTFRuntimeAlembic::ParseArchive(Root->Group().ToSharedRef());

	TestTrue("RootObject->GetChild(\"Cube\")->GetChild(\"Cube\")->GetProperty(\".geom\") != nullptr", RootObject->Find("Cube/Cube")->FindProperty(".geom/P") != nullptr);

	return true;
}

#endif