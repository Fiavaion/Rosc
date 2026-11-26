// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscPlan.h"

#include "Algo/Transform.h"
#include "RoscMarkup.h"

namespace
{
	/** Space kept clear around the plan for labels, and below it for the scale bar, in pixels. */
	constexpr double PlanMargin = 26.0;
	constexpr double ScaleBarRoom = 22.0;

	/** The camera body as drawn: a box this long along the lens axis and this wide, in pixels. */
	constexpr double CameraLength = 14.0;
	constexpr double CameraWidth = 9.0;

	/** Which way a yaw points on the page: world X is up, world Y is to the right. */
	FVector2D GetPageDirection(double YawDeg)
	{
		const double Yaw = FMath::DegreesToRadians(YawDeg);
		return FVector2D(FMath::Sin(Yaw), -FMath::Cos(Yaw));
	}

	FVector2D GetWorldDirection(double YawDeg)
	{
		const double Yaw = FMath::DegreesToRadians(YawDeg);
		return FVector2D(FMath::Cos(Yaw), FMath::Sin(Yaw));
	}

	/** The longest round length, in centimetres, whose bar is no longer than the room given. */
	double ChooseScaleBarLength(double PixelsPerCm, double MaxPixels, ERoscDistanceUnits Units)
	{
		static const double Steps[] = { 1.0, 2.0, 5.0 };
		const double UnitCm = Units == ERoscDistanceUnits::Imperial ? 30.48 : 10.0;
		double Best = UnitCm;
		for (double Decade = 1.0; Decade <= 100000.0; Decade *= 10.0)
		{
			for (const double Step : Steps)
			{
				const double Length = UnitCm * Step * Decade;
				if (Length * PixelsPerCm <= MaxPixels)
				{
					Best = Length;
				}
			}
		}
		return Best;
	}
}

