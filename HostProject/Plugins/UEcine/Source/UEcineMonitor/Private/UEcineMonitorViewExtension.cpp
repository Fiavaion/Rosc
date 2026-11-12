// Copyright 2026 Fiavaion. All rights reserved.

#include "UEcineMonitorViewExtension.h"

#include "CanvasTypes.h"
#include "Engine/Font.h"
#include "GlobalShader.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"
#include "UnrealEngine.h"

#define LOCTEXT_NAMESPACE "UEcineMonitor"

namespace
{
	// Bits of the Tools shader parameter. Kept in step with UEcineMonitor.usf.
	const uint32 ToolFalseColour = 1;
	const uint32 ToolZebras = 2;
	const uint32 ToolPeaking = 4;

	// Room in the shader for the false colour bands. Kept in step with UEcineMonitor.usf.
	const int32 MaxFalseColourBands = 8;

	// The scopes read every pixel of a picture up to this wide, and every second, third... of a wider one.
	const int32 ScopeFullSampleWidth = 960;

	// A waveform column with all its samples at one level, or a histogram level holding this
	// fraction of an evenly spread picture's share, is drawn at full strength.
	const float ScopeGain = 16.0f;

	void SetScopeDefines(FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("UECINE_SCOPE_COLUMNS"), FUEcineMonitorViewExtension::ScopeColumns);
		OutEnvironment.SetDefine(TEXT("UECINE_SCOPE_LEVELS"), FUEcineMonitorViewExtension::ScopeLevels);
	}

	/** Counts, for the waveform and the histogram, how many picture samples sit at each signal level. */
	class FUEcineMonitorScopeCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FUEcineMonitorScopeCS);
		SHADER_USE_PARAMETER_STRUCT(FUEcineMonitorScopeCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
			SHADER_PARAMETER(FIntPoint, ViewMin)
			SHADER_PARAMETER(FIntPoint, ViewSize)
			SHADER_PARAMETER(int32, SampleStep)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWWaveformTexture)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWHistogramTexture)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			SetScopeDefines(OutEnvironment);
		}
	};

	/** Draws the picture with the tools applied and the scopes over it. */
	class FUEcineMonitorPicturePS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FUEcineMonitorPicturePS);
		SHADER_USE_PARAMETER_STRUCT(FUEcineMonitorPicturePS, FGlobalShader);

		class FScopes : SHADER_PERMUTATION_BOOL("UECINE_SCOPES");
		using FPermutationDomain = TShaderPermutationDomain<FScopes>;

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
			SHADER_PARAMETER(uint32, Tools)
			SHADER_PARAMETER_ARRAY(FVector4f, FalseColourRanges, [MaxFalseColourBands])
			SHADER_PARAMETER_ARRAY(FVector4f, FalseColourColours, [MaxFalseColourBands])
			SHADER_PARAMETER(int32, FalseColourBandCount)
			SHADER_PARAMETER(float, ZebraLevel)
			SHADER_PARAMETER(FVector3f, PeakingColour)
			SHADER_PARAMETER(float, PeakingThreshold)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, WaveformTexture)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, HistogramTexture)
			SHADER_PARAMETER(FVector4f, WaveformRect)
			SHADER_PARAMETER(FVector4f, HistogramRect)
			SHADER_PARAMETER(float, WaveformScale)
			SHADER_PARAMETER(float, HistogramScale)
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			SetScopeDefines(OutEnvironment);
			OutEnvironment.SetDefine(TEXT("UECINE_MAX_FALSE_COLOUR_BANDS"), MaxFalseColourBands);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FUEcineMonitorScopeCS, "/Plugin/UEcine/Private/UEcineMonitor.usf", "ScopeCS", SF_Compute);
	IMPLEMENT_GLOBAL_SHADER(FUEcineMonitorPicturePS, "/Plugin/UEcine/Private/UEcineMonitor.usf", "PicturePS", SF_Pixel);

	FVector4f ToShaderRect(const FIntRect& Rect)
	{
		return FVector4f(Rect.Min.X, Rect.Min.Y, Rect.Max.X, Rect.Max.Y);
	}

	FVector3f ToShaderColour(const FColor& Colour)
	{
		return FVector3f(Colour.R, Colour.G, Colour.B) / 255.0f;
	}

	/**
	 * Draws over the finished picture in whole pixels. The canvas works in units of one pixel at a
	 * display scale of 100 %, so text keeps a readable size on a high-density display.
	 */
	class FOverlayPainter
	{
	public:
		explicit FOverlayPainter(FCanvas& InCanvas)
			: Canvas(InCanvas)
			, Scale(FMath::Max(InCanvas.GetDPIScale(), 0.01f))
		{
			Canvas.SetBaseTransform(FMatrix(FScaleMatrix(Scale) * Canvas.CalcBaseTransform2D(Canvas.GetViewRect().Width(), Canvas.GetViewRect().Height())));
		}

		/** Pixels taken by a length given in canvas units. */
		int32 ToPixels(float CanvasUnits) const
		{
			return FMath::CeilToInt32(CanvasUnits * Scale);
		}

		/** The room a piece of text takes, in pixels. */
		FIntPoint Measure(const FString& String) const
		{
			int32 Height = 0;
			int32 Width = 0;
			GetStatsFont()->GetStringHeightAndWidth(String, Height, Width);
			return FIntPoint(ToPixels(Width), ToPixels(Height));
		}

		/** The canvas encodes what it is given for the display, so a colour goes in decoded and lands as chosen. */
		void Fill(const FIntRect& Rect, const FColor& Colour) const
		{
			if (Rect.Width() > 0 && Rect.Height() > 0)
			{
				Canvas.DrawTile(Rect.Min.X / Scale, Rect.Min.Y / Scale, Rect.Width() / Scale, Rect.Height() / Scale, 0.0f, 0.0f, 1.0f, 1.0f, FLinearColor(Colour));
			}
		}

		/** The edge of a rectangle, drawn inwards from it. */
		void Outline(const FIntRect& Rect, int32 Thickness, const FColor& Colour) const
		{
			Fill(FIntRect(Rect.Min.X, Rect.Min.Y, Rect.Max.X, Rect.Min.Y + Thickness), Colour);
			Fill(FIntRect(Rect.Min.X, Rect.Max.Y - Thickness, Rect.Max.X, Rect.Max.Y), Colour);
			Fill(FIntRect(Rect.Min.X, Rect.Min.Y + Thickness, Rect.Min.X + Thickness, Rect.Max.Y - Thickness), Colour);
			Fill(FIntRect(Rect.Max.X - Thickness, Rect.Min.Y + Thickness, Rect.Max.X, Rect.Max.Y - Thickness), Colour);
		}

		/** A line round a rectangle with a black edge either side of it, so it shows on any picture. */
		void Line(const FIntRect& Rect, int32 Thickness, const FColor& Colour) const
		{
			Outline(FIntRect(Rect.Min - 1, Rect.Max + 1), Thickness + 2, FColor::Black);
			Outline(Rect, Thickness, Colour);
		}

		/** A filled bar with a black edge round it. */
		void Bar(const FIntRect& Rect, const FColor& Colour) const
		{
			Fill(FIntRect(Rect.Min - 1, Rect.Max + 1), FColor::Black);
			Fill(Rect, Colour);
		}

		void Text(FIntPoint Position, const FString& String) const
		{
			Canvas.DrawShadowedString(Position.X / Scale, Position.Y / Scale, String, GetStatsFont(), FLinearColor::White);
		}

	private:
		FCanvas& Canvas;
		float Scale;
	};

	// Dark enough to read white text on over any picture, and still let the picture show through.
	const FColor LabelBacking(0, 0, 0, 176);
}

