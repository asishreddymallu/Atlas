#pragma once
#include "../circuit_scene.h"

namespace Atlas
{
class RVSSCircuitScene : public CircuitScene
{
    Q_OBJECT
  public:
    explicit RVSSCircuitScene(QObject *parent = nullptr) : CircuitScene(parent)
    {
        loadScene(":/circuit_designs/single_cycle_draft1.json");
    }
};
} // namespace Atlas
