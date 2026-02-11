#include "CinematicImageEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <vector>

#include "Utils.h"
#include "ofxsImageEffect.h"
#include "ofxsInteract.h"
#include "ofxsLog.h"
#include "ofxsMultiThread.h"
#include "ofxsProcessing.H"
#include "ofxsSupportPrivate.h"

// Plugin Definitions
#define kPluginName "Cinematic Image Engine"
#define kPluginGrouping "ColormetricLabs"
#define kPluginDescription "Modular cinematic image pipeline."
#define kPluginIdentifier "com.ColormetricLabs.CinematicImageEngine"
#define kPluginVersionMajor 1
#define kPluginVersionMinor 3

#define kSupportsTiles true
#define kSupportsMultiResolution false
#define kSupportsMultipleClipPARs false

////////////////////////////////////////////////////////////////////////////////
// Pipeline Processor
////////////////////////////////////////////////////////////////////////////////

class PipelineProcessor : public OFX::ImageProcessor {
public:
  explicit PipelineProcessor(OFX::ImageEffect &p_Instance)
      : OFX::ImageProcessor(p_Instance), _srcImg(nullptr), _renderScaleX(1.0),
        _time(0.0), _rod{0, 0, 0, 0} {}

  virtual void multiThreadProcessImages(OfxRectI p_ProcWindow);

  void setSrcImg(OFX::Image *p_SrcImg) { _srcImg = p_SrcImg; }
  void setRenderScale(double scaleX) { _renderScaleX = scaleX; }
  void setTime(double t) { _time = t; }
  void setSourceRoD(const OfxRectD &rod) { _rod = rod; }

  // Params
  ColorIngestTweaks::Params cit;
  FilmResponse::Params pcr;
  TonalEngine::Params tonal;
  ColorEnergyEngine::Params energy;
  HighlightProtection::Params hlp;
  SplitToning::Params split;
  FilmGrain::Params grain;

  Dither::Params dither;

  // Spatial
  DreamyMist::Params mist;
  DreamyBlur::Params blur;
  CinematicGlow::Params glow;
  AnamorphicStreak::Params streak;
  Sharpening::Params sharp;
  Halation::Params halo;
  ChromaticAberration::Params ca;
  Vignette::Params vig;

private:
  OFX::Image *_srcImg;
  double _renderScaleX;
  double _time;
  OfxRectD _rod;
};