FRoscPlanDrawing Rosc::Plan::Layout(const FRoscPlan& Plan, FVector2D PictureSize, ERoscDistanceUnits Units)
{
	FRoscPlanDrawing Drawing;
	Drawing.Size = PictureSize;
	if (Plan.CameraPath.IsEmpty())
	{
		return Drawing;
	}

	const FRoscPlanCamera& First = Plan.CameraPath[0];
	const FRoscPlanCamera& Last = Plan.CameraPath.Last();
	const bool bCameraTravels = !First.Position.Equals(Last.Position, 1.0);
	const bool bCameraChanges = bCameraTravels || !FMath::IsNearlyEqual(First.YawDeg, Last.YawDeg, 0.5) || !FMath::IsNearlyEqual(First.HorizontalFovDeg, Last.HorizontalFovDeg, 0.5);

	// Everything the plan has to hold, in world centimetres.
	FBox2D Bounds(ForceInit);
	for (const FRoscPlanCamera& Camera : Plan.CameraPath)
	{
		Bounds += Camera.Position;
	}
	for (const TArray<FVector2D>& Track : Plan.Tracks)
	{
		for (const FVector2D& Point : Track)
		{
			Bounds += Point;
		}
	}
	for (const FRoscPlanSubject& Subject : Plan.Subjects)
	{
		Bounds += Subject.Position;
	}

	// The lines showing what the lens takes in reach as far as the farthest subject, or a few metres when there is none.
	double ViewLength = 0.0;
	for (const FRoscPlanSubject& Subject : Plan.Subjects)
	{
		ViewLength = FMath::Max(ViewLength, FVector2D::Distance(First.Position, Subject.Position));
	}
	ViewLength = FMath::Max(300.0, FMath::Max(ViewLength * 1.15, 0.5 * Bounds.GetSize().GetMax()));

	const auto GetViewEdges = [ViewLength](const FRoscPlanCamera& Camera)
	{
		const double HalfAngle = 0.5 * FMath::Clamp(Camera.HorizontalFovDeg, 1.0, 170.0);
		return TArray<FVector2D, TInlineAllocator<2>>
		{
			Camera.Position + ViewLength * GetWorldDirection(Camera.YawDeg - HalfAngle),
			Camera.Position + ViewLength * GetWorldDirection(Camera.YawDeg + HalfAngle),
		};
	};
	TArray<const FRoscPlanCamera*, TInlineAllocator<2>> DrawnCameras;
	DrawnCameras.Add(&First);
	if (bCameraChanges)
	{
		DrawnCameras.Add(&Last);
	}
	for (const FRoscPlanCamera* Camera : DrawnCameras)
	{
		for (const FVector2D& Edge : GetViewEdges(*Camera))
		{
			Bounds += Edge;
		}
	}

	// World X runs up the page and world Y across it, to one scale.
	const FVector2D Room(FMath::Max(1.0, PictureSize.X - 2.0 * PlanMargin), FMath::Max(1.0, PictureSize.Y - 2.0 * PlanMargin - ScaleBarRoom));
	const FVector2D Extent(FMath::Max(1.0, Bounds.GetSize().Y), FMath::Max(1.0, Bounds.GetSize().X));
	const double PixelsPerCm = FMath::Min(Room.X / Extent.X, Room.Y / Extent.Y);
	const FVector2D WorldCentre = Bounds.GetCenter();
	const FVector2D PageCentre(0.5 * PictureSize.X, PlanMargin + 0.5 * Room.Y);
	const auto ToPage = [PixelsPerCm, WorldCentre, PageCentre](const FVector2D& World)
	{
		return PageCentre + PixelsPerCm * FVector2D(World.Y - WorldCentre.Y, -(World.X - WorldCentre.X));
	};

	for (const TArray<FVector2D>& Track : Plan.Tracks)
	{
		FRoscPlanLine& Line = Drawing.Lines.AddDefaulted_GetRef();
		Line.Stroke = ERoscPlanStroke::Track;
		Algo::Transform(Track, Line.Points, ToPage);
	}

	if (bCameraTravels)
	{
		FRoscPlanLine& Line = Drawing.Lines.AddDefaulted_GetRef();
		Line.Stroke = ERoscPlanStroke::Path;
		Algo::Transform(Plan.CameraPath, Line.Points, [&ToPage](const FRoscPlanCamera& Camera) { return ToPage(Camera.Position); });
	}

	const FVector2D TravelDirection = (ToPage(Last.Position) - ToPage(First.Position)).GetSafeNormal();
	for (int32 CameraIndex = 0; CameraIndex < DrawnCameras.Num(); ++CameraIndex)
	{
		const FRoscPlanCamera& Camera = *DrawnCameras[CameraIndex];
		const FVector2D Position = ToPage(Camera.Position);
		const TArray<FVector2D, TInlineAllocator<2>> Edges = GetViewEdges(Camera);

		FRoscPlanLine& View = Drawing.Lines.AddDefaulted_GetRef();
		View.Stroke = ERoscPlanStroke::FieldOfView;
		View.Points = { ToPage(Edges[0]), Position, ToPage(Edges[1]) };

		// The body sits behind the point the lens looks from.
		const FVector2D Forward = GetPageDirection(Camera.YawDeg);
		const FVector2D Side(-Forward.Y, Forward.X);
		const FVector2D Back = Position - CameraLength * Forward;
		FRoscPlanLine& Body = Drawing.Lines.AddDefaulted_GetRef();
		Body.Stroke = ERoscPlanStroke::Camera;
		Body.Points =
		{
			Position - 0.5 * CameraWidth * Side,
			Position + 0.5 * CameraWidth * Side,
			Back + 0.5 * CameraWidth * Side,
			Back - 0.5 * CameraWidth * Side,
			Position - 0.5 * CameraWidth * Side,
		};

		if (bCameraChanges)
		{
			// The letter stands behind the camera, or beside it when the camera travels the way it
			// looks and the path runs through there.
			const bool bPathBehind = bCameraTravels && FMath::Abs(FVector2D::DotProduct(TravelDirection, Forward)) > 0.7;
			FRoscPlanLabel& Letter = Drawing.Labels.AddDefaulted_GetRef();
			Letter.Position = (bPathBehind ? Position - 0.5 * CameraLength * Forward + (0.5 * CameraWidth + 10.0) * Side : Back - 12.0 * Forward) - FVector2D(4.0, 0.0);
			Letter.Text = CameraIndex == 0 ? TEXT("A") : TEXT("B");
		}
	}

	for (const FRoscPlanSubject& Subject : Plan.Subjects)
	{
		const FVector2D Position = ToPage(Subject.Position);
		Drawing.Subjects.Add(Position);
		FRoscPlanLabel& Name = Drawing.Labels.AddDefaulted_GetRef();
		Name.Position = Position + FVector2D(SubjectRadius + 4.0, 0.0);
		Name.Text = Subject.Name;
	}

	// The scale bar, bottom left, with a tick at each end.
	const double BarLengthCm = ChooseScaleBarLength(PixelsPerCm, 0.3 * PictureSize.X, Units);
	const FVector2D BarStart(PlanMargin, PictureSize.Y - 12.0);
	const FVector2D BarEnd = BarStart + FVector2D(BarLengthCm * PixelsPerCm, 0.0);
	FRoscPlanLine& Bar = Drawing.Lines.AddDefaulted_GetRef();
	Bar.Stroke = ERoscPlanStroke::Scale;
	Bar.Points = { BarStart - FVector2D(0.0, 4.0), BarStart, BarEnd, BarEnd - FVector2D(0.0, 4.0) };
	FRoscPlanLabel& BarLabel = Drawing.Labels.AddDefaulted_GetRef();
	BarLabel.Position = BarEnd + FVector2D(6.0, -3.0);
	BarLabel.Text = Rosc::Units::FormatDistance(BarLengthCm * 10.0, Units).ToString();

	return Drawing;
}

