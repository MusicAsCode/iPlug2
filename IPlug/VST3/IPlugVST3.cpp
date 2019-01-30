/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers. 
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#include <cstdio>

#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include "IPlugVST3.h"

using namespace Steinberg;
using namespace Vst;

#include "IPlugVST3_Parameter.h"

#pragma mark - IPlugVST3 Constructor

IPlugVST3::IPlugVST3(IPlugInstanceInfo instanceInfo, IPlugConfig c)
: IPlugAPIBase(c, kAPIVST3)
, IPlugVST3_ProcessorBase(c)
{
  CreateTimer();
}

IPlugVST3::~IPlugVST3() {}

#pragma mark AudioEffect overrides

tresult PLUGIN_API IPlugVST3::initialize(FUnknown* context)
{
  TRACE;

  tresult result = SingleComponentEffect::initialize(context);

  String128 tmpStringBuf;
  char hostNameCString[128];
  FUnknownPtr<IHostApplication>app(context);

  if ((GetHost() == kHostUninit) && app)
  {
    app->getName(tmpStringBuf);
    Steinberg::UString(tmpStringBuf, 128).toAscii(hostNameCString, 128);
    SetHost(hostNameCString, 0); // Can't get version in VST3
  }

  if (result == kResultOk)
  {
    Initialize(this);

    if(DoesMIDIIn())
      addEventInput(STR16("MIDI Input"), 1);
    
    if(DoesMIDIOut())
      addEventOutput(STR16("MIDI Output"), 1);

    if (NPresets())
    {
      parameters.addParameter(new IPlugVST3PresetParameter(NPresets()));
    }

    if(!IsInstrument())
    {
      parameters.addParameter(new IPlugVST3BypassParameter());
    }

    for (int i=0; i<NParams(); i++)
    {
      IParam *p = GetParam(i);

      UnitID unitID = kRootUnitId;

      const char* paramGroupName = p->GetGroupForHost();

      if (CStringHasContents(paramGroupName))
      {
        for(int j = 0; j < NParamGroups(); j++)
        {
          if(strcmp(paramGroupName, GetParamGroupName(j)) == 0)
          {
            unitID = j+1;
          }
        }

        if (unitID == kRootUnitId) // new unit, nothing found, so add it
        {
          unitID = AddParamGroup(paramGroupName);
        }
      }

      Parameter* pVST3Parameter = new IPlugVST3Parameter(p, i, unitID);
      parameters.addParameter(pVST3Parameter);
    }
  }

  OnHostIdentified();
  OnParamReset(kReset);

  return result;
}

tresult PLUGIN_API IPlugVST3::terminate()
{
  TRACE;

  mViews.empty();
  return SingleComponentEffect::terminate();
}

tresult PLUGIN_API IPlugVST3::setBusArrangements(SpeakerArrangement* pInputBusArrangements, int32 numInBuses, SpeakerArrangement* pOutputBusArrangements, int32 numOutBuses)
{
  TRACE;

  SetBusArrangments(pInputBusArrangements, numInBuses, pOutputBusArrangements, numOutBuses);
  
  return kResultTrue;
}