void PipelineProcessor::multiThreadProcessImages(OfxRectI p_ProcWindow) {
  if (!_dstImg || !_srcImg)
    return;

  // ========================================================================
  // APRON CALCULATION
  // ========================================================================
  const float mistR = mist.enable ? 6.0f * (float)_renderScaleX : 0.0f;
  float blurR = blur.enable ? (float)(blur.blurRadius * _renderScaleX) : 0.0f;
  const float glowR = glow.enable ? (float)(glow.radius * _renderScaleX) : 0.0f;
  float haloR = halo.enable ? (float)(halo.radius * _renderScaleX) : 0.0f;
  const float sharpR = sharp.enable ? 2.0f : 0.0f;
  float defR = 0.0f;
  if (vig.enable && vig.type == Vignette::eDefocus) {
    defR = (float)(vig.defocusSoftness * 20.0 * _renderScaleX);
  }

  if (haloR > 50.0f)
    haloR = 50.0f;
  if (blurR < 0.0f)
    blurR = 0.0f;

  float totalR = 0.0f;
  if (mist.enable)
    totalR = std::max(totalR, mistR);
  if (blur.enable)
    totalR += blurR;
  if (halo.enable)
    totalR += haloR;
  if (glow.enable)
    totalR += glowR;
  if (sharp.enable)
    totalR += sharpR;
  if (defR > 0)
    totalR += defR;

  const int apron = (int)std::ceil(totalR) + 2;

  OfxRectI bufARect = p_ProcWindow;
  bufARect.x1 -= apron;
  bufARect.x2 += apron;
  bufARect.y1 -= apron;
  bufARect.y2 += apron;

  const int bufAW = bufARect.x2 - bufARect.x1;
  const int bufAH = bufARect.y2 - bufARect.y1;
  const int bufPixels = bufAW * bufAH;
  const int bufSize = bufPixels * 4;

  // ========================================================================
  // BUFFER ALLOCATION  –  single shared temp buffer for ALL blur operations
  // ========================================================================
  std::vector<float> bufA(bufSize);
  std::vector<float> bufB(bufSize);

  // Only allocate the blur scratch buffer if any spatial effect is enabled
  const bool anySpatial = mist.enable || blur.enable || glow.enable ||
                          streak.enable || sharp.enable || halo.enable ||
                          ca.enable;
  std::vector<float> bufTemp;
  if (anySpatial) {
    bufTemp.resize(bufSize);
  }

  const OfxRectI srcBounds = _srcImg->getBounds();
  auto getSrcPixel = [&](int x, int y, float *outR, float *outG, float *outB) {
    float *p = (float *)_srcImg->getPixelAddress(x, y);
    if (!p) {
      int cx = std::min(std::max(x, srcBounds.x1), srcBounds.x2 - 1);
      int cy = std::min(std::max(y, srcBounds.y1), srcBounds.y2 - 1);
      p = (float *)_srcImg->getPixelAddress(cx, cy);
    }
    if (p) {
      *outR = p[0];
      *outG = p[1];
      *outB = p[2];
    } else {
      *outR = 0;
      *outG = 0;
      *outB = 0;
    }
  };

  // ========================================================================
  // PRE-COMPUTE per-frame constants (moved out of pixel loop)
  // ========================================================================
  const int frameSeed = grain.enable ? (int)std::floor(_time * 24.0) : 0;
  const int imgW = (int)(_rod.x2 - _rod.x1);
  const int imgH = (int)(_rod.y2 - _rod.y1);

  // ========================================================================
  // STAGE 0: Per-Pixel Pipeline
  // Order: CIT -> PCR -> Tonal -> Energy -> HLP -> Split -> Grain
  // ========================================================================
  for (int y = 0; y < bufAH; ++y) {
    const int gy = bufARect.y1 + y;
    float *rowOut = &bufA[y * bufAW * 4];

    for (int x = 0; x < bufAW; ++x) {
      const int gx = bufARect.x1 + x;
      float r, g, b;
      getSrcPixel(gx, gy, &r, &g, &b);

      // 1. Color Ingest Tweaks
      if (cit.enable)
        ColorIngestTweaks::process(&r, &g, &b, cit);

      // 2. Photochemical Color Response
      if (pcr.enable)
        FilmResponse::processPixel(&r, &g, &b, pcr);

      // 3. Tonal Engine
      TonalEngine::processPixel(&r, &g, &b, tonal);

      // 4. Color Energy Engine
      if (energy.enable)
        ColorEnergyEngine::process(&r, &g, &b, energy);

      // 5. Highlight Protection
      HighlightProtection::processPixel(&r, &g, &b, hlp);

      // 6. Split Toning
      if (split.enable)
        SplitToning::processPixel(&r, &g, &b, split);

      // 7. Film Grain
      if (grain.enable)
        FilmGrain::applyGrain(&r, &g, &b, gx, gy, frameSeed, imgW, imgH, grain);

      // 8. Dither (banding reduction)
      if (dither.enable)
        Dither::process(&r, &g, &b, gx, gy, dither);

      // Store
      float *out = rowOut + x * 4;
      out[0] = r;
      out[1] = g;
      out[2] = b;
      out[3] = 1.0f;
    }
  }

  // ========================================================================
  // STAGE 1: Spatial Effects  –  reusing bufTemp across all effects
  // ========================================================================

  // Mist
  if (mist.enable) {
    const int r = std::max(1, (int)std::ceil(mistR));
    for (int i = 0; i < bufPixels; ++i) {
      const float *s = &bufA[i * 4];
      float mR, mG, mB;
      DreamyMist::computeMistSource(s[0], s[1], s[2], 0.0f, mR, mG, mB, mist);
      bufB[i * 4 + 0] = mR;
      bufB[i * 4 + 1] = mG;
      bufB[i * 4 + 2] = mB;
      bufB[i * 4 + 3] = 0.0f;
    }
    Utils::gaussianBlur(bufB.data(), bufB.data(), bufTemp.data(), bufAW, bufAH,
                        r);
    for (int i = 0; i < bufPixels; ++i) {
      float *d = &bufA[i * 4];
      const float *m = &bufB[i * 4];
      DreamyMist::applyMist(d[0], d[1], d[2], m[0], m[1], m[2], mist);
    }
  }

  // Dreamy Blur
  if (blur.enable) {
    const int r = std::max(1, (int)std::ceil(blurR));
    std::memcpy(bufB.data(), bufA.data(), bufSize * sizeof(float));
    Utils::gaussianBlur(bufB.data(), bufB.data(), bufTemp.data(), bufAW, bufAH,
                        r);
    for (int i = 0; i < bufPixels; ++i) {
      float *d = &bufA[i * 4];
      const float *bl = &bufB[i * 4];
      DreamyBlur::applyDreamyBlur(d[0], d[1], d[2], bl[0], bl[1], bl[2], 0.0f,
                                  blur);
    }
  }

  // Cinematic Glow
  if (glow.enable) {
    const int r = std::max(1, (int)std::ceil(glowR));
    for (int i = 0; i < bufPixels; ++i) {
      const float *s = &bufA[i * 4];
      float gR, gG, gB;
      CinematicGlow::computeGlowSource(s[0], s[1], s[2], gR, gG, gB, glow);
      bufB[i * 4 + 0] = gR;
      bufB[i * 4 + 1] = gG;
      bufB[i * 4 + 2] = gB;
      bufB[i * 4 + 3] = 0.0f;
    }
    Utils::gaussianBlur(bufB.data(), bufB.data(), bufTemp.data(), bufAW, bufAH,
                        r);
    for (int i = 0; i < bufPixels; ++i) {
      float *d = &bufA[i * 4];
      const float *gl = &bufB[i * 4];
      CinematicGlow::applyGlow(d[0], d[1], d[2], gl[0], gl[1], gl[2], glow);
    }
  }

  // Anamorphic Streak
  if (streak.enable) {
    const int sLen = std::max(1, (int)(streak.length * 80.0 * _renderScaleX));
    for (int i = 0; i < bufPixels; ++i) {
      const float *s = &bufA[i * 4];
      float sR, sG, sB;
      AnamorphicStreak::computeStreakSource(s[0], s[1], s[2], sR, sG, sB,
                                            streak);
      bufB[i * 4 + 0] = sR;
      bufB[i * 4 + 1] = sG;
      bufB[i * 4 + 2] = sB;
      bufB[i * 4 + 3] = 0.0f;
    }
    // Horizontal-only blur (3 passes for Gaussian approximation)
    // Ping-pong between bufB and bufTemp to avoid in-place aliasing
    AnamorphicStreak::boxBlurH1D(bufB.data(), bufTemp.data(), bufAW, bufAH,
                                 sLen);
    AnamorphicStreak::boxBlurH1D(bufTemp.data(), bufB.data(), bufAW, bufAH,
                                 sLen);
    AnamorphicStreak::boxBlurH1D(bufB.data(), bufTemp.data(), bufAW, bufAH,
                                 sLen);
    // Result is in bufTemp — copy back to bufB for the apply step
    std::memcpy(bufB.data(), bufTemp.data(), bufSize * sizeof(float));
    for (int i = 0; i < bufPixels; ++i) {
      float *d = &bufA[i * 4];
      AnamorphicStreak::applyStreak(d[0], d[1], d[2], bufB[i * 4],
                                    bufB[i * 4 + 1], bufB[i * 4 + 2], streak);
    }
  }

  // Sharpening
  if (sharp.enable) {
    const int r = std::max(1, (int)std::ceil(sharpR));
    std::memcpy(bufB.data(), bufA.data(), bufSize * sizeof(float));
    Utils::gaussianBlur(bufB.data(), bufB.data(), bufTemp.data(), bufAW, bufAH,
                        r);
    for (int i = 0; i < bufPixels; ++i) {
      float *d = &bufA[i * 4];
      const float *bl = &bufB[i * 4];
      Sharpening::applySharpen(d[0], d[1], d[2], bl[0], bl[1], bl[2], sharp);
    }
  }

  // Halation
  if (halo.enable) {
    const int r = std::max(1, (int)std::ceil(haloR));
    for (int i = 0; i < bufPixels; ++i) {
      const float *s = &bufA[i * 4];
      float hR, hG, hB;
      Halation::computeHalationSource(s[0], s[1], s[2], 0, hR, hG, hB, halo);
      bufB[i * 4 + 0] = hR;
      bufB[i * 4 + 1] = hG;
      bufB[i * 4 + 2] = hB;
      bufB[i * 4 + 3] = 0.0f;
    }
    Utils::gaussianBlur(bufB.data(), bufB.data(), bufTemp.data(), bufAW, bufAH,
                        r);
    for (int i = 0; i < bufPixels; ++i) {
      float *d = &bufA[i * 4];
      const float *h = &bufB[i * 4];
      Halation::applyHalation(d, d + 1, d + 2, h[0], h[1], h[2], halo);
    }
  }

  // Chromatic Aberration
  if (ca.enable) {
    std::memcpy(bufB.data(), bufA.data(), bufSize * sizeof(float));
    ChromaticAberration::process(bufB.data(), bufA.data(), bufAW, bufAH,
                                 (float)_rod.x1, (float)_rod.y1, (float)imgW,
                                 (float)imgH, bufARect.x1, bufARect.y1, ca);
  }

  // Vignette
  if (vig.enable) {
    const float vImgW = (float)imgW;
    const float vImgH = (float)imgH;
    const float aspect = vImgW / std::max(1.0f, vImgH);
    const float invW = 1.0f / vImgW;
    const float invH = 1.0f / vImgH;
    const float rodX1 = (float)_rod.x1;
    const float rodY1 = (float)_rod.y1;

    for (int y = 0; y < bufAH; ++y) {
      const float v = ((float)(bufARect.y1 + y) - rodY1) * invH;
      for (int x = 0; x < bufAW; ++x) {
        const float u = ((float)(bufARect.x1 + x) - rodX1) * invW;
        const float V = Vignette::computeMask(u, v, aspect, vig);
        float *d = &bufA[(y * bufAW + x) * 4];
        Vignette::processPixel(d, d + 1, d + 2, V, 0.0f, vig);
      }
    }
  }

  // ========================================================================
  // FINAL OUTPUT  –  copy from apron buffer to destination
  // ========================================================================
  const int dstWidth = p_ProcWindow.x2 - p_ProcWindow.x1;
  const int dstHeight = p_ProcWindow.y2 - p_ProcWindow.y1;
  const int axOff = p_ProcWindow.x1 - bufARect.x1;
  const int ayOff = p_ProcWindow.y1 - bufARect.y1;
  const size_t rowBytes = (size_t)dstWidth * 4 * sizeof(float);

  for (int y = 0; y < dstHeight; ++y) {
    float *dstPix =
        (float *)_dstImg->getPixelAddress(p_ProcWindow.x1, p_ProcWindow.y1 + y);
    if (!dstPix)
      continue;
    const float *srcRow = &bufA[((ayOff + y) * bufAW + axOff) * 4];
    // Use memcpy for bulk row transfer — the alpha channel is already 1.0
    // from Stage 0, so we can copy all 4 channels directly.
    std::memcpy(dstPix, srcRow, rowBytes);
  }
}

////////////////////////////////////////////////////////////////////////////////
// CinematicPlugin
////////////////////////////////////////////////////////////////////////////////

