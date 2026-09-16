#pragma once

#include <cmath>

namespace Oot3dNativeGame {
// All inputs describe the same completed-frame interval. Submission contains
// plan construction and nested guest resumes; neither may be counted twice.
struct PreBackendBudget {
  double Guest = 0;
  double Submission = 0;
  double GuestInsideSubmission = 0;
  double Plan = 0;
  double Host = 0;
  double Input = 0;
  double Dsp = 0;
  double AudioOutput = 0;

  double QueueAndCapture() const {
    return Submission - GuestInsideSubmission - Plan;
  }
  double Total() const {
    return Guest + Submission - GuestInsideSubmission + Host + Input + Dsp + AudioOutput;
  }
  bool Valid() const {
    return std::isfinite(Total()) && Guest >= 0 && Plan >= 0 && Host >= 0 &&
           Input >= 0 && Dsp >= 0 && AudioOutput >= 0 && GuestInsideSubmission >= 0 &&
           GuestInsideSubmission <= Guest && QueueAndCapture() >= -1e-9;
  }
};
}
