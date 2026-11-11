#include "UEcineMonitorViewExtension.h"

#include "GlobalShader.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"

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
}

FUEcineMonitorViewExtension::FUEcineMonitorViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

void FUEcineMonitorViewExtension::SetSettings(const FUEcineMonitorSettings& InSettings)
{
	check(IsInGameThread());
	Settings = InSettings;
	ENQUEUE_RENDER_COMMAND(UEcineMonitorSettings)(
		[WeakThis = AsWeak(), InSettings](FRHICommandListImmediate&)
		{
			if (const TSharedPtr<FSceneViewExtensionBase, ESPMode::ThreadSafe> This = WeakThis.Pin())
			{
				StaticCastSharedPtr<FUEcineMonitorViewExtension>(This)->Settings_RenderThread = InSettings;
			}
		});
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

	if (Pass == LastPicturePass && bIsPassEnabled && Settings_RenderThread.UsesPicturePass() && InView.GetFeatureLevel() >= ERHIFeatureLevel::SM5)
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

	return MoveTemp(Output);
}