CinematicPlugin::CinematicPlugin(OfxImageEffectHandle p_Handle)
    : ImageEffect(p_Handle) {
  m_DstClip = fetchClip(kOfxImageEffectOutputClipName);
  m_SrcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);

  // Fetch parameters matching defineInContext
  // 1. CIT
  m_EnableCIT = fetchBooleanParam("EnableCIT");
  m_CITExposure = fetchDoubleParam("CITExposure");
  m_CITChromaCeiling = fetchDoubleParam("CITChromaCeiling");
  m_CITWhiteBias = fetchDoubleParam("CITWhiteBias");
  m_CITTemperature = fetchDoubleParam("CITTemperature");
  m_CITTint = fetchDoubleParam("CITTint");
  m_CITGlobalSat = fetchDoubleParam("CITGlobalSat");

  // 2. PCR
  m_EnablePCR = fetchBooleanParam("EnablePCR");
  m_PCRAmount = fetchDoubleParam("PCRAmount");
  m_PCRShadowCoolBias = fetchDoubleParam("PCRShadowCoolBias");
  m_PCRMidtoneColorFocus = fetchDoubleParam("PCRMidtoneColorFocus");
  m_PCRHighlightWarmth = fetchDoubleParam("PCRHighlightWarmth");
  m_PCRHighlightCompression = fetchDoubleParam("PCRHighlightCompression");
  m_PCRPreset = fetchChoiceParam("PCRPreset");
  m_PCRCrossProcess = fetchBooleanParam("PCRCrossProcess");

  // 3. Tonal
  m_EnableTonal = fetchBooleanParam("EnableTonal");
  m_TonalContrast = fetchDoubleParam("TonalContrast");
  m_TonalPivot = fetchDoubleParam("TonalPivot");
  m_TonalStrength = fetchDoubleParam("TonalStrength");
  m_TonalBlackFloor = fetchDoubleParam("TonalBlackFloor");
  m_TonalHighContrast = fetchDoubleParam("TonalHighContrast");
  m_TonalSoftClip = fetchDoubleParam("TonalSoftClip");

  // 4. Energy
  m_EnableEnergy = fetchBooleanParam("EnableEnergy");
  m_EnergyDensity = fetchDoubleParam("EnergyDensity");
  m_EnergySeparation = fetchDoubleParam("EnergySeparation");
  m_EnergyHighRollOff = fetchDoubleParam("EnergyHighRollOff");
  m_EnergyShadowBias = fetchDoubleParam("EnergyShadowBias");
  m_EnergyVibrance = fetchDoubleParam("EnergyVibrance");

  // 5. HLP
  m_EnableHLP = fetchBooleanParam("EnableHLP");
  m_HLPThreshold = fetchDoubleParam("HLPThreshold");
  m_HLPRolloff = fetchDoubleParam("HLPRolloff");
  m_HLPPreserveColor = fetchBooleanParam("HLPPreserveColor");

  // 6. Split
  m_EnableSplit = fetchBooleanParam("EnableSplit");
  m_SplitStrength = fetchDoubleParam("SplitStrength");
  m_SplitShadowHue = fetchDoubleParam("SplitShadowHue");
  m_SplitHighlightHue = fetchDoubleParam("SplitHighlightHue");
  m_SplitBalance = fetchDoubleParam("SplitBalance");
  m_SplitMidtoneHue = fetchDoubleParam("SplitMidtoneHue");
  m_SplitMidtoneSat = fetchDoubleParam("SplitMidtoneSat");

  // 7. Grain
  m_EnableGrain = fetchBooleanParam("EnableGrain");
  m_GrainType = fetchChoiceParam("GrainType");
  m_GrainAmount = fetchDoubleParam("GrainAmount");
  m_GrainSize = fetchDoubleParam("GrainSize");
  m_GrainShadowWeight = fetchDoubleParam("GrainShadowWeight");
  m_GrainMidWeight = fetchDoubleParam("GrainMidWeight");
  m_GrainHighlightWeight = fetchDoubleParam("GrainHighlightWeight");
  m_GrainChromatic = fetchBooleanParam("GrainChromatic");
  m_GrainTemporalSpeed = fetchDoubleParam("GrainTemporalSpeed");

  // 8. Dither
  m_EnableDither = fetchBooleanParam("EnableDither");
  m_DitherAmount = fetchDoubleParam("DitherAmount");

  // 9. Spatial
  m_EnableMist = fetchBooleanParam("EnableMist");
  m_MistAmount = fetchDoubleParam("MistAmount");
  m_MistThreshold = fetchDoubleParam("MistThreshold");
  m_MistSoftness = fetchDoubleParam("MistSoftness");
  m_MistDepthBias = fetchDoubleParam("MistDepthBias");
  m_MistWarmth = fetchDoubleParam("MistWarmth");

  m_EnableBlur = fetchBooleanParam("EnableBlur");
  m_BlurRadius = fetchDoubleParam("BlurRadius");
  m_BlurStrength = fetchDoubleParam("BlurStrength");
  m_BlurShadowAmt = fetchDoubleParam("BlurShadowAmt");
  m_BlurHighlightAmt = fetchDoubleParam("BlurHighlightAmt");
  m_BlurTonalSoft = fetchDoubleParam("BlurTonalSoft");
  m_BlurSat = fetchDoubleParam("BlurSat");

  m_EnableGlow = fetchBooleanParam("EnableGlow");
  m_GlowAmount = fetchDoubleParam("GlowAmount");
  m_GlowThreshold = fetchDoubleParam("GlowThreshold");
  m_GlowKnee = fetchDoubleParam("GlowKnee");
  m_GlowRadius = fetchDoubleParam("GlowRadius");
  m_GlowFidelity = fetchDoubleParam("GlowFidelity");
  m_GlowWarmth = fetchDoubleParam("GlowWarmth");

  m_EnableSharp = fetchBooleanParam("EnableSharp");
  m_SharpType = fetchChoiceParam("SharpType");
  m_SharpAmount = fetchDoubleParam("SharpAmount");
  m_SharpRadius = fetchDoubleParam("SharpRadius");
  m_SharpDetail = fetchDoubleParam("SharpDetail");
  m_SharpEdgeProt = fetchDoubleParam("SharpEdgeProt");
  m_SharpNoiseSupp = fetchDoubleParam("SharpNoiseSupp");
  m_SharpShadowProt = fetchDoubleParam("SharpShadowProt");
  m_SharpHighProt = fetchDoubleParam("SharpHighProt");

  m_EnableHalo = fetchBooleanParam("EnableHalo");
  m_HaloAmount = fetchDoubleParam("HaloAmount");
  m_HaloThreshold = fetchDoubleParam("HaloThreshold");
  m_HaloKnee = fetchDoubleParam("HaloKnee");
  m_HaloWarmth = fetchDoubleParam("HaloWarmth");
  m_HaloRadius = fetchDoubleParam("HaloRadius");
  m_HaloSat = fetchDoubleParam("HaloSat");
  m_HaloHueShift = fetchDoubleParam("HaloHueShift");

  // Streak
  m_EnableStreak = fetchBooleanParam("EnableStreak");
  m_StreakAmount = fetchDoubleParam("StreakAmount");
  m_StreakThreshold = fetchDoubleParam("StreakThreshold");
  m_StreakLength = fetchDoubleParam("StreakLength");
  m_StreakTint = fetchDoubleParam("StreakTint");

  // Chromatic Aberration
  m_EnableCA = fetchBooleanParam("EnableCA");
  m_CAAmount = fetchDoubleParam("CAAmount");
  m_CACenterX = fetchDoubleParam("CACenterX");
  m_CACenterY = fetchDoubleParam("CACenterY");

  // Vignette
  m_EnableVignette = fetchBooleanParam("EnableVignette");
  m_VignetteType = fetchChoiceParam("VignetteType");
  m_VignetteAmount = fetchDoubleParam("VignetteAmount");
  m_VignetteInvert = fetchBooleanParam("VignetteInvert");
  m_VignetteSize = fetchDoubleParam("VignetteSize");
  m_VignetteRoundness = fetchDoubleParam("VignetteRoundness");
  m_VignetteSoftness = fetchDoubleParam("VignetteSoftness");
  m_VignetteDefocus = fetchDoubleParam("VignetteDefocus");
  m_VignetteDefocusSoft = fetchDoubleParam("VignetteDefocusSoft");
  m_VignetteCenterX = fetchDoubleParam("VignetteCenterX");
  m_VignetteCenterY = fetchDoubleParam("VignetteCenterY");
  m_VignetteTintR = fetchDoubleParam("VignetteTintR");
  m_VignetteTintG = fetchDoubleParam("VignetteTintG");
  m_VignetteTintB = fetchDoubleParam("VignetteTintB");
}

