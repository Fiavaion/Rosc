// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "CineCameraActor.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "LevelEditor.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "LevelEditorSubsystem.h"
#include "LevelEditorViewport.h"
#include "SLevelViewport.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "ObjectTools.h"
#include "SRoscPanel.h"
#include "Tests/AutomationCommon.h"
#include "RoscCameraEditing.h"
#include "RoscCameraPackageComponent.h"
#include "Tests/RoscDeliverChoicesBefore.h"
#include "Tests/RoscWidgetTestUtils.h"
#include "RoscDeliverEditing.h"
#include "RoscEditorSettings.h"
#include "RoscFocusEditing.h"
#include "RoscGripEditing.h"
#include "RoscMonitorSubsystem.h"
#include "RoscRig.h"
#include "RoscShotEditing.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Every widget under another that takes keyboard focus, is shown and can be used. */
	void CollectKeyboardStops(const TSharedRef<SWidget>& Widget, TArray<TSharedRef<SWidget>>& OutStops)
	{
		if (!Widget->GetVisibility().IsVisible() || !Widget->IsEnabled())
		{
			return;
		}
		if (Widget->SupportsKeyboardFocus())
		{
			OutStops.Add(Widget);
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			CollectKeyboardStops(Children->GetChildAt(Index), OutStops);
		}
	}

	bool IsInside(const SWidget* Widget, const SWidget& Ancestor)
	{
		for (; Widget != nullptr; Widget = Widget->GetParentWidget().Get())
		{
			if (Widget == &Ancestor)
			{
				return true;
			}
		}
		return false;
	}

	/** True when a control has a tooltip that names it, its own or that of the control it is the inside of. */
	bool HasName(SWidget& Control, const SWidget& Panel)
	{
		for (SWidget* Widget = &Control; Widget != nullptr && Widget != &Panel; Widget = Widget->GetParentWidget().Get())
		{
			const TSharedPtr<IToolTip> ToolTip = Widget->GetToolTip();
			if (ToolTip.IsValid() && !ToolTip->IsEmpty())
			{
				return true;
			}
		}
		return false;
	}

	/** A control by its kind and the line of code that made it. */
	FString DescribeControl(const SWidget& Control)
	{
		return Control.ToString();
	}

	/**
	 * The control a screen reader would call by this name, or null. A drop-down's focus is taken by
	 * the button inside it, so a stop inside a widget of that name counts too.
	 */
	TSharedPtr<SWidget> FindControlCalled(SRoscPanel& Panel, const TCHAR* AccessibleName)
	{
		TArray<TSharedRef<SWidget>> Stops;
		CollectKeyboardStops(Panel.AsShared(), Stops);
		for (const TSharedRef<SWidget>& Stop : Stops)
		{
			for (SWidget* Widget = &Stop.Get(); Widget != nullptr && Widget != &Panel; Widget = Widget->GetParentWidget().Get())
			{
				if (Widget->GetAccessibleText().ToString() == AccessibleName)
				{
					return Stop;
				}
			}
		}
		return nullptr;
	}

	/** The words on the button that has the keyboard focus, or what has it when that is no button with words. */
	FString DescribeFocus(const SRoscPanel& Panel)
	{
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (!Focused.IsValid() || !IsInside(Focused.Get(), Panel))
		{
			return TEXT("nothing in the panel");
		}
		FChildren* Children = Focused->GetChildren();
		for (TSharedPtr<SWidget> Inside = Children->Num() > 0 ? Children->GetChildAt(0).ToSharedPtr() : nullptr; Inside.IsValid();
			Inside = Inside->GetChildren()->Num() > 0 ? Inside->GetChildren()->GetChildAt(0).ToSharedPtr() : nullptr)
		{
			if (Inside->GetType() == FName("STextBlock"))
			{
				return StaticCastSharedPtr<STextBlock>(Inside)->GetText().ToString();
			}
		}
		return Focused->ToString();
	}

	/** Presses a key and lets it go, on whatever has the keyboard focus. */
	void PressKey(const FKey& Key)
	{
		const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
		FSlateApplication::Get().ProcessKeyDownEvent(Event);
		FSlateApplication::Get().ProcessKeyUpEvent(Event);
	}

	/** Puts the focus on a control by its name and presses a key on it (Enter unless told), as a keyboard user would. */
	bool PressControlCalled(SRoscPanel& Panel, const TCHAR* AccessibleName, const FKey& Key = EKeys::Enter)
	{
		const TSharedPtr<SWidget> Control = FindControlCalled(Panel, AccessibleName);
		if (!Control.IsValid())
		{
			return false;
		}
		FSlateApplication::Get().SetKeyboardFocus(Control, EFocusCause::SetDirectly);
		PressKey(Key);
		return true;
	}

	/**
	 * Puts the focus in a text box by its name, types over what is in it and presses a key to finish
	 * (Enter unless told), as a keyboard user would.
	 */
	bool TypeIntoControlCalled(SRoscPanel& Panel, const TCHAR* AccessibleName, const FString& Typed, const FKey& Finish = EKeys::Enter)
	{
		const TSharedPtr<SWidget> Control = FindControlCalled(Panel, AccessibleName);
		if (!Control.IsValid())
		{
			return false;
		}
		FSlateApplication& Slate = FSlateApplication::Get();
		Slate.SetKeyboardFocus(Control, EFocusCause::SetDirectly);
		const FKeyEvent SelectAll(EKeys::A, FModifierKeysState(false, false, true, false, false, false, false, false, false), 0, false, 0, 0);
		Slate.ProcessKeyDownEvent(SelectAll);
		Slate.ProcessKeyUpEvent(SelectAll);
		for (const TCHAR Character : Typed)
		{
			Slate.ProcessKeyCharEvent(FCharacterEvent(Character, FModifierKeysState(), 0, false));
		}
		PressKey(Finish);
		return true;
	}

	/** The view of the level editor's active viewport, the one Look Through takes over. */
	FLevelEditorViewportClient* GetActiveViewClient()
	{
		const TSharedPtr<ILevelEditor> Editor = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor").GetFirstLevelEditor();
		const TSharedPtr<SLevelViewport> Viewport = Editor.IsValid() ? Editor->GetActiveViewportInterface() : nullptr;
		return Viewport.IsValid() ? &Viewport->GetLevelViewportClient() : nullptr;
	}

	const TCHAR* GetDepartmentTestName(ERoscDepartment Department)
	{
		switch (Department)
		{
		case ERoscDepartment::Camera: return TEXT("Camera");
		case ERoscDepartment::Grip: return TEXT("Grip");
		case ERoscDepartment::Focus: return TEXT("Focus");
		case ERoscDepartment::Monitor: return TEXT("Monitor");
		case ERoscDepartment::Shots: return TEXT("Shots");
		case ERoscDepartment::Deliver: return TEXT("Deliver");
		default: return TEXT("AI");
		}
	}

	/**
	 * Presses Tab through a department of the panel in a window, from its first control to its
	 * last, and checks what a keyboard user needs: every control is reached, in the order they are
	 * laid out, none holds the focus, Shift+Tab goes back, and every control has words that name it.
	 */
	void WalkDepartmentByKeyboard(FAutomationTestBase& Test, SRoscPanel& Panel, ERoscDepartment Department)
	{
		const FString Name = GetDepartmentTestName(Department);
		FSlateApplication& Slate = FSlateApplication::Get();

		TArray<TSharedRef<SWidget>> Stops;
		CollectKeyboardStops(Panel.AsShared(), Stops);
		// A control with another inside it that takes the focus (a text box and its text) hands the focus on.
		TArray<TSharedRef<SWidget>> Controls;
		for (const TSharedRef<SWidget>& Stop : Stops)
		{
			TArray<TSharedRef<SWidget>> Inside;
			CollectKeyboardStops(Stop, Inside);
			if (Inside.Num() == 1)
			{
				Controls.Add(Stop);
			}
		}
		if (!Test.TestTrue(FString::Printf(TEXT("%s: the tab has controls"), *Name), Controls.Num() > 3))
		{
			return;
		}

		Slate.SetKeyboardFocus(Controls[0], EFocusCause::SetDirectly);
		TSharedPtr<SWidget> Focused = Slate.GetKeyboardFocusedWidget();
		if (!Test.TestTrue(FString::Printf(TEXT("%s: the first control takes the focus"), *Name), Focused.IsValid() && IsInside(Focused.Get(), *Controls[0])))
		{
			return;
		}

		// Tab goes from each control to the one after it, in the order they are laid out. A number
		// box hands the focus to the text inside it, so a control counts as reached from within.
		const FKeyEvent Tab(EKeys::Tab, FModifierKeysState(), 0, false, 0, 0);
		const FKeyEvent ShiftTab(EKeys::Tab, FModifierKeysState(true, false, false, false, false, false, false, false, false), 0, false, 0, 0);
		for (int32 Index = 1; Index < Controls.Num(); ++Index)
		{
			Slate.ProcessKeyDownEvent(Tab);
			Slate.ProcessKeyUpEvent(Tab);
			const TSharedPtr<SWidget> Next = Slate.GetKeyboardFocusedWidget();
			if (Next == Focused)
			{
				Test.AddError(FString::Printf(TEXT("%s: Tab does not leave %s (control %d of %d)"), *Name, *DescribeControl(*Controls[Index - 1]), Index, Controls.Num()));
				return;
			}
			if (!Next.IsValid() || !IsInside(Next.Get(), *Controls[Index]))
			{
				Test.AddError(FString::Printf(TEXT("%s: Tab from %s goes to %s, not to %s"), *Name, *DescribeControl(*Controls[Index - 1]),
					Next.IsValid() ? *DescribeControl(*Next) : TEXT("nothing"), *DescribeControl(*Controls[Index])));
				return;
			}
			Focused = Next;
		}

		// And back: Shift+Tab from the last control goes to the one before it.
		Slate.ProcessKeyDownEvent(ShiftTab);
		Slate.ProcessKeyUpEvent(ShiftTab);
		const TSharedPtr<SWidget> Back = Slate.GetKeyboardFocusedWidget();
		Test.TestTrue(FString::Printf(TEXT("%s: Shift+Tab from the last control goes to the one before"), *Name),
			Back.IsValid() && IsInside(Back.Get(), *Controls[Controls.Num() - 2]));

		int32 Unnamed = 0;
		for (const TSharedRef<SWidget>& Control : Controls)
		{
			if (!HasName(*Control, Panel))
			{
				++Unnamed;
				Test.AddError(FString::Printf(TEXT("%s: %s has no tooltip to name it"), *Name, *DescribeControl(*Control)));
			}
		}
		Test.AddInfo(FString::Printf(TEXT("%s: Tab walks all %d controls in order; %d without a name"), *Name, Controls.Num(), Unnamed));
	}
}

