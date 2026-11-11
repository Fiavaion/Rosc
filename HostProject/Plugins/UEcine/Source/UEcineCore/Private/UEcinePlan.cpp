#include "UEcinePlan.h"

#include "Algo/Transform.h"
#include "UEcineMarkup.h"

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
	double ChooseScaleBarLength(double PixelsPerCm, double MaxPixels, EUEcineDistanceUnits Units)
	{
		static const double Steps[] = { 1.0, 2.0, 5.0 };
		const double UnitCm = Units == EUEcineDistanceUnits::Imperial ? 30.48 : 10.0;
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

FUEcinePlanDrawing UEcine::Plan::Layout(const FUEcinePlan& Plan, FVector2D PictureSize, EUEcineDistanceUnits Units)
{
	FUEcinePlanDrawing Drawing;
	Drawing.Size = PictureSize;
	if (Plan.CameraPath.IsEmpty())
	{
		return Drawing;
	}

	const FUEcinePlanCamera& First = Plan.CameraPath[0];
	const FUEcinePlanCamera& Last = Plan.CameraPath.Last();
	const bool bCameraTravels = !First.Position.Equals(Last.Position, 1.0);
	const bool bCameraChanges = bCameraTravels || !FMath::IsNearlyEqual(First.YawDeg, Last.YawDeg, 0.5) || !FMath::IsNearlyEqual(First.HorizontalFovDeg, Last.HorizontalFovDeg, 0.5);

	// Everything the plan has to hold, in world centimetres.
	FBox2D Bounds(ForceInit);
	for (const FUEcinePlanCamera& Camera : Plan.CameraPath)
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
	for (const FUEcinePlanSubject& Subject : Plan.Subjects)
	{
		Bounds += Subject.Position;
	}

	// The lines showing what the lens takes in reach as far as the farthest subject, or a few metres when there is none.
	double ViewLength = 0.0;
	for (const FUEcinePlanSubject& Subject : Plan.Subjects)
	{
		ViewLength = FMath::Max(ViewLength, FVector2D::Distance(First.Position, Subject.Position));
	}
	ViewLength = FMath::Max(300.0, FMath::Max(ViewLength * 1.15, 0.5 * Bounds.GetSize().GetMax()));

	const auto GetViewEdges = [ViewLength](const FUEcinePlanCamera& Camera)
	{
		const double HalfAngle = 0.5 * FMath::Clamp(Camera.HorizontalFovDeg, 1.0, 170.0);
		return TArray<FVector2D, TInlineAllocator<2>>
		{
			Camera.Position + ViewLength * GetWorldDirection(Camera.YawDeg - HalfAngle),
			Camera.Position + ViewLength * GetWorldDirection(Camera.YawDeg + HalfAngle),
		};
	};
	TArray<const FUEcinePlanCamera*, TInlineAllocator<2>> DrawnCameras;
	DrawnCameras.Add(&First);
	if (bCameraChanges)
	{
		DrawnCameras.Add(&Last);
	}
	for (const FUEcinePlanCamera* Camera : DrawnCameras)
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
		FUEcinePlanLine& Line = Drawing.Lines.AddDefaulted_GetRef();
		Line.Stroke = EUEcinePlanStroke::Track;
		Algo::Transform(Track, Line.Points, ToPage);
	}

	if (bCameraTravels)
	{
		FUEcinePlanLine& Line = Drawing.Lines.AddDefaulted_GetRef();
		Line.Stroke = EUEcinePlanStroke::Path;
		Algo::Transform(Plan.CameraPath, Line.Points, [&ToPage](const FUEcinePlanCamera& Camera) { return ToPage(Camera.Position); });
	}

	for (int32 CameraIndex = 0; CameraIndex < DrawnCameras.Num(); ++CameraIndex)
	{
		const FUEcinePlanCamera& Camera = *DrawnCameras[CameraIndex];
		const FVector2D Position = ToPage(Camera.Position);
		const TArray<FVector2D, TInlineAllocator<2>> Edges = GetViewEdges(Camera);

		FUEcinePlanLine& View = Drawing.Lines.AddDefaulted_GetRef();
		View.Stroke = EUEcinePlanStroke::FieldOfView;
		View.Points = { ToPage(Edges[0]), Position, ToPage(Edges[1]) };

		// The body sits behind the point the lens looks from.
		const FVector2D Forward = GetPageDirection(Camera.YawDeg);
		const FVector2D Side(-Forward.Y, Forward.X);
		const FVector2D Back = Position - CameraLength * Forward;
		FUEcinePlanLine& Body = Drawing.Lines.AddDefaulted_GetRef();
		Body.Stroke = EUEcinePlanStroke::Camera;
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
			FUEcinePlanLabel& Letter = Drawing.Labels.AddDefaulted_GetRef();
			Letter.Position = Back - 12.0 * Forward - FVector2D(4.0, 0.0);
			Letter.Text = CameraIndex == 0 ? TEXT("A") : TEXT("B");
		}
	}

	for (const FUEcinePlanSubject& Subject : Plan.Subjects)
	{
		const FVector2D Position = ToPage(Subject.Position);
		Drawing.Subjects.Add(Position);
		FUEcinePlanLabel& Name = Drawing.Labels.AddDefaulted_GetRef();
		Name.Position = Position + FVector2D(SubjectRadius + 4.0, 0.0);
		Name.Text = Subject.Name;
	}

	// The scale bar, bottom left, with a tick at each end.
	const double BarLengthCm = ChooseScaleBarLength(PixelsPerCm, 0.3 * PictureSize.X, Units);
	const FVector2D BarStart(PlanMargin, PictureSize.Y - 12.0);
	const FVector2D BarEnd = BarStart + FVector2D(BarLengthCm * PixelsPerCm, 0.0);
	FUEcinePlanLine& Bar = Drawing.Lines.AddDefaulted_GetRef();
	Bar.Stroke = EUEcinePlanStroke::Scale;
	Bar.Points = { BarStart - FVector2D(0.0, 4.0), BarStart, BarEnd, BarEnd - FVector2D(0.0, 4.0) };
	FUEcinePlanLabel& BarLabel = Drawing.Labels.AddDefaulted_GetRef();
	BarLabel.Position = BarEnd + FVector2D(6.0, -3.0);
	BarLabel.Text = UEcine::Units::FormatDistance(BarLengthCm * 10.0, Units).ToString();

	return Drawing;
}