void CinematicPlugin::render(const OFX::RenderArguments &p_Args) {
  if ((m_DstClip->getPixelDepth() == OFX::eBitDepthFloat) &&
      (m_DstClip->getPixelComponents() == OFX::ePixelComponentRGBA)) {
    PipelineProcessor processor(*this);
    processor.setRenderScale(p_Args.renderScale.x);
    processor.setDstImg(m_DstClip->fetchImage(p_Args.time));
    processor.setSrcImg(m_SrcClip->fetchImage(p_Args.time));
    processor.setRenderWindow(p_Args.renderWindow);
    processor.setTime(p_Args.time);
    processor.setSourceRoD(m_SrcClip->getRegionOfDefinition(p_Args.time));

    // Populate Params
    double t = p_Args.time;

    processor.cit.enable = m_EnableCIT->getValueAtTime(t);
    processor.cit.exposureTrim = m_CITExposure->getValueAtTime(t);
    processor.cit.chromaCeiling = m_CITChromaCeiling->getValueAtTime(t);
    processor.cit.whiteBias = m_CITWhiteBias->getValueAtTime(t);
    processor.cit.temperature = m_CITTemperature->getValueAtTime(t);
    processor.cit.tint = m_CITTint->getValueAtTime(t);
    processor.cit.globalSaturation = m_CITGlobalSat->getValueAtTime(t);

    processor.pcr.enable = m_EnablePCR->getValueAtTime(t);
    processor.pcr.amount = m_PCRAmount->getValueAtTime(t);
    processor.pcr.shadowCoolBias = m_PCRShadowCoolBias->getValueAtTime(t);
    processor.pcr.midtoneColorFocus = m_PCRMidtoneColorFocus->getValueAtTime(t);
    processor.pcr.highlightWarmth = m_PCRHighlightWarmth->getValueAtTime(t);
    processor.pcr.highlightCompression =
        m_PCRHighlightCompression->getValueAtTime(t);
    int pcrPreset = 0;
    m_PCRPreset->getValueAtTime(t, pcrPreset);
    processor.pcr.preset = pcrPreset;
    processor.pcr.crossProcess = m_PCRCrossProcess->getValueAtTime(t);

    processor.tonal.contrast = m_TonalContrast->getValueAtTime(t);
    processor.tonal.pivot = m_TonalPivot->getValueAtTime(t);
    processor.tonal.strength = m_TonalStrength->getValueAtTime(t);
    if (!m_EnableTonal->getValueAtTime(t))
      processor.tonal.strength = 0;
    processor.tonal.blackFloor = m_TonalBlackFloor->getValueAtTime(t);
    processor.tonal.highlightContrast = m_TonalHighContrast->getValueAtTime(t);
    processor.tonal.softClip = m_TonalSoftClip->getValueAtTime(t);

    processor.energy.enable = m_EnableEnergy->getValueAtTime(t);
    processor.energy.density = m_EnergyDensity->getValueAtTime(t);
    processor.energy.separation = m_EnergySeparation->getValueAtTime(t);
    processor.energy.highlightRollOff = m_EnergyHighRollOff->getValueAtTime(t);
    processor.energy.shadowBias = m_EnergyShadowBias->getValueAtTime(t);
    processor.energy.vibrance = m_EnergyVibrance->getValueAtTime(t);

    processor.hlp.threshold = m_HLPThreshold->getValueAtTime(t);
    processor.hlp.rolloff = m_HLPRolloff->getValueAtTime(t);
    processor.hlp.preserveColor = m_HLPPreserveColor->getValueAtTime(t);
    if (!m_EnableHLP->getValueAtTime(t))
      processor.hlp.threshold = 100.0;

    processor.split.enable = m_EnableSplit->getValueAtTime(t);
    processor.split.strength = (float)m_SplitStrength->getValueAtTime(t);
    processor.split.shadowHue = (float)m_SplitShadowHue->getValueAtTime(t);
    processor.split.highlightHue =
        (float)m_SplitHighlightHue->getValueAtTime(t);
    processor.split.balance = (float)m_SplitBalance->getValueAtTime(t);
    processor.split.midtoneHue = (float)m_SplitMidtoneHue->getValueAtTime(t);
    processor.split.midtoneSaturation =
        (float)m_SplitMidtoneSat->getValueAtTime(t);
    // Pre-compute sin/cos hue vectors once per frame (not per pixel)
    if (processor.split.enable) {
      SplitToning::precomputeVectors(processor.split);
    }

    processor.grain.enable = m_EnableGrain->getValueAtTime(t);
    int gType = 0;
    m_GrainType->getValueAtTime(t, gType);
    processor.grain.grainType = gType;
    processor.grain.amount = (float)m_GrainAmount->getValueAtTime(t);
    processor.grain.size = (float)m_GrainSize->getValueAtTime(t);
    processor.grain.shadowWeight =
        (float)m_GrainShadowWeight->getValueAtTime(t);
    processor.grain.midWeight = (float)m_GrainMidWeight->getValueAtTime(t);
    processor.grain.highlightWeight =
        (float)m_GrainHighlightWeight->getValueAtTime(t);
    processor.grain.chromatic = m_GrainChromatic->getValueAtTime(t);
    processor.grain.temporalSpeed =
        (float)m_GrainTemporalSpeed->getValueAtTime(t);

    processor.dither.enable = m_EnableDither->getValueAtTime(t);
    processor.dither.amount = m_DitherAmount->getValueAtTime(t);

    processor.mist.enable = m_EnableMist->getValueAtTime(t);
    processor.mist.strength = m_MistAmount->getValueAtTime(t);
    processor.mist.threshold = m_MistThreshold->getValueAtTime(t);
    processor.mist.softness = m_MistSoftness->getValueAtTime(t);
    processor.mist.depthBias = m_MistDepthBias->getValueAtTime(t);
    processor.mist.colorBias = m_MistWarmth->getValueAtTime(t);

    processor.blur.enable = m_EnableBlur->getValueAtTime(t);
    processor.blur.blurRadius = m_BlurRadius->getValueAtTime(t);
    processor.blur.strength = m_BlurStrength->getValueAtTime(t);
    processor.blur.shadowAmt = m_BlurShadowAmt->getValueAtTime(t);
    processor.blur.highlightAmt = m_BlurHighlightAmt->getValueAtTime(t);
    processor.blur.tonalSoftness = m_BlurTonalSoft->getValueAtTime(t);
    processor.blur.saturation = m_BlurSat->getValueAtTime(t);

    processor.glow.enable = m_EnableGlow->getValueAtTime(t);
    processor.glow.amount = m_GlowAmount->getValueAtTime(t);
    processor.glow.threshold = m_GlowThreshold->getValueAtTime(t);
    processor.glow.knee = m_GlowKnee->getValueAtTime(t);
    processor.glow.radius = m_GlowRadius->getValueAtTime(t);
    processor.glow.colorFidelity = m_GlowFidelity->getValueAtTime(t);
    processor.glow.warmth = m_GlowWarmth->getValueAtTime(t);

    processor.sharp.enable = m_EnableSharp->getValueAtTime(t);
    int sType = 0;
    m_SharpType->getValueAtTime(t, sType);
    processor.sharp.type = sType;
    processor.sharp.amount = m_SharpAmount->getValueAtTime(t);
    processor.sharp.radius = m_SharpRadius->getValueAtTime(t);
    processor.sharp.detailAmount = m_SharpDetail->getValueAtTime(t);
    processor.sharp.edgeProtection = m_SharpEdgeProt->getValueAtTime(t);
    processor.sharp.noiseSuppression = m_SharpNoiseSupp->getValueAtTime(t);
    processor.sharp.shadowProtection = m_SharpShadowProt->getValueAtTime(t);
    processor.sharp.highlightProtection = m_SharpHighProt->getValueAtTime(t);

    processor.halo.enable = m_EnableHalo->getValueAtTime(t);
    processor.halo.amount = m_HaloAmount->getValueAtTime(t);
    processor.halo.threshold = m_HaloThreshold->getValueAtTime(t);
    processor.halo.knee = m_HaloKnee->getValueAtTime(t);
    processor.halo.warmth = m_HaloWarmth->getValueAtTime(t);
    processor.halo.radius = m_HaloRadius->getValueAtTime(t);
    processor.halo.saturation = m_HaloSat->getValueAtTime(t);
    processor.halo.hueShift = m_HaloHueShift->getValueAtTime(t);

    processor.streak.enable = m_EnableStreak->getValueAtTime(t);
    processor.streak.amount = m_StreakAmount->getValueAtTime(t);
    processor.streak.threshold = m_StreakThreshold->getValueAtTime(t);
    processor.streak.length = m_StreakLength->getValueAtTime(t);
    processor.streak.tint = m_StreakTint->getValueAtTime(t);

    processor.ca.enable = m_EnableCA->getValueAtTime(t);
    processor.ca.amount = m_CAAmount->getValueAtTime(t);
    processor.ca.centerX = m_CACenterX->getValueAtTime(t);
    processor.ca.centerY = m_CACenterY->getValueAtTime(t);

    processor.vig.enable = m_EnableVignette->getValueAtTime(t);
    int vType = 0;
    m_VignetteType->getValueAtTime(t, vType);
    processor.vig.type = vType;
    processor.vig.amount = m_VignetteAmount->getValueAtTime(t);
    processor.vig.invert = m_VignetteInvert->getValueAtTime(t);
    processor.vig.size = m_VignetteSize->getValueAtTime(t);
    processor.vig.roundness = m_VignetteRoundness->getValueAtTime(t);
    processor.vig.edgeSoftness = m_VignetteSoftness->getValueAtTime(t);
    processor.vig.defocusAmount = m_VignetteDefocus->getValueAtTime(t);
    processor.vig.defocusSoftness = m_VignetteDefocusSoft->getValueAtTime(t);
    processor.vig.centerX = m_VignetteCenterX->getValueAtTime(t);
    processor.vig.centerY = m_VignetteCenterY->getValueAtTime(t);
    processor.vig.tintR = m_VignetteTintR->getValueAtTime(t);
    processor.vig.tintG = m_VignetteTintG->getValueAtTime(t);
    processor.vig.tintB = m_VignetteTintB->getValueAtTime(t);

    processor.process();
  } else {
    OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
  }
}

