// Copyright 2025 - Roberto De Ioris

#if WITH_DEV_AUTOMATION_TESTS
#include "glTFRuntimeAlembicTests.h"
#include "glTFRuntimeABCFunctionLibrary.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeAlembicTests_Ogawa_ZeroBlob, "glTFRuntime.Alembic.UnitTests.Ogawa.ZeroBlob", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeAlembicTests_Ogawa_ZeroBlob::RunTest(const FString& Parameters)
{
	TestTrue("ParseOgawaBlob(FglTFRuntimeBlob()) == nullptr", glTFRuntimeAlembic::ParseOgawaBlob({}) == nullptr);

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeAlembicTests_Ogawa_Empty, "glTFRuntime.Alembic.UnitTests.Ogawa.Empty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeAlembicTests_Ogawa_Empty::RunTest(const FString& Parameters)
{
	glTFRuntimeAlembic::Tests::FFixture Fixture("empty.abc");

	TSharedPtr<glTFRuntimeAlembic::IOgawaNode> Root = glTFRuntimeAlembic::ParseOgawaBlob(Fixture.Blob);

	TestTrue("Root != nullptr", Root != nullptr);

	TSharedPtr<glTFRuntimeAlembic::FOgawaGroup> RootGroup = Root->Group();

	TestTrue("RootGroup != nullptr", RootGroup != nullptr);

	TestEqual("RootGroup->Children.Num() == 6", RootGroup->Children.Num(), 6);

	TestEqual("RootGroup->Children[0]->bIsData == true", RootGroup->Children[0]->bIsData, true);
	TestEqual("RootGroup->Children[1]->bIsData == true", RootGroup->Children[1]->bIsData, true);
	TestEqual("RootGroup->Children[2]->bIsData == false", RootGroup->Children[2]->bIsData, false);
	TestEqual("RootGroup->Children[3]->bIsData == true", RootGroup->Children[3]->bIsData, true);
	TestEqual("RootGroup->Children[4]->bIsData == true", RootGroup->Children[4]->bIsData, true);
	TestEqual("RootGroup->Children[5]->bIsData == true", RootGroup->Children[5]->bIsData, true);

	return true;
}

#endif