tresult PLUGIN_API IPlugVST3::setActive(TBool state)
{
  TRACE;

  OnActivate((bool) state);

  return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API IPlugVST3::setupProcessing(ProcessSetup& newSetup)
{
  TRACE;

  if (SetupProcessing(newSetup))
  {
    processSetup = newSetup;
    return kResultOk;
  }
      
  return kResultFalse;
}

tresult PLUGIN_API IPlugVST3::process(ProcessData& data)
{
  TRACE;

  Process(this, data, processSetup, audioInputs, audioOutputs, mMidiMsgsFromEditor, mMidiMsgsFromProcessor, mSysExDataFromEditor, mSysexBuf);

  return kResultOk;
}

//TODO: VST3 State needs work

tresult PLUGIN_API IPlugVST3::canProcessSampleSize(int32 symbolicSampleSize)
{
  return CanProcessSampleSize(symbolicSampleSize) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API IPlugVST3::setState(IBStream* state)
{
  TRACE;
  
  return IPlugVST3State::SetState(this, state)  ? kResultOk :kResultFalse;
}

tresult PLUGIN_API IPlugVST3::getState(IBStream* state)
{
  TRACE;
  
  return IPlugVST3State::GetState(this, state) ? kResultOk :kResultFalse;
  
}

#pragma mark IEditController overrides

IPlugView* PLUGIN_API IPlugVST3::createView(const char* name)
{
  if (name && strcmp(name, "editor") == 0)
  {
    return new ViewType(*this);
  }
  
  return 0;
}

tresult PLUGIN_API IPlugVST3::setEditorState(IBStream* state)
{
  // Nothing to do here
  return kResultOk;
}

tresult PLUGIN_API IPlugVST3::getEditorState(IBStream* state)
{
  // Nothing to do here
  return kResultOk;
}

tresult PLUGIN_API IPlugVST3::setComponentState(IBStream* state)
{
  // We get the state through setState so do nothing here
  return kResultOk;
}

void IPlugVST3::addDependentView(ViewType* view)
{
  mViews.push_back(view);
}

void IPlugVST3::removeDependentView(ViewType* view)
{
  mViews.erase(std::remove(mViews.begin(), mViews.end(), view));
}

#pragma mark IUnitInfo overrides

int32 PLUGIN_API IPlugVST3::getUnitCount()
{
  TRACE;

  return NParamGroups() + 1;
}

tresult PLUGIN_API IPlugVST3::getUnitInfo(int32 unitIndex, UnitInfo& info)
{
  TRACE;

  if (unitIndex == 0)
  {
    info.id = kRootUnitId;
    info.parentUnitId = kNoParentUnitId;
    UString name(info.name, 128);
    name.fromAscii("Root Unit");
#ifdef VST3_PRESET_LIST
    info.programListId = kPresetParam;
#else
    info.programListId = kNoProgramListId;
#endif
    return kResultTrue;
  }
  else if (unitIndex > 0 && NParamGroups())
  {
    info.id = unitIndex;
    info.parentUnitId = kRootUnitId;
    info.programListId = kNoProgramListId;

    UString name(info.name, 128);
    name.fromAscii(GetParamGroupName(unitIndex-1));

    return kResultTrue;
  }

  return kResultFalse;
}

int32 PLUGIN_API IPlugVST3::getProgramListCount()
{
#ifdef VST3_PRESET_LIST
  return (NPresets() > 0);
#else
  return 0;
#endif
}

tresult PLUGIN_API IPlugVST3::getProgramListInfo(int32 listIndex, ProgramListInfo& info /*out*/)
{
  if (listIndex == 0)
  {
    info.id = kPresetParam;
    info.programCount = (int32) NPresets();
    UString name(info.name, 128);
    name.fromAscii("Factory Presets");
    return kResultTrue;
  }
  return kResultFalse;
}

tresult PLUGIN_API IPlugVST3::getProgramName(ProgramListID listId, int32 programIndex, String128 name /*out*/)
{
  if (listId == kPresetParam)
  {
    Steinberg::UString(name, 128).fromAscii(GetPresetName(programIndex));
    return kResultTrue;
  }
  return kResultFalse;
}

#pragma mark IPlugAPIBase overrides

void IPlugVST3::BeginInformHostOfParamChange(int idx)
{
  Trace(TRACELOC, "%d", idx);
  beginEdit(idx);
}

void IPlugVST3::InformHostOfParamChange(int idx, double normalizedValue)
{
  Trace(TRACELOC, "%d:%f", idx, normalizedValue);
  performEdit(idx, normalizedValue);
}

void IPlugVST3::EndInformHostOfParamChange(int idx)
{
  Trace(TRACELOC, "%d", idx);
  endEdit(idx);
}

void IPlugVST3::InformHostOfParameterDetailsChange()
{
  FUnknownPtr<IComponentHandler>handler(componentHandler);
  handler->restartComponent(kParamTitlesChanged);
}

void IPlugVST3::EditorPropertiesChangedFromDelegate(int viewWidth, int viewHeight, const IByteChunk& data)
{
  if (HasUI() && (viewWidth != GetEditorWidth() || viewHeight != GetEditorHeight()))
  {
    mViews.at(0)->resize(viewWidth, viewHeight);
    IPlugAPIBase::EditorPropertiesChangedFromDelegate(viewWidth, viewHeight, data);
  }
}

void IPlugVST3::DirtyParametersFromUI()
{
  startGroupEdit();
  IPlugAPIBase::DirtyParametersFromUI();
  finishGroupEdit();
}

void IPlugVST3::SetLatency(int latency)
{
  IPlugProcessor::SetLatency(latency);

  FUnknownPtr<IComponentHandler>handler(componentHandler);
  handler->restartComponent(kLatencyChanged);
}