bool CinematicPlugin::isIdentity(const OFX::IsIdentityArguments &p_Args,
                                 OFX::Clip *&p_IdentityClip,
                                 double &p_IdentityTime) {
  double t = p_Args.time;

  // Check if ALL modules are at their identity/default state.
  // If everything is disabled or at neutral settings, pass source through.
  bool citActive = m_EnableCIT->getValueAtTime(t);
  bool pcrActive =
      m_EnablePCR->getValueAtTime(t) && m_PCRAmount->getValueAtTime(t) > 0.0;
  // Tonal is identity when strength == 0
  bool tonalActive = m_EnableTonal->getValueAtTime(t) &&
                     m_TonalStrength->getValueAtTime(t) > 0.0;
  bool energyActive = m_EnableEnergy->getValueAtTime(t);
  // HLP is identity when threshold >= 100 (way above any pixel value)
  bool hlpActive = m_EnableHLP->getValueAtTime(t) &&
                   m_HLPThreshold->getValueAtTime(t) < 100.0;
  bool splitActive = m_EnableSplit->getValueAtTime(t) &&
                     m_SplitStrength->getValueAtTime(t) > 0.0;
  bool grainActive = m_EnableGrain->getValueAtTime(t) &&
                     m_GrainAmount->getValueAtTime(t) > 0.0;
  bool ditherActive = m_EnableDither->getValueAtTime(t) &&
                      m_DitherAmount->getValueAtTime(t) > 0.0;
  bool mistActive = m_EnableMist->getValueAtTime(t);
  bool blurActive = m_EnableBlur->getValueAtTime(t);
  bool glowActive = m_EnableGlow->getValueAtTime(t);
  bool streakActive = m_EnableStreak->getValueAtTime(t) &&
                      m_StreakAmount->getValueAtTime(t) > 0.0;
  bool sharpActive = m_EnableSharp->getValueAtTime(t);
  bool haloActive = m_EnableHalo->getValueAtTime(t);
  bool caActive =
      m_EnableCA->getValueAtTime(t) && m_CAAmount->getValueAtTime(t) > 0.0;
  bool vigActive = m_EnableVignette->getValueAtTime(t);

  // CIT is identity when all its controls are at defaults
  if (citActive) {
    bool citNeutral = (m_CITExposure->getValueAtTime(t) == 0.0 &&
                       m_CITChromaCeiling->getValueAtTime(t) >= 1.0 &&
                       m_CITWhiteBias->getValueAtTime(t) == 0.0 &&
                       m_CITTemperature->getValueAtTime(t) == 0.0 &&
                       m_CITTint->getValueAtTime(t) == 0.0 &&
                       m_CITGlobalSat->getValueAtTime(t) == 1.0);
    citActive = !citNeutral;
  }

  if (!citActive && !pcrActive && !tonalActive && !energyActive && !hlpActive &&
      !splitActive && !grainActive && !ditherActive && !mistActive &&
      !blurActive && !glowActive && !streakActive && !sharpActive &&
      !haloActive && !caActive && !vigActive) {
    p_IdentityClip = m_SrcClip;
    p_IdentityTime = t;
    return true;
  }

  return false;
}

void CinematicPlugin::changedParam(const OFX::InstanceChangedArgs &p_Args,
                                   const std::string &p_ParamName) {
  // Handle Grain Preset updates?
  // "Initializing sliders" logic.
  // If GrainType changes, we set the sliders.
  if (p_ParamName == "GrainType") {
    int gType = 0;
    m_GrainType->getValueAtTime(p_Args.time, gType);

    // Default values for presets
    double amt = 0, sz = 0, sh = 1, md = 1, hi = 1;
    bool set = true;

    // Note: These must match what we think "initializing" means.
    // If the user selects "8mm", we set Amount=0.5, Size=0.5, etc.
    // User can then tweak them.
    switch (gType) {
    case 1: // 8mm
      amt = 0.7;
      sz = 0.8;
      sh = 0.8;
      md = 0.6;
      hi = 0.2;
      break;
    case 2: // 16mm
      amt = 0.5;
      sz = 0.6;
      sh = 0.6;
      md = 0.6;
      hi = 0.3;
      break;
    case 3: // S16
      amt = 0.4;
      sz = 0.5;
      sh = 0.5;
      md = 0.5;
      hi = 0.5;
      break;
    case 4: // 35mm
      amt = 0.25;
      sz = 0.3;
      sh = 0.3;
      md = 0.6;
      hi = 0.4;
      break;
    case 5: // 65mm
      amt = 0.15;
      sz = 0.2;
      sh = 0.2;
      md = 0.5;
      hi = 0.3;
      break;
    case 6: // Clean
      amt = 0.0;
      sz = 0.1;
      sh = 0.5;
      md = 0.5;
      hi = 0.5;
      break;
    default:
      set = false;
      break; // Custom
    }

    if (set) {
      m_GrainAmount->setValue(amt);
      m_GrainSize->setValue(sz);
      m_GrainShadowWeight->setValue(sh);
      m_GrainMidWeight->setValue(md);
      m_GrainHighlightWeight->setValue(hi);
    }
  }
}

