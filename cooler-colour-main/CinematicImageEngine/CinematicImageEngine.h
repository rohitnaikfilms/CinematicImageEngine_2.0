#pragma once

#include "ofxsImageEffect.h"
#include "ofxsProcessing.H"

// Per-pixel modules
#include "ColorEnergyEngine.h"
#include "ColorIngestTweaks.h"
#include "Dither.h"
#include "FilmGrain.h"
#include "FilmResponse.h"
#include "HighlightProtection.h"
#include "SplitToning.h"
#include "TonalEngine.h"

// Spatial modules
#include "AnamorphicStreak.h"
#include "ChromaticAberration.h"
#include "CinematicGlow.h"
#include "DreamyBlur.h"
#include "DreamyMist.h"
#include "Halation.h"
#include "Sharpening.h"
#include "Vignette.h"

class CinematicPluginFactory
    : public OFX::PluginFactoryHelper<CinematicPluginFactory> {
public:
  CinematicPluginFactory();
  virtual void load() {}
  virtual void unload() {}
  virtual void describe(OFX::ImageEffectDescriptor &p_Desc);
  virtual void describeInContext(OFX::ImageEffectDescriptor &p_Desc,
                                 OFX::ContextEnum p_Context);
  virtual OFX::ImageEffect *createInstance(OfxImageEffectHandle p_Handle,
                                           OFX::ContextEnum p_Context);
};

class CinematicPlugin : public OFX::ImageEffect {
public:
  explicit CinematicPlugin(OfxImageEffectHandle p_Handle);

  virtual void render(const OFX::RenderArguments &p_Args);
  virtual bool isIdentity(const OFX::IsIdentityArguments &p_Args,
                          OFX::Clip *&p_IdentityClip, double &p_IdentityTime);
  virtual void changedParam(const OFX::InstanceChangedArgs &p_Args,
                            const std::string &p_ParamName);
  virtual void
  getRegionsOfInterest(const OFX::RegionsOfInterestArguments &p_Args,
                       OFX::RegionOfInterestSetter &p_ROIS);

  // ==========================================
  // 1. Color Ingest Tweaks
  // ==========================================
  OFX::BooleanParam *m_EnableCIT;
  OFX::DoubleParam *m_CITExposure;
  OFX::DoubleParam *m_CITChromaCeiling;
  OFX::DoubleParam *m_CITWhiteBias;
  OFX::DoubleParam *m_CITTemperature;
  OFX::DoubleParam *m_CITTint;
  OFX::DoubleParam *m_CITGlobalSat;

  // ==========================================
  // 2. Film Response (PCR)
  // ==========================================
  OFX::BooleanParam *m_EnablePCR;
  OFX::DoubleParam *m_PCRAmount;
  OFX::DoubleParam *m_PCRShadowCoolBias;
  OFX::DoubleParam *m_PCRMidtoneColorFocus;
  OFX::DoubleParam *m_PCRHighlightWarmth;
  OFX::DoubleParam *m_PCRHighlightCompression;
  OFX::ChoiceParam *m_PCRPreset;
  OFX::BooleanParam *m_PCRCrossProcess;

  // ==========================================
  // 3. Tonal Engine
  // ==========================================
  OFX::BooleanParam *m_EnableTonal;
  OFX::DoubleParam *m_TonalContrast;
  OFX::DoubleParam *m_TonalPivot;
  OFX::DoubleParam *m_TonalStrength;
  OFX::DoubleParam *m_TonalBlackFloor;
  OFX::DoubleParam *m_TonalHighContrast;
  OFX::DoubleParam *m_TonalSoftClip;

  // ==========================================
  // 4. Color Energy Engine
  // ==========================================
  OFX::BooleanParam *m_EnableEnergy;
  OFX::DoubleParam *m_EnergyDensity;
  OFX::DoubleParam *m_EnergySeparation;
  OFX::DoubleParam *m_EnergyHighRollOff;
  OFX::DoubleParam *m_EnergyShadowBias;
  OFX::DoubleParam *m_EnergyVibrance;

  // ==========================================
  // 5. Highlight Protection
  // ==========================================
  OFX::BooleanParam *m_EnableHLP;
  OFX::DoubleParam *m_HLPThreshold;
  OFX::DoubleParam *m_HLPRolloff;
  OFX::BooleanParam *m_HLPPreserveColor;

  // ==========================================
  // 6. Split Toning
  // ==========================================
  OFX::BooleanParam *m_EnableSplit;
  OFX::DoubleParam *m_SplitStrength;
  OFX::DoubleParam *m_SplitShadowHue;
  OFX::DoubleParam *m_SplitHighlightHue;
  OFX::DoubleParam *m_SplitBalance;
  OFX::DoubleParam *m_SplitMidtoneHue;
  OFX::DoubleParam *m_SplitMidtoneSat;

