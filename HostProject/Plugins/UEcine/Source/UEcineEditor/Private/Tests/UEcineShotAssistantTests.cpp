#include "CineCameraActor.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/AutomationTest.h"
#include "MovieScene.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineFocusEditing.h"
#include "UEcineGripEditing.h"
#include "UEcineRig.h"
#include "UEcineSequenceUtils.h"
#include "UEcineShotAssistant.h"
#include "UEcineShotEditing.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FUEcineShotPlan ReadAssistantTestPlan(FAutomationTestBase& Test, const TCHAR* Json)
	{
		FUEcineShotPlan Plan;
		FString Error;
		Test.TestTrue(*FString::Printf(TEXT("The test's plan is read (%s)"), *Error), UEcine::ShotPlan::Parse(Json, Plan, Error));
		return Plan;
	}

	bool HasWarning(const UEcine::ShotAssistant::FPlanLine& Line, const TCHAR* Words)
	{
		return Line.Warnings.ContainsByPredicate([Words](const FText& Warning) { return Warning.ToString().Contains(Words); });
	}
}

/**
 * A shot plan as the assistant would give it, read back to the user, carried out through the
 * tabs' own operations, and taken back with one undo.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineShotAssistantTest, "UEcine.Editor.ShotAssistant", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineShotAssistantTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::ShotAssistant;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	TArray<AActor*> SpawnedActors;
	TArray<ULevelSequence*> CreatedSequences;
	ON_SCOPE_EXIT
	{
		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
		GEditor->SelectNone(false, true, false);
		for (AActor* Actor : SpawnedActors)
		{
			if (AUEcineRig* Rig = UEcine::GripEditing::FindRig(Cast<ACineCameraActor>(Actor)))
			{
				World->EditorDestroyActor(Rig, false);
			}
			World->EditorDestroyActor(Actor, false);
		}
		GEditor->ResetTransaction(FText::FromString(TEXT("UEcine shot assistant test")));
		for (ULevelSequence* Sequence : CreatedSequences)
		{
			TestTrue(TEXT("The test's sequence asset is removed"), ObjectTools::DeleteSingleObject(Sequence, false));
		}
	};
	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();

	// A camera looking along X at Anna, 6 m away and a little to its right, with a table nearer.
	const FTransform CameraStart(FRotator::ZeroRotator, FVector(0.0, 0.0, 160.0));
	ACineCameraActor* Camera = UEcine::CameraEditing::SpawnCamera(World, CameraStart);
	AStaticMeshActor* Anna = World->SpawnActor<AStaticMeshActor>(FVector(600.0, 80.0, 90.0), FRotator::ZeroRotator);
	AStaticMeshActor* Table = World->SpawnActor<AStaticMeshActor>(FVector(300.0, -50.0, 50.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("A camera"), Camera) || !TestNotNull(TEXT("Anna"), Anna) || !TestNotNull(TEXT("A table"), Table))
	{
		return false;
	}
	SpawnedActors = { Camera, Anna, Table };
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Anna->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Anna->SetActorScale3D(FVector(0.5, 0.5, 1.8));
	Anna->SetActorLabel(TEXT("Anna"));
	Table->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Table->SetActorLabel(TEXT("Table"));
	Camera->SetActorLabel(TEXT("A Cam"));
	UUEcineCameraPackageComponent* Component = UUEcineCameraPackageComponent::Find(Camera);
	const FUEcineCameraPackage PackageBefore = Component->Package;

	// What the assistant is told of the set.
	{
		TSharedPtr<FJsonObject> Set;
		const FString Description = DescribeSet(Camera);
		AddInfo(FString::Printf(TEXT("The set: %s"), *Description));
		TestTrue(TEXT("Numbers are written as a crew would say them"), Description.Contains(TEXT("\"tStop\":2.8,")));
		if (TestTrue(TEXT("The set's description is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Description), Set) && Set.IsValid()))
		{
			const TSharedPtr<FJsonObject> CameraObject = Set->GetObjectField(TEXT("camera"));
			TestEqual(TEXT("The camera is named"), *CameraObject->GetStringField(TEXT("name")), TEXT("A Cam"));
			TestEqual(TEXT("With its body"), *CameraObject->GetStringField(TEXT("bodyId")), *PackageBefore.BodyId);
			TestEqual(TEXT("Its lens kit"), *CameraObject->GetStringField(TEXT("lensKitId")), *PackageBefore.LensKitId);
			TestEqual(TEXT("Its focal length"), CameraObject->GetNumberField(TEXT("focalLengthMm")), static_cast<double>(PackageBefore.FocalLengthMm), 0.1);
			TestEqual(TEXT("And its ISO"), CameraObject->GetIntegerField(TEXT("iso")), PackageBefore.Iso);
			TestTrue(TEXT("Where its focus is"), CameraObject->GetObjectField(TEXT("focus"))->HasField(TEXT("distanceM")));
			TestFalse(TEXT("A camera on no support has none to tell of"), Set->HasField(TEXT("support")));
			TestFalse(TEXT("With no sequence open there is none to tell of"), Set->HasField(TEXT("sequence")));

			const TArray<TSharedPtr<FJsonValue>>& Subjects = Set->GetArrayField(TEXT("subjects"));
			if (TestEqual(TEXT("The things near the camera are told, and the camera is not one of them"), Subjects.Num(), 2))
			{
				TestEqual(TEXT("Nearest first"), *Subjects[0]->AsObject()->GetStringField(TEXT("name")), TEXT("Table"));
				const FJsonObject& AnnaObject = *Subjects[1]->AsObject();
				TestEqual(TEXT("Anna by name"), *AnnaObject.GetStringField(TEXT("name")), TEXT("Anna"));
				TestEqual(TEXT("6 m ahead"), AnnaObject.GetNumberField(TEXT("aheadM")), 6.0, 0.01);
				TestEqual(TEXT("0.8 m to the right"), AnnaObject.GetNumberField(TEXT("rightM")), 0.8, 0.01);
				TestEqual(TEXT("Her middle 0.7 m below the lens"), AnnaObject.GetNumberField(TEXT("upM")), -0.7, 0.01);
				TestEqual(TEXT("1.8 m tall"), AnnaObject.GetNumberField(TEXT("heightM")), 1.8, 0.01);
			}
		}
	}

	// A plan that cannot be carried out says where and why, and changes nothing.
	{
		const FUEcineShotPlan Faulty = ReadAssistantTestPlan(*this, TEXT(R"({ "summary": "", "steps": [
			{ "step": "choose_camera", "bodyId": "kinetoscope", "formatId": "35mm" },
			{ "step": "choose_camera", "bodyId": "arri-alexa-35", "formatId": "imax" },
			{ "step": "choose_lens", "lensKitId": "magic-lantern", "focalLengthMm": 50 },
			{ "step": "put_on_support", "supportId": "hoverboard" },
			{ "step": "move_to_mark", "travelM": 2, "sidewaysM": 0, "liftM": 0, "swingDeg": 0, "boomDeg": 0, "extensionM": 0,
			  "panDeg": 0, "tiltDeg": 0, "rollDeg": 0, "durationSeconds": 4, "easeIn": 0.25, "easeOut": 0.25 },
			{ "step": "focus_on", "subject": "Ben", "follow": true },
			{ "step": "bake_move" } ] })"));
		const TArray<FPlanLine> Lines = Review(Faulty, Camera);
		if (TestEqual(TEXT("Every step has its line"), Lines.Num(), 7))
		{
			TestEqual(TEXT("A body that is not in the database"), *Lines[0].Problem.ToString(), TEXT("The camera database has no body \"kinetoscope\"."));
			TestEqual(TEXT("A format the body does not have"), *Lines[1].Problem.ToString(), TEXT("ARRI ALEXA 35 has no recording format \"imax\"."));
			TestEqual(TEXT("A lens kit that is not in the database"), *Lines[2].Problem.ToString(), TEXT("The camera database has no lens kit \"magic-lantern\"."));
			TestEqual(TEXT("A support that does not exist"), *Lines[3].Problem.ToString(), TEXT("There is no support \"hoverboard\"."));
			TestEqual(TEXT("A move with nothing to make it"), *Lines[4].Problem.ToString(), TEXT("The camera is on no support to move. Put it on one first."));
			TestEqual(TEXT("A subject that is not there"), *Lines[5].Problem.ToString(), TEXT("Nothing near the camera is called \"Ben\"."));
			TestEqual(TEXT("A bake with no move"), *Lines[6].Problem.ToString(), TEXT("There is no move to bake: the rig needs two marks first."));
		}
		TestFalse(TEXT("Such a plan cannot be applied"), CanApply(Lines));
		FText Error;
		TestFalse(TEXT("And is not"), Apply(Faulty, Camera, Error));
		TestEqual(TEXT("The reason names the first step in the way"), *Error.ToString(), TEXT("Step 1 cannot be carried out. The camera database has no body \"kinetoscope\"."));
		TestTrue(TEXT("Nothing changed"), Component->Package.BodyId == PackageBefore.BodyId && UEcine::GripEditing::FindRig(Camera) == nullptr);
		TestFalse(TEXT("A plan with no steps cannot be applied either"), CanApply(Review(FUEcineShotPlan(), Camera)));
	}

	// A plan that sets the camera up and blocks a crane move.
	const FUEcineShotPlan Plan = ReadAssistantTestPlan(*this, TEXT(R"({ "summary": "On the crane, rising over the table to Anna.", "steps": [
		{ "step": "choose_camera", "bodyId": "sony-venice-2-8k", "formatId": "8.6k-17x9" },
		{ "step": "choose_lens", "lensKitId": "cooke-s4i", "focalLengthMm": 30 },
		{ "step": "set_exposure", "tStop": 1.4, "iso": 1600, "shutterAngleDeg": 172.8, "ndStops": 2 },
		{ "step": "put_on_support", "supportId": "crane-30ft" },
		{ "step": "move_to_mark", "travelM": 2, "sidewaysM": 0, "liftM": 0, "swingDeg": 0, "boomDeg": 20, "extensionM": 1.5,
		  "panDeg": 0, "tiltDeg": -10, "rollDeg": 0, "durationSeconds": 8, "easeIn": 0.25, "easeOut": 0.25 },
		{ "step": "move_to_mark", "travelM": 0, "sidewaysM": 0, "liftM": 0, "swingDeg": 90, "boomDeg": 0, "extensionM": 0,
		  "panDeg": 0, "tiltDeg": 0, "rollDeg": 0, "durationSeconds": 1, "easeIn": 0.25, "easeOut": 0.25 },
		{ "step": "focus_on", "subject": "anna", "follow": true } ] })"));
	const TArray<FPlanLine> Lines = Review(Plan, Camera);
	for (const FPlanLine& Line : Lines)
	{
		AddInfo(FString::Printf(TEXT("%s%s"), *Line.Text.ToString(), Line.Warnings.IsEmpty() ? TEXT("") : *(TEXT("  [") + FText::Join(FText::FromString(TEXT(" | ")), Line.Warnings).ToString() + TEXT("]"))));
	}
	if (!TestEqual(TEXT("Every step has its line"), Lines.Num(), 7))
	{
		return false;
	}
	TestEqual(TEXT("The camera, by the names a crew knows"), *Lines[0].Text.ToString(), TEXT("Camera: Sony VENICE 2 8K, 8.6K 17:9"));
	TestEqual(TEXT("The lens that will be put on"), *Lines[1].Text.ToString(), TEXT("Lens: Cooke S4/i 32mm T2"));
	TestTrue(TEXT("A focal length the kit lacks is said"), HasWarning(Lines[1], TEXT("The kit has no 30 mm. The 32 mm is the nearest.")));
	TestTrue(TEXT("A kit that does not cover the format is said"), HasWarning(Lines[1], TEXT("vignette")));
	TestEqual(TEXT("The exposure"), *Lines[2].Text.ToString(), TEXT("Exposure: T1.4, ISO 1600, 172.8° shutter, ND 0.6"));
	TestTrue(TEXT("A stop the lens cannot do is said"), HasWarning(Lines[2], TEXT("This lens cannot be set to T1.4. It goes to T2.")));
	TestEqual(TEXT("What the lens step already warned of is not said again"), Lines[2].Warnings.Num(), 1);
	TestEqual(TEXT("The support"), *Lines[3].Text.ToString(), TEXT("Support: Telescoping crane, 30 ft"));
	TestEqual(TEXT("The move, in the crane's own axes"), *Lines[4].Text.ToString(), TEXT("Move over 8 s: Boom +20°, Telescope +1.5 m, Tilt -10°"));
	TestTrue(TEXT("An axis the crane does not have is left out, and said"), HasWarning(Lines[4], TEXT("(Crane) has no Forward axis")));
	TestEqual(TEXT("Nothing else is wrong with a move the crane can make"), Lines[4].Warnings.Num(), 1);
	TestEqual(TEXT("The second move"), *Lines[5].Text.ToString(), TEXT("Move over 1 s: Swing +90°"));
	TestTrue(TEXT("A move the crane cannot make in the time is flagged, with the time it needs"), !Lines[5].Warnings.IsEmpty() && Lines[5].Warnings.Last().ToString().Contains(TEXT(" s")));
	TestEqual(TEXT("The focus"), *Lines[6].Text.ToString(), TEXT("Focus: follow Anna"));
	TestTrue(TEXT("Warnings do not stop a plan"), CanApply(Lines));
	TestTrue(TEXT("Reading a plan changes nothing"), Component->Package.BodyId == PackageBefore.BodyId && UEcine::GripEditing::FindRig(Camera) == nullptr);

	FText Error;
	if (!TestTrue(*FString::Printf(TEXT("The plan is carried out (%s)"), *Error.ToString()), Apply(Plan, Camera, Error)))
	{
		return false;
	}
	const FUEcineCameraPackage& Package = Component->Package;
	TestEqual(TEXT("The body is on"), *Package.BodyId, TEXT("sony-venice-2-8k"));
	TestEqual(TEXT("In the format asked for"), *Package.FormatId, TEXT("8.6k-17x9"));
	TestEqual(TEXT("The kit is on"), *Package.LensKitId, TEXT("cooke-s4i"));
	TestEqual(TEXT("With its nearest lens"), Package.FocalLengthMm, 32.0f);
	TestEqual(TEXT("At the widest the lens goes"), Package.TStop, 2.0f);
	TestEqual(TEXT("The ISO is set"), Package.Iso, 1600);
	TestEqual(TEXT("The shutter is set"), Package.ShutterAngleDeg, 172.8f);
	TestEqual(TEXT("Two stops of ND are in"), Package.NdDensity, 0.6f);

	AUEcineRig* Rig = UEcine::GripEditing::FindRig(Camera);
	if (!TestNotNull(TEXT("The camera is on a support"), Rig))
	{
		return false;
	}
	TestTrue(TEXT("A crane"), Rig->GetSpec().Type == EUEcineRigType::Crane && Rig->GetSpec().PresetId == TEXT("crane-30ft"));
	if (TestEqual(TEXT("With the start and both moves marked"), Rig->Marks.Num(), 3))
	{
		const FUEcineRigPose& Start = Rig->Marks[0].Pose;
		const FUEcineRigPose& Second = Rig->Marks[1].Pose;
		TestEqual(TEXT("The boom rises 20 degrees"), Second.Boom - Start.Boom, 20.0, 1.e-3);
		TestEqual(TEXT("The arm reaches out 1.5 m"), Second.Extension - Start.Extension, 150.0, 1.e-3);
		TestEqual(TEXT("The head tilts down 10 degrees"), Second.Tilt - Start.Tilt, -10.0, 1.e-3);
		TestEqual(TEXT("The crane has no track to go along"), Second.Travel, 0.0);
		TestEqual(TEXT("The first move takes 8 s"), Rig->Marks[1].Arrival.DurationSeconds, 8.0);
		TestEqual(TEXT("The second swings on from there"), Rig->Marks[2].Pose.Swing - Second.Swing, 90.0, 1.e-3);
		TestEqual(TEXT("And keeps the reach of the first"), Rig->Marks[2].Pose.Extension, Second.Extension, 1.e-3);
		TestEqual(TEXT("In the second it was given"), Rig->Marks[2].Arrival.DurationSeconds, 1.0);
		TestEqual(TEXT("The rig is left on its first mark"), Rig->GetPose().Boom, Start.Boom, 1.e-3);
	}
	TestTrue(TEXT("So the camera is where the shot starts"), Camera->GetActorLocation().Equals(CameraStart.GetLocation(), 0.05));
	TestTrue(TEXT("The focus follows Anna, found whatever the case of her name"), UEcine::FocusEditing::GetFollowedActor(Camera) == Anna);

	// One undo takes the whole plan back; one redo carries it out again.
	TestTrue(TEXT("The plan can be undone"), GEditor->UndoTransaction());
	TestNull(TEXT("After one undo the camera is on no support"), UEcine::GripEditing::FindRig(Camera));
	TestTrue(TEXT("The body is the one it had"), Component->Package.BodyId == PackageBefore.BodyId && Component->Package.FormatId == PackageBefore.FormatId);
	TestTrue(TEXT("So is the lens"), Component->Package.LensKitId == PackageBefore.LensKitId && Component->Package.FocalLengthMm == PackageBefore.FocalLengthMm);
	TestTrue(TEXT("And the exposure"), Component->Package.Iso == PackageBefore.Iso && Component->Package.TStop == PackageBefore.TStop && Component->Package.NdDensity == PackageBefore.NdDensity);
	TestNull(TEXT("The focus follows nothing"), UEcine::FocusEditing::GetFollowedActor(Camera));
	TestTrue(TEXT("The plan can be redone"), GEditor->RedoTransaction());
	Rig = UEcine::GripEditing::FindRig(Camera);
	if (!TestNotNull(TEXT("After redo the camera is on the crane again"), Rig) || !TestEqual(TEXT("With its marks"), Rig->Marks.Num(), 3))
	{
		return false;
	}

	// A second plan for the camera as it now stands: keep the move as a shot.
	const FUEcineShotPlan Keep = ReadAssistantTestPlan(*this, TEXT(R"({ "summary": "Kept as a shot.", "steps": [
		{ "step": "focus_at", "distanceM": 4.5 },
		{ "step": "add_shot", "scene": "12", "size": "close_up", "move": "Crane up", "notes": "As she stands" },
		{ "step": "bake_move" } ] })"));
	const TArray<FPlanLine> KeepLines = Review(Keep, Camera);
	if (TestEqual(TEXT("Every step has its line"), KeepLines.Num(), 3))
	{
		TestEqual(TEXT("A focus distance, in the user's units"), *KeepLines[0].Text.ToString(), TEXT("Focus: 4.5 m"));
		TestEqual(TEXT("The shot's line of the shot list"), *KeepLines[1].Text.ToString(), TEXT("New shot: scene 12, close-up, Crane up"));
		TestEqual(TEXT("The bake"), *KeepLines[2].Text.ToString(), TEXT("Bake the move into the sequence"));
		TestTrue(TEXT("The rig's marks from the plan before are a move to bake"), KeepLines[2].Problem.IsEmpty());
	}
	const bool bKept = Apply(Keep, Camera, Error);
	if (ULevelSequence* Open = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence())
	{
		CreatedSequences.Add(Open);
	}
	const TArray<UEcine::ShotEditing::FShot> Shots = UEcine::ShotEditing::GetShots();
	for (const UEcine::ShotEditing::FShot& Shot : Shots)
	{
		CreatedSequences.AddUnique(Shot.Sequence);
	}
	if (!TestTrue(*FString::Printf(TEXT("The second plan is carried out (%s)"), *Error.ToString()), bKept) || !TestEqual(TEXT("There is one shot"), Shots.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("The focus is set by hand"), UEcine::FocusEditing::GetFocusDistance(Camera), 450.0f, 0.5f);
	TestNull(TEXT("And follows nothing"), UEcine::FocusEditing::GetFollowedActor(Camera));
	ULevelSequence* ShotSequence = Shots[0].Sequence;
	TestTrue(TEXT("The shot is seen through the camera"), UEcine::ShotEditing::FindShotCamera(Shots[0]) == Camera);
	TestEqual(TEXT("Its scene is filled in"), *UEcine::ShotEditing::GetScene(ShotSequence), TEXT("12"));
	TestTrue(TEXT("Its size"), UEcine::ShotEditing::GetSize(ShotSequence) == EUEcineShotSize::CloseUp);
	TestEqual(TEXT("Its move"), *UEcine::ShotEditing::GetMove(ShotSequence), TEXT("Crane up"));
	TestEqual(TEXT("And its notes"), *UEcine::ShotEditing::GetNotes(ShotSequence), TEXT("As she stands"));
	const FMovieSceneBinding* RigBinding = ShotSequence->GetMovieScene()->FindBinding(UEcine::SequenceUtils::FindBinding(ShotSequence, Rig));
	TestTrue(TEXT("The move is baked into the shot, on the rig's axes"), RigBinding != nullptr && RigBinding->GetTracks().Num() >= 3);

	// The set is told as it now stands.
	TSharedPtr<FJsonObject> Set;
	if (TestTrue(TEXT("The set's description is still JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(DescribeSet(Camera)), Set) && Set.IsValid()))
	{
		const TSharedPtr<FJsonObject>* Support = nullptr;
		if (TestTrue(TEXT("The support is told"), Set->TryGetObjectField(TEXT("support"), Support)))
		{
			TestEqual(TEXT("By its id"), *(*Support)->GetStringField(TEXT("id")), TEXT("crane-30ft"));
			TestEqual(TEXT("With its marks"), (*Support)->GetIntegerField(TEXT("marks")), 3);
			TestTrue(TEXT("And where its axes are, in the plan's own names"), (*Support)->GetObjectField(TEXT("position"))->HasField(TEXT("extensionM")));
		}
		const TSharedPtr<FJsonObject>* Sequence = nullptr;
		if (TestTrue(TEXT("The open sequence is told"), Set->TryGetObjectField(TEXT("sequence"), Sequence)))
		{
			TestEqual(TEXT("With its shots"), (*Sequence)->GetArrayField(TEXT("shots")).Num(), 1);
		}
		TestEqual(TEXT("The rig is not a subject"), Set->GetArrayField(TEXT("subjects")).Num(), 2);
	}
	return true;
}

#endif