FString UEcine::Plan::WriteSvg(const FUEcinePlanDrawing& Drawing, const FString& Description)
{
	const auto GetStrokeAttributes = [](EUEcinePlanStroke Stroke) -> const TCHAR*
	{
		switch (Stroke)
		{
		case EUEcinePlanStroke::Track: return TEXT("stroke=\"#555555\" stroke-width=\"4\"");
		case EUEcinePlanStroke::Path: return TEXT("stroke=\"#1456c8\" stroke-width=\"2\"");
		case EUEcinePlanStroke::FieldOfView: return TEXT("stroke=\"#666666\" stroke-width=\"1\" stroke-dasharray=\"5 4\"");
		case EUEcinePlanStroke::Camera: return TEXT("stroke=\"#000000\" stroke-width=\"2\"");
		default: return TEXT("stroke=\"#000000\" stroke-width=\"1.5\"");
		}
	};

	FString Svg = FString::Printf(TEXT("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %.0f %.0f\" width=\"%.0f\" height=\"%.0f\" role=\"img\" aria-label=\"%s\">\n"),
		Drawing.Size.X, Drawing.Size.Y, Drawing.Size.X, Drawing.Size.Y, *UEcine::Markup::Escape(Description));
	Svg += FString::Printf(TEXT("<rect width=\"%.0f\" height=\"%.0f\" fill=\"#ffffff\"/>\n"), Drawing.Size.X, Drawing.Size.Y);
	for (const FUEcinePlanLine& Line : Drawing.Lines)
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
	for (const FUEcinePlanLabel& Label : Drawing.Labels)
	{
		Svg += FString::Printf(TEXT("<text x=\"%.1f\" y=\"%.1f\" font-family=\"sans-serif\" font-size=\"12\" dominant-baseline=\"middle\" fill=\"#000000\">%s</text>\n"),
			Label.Position.X, Label.Position.Y, *UEcine::Markup::Escape(Label.Text));
	}
	Svg += TEXT("</svg>\n");
	return Svg;
}
