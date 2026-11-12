#include "CineCameraActor.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "SUEcinePanel.h"
#include "Tests/AutomationCommon.h"
#include "UEcineCameraEditing.h"
#include "UEcineFocusEditing.h"
#include "UEcineGripEditing.h"
#include "UEcineRig.h"
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
	TSharedPtr<SWidget> FindControlCalled(SUEcinePanel& Panel, const TCHAR* AccessibleName)
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
	FString DescribeFocus(const SUEcinePanel& Panel)
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
	bool PressControlCalled(SUEcinePanel& Panel, const TCHAR* AccessibleName)
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

	const TCHAR* GetDepartmentTestName(EUEcineDepartment Department)
	{
		switch (Department)
		{
		case EUEcineDepartment::Camera: return TEXT("Camera");
		case EUEcineDepartment::Grip: return TEXT("Grip");
		case EUEcineDepartment::Focus: return TEXT("Focus");
		case EUEcineDepartment::Monitor: return TEXT("Monitor");
		case EUEcineDepartment::Shots: return TEXT("Shots");
		case EUEcineDepartment::Deliver: return TEXT("Deliver");
		default: return TEXT("AI");
		}
	}

	/**
	 * Presses Tab through a department of the panel in a window, from its first control to its
	 * last, and checks what a keyboard user needs: every control is reached, in the order they are
	 * laid out, none holds the focus, Shift+Tab goes back, and every control has words that name it.
	 */
	void WalkDepartmentByKeyboard(FAutomationTestBase& Test, SUEcinePanel& Panel, EUEcineDepartment Department)
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineKeyboardTest, "UEcine.Editor.Keyboard", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineKeyboardTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Skipped: walking the panel in a window needs a renderer (tools/test.ps1 -Rendering)."));
		return true;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ACineCameraActor* Camera = UEcine::CameraEditing::SpawnCamera(World, FTransform(FVector(0.0, 0.0, 160.0)));
	AUEcineRig* Rig = UEcine::GripEditing::PutCameraOnRig(Camera, *UEcine::Rig::FindPreset(TEXT("crane-30ft")));
	UEcine::GripEditing::AddMark(Rig);
	UEcine::GripEditing::EditRig(Rig, FText::FromString(TEXT("Swing")), [](AUEcineRig& EditedRig)
	{
		FUEcineRigPose Pose = EditedRig.GetPose();
		Pose.Swing += 20.0;
		EditedRig.SetPose(Pose);
	});
	UEcine::GripEditing::AddMark(Rig);
	UEcine::FocusEditing::AddFocusMark(Camera);
	UEcine::FocusEditing::AddFocusMark(Camera);
	GEditor->SelectNone(false, true, false);
	GEditor->SelectActor(Camera, true, true);

	struct FWalk
	{
		TSharedPtr<SWindow> Window;
		TSharedPtr<SUEcinePanel> Panel;
	};
	const TSharedRef<FWalk> Walk = MakeShared<FWalk>();
	Walk->Window = SNew(SWindow)
		.Title(FText::FromString(TEXT("UEcine keyboard test")))
		.ClientSize(FVector2D(320.0, 900.0))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SAssignNew(Walk->Panel, SUEcinePanel)
		];
	FSlateApplication::Get().AddWindow(Walk->Window.ToSharedRef(), true);

	for (const EUEcineDepartment Department : { EUEcineDepartment::Camera, EUEcineDepartment::Grip, EUEcineDepartment::Focus, EUEcineDepartment::Monitor,
		EUEcineDepartment::Shots, EUEcineDepartment::Deliver, EUEcineDepartment::AI })
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
	const TWeakObjectPtr<AUEcineRig> WeakRig = Rig;
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Walk]
	{
		Walk->Panel->ShowDepartment(EUEcineDepartment::Grip);
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

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Walk, WeakCamera = TWeakObjectPtr<ACineCameraActor>(Camera)]
	{
		Walk->Window->RequestDestroyWindow();
		GEditor->SelectNone(false, true, false);
		if (ACineCameraActor* CameraToRemove = WeakCamera.Get())
		{
			UEcine::GripEditing::TakeCameraOffRig(CameraToRemove);
			CameraToRemove->GetWorld()->EditorDestroyActor(CameraToRemove, false);
		}
		GEditor->ResetTransaction(FText::FromString(TEXT("UEcine keyboard test")));
		return true;
	}));
	return true;
}

#endif