FUEcineMonitorViewExtension::FUEcineMonitorViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

void FUEcineMonitorViewExtension::SetSettings(const FUEcineMonitorSettings& InSettings)
{
	check(IsInGameThread());
	Settings = InSettings;

	FLabels Labels;
	Labels.WaveformTitle = LOCTEXT("WaveformTitle", "Waveform").ToString();
	Labels.HistogramTitle = LOCTEXT("HistogramTitle", "Histogram").ToString();
	for (const FUEcineFalseColourBand& Band : UEcine::Monitor::GetFalseColourBands())
	{
		Labels.FalseColourLegend.Add(FText::Format(LOCTEXT("LegendEntry", "{0}  {1}"), Band.Name, UEcine::Monitor::GetFalseColourRange(Band)).ToString());
	}

	ENQUEUE_RENDER_COMMAND(UEcineMonitorSettings)(
		[WeakThis = AsWeak(), InSettings, Labels = MoveTemp(Labels)](FRHICommandListImmediate&) mutable
		{
			if (const TSharedPtr<FSceneViewExtensionBase, ESPMode::ThreadSafe> This = WeakThis.Pin())
			{
				FUEcineMonitorViewExtension& Extension = static_cast<FUEcineMonitorViewExtension&>(*This);
				Extension.Settings_RenderThread = InSettings;
				Extension.Labels_RenderThread = MoveTemp(Labels);
			}
		});
}

