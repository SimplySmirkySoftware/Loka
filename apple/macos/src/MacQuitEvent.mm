#include "MacQuitEvent.hpp"
#include <Foundation/Foundation.h>
#include <ApplicationServices/ApplicationServices.h>

MacQuitEventReply::~MacQuitEventReply()
{
  this->decline();
}

bool MacQuitEventReply::hold()
{
  // A second hold would orphan the first suspended event.
  if (this->isHeld())
    return false;
  this->suspension_ = [[NSAppleEventManager sharedAppleEventManager] suspendCurrentAppleEvent];
  return this->isHeld();
}

void MacQuitEventReply::accept()
{
  const NSAppleEventManagerSuspensionID suspension = this->suspension_;
  this->suspension_ = 0;
  if (suspension)
    [[NSAppleEventManager sharedAppleEventManager] resumeWithSuspensionID:suspension];
}

void MacQuitEventReply::decline()
{
  const NSAppleEventManagerSuspensionID suspension = this->suspension_;
  this->suspension_ = 0;
  if (!suspension)
    return;
  NSAppleEventManager *manager = [NSAppleEventManager sharedAppleEventManager];
  [[manager replyAppleEventForSuspensionID:suspension]
      setParamDescriptor:[NSAppleEventDescriptor descriptorWithInt32:userCanceledErr]
              forKeyword:keyErrorNumber];
  [manager resumeWithSuspensionID:suspension];
}
