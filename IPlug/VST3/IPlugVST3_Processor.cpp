/*
 ==============================================================================
 
 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers. 
 
 See LICENSE.txt for  more info.
 
 ==============================================================================
*/

#include "IPlugVST3_Processor.h"

#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

IPlugVST3Processor::IPlugVST3Processor(IPlugInstanceInfo instanceInfo, IPlugConfig c)
: IPlugAPIBase(c, kAPIVST3)
, IPlugVST3_ProcessorBase(c)
{
  setControllerClass(instanceInfo.mOtherGUID);
  CreateTimer();
}

IPlugVST3Processor::~IPlugVST3Processor() {}

#pragma mark AudioEffect overrides

tresult PLUGIN_API IPlugVST3Processor::initialize(FUnknown* context)
{
  TRACE;
  
  tresult result = AudioEffect::initialize(context);
  
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
  }
  
  return result;
}

//tresult PLUGIN_API IPlugVST3Processor::terminate()
//{
//  return AudioEffect::terminate();
//}

tresult PLUGIN_API IPlugVST3Processor::setBusArrangements(SpeakerArrangement* pInputBusArrangements, int32 numInBuses, SpeakerArrangement* pOutputBusArrangements, int32 numOutBuses)
{
  TRACE;
  
  SetBusArrangments(pInputBusArrangements, numInBuses, pOutputBusArrangements, numOutBuses);
  
  return kResultTrue;
}

tresult PLUGIN_API IPlugVST3Processor::setActive(TBool state)
{
  OnActivate((bool) state);
  return AudioEffect::setActive(state);
}

tresult PLUGIN_API IPlugVST3Processor::setupProcessing(ProcessSetup& newSetup)
{
  TRACE;
  
  if (SetupProcessing(newSetup))
  {
    processSetup = newSetup;
    return kResultOk;
  }
    
  return kResultFalse;
}

tresult PLUGIN_API IPlugVST3Processor::process(ProcessData& data)
{
  TRACE;
  
  Process(this, data, processSetup, audioInputs, audioOutputs, mMidiMsgsFromEditor, mMidiMsgsFromProcessor, mSysExDataFromEditor, mSysexBuf);
    
  return kResultOk;
}

tresult PLUGIN_API IPlugVST3Processor::canProcessSampleSize(int32 symbolicSampleSize)
{
  return CanProcessSampleSize(symbolicSampleSize) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API IPlugVST3Processor::setState(IBStream* state)
{
  TRACE;
  
  return IPlugVST3State::SetState(this, state)  ? kResultOk :kResultFalse;
}

tresult PLUGIN_API IPlugVST3Processor::getState(IBStream* state)
{
  TRACE;
    
  return IPlugVST3State::GetState(this, state) ? kResultOk :kResultFalse;
}

void IPlugVST3Processor::SendControlValueFromDelegate(int controlTag, double normalizedValue)
{
  OPtr<IMessage> message = allocateMessage();
  
  if (!message)
    return;
  
  message->setMessageID("SCVFD");
  message->getAttributes()->setInt("CT", controlTag);
  message->getAttributes()->setFloat("NV", normalizedValue);
  
  sendMessage(message);
}

void IPlugVST3Processor::SendControlMsgFromDelegate(int controlTag, int messageTag, int dataSize, const void* pData)
{
  OPtr<IMessage> message = allocateMessage();
  
  if (!message)
    return;
  
  message->setMessageID("SCMFD");
  message->getAttributes()->setInt("CT", controlTag);
  message->getAttributes()->setInt("MT", messageTag);
  message->getAttributes()->setBinary("D", pData, dataSize);
  
  sendMessage(message);
}

void IPlugVST3Processor::SendArbitraryMsgFromDelegate(int messageTag, int dataSize, const void* pData)
{
  OPtr<IMessage> message = allocateMessage();
  
  if (!message)
    return;
  
  if(dataSize == 0) // allow sending messages with no data
  {
    dataSize = 1;
    uint8_t dummy = 0;
    pData = &dummy;
  }
  
  message->setMessageID("SAMFD");
  message->getAttributes()->setInt("MT", messageTag);
  message->getAttributes()->setBinary("D", pData, dataSize);
  sendMessage(message);
}

tresult PLUGIN_API IPlugVST3Processor::notify(IMessage* message)
{
  if (!message)
    return kInvalidArgument;
  
  const void* data = nullptr;
  uint32 size;
  
  if (!strcmp (message->getMessageID(), "SMMFUI")) // midi message from UI
  {
    if (message->getAttributes()->getBinary("D", data, size) == kResultOk)
    {
      if (size == sizeof(IMidiMsg))
      {
        IMidiMsg msg;
        memcpy(&msg, data, size);
        mMidiMsgsFromEditor.Push(msg);
        return kResultOk;
      }
      
      return kResultFalse;
    }
  }
  else if (!strcmp (message->getMessageID(), "SAMFUI")) // message from UI
  {
    int64 messageTag;
    int64 controlTag;

    if (message->getAttributes()->getInt("MT", messageTag) == kResultOk && message->getAttributes()->getInt("CT", controlTag) == kResultOk)
    {
      if (message->getAttributes()->getBinary("D", data, size) == kResultOk)
      {
        if(OnMessage((int) messageTag, (int) controlTag, size, data))
        {
          return kResultOk;
        }
      }
      
      return kResultFalse;
    }
  }
  
  return AudioEffect::notify(message);
}

void IPlugVST3Processor::TransmitMidiMsgFromProcessor(const IMidiMsg& msg)
{
  OPtr<IMessage> message = allocateMessage();
  
  if (!message)
    return;
  
  message->setMessageID("SMMFD");
  message->getAttributes()->setBinary("D", (void*) &msg, sizeof(IMidiMsg));
  sendMessage(message);
}

void IPlugVST3Processor::TransmitSysExDataFromProcessor(const SysExData& data)
{
  OPtr<IMessage> message = allocateMessage();
  
  if (!message)
    return;
  
  message->setMessageID("SSMFD");
  message->getAttributes()->setBinary("D", (void*) data.mData, data.mSize);
  message->getAttributes()->setInt("O", data.mOffset);
  sendMessage(message);
}