bool FUEcineMonitorViewExtension::SetCameraOverlays(const TMap<const FRenderTarget*, FUEcineMonitorCameraOverlay>& InOverlays)
{
	check(IsInGameThread());
	if (CameraOverlays.OrderIndependentCompareEqual(InOverlays))
	{
		return false;
	}
	CameraOverlays = InOverlays;

	ENQUEUE_RENDER_COMMAND(UEcineMonitorCameraOverlays)(
		[WeakThis = AsWeak(), Overlays = InOverlays](FRHICommandListImmediate&) mutable
		{
			if (const TSharedPtr<FSceneViewExtensionBase, ESPMode::ThreadSafe> This = WeakThis.Pin())
			{
				static_cast<FUEcineMonitorViewExtension&>(*This).CameraOverlays_RenderThread = MoveTemp(Overlays);
			}
		});
	return true;
}

void FUEcineMonitorViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	// The tools measure the finished picture and must stay sharp themselves, so they go after the last
	// pass that changes it: tone mapping, or the anti-aliasing that follows tone mapping when the view uses one.
	EPostProcessingPass LastPicturePass = EPostProcessingPass::Tonemap;
	if (InView.AntiAliasingMethod == AAM_FXAA)
	{
		LastPicturePass = EPostProcessingPass::FXAA;
	}
	else if (InView.AntiAliasingMethod == AAM_SMAA)
	{
		LastPicturePass = EPostProcessingPass::SMAA;
	}

	if (Pass == LastPicturePass && bIsPassEnabled && Settings_RenderThread.IsAnyToolOn() && InView.GetFeatureLevel() >= ERHIFeatureLevel::SM5)
	{
		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FUEcineMonitorViewExtension::AddPicturePass_RenderThread));
	}
}

