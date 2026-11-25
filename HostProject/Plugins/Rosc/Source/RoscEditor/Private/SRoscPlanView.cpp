// Copyright 2026 Fiavaion. All rights reserved.

#include "SRoscPlanView.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "RoscEditorSettings.h"

namespace
{
	/** The plan's height in the tab. Its width is the tab's. */
	constexpr float PlanViewHeight = 200.0f;

	struct FPlanStrokeLook
	{
		FLinearColor Colour;
		float Thickness = 1.0f;
		bool bDashed = false;
	};

	/** Light on the tab's dark ground; the camera's path alone is in colour, and it is also the only heavy unbroken line besides the track. */
	FPlanStrokeLook GetPlanStrokeLook(ERoscPlanStroke Stroke)
	{
		const FLinearColor Foreground = FStyleColors::Foreground.GetSpecifiedColor();
		switch (Stroke)
		{
		case ERoscPlanStroke::Track:
			return { Foreground.CopyWithNewOpacity(0.55f), 4.0f, false };
		case ERoscPlanStroke::Path:
			return { FStyleColors::AccentBlue.GetSpecifiedColor(), 2.0f, false };
		case ERoscPlanStroke::FieldOfView:
			return { Foreground.CopyWithNewOpacity(0.7f), 1.0f, true };
		case ERoscPlanStroke::Camera:
			return { FStyleColors::White.GetSpecifiedColor(), 1.5f, false };
		case ERoscPlanStroke::Scale:
		default:
			return { Foreground, 1.0f, false };
		}
	}
}

void SRoscPlanView::Construct(const FArguments& InArgs)
{
	SetCanTick(false);
}

void SRoscPlanView::SetPlan(const FRoscPlan& InPlan)
{
	Plan = InPlan;
	Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2D SRoscPlanView::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(PlanViewHeight, PlanViewHeight);
}

int32 SRoscPlanView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();
	// A colour brush carries its colour as a tint, which a drawn box has to be given.
	const FSlateBrush* Ground = FAppStyle::Get().GetBrush("Brushes.Recessed");
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PaintGeometry, Ground, ESlateDrawEffect::None, Ground->GetTint(InWidgetStyle));

	const FRoscPlanDrawing Drawing = Rosc::Plan::Layout(Plan, FVector2D(AllottedGeometry.GetLocalSize()), URoscEditorSettings::GetDistanceUnits());

	const int32 LineLayer = LayerId + 1;
	for (const FRoscPlanLine& Line : Drawing.Lines)
	{
		const FPlanStrokeLook Look = GetPlanStrokeLook(Line.Stroke);
		TArray<FVector2f> Points;
		Points.Reserve(Line.Points.Num());
		for (const FVector2D& Point : Line.Points)
		{
			Points.Add(FVector2f(Point));
		}
		if (Look.bDashed)
		{
			FSlateDrawElement::MakeDashedLines(OutDrawElements, LineLayer, PaintGeometry, MoveTemp(Points), ESlateDrawEffect::None, Look.Colour, Look.Thickness, 6.0f);
		}
		else
		{
			FSlateDrawElement::MakeLines(OutDrawElements, LineLayer, PaintGeometry, MoveTemp(Points), ESlateDrawEffect::None, Look.Colour, true, Look.Thickness);
		}
	}

	const int32 MarkLayer = LineLayer + 1;
	const FLinearColor MarkColour = FStyleColors::White.GetSpecifiedColor();
	const FSlateBrush* Solid = FAppStyle::Get().GetBrush("WhiteBrush");
	const FVector2D DotSize(2.0 * Rosc::Plan::SubjectRadius, 2.0 * Rosc::Plan::SubjectRadius);
	for (const FVector2D& Subject : Drawing.Subjects)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, MarkLayer, AllottedGeometry.ToPaintGeometry(DotSize, FSlateLayoutTransform(Subject - 0.5 * DotSize)), Solid, ESlateDrawEffect::None, MarkColour);
	}

	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const float HalfTextHeight = 0.5f * FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->GetMaxCharacterHeight(Font);
	for (const FRoscPlanLabel& Label : Drawing.Labels)
	{
		const FVector2D TopLeft = Label.Position - FVector2D(0.0, HalfTextHeight);
		FSlateDrawElement::MakeText(OutDrawElements, MarkLayer, AllottedGeometry.ToPaintGeometry(AllottedGeometry.GetLocalSize(), FSlateLayoutTransform(TopLeft)), Label.Text, Font, ESlateDrawEffect::None, MarkColour);
	}
	return MarkLayer;
}
