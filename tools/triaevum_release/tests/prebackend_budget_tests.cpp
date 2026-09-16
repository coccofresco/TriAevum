#include "../../oot3d/native_game_runtime/oot3d_prebackend_budget.h"
#include <cmath>
#include <iostream>

int main() {
  using Oot3dNativeGame::PreBackendBudget;
  PreBackendBudget budget{6, 2, .5, 1, .1, .02, .3, .03};
  if (!budget.Valid() || std::abs(budget.Total()-7.95)>1e-9 ||
      std::abs(budget.QueueAndCapture()-.5)>1e-9) return 1;
  budget.GuestInsideSubmission=3;
  if (budget.Valid()) return 2;
  budget={};
  if (!budget.Valid() || budget.Total()!=0) return 3;
  std::cout << "Pre-backend budget: overlap, disjoint sum and invalid intervals passed\n";
}