FScreenPassTexture FUEcineMonitorViewExtension::AddPicturePass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FUEcineMonitorSettings& ToolSettings = Settings_RenderThread;
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	check(SceneColor.IsValid());

	// An output is handed in when this is the last pass of post processing.
	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, View.GetOverwriteLoadAction(), TEXT("UEcineMonitor.Picture"));
	}

	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	const FIntPoint ViewSize = SceneColor.ViewRect.Size();
	const FUEcineScopeLayout ScopeLayout = UEcine::Monitor::GetScopeLayout(ViewSize, ToolSettings.bWaveform, ToolSettings.bHistogram);
	const bool bScopes = ScopeLayout.Waveform.Area() > 0 || ScopeLayout.Histogram.Area() > 0;

	FUEcineMonitorPicturePS::FParameters* PictureParameters = GraphBuilder.AllocParameters<FUEcineMonitorPicturePS::FParameters>();

	if (bScopes)
	{
		const FRDGTextureRef WaveformTexture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(FIntPoint(ScopeColumns, ScopeLevels), PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("UEcineMonitor.Waveform"));
		const FRDGTextureRef HistogramTexture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(FIntPoint(ScopeLevels, 1), PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("UEcineMonitor.Histogram"));
		const FRDGTextureUAVRef WaveformUAV = GraphBuilder.CreateUAV(WaveformTexture);
		const FRDGTextureUAVRef HistogramUAV = GraphBuilder.CreateUAV(HistogramTexture);
		AddClearUAVPass(GraphBuilder, WaveformUAV, 0u);
		AddClearUAVPass(GraphBuilder, HistogramUAV, 0u);

		const int32 SampleStep = FMath::DivideAndRoundUp(ViewSize.X, ScopeFullSampleWidth);
		// Sample i is taken at pixel i * step + step / 2, so this many fit in the picture.
		const FIntPoint SampleCount(
			FMath::DivideAndRoundUp(ViewSize.X - SampleStep / 2, SampleStep),
			FMath::DivideAndRoundUp(ViewSize.Y - SampleStep / 2, SampleStep));

		FUEcineMonitorScopeCS::FParameters* ScopeParameters = GraphBuilder.AllocParameters<FUEcineMonitorScopeCS::FParameters>();
		ScopeParameters->SceneColorTexture = SceneColor.Texture;
		ScopeParameters->ViewMin = SceneColor.ViewRect.Min;
		ScopeParameters->ViewSize = ViewSize;
		ScopeParameters->SampleStep = SampleStep;
		ScopeParameters->RWWaveformTexture = WaveformUAV;
		ScopeParameters->RWHistogramTexture = HistogramUAV;
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("UEcineMonitor.Scopes"),
			TShaderMapRef<FUEcineMonitorScopeCS>(ShaderMap),
			ScopeParameters,
			FComputeShaderUtils::GetGroupCount(SampleCount, 8));

		const float TotalSamples = static_cast<float>(SampleCount.X) * SampleCount.Y;
		PictureParameters->WaveformTexture = WaveformTexture;
		PictureParameters->HistogramTexture = HistogramTexture;
		PictureParameters->WaveformRect = ToShaderRect(ScopeLayout.Waveform);
		PictureParameters->HistogramRect = ToShaderRect(ScopeLayout.Histogram);
		PictureParameters->WaveformScale = ScopeGain * ScopeColumns / TotalSamples;
		PictureParameters->HistogramScale = ScopeGain / TotalSamples;
	}

	const TConstArrayView<FUEcineFalseColourBand> Bands = UEcine::Monitor::GetFalseColourBands();
	check(Bands.Num() <= MaxFalseColourBands);
	for (int32 BandIndex = 0; BandIndex < MaxFalseColourBands; ++BandIndex)
	{
		const bool bUsed = BandIndex < Bands.Num();
		PictureParameters->FalseColourRanges[BandIndex] = bUsed ? FVector4f(Bands[BandIndex].FromPercent / 100.0f, Bands[BandIndex].ToPercent / 100.0f, 0.0f, 0.0f) : FVector4f(ForceInitToZero);
		PictureParameters->FalseColourColours[BandIndex] = bUsed ? FVector4f(ToShaderColour(Bands[BandIndex].Colour), 1.0f) : FVector4f(ForceInitToZero);
	}
	PictureParameters->FalseColourBandCount = Bands.Num();

	PictureParameters->Input = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(SceneColor));
	PictureParameters->SceneColorTexture = SceneColor.Texture;
	PictureParameters->SceneColorSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PictureParameters->Tools = (ToolSettings.bFalseColour ? ToolFalseColour : 0) | (ToolSettings.bZebras ? ToolZebras : 0) | (ToolSettings.bPeaking ? ToolPeaking : 0);
	PictureParameters->ZebraLevel = ToolSettings.ZebraLevelPercent / 100.0f;
	PictureParameters->PeakingColour = ToShaderColour(UEcine::Monitor::GetColour(ToolSettings.PeakingColour));
	PictureParameters->PeakingThreshold = ToolSettings.PeakingThresholdPercent / 100.0f;
	PictureParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FUEcineMonitorPicturePS::FPermutationDomain Permutation;
	Permutation.Set<FUEcineMonitorPicturePS::FScopes>(bScopes);
	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("UEcineMonitor.Picture"),
		FScreenPassViewInfo(View),
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(SceneColor),
		TShaderMapRef<FScreenPassVS>(ShaderMap),
		TShaderMapRef<FUEcineMonitorPicturePS>(ShaderMap, Permutation),
		PictureParameters);

	// Lines and words go over the finished picture, scopes included.
	Output.LoadAction = ERenderTargetLoadAction::ELoad;
	const FUEcineMonitorCameraOverlay* Camera = CameraOverlays_RenderThread.Find(View.Family->RenderTarget);
	AddDrawCanvasPass(GraphBuilder, RDG_EVENT_NAME("UEcineMonitor.Overlay"), View, Output, [this, ViewSize, &ScopeLayout, Camera](FCanvas& Canvas)
	{
		DrawOverlay_RenderThread(Canvas, ViewSize, ScopeLayout, Camera);
	});

	return MoveTemp(Output);
}