void CinematicPlugin::getRegionsOfInterest(
    const OFX::RegionsOfInterestArguments &p_Args,
    OFX::RegionOfInterestSetter &p_ROIS) {
  double mistR = m_EnableMist->getValueAtTime(p_Args.time) ? 6.0 : 0.0;
  double blurR = m_EnableBlur->getValueAtTime(p_Args.time)
                     ? m_BlurRadius->getValueAtTime(p_Args.time)
                     : 0.0;
  double glowR = m_EnableGlow->getValueAtTime(p_Args.time)
                     ? m_GlowRadius->getValueAtTime(p_Args.time)
                     : 0.0;
  double haloR = m_EnableHalo->getValueAtTime(p_Args.time)
                     ? m_HaloRadius->getValueAtTime(p_Args.time)
                     : 0.0;
  double sharpR = m_EnableSharp->getValueAtTime(p_Args.time) ? 2.0 : 0.0;
  double streakR = m_EnableStreak->getValueAtTime(p_Args.time)
                       ? m_StreakLength->getValueAtTime(p_Args.time) * 80.0
                       : 0.0;
  double caR = m_EnableCA->getValueAtTime(p_Args.time)
                   ? m_CAAmount->getValueAtTime(p_Args.time) * 20.0
                   : 0.0;

  double total = mistR + blurR + glowR + haloR + sharpR + streakR + caR + 10.0;

  OfxRectD srcRect = p_Args.regionOfInterest;
  srcRect.x1 -= total;
  srcRect.x2 += total;
  srcRect.y1 -= total;
  srcRect.y2 += total;
  p_ROIS.setRegionOfInterest(*m_SrcClip, srcRect);
}

// Factory
CinematicPluginFactory::CinematicPluginFactory()
    : OFX::PluginFactoryHelper<CinematicPluginFactory>(
          kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor) {}

void CinematicPluginFactory::describe(OFX::ImageEffectDescriptor &p_Desc) {
  p_Desc.setLabels(kPluginName, kPluginName, kPluginName);
  p_Desc.setPluginGrouping(kPluginGrouping);
  p_Desc.setPluginDescription(kPluginDescription);
  p_Desc.addSupportedContext(OFX::eContextFilter);
  p_Desc.addSupportedContext(OFX::eContextGeneral);
  p_Desc.addSupportedBitDepth(OFX::eBitDepthFloat);
  p_Desc.setSingleInstance(false);
  p_Desc.setHostFrameThreading(false);
  p_Desc.setSupportsMultiResolution(kSupportsMultiResolution);
  p_Desc.setSupportsTiles(kSupportsTiles);
  p_Desc.setTemporalClipAccess(false);
  p_Desc.setRenderTwiceAlways(false);
  p_Desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
}