/**
 * The keyboard pass over the panel: every department is shown in a real window at the narrowest
 * width and walked with the Tab key. The camera is on a crane with marks and has focus marks, so
 * the rows that come and go are there to be walked.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscKeyboardTest, "Rosc.Editor.Keyboard", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscKeyboardTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Skipped: walking the panel in a window needs a renderer (tools/test.ps1 -Rendering)."));
		return true;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ACineCameraActor* Camera = Rosc::CameraEditing::SpawnCamera(World, FTransform(FVector(0.0, 0.0, 160.0)));
	ARoscRig* Rig = Rosc::GripEditing::PutCameraOnRig(Camera, *Rosc::Rig::FindPreset(TEXT("crane-30ft")));
	Rosc::GripEditing::AddMark(Rig);
	Rosc::GripEditing::EditRig(Rig, FText::FromString(TEXT("Swing")), [](ARoscRig& EditedRig)
	{
		FRoscRigPose Pose = EditedRig.GetPose();
		Pose.Swing += 20.0;
		EditedRig.SetPose(Pose);
	});
	Rosc::GripEditing::AddMark(Rig);
	Rosc::FocusEditing::AddFocusMark(Camera);
	Rosc::FocusEditing::AddFocusMark(Camera);
	GEditor->SelectNone(false, true, false);
	GEditor->SelectActor(Camera, true, true);

	struct FWalk
	{
		TSharedPtr<SWindow> Window;
		TSharedPtr<SRoscPanel> Panel;
	};
	const TSharedRef<FWalk> Walk = MakeShared<FWalk>();
	Walk->Window = SNew(SWindow)
		.Title(FText::FromString(TEXT("Rosc keyboard test")))
		.ClientSize(FVector2D(320.0, 900.0))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SAssignNew(Walk->Panel, SRoscPanel)
		];
	FSlateApplication::Get().AddWindow(Walk->Window.ToSharedRef(), true);

	for (const ERoscDepartment Department : { ERoscDepartment::Camera, ERoscDepartment::Grip, ERoscDepartment::Focus, ERoscDepartment::Monitor,
		ERoscDepartment::Shots, ERoscDepartment::Deliver, ERoscDepartment::AI })
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Walk, Department]
		{
			Walk->Panel->ShowDepartment(Department);
			return true;
		}));
		// The tab is laid out by the frames that pass before it is walked.
		ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Walk, Department]
		{
			WalkDepartmentByKeyboard(*this, *Walk->Panel, Department);
		}, 0.3f));
	}

	// Removing a row takes its buttons with it. The focus goes to the row that takes its place,
	// and to the button that adds a row when none is left: it does not fall out of the panel.
	const TWeakObjectPtr<ARoscRig> WeakRig = Rig;
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Walk]
	{
		Walk->Panel->ShowDepartment(ERoscDepartment::Grip);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Walk]
	{
		TestTrue(TEXT("Enter is pressed on Remove mark 1"), PressControlCalled(*Walk->Panel, TEXT("Remove mark 1")));
	}, 0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Walk, WeakRig]
	{
		TestEqual(TEXT("One mark is left"), WeakRig.IsValid() ? WeakRig->Marks.Num() : -1, 1);
		TestEqual(TEXT("With a mark removed, the focus is on the mark that took its place"), DescribeFocus(*Walk->Panel), FString(TEXT("Mark 1")));
		TestTrue(TEXT("Enter is pressed on Remove mark 1 again"), PressControlCalled(*Walk->Panel, TEXT("Remove mark 1")));
	}, 0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Walk, WeakRig]
	{
		TestEqual(TEXT("No mark is left"), WeakRig.IsValid() ? WeakRig->Marks.Num() : -1, 0);
		TestEqual(TEXT("With the last mark removed, the focus is on the button that marks a position"), DescribeFocus(*Walk->Panel), FString(TEXT("Mark Position")));
	}, 0.3f));

	// A file name typed without {shot} is refused where it was typed: it stays in the box with the
	// reason under it, and files keep the name they had until it is put right.
	const TSharedRef<FRoscDeliverChoicesBefore> ChoicesBefore = MakeShared<FRoscDeliverChoicesBefore>();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Walk]
	{
		Rosc::DeliverEditing::EditChoices([](URoscEditorSettings& Choices) { Choices.DeliverNamePattern = Rosc::Deliver::DefaultNamePattern; });
		Walk->Panel->ShowDepartment(ERoscDepartment::Deliver);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Walk]
	{
		TestTrue(TEXT("A name without the shot is typed into the Name box"), TypeIntoControlCalled(*Walk->Panel, TEXT("Name"), TEXT("{sequence}_{intent}")));
	}, 0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Walk]
	{
		TestEqual(TEXT("Files keep the name they had"), GetDefault<URoscEditorSettings>()->DeliverNamePattern, FString(Rosc::Deliver::DefaultNamePattern));
		const TSharedPtr<SWidget> NameBox = FindControlCalled(*Walk->Panel, TEXT("Name"));
		TestEqual(TEXT("The box keeps what was typed"), NameBox.IsValid() ? StaticCastSharedPtr<SEditableTextBox>(NameBox)->GetText().ToString() : FString(), FString(TEXT("{sequence}_{intent}")));
		TestTrue(TEXT("The tab says why it was refused"), Rosc::WidgetTests::ShowsText(Walk->Panel.ToSharedRef(), TEXT("needs {shot}")));
		TestTrue(TEXT("A name with the shot is typed in"), TypeIntoControlCalled(*Walk->Panel, TEXT("Name"), TEXT("{sequence}_{intent}_{shot}")));
	}, 0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Walk, ChoicesBefore]
	{
		TestEqual(TEXT("Files are named by it"), GetDefault<URoscEditorSettings>()->DeliverNamePattern, FString(TEXT("{sequence}_{intent}_{shot}")));
		TestFalse(TEXT("The reason is gone"), Rosc::WidgetTests::ShowsText(Walk->Panel.ToSharedRef(), TEXT("needs {shot}")));
		ChoicesBefore->PutBack();
	}, 0.3f));

	// Look Through and back: the view returns to where it was, not left where the camera is.
	const TCHAR* const LookThroughName = TEXT("Look through this camera in the viewport. Moving the viewport then moves the camera. On a support, turning the view pans and tilts the head, and the camera stays on its mount.");
	const FVector OwnViewLocation(-600.0, 250.0, 300.0);
	const FRotator OwnViewRotation(-10.0, 30.0, 0.0);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Walk, LookThroughName, OwnViewLocation, OwnViewRotation]
	{
		if (FLevelEditorViewportClient* View = GetActiveViewClient(); TestNotNull(TEXT("The level editor has an active view"), View))
		{
			View->SetViewLocation(OwnViewLocation);
			View->SetViewRotation(OwnViewRotation);
		}
		TestTrue(TEXT("Enter is pressed on Look Through"), PressControlCalled(*Walk->Panel, LookThroughName));
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Walk, LookThroughName, WeakCamera = TWeakObjectPtr<ACineCameraActor>(Camera)]
	{
		ULevelEditorSubsystem* LevelEditor = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
		TestTrue(TEXT("The view looks through the camera"), WeakCamera.IsValid() && LevelEditor->GetPilotLevelActor() == WeakCamera.Get());
		TestTrue(TEXT("Enter is pressed on Look Through again"), PressControlCalled(*Walk->Panel, LookThroughName));
	}, 0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, OwnViewLocation, OwnViewRotation]
	{
		TestNull(TEXT("The view no longer looks through the camera"), GEditor->GetEditorSubsystem<ULevelEditorSubsystem>()->GetPilotLevelActor());
		if (const FLevelEditorViewportClient* View = GetActiveViewClient())
		{
			AddInfo(FString::Printf(TEXT("The view is back at %s looking %s"), *View->GetViewLocation().ToString(), *View->GetViewRotation().ToString()));
			TestTrue(TEXT("The view is back where it was"), View->GetViewLocation().Equals(OwnViewLocation, 0.01));
			TestTrue(TEXT("And looks where it looked"), View->GetViewRotation().Equals(OwnViewRotation, 0.01));
		}
	}, 0.3f));

	// Space works a button as Enter does: a monitor tool goes on and off, and Add Token opens its
	// menu, which Escape closes.
	const TCHAR* const FalseColourName = TEXT("False colour: show the picture in exposure bands, so crushed blacks, middle grey, a stop over grey and clipping stand out. The bands are listed here while it is on.");
	const TSharedRef<FRoscMonitorSettings> MonitorBefore = MakeShared<FRoscMonitorSettings>(URoscMonitorSubsystem::Get()->GetSettings());
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Walk]
	{
		Walk->Panel->ShowDepartment(ERoscDepartment::Monitor);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Walk, FalseColourName, MonitorBefore]
	{
		TestTrue(TEXT("Space is pressed on False colour"), PressControlCalled(*Walk->Panel, FalseColourName, EKeys::SpaceBar));
		TestEqual(TEXT("Space turns false colour over"), URoscMonitorSubsystem::Get()->GetSettings().bFalseColour, !MonitorBefore->bFalseColour);
		TestTrue(TEXT("Space is pressed on False colour again"), PressControlCalled(*Walk->Panel, FalseColourName, EKeys::SpaceBar));
		TestEqual(TEXT("And back"), URoscMonitorSubsystem::Get()->GetSettings().bFalseColour, MonitorBefore->bFalseColour);
		URoscMonitorSubsystem::Get()->SetSettings(*MonitorBefore);
	}, 0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Walk]
	{
		Walk->Panel->ShowDepartment(ERoscDepartment::Deliver);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Walk]
	{
		TestTrue(TEXT("Space is pressed on Add token"), PressControlCalled(*Walk->Panel, TEXT("Add token"), EKeys::SpaceBar));
		TestTrue(TEXT("Space opens the token menu"), FSlateApplication::Get().AnyMenusVisible());
		PressKey(EKeys::Escape);
		TestFalse(TEXT("Escape closes it"), FSlateApplication::Get().AnyMenusVisible());
		FSlateApplication::Get().DismissAllMenus();
	}, 0.3f));

	// A drop-down by keyboard: Space opens it and Escape closes it with the choice as it was. The
	// arrow keys take the focus to the list's search box, which the first Escape leaves; the second
	// closes the list. (Choosing an entry by keyboard is checked by hand: in a test the engine's
	// menus do not take arrow keys from the search box.)
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Walk]
	{
		Walk->Panel->ShowDepartment(ERoscDepartment::Camera);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Walk, WeakCamera = TWeakObjectPtr<ACineCameraActor>(Camera)]
	{
		const URoscCameraPackageComponent* Package = WeakCamera.IsValid() ? URoscCameraPackageComponent::Find(WeakCamera.Get()) : nullptr;
		if (!TestNotNull(TEXT("The camera has its package"), Package))
		{
			return;
		}
		const FString BodyBefore = Package->Package.BodyId;
		TestTrue(TEXT("Space is pressed on Body"), PressControlCalled(*Walk->Panel, TEXT("Body"), EKeys::SpaceBar));
		TestTrue(TEXT("Space opens the list of bodies"), FSlateApplication::Get().AnyMenusVisible());
		PressKey(EKeys::Down);
		TestTrue(TEXT("The list stays open under the arrow keys"), FSlateApplication::Get().AnyMenusVisible());
		PressKey(EKeys::Escape);
		PressKey(EKeys::Escape);
		TestFalse(TEXT("Escape closes the list"), FSlateApplication::Get().AnyMenusVisible());
		TestEqual(TEXT("And leaves the body as it was"), Package->Package.BodyId, BodyBefore);
		FSlateApplication::Get().DismissAllMenus();

		// Up and Down step a ladder value with the focus anywhere on it: on either of its buttons, as
		// a click leaves it, or on the value.
		const float TStopBefore = Package->Package.TStop;
		TestTrue(TEXT("Up is pressed on Close the iris"), PressControlCalled(*Walk->Panel, TEXT("Close the iris a third of a stop"), EKeys::Up));
		TestTrue(TEXT("Up on the T-stop's button closes the iris"), Package->Package.TStop > TStopBefore);
		TestTrue(TEXT("And leaves the focus on the button"),
			FSlateApplication::Get().GetKeyboardFocusedWidget() == FindControlCalled(*Walk->Panel, TEXT("Close the iris a third of a stop")));
		TestTrue(TEXT("Down is pressed on Open the iris"), PressControlCalled(*Walk->Panel, TEXT("Open the iris a third of a stop"), EKeys::Down));
		TestEqual(TEXT("Down on the other button opens it again"), Package->Package.TStop, TStopBefore);
		TestTrue(TEXT("Up is pressed on the T-stop's value"), PressControlCalled(*Walk->Panel, TEXT("T-stop"), EKeys::Up));
		TestTrue(TEXT("Up on the value closes the iris"), Package->Package.TStop > TStopBefore);
		PressKey(EKeys::Down);
		TestEqual(TEXT("And Down opens it again"), Package->Package.TStop, TStopBefore);
	}, 0.3f));

	// The shot's Scene, Move and Notes by keyboard: Enter keeps what is typed, Escape gives it up,
	// one undo takes it back, and Tab leaves the Notes box with what was typed kept.
	struct FShotSequences
	{
		TWeakObjectPtr<ULevelSequence> Open;
		TWeakObjectPtr<ULevelSequence> Shot;
		ULevelSequence* Get() const { return Shot.Get(); }
	};
	const TSharedRef<FShotSequences> ShotSequence = MakeShared<FShotSequences>();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Walk, ShotSequence, WeakCamera = TWeakObjectPtr<ACineCameraActor>(Camera)]
	{
		FText Error;
		if (TestTrue(TEXT("A shot is added"), WeakCamera.IsValid() && Rosc::ShotEditing::AddShot(WeakCamera.Get(), Error)))
		{
			ShotSequence->Open = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
			const TArray<Rosc::ShotEditing::FShot> Shots = Rosc::ShotEditing::GetShots();
			ShotSequence->Shot = Shots.Num() == 1 ? Shots[0].Sequence : nullptr;
		}
		Walk->Panel->ShowDepartment(ERoscDepartment::Shots);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Walk]
	{
		TestTrue(TEXT("12A is typed into Scene and Enter pressed"), TypeIntoControlCalled(*Walk->Panel, TEXT("Scene"), TEXT("12A")));
		TestTrue(TEXT("Push in is typed into Move and Enter pressed"), TypeIntoControlCalled(*Walk->Panel, TEXT("Move"), TEXT("Push in")));
		TestTrue(TEXT("Rain is typed into Notes and Tab pressed"), TypeIntoControlCalled(*Walk->Panel, TEXT("Notes"), TEXT("Rain"), EKeys::Tab));
	}, 0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Walk, ShotSequence]
	{
		const ULevelSequence* Sequence = ShotSequence->Get();
		if (!TestNotNull(TEXT("The shot's sequence is open"), Sequence))
		{
			return;
		}
		TestEqual(TEXT("Enter keeps the scene"), Rosc::ShotEditing::GetScene(Sequence), FString(TEXT("12A")));
		TestEqual(TEXT("Enter keeps the move"), Rosc::ShotEditing::GetMove(Sequence), FString(TEXT("Push in")));
		TestEqual(TEXT("Tab leaves the Notes box and keeps the notes, with no tab typed"), Rosc::ShotEditing::GetNotes(Sequence), FString(TEXT("Rain")));
		TestFalse(TEXT("The focus has left the Notes box"), DescribeFocus(*Walk->Panel).Contains(TEXT("Notes")));
		TestTrue(TEXT("99 is typed into Scene and Escape pressed"), TypeIntoControlCalled(*Walk->Panel, TEXT("Scene"), TEXT("99"), EKeys::Escape));
	}, 0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, ShotSequence]
	{
		const ULevelSequence* Sequence = ShotSequence->Get();
		TestEqual(TEXT("Escape gives up what was typed"), Sequence != nullptr ? Rosc::ShotEditing::GetScene(Sequence) : FString(), FString(TEXT("12A")));
		GEditor->UndoTransaction();
		TestEqual(TEXT("One undo takes back the notes"), Sequence != nullptr ? Rosc::ShotEditing::GetNotes(Sequence) : FString(TEXT("?")), FString());
		TestEqual(TEXT("And only the notes"), Sequence != nullptr ? Rosc::ShotEditing::GetMove(Sequence) : FString(), FString(TEXT("Push in")));
	}, 0.3f));

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Walk, ShotSequence, WeakCamera = TWeakObjectPtr<ACineCameraActor>(Camera)]
	{
		Walk->Window->RequestDestroyWindow();
		GEditor->SelectNone(false, true, false);
		if (ACineCameraActor* CameraToRemove = WeakCamera.Get())
		{
			Rosc::GripEditing::TakeCameraOffRig(CameraToRemove);
			CameraToRemove->GetWorld()->EditorDestroyActor(CameraToRemove, false);
		}
		GEditor->ResetTransaction(FText::FromString(TEXT("Rosc keyboard test")));
		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
		for (ULevelSequence* Sequence : { ShotSequence->Open.Get(), ShotSequence->Shot.Get() })
		{
			if (Sequence != nullptr)
			{
				TestTrue(TEXT("The test's sequence asset is removed"), ObjectTools::DeleteSingleObject(Sequence, false));
			}
		}
		return true;
	}));
	return true;
}

#endif
