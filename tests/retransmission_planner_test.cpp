#include "retransmission_planner.hpp"
#include <cassert>

int main() {
  RetransmissionPlanner planner;
  const RetryPolicy policy{100, 50, 20, 1000};
  planner.noteMissing(65535, 1000);
  planner.noteMissing(0, 1000);
  planner.noteMissing(2, 1000);
  assert(planner.due(1099, policy, {65535, 3}).empty());
  auto first = planner.due(1100, policy, {65535, 3});
  assert(first.size() == 2 && first[0].first == 65535 && first[0].count == 2);
  assert(first[1].first == 2 && first[1].count == 1);
  assert(planner.due(1149, policy, {65535, 3}).empty());
  auto repeat = planner.due(1150, policy, {65535, 3});
  assert(repeat.size() == 2);
  planner.resolve(0);
  auto lastChance = planner.due(1980, policy, {65535, 3});
  assert(lastChance.size() == 2 && lastChance[0].count == 1);
  assert(planner.due(1981, policy, {65535, 3}).empty());
  planner.reset();
  assert(planner.due(2000, policy, {65535, 3}).empty());
}
