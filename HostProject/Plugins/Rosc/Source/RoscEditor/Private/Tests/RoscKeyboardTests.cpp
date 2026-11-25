// Copyright 2026 Fiavaion. All rights reserved.

#include "CineCameraActor.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "SRoscPanel.h"
#include "Tests/AutomationCommon.h"
#include "RoscCameraEditing.h"
#include "RoscDeliverChoicesBefore.h"
#include "RoscDeliverEditing.h"
#include "RoscEditorSettings.h"
#include "RoscFocusEditing.h"
#include "RoscGripEditing.h"
#include "RoscRig.h"
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

	/** The control a screen reader would call by this name, or null. */
	TSharedPtr<SWidget> FindControlCalled(SRoscPanel& Panel, const TCHAR* AccessibleName)
	{
		TArray<TSharedRef<SWidget>> Stops;
		CollectKeyboardStops(Panel.AsShared(), Stops);
		for (const TSharedRef<SWidget>& Stop : Stops)
		{
			if (Stop->GetAccessibleText().ToString() == AccessibleName)
			{
				return Stop;
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

	/** Puts the focus on a control by its name and presses Enter on it, as a keyboard user would. */
	bool PressControlCalled(SRoscPanel& Panel, const TCHAR* AccessibleName)
	{
		const TSharedPtr<SWidget> Control = FindControlCalled(Panel, AccessibleName);
		if (!Control.IsValid())
		{
			return false;
		}
		FSlateApplication& Slate = FSlateApplication::Get();
		Slate.SetKeyboardFocus(Control, EFocusCause::SetDirectly);
		const FKeyEvent Enter(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
		Slate.ProcessKeyDownEvent(Enter);
		Slate.ProcessKeyUpEvent(Enter);
		return true;
	}

	/** Puts the focus in a text box by its name, types over what is in it and presses Enter, as a keyboard user would. */
	bool TypeIntoControlCalled(SRoscPanel& Panel, const TCHAR* AccessibleName, const FString& Typed)
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
		const FKeyEvent Enter(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
		Slate.ProcessKeyDownEvent(Enter);
		Slate.ProcessKeyUpEvent(Enter);
		return true;
	}

	/** True when a line of text that is shown somewhere under a widget has these words in it. */
	bool ShowsText(const TSharedRef<SWidget>& Widget, const TCHAR* Words)
	{
		if (!Widget->GetVisibility().IsVisible())
		{
			return false;
		}
		if (Widget->GetType() == FName("STextBlock") && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString().Contains(Words))
		{
			return true;
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (ShowsText(Children->GetChildAt(Index), Words))
			{
				return true;
			}
		}
		return false;
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
		TestTrue(TEXT("The tab says why it was refused"), ShowsText(Walk->Panel.ToSharedRef(), TEXT("needs {shot}")));
		TestTrue(TEXT("A name with the shot is typed in"), TypeIntoControlCalled(*Walk->Panel, TEXT("Name"), TEXT("{sequence}_{intent}_{shot}")));
	}, 0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Walk, ChoicesBefore]
	{
		TestEqual(TEXT("Files are named by it"), GetDefault<URoscEditorSettings>()->DeliverNamePattern, FString(TEXT("{sequence}_{intent}_{shot}")));
		TestFalse(TEXT("The reason is gone"), ShowsText(Walk->Panel.ToSharedRef(), TEXT("needs {shot}")));
		ChoicesBefore->PutBack();
	}, 0.3f));

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Walk, WeakCamera = TWeakObjectPtr<ACineCameraActor>(Camera)]
	{
		Walk->Window->RequestDestroyWindow();
		GEditor->SelectNone(false, true, false);
		if (ACineCameraActor* CameraToRemove = WeakCamera.Get())
		{
			Rosc::GripEditing::TakeCameraOffRig(CameraToRemove);
			CameraToRemove->GetWorld()->EditorDestroyActor(CameraToRemove, false);
		}
		GEditor->ResetTransaction(FText::FromString(TEXT("Rosc keyboard test")));
		return true;
	}));
	return true;
}

#endif