void CinematicPluginFactory::describeInContext(
    OFX::ImageEffectDescriptor &p_Desc, OFX::ContextEnum p_Context) {
  OFX::ClipDescriptor *srcClip =
      p_Desc.defineClip(kOfxImageEffectSimpleSourceClipName);
  srcClip->addSupportedComponent(OFX::ePixelComponentRGBA);
  srcClip->setTemporalClipAccess(false);
  srcClip->setSupportsTiles(kSupportsTiles);

  OFX::ClipDescriptor *dstClip =
      p_Desc.defineClip(kOfxImageEffectOutputClipName);
  dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
  dstClip->setSupportsTiles(kSupportsTiles);

  OFX::PageParamDescriptor *page = p_Desc.definePageParam("Controls");

  // 1. CIT
  {
    OFX::GroupParamDescriptor *group = p_Desc.defineGroupParam("GroupCIT");
    group->setLabels("Color Ingest", "Color Ingest", "CIT");
    page->addChild(*group);
    auto *p = p_Desc.defineBooleanParam("EnableCIT");
    p->setDefault(true);
    p->setParent(*group);
    page->addChild(*p);
    auto *d = p_Desc.defineDoubleParam("CITExposure");
    d->setLabels("Exposure Trim", "Exposure", "Exp");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(-2.0, 2.0);
    d->setDisplayRange(-2.0, 2.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("CITChromaCeiling");
    d->setLabels("Chroma Ceiling", "Ceiling", "Ceil");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("CITWhiteBias");
    d->setLabels("White Bias", "White Bias", "Bias");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(-1.0, 1.0);
    d->setDisplayRange(-1.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("CITTemperature");
    d->setLabels("Temperature", "Temp", "Temp");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(-1.0, 1.0);
    d->setDisplayRange(-1.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("CITTint");
    d->setLabels("Tint", "Tint", "Tint");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(-1.0, 1.0);
    d->setDisplayRange(-1.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("CITGlobalSat");
    d->setLabels("Global Saturation", "Global Sat", "GSat");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 2.0);
    d->setDisplayRange(0.0, 2.0);
    d->setDefault(1.0);
    d->setParent(*group);
    page->addChild(*d);
  }

  // 2. PCR
  {
    OFX::GroupParamDescriptor *group = p_Desc.defineGroupParam("GroupPCR");
    group->setLabels("Film Response", "Film Response", "PCR");
    page->addChild(*group);
    auto *p = p_Desc.defineBooleanParam("EnablePCR");
    p->setDefault(true);
    p->setParent(*group);
    page->addChild(*p);
    auto *d = p_Desc.defineDoubleParam("PCRAmount");
    d->setLabels("Amount", "Amount", "Amt");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("PCRShadowCoolBias");
    d->setLabels("Shadow Cool Bias", "Shad Cool", "SCool");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("PCRMidtoneColorFocus");
    d->setLabels("Midtone Color Focus", "Mid Focus", "Mid");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("PCRHighlightWarmth");
    d->setLabels("Highlight Warmth", "High Warmth", "HWarm");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("PCRHighlightCompression");
    d->setLabels("Highlight Compression", "High Comp", "HComp");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    auto *c = p_Desc.defineChoiceParam("PCRPreset");
    c->setLabels("Film Stock", "Stock", "Stock");
    c->appendOption("None");
    c->appendOption("Vision3 500T");
    c->appendOption("Eterna");
    c->appendOption("Portra");
    c->appendOption("Ektachrome");
    c->appendOption("Cross Process");
    c->setParent(*group);
    page->addChild(*c);
    auto *b = p_Desc.defineBooleanParam("PCRCrossProcess");
    b->setLabels("Cross Process", "Cross", "XProc");
    b->setParent(*group);
    page->addChild(*b);
  }

  // 3. Tonal
  {
    OFX::GroupParamDescriptor *group = p_Desc.defineGroupParam("GroupTonal");
    group->setLabels("Tonal Engine", "Tonal Engine", "Tonal");
    page->addChild(*group);
    auto *p = p_Desc.defineBooleanParam("EnableTonal");
    p->setDefault(true);
    p->setParent(*group);
    page->addChild(*p);
    auto *d = p_Desc.defineDoubleParam("TonalContrast");
    d->setLabels("Contrast", "Contrast", "Con");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 2.0);
    d->setDisplayRange(0.0, 2.0);
    d->setDefault(1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("TonalPivot");
    d->setLabels("Pivot", "Pivot", "Piv");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.18);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("TonalStrength");
    d->setLabels("Strength", "Strength", "Str");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("TonalBlackFloor");
    d->setLabels("Black Floor", "Blk Floor", "Blk");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 0.1);
    d->setDisplayRange(0.0, 0.1);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("TonalHighContrast");
    d->setLabels("Highlight Contrast", "High Con", "HCon");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 2.0);
    d->setDisplayRange(0.0, 2.0);
    d->setDefault(1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("TonalSoftClip");
    d->setLabels("Soft Clip", "Soft Clip", "SClip");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
  }

  // 4. Energy
  {
    OFX::GroupParamDescriptor *group = p_Desc.defineGroupParam("GroupEnergy");
    group->setLabels("Color Energy", "Color Energy", "Energy");
    page->addChild(*group);
    auto *p = p_Desc.defineBooleanParam("EnableEnergy");
    p->setParent(*group);
    page->addChild(*p);
    auto *d = p_Desc.defineDoubleParam("EnergyDensity");
    d->setLabels("Density", "Density", "Dens");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 2.0);
    d->setDisplayRange(0.0, 2.0);
    d->setDefault(1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("EnergySeparation");
    d->setLabels("Separation", "Separation", "Sep");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("EnergyHighRollOff");
    d->setLabels("Highlight Rolloff", "High Roll", "HRoll");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("EnergyShadowBias");
    d->setLabels("Shadow Bias", "Shad Bias", "SBias");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("EnergyVibrance");
    d->setLabels("Vibrance", "Vibrance", "Vib");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 2.0);
    d->setDisplayRange(0.0, 2.0);
    d->setDefault(1.0);
    d->setParent(*group);
    page->addChild(*d);
  }

  // 5. HLP
  {
    OFX::GroupParamDescriptor *group = p_Desc.defineGroupParam("GroupHLP");
    group->setLabels("Highlight Protection", "Highlight Protection", "HLP");
    page->addChild(*group);
    auto *p = p_Desc.defineBooleanParam("EnableHLP");
    p->setParent(*group);
    page->addChild(*p);
    auto *d = p_Desc.defineDoubleParam("HLPThreshold");
    d->setLabels("Threshold", "Threshold", "Thr");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 2.0);
    d->setDisplayRange(0.0, 2.0);
    d->setDefault(1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("HLPRolloff");
    d->setLabels("Rolloff", "Rolloff", "Roll");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    p = p_Desc.defineBooleanParam("HLPPreserveColor");
    p->setLabels("Preserve Color", "Preserve Color", "Col");
    p->setParent(*group);
    page->addChild(*p);
  }

  // 6. Split
  {
    OFX::GroupParamDescriptor *group = p_Desc.defineGroupParam("GroupSplit");
    group->setLabels("Split Toning", "Split Toning", "Split");
    page->addChild(*group);
    auto *p = p_Desc.defineBooleanParam("EnableSplit");
    p->setParent(*group);
    page->addChild(*p);
    auto *d = p_Desc.defineDoubleParam("SplitStrength");
    d->setLabels("Strength", "Strength", "Str");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("SplitShadowHue");
    d->setLabels("Shadow Hue", "Shad Hue", "SHue");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 360.0);
    d->setDisplayRange(0.0, 360.0);
    d->setDoubleType(OFX::eDoubleTypeAngle);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("SplitHighlightHue");
    d->setLabels("Highlight Hue", "High Hue", "HHue");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 360.0);
    d->setDisplayRange(0.0, 360.0);
    d->setDoubleType(OFX::eDoubleTypeAngle);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("SplitBalance");
    d->setLabels("Balance", "Balance", "Bal");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(-1.0, 1.0);
    d->setDisplayRange(-1.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("SplitMidtoneHue");
    d->setLabels("Midtone Hue", "Mid Hue", "MHue");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 360.0);
    d->setDisplayRange(0.0, 360.0);
    d->setDoubleType(OFX::eDoubleTypeAngle);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("SplitMidtoneSat");
    d->setLabels("Midtone Saturation", "Mid Sat", "MSat");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
  }

  // 7. Grain
  {
    OFX::GroupParamDescriptor *group = p_Desc.defineGroupParam("GroupGrain");
    group->setLabels("Film Grain", "Film Grain", "Grain");
    page->addChild(*group);
    auto *p = p_Desc.defineBooleanParam("EnableGrain");
    p->setParent(*group);
    page->addChild(*p);

    auto *c = p_Desc.defineChoiceParam("GrainType");
    c->appendOption("Custom");
    c->appendOption("8mm");
    c->appendOption("16mm");
    c->appendOption("Super 16");
    c->appendOption("35mm");
    c->appendOption("65mm");
    c->appendOption("Clean");
    c->setParent(*group);
    page->addChild(*c);

    auto *d = p_Desc.defineDoubleParam("GrainAmount");
    d->setLabels("Amount", "Amount", "Amt");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("GrainSize");
    d->setLabels("Size", "Size", "Size");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("GrainShadowWeight");
    d->setLabels("Shadow Weight", "Shad W.", "SW");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("GrainMidWeight");
    d->setLabels("Mid Weight", "Mid W.", "MW");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("GrainHighlightWeight");
    d->setLabels("High Weight", "High W.", "HW");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    p = p_Desc.defineBooleanParam("GrainChromatic");
    p->setLabels("Chromatic Grain", "Chromatic", "Chrom");
    p->setParent(*group);
    page->addChild(*p);
    d = p_Desc.defineDoubleParam("GrainTemporalSpeed");
    d->setLabels("Temporal Speed", "Temp Speed", "TSpd");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
  }

  // 8. Dither
  {
    OFX::GroupParamDescriptor *group = p_Desc.defineGroupParam("GroupDither");
    group->setLabels("Dither", "Dither", "Dither");
    page->addChild(*group);
    auto *p = p_Desc.defineBooleanParam("EnableDither");
    p->setLabels("Enable Dither", "Enable Dither", "Dither");
    p->setParent(*group);
    page->addChild(*p);
    auto *d = p_Desc.defineDoubleParam("DitherAmount");
    d->setLabels("Dither Amount", "Dither Amt", "DAmt");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
  }

  // 8. Spatial Group (Mist, Blur, Glow, Sharp, Halo, Vignette)
  {
    OFX::GroupParamDescriptor *group = p_Desc.defineGroupParam("GroupSpatial");
    group->setLabels("Spatial Effects", "Spatial Effects", "Spatial");
    page->addChild(*group);

    // Subgroups or just flat?
    // Let's use Prefixes to organize
    // Mist
    auto *p = p_Desc.defineBooleanParam("EnableMist");
    p->setLabels("Enable Mist", "Enable Mist", "Mist");
    p->setParent(*group);
    page->addChild(*p);
    auto *d = p_Desc.defineDoubleParam("MistAmount");
    d->setLabels("Mist Amount", "Mist Amt", "MAmt");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("MistThreshold");
    d->setLabels("Mist Threshold", "Mist Thr", "MThr");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 2.0);
    d->setDisplayRange(0.0, 2.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("MistSoftness");
    d->setLabels("Mist Softness", "Mist Soft", "MSoft");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("MistDepthBias");
    d->setLabels("Mist Depth Bias", "Mist Bias", "MBias");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(-1.0, 1.0);
    d->setDisplayRange(-1.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("MistWarmth");
    d->setLabels("Mist Warmth", "Mist Warm", "MWarm");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(-1.0, 1.0);
    d->setDisplayRange(-1.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);

    // Blur
    p = p_Desc.defineBooleanParam("EnableBlur");
    p->setLabels("Enable Blur", "Enable Blur", "Blur");
    p->setParent(*group);
    page->addChild(*p);
    d = p_Desc.defineDoubleParam("BlurRadius");
    d->setLabels("Blur Radius", "Blur Rad", "BRad");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 50.0);
    d->setDisplayRange(0.0, 50.0);
    d->setDefault(4.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("BlurStrength");
    d->setLabels("Blur Strength", "Blur Str", "BStr");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("BlurShadowAmt");
    d->setLabels("Blur Shadow Amt", "Blur Shad", "BShad");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.3);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("BlurHighlightAmt");
    d->setLabels("Blur High Amt", "Blur High", "BHigh");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.8);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("BlurTonalSoft");
    d->setLabels("Blur Tonal Soft", "Blur Soft", "BSoft");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("BlurSat");
    d->setLabels("Blur Saturation", "Blur Sat", "BSat");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 2.0);
    d->setDisplayRange(0.0, 2.0);
    d->setDefault(1.0);
    d->setParent(*group);
    page->addChild(*d);

    // Glow
    p = p_Desc.defineBooleanParam("EnableGlow");
    p->setLabels("Enable Glow", "Enable Glow", "Glow");
    p->setParent(*group);
    page->addChild(*p);
    d = p_Desc.defineDoubleParam("GlowAmount");
    d->setLabels("Glow Amount", "Glow Amt", "GAmt");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("GlowThreshold");
    d->setLabels("Glow Threshold", "Glow Thr", "GThr");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 2.0);
    d->setDisplayRange(0.0, 2.0);
    d->setDefault(0.8);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("GlowKnee");
    d->setLabels("Glow Knee", "Glow Knee", "GKnee");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("GlowRadius");
    d->setLabels("Glow Radius", "Glow Rad", "GRad");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 100.0);
    d->setDisplayRange(0.0, 100.0);
    d->setDefault(10.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("GlowFidelity");
    d->setLabels("Glow Fidelity", "Glow Fid", "GFid");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("GlowWarmth");
    d->setLabels("Glow Warmth", "Glow Warm", "GWarm");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(-1.0, 1.0);
    d->setDisplayRange(-1.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);

    // Sharpening
    p = p_Desc.defineBooleanParam("EnableSharp");
    p->setLabels("Enable Sharpening", "Enable Sharp", "Sharp");
    p->setParent(*group);
    page->addChild(*p);
    auto *c = p_Desc.defineChoiceParam("SharpType");
    c->appendOption("Soft");
    c->appendOption("Micro");
    c->appendOption("Edge");
    c->appendOption("Deconv");
    c->setParent(*group);
    page->addChild(*c);
    d = p_Desc.defineDoubleParam("SharpAmount");
    d->setLabels("Sharp Amount", "Sharp Amt", "SAmt");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("SharpRadius");
    d->setLabels("Sharp Radius", "Sharp Rad", "SRad");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 10.0);
    d->setDisplayRange(0.0, 10.0);
    d->setDefault(1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("SharpDetail");
    d->setLabels("Sharp Detail", "Sharp Det", "SDet");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("SharpEdgeProt");
    d->setLabels("Sharp Edge Prot", "Edge Prot", "SEdge");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("SharpNoiseSupp");
    d->setLabels("Sharp Noise Supp", "Noise Supp", "SNoise");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("SharpShadowProt");
    d->setLabels("Sharp Shad Prot", "Shad Prot", "SShad");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("SharpHighProt");
    d->setLabels("Sharp High Prot", "High Prot", "SHigh");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);

    // Halation
    p = p_Desc.defineBooleanParam("EnableHalo");
    p->setLabels("Enable Halation", "Enable Halo", "Halo");
    p->setParent(*group);
    page->addChild(*p);
    d = p_Desc.defineDoubleParam("HaloAmount");
    d->setLabels("Halo Amount", "Halo Amt", "HAmt");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("HaloThreshold");
    d->setLabels("Halo Threshold", "Halo Thr", "HThr");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 2.0);
    d->setDisplayRange(0.0, 2.0);
    d->setDefault(0.8);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("HaloKnee");
    d->setLabels("Halo Knee", "Halo Knee", "HKnee");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("HaloWarmth");
    d->setLabels("Halo Warmth", "Halo Warm", "HWarm");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("HaloRadius");
    d->setLabels("Halo Radius", "Halo Rad", "HRad");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 100.0);
    d->setDisplayRange(0.0, 100.0);
    d->setDefault(10.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("HaloSat");
    d->setLabels("Halo Saturation", "Halo Sat", "HSat");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 2.0);
    d->setDisplayRange(0.0, 2.0);
    d->setDefault(1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("HaloHueShift");
    d->setLabels("Halo Hue Shift", "Halo Hue", "HHue");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(-180.0, 180.0);
    d->setDisplayRange(-180.0, 180.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);

    // Vignette
    p = p_Desc.defineBooleanParam("EnableVignette");
    p->setLabels("Enable Vignette", "Enable Vig", "Vig");
    p->setParent(*group);
    page->addChild(*p);
    c = p_Desc.defineChoiceParam("VignetteType");
    c->appendOption("Dark");
    c->appendOption("Light");
    c->appendOption("Defocus");
    c->setParent(*group);
    page->addChild(*c);
    d = p_Desc.defineDoubleParam("VignetteAmount");
    d->setLabels("Vig Amount", "Vig Amt", "VAmt");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);
    p = p_Desc.defineBooleanParam("VignetteInvert");
    p->setLabels("Vig Invert", "Vig Inv", "VInv");
    p->setParent(*group);
    page->addChild(*p);
    d = p_Desc.defineDoubleParam("VignetteSize");
    d->setLabels("Vig Size", "Vig Size", "VSize");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("VignetteRoundness");
    d->setLabels("Vig Roundness", "Vig Rnd", "VRnd");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("VignetteSoftness");
    d->setLabels("Vig Softness", "Vig Soft", "VSoft");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("VignetteDefocus");
    d->setLabels("Vig Defocus", "Vig Def", "VDef");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("VignetteDefocusSoft");
    d->setLabels("Vig Def Soft", "Vig DSoft", "VDSoft");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("VignetteCenterX");
    d->setLabels("Vig Center X", "Vig CX", "VCX");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(-1.0, 1.0);
    d->setDisplayRange(-1.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("VignetteCenterY");
    d->setLabels("Vig Center Y", "Vig CY", "VCY");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(-1.0, 1.0);
    d->setDisplayRange(-1.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("VignetteTintR");
    d->setLabels("Vig Tint R", "Vig R", "VR");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("VignetteTintG");
    d->setLabels("Vig Tint G", "Vig G", "VG");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("VignetteTintB");
    d->setLabels("Vig Tint B", "Vig B", "VB");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);

    // Anamorphic Streak
    p = p_Desc.defineBooleanParam("EnableStreak");
    p->setLabels("Enable Streak", "Enable Streak", "Streak");
    p->setParent(*group);
    page->addChild(*p);
    d = p_Desc.defineDoubleParam("StreakAmount");
    d->setLabels("Streak Amount", "Streak Amt", "SkAmt");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("StreakThreshold");
    d->setLabels("Streak Threshold", "Streak Thr", "SkThr");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 2.0);
    d->setDisplayRange(0.0, 2.0);
    d->setDefault(0.8);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("StreakLength");
    d->setLabels("Streak Length", "Streak Len", "SkLen");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.5);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("StreakTint");
    d->setLabels("Streak Tint", "Streak Tint", "SkTint");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);

    // Chromatic Aberration
    p = p_Desc.defineBooleanParam("EnableCA");
    p->setLabels("Enable CA", "Enable CA", "CA");
    p->setParent(*group);
    page->addChild(*p);
    d = p_Desc.defineDoubleParam("CAAmount");
    d->setLabels("CA Amount", "CA Amt", "CAAmt");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(0.0, 1.0);
    d->setDisplayRange(0.0, 1.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("CACenterX");
    d->setLabels("CA Center X", "CA CX", "CACX");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(-1.0, 1.0);
    d->setDisplayRange(-1.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
    d = p_Desc.defineDoubleParam("CACenterY");
    d->setLabels("CA Center Y", "CA CY", "CACY");
    d->setDigits(3);
    d->setIncrement(0.001);
    d->setRange(-1.0, 1.0);
    d->setDisplayRange(-1.0, 1.0);
    d->setDefault(0.0);
    d->setParent(*group);
    page->addChild(*d);
  }
}

OFX::ImageEffect *
CinematicPluginFactory::createInstance(OfxImageEffectHandle p_Handle,
                                       OFX::ContextEnum /*p_Context*/) {
  return new CinematicPlugin(p_Handle);
}

void OFX::Plugin::getPluginIDs(OFX::PluginFactoryArray &p_FactoryArray) {
  static CinematicPluginFactory plugin;
  p_FactoryArray.push_back(&plugin);
}