void FUEcineMonitorViewExtension::DrawOverlay_RenderThread(FCanvas& Canvas, FIntPoint PictureSize, const FUEcineScopeLayout& ScopeLayout, const FUEcineMonitorCameraOverlay* Camera) const
{
	const FUEcineMonitorSettings& ToolSettings = Settings_RenderThread;
	const FOverlayPainter Painter(Canvas);
	const FIntRect Picture(FIntPoint::ZeroValue, PictureSize);
	const FIntRect Frame = UEcine::Monitor::GetDeliveryFrame(PictureSize, Camera != nullptr ? Camera->DeliveryAspect : 0.0f);
	const FColor LineColour = UEcine::Monitor::GetColour(ToolSettings.LineColour);

	// Frame lines only say something when the delivery frame is not the whole picture.
	if (ToolSettings.bFrameLines && Frame != Picture)
	{
		Painter.Line(Frame, 2, LineColour);
	}
	if (ToolSettings.bSafeAreas)
	{
		Painter.Line(UEcine::Monitor::GetSafeArea(Frame, UEcine::Monitor::ActionSafeFraction), 1, LineColour);
		Painter.Line(UEcine::Monitor::GetSafeArea(Frame, UEcine::Monitor::TitleSafeFraction), 1, LineColour);
	}
	if (ToolSettings.bCentreMark)
	{
		const FIntPoint Centre = (Frame.Min + Frame.Max) / 2;
		const int32 Arm = FMath::Max(8, PictureSize.Y / 45);
		const FIntRect Across(Centre.X - Arm, Centre.Y - 1, Centre.X + Arm, Centre.Y + 1);
		const FIntRect Down(Centre.X - 1, Centre.Y - Arm, Centre.X + 1, Centre.Y + Arm);
		Painter.Fill(FIntRect(Across.Min - 1, Across.Max + 1), FColor::Black);
		Painter.Fill(FIntRect(Down.Min - 1, Down.Max + 1), FColor::Black);
		Painter.Fill(Across, LineColour);
		Painter.Fill(Down, LineColour);
	}

	// Words run down the top left corner: the camera data, then what the false colours mean.
	const int32 Margin = FMath::Max(8, PictureSize.Y / 67);
	const int32 Padding = Painter.ToPixels(4.0f);
	FIntPoint Cursor(Margin, Margin);
	if (ToolSettings.bDataReadout && Camera != nullptr && !Camera->DataReadout.IsEmpty())
	{
		const FIntPoint Size = Painter.Measure(Camera->DataReadout) + 2 * Padding;
		Painter.Fill(FIntRect(Cursor, Cursor + Size), LabelBacking);
		Painter.Text(Cursor + Padding, Camera->DataReadout);
		Cursor.Y += Size.Y + Padding;
	}
	if (ToolSettings.bFalseColour && !Labels_RenderThread.FalseColourLegend.IsEmpty())
	{
		const TConstArrayView<FUEcineFalseColourBand> Bands = UEcine::Monitor::GetFalseColourBands();
		int32 TextWidth = 0;
		int32 LineHeight = 0;
		for (const FString& Entry : Labels_RenderThread.FalseColourLegend)
		{
			const FIntPoint EntrySize = Painter.Measure(Entry);
			TextWidth = FMath::Max(TextWidth, EntrySize.X);
			LineHeight = FMath::Max(LineHeight, EntrySize.Y);
		}
		const int32 Swatch = LineHeight - 2;
		const int32 EntryCount = FMath::Min(Bands.Num(), Labels_RenderThread.FalseColourLegend.Num());
		const FIntPoint Size(Swatch + TextWidth + 3 * Padding, EntryCount * (LineHeight + Padding) + Padding);
		Painter.Fill(FIntRect(Cursor, Cursor + Size), LabelBacking);
		// Brightest first, as the bands sit on a waveform.
		for (int32 Row = 0; Row < EntryCount; ++Row)
		{
			const int32 BandIndex = EntryCount - 1 - Row;
			const FIntPoint RowMin = Cursor + FIntPoint(Padding, Padding + Row * (LineHeight + Padding));
			Painter.Bar(FIntRect(RowMin + FIntPoint(1, 2), RowMin + FIntPoint(Swatch - 1, Swatch)), Bands[BandIndex].Colour);
			Painter.Text(RowMin + FIntPoint(Swatch + Padding, 0), Labels_RenderThread.FalseColourLegend[BandIndex]);
		}
	}

	// Each scope is named in its top left corner, so the two are told apart without knowing their shapes.
	if (ScopeLayout.Waveform.Area() > 0)
	{
		Painter.Text(ScopeLayout.Waveform.Min + Padding, Labels_RenderThread.WaveformTitle);
	}
	if (ScopeLayout.Histogram.Area() > 0)
	{
		Painter.Text(ScopeLayout.Histogram.Min + Padding, Labels_RenderThread.HistogramTitle);
	}
}

#undef LOCTEXT_NAMESPACE