FString Rosc::Plan::WriteSvg(const FRoscPlanDrawing& Drawing, const FString& Description)
{
	const auto GetStrokeAttributes = [](ERoscPlanStroke Stroke) -> const TCHAR*
	{
		switch (Stroke)
		{
		case ERoscPlanStroke::Track: return TEXT("stroke=\"#555555\" stroke-width=\"4\"");
		case ERoscPlanStroke::Path: return TEXT("stroke=\"#1456c8\" stroke-width=\"2\"");
		case ERoscPlanStroke::FieldOfView: return TEXT("stroke=\"#666666\" stroke-width=\"1\" stroke-dasharray=\"5 4\"");
		case ERoscPlanStroke::Camera: return TEXT("stroke=\"#000000\" stroke-width=\"2\"");
		default: return TEXT("stroke=\"#000000\" stroke-width=\"1.5\"");
		}
	};

	FString Svg = FString::Printf(TEXT("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %.0f %.0f\" width=\"%.0f\" height=\"%.0f\" role=\"img\" aria-label=\"%s\">\n"),
		Drawing.Size.X, Drawing.Size.Y, Drawing.Size.X, Drawing.Size.Y, *Rosc::Markup::Escape(Description));
	Svg += FString::Printf(TEXT("<rect width=\"%.0f\" height=\"%.0f\" fill=\"#ffffff\"/>\n"), Drawing.Size.X, Drawing.Size.Y);
	for (const FRoscPlanLine& Line : Drawing.Lines)
	{
		FString Points;
		for (const FVector2D& Point : Line.Points)
		{
			Points += FString::Printf(TEXT("%s%.1f,%.1f"), Points.IsEmpty() ? TEXT("") : TEXT(" "), Point.X, Point.Y);
		}
		Svg += FString::Printf(TEXT("<polyline fill=\"none\" %s points=\"%s\"/>\n"), GetStrokeAttributes(Line.Stroke), *Points);
	}
	for (const FVector2D& Subject : Drawing.Subjects)
	{
		Svg += FString::Printf(TEXT("<circle cx=\"%.1f\" cy=\"%.1f\" r=\"%.0f\" fill=\"#000000\"/>\n"), Subject.X, Subject.Y, SubjectRadius);
	}
	for (const FRoscPlanLabel& Label : Drawing.Labels)
	{
		Svg += FString::Printf(TEXT("<text x=\"%.1f\" y=\"%.1f\" font-family=\"sans-serif\" font-size=\"12\" dominant-baseline=\"middle\" fill=\"#000000\">%s</text>\n"),
			Label.Position.X, Label.Position.Y, *Rosc::Markup::Escape(Label.Text));
	}
	Svg += TEXT("</svg>\n");
	return Svg;
}