  // ==========================================
  // 7. Film Grain
  // ==========================================
  OFX::BooleanParam *m_EnableGrain;
  OFX::DoubleParam *m_GrainAmount;
  OFX::DoubleParam *m_GrainSize;
  OFX::DoubleParam *m_GrainShadowWeight;
  OFX::DoubleParam *m_GrainMidWeight;
  OFX::DoubleParam *m_GrainHighlightWeight;
  OFX::ChoiceParam *m_GrainType;
  OFX::BooleanParam *m_GrainChromatic;
  OFX::DoubleParam *m_GrainTemporalSpeed;

  // ==========================================
  // 8. Dither
  // ==========================================
  OFX::BooleanParam *m_EnableDither;
  OFX::DoubleParam *m_DitherAmount;

  // ==========================================
  // 9. Spatial — Mist
  // ==========================================
  OFX::BooleanParam *m_EnableMist;
  OFX::DoubleParam *m_MistAmount;
  OFX::DoubleParam *m_MistThreshold;
  OFX::DoubleParam *m_MistSoftness;
  OFX::DoubleParam *m_MistDepthBias;
  OFX::DoubleParam *m_MistWarmth;

  // ==========================================
  // 10. Spatial — Blur
  // ==========================================
  OFX::BooleanParam *m_EnableBlur;
  OFX::DoubleParam *m_BlurRadius;
  OFX::DoubleParam *m_BlurStrength;
  OFX::DoubleParam *m_BlurShadowAmt;
  OFX::DoubleParam *m_BlurHighlightAmt;
  OFX::DoubleParam *m_BlurTonalSoft;
  OFX::DoubleParam *m_BlurSat;

  // ==========================================
  // 11. Spatial — Glow
  // ==========================================
  OFX::BooleanParam *m_EnableGlow;
  OFX::DoubleParam *m_GlowAmount;
  OFX::DoubleParam *m_GlowThreshold;
  OFX::DoubleParam *m_GlowKnee;
  OFX::DoubleParam *m_GlowRadius;
  OFX::DoubleParam *m_GlowFidelity;
  OFX::DoubleParam *m_GlowWarmth;

  // ==========================================
  // 12. Spatial — Anamorphic Streak
  // ==========================================
  OFX::BooleanParam *m_EnableStreak;
  OFX::DoubleParam *m_StreakAmount;
  OFX::DoubleParam *m_StreakThreshold;
  OFX::DoubleParam *m_StreakLength;
  OFX::DoubleParam *m_StreakTint;

  // ==========================================
  // 13. Spatial — Sharpening
  // ==========================================
  OFX::BooleanParam *m_EnableSharp;
  OFX::ChoiceParam *m_SharpType;
  OFX::DoubleParam *m_SharpAmount;
  OFX::DoubleParam *m_SharpRadius;
  OFX::DoubleParam *m_SharpDetail;
  OFX::DoubleParam *m_SharpEdgeProt;
  OFX::DoubleParam *m_SharpNoiseSupp;
  OFX::DoubleParam *m_SharpShadowProt;
  OFX::DoubleParam *m_SharpHighProt;

  // ==========================================
  // 14. Spatial — Halation
  // ==========================================
  OFX::BooleanParam *m_EnableHalo;
  OFX::DoubleParam *m_HaloAmount;
  OFX::DoubleParam *m_HaloThreshold;
  OFX::DoubleParam *m_HaloKnee;
  OFX::DoubleParam *m_HaloWarmth;
  OFX::DoubleParam *m_HaloRadius;
  OFX::DoubleParam *m_HaloSat;
  OFX::DoubleParam *m_HaloHueShift;

  // ==========================================
  // 15. Spatial — Chromatic Aberration
  // ==========================================
  OFX::BooleanParam *m_EnableCA;
  OFX::DoubleParam *m_CAAmount;
  OFX::DoubleParam *m_CACenterX;
  OFX::DoubleParam *m_CACenterY;

  // ==========================================
  // 16. Spatial — Vignette
  // ==========================================
  OFX::BooleanParam *m_EnableVignette;
  OFX::ChoiceParam *m_VignetteType;
  OFX::DoubleParam *m_VignetteAmount;
  OFX::BooleanParam *m_VignetteInvert;
  OFX::DoubleParam *m_VignetteSize;
  OFX::DoubleParam *m_VignetteRoundness;
  OFX::DoubleParam *m_VignetteSoftness;
  OFX::DoubleParam *m_VignetteDefocus;
  OFX::DoubleParam *m_VignetteDefocusSoft;
  OFX::DoubleParam *m_VignetteCenterX;
  OFX::DoubleParam *m_VignetteCenterY;
  OFX::DoubleParam *m_VignetteTintR;
  OFX::DoubleParam *m_VignetteTintG;
  OFX::DoubleParam *m_VignetteTintB;

private:
  OFX::Clip *m_DstClip;
  OFX::Clip *m_SrcClip;
};
