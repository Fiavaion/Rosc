// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"
#include "RoscPaperwork.h"
#include "RoscPlan.h"
#include "RoscShotTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FRoscPaperwork MakeTestPaperwork()
	{
		FRoscPaperwork Paperwork;
		Paperwork.Title = TEXT("Harbour <Night>");
		Paperwork.Date = TEXT("11 November 2026");

		FRoscShotReport& First = Paperwork.Shots.AddDefaulted_GetRef();
		First.Scene = TEXT("12");
		First.Shot = TEXT("shot0010");
		First.Size = TEXT("WS");
		First.Move = TEXT("Crane up, then hold");
		First.Notes = TEXT("She says \"wait\".\nBoom shadow on <wall> & floor");
		First.Start = TEXT("00:00:00:00");
		First.Duration = TEXT("4 s 12 fr");
		First.Camera = TEXT("A Cam");
		First.Body = TEXT("ARRI ALEXA 35");
		First.Format = TEXT("4.6K 3:2 Open Gate");
		First.LensKit = TEXT("ARRI Signature Prime");
		First.FocalLength = TEXT("35 mm");
		First.TStop = TEXT("T2.8");
		First.Iso = TEXT("800");
		First.Shutter = TEXT("180°");
		First.FrameRate = TEXT("24 fps");
		First.Focus = TEXT("3.05 m");
		First.Support = TEXT("Telescoping crane");
		First.Storyboard.Add({ TEXT("Frame 1"), { 0xFF, 0xD8, 0xFF, 0xE0 } });
		First.PlanSvg = TEXT("<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>\n");

		FRoscShotReport& Second = Paperwork.Shots.AddDefaulted_GetRef();
		Second.Shot = TEXT("shot0020");
		Second.Notes = TEXT("=SUM(A1:A9)");
		Second.Move = TEXT("-");
		return Paperwork;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscPaperworkCsvTest, "Rosc.Core.Paperwork.Csv", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscPaperworkCsvTest::RunTest(const FString& Parameters)
{
	const FRoscPaperwork Paperwork = MakeTestPaperwork();

	TArray<FString> Lines;
	Rosc::Paperwork::WriteShotListCsv(Paperwork).ParseIntoArray(Lines, TEXT("\r\n"));
	// Lines of the file end with a carriage return and a line feed. The line feed alone inside the first shot's notes is part of its field.
	if (!TestEqual(TEXT("Shot list: a heading line, then a line for each shot"), Lines.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("Shot list: the heading line"), Lines[0], FString(TEXT("Scene,Shot,Size,Move,Lens,Support,Start,Duration,Notes")));
	TestEqual(TEXT("Shot list: a field with a comma, quotes or a line break is quoted, and its quotes doubled"),
		Lines[1],
		FString(TEXT("12,shot0010,WS,\"Crane up, then hold\",35 mm,Telescoping crane,00:00:00:00,4 s 12 fr,\"She says \"\"wait\"\".\nBoom shadow on <wall> & floor\"")));
	TestEqual(TEXT("Shot list: a field a spreadsheet would run as a formula is made plain text"),
		Lines[2], FString(TEXT(",shot0020,,'-,,,,,'=SUM(A1:A9)")));

	Rosc::Paperwork::WriteCameraReportCsv(Paperwork).ParseIntoArray(Lines, TEXT("\r\n"));
	if (TestEqual(TEXT("Camera report: a heading line, then a line for each shot"), Lines.Num(), 3))
	{
		TestEqual(TEXT("Camera report: the heading line"), Lines[0],
			FString(TEXT("Scene,Shot,Camera,Body,Recording format,Lens kit,Focal length,T-stop,ISO,Shutter angle,Frame rate,ND,Focus,Support,Start,Duration,Notes")));
		TestTrue(TEXT("Camera report: the camera, lens and exposure of the first shot"),
			Lines[1].StartsWith(TEXT("12,shot0010,A Cam,ARRI ALEXA 35,4.6K 3:2 Open Gate,ARRI Signature Prime,35 mm,T2.8,800,180°,24 fps,,3.05 m,Telescoping crane,")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscPaperworkHtmlTest, "Rosc.Core.Paperwork.Html", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscPaperworkHtmlTest::RunTest(const FString& Parameters)
{
	const FString Html = Rosc::Paperwork::WriteHtml(MakeTestPaperwork());

	TestTrue(TEXT("The page says what it is"), Html.StartsWith(TEXT("<!DOCTYPE html>\n<html lang=\"en\">")));
	TestTrue(TEXT("The page ends properly"), Html.EndsWith(TEXT("</body>\n</html>\n")));
	TestTrue(TEXT("The title is written as text, not as markup"), Html.Contains(TEXT("<h1>Harbour &lt;Night&gt;: shot list and camera report</h1>")));
	TestTrue(TEXT("Notes are written as text, with their line break kept"), Html.Contains(TEXT("She says &quot;wait&quot;.<br>Boom shadow on &lt;wall&gt; &amp; floor")));
	TestFalse(TEXT("Nothing the user typed becomes markup"), Html.Contains(TEXT("<wall>")));
	TestTrue(TEXT("Table headings are marked as headings of their columns"), Html.Contains(TEXT("<th scope=\"col\">Scene</th>")));
	TestTrue(TEXT("Each shot has a heading with its scene, name and size"), Html.Contains(TEXT("<h3>Scene 12 · shot0010 · WS</h3>")));
	TestTrue(TEXT("A shot with no scene or size is headed by its name"), Html.Contains(TEXT("<h3>shot0020</h3>")));
	TestTrue(TEXT("A storyboard frame is inside the page, with a description"),
		Html.Contains(TEXT("<img alt=\"Storyboard frame of shot0010: Frame 1\" src=\"data:image/jpeg;base64,/9j/4A==\">")));
	TestTrue(TEXT("The overhead plan is inside the page"), Html.Contains(TEXT("<figure class=\"plan\">\n<svg xmlns=")));
	TestTrue(TEXT("The camera report gives the lens"), Html.Contains(TEXT("<tr><th scope=\"row\">Focal length</th><td>35 mm</td></tr>")));
	TestFalse(TEXT("The camera report leaves out what is not known"), Html.Contains(TEXT("<th scope=\"row\">ND</th>")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscPlanLayoutTest, "Rosc.Core.Plan.Layout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscPlanLayoutTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::Plan;

	const FVector2D PictureSize(480.0, 360.0);
	const auto FindLines = [](const FRoscPlanDrawing& Drawing, ERoscPlanStroke Stroke)
	{
		return Drawing.Lines.FilterByPredicate([Stroke](const FRoscPlanLine& Line) { return Line.Stroke == Stroke; });
	};
	const auto IsInPicture = [&PictureSize](const FVector2D& Point)
	{
		return Point.X >= 0.0 && Point.Y >= 0.0 && Point.X <= PictureSize.X && Point.Y <= PictureSize.Y;
	};

	TestTrue(TEXT("A plan with no camera draws nothing"), Layout(FRoscPlan(), PictureSize, ERoscDistanceUnits::Metric).Lines.IsEmpty());

	// A locked-off camera at the origin looking along world X, a subject 5 m ahead and another 3 m to its right.
	FRoscPlan Static;
	Static.CameraPath.Add({ FVector2D(0.0, 0.0), 0.0, 40.0 });
	Static.CameraPath.Add({ FVector2D(0.0, 0.0), 0.0, 40.0 });
	Static.Subjects.Add({ TEXT("Hero"), FVector2D(500.0, 0.0) });
	Static.Subjects.Add({ TEXT("Car"), FVector2D(0.0, 300.0) });
	const FRoscPlanDrawing StaticDrawing = Layout(Static, PictureSize, ERoscDistanceUnits::Metric);

	const TArray<FRoscPlanLine> StaticViews = FindLines(StaticDrawing, ERoscPlanStroke::FieldOfView);
	if (!TestEqual(TEXT("Locked off: the camera is drawn once"), StaticViews.Num(), 1) || !TestEqual(TEXT("Two subjects, two dots"), StaticDrawing.Subjects.Num(), 2))
	{
		return false;
	}
	const FVector2D CameraPoint = StaticViews[0].Points[1];
	TestEqual(TEXT("A subject ahead of a camera with no yaw is straight up the page"), StaticDrawing.Subjects[0].X, CameraPoint.X, 0.01);
	TestTrue(TEXT("A subject ahead of the camera is above it on the page"), StaticDrawing.Subjects[0].Y < CameraPoint.Y);
	TestEqual(TEXT("A subject to the camera's right is level with it"), StaticDrawing.Subjects[1].Y, CameraPoint.Y, 0.01);
	TestTrue(TEXT("A subject to the camera's right is to its right on the page"), StaticDrawing.Subjects[1].X > CameraPoint.X);
	// To one scale: 5 m up the page is five thirds of 3 m across it.
	TestEqual(TEXT("The plan is to one scale in both directions"),
		(CameraPoint.Y - StaticDrawing.Subjects[0].Y) / (StaticDrawing.Subjects[1].X - CameraPoint.X), 500.0 / 300.0, 0.001);
	// The edges of a 40 degree lens are 20 degrees either side of straight up.
	const FVector2D LeftEdge = StaticViews[0].Points[0] - CameraPoint;
	const FVector2D RightEdge = StaticViews[0].Points[2] - CameraPoint;
	TestEqual(TEXT("The left edge of the lens's view is half its angle off the axis"), FMath::RadiansToDegrees(FMath::Atan2(-LeftEdge.X, -LeftEdge.Y)), 20.0, 0.01);
	TestEqual(TEXT("The right edge of the lens's view is half its angle off the axis"), FMath::RadiansToDegrees(FMath::Atan2(RightEdge.X, -RightEdge.Y)), 20.0, 0.01);
	TestTrue(TEXT("Locked off: no path and no letters"), FindLines(StaticDrawing, ERoscPlanStroke::Path).IsEmpty()
		&& !StaticDrawing.Labels.ContainsByPredicate([](const FRoscPlanLabel& Label) { return Label.Text == TEXT("A"); }));
	TestTrue(TEXT("Subjects are named"), StaticDrawing.Labels.ContainsByPredicate([](const FRoscPlanLabel& Label) { return Label.Text == TEXT("Hero"); }));
	for (const FRoscPlanLine& Line : StaticDrawing.Lines)
	{
		for (const FVector2D& Point : Line.Points)
		{
			TestTrue(TEXT("Every line is inside the picture"), IsInPicture(Point));
		}
	}

	// The scale bar is a round length no longer than a third of the picture, and says how long it is.
	const TArray<FRoscPlanLine> Bars = FindLines(StaticDrawing, ERoscPlanStroke::Scale);
	if (TestEqual(TEXT("There is one scale bar"), Bars.Num(), 1))
	{
		const double BarPixels = Bars[0].Points[2].X - Bars[0].Points[1].X;
		const double PixelsPerCm = (StaticDrawing.Subjects[1].X - CameraPoint.X) / 300.0;
		TestEqual(TEXT("The scale bar is a round length"), BarPixels / PixelsPerCm, 200.0, 0.01);
		TestTrue(TEXT("The scale bar says how long it is"), StaticDrawing.Labels.ContainsByPredicate([](const FRoscPlanLabel& Label) { return Label.Text == TEXT("2 m"); }));
	}
	const FRoscPlanDrawing ImperialDrawing = Layout(Static, PictureSize, ERoscDistanceUnits::Imperial);
	TestTrue(TEXT("In feet and inches the scale bar is a round number of feet"), ImperialDrawing.Labels.ContainsByPredicate([](const FRoscPlanLabel& Label) { return Label.Text == TEXT("5' 0\""); }));

	// A dolly move along a track, panning as it goes.
	FRoscPlan Move;
	Move.Tracks.Add({ FVector2D(0.0, -200.0), FVector2D(0.0, 400.0) });
	Move.CameraPath.Add({ FVector2D(0.0, -100.0), 0.0, 30.0 });
	Move.CameraPath.Add({ FVector2D(0.0, 100.0), -10.0, 30.0 });
	Move.CameraPath.Add({ FVector2D(0.0, 300.0), -20.0, 30.0 });
	const FRoscPlanDrawing MoveDrawing = Layout(Move, PictureSize, ERoscDistanceUnits::Metric);
	TestEqual(TEXT("A move: the camera is drawn where it starts and where it ends"), FindLines(MoveDrawing, ERoscPlanStroke::Camera).Num(), 2);
	const TArray<FRoscPlanLine> Paths = FindLines(MoveDrawing, ERoscPlanStroke::Path);
	if (TestEqual(TEXT("A move: one path"), Paths.Num(), 1))
	{
		TestEqual(TEXT("A move: the path goes through every position"), Paths[0].Points.Num(), 3);
		TestTrue(TEXT("A move along world Y runs left to right"), Paths[0].Points[2].X > Paths[0].Points[0].X);
		TestEqual(TEXT("A move along world Y stays level on the page"), Paths[0].Points[2].Y, Paths[0].Points[0].Y, 0.01);
	}
	TestEqual(TEXT("A move: the track is drawn"), FindLines(MoveDrawing, ERoscPlanStroke::Track).Num(), 1);
	TestTrue(TEXT("A move: the start is lettered A and the end B"),
		MoveDrawing.Labels.ContainsByPredicate([](const FRoscPlanLabel& Label) { return Label.Text == TEXT("A"); })
		&& MoveDrawing.Labels.ContainsByPredicate([](const FRoscPlanLabel& Label) { return Label.Text == TEXT("B"); }));

	// A push in: the camera travels the way it looks, so its letters stand beside it, clear of the path.
	FRoscPlan PushIn;
	PushIn.CameraPath.Add({ FVector2D(0.0, 0.0), 0.0, 30.0 });
	PushIn.CameraPath.Add({ FVector2D(300.0, 0.0), 0.0, 30.0 });
	const FRoscPlanDrawing PushInDrawing = Layout(PushIn, PictureSize, ERoscDistanceUnits::Metric);
	const TArray<FRoscPlanLine> PushInPaths = FindLines(PushInDrawing, ERoscPlanStroke::Path);
	if (TestEqual(TEXT("A push in: one path"), PushInPaths.Num(), 1))
	{
		const double PathX = PushInPaths[0].Points[0].X;
		TestEqual(TEXT("A push in runs straight up the page"), PushInPaths[0].Points[1].X, PathX, 0.01);
		for (const FRoscPlanLabel& Label : PushInDrawing.Labels)
		{
			if (Label.Text == TEXT("A") || Label.Text == TEXT("B"))
			{
				TestTrue(TEXT("A push in: the letter starts clear of the path"), Label.Position.X > PathX + 5.0);
			}
		}
	}

	// A pan on the spot: two cameras drawn, no path.
	FRoscPlan Pan;
	Pan.CameraPath.Add({ FVector2D::ZeroVector, 0.0, 30.0 });
	Pan.CameraPath.Add({ FVector2D::ZeroVector, 45.0, 30.0 });
	const FRoscPlanDrawing PanDrawing = Layout(Pan, PictureSize, ERoscDistanceUnits::Metric);
	TestEqual(TEXT("A pan: the view is drawn at its start and at its end"), FindLines(PanDrawing, ERoscPlanStroke::FieldOfView).Num(), 2);
	TestTrue(TEXT("A pan: no path"), FindLines(PanDrawing, ERoscPlanStroke::Path).IsEmpty());

	const FString Svg = WriteSvg(MoveDrawing, TEXT("Overhead plan of shot <10>"));
	TestTrue(TEXT("The picture is an SVG of the size asked for"), Svg.StartsWith(TEXT("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 480 360\"")));
	TestTrue(TEXT("The picture is described in words"), Svg.Contains(TEXT("aria-label=\"Overhead plan of shot &lt;10&gt;\"")));
	TestTrue(TEXT("The picture ends properly"), Svg.EndsWith(TEXT("</svg>\n")));
	TestEqual(TEXT("The picture has a line for each line of the drawing"), (Svg.Len() - Svg.Replace(TEXT("<polyline"), TEXT("")).Len()) / 9, MoveDrawing.Lines.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscShotSizeTest, "Rosc.Core.Shots.Sizes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscShotSizeTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::Shots;

	const UEnum* Sizes = StaticEnum<ERoscShotSize>();
	TestEqual(TEXT("Every shot size is offered"), GetShotSizes().Num(), Sizes->NumEnums() - 1);
	TestTrue(TEXT("The list starts with no size chosen"), GetShotSizes()[0] == ERoscShotSize::Unset);

	TSet<FString> Names;
	TSet<FString> Abbreviations;
	for (const ERoscShotSize Size : GetShotSizes())
	{
		Names.Add(GetShotSizeName(Size).ToString());
		Abbreviations.Add(GetShotSizeAbbreviation(Size).ToString());
	}
	TestEqual(TEXT("Every shot size has its own name"), Names.Num(), GetShotSizes().Num());
	TestEqual(TEXT("Every shot size has its own short form"), Abbreviations.Num(), GetShotSizes().Num());
	TestEqual(TEXT("A close-up is written CU"), GetShotSizeAbbreviation(ERoscShotSize::CloseUp).ToString(), FString(TEXT("CU")));
	TestTrue(TEXT("A size not chosen has no short form"), GetShotSizeAbbreviation(ERoscShotSize::Unset).IsEmpty());
	return true;
}

#endif
